"""Writes the Mabinogi wiki's data file from a game folder, as the page's own setup does (prepare.js),
so the page can be worked on from the command line:

    python Kanan/MabiWiki/make_data.py "C:\\path\\to\\Mabinogi" [OUTDIR]

OUTDIR (default: Kanan/MabiWiki/) gets data/game-data.js; open OUTDIR/index.html.

data/game-data.js is

    window.GAME_DATA = {"version": 1, "built": "...",
                           "source": {name: {"pack": ..., "version": ..., "size": ...}},
                           "files": {name: text}};

with name the file's path in the packs below data\\, lowercase, with "/" ("db/itemdb.xml"). The
text is the file as UTF-8, without its byte order mark.

Packs (package/*.pack, PACK v2): a 512-byte header, a 32-byte package info (entry count, index
size, blank size, data size), then the index. Each index entry is a length-coded name, then
seed, flags, offset, compressed size, size, compressed flag and five FILETIMEs. Entry data is
XORed with the low byte of an MT19937 stream seeded with (seed << 7) ^ 0xA9C36DE1, then
zlib-compressed. When packs hold the same file, the game keeps the copy with the highest seed
(its version); on a tie the first pack in name order.
"""
import json
import os
import random
import struct
import sys
import time
import zlib

# The files the wiki reads. Text tables (local/xml/<name>.<locale>.txt) are found by name: the
# first locale of TEXT_LOCALES the packs have (the game's English text is in the "japan" files).
DB_FILES = ['db/itemdb.xml', 'db/optionset.xml', 'db/title.xml', 'db/skill/skillinfo.xml',
            'db/skill/skillleveldescription.xml', 'db/race.xml', 'db/collectingform.xml',
            'db/itemupgradedb.xml', 'db/setitemdesc.xml', 'db/npcinfo.xml', 'db/charactercondition.xml',
            'db/pet/petracedesc.xml', 'db/pet/petagetable.xml', 'db/pet/petskilltable.xml',
            'db/pet/petcolorset.xml', 'db/pet/petrebirthtable.xml', 'db/vehicledesc.xml',
            'db/aircraftdesc.xml', 'db/birdpetgiftitem.xml',
            # Special upgrades: which weapon becomes which S and R weapon, and the bonuses' names.
            'db/itemenhancecommon.xml',
            # Spirit weapons: how much weaker each one gets as its mood drops.
            'db/egoweapon/egodesire.xml',
            # Crafting: Tailoring and Blacksmithing manuals, and the other production skills' recipes.
            'db/manualform.xml', 'db/production.xml', 'db/cookingaction.xml',
            # The regions' names (where NPCs with no town of their own are found).
            'db/minimapinfo.xml',
            # The pets' AI (the files db/pet/petracedesc.xml's AI_Normal and AI_Idle name, by their
            # <definition race>) and the commands file they import.
            'db/ai/local/aidescdata_pet_bird.xml', 'db/ai/local/aidescdata_pet_bird_idle.xml',
            'db/ai/local/aidescdata_pet_bordercollie.xml', 'db/ai/local/aidescdata_pet_cat.xml',
            'db/ai/local/aidescdata_pet_chicken.xml', 'db/ai/local/aidescdata_pet_cow.xml',
            'db/ai/local/aidescdata_pet_crab.xml', 'db/ai/local/aidescdata_pet_dog.xml',
            'db/ai/local/aidescdata_pet_dog_m.xml', 'db/ai/local/aidescdata_pet_dragon.xml',
            'db/ai/local/aidescdata_pet_elephant.xml', 'db/ai/local/aidescdata_pet_fox.xml',
            'db/ai/local/aidescdata_pet_griffin.xml', 'db/ai/local/aidescdata_pet_horse.xml',
            'db/ai/local/aidescdata_pet_idle.xml', 'db/ai/local/aidescdata_pet_idle_bdog.xml',
            'db/ai/local/aidescdata_pet_idle_minibear.xml', 'db/ai/local/aidescdata_pet_idle_snake.xml',
            'db/ai/local/aidescdata_pet_idle_spider.xml', 'db/ai/local/aidescdata_pet_imp.xml',
            'db/ai/local/aidescdata_pet_kangaroo_rat.xml', 'db/ai/local/aidescdata_pet_lazy_cat.xml',
            'db/ai/local/aidescdata_pet_lizard.xml', 'db/ai/local/aidescdata_pet_mimic.xml',
            'db/ai/local/aidescdata_pet_minibear.xml', 'db/ai/local/aidescdata_pet_minighost.xml',
            'db/ai/local/aidescdata_pet_miniglas.xml', 'db/ai/local/aidescdata_pet_miniskeleton.xml',
            'db/ai/local/aidescdata_pet_monkey.xml', 'db/ai/local/aidescdata_pet_music_bird.xml',
            'db/ai/local/aidescdata_pet_music_ostrich.xml', 'db/ai/local/aidescdata_pet_ostrich.xml',
            'db/ai/local/aidescdata_pet_owl.xml', 'db/ai/local/aidescdata_pet_paladin_dog.xml',
            'db/ai/local/aidescdata_pet_pig.xml', 'db/ai/local/aidescdata_pet_pinscher.xml',
            'db/ai/local/aidescdata_pet_pumpkin.xml', 'db/ai/local/aidescdata_pet_puppet.xml',
            'db/ai/local/aidescdata_pet_puppetwc.xml', 'db/ai/local/aidescdata_pet_ratman.xml',
            'db/ai/local/aidescdata_pet_riding_bird.xml', 'db/ai/local/aidescdata_pet_riding_broom.xml',
            'db/ai/local/aidescdata_pet_riding_tiger.xml', 'db/ai/local/aidescdata_pet_samoyed.xml',
            'db/ai/local/aidescdata_pet_seal.xml', 'db/ai/local/aidescdata_pet_sheep.xml',
            'db/ai/local/aidescdata_pet_siren.xml', 'db/ai/local/aidescdata_pet_snake.xml',
            'db/ai/local/aidescdata_pet_spider.xml', 'db/ai/local/aidescdata_pet_sprite.xml',
            'db/ai/local/aidescdata_pet_thunder_dog.xml', 'db/ai/local/aidescdata_pet_tiger.xml',
            'db/ai/local/aidescdata_pet_transform.xml', 'db/ai/local/aidescdata_pet_wolf.xml',
            'db/ai/local/common_pet.xml']
