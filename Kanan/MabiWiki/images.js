/* Mabinogi Wiki: the item icons, skill icons, NPC faces and portraits, made in the browser from the
 * game's packs, pixel for pixel as Kanan makes them (MabiWiki.cpp makeIcons, makeSkillIcons,
 * makeNpcImages).
 *
 *   MabiImages.itemIcons(packs, sink, onProgress)  -> { map: {item id: file}, count }
 *   MabiImages.skillIcons(packs, sink, onProgress) -> { map: {skill id: file}, count }
 *   MabiImages.npcImages(packs, sink, onProgress)  -> { map: {name: {icon, portrait}}, icons, portraits }
 *   MabiImages.pngEncode(width, height, rgba)      -> Promise<Uint8Array>
 *   MabiImages.script(global, map)                 -> the .js file's text as Kanan writes it
 *
 * packs: MabiPacks.open's (has, read, text, and names() or entries). sink.write(path, bytes) with
 * paths below MabiWiki\data\: icons/items/<name>_<x>x<y>_<c1>-<c2>-<c3>.png, icons/skills/<id>.png,
 * icons/npcs/<name>.png, portraits/<key>.png. The maps are window.GAME_ICONS, GAME_SKILL_ICONS and
 * GAME_NPC_IMAGES; script() gives them in Kanan's text (keys sorted, no spaces). The sink is
 * expected to have cleared the category's folder (Kanan deletes the last run's PNGs first).
 * onProgress(done, total) counts PNGs.
 *
 * The pixel work runs in up to 4 workers (Blob URL workers: new Worker('file.js') isn't allowed
 * from file://). Each gets the pack's File and where the entry is, and reads, decodes, composites
 * and encodes it itself. Without workers (or with MabiImages.useWorkers = false) the same code runs
 * here, letting the page draw every 20 ms or so. PNGs are encoded here, not by a canvas: a canvas
 * premultiplies alpha, which changes the portraits' partly transparent pixels.
 */
(function (root) {
  'use strict';

  // ======================================================================================
  // The pixel work. One function, so that its source also starts the workers.
  // ======================================================================================
  function core() {
    const f32 = Math.fround;

    // The entry's bytes: { bytes } as they are, or { file, start, packed, seed, compressed } read
    // from the pack: XORed with the low byte of each MT19937 output, then zlib.
    async function readEntry(r) {
      if (!r) return null;
      if (r.bytes) return r.bytes;
      const data = new Uint8Array(await r.file.slice(r.start, r.start + r.packed).arrayBuffer());
      unxor(data, ((r.seed << 7) ^ 0xA9C36DE1) >>> 0);
      return r.compressed ? stream(data, new DecompressionStream('deflate')) : data;
    }

    function unxor(data, seed) {
      const mt = new Uint32Array(624);
      mt[0] = seed;
      for (let i = 1; i < 624; i++) {
        const p = mt[i - 1] ^ (mt[i - 1] >>> 30);
        mt[i] = (Math.imul(1812433253, p) + i) >>> 0;
      }
      let k = 624;
      for (let o = 0; o < data.length; o++) {
        if (k >= 624) {
          for (let i = 0; i < 624; i++) {
            const y = (mt[i] & 0x80000000) | (mt[(i + 1) % 624] & 0x7fffffff);
            mt[i] = mt[(i + 397) % 624] ^ (y >>> 1) ^ (y & 1 ? 0x9908b0df : 0);
          }
          k = 0;
        }
        let y = mt[k++];
        y ^= y >>> 11;
        y ^= (y << 7) & 0x9d2c5680;
        y ^= (y << 15) & 0xefc60000;
        y ^= y >>> 18;
        data[o] ^= y & 255;
      }
    }

    async function stream(data, transform) {
      const s = new Blob([data]).stream().pipeThrough(transform);
      return new Uint8Array(await new Response(s).arrayBuffer());
    }

    // ---------- DDS ----------

    function read32(d, at) {
      return at + 4 <= d.length ? (d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | (d[at + 3] << 24)) >>> 0 : 0;
    }

    // An n-bit channel as 8 bits.
    function expand(v, bits) {
      if (bits === 0) return 255;
      if (bits >= 8) return (v >>> (bits - 8)) & 255;
      const top = (1 << bits) - 1;
      return Math.floor((v * 255 + (top >> 1)) / top);
    }

    function rgb565(c, out) {
      const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
      out[0] = (r << 3) | (r >> 2);
      out[1] = (g << 2) | (g >> 4);
      out[2] = (b << 3) | (b >> 2);
    }

    // A DXT color block (8 bytes at `at`) into the 4x4 pixels at (bx, by).
    const pal = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]];
    function colorBlock(d, at, img, bx, by, dxt1) {
      const c0 = d[at] | (d[at + 1] << 8), c1 = d[at + 2] | (d[at + 3] << 8), idx = read32(d, at + 4);
      rgb565(c0, pal[0]);
      rgb565(c1, pal[1]);
      const four = c0 > c1 || !dxt1;
      for (let i = 0; i < 3; i++) {
        if (four) {
          pal[2][i] = Math.floor((2 * pal[0][i] + pal[1][i]) / 3);
          pal[3][i] = Math.floor((pal[0][i] + 2 * pal[1][i]) / 3);
        } else {
          pal[2][i] = (pal[0][i] + pal[1][i]) >> 1;
          pal[3][i] = 0;
        }
      }
      pal[0][3] = pal[1][3] = pal[2][3] = 255;
      pal[3][3] = four ? 255 : 0;
      for (let i = 0; i < 16; i++) {
        const x = bx + (i & 3), y = by + (i >> 2);
        if (x < img.w && y < img.h) {
          const p = pal[(idx >>> (2 * i)) & 3], o = (y * img.w + x) * 4;
          img.px[o] = p[0]; img.px[o + 1] = p[1]; img.px[o + 2] = p[2]; img.px[o + 3] = p[3];
        }
      }
    }

    // A DDS's top mip level as { w, h, px: RGBA }, or null: uncompressed masks (A1R5G5B5,
    // A4R4G4B4, R5G6B5, A8R8G8B8...) or DXT1/3/5. MabiWiki.cpp decodeDds, rounding included.
    function decodeDds(d) {
      if (!d || d.length < 128 || d[0] !== 0x44 || d[1] !== 0x44 || d[2] !== 0x53 || d[3] !== 0x20) return null;
      const flags = read32(d, 8), h = read32(d, 12) | 0, w = read32(d, 16) | 0, pitch = read32(d, 20);
      const pf = read32(d, 80), four = String.fromCharCode(d[84], d[85], d[86], d[87]), bits = read32(d, 88);
      const masks = [read32(d, 92), read32(d, 96), read32(d, 100), read32(d, 104)];
      if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return null;
      const img = { w, h, px: new Uint8Array(w * h * 4) };
      const size = d.length - 128;
      if (pf & 4) {
        const dxt1 = four === 'DXT1', dxt3 = four === 'DXT3', dxt5 = four === 'DXT5';
        if (!dxt1 && !dxt3 && !dxt5) return null;
        const bs = dxt1 ? 8 : 16, bw = (w + 3) >> 2, bh = (h + 3) >> 2;
        if (size < bw * bh * bs) return null;
        const alpha = new Uint8Array(16), apal = new Uint8Array(8);
        for (let byi = 0; byi < bh; byi++) {
          for (let bxi = 0; bxi < bw; bxi++) {
            const at = 128 + (byi * bw + bxi) * bs;
            if (dxt1) { colorBlock(d, at, img, bxi * 4, byi * 4, true); continue; }
            colorBlock(d, at + 8, img, bxi * 4, byi * 4, false);
            if (dxt3) {
              for (let i = 0; i < 16; i++) alpha[i] = ((d[at + (i >> 1)] >> ((i & 1) * 4)) & 15) * 17;
            } else {
              const a0 = d[at], a1 = d[at + 1];
              apal[0] = a0; apal[1] = a1;
              if (a0 > a1) {
                for (let i = 0; i < 6; i++) apal[2 + i] = Math.floor(((6 - i) * a0 + (1 + i) * a1) / 7);
              } else {
                for (let i = 0; i < 4; i++) apal[2 + i] = Math.floor(((4 - i) * a0 + (1 + i) * a1) / 5);
                apal[6] = 0; apal[7] = 255;
              }
              let lo = d[at + 2] | (d[at + 3] << 8) | (d[at + 4] << 16), hi = d[at + 5] | (d[at + 6] << 8) | (d[at + 7] << 16);
              for (let i = 0; i < 8; i++) { alpha[i] = apal[lo & 7]; lo >>= 3; }
              for (let i = 8; i < 16; i++) { alpha[i] = apal[hi & 7]; hi >>= 3; }
            }
            for (let i = 0; i < 16; i++) {
              const x = bxi * 4 + (i & 3), y = byi * 4 + (i >> 2);
              if (x < w && y < h) img.px[(y * w + x) * 4 + 3] = alpha[i];
            }
          }
        }
        return img;
      }
      if (!(pf & 0x40) || bits % 8 !== 0 || bits === 0 || bits > 32) return null;
      const hasAlpha = (pf & 1) !== 0, bpp = bits / 8;
      const row = (flags & 8) && pitch ? pitch : w * bpp;
      if (size < row * (h - 1) + w * bpp) return null;
      const shift = [0, 0, 0, 0], width = [0, 0, 0, 0], mask = [0, 0, 0, 0];
      for (let c = 0; c < 4; c++) {
        const m = c === 3 && !hasAlpha ? 0 : masks[c];
        if (!m) continue;
        while (((m >>> shift[c]) & 1) === 0) shift[c]++;
        while (shift[c] + width[c] < 32 && ((m >>> (shift[c] + width[c])) & 1) !== 0) width[c]++;
        mask[c] = width[c] >= 32 ? 0xffffffff : (2 ** width[c]) - 1;
      }
      const px = img.px;
      for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
          const at = 128 + y * row + x * bpp;
          let v = 0;
          for (let b = bpp - 1; b >= 0; b--) v = v * 256 + d[at + b];
          const o = (y * w + x) * 4;
          for (let c = 0; c < 4; c++) px[o + c] = width[c] ? expand(((v >>> shift[c]) & mask[c]) >>> 0, width[c]) : 255;
        }
      }
      return img;
    }

    // decodeDds, plus DX10 files of 8-bit BGRA or RGBA (four high_ portraits are B8G8R8A8_SRGB).
    function decodeImage(d) {
      if (!d || d.length < 148 || d[0] !== 0x44 || d[1] !== 0x44 || d[2] !== 0x53 || d[3] !== 0x20 ||
        String.fromCharCode(d[84], d[85], d[86], d[87]) !== 'DX10') return decodeDds(d);
      const h = read32(d, 12) | 0, w = read32(d, 16) | 0, format = read32(d, 128);
      const bgra = format === 87 || format === 91;
      if (!bgra && format !== 28 && format !== 29) return null;
      if (w <= 0 || h <= 0 || w > 4096 || h > 4096 || d.length < 148 + w * h * 4) return null;
      const px = d.slice(148, 148 + w * h * 4);
      if (bgra) for (let i = 0; i < px.length; i += 4) { const t = px[i]; px[i] = px[i + 2]; px[i + 2] = t; }
      return { w, h, px };
    }

    // ---------- item icons ----------
    // Gamma 2.2 both ways through 4096-step tables, in 32-bit floats as the C++ computes them.
    let LIN = null, SRGB = null;
    const THIRD = f32(1 / 3), AF = new Float32Array(256);
    function tables() {
      LIN = new Float32Array(4097);
      SRGB = new Uint8Array(4097);
      const g = f32(2.2), inv = f32(1 / g);
      for (let i = 0; i <= 4096; i++) {
        const x = f32(i / 4096);
        LIN[i] = Math.pow(x, g);
        SRGB[i] = Math.floor(f32(f32(Math.pow(x, inv)) * 255) + 0.5);
      }
      for (let a = 0; a < 256; a++) AF[a] = a / 255;
    }
    // lroundf(clamp(v, 0, 1) * 4096)
    const step = (v) => Math.floor((v < 0 ? 0 : v > 1 ? 1 : v) * 4096 + 0.5);

    // The icon from its strip and the item's three colors (sRGB, 0..1, as 32-bit floats). Frames
    // 0-2 are tinted, frame 3 is sRGB 0.5, later frames black; a layer's texel t gives
    // clamp(color + 2t - 1), the layers add up in linear light weighted by alpha, and a pixel shows
    // where the alpha adds up to 1/3. Null when nothing shows.
    function composite(src, invX, invY, colors) {
      if (invX > 170 || invY > 170) return null; // 4096 / 24
      if (!LIN) tables();
      const fw = invX * 24, fh = invY * 24;
      const frames = fw > 0 ? Math.min(Math.floor(src.w / fw), 16) : 0;
      if (frames === 0 || fh <= 0) return null;
      // Each frame's linear value for each channel and texel byte.
      const lin = [];
      for (let k = 0; k < frames; k++) {
        for (let c = 0; c < 3; c++) {
          const slot = k < 3 ? colors[k][c] : k === 3 ? 0.5 : 0, t = new Float32Array(256);
          for (let p = 0; p < 256; p++) t[p] = LIN[step(f32(f32(slot + f32(p / 127.5)) - 1))];
          lin.push(t);
        }
      }
      const out = { w: fw, h: fh, px: new Uint8Array(fw * fh * 4) };
      const sp = src.px, op = out.px, rows = Math.min(fh, src.h);
      let any = false;
      for (let y = 0; y < rows; y++) {
        for (let x = 0; x < fw; x++) {
          let s0 = 0, s1 = 0, s2 = 0, alpha = 0;
          for (let k = 0; k < frames; k++) {
            const o = (y * src.w + k * fw + x) * 4, a = sp[o + 3];
            if (a === 0) continue;
            const af = AF[a];
            s0 = f32(s0 + f32(lin[k * 3][sp[o]] * af));
            s1 = f32(s1 + f32(lin[k * 3 + 1][sp[o + 1]] * af));
            s2 = f32(s2 + f32(lin[k * 3 + 2][sp[o + 2]] * af));
            alpha = f32(alpha + af);
          }
          if (alpha >= THIRD) {
            const q = (y * fw + x) * 4;
            op[q] = SRGB[step(s0)]; op[q + 1] = SRGB[step(s1)]; op[q + 2] = SRGB[step(s2)]; op[q + 3] = 255;
            any = true;
          }
        }
      }
      return any ? out : null;
    }

    // ---------- atlas cells and portraits ----------

    function cell(src, x, y, size) {
      const left = x * size, top = y * size;
      if (!src || x < 0 || y < 0 || left + size > src.w || top + size > src.h) return null;
      const px = new Uint8Array(size * size * 4);
      for (let r = 0; r < size; r++) px.set(src.px.subarray(((top + r) * src.w + left) * 4, ((top + r) * src.w + left + size) * 4), r * size * 4);
      return { w: size, h: size, px };
    }

    // The clips ({ x1, y1, x2, y2, ox, oy }: that rectangle, corners included, drawn with its top
    // left at the origin), in order, with straight alpha, cropped to what shows. Null when empty.
    function portrait(src, clips) {
      const pieces = [];
      let cw = 0, ch = 0;
      for (const c0 of clips) {
        const c = Object.assign({}, c0);
        c.x2 = Math.min(c.x2, src.w - 1);
        c.y2 = Math.min(c.y2, src.h - 1);
        if (c.ox < 0) { c.x1 -= c.ox; c.ox = 0; }
        if (c.oy < 0) { c.y1 -= c.oy; c.oy = 0; }
        c.x1 = Math.max(c.x1, 0);
        c.y1 = Math.max(c.y1, 0);
        if (c.x2 < c.x1 || c.y2 < c.y1) continue;
        pieces.push(c);
        cw = Math.max(cw, c.ox + c.x2 - c.x1 + 1);
        ch = Math.max(ch, c.oy + c.y2 - c.y1 + 1);
      }
      if (!pieces.length || cw > 4096 || ch > 4096) return null;
      const canvas = new Uint8Array(cw * ch * 4), s = src.px;
      for (const c of pieces) {
        for (let y = c.y1; y <= c.y2; y++) {
          for (let x = c.x1; x <= c.x2; x++) {
            const si = (y * src.w + x) * 4, di = ((c.oy + y - c.y1) * cw + c.ox + x - c.x1) * 4;
            const sa = s[si + 3], da = canvas[di + 3];
            if (sa === 255 || da === 0) {
              canvas[di] = s[si]; canvas[di + 1] = s[si + 1]; canvas[di + 2] = s[si + 2]; canvas[di + 3] = sa;
            } else if (sa !== 0) {
              const t = da * (255 - sa), all = sa * 255 + t;
              for (let k = 0; k < 3; k++) canvas[di + k] = Math.floor((s[si + k] * sa * 255 + canvas[di + k] * t + (all >> 1)) / all);
              canvas[di + 3] = Math.floor((all + 127) / 255);
            }
          }
        }
      }
      let left = cw, top = ch, right = -1, bottom = -1;
      for (let y = 0; y < ch; y++) {
        for (let x = 0; x < cw; x++) {
          if (canvas[(y * cw + x) * 4 + 3] !== 0) {
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
          }
        }
      }
      if (right < 0) return null;
      const w = right - left + 1, h = bottom - top + 1, px = new Uint8Array(w * h * 4);
      for (let y = 0; y < h; y++) px.set(canvas.subarray(((top + y) * cw + left) * 4, ((top + y) * cw + left + w) * 4), y * w * 4);
      return { w, h, px };
    }

    // ---------- PNG ----------
    let crcTable = null;
    function crc32(d, c) {
      if (!crcTable) {
        crcTable = new Uint32Array(256);
        for (let n = 0; n < 256; n++) {
          let v = n;
          for (let k = 0; k < 8; k++) v = v & 1 ? 0xEDB88320 ^ (v >>> 1) : v >>> 1;
          crcTable[n] = v;
        }
      }
      for (let i = 0; i < d.length; i++) c = crcTable[(c ^ d[i]) & 255] ^ (c >>> 8);
      return c;
    }

    // RGBA, 8 bits, filter 0 on every row, zlib through CompressionStream.
    async function pngEncode(w, h, rgba) {
      const stride = w * 4, raw = new Uint8Array((stride + 1) * h);
      for (let y = 0; y < h; y++) raw.set(rgba.subarray(y * stride, (y + 1) * stride), y * (stride + 1) + 1);
      const z = await stream(raw, new CompressionStream('deflate'));
      const out = new Uint8Array(8 + 25 + 12 + z.length + 12), dv = new DataView(out.buffer);
      out.set([137, 80, 78, 71, 13, 10, 26, 10]);
      let at = 8;
      const chunk = (type, data) => {
        dv.setUint32(at, data.length);
        for (let i = 0; i < 4; i++) out[at + 4 + i] = type.charCodeAt(i);
        out.set(data, at + 8);
        const crc = crc32(data, crc32(out.subarray(at + 4, at + 8), -1));
        dv.setUint32(at + 8 + data.length, ~crc >>> 0);
        at += 12 + data.length;
      };
      const ihdr = new Uint8Array(13), hv = new DataView(ihdr.buffer);
      hv.setUint32(0, w);
      hv.setUint32(4, h);
      ihdr.set([8, 6, 0, 0, 0], 8);
      chunk('IHDR', ihdr);
      chunk('IDAT', z);
      chunk('IEND', new Uint8Array(0));
      return out;
    }

    const png = (img) => (img ? pngEncode(img.w, img.h, img.px) : null);

    // A file that can't be read any more (the game was patched) stops the run; a file that
    // doesn't inflate or decode just has no picture, as in Kanan.
    const gone = (e) => e && (e.name === 'NotReadableError' || e.name === 'NotFoundError');
    async function decoded(ref, decode) {
      try { return decode(await readEntry(ref)); } catch (e) { if (gone(e)) throw e; return null; }
    }

    // A task: { kind: 'icons', src, uses: [{ invX, invY, colors }] } (a texture's icons; the
    // texture as it is where the composite has nothing), { kind: 'cells', src, image, size,
    // cells: [[x, y]] } (atlas cells; image: DX10 allowed) or { kind: 'portrait', normal, high,
    // clips } (high when it decodes and is the size of normal). Gives the PNGs, null for each one
    // that can't be made.
    async function run(task) {
      if (task.kind === 'icons') {
        const src = await decoded(task.src, decodeDds);
        return Promise.all(task.uses.map((u) => src && png(composite(src, u.invX, u.invY, u.colors) || src)));
      }
      if (task.kind === 'cells') {
        const src = await decoded(task.src, task.image ? decodeImage : decodeDds);
        return Promise.all(task.cells.map(([x, y]) => png(cell(src, x, y, task.size))));
      }
      const normal = task.normal && await decoded(task.normal, decodeImage);
      const better = task.high && await decoded(task.high, decodeImage);
      const src = better && (!normal || (better.w === normal.w && better.h === normal.h)) ? better : normal;
      return [src ? await png(portrait(src, task.clips)) : null];
    }

    return { run, pngEncode, decodeDds, decodeImage, composite, readEntry, gone };
  }

  const C = core();

  // ======================================================================================
  // Workers
  // ======================================================================================
  const WORKER_SOURCE = 'const C = (' + core.toString() + ')();\n' +
    'onmessage = async (e) => {\n' +
    '  const { id, task } = e.data;\n' +
    '  try {\n' +
    '    const out = await C.run(task);\n' +
    '    postMessage({ id, out }, out.filter(Boolean).map((p) => p.buffer));\n' +
    '  } catch (err) { postMessage({ id, error: String(err && err.message || err), name: err && err.name }); }\n' +
    '};\n';

  let pool = null, users = 0;

  // Up to 4 workers (one core left for the page), or null where they can't start. Each takes up
  // to SLOTS tasks at once, so that it has pixels to work on while a read or a deflate is under
  // way. One that fails is dropped and its tasks done here.
  const SLOTS = 3;
  async function startPool() {
    api.poolSize = 0;
    if (!api.useWorkers || typeof Worker === 'undefined' || typeof Blob === 'undefined') return null;
    const n = Math.min(4, Math.max(1, (navigator.hardwareConcurrency || 2) - 1));
    let url;
    try { url = URL.createObjectURL(new Blob([WORKER_SOURCE], { type: 'text/javascript' })); } catch (e) { return null; }
    const workers = [];
    for (let i = 0; i < n; i++) {
      try { workers.push(new Worker(url)); } catch (e) { break; }
    }
    URL.revokeObjectURL(url);
    const p = { idle: [], queue: [], jobs: new Map(), next: 1, size: 0 };
    const give = (w) => {
      if (w.dead) return;
      const job = p.queue.shift();
      if (!job) { p.idle.push(w); return; }
      p.jobs.set(job.id, Object.assign(job, { w }));
      w.postMessage({ id: job.id, task: job.task });
    };
    // A worker that can't run (a blocked Blob URL) says so with an error, or never answers: each
    // gets a first empty task, and those that answer within 10 s join.
    const alive = await Promise.all(workers.map((w) => new Promise((resolve) => {
      const timer = setTimeout(() => resolve(false), 10000);
      w.onmessage = () => { clearTimeout(timer); resolve(true); };
      w.onerror = () => { clearTimeout(timer); resolve(false); };
      w.postMessage({ id: 0, task: { kind: 'cells', src: null, cells: [] } });
    })));
    for (let i = 0; i < workers.length; i++) {
      const w = workers[i];
      if (!alive[i]) { w.terminate(); continue; }
      p.size++;
      w.onmessage = (e) => {
        const job = p.jobs.get(e.data.id);
        p.jobs.delete(e.data.id);
        if (job) e.data.error ? job.reject(Object.assign(new Error(e.data.error), { name: e.data.name || 'Error' })) : job.resolve(e.data.out);
        give(w);
      };
      w.onerror = (e) => {
        e.preventDefault();
        p.size--;
        w.dead = true;
        w.terminate();
        p.idle = p.idle.filter((x) => x !== w);
        for (const [id, job] of p.jobs) if (job.w === w) { p.jobs.delete(id); C.run(job.task).then(job.resolve, job.reject); }
      };
      for (let k = 0; k < SLOTS; k++) give(w);
    }
    api.poolSize = p.size;
    if (!p.size) return null;
    p.run = (task) => (p.size <= 0 ? C.run(task) : new Promise((resolve, reject) => {
      p.queue.push({ id: p.next++, task, resolve, reject });
      if (p.idle.length) give(p.idle.pop());
    }));
    p.close = () => { for (const w of workers) w.terminate(); };
    return p;
  }

  async function acquire() {
    users++;
    if (!pool) pool = startPool();
    return pool;
  }

  async function release() {
    if (--users > 0 || !pool) return;
    const p = await pool;
    pool = null;
    if (p) p.close();
  }

  // Lets the page draw and take input. A message, not setTimeout, which a hidden tab slows to 1 s.
  function yieldToPage() {
    return new Promise((resolve) => {
      const ch = new MessageChannel();
      ch.port1.onmessage = () => { ch.port1.close(); resolve(); };
      ch.port2.postMessage(0);
    });
  }

  // Runs the tasks, at most a few at a time, and hands each one's PNGs to done(task, pngs) (which
  // may write them) before taking more. Here (no workers) it lets the page draw every 20 ms.
  async function runTasks(tasks, done) {
    const p = await acquire();
    try {
      let next = 0, last = performance.now(), failed = null;
      const exec = async (task) => {
        if (p) return p.run(task);
        if (performance.now() - last > 20) {
          await yieldToPage();
          last = performance.now();
        }
        return C.run(task);
      };
      const lane = async () => {
        while (next < tasks.length && !failed) {
          const task = tasks[next++];
          let out;
          try { out = await exec(task); } catch (e) { if (C.gone(e)) { failed = failed || e; break; } out = null; }
          await done(task, out || []);
        }
      };
      await Promise.all(Array.from({ length: p ? p.size * SLOTS + 2 : 2 }, lane));
      if (failed) throw failed;
    } finally {
      await release();
    }
  }

  // ======================================================================================
  // Reading the game's data (as MabiWiki.cpp does, quirks included)
  // ======================================================================================

  // Where an entry is in its pack, for a worker to read it itself; else its bytes.
  async function ref(packs, name) {
    const e = packs.entry ? packs.entry(name) : packs.entries && packs.entries.get(name);
    if (e && e.file && typeof e.file.slice === 'function' && e.dataStart != null) {
      return { file: e.file, start: e.dataStart + e.offset, packed: e.packed, seed: e.seed, compressed: !!e.compressed };
    }
    const bytes = await soft(packs.read(name));
    return bytes ? { bytes } : null;
  }

  // The promise's value, or null when it fails, except when the pack can't be read any more.
  const soft = (promise) => promise.catch((e) => { if (C.gone(e)) throw e; return null; });

  const names = (packs) => (packs.names ? [...packs.names()] : [...packs.entries.keys()]);
  const lower = (s) => s.replace(/[A-Z\\]/g, (c) => (c === '\\' ? '/' : c.toLowerCase()));
  const utf8 = new TextEncoder();

  // Whether a name can go in a file name as it is (MabiWiki.cpp safeName).
  const safeName = (s) => !!s && !s.includes('..') && !/[\x00-\x1f\/\\:*?"<>|]/.test(s);

  // A name as a file name: lowercase letters, digits, "_", "-" and "."; any other byte is "_".
  function fileName(name) {
    let out = '';
    for (const b of utf8.encode(name)) {
      const l = b >= 65 && b <= 90 ? b + 32 : b;
      out += (l >= 97 && l <= 122) || (l >= 48 && l <= 57) || l === 95 || l === 45 || l === 46 ? String.fromCharCode(l) : '_';
    }
    return out;
  }

  // std::stoi with a default: leading space, a sign, digits; nothing or out of range: def.
  function toInt(row, key, def) {
    const v = row[key];
    const m = v ? /^[ \t\n\v\f\r]*([+-]?\d+)/.exec(v) : null;
    if (!m) return def;
    const n = parseInt(m[1], 10);
    return n > 2147483647 || n < -2147483648 ? def : n;
  }

  // Where the tag from `from` ends: its '>', not one in a quoted value; -1 if none.
  function tagEnd(text, from) {
    let quote = '';
    for (let p = from; p < text.length; p++) {
      const c = text[p];
      if (quote) { if (c === quote) quote = ''; }
      else if (c === '"' || c === '\'') quote = c;
      else if (c === '>') return p;
    }
    return -1;
  }

  const ENTITIES = [['&lt;', '<'], ['&gt;', '>'], ['&quot;', '"'], ['&apos;', '\''], ['&amp;', '&']];
  function entities(v) {
    if (!v.includes('&')) return v;
    for (const [from, to] of ENTITIES) {
      for (let f = v.indexOf(from); f !== -1; f = v.indexOf(from, f + to.length)) v = v.slice(0, f) + to + v.slice(f + from.length);
    }
    return v;
  }

  const isTrim = (c) => c === ' ' || c === '\t' || c === '\r' || c === '\n';
  function attrName(text, p, eq) {
    let s = p, e = eq - 1;
    while (s < text.length && isTrim(text[s])) s++;
    while (e >= 0 && isTrim(text[e])) e--;
    return s < text.length && e >= 0 && e >= s ? { s, e } : null;
  }

  // Every <tag ...> row, as {attribute: value} with double quotes and entities decoded
  // (MabiWiki.cpp forEachRow).
  function forEachRow(text, tag, fn) {
    const open = '<' + tag + ' ';
    let at = 0;
    while ((at = text.indexOf(open, at)) !== -1) {
      const end = tagEnd(text, at);
      if (end === -1) break;
      const row = {};
      let p = at + open.length;
      while (p < end) {
        const eq = text.indexOf('=', p);
        const q1 = eq === -1 ? -1 : text.indexOf('"', eq);
        const q2 = q1 === -1 ? -1 : text.indexOf('"', q1 + 1);
        if (q2 === -1 || q2 > end) break;
        const n = attrName(text, p, eq);
        if (n) row[text.slice(n.s, n.e + 1)] = entities(text.slice(q1 + 1, q2));
        p = q2 + 1;
      }
      fn(row);
      at = end;
    }
  }

  // Every <tag ...> in text[from, to), with either quote and no entities, and where it ends
  // (MabiWiki.cpp forEachTag).
  function forEachTag(text, tag, from, to, fn) {
    const open = '<' + tag;
    for (let at = text.indexOf(open, from); at !== -1 && at < to; at = text.indexOf(open, at + 1)) {
      let p = at + open.length;
      if (p >= text.length || !/[ \t\n\v\f\r]/.test(text[p])) continue; // <clipInformations>
      const end = tagEnd(text, p);
      if (end === -1) break;
      const row = {};
      while (p < end) {
        const eq = text.indexOf('=', p);
        if (eq === -1 || eq > end) break;
        let q1 = -1;
        for (let i = eq; i < text.length; i++) if (text[i] === '"' || text[i] === '\'') { q1 = i; break; }
        if (q1 === -1 || q1 > end) break;
        const q2 = text.indexOf(text[q1], q1 + 1);
        if (q2 === -1 || q2 > end) break;
        const n = attrName(text, p, eq);
        if (n && n.s < eq) row[text.slice(n.s, n.e + 1)] = text.slice(q1 + 1, q2);
        p = q2 + 1;
      }
      fn(row, end);
    }
  }

  function stripComments(text) {
    let out = '', at = 0;
    for (;;) {
      const open = text.indexOf('<!--', at);
      if (open === -1) { out += text.slice(at); break; }
      out += text.slice(at, open);
      const close = text.indexOf('-->', open + 4);
      if (close === -1) break;
      at = close + 3;
    }
    return out;
  }

  // colortable.data: {color id: color map path below data\ ("color/cloth.raw")}.
  function colorMaps(d) {
    const out = new Map();
    const r32 = (at) => (at + 4 <= d.length ? (d[at] | (d[at + 1] << 8) | (d[at + 2] << 16) | (d[at + 3] << 24)) >>> 0 : 0);
    const schema = r32(16), poolSize = r32(20), dataSize = r32(24);
    const pool = 16 + schema, records = pool + poolSize;
    for (let i = 0; i < Math.floor(dataSize / 72); i++) {
      const at = records + 72 * i;
      if (at + 72 > d.length) break;
      const id = r32(at) | 0, name = r32(at + 4) | 0;
      if (name < 0 || name >= poolSize || pool + name >= d.length) break;
      let end = pool + name;
      while (end < d.length && d[end] !== 0) end++;
      const path = lower(String.fromCharCode.apply(null, d.subarray(pool + name, end)));
      if (!path.startsWith('data/color/')) break; // the distortion maps that follow
      out.set(id, path.slice(5));
    }
    return out;
  }

  // sink.write as true when it's written, false when it failed.
  const writer = (sink) => async (path, bytes) => {
    try { await sink.write(path, bytes); return true; } catch (e) { return false; }
  };

  // fn(item) for each, at most n at a time.
  async function each(list, n, fn) {
    let next = 0;
    const lane = async () => { while (next < list.length) await fn(list[next++]); };
    await Promise.all(Array.from({ length: n }, lane));
  }

  // ======================================================================================
  // Item icons (MabiWiki.cpp makeIcons)
  // ======================================================================================
  async function itemIcons(packs, sink, onProgress) {
    const table = await soft(packs.read('db/colortable.data'));
    if (!table) throw new Error("Couldn't read the color table.");
    const colors = new Map();
    await Promise.all([...colorMaps(table)].map(async ([id, path]) => {
      const raw = packs.has(path) ? await soft(packs.read(path)) : null;
      if (raw && raw.length >= 3) colors.set(id, [raw[0] / 255, raw[1] / 255, raw[2] / 255].map(Math.fround));
    }));
    const itemdb = await packs.text('db/itemdb.xml');
    if (!itemdb) throw new Error("Couldn't read itemdb.xml.");

    // Each item's icon file (the first name of File_InvImage the packs have), by texture.
    const uses = [], byTexture = new Map();
    forEachRow(itemdb, 'Mabi_Item', (row) => {
      const id = row.ID, value = row.File_InvImage;
      if (!id || !value) return;
      let name = '';
      for (let part of value.split(';')) {
        part = part.split('<')[0].replace(/^[ \t]+|[ \t]+$/g, '');
        part = lower(part);
        if (safeName(part) && packs.has('gfx/image/' + part + '.dds')) { name = part; break; }
      }
      if (!name) return;
      const invX = Math.max(1, toInt(row, 'Inv_XSize', 1)), invY = Math.max(1, toInt(row, 'Inv_YSize', 1));
      const ids = [toInt(row, 'App_Color1', 0), toInt(row, 'App_Color2', 0), toInt(row, 'App_Color3', 0)];
      const file = name + '_' + invX + 'x' + invY + '_' + ids.join('-') + '.png';
      if (!byTexture.has(name)) byTexture.set(name, new Map());
      const files = byTexture.get(name);
      if (!files.has(file)) files.set(file, { invX, invY, colors: ids.map((c) => colors.get(c) || [0, 0, 0]) });
      uses.push([id, file]);
    });

    const tasks = [];
    let total = 0;
    for (const [name, files] of byTexture) {
      const src = await ref(packs, 'gfx/image/' + name + '.dds');
      tasks.push({ kind: 'icons', src, files: [...files.keys()], uses: [...files.values()] });
      total += files.size;
    }
    const write = writer(sink), written = new Set();
    let done = 0;
    await runTasks(tasks, async (task, pngs) => {
      for (let i = 0; i < task.files.length; i++) {
        if (pngs[i] && await write('icons/items/' + task.files[i], pngs[i])) written.add(task.files[i]);
      }
      done += task.files.length;
      if (onProgress) onProgress(done, total);
    });

    // In itemdb's order: an id's last row is the one.
    const map = {};
    for (const [id, file] of uses) if (written.has(file)) map[id] = file;
    return { map, count: written.size };
  }

  // ======================================================================================
  // Skill icons (MabiWiki.cpp makeSkillIcons): a 32x32 cell of an atlas; the row with the
  // highest Version per SkillID, the first of those on a tie.
  // ======================================================================================
  async function skillIcons(packs, sink, onProgress) {
    const skillinfo = await packs.text('db/skill/skillinfo.xml');
    if (!skillinfo) throw new Error("Couldn't read skillinfo.xml.");
    const cells = new Map();
    forEachRow(skillinfo, 'Skill', (row) => {
      const id = row.SkillID || '';
      let atlas = lower(row.ImageFile || '');
      if (!id || !atlas) return;
      if (atlas.startsWith('data/')) atlas = atlas.slice(5);
      const c = { atlas, x: toInt(row, 'PositionX', 0), y: toInt(row, 'PositionY', 0), version: toInt(row, 'Version', 0) };
      const old = cells.get(id);
      if (!old || c.version > old.version) cells.set(id, c);
    });
    if (!cells.size) throw new Error('No skills in skillinfo.xml.');

    const byAtlas = new Map();
    for (const [id, c] of cells) {
      if (!safeName(id) || !packs.has(c.atlas)) continue;
      if (!byAtlas.has(c.atlas)) byAtlas.set(c.atlas, []);
      byAtlas.get(c.atlas).push([id, c.x, c.y]);
    }
    const tasks = [];
    let total = 0, done = 0;
    for (const [atlas, list] of byAtlas) {
      tasks.push({ kind: 'cells', src: await ref(packs, atlas), size: 32, ids: list.map((l) => l[0]), cells: list.map((l) => [l[1], l[2]]) });
      total += list.length;
    }
    const write = writer(sink), map = {};
    await runTasks(tasks, async (task, pngs) => {
      for (let i = 0; i < task.ids.length; i++) {
        const id = task.ids[i];
        if (pngs[i] && await write('icons/skills/' + id + '.png', pngs[i])) map[id] = id + '.png';
      }
      done += task.ids.length;
      if (onProgress) onProgress(done, total);
    });
    return { map, count: Object.keys(map).length };
  }

  // ======================================================================================
  // NPC faces and portraits (MabiWiki.cpp makeNpcImages, makePortrait)
  // ======================================================================================

  // Faces the file gives NPCs that have none of their own: cell, and whose face it is ('' when
  // it's nobody's). Those NPCs get none.
  const NPC_ICON_FILLERS = [
    ['gfx/image/npcicon_0.dds:0,0', ''],
    ['gfx/image/npcicon_0.dds:3,0', 'nora'],
    ['gfx/image/npcicon_1.dds:0,0', 'granites'],
  ];

  // A portrait's task from its animation file, or null (no image, no base).
  async function portraitTask(packs, xml) {
    let image = '';
    forEachTag(xml, 'portraitAnimation', 0, xml.length, (row) => { if (!image) image = lower(row.image || ''); });
    if (!image) return null;
    if (image.startsWith('data/')) image = image.slice(5);
    if (!image.includes('/')) image = 'gfx/image/npc/' + image;

    const clips = [];
    forEachTag(xml, 'clipInformation', 0, xml.length, (row) => {
      const get = (k, def) => Math.min(Math.max(toInt(row, k, def), -(1 << 20)), 1 << 20);
      clips.push({ x1: get('x1', 0), y1: get('y1', 0), x2: get('x2', -1), y2: get('y2', -1), ox: get('originX', 0), oy: get('originY', 0) });
    });

    // The background clips of the first animation of each name: base's, then normal's.
    const backgrounds = new Map();
    const npos = (i) => (i === -1 ? Infinity : i);
    forEachTag(xml, 'animation', 0, xml.length, (row, end) => {
      const name = row.name || '';
      if (backgrounds.has(name)) return;
      const list = [];
      backgrounds.set(name, list);
      const close = npos(xml.indexOf('</animation>', end)), bg = npos(xml.indexOf('<background>', end));
      if (bg === Infinity || bg > close) return;
      const bgEnd = Math.min(npos(xml.indexOf('</background>', bg)), close);
      forEachTag(xml, 'clip', bg, bgEnd, (clip) => list.push(toInt(clip, 'value', -1)));
    });
    const order = (backgrounds.get('base') || []).concat(backgrounds.get('normal') || []);
    if (!order.length) return null;

    const slash = image.lastIndexOf('/');
    const high = image.slice(0, slash + 1) + 'high_' + image.slice(slash + 1);
    const normal = packs.has(image) ? await ref(packs, image) : null;
    const better = packs.has(high) ? await ref(packs, high) : null;
    if (!normal && !better) return null;
    return { kind: 'portrait', normal, high: better, clips: order.filter((i) => i >= 0 && i < clips.length).map((i) => clips[i]) };
  }

  async function npcImages(packs, sink, onProgress) {
    const write = writer(sink), map = {};
    let icons = 0, portraits = 0;

    // Faces: in npcinfo's order, an NPC's last row with a face is the one.
    const npcinfo = packs.has('db/npcinfo.xml') ? (await soft(packs.text('db/npcinfo.xml'))) || '' : '';
    const rows = [], bySheet = new Map();
    forEachRow(stripComments(npcinfo), 'NpcInfo', (row) => {
      const name = row.GeneralName || '';
      let sheet = lower(row.IconImageName || '');
      if (!name || !sheet || !row.IconX || !row.IconY) return;
      if (sheet.startsWith('data/')) sheet = sheet.slice(5);
      const x = toInt(row, 'IconX', -1), y = toInt(row, 'IconY', -1), key = fileName(name);
      const at = sheet + ':' + x + ',' + y;
      if (NPC_ICON_FILLERS.some(([filler, owner]) => at === filler && key !== owner)) return;
      if (!bySheet.has(sheet)) bySheet.set(sheet, new Map());
      const cellsOf = bySheet.get(sheet), xy = x + ',' + y;
      if (!cellsOf.has(xy)) cellsOf.set(xy, [x, y]);
      rows.push({ key, sheet, xy });
    });

    // Portraits: db/npc/npcportrait_ani_<key>.xml, or npcportrait_<key>.xml when there's no ani_
    // file of the name; in name order, a key's last one is the one.
    const xmls = [];
    for (const name of names(packs).sort()) {
      if (!name.startsWith('db/npc/npcportrait_') || name.indexOf('/', 7) !== -1 || !name.endsWith('.xml')) continue;
      let key = name.slice('db/npc/npcportrait_'.length, -4);
      if (key.startsWith('ani_')) key = key.slice(4);
      else if (packs.has('db/npc/npcportrait_ani_' + key + '.xml')) continue;
      xmls.push([fileName(key), name]);
    }

    const tasks = [];
    for (const [sheet, cellsOf] of bySheet) {
      tasks.push({ kind: 'cells', image: true, size: 32, sheet, src: packs.has(sheet) ? await ref(packs, sheet) : null, xys: [...cellsOf.keys()], cells: [...cellsOf.values()] });
    }
    for (const [key, name] of xmls) {
      const raw = await soft(packs.read(name));
      const task = raw && await portraitTask(packs, stripComments(asText(raw)));
      if (task) tasks.push(Object.assign(task, { key, name }));
    }
    const total = rows.length + xmls.length;
    let done = 0;
    const faces = new Map(), pictures = new Map();
    await runTasks(tasks, async (task, pngs) => {
      if (task.kind === 'cells') {
        task.xys.forEach((xy, i) => faces.set(task.sheet + ':' + xy, pngs[i]));
        done += rows.filter((r) => r.sheet === task.sheet).length;
      } else {
        pictures.set(task.name, pngs[0]);
        done++;
      }
      if (onProgress) onProgress(Math.min(done, total), total);
    });

    const face = new Map();
    for (const r of rows) {
      const p = faces.get(r.sheet + ':' + r.xy);
      if (p) face.set(r.key, p);
    }
    await each([...face], 8, async ([key, p]) => {
      if (!await write('icons/npcs/' + key + '.png', p)) return;
      map[key] = { icon: key + '.png', portrait: null };
      icons++;
    });
    const picture = new Map();
    for (const [key, name] of xmls) {
      const p = pictures.get(name);
      if (p) picture.set(key, p);
    }
    await each([...picture], 8, async ([key, p]) => {
      if (!await write('portraits/' + key + '.png', p)) return;
      if (!map[key]) map[key] = { icon: null, portrait: null };
      map[key].portrait = key + '.png';
      portraits++;
    });
    return { map, icons, portraits };
  }

  // The file as text, without a byte order mark (UTF-16LE with one, an odd last byte dropped).
  function asText(d) {
    if (d[0] === 0xff && d[1] === 0xfe) return new TextDecoder('utf-16le').decode(d.subarray(2, d.length - (d.length & 1)));
    if (d[0] === 0xef && d[1] === 0xbb && d[2] === 0xbf) return new TextDecoder('utf-8').decode(d.subarray(3));
    return new TextDecoder('utf-8').decode(d);
  }

  // The map as Kanan writes it: window.<global> = {...}; with the keys in byte order and no spaces.
  function script(global, map) {
    const json = (v) => {
      if (v === null || typeof v !== 'object') return JSON.stringify(v);
      return '{' + Object.keys(v).sort(byBytes).map((k) => JSON.stringify(k) + ':' + json(v[k])).join(',') + '}';
    };
    return 'window.' + global + ' = ' + json(map) + ';\n';
  }
  function byBytes(a, b) {
    if (a === b) return 0;
    const x = utf8.encode(a), y = utf8.encode(b);
    for (let i = 0; i < x.length && i < y.length; i++) if (x[i] !== y[i]) return x[i] - y[i];
    return x.length - y.length;
  }

  const api = {
    useWorkers: true,
    poolSize: 0, // how many workers the last run had (0: it ran here)
    pngEncode: C.pngEncode,
    itemIcons,
    skillIcons,
    npcImages,
    script,
    core: { decodeDds: C.decodeDds, decodeImage: C.decodeImage, composite: C.composite, colorMaps, fileName },
  };
  root.MabiImages = api;
})(typeof window !== 'undefined' ? window : globalThis);