TEXT_TABLES = ['itemdb', 'optionset', 'title', 'skillinfo', 'skillleveldescription', 'race',
               'itemupgradedb', 'setitemdesc', 'npcinfo', 'charactercondition', 'petcolorset', 'itemenhancecommon', 'manualform', 'production', 'cookingaction', 'minimapinfo']
TEXT_LOCALES = ['english', 'usa', 'japan', 'korea']


def mt_bytes(seed, n):
    """The low bytes of the first n outputs of MT19937 seeded with init_genrand(seed)."""
    mt = [seed & 0xFFFFFFFF]
    for i in range(1, 624):
        mt.append((1812433253 * (mt[-1] ^ (mt[-1] >> 30)) + i) & 0xFFFFFFFF)
    r = random.Random()
    r.setstate((3, tuple(mt + [624]), None))  # Python's generator is MT19937: same outputs
    words = r.getrandbits(32 * n).to_bytes(4 * n, 'little') if n else b''
    return words[0::4]


def read_packs(folder):
    """{name: (pack, seed, offset, packed, size, compressed, data_start)} for the copy the game uses."""
    best = {}
    for pack in sorted((n for n in os.listdir(folder) if n.lower().endswith('.pack')), key=str.lower):
        path = os.path.join(folder, pack)
        with open(path, 'rb') as f:
            if f.read(512)[:4] != b'PACK':
                continue
            count, index_size, blank_size, _ = struct.unpack('<IIII', f.read(16))
            f.read(16)
            index = f.read(index_size)
        data_start = 512 + 32 + index_size + blank_size
        pos = 0
        for _ in range(count):
            length = index[pos]
            pos += 1
            if length < 4:
                size = 0x10 * (length + 1) - 1
            elif length == 4:
                size = 0x60 - 1
            else:
                size = struct.unpack_from('<I', index, pos)[0]
                pos += 4
            name = index[pos:pos + size].split(b'\0', 1)[0].decode('ascii', 'replace').lower().replace('\\', '/')
            pos += size
            seed, _flags, offset, packed, unpacked, compressed = struct.unpack_from('<IIIIII', index, pos)
            pos += 24 + 40
            if name not in best or seed > best[name][1]:
                best[name] = (path, seed, offset, packed, unpacked, compressed, data_start)
    return best


def read_entry(e):
    path, seed, offset, packed, _size, compressed, data_start = e
    with open(path, 'rb') as f:
        f.seek(data_start + offset)
        data = f.read(packed)
    key = mt_bytes((seed << 7) ^ 0xA9C36DE1, len(data))
    data = (int.from_bytes(data, 'little') ^ int.from_bytes(key, 'little')).to_bytes(len(data), 'little')
    return zlib.decompress(data) if compressed else data


def wanted(entries):
    names = list(DB_FILES)
    for table in TEXT_TABLES:
        for loc in TEXT_LOCALES:
            n = 'local/xml/%s.%s.txt' % (table, loc)
            if n in entries:
                names.append(n)
                break
    return [n for n in names if n in entries]


def text(raw):
    if raw[:3] == b'\xef\xbb\xbf':
        return raw[3:].decode('utf-8', 'replace')
    if raw[:2] in (b'\xff\xfe', b'\xfe\xff'):
        return raw.decode('utf-16', 'replace')
    return raw.decode('utf-8', 'replace')


SKILL_CELL = 32


def safe_name(s):
    """Whether a name from the game's data can be used in a file name as it is (MabiWiki.cpp's
    safeName): nothing that leaves the folder or names something else."""
    return bool(s) and '..' not in s and not any(c < ' ' or c in '/\\:*?"<>|' for c in s)


def dds_cell(raw, left, top, n):
    """An n x n RGBA block of an uncompressed DDS's top level at (left, top); None if it can't."""
    if raw[:4] != b'DDS ' or len(raw) < 128:
        return None
    flags, h, w, pitch = struct.unpack_from('<IIII', raw, 8)
    pf, = struct.unpack_from('<I', raw, 80)
    bits, = struct.unpack_from('<I', raw, 88)
    masks = list(struct.unpack_from('<IIII', raw, 92))
    if pf & 4 or not pf & 0x40 or bits % 8 or not 0 < bits <= 32:
        return None  # compressed: the skill atlases aren't
    if not pf & 1:
        masks[3] = 0
    bpp = bits // 8
    row = pitch if flags & 8 and pitch else w * bpp
    if left < 0 or top < 0 or left + n > w or top + n > h or len(raw) < 128 + row * (h - 1) + w * bpp:
        return None
    chans = []
    for m in masks:
        shift = (m & -m).bit_length() - 1 if m else 0
        width = bin(m).count('1')
        chans.append((m, shift, (1 << width) - 1))
    out = bytearray()
    for y in range(top, top + n):
        at = 128 + y * row + left * bpp
        for x in range(n):
            v = int.from_bytes(raw[at + x * bpp:at + (x + 1) * bpp], 'little')
            for m, shift, top_value in chans:
                out.append((((v & m) >> shift) * 255 + top_value // 2) // top_value if m else 255)
    return bytes(out)


def png(w, h, rgba):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def skill_icons(entries, out):
    """data/icons/skills/<id>.png and data/skill-icons.js, as MabiWiki.cpp makes them: each
    skill's 32x32 cell (skillinfo.xml PositionX, PositionY from 0) of its ImageFile atlas; an id's
    row with the highest Version, the first on a tie."""
    import re
    if 'db/skill/skillinfo.xml' not in entries:
        return 0
    cells = {}
    # (A '>' in a quoted value doesn't end the tag.)
    for attrs in re.findall(r'<Skill ((?:[^>"\']|"[^"]*"|\'[^\']*\')*?)/?>', text(read_entry(entries['db/skill/skillinfo.xml']))):
        row = dict(re.findall(r'([\w]+)\s*=\s*"([^"]*)"', attrs))
        sid, atlas = row.get('SkillID', ''), row.get('ImageFile', '').lower().replace('\\', '/')
        if not sid or not atlas or not safe_name(sid):
            continue
        atlas = atlas[5:] if atlas.startswith('data/') else atlas
        num = lambda k: int(row.get(k) or 0)
        if sid not in cells or num('Version') > cells[sid][3]:
            cells[sid] = (atlas, num('PositionX'), num('PositionY'), num('Version'))
    folder = os.path.join(out, 'data', 'icons', 'skills')
    os.makedirs(folder, exist_ok=True)
    for old in os.listdir(folder):
        if old.lower().endswith('.png'):
            os.remove(os.path.join(folder, old))
    atlases, icons = {}, {}
    for sid, (atlas, x, y, _) in sorted(cells.items()):
        if atlas not in atlases:
            atlases[atlas] = read_entry(entries[atlas]) if atlas in entries else b''
        px = dds_cell(atlases[atlas], x * SKILL_CELL, y * SKILL_CELL, SKILL_CELL)
        if px is None:
            print('skill %s has no icon (%s %d,%d)' % (sid, atlas, x, y))
            continue
        with open(os.path.join(folder, sid + '.png'), 'wb') as f:
            f.write(png(SKILL_CELL, SKILL_CELL, px))
        icons[sid] = sid + '.png'
    with open(os.path.join(out, 'data', 'skill-icons.js'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('window.GAME_SKILL_ICONS = ')
        json.dump(icons, f, separators=(',', ':'))
        f.write(';\n')
    return len(icons)


# ---------- NPC faces and portraits (MabiWiki.cpp: makeNpcImages has the layouts) ----------

NPC_ICON_CELL = 32
# {cell: the NPC whose face it is, or '' when it's nobody's}: faces the file gives NPCs without
# one of their own.
NPC_ICON_FILLERS = {'gfx/image/npcicon_0.dds:0,0': '', 'gfx/image/npcicon_0.dds:3,0': 'nora',
                    'gfx/image/npcicon_1.dds:0,0': 'granites'}


def strip_comments(t):
    import re
    return re.sub(r'<!--.*?-->', '', t, flags=re.S)


def tag_rows(t, tag, start=0, end=None):
    """[(attributes, where the tag ends)] of every <tag ...> in t[start:end], with either quote (a
    '>' in a quoted value doesn't end the tag)."""
    import re
    end = len(t) if end is None else end
    attr = re.compile(r'([\w:]+)\s*=\s*(?:"([^"]*)"|\'([^\']*)\')')
    return [({k: a or b for k, a, b in attr.findall(m.group(1))}, m.end())
            for m in re.compile(r'<%s\s((?:[^>"\']|"[^"]*"|\'[^\']*\')*)>' % tag).finditer(t, start, end)]


def to_int(v, default):
    try:
        return int(v.strip())
    except (AttributeError, ValueError):
        return default


def dxt_decode(data, w, h, kind):
    """DXT1/3/5 as RGBA, as MabiWiki.cpp's decodeDds (pure Python: about a second for 512x512)."""
    out = bytearray(w * h * 4)
    bw, bh = (w + 3) // 4, (h + 3) // 4
    size = 8 if kind == 'DXT1' else 16
    if len(data) < bw * bh * size:
        return None
    for by in range(bh):
        for bx in range(bw):
            at = (by * bw + bx) * size
            cb = at if kind == 'DXT1' else at + 8
            c0, c1, idx = struct.unpack_from('<HHI', data, cb)
            pal = []
            for c in (c0, c1):
                r, g, b = (c >> 11) & 31, (c >> 5) & 63, c & 31
                pal.append([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255])
            if c0 > c1 or kind != 'DXT1':
                pal.append([(2 * pal[0][i] + pal[1][i]) // 3 for i in range(3)] + [255])
                pal.append([(pal[0][i] + 2 * pal[1][i]) // 3 for i in range(3)] + [255])
            else:
                pal.append([(pal[0][i] + pal[1][i]) // 2 for i in range(3)] + [255])
                pal.append([0, 0, 0, 0])
            if kind == 'DXT3':
                a = int.from_bytes(data[at:at + 8], 'little')
                alpha = [((a >> (4 * i)) & 15) * 17 for i in range(16)]
            elif kind == 'DXT5':
                a0, a1 = data[at], data[at + 1]
                ap = [a0, a1]
                if a0 > a1:
                    ap += [((6 - i) * a0 + (1 + i) * a1) // 7 for i in range(6)]
                else:
                    ap += [((4 - i) * a0 + (1 + i) * a1) // 5 for i in range(4)] + [0, 255]
                bits = int.from_bytes(data[at + 2:at + 8], 'little')
                alpha = [ap[(bits >> (3 * i)) & 7] for i in range(16)]
            else:
                alpha = None
            for i in range(16):
                x, y = bx * 4 + (i & 3), by * 4 + (i >> 2)
                if x < w and y < h:
                    p = pal[(idx >> (2 * i)) & 3]
                    o = (y * w + x) * 4
                    out[o:o + 4] = bytes((p[0], p[1], p[2], p[3] if alpha is None else alpha[i]))
    return out


def dds_image(raw):
    """(w, h, RGBA) of a DDS's top level: uncompressed masks, DXT1/3/5, or DX10 8-bit BGRA/RGBA;
    None if it can't."""
    if raw[:4] != b'DDS ' or len(raw) < 128:
        return None
    flags, h, w, pitch = struct.unpack_from('<IIII', raw, 8)
    pf, = struct.unpack_from('<I', raw, 80)
    fourcc = raw[84:88]
    bits, = struct.unpack_from('<I', raw, 88)
    masks = list(struct.unpack_from('<IIII', raw, 92))
    if not (0 < w <= 4096 and 0 < h <= 4096):
        return None
    if pf & 4 and fourcc == b'DX10':
        fmt, = struct.unpack_from('<I', raw, 128)
        px = bytearray(raw[148:148 + w * h * 4])
        if fmt not in (87, 91, 28, 29) or len(px) < w * h * 4:
            return None
        if fmt in (87, 91):
            px[0::4], px[2::4] = px[2::4], px[0::4]
        return w, h, px
    if pf & 4:
        kind = fourcc.decode('ascii', 'replace')
        px = dxt_decode(raw[128:], w, h, kind) if kind in ('DXT1', 'DXT3', 'DXT5') else None
        return (w, h, px) if px is not None else None
    if not pf & 0x40 or bits % 8 or not 0 < bits <= 32:
        return None
    if not pf & 1:
        masks[3] = 0
    bpp = bits // 8
    row = pitch if flags & 8 and pitch else w * bpp
    if len(raw) < 128 + row * (h - 1) + w * bpp:
        return None
    body = b''.join(raw[128 + y * row:128 + y * row + w * bpp] for y in range(h))
    byte_masks = [0xFF << (8 * i) for i in range(bpp)]
    if all(m in byte_masks or m == 0 for m in masks):
        # 8-bit channels: moved by slicing.
        px = bytearray(b'\xff' * (w * h * 4))
        for c, m in enumerate(masks):
            if m:
                px[c::4] = body[byte_masks.index(m)::bpp]
        return w, h, px
    chans = []
    for m in masks:
        chans.append((m, (m & -m).bit_length() - 1 if m else 0, (1 << bin(m).count('1')) - 1))
    channel = lambda v: bytes((((v & m) >> s) * 255 + t // 2) // t if m else 255 for m, s, t in chans)
    values = [int.from_bytes(body[i:i + bpp], 'little') for i in range(0, len(body), bpp)]
    if bpp <= 2:
        table = [channel(v) for v in range(1 << (8 * bpp))]  # every value, once
        return w, h, bytearray(b''.join(table[v] for v in values))
    return w, h, bytearray(b''.join(channel(v) for v in values))


def portrait(entries, key, xml):
    """The still portrait of an animation file: (w, h, RGBA), or None (with why) if it can't."""
    import re
    rows = tag_rows(xml, 'portraitAnimation')
    image = rows[0][0].get('image', '').lower().replace('\\', '/') if rows else ''
    if not image:
        print('portrait %s names no image' % key)
        return None
    image = image[5:] if image.startswith('data/') else image
    image = image if '/' in image else 'gfx/image/npc/' + image
    clips = [tuple(to_int(r.get(k), d) for k, d in (('x1', 0), ('y1', 0), ('x2', -1), ('y2', -1), ('originX', 0), ('originY', 0)))
             for r, _ in tag_rows(xml, 'clipInformation')]
    backgrounds = {}
    for r, end in tag_rows(xml, 'animation'):
        name = r.get('name', '')
        if name in backgrounds:
            continue
        backgrounds[name] = []
        close = xml.find('</animation>', end)
        close = len(xml) if close < 0 else close
        bg = xml.find('<background>', end)
        if bg < 0 or bg > close:
            continue
        bg_end = xml.find('</background>', bg)
        bg_end = close if bg_end < 0 else min(bg_end, close)
        backgrounds[name] = [to_int(c.get('value'), -1) for c, _ in tag_rows(xml, 'clip', bg, bg_end)]
    order = backgrounds.get('base', []) + backgrounds.get('normal', [])
    if not order:
        print('portrait %s has no base' % key)
        return None
    # high_<image> when it's there, decodes and is the same size as the other (whose DXT is only
    # decoded when it's needed: pure Python takes about a second for one).
    high = image[:image.rfind('/') + 1] + 'high_' + image[image.rfind('/') + 1:]
    raw = read_entry(entries[image]) if image in entries else b''
    size = struct.unpack_from('<II', raw, 12)[::-1] if raw[:4] == b'DDS ' and len(raw) >= 128 else None
    src = dds_image(read_entry(entries[high])) if high in entries else None
    if src is None or (size is not None and src[:2] != size):
        src = dds_image(raw) if raw else None
    if src is None:
        print('portrait %s: no image %a' % (key, image))  # (one is a Korean name)
        return None
    sw, sh, spx = src
    pieces, cw, ch = [], 0, 0
    for i in order:
        if not 0 <= i < len(clips):
            continue
        x1, y1, x2, y2, ox, oy = clips[i]
        x2, y2 = min(x2, sw - 1), min(y2, sh - 1)
        if ox < 0:
            x1, ox = x1 - ox, 0
        if oy < 0:
            y1, oy = y1 - oy, 0
        x1, y1 = max(x1, 0), max(y1, 0)
        if x2 < x1 or y2 < y1:
            continue
        pieces.append((x1, y1, x2, y2, ox, oy))
        cw, ch = max(cw, ox + x2 - x1 + 1), max(ch, oy + y2 - y1 + 1)
    if not pieces or cw > 4096 or ch > 4096:
        print('portrait %s has no pieces' % key)
        return None
    canvas = bytearray(cw * ch * 4)
    for x1, y1, x2, y2, ox, oy in pieces:
        n = (x2 - x1 + 1) * 4
        for y in range(y1, y2 + 1):
            s = spx[(y * sw + x1) * 4:(y * sw + x1) * 4 + n]
            at = ((oy + y - y1) * cw + ox) * 4
            d = canvas[at:at + n]
            if not any(d[3::4]) or s[3::4] == b'\xff' * (n // 4):
                canvas[at:at + n] = s
                continue
            for p in range(0, n, 4):  # drawn over what's there (straight alpha)
                sa, da = s[p + 3], d[p + 3]
                if sa == 255 or da == 0:
                    d[p:p + 4] = s[p:p + 4]
                elif sa:
                    t = da * (255 - sa)
                    total = sa * 255 + t
                    for k in range(3):
                        d[p + k] = (s[p + k] * sa * 255 + d[p + k] * t + total // 2) // total
                    d[p + 3] = (total + 127) // 255
            canvas[at:at + n] = d
    alpha = bytes(canvas[3::4])
    rows_shown = [y for y in range(ch) if alpha[y * cw:(y + 1) * cw].count(0) != cw]
    if not rows_shown:
        print('portrait %s is empty' % key)
        return None
    top, bottom = rows_shown[0], rows_shown[-1]
    left, right = cw, -1
    for y in rows_shown:
        r = alpha[y * cw:(y + 1) * cw]
        left = min(left, cw - len(r.lstrip(b'\0')))
        right = max(right, len(r.rstrip(b'\0')) - 1)
    w, h = right - left + 1, bottom - top + 1
    return w, h, b''.join(canvas[((top + y) * cw + left) * 4:((top + y) * cw + left + w) * 4] for y in range(h))


def npc_images(entries, out):
    """data/icons/npcs/<name>.png, data/portraits/<key>.png and data/npc-images.js, as
    MabiWiki.cpp makes them. Returns (faces, portraits)."""
    def file_name(n):  # per UTF-8 byte, as MabiWiki.cpp's fileName
        return ''.join(chr(b).lower() if b < 128 and (chr(b).isalnum() or chr(b) in '_-.') else '_' for b in n.encode('utf-8'))
    icons_folder = os.path.join(out, 'data', 'icons', 'npcs')
    portraits_folder = os.path.join(out, 'data', 'portraits')
    for folder in (icons_folder, portraits_folder):
        os.makedirs(folder, exist_ok=True)
        for old in os.listdir(folder):
            if old.lower().endswith('.png'):
                os.remove(os.path.join(folder, old))
    by_name, sheets, faces, portraits = {}, {}, 0, 0
    npcinfo = strip_comments(text(read_entry(entries['db/npcinfo.xml']))) if 'db/npcinfo.xml' in entries else ''
    for row, _ in tag_rows(npcinfo, 'NpcInfo'):
        # Values as MabiWiki.cpp's forEachRow has them: named entities decoded, others not.
        for k, v in row.items():
            for a, b in (('&lt;', '<'), ('&gt;', '>'), ('&quot;', '"'), ('&apos;', "'"), ('&amp;', '&')):
                v = v.replace(a, b)
            row[k] = v
        name, sheet = row.get('GeneralName', ''), row.get('IconImageName', '').lower().replace('\\', '/')
        if not name or not sheet or not row.get('IconX') or not row.get('IconY'):
            continue
        sheet = sheet[5:] if sheet.startswith('data/') else sheet
        x, y = to_int(row['IconX'], -1), to_int(row['IconY'], -1)
        cell = '%s:%d,%d' % (sheet, x, y)
        if cell in NPC_ICON_FILLERS and NPC_ICON_FILLERS[cell] != file_name(name):
            continue
        if sheet not in sheets:
            sheets[sheet] = read_entry(entries[sheet]) if sheet in entries else b''
        px = dds_cell(sheets[sheet], x * NPC_ICON_CELL, y * NPC_ICON_CELL, NPC_ICON_CELL) if x >= 0 and y >= 0 else None
        if px is None:
            continue
        key = file_name(name)
        with open(os.path.join(icons_folder, key + '.png'), 'wb') as f:
            f.write(png(NPC_ICON_CELL, NPC_ICON_CELL, px))
        by_name[key] = {'icon': key + '.png', 'portrait': None}
        faces += 1
    prefix = 'db/npc/npcportrait_'
    for name in sorted(entries):
        if not name.startswith(prefix) or '/' in name[len('db/npc/'):] or not name.endswith('.xml'):
            continue
        key = name[len(prefix):-4]
        if key.startswith('ani_'):
            key = key[4:]
        elif 'db/npc/npcportrait_ani_%s.xml' % key in entries:
            continue  # the ani_ file of the name is the one
        key = file_name(key)
        image = portrait(entries, key, strip_comments(text(read_entry(entries[name]))))
        if image is None:
            continue
        with open(os.path.join(portraits_folder, key + '.png'), 'wb') as f:
            f.write(png(*image))
        by_name.setdefault(key, {'icon': None, 'portrait': None})['portrait'] = key + '.png'
        portraits += 1
    with open(os.path.join(out, 'data', 'npc-images.js'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('window.GAME_NPC_IMAGES = ')
        json.dump(dict(sorted(by_name.items())), f, separators=(',', ':'))
        f.write(';\n')
    return faces, portraits


MAP_FOLDER = 'gfx/gui/map_jpg/'


def map_base(name):
    """A minimap file's name without folder, .jpg and language, and how good a pick it is (as
    MabiWiki.cpp's mapBase: English first, then none, then Japanese, then any)."""
    base = name[len(MAP_FOLDER):-4]
    for suffix, rank in (('_eng', 0), ('_jpn', 2), ('_kor', 3), ('_chn', 3), ('_twn', 3)):
        if len(base) > len(suffix) and base.endswith(suffix):
            return base[:-len(suffix)], rank
    return base, 1


def minimaps(entries, out):
    """data/maps/<name>.jpg (copied as they are) and data/maps.js, as MabiWiki.cpp makes them."""
    folder = os.path.join(out, 'data', 'maps')
    os.makedirs(folder, exist_ok=True)
    for old in os.listdir(folder):
        if old.lower().endswith('.jpg'):
            os.remove(os.path.join(folder, old))
    best = {}
    for name in entries:
        if name.startswith(MAP_FOLDER) and '/' not in name[len(MAP_FOLDER):] and name.endswith('.jpg') and len(name) > len(MAP_FOLDER) + 4:
            base, rank = map_base(name)
            base = ''.join(c if c.isalnum() or c in '_-.' else '_' for c in base.lower())
            if base not in best or rank < best[base][0]:
                best[base] = (rank, name)
    names = []
    for base, (_, name) in sorted(best.items()):
        raw = read_entry(entries[name])
        if not safe_name(base) or raw[:2] != b'\xff\xd8':
            continue
        with open(os.path.join(folder, base + '.jpg'), 'wb') as f:
            f.write(raw)
        names.append(base)
    with open(os.path.join(out, 'data', 'maps.js'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('window.GAME_MAPS = ' + json.dumps(names) + ';\n')
    return len(names)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    game = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.dirname(os.path.abspath(__file__))
    entries = read_packs(os.path.join(game, 'package'))
    data = {'version': 1, 'built': time.strftime('%Y-%m-%d %H:%M'), 'source': {}, 'files': {}}
    for name in wanted(entries):
        e = entries[name]
        t0 = time.time()
        data['files'][name] = text(read_entry(e))
        data['source'][name] = {'pack': os.path.basename(e[0]), 'version': e[1], 'size': e[4]}
        print('%-34s %-20s version %-5d %9d bytes  %.1fs' % (name, os.path.basename(e[0]), e[1], e[4], time.time() - t0))
    os.makedirs(os.path.join(out, 'data'), exist_ok=True)
    dest = os.path.join(out, 'data', 'game-data.js')
    with open(dest, 'w', encoding='utf-8', newline='\n') as f:
        f.write('window.GAME_DATA = ')
        json.dump(data, f, ensure_ascii=False, separators=(',', ':'))
        f.write(';\n')
    print('wrote', dest, os.path.getsize(dest), 'bytes')
    t0 = time.time()
    print('wrote %d skill icons  %.1fs' % (skill_icons(entries, out), time.time() - t0))
    t0 = time.time()
    print('wrote %d NPC faces and %d portraits  %.1fs' % (npc_images(entries, out) + (time.time() - t0,)))
    t0 = time.time()
    print('wrote %d minimaps  %.1fs' % (minimaps(entries, out), time.time() - t0))
    return 0


if __name__ == '__main__':
    sys.exit(main())
