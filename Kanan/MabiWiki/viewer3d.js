/* Mabinogi Wiki: 3D item, monster and prop viewer.
 *
 *   MabiWikiViewer.open(containerElement, itemId, options) -> { close(), setDyes([hex, hex, hex]) }
 *   MabiWikiViewer.openRace(containerElement, raceId, options) -> { close(), setDyes([hex, hex, hex]) }
 *   MabiWikiViewer.openProp(containerElement, classIds, options) -> { close() }
 *
 * openProp shows a prop (db/propdb.xml class: tree, rock, herb patch, building) as region3d.js's
 * walker draws it: the first of the class ids with a model.
 *
 * openRace shows a db/race.xml race (monster, NPC, pet): DefaultMesh on its Framework skeleton,
 * plus what FirstCreateStript's additemex puts on it, in the bind pose or the first frame of its
 * Standing / Battle stance animation (db/motion.xml). Its options: row (the race's race.xml
 * attributes; else looked up like items' rows), colors (['#rrggbb' x3]: the body's colors, which
 * the client doesn't have for monsters), skin, pose ('bind', 'StandFriendly01', 'StandOffensive01'),
 * yaw, pitch, zoom, spin.
 *
 * The models come straight from the player's own game packs (package\*.pack), read in the
 * browser: nothing is converted ahead of time and the game needn't run. Chrome lets a page opened
 * from file:// read a folder the user picks (showDirectoryPicker); the folder's handle is kept in
 * IndexedDB, so later visits need at most one click to allow it again. Browsers without the picker
 * get <input type=file webkitdirectory> on the package folder instead, every visit.
 *
 * A classic script (Chrome runs no module scripts from file://). three.js is loaded on demand from
 * (window.MABIWIKI_BASE || '') + 'lib/three-bundle.min.js'. The formats are described where each
 * is read below. Effects (particles, glows, beams) aren't shown.
 *
 * options (all optional):
 *   row     the item's itemdb attributes ({ File_FieldMesh: ..., App_Color1: ... }); otherwise the
 *           row is looked up in window.GAME_DATA's db/itemdb.xml, else in the packs.
 *   variant index of the model to show first (0 = first: dropped, then worn)
 *   stat    pose ('r', 'lr', 'e', 'le' ...); dyes: ['#rrggbb', ...] for dye 1-3
 *   yaw, pitch (degrees), zoom (1 = fit), spin (bool)
 *
 * A hosted copy of the wiki (MabiWiki_Hostable, set up with 3D models) sets window.MABIWIKI_MODELS to its models folder: the
 * model files its pages use, written by exportModels from the packs, read over HTTP (HostedPacks).
 *
 * MabiWikiViewer.usePackFiles(files) gives the packs as File objects (tests, other pages);
 * usePacks(packs) packs already open (MabiPacks.open).
 * MabiWikiViewer.core: the pack reader and converter without any page (used by the Node test).
 * The pack reader itself is packs.js (MabiPacks), loaded from next to this file when the page
 * doesn't have it yet.
 */
(function (root) {
  'use strict';

  const BASE = () => root.MABIWIKI_BASE || '';
  const enc = new TextDecoder('latin1');

  // ======================================================================================
  // Packs: packs.js (MabiPacks), the reader the wiki's preparer uses too
  // ======================================================================================
  // The page usually has it already (the single-file page carries it); otherwise it's loaded from
  // next to this file (lib/packs.js), or required in Node. The viewer waits for it before it opens
  // packs (packsLib); what it calls later (decodeDds, asText) is there by then.
  const here = typeof document !== 'undefined' && document.currentScript && /^(file|https?):/.test(document.currentScript.src) ? document.currentScript.src : '';
  if (!root.MabiPacks && typeof require === 'function') { try { require('./packs.js'); } catch (e) { /* not Node's require */ } }
  let libPromise = null;
  function packsLib() {
    if (root.MabiPacks) return Promise.resolve(root.MabiPacks);
    if (!libPromise) {
      libPromise = new Promise((resolve, reject) => {
        const s = document.createElement('script');
        s.src = here ? here.replace(/[^/]*$/, 'packs.js') : BASE() + 'lib/packs.js';
        s.onload = () => (root.MabiPacks ? resolve(root.MabiPacks) : reject(new Error('packs.js')));
        s.onerror = () => { libPromise = null; reject(new Error('packs.js')); };
        document.head.appendChild(s);
      });
    }
    return libPromise;
  }
  if (!root.MabiPacks && typeof document !== 'undefined') packsLib().catch(() => {});
  const P = () => root.MabiPacks;
  const mtLowBytes = (seed, n) => P().mtLowBytes(seed, n);
  const decodeDds = (d) => P().decodeDds(d);
  const asText = (d) => P().asText(d);

  // The two kinds of packs the viewer adds, made once packs.js is there.
  let classes = null;
  function packClasses() {
    if (classes) return classes;
    const { Packs } = P();

    // A hosted copy of the wiki (MabiWiki_Hostable with 3D): the model files its pages use, each stored as the
    // pack has it once un-XORed (zlib or not), under MABIWIKI_MODELS, whose index.json lists them
    // ({ files: [[name, path, seed, size, compressed]], motions: motionIndex's list }). Read over
    // HTTP, so not from file://.
    class HostedPacks extends Packs {
      static async open(base) {
        const r = await fetch(base + 'index.json');
        if (!r.ok) throw new Error('index.json: ' + r.status);
        const index = await r.json();
        const packs = new HostedPacks();
        packs.base = base;
        packs.motions = index.motions || [];
        for (const [name, path, seed, size, compressed] of index.files || []) packs.entries.set(name, { name, path, seed, size, compressed });
        return packs;
      }

      async raw(name) {
        const e = this.entries.get(name);
        if (!e) return null;
        const url = this.base + e.path.split('/').map(encodeURIComponent).join('/');
        // A dropped connection is tried again (twice); an HTTP error isn't.
        for (let tries = 0; ; tries++) {
          let r;
          try { r = await fetch(url); } catch (err) { if (tries < 2) continue; throw err; }
          if (!r.ok) throw new Error(name + ': ' + r.status);
          return { data: new Uint8Array(await r.arrayBuffer()), compressed: !!e.compressed };
        }
      }
    }

    // The packs as the converters read them, noting each file read (for a hosted copy's models). Textures aren't
    // decoded (noDecode): only which files they are matters.
    class RecordingPacks extends Packs {
      constructor(inner) {
        super();
        this.inner = inner;
        this.entries = inner.entries;
        this.seen = new Set();
        this.noDecode = true;
      }

      raw(name) {
        if (this.entries.has(name)) this.seen.add(name);
        return this.inner.raw(name);
      }
    }
    classes = { HostedPacks, RecordingPacks };
    return classes;
  }
  const isHosted = (packs) => !!classes && packs instanceof classes.HostedPacks;

  // ======================================================================================
  // .pmg meshes, .frm skeletons, .dds textures
  // ======================================================================================

  function cstr(u8, at, n) {
    let end = at;
    const stop = Math.min(at + n, u8.length);
    while (end < stop && u8[end] !== 0) end++;
    return enc.decode(u8.subarray(at, end));
  }

  // The parts of a mesh file; stops at the first chunk it can't read.
  function parsePmg(d) {
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    const parts = [];
    if (d.length < 0x8e || cstr(d, 0, 4) !== 'pmg') return { parts, problem: 'not a pmg' };
    let pos = dv.getUint32(6, true), problem = null;
    const f32 = (at, n) => { const a = new Float32Array(n); for (let i = 0; i < n; i++) a[i] = dv.getFloat32(at + 4 * i, true); return a; };
    while (pos + 10 <= d.length) {
      if (cstr(d, pos, 4) !== 'pm!') { problem = 'no chunk at ' + pos; break; }
      const version = dv.getUint16(pos + 4, true), chunkSize = dv.getUint32(pos + 6, true), S = pos;
      const m = {};
      let p, counts;
      try {
        if (version === 0x0701) {
          m.bone = cstr(d, S + 0x0a, 32); m.name = cstr(d, S + 0x2a, 128); m.joint = cstr(d, S + 0xaa, 32);
          m.stat = cstr(d, S + 0xca, 32); m.unknown = cstr(d, S + 0xea, 32); m.colormap = cstr(d, S + 0x10a, 32);
          m.minor = f32(S + 0x12a, 16); m.major = f32(S + 0x16a, 16); m.part = dv.getInt32(S + 0x1aa, true); m.texture = cstr(d, S + 0x1b6, 32);
          p = S + 0x1d6;
          counts = []; for (let i = 0; i < 30; i++) counts.push(dv.getInt32(p + 4 * i, true));
          p += 120;
        } else if (version === 0x0002) {
          m.minor = f32(S + 0x0a, 16); m.major = f32(S + 0x4a, 16); m.part = dv.getInt32(S + 0x8a, true);
          p = S + 0x96;
          counts = []; for (let i = 0; i < 30; i++) counts.push(dv.getInt32(p + 4 * i, true));
          p += 120;
          const names = [];
          for (let k = 0; k < 7; k++) {
            const n = dv.getUint32(p, true);
            if (n > 1024 || p + 4 + n > d.length) throw new Error('bad names');
            names.push(cstr(d, p + 4, n)); p += 4 + n;
          }
          [m.bone, m.name, m.joint, m.stat, m.unknown, m.colormap, m.texture] = names;
        } else { problem = 'chunk version 0x' + version.toString(16); break; }
        // The tag before the box is 64; a few files (tool_c4_tinyharp_01) have 0 there, same layout.
        const tag = dv.getInt32(p, true);
        if (tag !== 64 && tag !== 0) { problem = 'no 64 tag'; break; }
      } catch (e) { problem = 'short chunk'; break; }
      p += 4 + 60; // the tag, then the box
      const [faceCount, vertexCount] = [counts[10], counts[14]];
      const [faceBytes, stripBytes, vertexBytes, skinBytes] = counts.slice(25, 29);
      if ([faceCount, vertexCount, faceBytes, stripBytes, vertexBytes, skinBytes].some((x) => x < 0) || faceCount * 2 > faceBytes ||
          vertexCount * 36 > vertexBytes || p + faceBytes + stripBytes + vertexBytes > d.length) { problem = 'bad counts'; break; }
      m.faces = new Uint16Array(faceCount - (faceCount % 3));
      for (let i = 0; i < m.faces.length; i++) m.faces[i] = dv.getUint16(p + 2 * i, true);
      p += faceBytes + stripBytes;
      m.vertexCount = vertexCount;
      m.vertexAt = p;
      // Skin entries (16 bytes: i32 vertex, i32 0, f32 weight, i32 flag) follow the vertices.
      m.skinCount = counts[15] > 0 && counts[15] * 16 <= skinBytes && p + vertexBytes + skinBytes <= d.length ? counts[15] : 0;
      m.skinAt = p + vertexBytes;
      parts.push(m);
      if (!chunkSize) break;
      pos = S + chunkSize;
    }
    groupParts(d, dv, parts);
    return { parts, problem, dv };
  }

  // The header's subgroup table, as the client reads it (CMeshGroup::Read): after "pmg\0", a u16
  // version and the u32 header size come char[128] group and u32 subgroup count, then per subgroup
  // char[32] key1 (the state: f1, x1, h3, fistl, r, e, cover...), char[32] key2 (the size: s, m, l,
  // or none) and u32 count, then count entries of u32 chunk size, char[32] name, char[128] full
  // name, char[32] joint, u32 slot, u32 0, in the chunks' order. Each part gets key1, key2, sub (its
  // subgroup's index) and linked: the game links a part only when its entry's name is its joint
  // (helper boxes, footsteps and head01-style copies aren't). When the table doesn't line up with
  // the chunks read (a few files), each part keeps its own state, no size, and is linked.
  function groupParts(d, dv, parts) {
    const entries = [];
    try {
      const end = dv.getUint32(6, true), subs = dv.getUint32(138, true);
      let p = 142;
      for (let s = 0; s < subs && s < 4096 && p + 68 <= end; s++) {
        const key1 = cstr(d, p, 32).toLowerCase(), key2 = cstr(d, p + 32, 32).toLowerCase(), count = dv.getUint32(p + 64, true);
        p += 68;
        for (let k = 0; k < count && p + 204 <= end; k++, p += 204) {
          entries.push({ key1, key2, sub: s, name: cstr(d, p + 4, 32), joint: cstr(d, p + 164, 32) });
        }
      }
    } catch (e) { entries.length = 0; }
    const lined = entries.length === parts.length && entries.every((e, i) => e.name.toLowerCase() === parts[i].bone.toLowerCase());
    parts.forEach((m, i) => {
      const e = lined ? entries[i] : null;
      m.key1 = e ? e.key1 : (m.stat || '').toLowerCase();
      m.key2 = e ? e.key2 : '';
      m.sub = e ? e.sub : -1;
      m.linked = e ? e.name === e.joint : true;
    });
  }

  // {bone name without its _/- prefix, lower case: bone->model matrix (#2, row-major)}.
  function parseFrm(d) {
    const bones = new Map();
    if (!d || d.length < 8 || cstr(d, 0, 4) !== 'pf!') return bones;
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    const count = dv.getUint16(6, true);
    for (let i = 0; i < count; i++) {
      const R = 8 + i * 0x104;
      if (R + 0x104 > d.length) break;
      const m = new Float64Array(16);
      for (let k = 0; k < 16; k++) m[k] = dv.getFloat32(R + 0x40 + 4 * k, true);
      bones.set(cstr(d, R + 0xc0, 32).replace(/^[_-]+/, '').toLowerCase(), m);
    }
    return bones;
  }

  // The info line's note of parts left out for want of their bone.
  const hiddenNote = (n) => (n ? ', ' + n + ' left out (no bone to place ' + (n === 1 ? 'it' : 'them') + ' on)' : '');

  // A part's bone name as the skeletons key it.
  const boneName = (name) => name.replace(/^[\s_-]+/, '').trim().toLowerCase();

  // The skeleton's bone for a part (bones: a Map or Set by key): its own, else the same name but for
  // a number or _suffix (head01 -> head, arm2l01 -> arm2l, leg2l_s -> leg2l, chest -> chest1); null
  // when the skeleton has none: helper objects (footsteps, box01) and bones another skeleton has.
  function boneKey(bones, name) {
    const k = boneName(name);
    if (bones.has(k)) return k;
    const m = /^(.*[a-z])(\d+|_[a-z]+)$/.exec(k);
    if (m && bones.has(m[1])) return m[1];
    return bones.has(k + '1') ? k + '1' : null;
  }

  function mul4(a, b) {
    const o = new Float64Array(16);
    for (let r = 0; r < 4; r++) for (let c = 0; c < 4; c++) {
      let s = 0;
      for (let k = 0; k < 4; k++) s += a[r * 4 + k] * b[k * 4 + c];
      o[r * 4 + c] = s;
    }
    return o;
  }
  const IDENTITY = new Float64Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]);

  // DDS textures: packs.js decodeDds, as Kanan decodes them.

  // ======================================================================================
  // Materials (material/_define): texture -> D3D9 render state, for WebGL
  // ======================================================================================

  const ATTR = /([\w\-]+)\s*=\s*"([^"]*)"/g;
  const attrs = (s) => { const o = {}; for (const m of s.matchAll(ATTR)) o[m[1]] = m[2]; return o; };

  function summarizeRenderState(t) {
    const out = {};
    const b = /<Blend\s+Enable="(\w+)"\s+Op="D3DBLENDOP_(\w+)"\s+Src="D3DBLEND_(\w+)"\s+Dest="D3DBLEND_(\w+)"/.exec(t);
    out.blend = b && b[1] === 'true' ? [b[3], b[4], b[2]] : null;
    const z = /<ZDepthTest\s+Enable="(\w+)"[^>]*WriteEnable="(\w+)"/.exec(t);
    out.ztest = z ? z[1] === 'true' : true;
    out.zwrite = z ? z[2] === 'true' : true;
    // D3D passes a pixel when alpha * 255 > Ref.
    const a = /<AlphaTest\s+Enable="(\w+)"\s+Ref="(\w+)"/.exec(t);
    out.alphatest = a && a[1] === 'true' ? (parseInt(a[2], 16) + 0.5) / 255 : null;
    const c = /CullMode="D3DCULL_(\w+)"/.exec(t);
    out.cull = c ? c[1] : 'CCW';
    // How the first texture stage mixes the texture with the dyed color, and the second stage
    // (the gloss map: MODULATE4X with Sphere or Gloss coordinates, or DISABLE).
    const stages = [...t.matchAll(/<TssList\s+ColorOp="D3DTOP_(\w+)"[^>]*?TexCoord="(\w+)"/g)];
    out.op = stages[0] ? stages[0][1] : '';
    out.op1 = stages[1] ? stages[1][1] : 'DISABLE';
    out.tc1 = stages[1] ? stages[1][2] : '';
    // The first sampler's addressing (CLAMP on character, monster and item states).
    const ss = /<SsList\s+AddressU="D3DTADDRESS_(\w+)"\s+AddressV="D3DTADDRESS_(\w+)"/.exec(t);
    out.addr = ss ? [ss[1], ss[2]] : ['WRAP', 'WRAP'];
    // Whether the vertex alpha is part of the alpha (the alpha test and blending see): only when
    // the first stage's alpha takes the diffuse (effects, alpha-blend, distant meshes: 13 of 60).
    // Everything else takes the texture's alpha alone (AlphaOp SELECTARG1 TEXTURE).
    const ao = /AlphaOp="D3DTOP_(\w+)"\s+AlphaArg1="D3DTA_(\w+)"\s+AlphaArg2="D3DTA_(\w+)"/.exec(t);
    out.vertexAlpha = !!ao && (ao[1] === 'SELECTARG1' ? ao[2] === 'DIFFUSE' : ao[1] === 'SELECTARG2' ? ao[3] === 'DIFFUSE' : ao[2] === 'DIFFUSE' || ao[3] === 'DIFFUSE');
    return out;
  }

  // { textures: Map(texture -> [material, render state name, gloss texture, receives the main
  // light]), states: Map(name -> summary) }. Kept in IndexedDB, keyed by the define files'
  // versions, as it reads ~650 small files.
  function materials(packs) {
    return packs.once('materials', async () => {
      const names = [...packs.entries.keys()].filter((n) => n.startsWith('material/_define/') && n.endsWith('.xml'));
      const stamp = 'mat3:' + names.map((n) => { const e = packs.entries.get(n); return n + ':' + e.seed + ':' + e.size; }).join(';');
      const stampHash = await hash(stamp);
      const saved = await idbGet('materials').catch(() => null);
      if (saved && saved.stamp === stampHash) return { textures: new Map(saved.textures), states: new Map(saved.states) };
      const textures = new Map(), states = new Map();
      const texts = await Promise.all(names.map((n) => packs.text(n).then((t) => [n, t || ''])));
      for (const [n, t] of texts) {
        if (n.startsWith('material/_define/render_state/')) {
          states.set(n.split('/').pop().slice(0, -4), summarizeRenderState(t));
        } else if (n.startsWith('material/_define/material/')) {
          const head = /<xml\b([^>]*)>/.exec(t);
          if (!head) continue;
          const a = attrs(head[1]);
          for (const m of t.matchAll(/<TextureDesc\s+Name="([^"]*)"/g)) {
            const k = m[1].toLowerCase();
            if (!textures.has(k)) textures.set(k, [a.Name || '', a.RenderState || '', a.GlossTexture || '', a.ReceiveMainLight === 'true']);
          }
        }
      }
      idbSet('materials', { stamp: stampHash, textures: [...textures], states: [...states] }).catch(() => {});
      return { textures, states };
    });
  }

  async function hash(s) {
    if (root.crypto && crypto.subtle) {
      const h = await crypto.subtle.digest('SHA-1', new TextEncoder().encode(s));
      return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, '0')).join('');
    }
    return s.length + ':' + s.slice(-64);
  }

  function renderState(mats, texture) {
    const m = mats.textures.get(texture.toLowerCase());
    if (!m) return null;
    const st = mats.states.get(m[1].toLowerCase()) || summarizeRenderState('');
    return { material: m[0], rs: m[1], gloss: m[2] || '', mainLight: !!m[3], ...st };
  }

  // ======================================================================================
  // Items
  // ======================================================================================

  const FIELDS = [
    { field: 'File_FieldMesh', label: 'Dropped', folder: '/item/', skeleton: null },
    { field: 'File_MaleMesh', label: 'Worn (male)', folder: '/male/', skeleton: 'gfx/char/human/male/male_framework.frm' },
    { field: 'File_FemaleMesh', label: 'Worn (female)', folder: '/female/', skeleton: 'gfx/char/human/female/female_framework.frm' },
    { field: 'File_GiantMesh', label: 'Worn (giant)', folder: '/giant/', skeleton: 'gfx/char/giant/male/giant_male_framework.frm' },
    { field: 'File_FemaleGiantMesh', label: 'Worn (giant female)', folder: '/giant/', skeleton: 'gfx/char/giant/female/giant_female_framework.frm' },
  ];

  const ENTITIES = { '&lt;': '<', '&gt;': '>', '&quot;': '"', '&apos;': "'", '&amp;': '&' };
  const decodeEntities = (s) => s.replace(/&(lt|gt|quot|apos|amp);/g, (m) => ENTITIES[m]);

  // An item's <Mabi_Item> attributes from itemdb.xml's text (the last row with that ID); with tag,
  // another table's rows (race.xml's <Race>).
  function findRow(text, id, tag) {
    tag = '<' + (tag || 'Mabi_Item');
    const needle = ' ID="' + id + '"';
    let found = null;
    for (let at = text.indexOf(needle); at >= 0; at = text.indexOf(needle, at + 1)) {
      const open = text.lastIndexOf('<', at);
      if (text.startsWith(tag, open) && /\s/.test(text[open + tag.length])) {
        const end = text.indexOf('>', at);
        const row = {};
        for (const m of text.slice(open, end).matchAll(/(\w+)="([^"]*)"/g)) row[m[1]] = decodeEntities(m[2]);
        found = row;
      }
    }
    return found;
  }

  async function itemRow(packs, id) {
    const data = root.GAME_DATA && root.GAME_DATA.files && root.GAME_DATA.files['db/itemdb.xml'];
    if (data) return findRow(data, id);
    const text = await packs.once('itemdb', () => packs.text('db/itemdb.xml'));
    return text ? findRow(text, id) : null;
  }

  function byBase(packs) {
    return packs.once('bases', () => {
      const pmg = new Map(), dds = new Map(), frm = new Map(), set = new Map();
      for (const n of packs.entries.keys()) {
        // Models and skeletons: gfx/char/, and gfx/scene/ (prop races: crystals, barriers, the
        // Manekineko); a gfx/char/ file wins a name both have (findPmg, findRaceFile). Props' .set
        // files (propModel).
        const model = n.startsWith('gfx/char/') || n.startsWith('gfx/scene/');
        if (model && n.endsWith('.pmg')) push(pmg, n.slice(n.lastIndexOf('/') + 1, -4), n);
        else if (n.startsWith('material/') && n.endsWith('.dds')) push(dds, n.slice(n.lastIndexOf('/') + 1, -4), n);
        else if (model && n.endsWith('.frm')) push(frm, n.slice(n.lastIndexOf('/') + 1, -4), n);
        else if (model && n.endsWith('.set')) push(set, n.slice(n.lastIndexOf('/') + 1, -4), n);
      }
      return { pmg, dds, frm, set };
    });
  }
  const push = (m, k, v) => { const a = m.get(k); if (a) a.push(v); else m.set(k, [v]); };
  // Pack names compare as the game writes them, with '\'.
  const bs = (s) => s.replace(/\//g, '\\');
  const cmp = (a, b) => (a < b ? -1 : a > b ? 1 : 0);

  // The model files of that name, best first (in the field's folder, not chapter4, gfx/char/).
  function findPmgs(bases, mesh, f) {
    const paths = bases.pmg.get(mesh.toLowerCase());
    if (!paths) return [];
    return [...paths].sort((a, b) => (!a.includes(f.folder)) - (!b.includes(f.folder)) ||
      a.includes('chapter4') - b.includes('chapter4') || (!a.startsWith('gfx/char/')) - (!b.startsWith('gfx/char/')) || cmp(bs(a), bs(b)));
  }
  const findPmg = (bases, mesh, f) => findPmgs(bases, mesh, f)[0] || null;

  function findDds(bases, name) {
    const paths = bases.dds.get(name.toLowerCase());
    if (!paths) return null;
    return [...paths].sort((a, b) => (!a.startsWith('material/char')) - (!b.startsWith('material/char')) || cmp(bs(a), bs(b)))[0];
  }

  // Dye defaults: App_ColorN is a color id in db/colortable.data, naming a color map
  // (data/color/*.raw, RGBA). The item icons use the map's first pixel (MabiWiki.cpp); the game
  // picks among the map's pixels per item. presets: a few of the map's other colors.
  function colorMaps(packs) {
    return packs.once('colormaps', async () => {
      const d = await packs.read('db/colortable.data');
      const out = new Map();
      if (!d) return out;
      const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
      const schema = dv.getUint32(16, true), poolSize = dv.getUint32(20, true), dataSize = dv.getUint32(24, true);
      const pool = 16 + schema, records = pool + poolSize;
      for (let i = 0; i < Math.floor(dataSize / 72); i++) {
        const at = records + 72 * i;
        if (at + 72 > d.length) break;
        const id = dv.getUint32(at, true), name = dv.getInt32(at + 4, true);
        if (name < 0 || name >= poolSize) break;
        const path = cstr(d, pool + name, d.length - pool - name).toLowerCase().replace(/\\/g, '/');
        if (!path.startsWith('data/color/')) break; // the distortion maps that follow
        out.set(id, path.slice(5));
      }
      return out;
    });
  }

  async function dyeOf(packs, slot, value) {
    const out = { slot, color: null, rgb: null, presets: [] };
    if (!/^\d+$/.test(value || '')) return out;
    out.color = +value;
    const path = (await colorMaps(packs)).get(out.color);
    const raw = path && await packs.once('raw:' + path, () => packs.read(path));
    if (!raw || raw.length < 4) return out;
    // A pixel's color as the client reads it (CMGSColorMap::PickRGBRandomColor): bytes 0-2 as RGB
    // when the 4th byte is 0, else in the other order (ice, elf_cloth, metal_bright01: 13 maps).
    const hex = (p) => '#' + (raw[p * 4 + 3] ? [2, 1, 0] : [0, 1, 2]).map((c) => raw[p * 4 + c].toString(16).padStart(2, '0')).join('');
    out.rgb = hex(0);
    const pixels = raw.length >> 2;
    for (let i = 0; i < 64 && out.presets.length < 8; i++) {
      const c = hex(Math.floor(i * pixels / 64));
      if (!out.presets.includes(c)) out.presets.push(c);
    }
    return out;
  }

  // A material's gloss map: material/glossmap/<name>.dds (64x64, 0x80 the neutral value).
  function glossTexture(packs, bases, name) {
    return packs.once('gloss:' + name.toLowerCase(), async () => {
      const list = bases.dds.get(name.toLowerCase()) || [];
      const path = list.find((p) => p.includes('/glossmap/')) || list[0];
      if (!path) return null;
      const data = await packs.read(path);
      return packs.noDecode ? null : decodeDds(data);
    });
  }

  async function texture(packs, bases, mats, name) {
    return packs.once('tex:' + name.toLowerCase(), async () => {
      const path = findDds(bases, name);
      if (!path) return { image: null, info: null };
      const data = await packs.read(path);
      const img = packs.noDecode ? null : decodeDds(data);
      return { image: img, info: { path, fmt: img ? img.fmt : null, rs: renderState(mats, name) } };
    });
  }

  // One model file, placed with the skeleton when worn: model = BoneWorld(bone) x minor x v; dropped
  // (no skeleton): inverse(root turn) x minor x v.
  // withSkin: each mesh also gets skin (per vertex: the skin entry's weight, or -1 without one).
  // skel may be a function of the file's parts that picks the skeleton. A part whose bone the
  // skeleton lacks (boneKey) is left out and counted in hidden: its place is relative to that bone.
  // statics (props, setStatics): { flags: Map(lower-case part name -> .set mesh-record flag), posed:
  // Map(bone key -> posed world) }. A part whose record has flag 0 is a static mesh: the game places it
  // once, on its bone's pose in the prop's animation at time zero (posed x minor), not the bind pose.
  // Like every part it needs its bone: with none in the skeleton it is left out (the game never links it).
  async function convertMesh(packs, path, skel, out, withSkin, statics) {
    const d = await packs.read(path);
    if (!d) return null;
    const { parts, problem, dv } = parsePmg(d);
    if (problem && root.console) console.warn('[MabiWikiViewer]', path, problem, '(kept ' + parts.length + ' parts)');
    if (typeof skel === 'function') skel = await skel(parts);
    // Dropped: the parts hang off a root bone (com) whose minor carries the root's own turn (90°
    // about Z on nearly every item). Without a skeleton that turn is undone, so the parts keep
    // their places relative to each other and the model stands the way it was made.
    const rootPart = !skel && parts.find((m) => boneName(m.bone) === 'com');
    const unturn = rootPart ? Float64Array.from([0, 1, 2, 3].flatMap((r) => (r < 3 ? [0, 1, 2].map((c) => rootPart.minor[c * 4 + r]).concat(0) : [0, 0, 0, 1]))) : null;
    const meshes = [];
    let hidden = 0;
    // The part's bone, else its joint (a part on bone "4" has joint "head").
    const keyOf = (m) => (skel ? boneKey(skel, m.bone) ?? (m.joint ? boneKey(skel, m.joint) : null) : null);
    for (const m of parts) {
      const key = keyOf(m);
      // Parts the game never links (helpers: footsteps discs, boxes) and parts with no bone here.
      if ((skel && key == null) || !m.linked || /footsteps/i.test(m.bone)) { hidden++; continue; }
      const n = m.vertexCount;
      const pos = new Float32Array(n * 3), nrm = new Float32Array(n * 3), uv = new Float32Array(n * 2), col = new Uint8Array(n * 4);
      // Placed as the game places it (CTransformer::Calc): BoneWorld x minor, or BoneWorld alone
      // when the minor is the identity. Without a skeleton (dropped), minor alone. A static part
      // (statics): its bone's time-zero pose x minor.
      const posed = skel && statics && statics.flags.get(m.name.toLowerCase()) === 0 ? statics.posed.get(key) : null;
      const X = posed ? mul4(posed, Float64Array.from(m.minor))
        : skel ? mul4(skel.get(key), Float64Array.from(m.minor)) : unturn ? mul4(unturn, Float64Array.from(m.minor)) : Float64Array.from(m.minor);
      // Left out too: a part whose matrix isn't finite (NaN minors: female_3rd_newbie_bss_flat),
      // and a part whose texture name is garbage bytes (13th-anniversary wings' black plane002).
      if (!X.every(Number.isFinite) || (m.texture && /[^\x20-\x7e]/.test(m.texture))) { hidden++; continue; }
      for (let i = 0; i < n; i++) {
        const at = m.vertexAt + i * 36;
        const x = dv.getFloat32(at, true), y = dv.getFloat32(at + 4, true), z = dv.getFloat32(at + 8, true);
        const nx = dv.getFloat32(at + 12, true), ny = dv.getFloat32(at + 16, true), nz = dv.getFloat32(at + 20, true);
        if (X) {
          pos[i * 3] = X[0] * x + X[1] * y + X[2] * z + X[3];
          pos[i * 3 + 1] = X[4] * x + X[5] * y + X[6] * z + X[7];
          pos[i * 3 + 2] = X[8] * x + X[9] * y + X[10] * z + X[11];
          const qx = X[0] * nx + X[1] * ny + X[2] * nz, qy = X[4] * nx + X[5] * ny + X[6] * nz, qz = X[8] * nx + X[9] * ny + X[10] * nz;
          const l = Math.max(1e-6, Math.sqrt(qx * qx + qy * qy + qz * qz));
          nrm[i * 3] = qx / l; nrm[i * 3 + 1] = qy / l; nrm[i * 3 + 2] = qz / l;
        } else {
          pos[i * 3] = x; pos[i * 3 + 1] = y; pos[i * 3 + 2] = z;
          nrm[i * 3] = nx; nrm[i * 3 + 1] = ny; nrm[i * 3 + 2] = nz;
        }
        const c = dv.getUint32(at + 24, true);
        col[i * 4] = (c >>> 16) & 255; col[i * 4 + 1] = (c >>> 8) & 255; col[i * 4 + 2] = c & 255; col[i * 4 + 3] = c >>> 24;
        uv[i * 2] = dv.getFloat32(at + 28, true); uv[i * 2 + 1] = dv.getFloat32(at + 32, true);
      }
      // Triangles, without any that point past the vertices.
      const tri = [];
      for (let i = 0; i < m.faces.length; i += 3) {
        if (m.faces[i] < n && m.faces[i + 1] < n && m.faces[i + 2] < n) tri.push(m.faces[i], m.faces[i + 1], m.faces[i + 2]);
      }
      if (m.texture && !(m.texture in out.textures)) {
        const t = await texture(packs, out.bases, out.mats, m.texture);
        out.textures[m.texture] = t.image;
        out.texinfo[m.texture] = t.info;
        // Its material's gloss map (material/glossmap/<GlossTexture>.dds), for the second stage.
        const g = t.info && t.info.rs && t.info.rs.gloss;
        if (g && !(('gloss:' + g) in out.textures)) out.textures['gloss:' + g] = await glossTexture(packs, out.bases, g);
      }
      // A static part follows no animation after it is placed (boneKey null).
      const mesh = { name: m.name, bone: m.bone, boneKey: posed ? null : key, stat: m.stat, part: m.part, colormap: m.colormap, texture: m.texture,
        key1: m.key1, key2: m.key2, sub: m.sub, pos, nrm, uv, col, idx: Uint16Array.from(tri), verts: n, tris: tri.length / 3 };
      // Welding entries (pleione::welding, 16 bytes: u16 vertex, pad, i32 group, f32 weight,
      // i32 smooth): skin[v] is the weight (-1 for none), weld[v] 1 when it takes part (group 0),
      // smooth[v] 1 when its normal and color are averaged too.
      if (withSkin) {
        mesh.skin = new Float32Array(n).fill(-1);
        mesh.weld = new Uint8Array(n);
        mesh.smooth = new Uint8Array(n);
        for (let k = 0; k < m.skinCount; k++) {
          const at = m.skinAt + 16 * k, v = dv.getUint16(at, true);
          if (v >= n) continue;
          mesh.skin[v] = dv.getFloat32(at + 8, true);
          mesh.weld[v] = dv.getInt32(at + 4, true) === 0 ? 1 : 0;
          mesh.smooth[v] = dv.getInt32(at + 12, true) ? 1 : 0;
        }
      }
      meshes.push(mesh);
    }
    return { file: path, meshes, hidden, skeleton: skel && skel.path || null };
  }

  // The skeleton an item's worn model goes on: the body's (frm), unless its parts use bones that one
  // lacks and the model has its own. Nao's clothes are made for her skeleton (gfx/char/human/nao),
  // and accessories with moving parts bring theirs (<model>_framework.frm). Candidates: the body's,
  // the Framework of a race.xml race with this DefaultMesh, those named after the model, those in
  // its folder. The one with the fewest of the parts' bones missing (same names only) wins; ties
  // go to the earlier. Each skeleton Map gets its path.
  async function itemSkeleton(packs, bases, path, parts, frm) {
    const load = (p) => packs.once('frm:' + p, async () => { const s = parseFrm(await packs.read(p)); s.path = p; return s; });
    const used = [...new Set(parts.map((m) => boneName(m.bone)))];
    const missing = (s) => used.filter((b) => !s.has(b)).length;
    const body = await load(frm);
    if (!missing(body)) return body;
    const dir = path.slice(0, path.lastIndexOf('/') + 1), base = path.slice(dir.length, -4);
    const races = (root.GAME_DATA && root.GAME_DATA.files && root.GAME_DATA.files['db/race.xml']) ||
      await packs.once('racedb', () => packs.text('db/race.xml')) || '';
    const cands = [];
    const at = new RegExp('<Race\\b[^>]*\\bDefaultMesh="' + base.replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '"[^>]*>', 'gi');
    for (const m of races.matchAll(at)) {
      const fw = /\bFramework="([^"]*)"/.exec(m[0]);
      const p = fw && findRaceFile(bases.frm.get(fw[1].trim().toLowerCase()), path);
      if (p) cands.push(p);
    }
    // Named after the model (female_nao_toy_h01 -> female_nao_toy_h01_framework), or the model
    // named after it (acc_c4_shoulderpuppet_macha -> acc_c4_shoulderpuppet, longest first); then
    // the rest of its folder, whose generic bones (Bone01) would otherwise fit anything.
    const named = [], prefix = [], near = [];
    for (const [name, list] of bases.frm) {
      for (const p of list) {
        if (name.startsWith(base)) named.push(p);
        else if (name.length >= 6 && base.startsWith(name)) prefix.push(p);
        else if (p.startsWith(dir) && !p.slice(dir.length).includes('/')) near.push(p);
      }
    }
    const byName = (a, b) => cmp(bs(a), bs(b));
    const longest = (a, b) => b.length - a.length || byName(a, b);
    cands.push(...named.sort(byName), ...prefix.sort(longest), ...near.sort(byName));
    let best = body, fewest = missing(body);
    for (const p of cands) {
      if (p === frm) continue;
      const s = await load(p), n = missing(s);
      if (n < fewest) { best = s; fewest = n; }
      if (!n) break;
    }
    return best;
  }

  // The item's models: { id, colors, dyes, variants, textures: { name: { w, h, px } | null },
  // texinfo }, or null when the item isn't in itemdb.
  async function convertItem(packs, id, row) {
    row = row || await itemRow(packs, id);
    if (!row) return null;
    const bases = byBase(packs), mats = await materials(packs);
    const out = { id: String(id), colors: [1, 2, 3].map((i) => row['App_Color' + i] || ''), dyes: [], variants: [], wearType: row.App_WearType || '',
      textures: {}, texinfo: {}, bases, mats };
    const files = [];
    for (const f of FIELDS) {
      let v = (row[f.field] || '').trim();
      if (v.startsWith(';')) v = v.slice(1).split('<')[0];
      if (!v) continue;
      // The best file of that name that reads (a female copy of tool_c4_tinyharp_01 has no parts;
      // the shared one does).
      let path = null, mesh = null;
      for (const p of findPmgs(bases, v, f)) {
        const skel = f.skeleton ? (parts) => itemSkeleton(packs, bases, p, parts, f.skeleton) : null;
        const m = await convertMesh(packs, p, skel, out);
        if (m && (m.meshes.length || m.hidden)) { path = p; mesh = m; break; }
      }
      if (!path || files.includes(path)) continue;
      files.push(path);
      out.variants.push({ field: f.field, label: f.label, mesh: v, ...mesh });
    }
    for (let i = 0; i < 3; i++) out.dyes.push(await dyeOf(packs, i + 1, out.colors[i]));
    delete out.bases; delete out.mats;
    return out;
  }

  // ======================================================================================
  // Races (monsters, NPCs)
  // ======================================================================================
  // A db/race.xml row names its model: DefaultMesh (a .pmg under gfx/char/, by file name) on the
  // skeleton Framework (a .frm, by file name). FirstCreateStript gives what it wears and holds:
  // additemex(id:A|B col1:RRGGBB col2:.. col3:.. pocket:N) puts item A in pocket N with those
  // colors, setface(sc:N ...) sets the skin color (a db/color.xml palette number) and ft the face
  // item. Races without a DefaultMesh (imps, ogres, zombies) get their body from the pocket 5 item.
  // The body colors themselves come from the server: race.xml has none, so monsters show their
  // textures' own grey unless options.colors gives them (pets: db/pet/petracedesc.xml has them).

  // The pockets drawn, and what they are.
  const POCKETS = { 3: 'Face', 4: 'Hair', 5: 'Clothes', 6: 'Gloves', 7: 'Shoes', 8: 'Head', 9: 'Robe', 10: 'Right hand', 12: 'Left hand' };

  async function raceRow(packs, id) {
    const data = root.GAME_DATA && root.GAME_DATA.files && root.GAME_DATA.files['db/race.xml'];
    if (data) return findRow(data, id, 'Race');
    const text = await packs.once('racedb', () => packs.text('db/race.xml'));
    return text ? findRow(text, id, 'Race') : null;
  }

  // 'RRGGBB', 'AARRGGBB' or '#rrggbb' -> '#rrggbb', else null.
  const hexColor = (v) => (/^#?(?:[0-9a-f]{2})?[0-9a-f]{6}$/i.test(v || '') ? '#' + v.slice(-6).toLowerCase() : null);

  // FirstCreateStript -> { items: [{ id, pocket, colors }], face: { sc, ft, ... } }. Alternatives
  // (id:A|B, sc:16|17) are picked at random by the server; the first is taken.
  function parseRaceScript(s) {
    const out = { items: [], face: {} };
    s = decodeEntities(s || '');
    for (const m of s.matchAll(/additemex\s*\(([^)]*)\)/gi)) {
      const a = {};
      for (const kv of m[1].matchAll(/(\w+)\s*:\s*([^\s)]+)/g)) a[kv[1].toLowerCase()] = kv[2].split('|')[0];
      if (!/^\d+$/.test(a.id || '') || !(a.pocket in POCKETS)) continue;
      out.items.push({ id: a.id, pocket: +a.pocket, colors: [a.col1, a.col2, a.col3].map(hexColor) });
    }
    for (const m of s.matchAll(/setface\s*\(([^)]*)\)/gi)) {
      for (const kv of m[1].matchAll(/(\w+)\s*:\s*([^\s)]+)/g)) out.face[kv[1].toLowerCase()] = kv[2].split('|')[0];
    }
    return out;
  }

  // db/color.xml's system palette: number -> '#rrggbb' (skin colors, setface sc).
  function sysPalette(packs) {
    return packs.once('syspalette', async () => {
      const t = await packs.text('db/color.xml');
      const out = new Map();
      for (const m of (t || '').matchAll(/<MabiSysPalette\s[^>]*?number="(\d+)"\s+RGB="(\w+)"/g)) out.set(+m[1], hexColor(m[2]));
      return out;
    });
  }

  // A pet's colors from db/pet/petracedesc.xml (Color1..3), or null.
  async function petColors(packs, id) {
    const t = await packs.once('petracedesc', () => packs.text('db/pet/petracedesc.xml'));
    const m = t && new RegExp('<Pet\\s+ID\\s*=\\s*"' + id + '"[^>]*>').exec(t);
    if (!m) return null;
    const a = {};
    for (const kv of m[0].matchAll(/(\w+)\s*=\s*"([^"]*)"/g)) a[kv[1]] = kv[2];
    const c = [a.Color1, a.Color2, a.Color3].map(hexColor);
    return c.some(Boolean) ? c.map((x) => x || '#808080') : null;
  }

  // Every bone of a .frm in file order: { key (name without _/-, lower case), parent, world (#2),
  // local (#3, bone relative to its parent) }. world(b) = world(parent) x local(b).
  function parseSkeleton(d) {
    const bones = [];
    if (!d || d.length < 8 || cstr(d, 0, 4) !== 'pf!') return bones;
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    const count = dv.getUint16(6, true);
    for (let i = 0; i < count; i++) {
      const R = 8 + i * 0x104;
      if (R + 0x104 > d.length) break;
      const mat = (o) => { const m = new Float64Array(16); for (let k = 0; k < 16; k++) m[k] = dv.getFloat32(R + o + 4 * k, true); return m; };
      const parent = d[R + 0xe1];
      bones.push({ key: cstr(d, R + 0xc0, 32).replace(/^[_-]+/, '').toLowerCase(), parent: parent === 255 ? -1 : parent,
        world: mat(0x40), local: mat(0x80) });
    }
    return bones;
  }

  function findRaceFile(list, near) {
    if (!list) return null;
    const dir = near ? near.slice(0, near.lastIndexOf('/') + 1) : '';
    return [...list].sort((a, b) => (!a.startsWith(dir)) - (!b.startsWith(dir)) || a.includes('/custom/') - b.includes('/custom/') ||
      cmp(bs(a), bs(b)))[0];
  }

  // Which of an item's worn models a race wears: by its skeleton, then its gender (1 female).
  function itemFieldsFor(framework, gender) {
    const f = framework.toLowerCase(), female = gender === '1' || /female/.test(f);
    if (/giant/.test(f)) return female ? ['File_FemaleGiantMesh', 'File_GiantMesh'] : ['File_GiantMesh', 'File_FemaleGiantMesh'];
    return female ? ['File_FemaleMesh', 'File_MaleMesh'] : ['File_MaleMesh', 'File_FemaleMesh'];
  }

  // The race's model: { id, name, raceDesc, framework, mesh, bones, groups, parts, textures, texinfo,
  // dyes (the body's colors 1-3), skin, hair, colorSource }, or null when the race isn't in race.xml.
  // groups[0] is the race's own mesh; the others are the items it wears. Each part has group,
  // boneIndex, visible, and its vertices in the skeleton's bind pose.
  async function convertRace(packs, id, row, opts) {
    opts = opts || {};
    row = row || await raceRow(packs, id);
    if (!row) return null;
    const bases = byBase(packs), mats = await materials(packs);
    const out = { id: String(row.ID || id), name: row.EnglishName || '', raceDesc: (row.RaceDesc || '').toLowerCase(),
      framework: null, mesh: null, bones: [], groups: [], parts: [], hidden: 0, textures: {}, texinfo: {}, bases, mats };
    const fw = findRaceFile(bases.frm.get((row.Framework || '').trim().toLowerCase()));
    const meshName = (row.DefaultMesh || '').trim();
    const meshPath = meshName && findRaceFile(bases.pmg.get(meshName.toLowerCase()), fw);
    out.framework = fw && findRaceFile(bases.frm.get((row.Framework || '').trim().toLowerCase()), meshPath);
    out.mesh = meshPath || null;
    const skelData = out.framework ? await packs.read(out.framework) : null;
    out.bones = parseSkeleton(skelData);
    const skel = parseFrm(skelData);
    const boneIndex = new Map(out.bones.map((b, i) => [b.key, i]));
    const script = parseRaceScript(row.FirstCreateStript);
    const palette = await sysPalette(packs);
    const humanoid = /(^|_)(fe)?male_framework/i.test(row.Framework || '');

    // Body colors: options.colors, else the pet's, else none (the texture as it is).
    const given = (opts.colors || []).map(hexColor);
    const pet = given.some(Boolean) ? null : await petColors(packs, out.id);
    out.colorSource = given.some(Boolean) ? 'options' : pet ? 'pet' : 'none';
    // A monster's body colors come from the server (its Extra colors); with none, mid grey.
    const body = [0, 1, 2].map((i) => given[i] || (pet && pet[i]) || '#808080');
    out.dyes = body.map((rgb, i) => ({ slot: i + 1, color: null, rgb, presets: [] }));
    const sc = script.face.sc != null ? palette.get(+script.face.sc) : null;
    out.skin = hexColor(opts.skin) || sc || (humanoid ? FIXED.skin : '#808080');
    out.skinSource = opts.skin ? 'options' : sc ? 'setface' : humanoid ? 'default' : 'none';
    out.hair = FIXED.hair;

    const addParts = async (path, group) => {
      const mesh = await convertMesh(packs, path, skel, out, true);
      if (!mesh) return 0;
      out.hidden += mesh.hidden;
      for (const m of mesh.meshes) {
        const bi = boneIndex.has(m.boneKey) ? boneIndex.get(m.boneKey) : -1;
        out.parts.push({ ...m, group, file: path, boneIndex: bi, parentIndex: bi >= 0 ? out.bones[bi].parent : -1, visible: true });
      }
      return mesh.meshes.length;
    };
    out.groups.push({ kind: 'race', label: 'Body', mesh: meshName, file: meshPath, colors: body });
    if (meshPath) await addParts(meshPath, 0);

    // What it wears. A face item from setface ft, or the default face for human skeletons.
    const items = script.items.slice();
    if (/^\d+$/.test(script.face.ft || '')) items.unshift({ id: script.face.ft, pocket: 3, colors: [null, null, null] });
    else if (humanoid && !/giant/i.test(row.Framework)) items.unshift({ id: /female/i.test(row.Framework) ? '3900' : '4900', pocket: 3, colors: [null, null, null] });
    const fields = itemFieldsFor(row.Framework || '', row.Gender);
    const seen = new Set();
    const gearShows = row.EquipmentVisible !== 'false';
    for (const it of items) {
      if (seen.has(it.pocket)) continue;
      if (!gearShows && !(it.pocket === 5 && !meshPath)) continue;
      const irow = await itemRow(packs, it.id);
      if (!irow) continue;
      let mesh = '';
      for (const f of fields) { mesh = (irow[f] || '').trim(); if (mesh) break; }
      if (mesh.startsWith(';')) mesh = mesh.slice(1).split('<')[0];
      const path = mesh && findPmg(bases, mesh, { folder: /female/.test(fields[0].toLowerCase()) ? '/female/' : '/male/' });
      if (!path) continue;
      const defaults = [];
      for (let i = 0; i < 3; i++) defaults.push(it.colors[i] || (await dyeOf(packs, i + 1, irow['App_Color' + (i + 1)])).rgb || '#ffffff');
      const group = out.groups.length;
      out.groups.push({ kind: 'item', id: it.id, pocket: it.pocket, label: POCKETS[it.pocket], name: irow.Text_Name1 || irow.Text_Name0 || '',
        mesh, file: path, colors: defaults, anim: (/^\s*(tool_[\w]+)/i.exec(irow.App_AnimationType || '') || [])[1] || '',
        wearType: irow.App_WearType || '', xml: irow.XML || '', attrType: irow.Attr_Type || '' });
      if (await addParts(path, group)) seen.add(it.pocket);
      if (it.pocket === 4) out.hair = defaults[0];
    }
    raceVisibility(out);
    linkSeams(out);
    delete out.bases; delete out.mats;
    return out;
  }

  // --- Which parts show: the client's linking rules ---------------------------------------
  // The head's states by the hat's App_WearType (two digits; no hat is 01): [hat, face, hair], ''
  // for none (row 05: no hair under the hat). Rows 53-55 have a second form (y2) for a hat flag
  // that isn't known here.
  const HEAD_ROWS = { '01': ['x1', 'f1', 'h1'], '02': ['x1', 'f1', 'h2'], '03': ['x1', 'f2', 'h3'], '04': ['x1', 'f3', 'h4'],
    '05': ['x1', 'f4', ''], '06': ['x1', 'f1', 'h6'], '07': ['x1', 'f2', ''], '08': ['x1', 'f1', ''],
    '53': ['y1', 'f2', 'h3'], '54': ['y1', 'f3', 'h4'], '55': ['y1', 'f4', ''] };
  const headStates = (wearType) => (HEAD_ROWS[String(parseInt(wearType, 10) || 1).padStart(2, '0')] || HEAD_ROWS['01']).filter(Boolean);
  // A group's shown parts (CMeshGroup::GetMeshGroupStateIndex, per state): for each of its states
  // and '', the first subgroup with that state and the group's size. Parts the header didn't place
  // (sub -1) show when their state is one of them. Returns a flag per part.
  function pickSubgroups(parts, states, size) {
    const want = new Set(['', ...states]);
    const chosen = new Set();
    for (const s of want) {
      let first = Infinity;
      for (const p of parts) if (p.sub >= 0 && p.key1 === s && p.key2 === size && p.sub < first) first = p.sub;
      if (first < Infinity) chosen.add(first);
    }
    return parts.map((p) => (p.sub >= 0 ? chosen.has(p.sub) : want.has(p.key1)));
  }
  // Pockets' priority when two shown parts sit on the same bone and slot (the "+N" of the part):
  // the higher wins, a later group on a tie. The race's own body is lowest.
  const PRIORITY = { 5: 1, 9: 1, 8: 2, 4: 2, 6: 2, 7: 2, 10: 2, 12: 2, 3: 3 };
  // The size letter gloves (0) and shoes (1) take: the robe's XML glove_size / shoes_size (else l),
  // else the clothes' App_WearType letter, else s (nothing worn).
  function wornSize(model, which) {
    const robe = model.groups.find((g) => g.pocket === 9), clothes = model.groups.find((g) => g.pocket === 5);
    if (robe) {
      const m = new RegExp((which ? 'shoes' : 'glove') + '_size\\s*[=:]\\s*"?([a-z])', 'i').exec(robe.xml || '');
      return m ? m[1].toLowerCase() : 'l';
    }
    const t = clothes && (clothes.wearType || '').trim().toLowerCase();
    return t && /[a-z]/.test(t[which] || '') ? t[which] : 's';
  }

  // The race's shown parts, by the game's rules: per worn group, its states (the hat's head row; a
  // fist on a hand that holds something, else palm, fist for giants; tools held: r in the right
  // hand, lr in the left for one-handed weapons; robes with the hood down) and its size (gloves and
  // shoes: wornSize); then on each bone and slot only the highest-priority group's part.
  function raceVisibility(model) {
    const G = model.groups, giant = /giant/i.test(model.framework || '');
    const holds = { r: G.some((g) => g.pocket === 10), l: G.some((g) => g.pocket === 12) };
    const hat = G.find((g) => g.pocket === 8);
    const common = [...headStates(hat ? hat.wearType : ''), 'uncover',
      (holds.l || giant ? 'fist' : 'palm') + 'l', (holds.r || giant ? 'fist' : 'palm') + 'r'];
    const byGroup = new Map();
    model.parts.forEach((p) => { if (!byGroup.has(p.group)) byGroup.set(p.group, []); byGroup.get(p.group).push(p); });
    for (const [gi, parts] of byGroup) {
      const g = G[gi];
      const states = common.concat(g.pocket === 10 ? ['r'] : g.pocket === 12 ? [String(g.attrType) === '7' ? 'lr' : 'r'] : []);
      const size = g.pocket === 6 ? wornSize(model, 0) : g.pocket === 7 ? wornSize(model, 1) : '';
      let show = pickSubgroups(parts, states, size);
      // A group none of whose subgroups match (a monster body made only of alternatives): its first.
      if (!show.some(Boolean)) {
        const first = Math.min(...parts.map((p) => (p.sub >= 0 ? p.sub : Infinity)));
        show = parts.map((p) => p.sub === first || (first === Infinity && !p.key1));
      }
      parts.forEach((p, i) => { p.visible = show[i]; });
    }
    const top = new Map();
    model.parts.forEach((p, i) => {
      if (!p.visible) return;
      const k = p.boneKey + '#' + p.part, pr = PRIORITY[G[p.group].pocket] || 0, cur = top.get(k);
      if (!cur || pr >= cur.pr) top.set(k, { i, pr });
    });
    const keep = new Set([...top.values()].map((x) => x.i));
    model.parts.forEach((p, i) => { if (p.visible && !keep.has(i)) p.visible = false; });
  }

  // --- Poses ---------------------------------------------------------------------------
  // db/motion.xml maps a race to its animations: <Motion Class Classifier="Wolf/*/*/*"
  // Parent_Class> blocks of <Property Element="StandFriendly01" Value="monster/anim/wolf/..">.
  // The classifier is RaceDesc's first part / gender / right-hand tool / left-hand tool (the items'
  // App_AnimationType, e.g. Tool_A01); a missing element is looked up in Parent_Class. The value is
  // gfx/char/<value>.ani.
  const POSES = [['StandFriendly01', 'Standing'], ['StandOffensive01', 'Battle stance']];

  // { class (lower case): { classifier: [4 parts], parent, props: { element: value } } }; only the
  // POSES elements are kept. motion.xml is ~60 MB, so the index is kept in IndexedDB.
  function motionIndex(packs) {
    return packs.once('motions', async () => {
      if (packs.motions) return new Map(packs.motions); // a hosted copy's, made by the export
      const e = packs.entries.get('db/motion.xml');
      if (!e) return new Map();
      const stamp = 'mot1:' + e.seed + ':' + e.size;
      const saved = await idbGet('motions').catch(() => null);
      if (saved && saved.stamp === stamp) return new Map(saved.list);
      const t = await packs.text('db/motion.xml');
      const out = new Map();
      const want = new RegExp('<Property\\s+Category="\\w*"\\s+Element="(' + POSES.map((p) => p[0]).join('|') + ')"\\s+Value="([^"]*)"', 'g');
      let at = t ? t.indexOf('<Motion ') : -1;
      while (at >= 0) {
        const next = t.indexOf('<Motion ', at + 8);
        const block = t.slice(at, next < 0 ? t.length : next);
        const head = attrs(block.slice(0, block.indexOf('>')));
        const props = {};
        for (const m of block.matchAll(want)) props[m[1]] = m[2];
        if (head.Class) out.set(head.Class.toLowerCase(), { classifier: (head.Classifier || '').toLowerCase().split('/'), parent: (head.Parent_Class || '').toLowerCase(), props });
        at = next;
      }
      idbSet('motions', { stamp, list: [...out] }).catch(() => {});
      return out;
    });
  }

  // The motion class for a race and what it holds (best classifier match), or null.
  function findMotion(index, raceDesc, right, left) {
    const [kind, gender] = (raceDesc || '').toLowerCase().split('/');
    const seg = (c, v) => (c === v ? 2 : c === '*' ? 1 : 0);
    const best = (r, l) => {
      let found = null, score = 0;
      for (const [cls, m] of index) {
        const c = m.classifier;
        if (c[0] !== kind) continue;
        const s = [seg(c[1], gender || 'none') || (c[1] === 'none' || !gender || gender === 'none' ? 1 : 0), seg(c[2], r), seg(c[3], l)];
        if (s.includes(0)) continue;
        const total = s[0] + s[1] * 4 + s[2] * 4;
        if (total > score) { score = total; found = cls; }
      }
      return found;
    };
    return best(right, left) || best(right, 'none') || best('none', 'none');
  }

  function motionProp(index, cls, element) {
    for (let i = 0; cls && i < 32; i++) {
      const m = index.get(cls);
      if (!m) return null;
      if (m.props[element]) return m.props[element];
      cls = m.parent;
    }
    return null;
  }

  // The poses a race can show: [{ element, label, path }] (bind pose not included).
  async function racePoses(packs, model) {
    const index = await motionIndex(packs);
    const tool = (pocket) => ((model.groups.find((g) => g.pocket === pocket) || {}).anim || 'none').toLowerCase();
    const cls = findMotion(index, model.raceDesc, tool(10), tool(12));
    model.motion = cls;
    const out = [];
    for (const [element, label] of POSES) {
      const v = motionProp(index, cls, element);
      const path = v && 'gfx/char/' + v.toLowerCase().replace(/\\/g, '/') + '.ani';
      if (!path || !packs.has(path) || out.some((p) => p.path === path)) continue;
      // Some races' "standing" motion is a knocked-down, dying or dead one (the human soldiers lie
      // flat) or a cutscene's (Cai kneeling): not a pose to show them in.
      if (/natural_down|dying|_dead|_die\b|_death|cutscene/.test(path)) continue;
      if (!fitsSkeleton(model.bones, parseAni(await packs.read(path)))) continue;
      out.push({ element, label, path });
    }
    return out;
  }

  // Whether an animation was made for this skeleton: its first keys' bone offsets against the
  // skeleton's own. The motion class comes from RaceDesc, which some races share with a different
  // skeleton (wagon and joust horses, Krug, the Dark Commanders, the magic carpets): those animations
  // crumple the model, so they aren't offered. Off overall by over 45% of the bone lengths and on
  // most bones (one far-moved bone, as a ghost hiding its spare bodies, isn't enough).
  function fitsSkeleton(bones, tracks) {
    let off = 0, len = 0, bad = 0, n = 0;
    for (let i = 1; i < Math.min(tracks.length, bones.length); i++) {
      const k = tracks[i] && tracks[i][0];
      if (!k) continue;
      const b = bones[i].local, L = Math.hypot(b[3], b[7], b[11]), d = Math.hypot(k.pos[0] - b[3], k.pos[1] - b[7], k.pos[2] - b[11]);
      off += d; len += L; n++;
      if (d > Math.max(1, 0.25 * L)) bad++;
    }
    return !(len && off / len > 0.45 && bad > n / 2);
  }

  // .ani: "pa!\0", u16 version (0x0301), header to 0x3A (frame count, frame time, duration, bone
  // count...), then one track per bone in the skeleton's order (and sometimes one more): a 24-byte
  // head (u32, u16 key count at +4, junk, u32 key bytes at +12, two u32 flags) and 36-byte keys:
  // u32 time (ms), f32 position[3] (relative to the parent), f32 (0 or 1, unknown), f32 rotation
  // quaternion x, y, z, w. The first key of each track is at time 0.
  function parseAni(d) {
    const tracks = [];
    if (!d || d.length < 0x3a || cstr(d, 0, 4) !== 'pa!') return tracks;
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    let p = 0x3a;
    while (p + 24 <= d.length) {
      const n = dv.getUint16(p + 4, true);
      if (p + 24 + n * 36 > d.length) break;
      const keys = [];
      for (let i = 0; i < n; i++) {
        const k = p + 24 + 36 * i;
        keys.push({ time: dv.getUint32(k, true), pos: [0, 1, 2].map((j) => dv.getFloat32(k + 4 + 4 * j, true)),
          rot: [0, 1, 2, 3].map((j) => dv.getFloat32(k + 20 + 4 * j, true)) });
      }
      tracks.push(keys);
      p += 24 + n * 36;
    }
    return tracks;
  }

  function invert4(m) {
    const o = new Float64Array(16);
    const [a, b, c, d, e, f, g, h, i, j, k, l, mm, n, oo, p] = m;
    o[0] = f * k * p - f * l * oo - j * g * p + j * h * oo + n * g * l - n * h * k;
    o[4] = -e * k * p + e * l * oo + i * g * p - i * h * oo - mm * g * l + mm * h * k;
    o[8] = e * j * p - e * l * n - i * f * p + i * h * n + mm * f * l - mm * h * j;
    o[12] = -e * j * oo + e * k * n + i * f * oo - i * g * n - mm * f * k + mm * g * j;
    o[1] = -b * k * p + b * l * oo + j * c * p - j * d * oo - n * c * l + n * d * k;
    o[5] = a * k * p - a * l * oo - i * c * p + i * d * oo + mm * c * l - mm * d * k;
    o[9] = -a * j * p + a * l * n + i * b * p - i * d * n - mm * b * l + mm * d * j;
    o[13] = a * j * oo - a * k * n - i * b * oo + i * c * n + mm * b * k - mm * c * j;
    o[2] = b * g * p - b * h * oo - f * c * p + f * d * oo + n * c * h - n * d * g;
    o[6] = -a * g * p + a * h * oo + e * c * p - e * d * oo - mm * c * h + mm * d * g;
    o[10] = a * f * p - a * h * n - e * b * p + e * d * n + mm * b * h - mm * d * f;
    o[14] = -a * f * oo + a * g * n + e * b * oo - e * c * n - mm * b * g + mm * c * f;
    o[3] = -b * g * l + b * h * k + f * c * l - f * d * k - j * c * h + j * d * g;
    o[7] = a * g * l - a * h * k - e * c * l + e * d * k + i * c * h - i * d * g;
    o[11] = -a * f * l + a * h * j + e * b * l - e * d * j - i * b * h + i * d * f;
    o[15] = a * f * k - a * g * j - e * b * k + e * c * j + i * b * g - i * c * f;
    const det = a * o[0] + b * o[4] + c * o[8] + d * o[12];
    if (!det) return IDENTITY;
    for (let x = 0; x < 16; x++) o[x] /= det;
    return o;
  }

  // Per bone: posed world x inverse(bind world), from the first key of each track. A track's
  // rotation is the transpose of the frm's local rotation (checked against the wolf: same
  // translations, transposed axes). When the animation belongs to a bigger skeleton (goblins use
  // the human ones) only its rotations are used, with the race's own bone lengths.
  function poseMatrices(bones, tracks) {
    const own = tracks.length <= bones.length + 1;
    const world = [], out = [];
    bones.forEach((b, i) => {
      const k = tracks[i] && tracks[i][0];
      let local = b.local;
      if (k) {
        let [x, y, z, w] = k.rot;
        const l = Math.hypot(x, y, z, w) || 1;
        x /= -l; y /= -l; z /= -l; w /= l; // the conjugate: the transposed rotation
        const t = own ? k.pos : [b.local[3], b.local[7], b.local[11]];
        local = new Float64Array([
          1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), t[0],
          2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), t[1],
          2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), t[2],
          0, 0, 0, 1]);
      }
      world[i] = b.parent >= 0 && world[b.parent] ? mul4(world[b.parent], local) : local;
      out[i] = mul4(world[i], invert4(b.world));
    });
    return out;
  }

  // Welding, as the client does it (CFramework::UpdateNewMeshLinkage, then _Transform each frame):
  // parts move rigidly with their bones; then the copies of a vertex that sat within WELD of each
  // other in the bind pose, in different parts (anywhere on the model: body and everything worn),
  // chained through shared copies, all take their weights' mean position, and the smooth copies
  // their weighted normal. Only welding entries of group 0 take part; a vertex with no partner
  // stays rigid. model.welds: [[part index, vertex, weight, smooth], ...] per group of copies.
  const WELD = 0.2;
  function linkSeams(model) {
    const parts = model.parts, cell = (v) => Math.floor(v / WELD), grid = new Map(), ids = [];
    parts.forEach((p, pi) => {
      if (!p.visible || p.boneIndex < 0 || !p.weld) return;
      for (let i = 0; i < p.verts; i++) {
        if (!p.weld[i]) continue;
        const x = p.pos[i * 3], y = p.pos[i * 3 + 1], z = p.pos[i * 3 + 2], k = cell(x) + ',' + cell(y) + ',' + cell(z);
        if (!grid.has(k)) grid.set(k, []);
        grid.get(k).push(ids.length);
        ids.push([pi, i, x, y, z]);
      }
    });
    const up = ids.map((_, i) => i);
    const find = (i) => { while (up[i] !== i) { up[i] = up[up[i]]; i = up[i]; } return i; };
    ids.forEach(([pi, , x, y, z], a) => {
      for (let dx = -1; dx <= 1; dx++) for (let dy = -1; dy <= 1; dy++) for (let dz = -1; dz <= 1; dz++) {
        for (const b of grid.get((cell(x) + dx) + ',' + (cell(y) + dy) + ',' + (cell(z) + dz)) || []) {
          if (b <= a || ids[b][0] === pi) continue;
          const q = ids[b], d2 = (q[2] - x) ** 2 + (q[3] - y) ** 2 + (q[4] - z) ** 2;
          if (d2 < WELD * WELD) { const ra = find(a), rb = find(b); if (ra !== rb) up[ra] = rb; }
        }
      }
    });
    const sets = new Map();
    ids.forEach(([pi, i], a) => { const r = find(a); if (!sets.has(r)) sets.set(r, []); sets.get(r).push([pi, i, parts[pi].skin[i], parts[pi].smooth[i]]); });
    model.welds = [...sets.values()].filter((s) => s.length > 1);
  }

  // A part's vertices in a pose, rigid on its bone (mats null: the bind pose as it is).
  function posePart(part, mats) {
    if (!mats) return { pos: part.pos.slice(), nrm: part.nrm.slice() };
    const n = part.verts, pos = new Float32Array(n * 3), nrm = new Float32Array(n * 3);
    const X = part.boneIndex >= 0 && mats[part.boneIndex] || IDENTITY;
    for (let i = 0; i < n; i++) {
      const x = part.pos[i * 3], y = part.pos[i * 3 + 1], z = part.pos[i * 3 + 2];
      pos[i * 3] = X[0] * x + X[1] * y + X[2] * z + X[3];
      pos[i * 3 + 1] = X[4] * x + X[5] * y + X[6] * z + X[7];
      pos[i * 3 + 2] = X[8] * x + X[9] * y + X[10] * z + X[11];
      const nx = part.nrm[i * 3], ny = part.nrm[i * 3 + 1], nz = part.nrm[i * 3 + 2];
      const qx = X[0] * nx + X[1] * ny + X[2] * nz, qy = X[4] * nx + X[5] * ny + X[6] * nz, qz = X[8] * nx + X[9] * ny + X[10] * nz;
      const l = Math.max(1e-6, Math.hypot(qx, qy, qz));
      nrm[i * 3] = qx / l; nrm[i * 3 + 1] = qy / l; nrm[i * 3 + 2] = qz / l;
    }
    return { pos, nrm };
  }

  // Every part posed, then welded: posed[i] = { pos, nrm } for the model's parts (null when hidden).
  function poseModel(model, mats) {
    const posed = model.parts.map((p) => (p.visible && p.tris ? posePart(p, mats) : null));
    for (const set of model.welds || []) {
      let sw = 0;
      for (const c of set) if (posed[c[0]]) sw += c[2] > 0 ? c[2] : 0;
      const even = sw <= 0;
      const P = [0, 0, 0], N = [0, 0, 0];
      let total = 0;
      for (const [pi, i, w0, smooth] of set) {
        const q = posed[pi];
        if (!q) continue;
        const w = even ? 1 : Math.max(0, w0);
        total += w;
        for (let k = 0; k < 3; k++) { P[k] += w * q.pos[i * 3 + k]; if (smooth) N[k] += w * q.nrm[i * 3 + k]; }
      }
      if (!total) continue;
      const l = Math.hypot(N[0], N[1], N[2]);
      for (const [pi, i, , smooth] of set) {
        const q = posed[pi];
        if (!q) continue;
        for (let k = 0; k < 3; k++) q.pos[i * 3 + k] = P[k] / total;
        if (smooth && l > 1e-6) for (let k = 0; k < 3; k++) q.nrm[i * 3 + k] = N[k] / l;
      }
    }
    return posed;
  }

  // A pose's bone matrices (null for the bind pose or an unreadable file).
  async function loadPose(packs, model, pose) {
    if (!pose || !pose.path) return null;
    return packs.once('pose:' + model.framework + ':' + pose.path, async () => {
      const tracks = parseAni(await packs.read(pose.path));
      return tracks.length ? poseMatrices(model.bones, tracks) : null;
    });
  }

  // ======================================================================================
  // Props (trees, rocks, herb patches, buildings): shared with region3d.js's walker
  // ======================================================================================

  // db/propdb.xml's classes: class id -> { name (its model's file name), buried, block, event }.
  // event: its ClassPath starts with the Korean for "event": seasonal decorations (Halloween, Christmas lights, festival
  // flags) that stay in the area files all year but only show while their event runs.
  async function propClasses(packs) {
    return packs.once('propclasses', async () => {
      const text = (root.GAME_DATA && root.GAME_DATA.files && root.GAME_DATA.files['db/propdb.xml']) || await packs.text('db/propdb.xml') || '';
      const out = new Map();
      for (const m of text.matchAll(/<PropClass\b([^>]*)>/g)) {
        const a = {};
        for (const k of m[1].matchAll(/(\w+)="([^"]*)"/g)) a[k[1]] = k[2];
        if (a.ClassID) out.set(+a.ClassID, { name: (a.ClassName || '').toLowerCase(), buried: a.IsBuried === 'true', block: a.IsTerrainBlock === 'true', event: /^\/?이벤트/.test(a.ClassPath || '') });
      }
      return out;
    });
  }

  // A prop's skeletons, from its .set (CSettingClass, version 2.2): each state's framework is a .frm
  // ("pf!") right after the class name and the state name (UTF-16). { state: bones (parseFrm) }.
  function setSkeletons(d) {
    const out = {};
    if (!d) return out;
    const u16 = (at) => d[at] | (d[at + 1] << 8);
    for (let at = 0; (at = indexOf(d, PF, at)) >= 0; at += 3) {
      // The state name: the UTF-16 string ending just before (its 0 character at at-2).
      let s = at - 2;
      while (s >= 2 && u16(s - 2)) s -= 2;
      const state = new TextDecoder('utf-16le').decode(d.subarray(s, at - 2)).toLowerCase();
      out[state || 'single'] = parseFrm(d.subarray(at));
    }
    return out;
  }
  const PF = [0x70, 0x66, 0x21, 0x00];

  // A prop's .set mesh-record flags (section 2, read by the game's FUN_100b115b): "set\0", version 2.2,
  // u32 file size, u8 multi-state, u32 state count; per state: u32 2, the state name (UTF-16, 0-ended),
  // u16 record count, 208-byte records (part name char[128] at +4, u32 flag at +0xc8). Returns
  // Map(lower-case part name -> flag) for `state` (multi-state sets) or the first state; the first
  // record of a name wins. Null when the section can't be read.
  function setMeshRecordFlags(d, state) {
    if (!d || d.length < 15 || d[0] !== 0x73 || d[1] !== 0x65 || d[2] !== 0x74 || d[3] !== 0 || d[4] !== 2 || d[5] !== 2) return null;
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    const multi = d[10] !== 0, count = dv.getUint32(11, true);
    const want = (state || 'single').toLowerCase();
    let at = 15, first = null;
    for (let i = 0; i < count; i++) {
      if (at + 4 > d.length || dv.getUint32(at, true) !== 2) return first;
      at += 4;
      const start = at;
      while (at + 1 < d.length && (d[at] | (d[at + 1] << 8))) at += 2;
      const name = new TextDecoder('utf-16le').decode(d.subarray(start, at)).toLowerCase();
      at += 2;
      if (at + 2 > d.length) return first;
      const n = dv.getUint16(at, true);
      at += 2;
      if (at + n * 208 > d.length) return first;
      const flags = new Map();
      for (let r = 0; r < n; r++, at += 208) {
        const part = cstr(d, at + 4, 128).toLowerCase();
        if (!flags.has(part)) flags.set(part, dv.getUint32(at + 0xc8, true));
      }
      if (!first) first = flags;
      if (multi && name === want) return flags;
    }
    return first;
  }

  // A prop's static parts (convertMesh's statics) for a state, or null when its .set has no mesh-record
  // flags. The game (Renderer2 CSettingClass::CreateInstance) links the state's animation before its
  // mesh group and starts it at time zero; LinkMeshgroup passes each record's `flag == 0`, and the joint
  // attacher files flag-0 meshes as static: transformed once, with their bone's pose at that time
  // (CFramework::_Transform), not every frame. So a flag-0 part sits on its bone's time-zero pose. The
  // embedded resources (Renderer2 FUN_10130390): the u32 at +6 points to a resource count; each resource
  // is u32 type (1000 framework "pf!", 1001 animation "pa!"), u32 byte length, the resource name and the
  // state name (UTF-16, 0-ended), then the payload. The framework and animation are picked by state name
  // as setSkeletons picks skeletons (the state's, else 'single', else the first; a later one of a name
  // wins). Without an animation the bind pose. posed: Map(bone key -> posed world).
  function setStatics(d, state) {
    const flags = setMeshRecordFlags(d, state);
    if (!flags) return null;
    const frameworks = {}, animations = {};
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    let at = dv.getUint32(6, true);
    const count = at >= 11 && at + 4 <= d.length ? dv.getUint32(at, true) : 0;
    at += 4;
    const str = () => {
      const s = at;
      while (at + 1 < d.length && (d[at] | (d[at + 1] << 8))) at += 2;
      at += 2;
      return new TextDecoder('utf-16le').decode(d.subarray(s, at - 2)).toLowerCase();
    };
    for (let i = 0; i < count && at + 8 <= d.length; i++) {
      const type = dv.getUint32(at, true), length = dv.getUint32(at + 4, true);
      at += 8;
      str();
      const st = str() || 'single';
      if (at + length > d.length) break;
      const payload = d.subarray(at, at + length);
      at += length;
      if (type === 1000) frameworks[st] = payload;
      else if (type === 1001) animations[st] = payload;
      else break;
    }
    const pick = (o) => o[(state || 'single').toLowerCase()] || o.single || Object.values(o)[0] || null;
    const bones = parseSkeleton(pick(frameworks)), anim = pick(animations);
    const posed = new Map();
    const pose = anim && bones.length ? poseMatrices(bones, parseAni(anim)) : null;
    bones.forEach((b, i) => posed.set(b.key, pose ? mul4(pose[i], b.world) : b.world));
    return { flags, posed };
  }

  function indexOf(d, pat, from) {
    outer: for (let i = from; i + pat.length <= d.length; i++) {
      for (let k = 0; k < pat.length; k++) if (d[i + k] !== pat[k]) continue outer;
      return i;
    }
    return -1;
  }

  // The models a prop's .set gives each state. Its first part lists, after each state's name
  // (UTF-16), the state's objects as "<model>_<bone>__" (ASCII, 208 bytes each: herbs' "normal"
  // state has scene_prop_herb_before_bone01__, its "empty" one scene_prop_herb_after_bone01__);
  // the model is the longest .pmg name (pmg: bases.pmg) that starts the object's name. The
  // skeletons' part after it names states too, with no objects. { state: [model names] } in the
  // file's order, so the first state (the prop's own, before anything happens to it) comes first.
  function setModels(d, pmg) {
    const out = {};
    if (!d) return out;
    const s = new TextDecoder('latin1').decode(d), marks = [];
    for (const m of s.matchAll(/((?:[A-Za-z0-9_]\0){3,})\0\0/g)) marks.push([m.index, 'state', m[1].replace(/\0/g, '').toLowerCase()]);
    for (const m of s.matchAll(/([A-Za-z0-9_+.-]{4,})__\0/g)) marks.push([m.index, 'object', m[1].toLowerCase()]);
    marks.sort((a, b) => a[0] - b[0]);
    let state = null;
    for (const [, kind, name] of marks) {
      if (kind === 'state') { state = name; continue; }
      if (!state) continue;
      for (let i = name.lastIndexOf('_'); i > 0; i = name.lastIndexOf('_', i - 1)) {
        const base = name.slice(0, i);
        if (pmg.has(base)) {
          const list = out[state] || (out[state] = []);
          if (!list.includes(base)) list.push(base);
          break;
        }
      }
    }
    return out;
  }

  // A prop's model in a state: data/gfx/scene/**/<class name>.pmg on its .set's framework for that
  // state (each part at BoneWorld(bone) x its matrix, as characters are placed), else without a
  // skeleton. Props whose model changes with their state (herbs, ore, mushrooms, wells: before and
  // after they're gathered or used) have no model of the class's name: their .set names each
  // state's models (setModels), drawn together on that state's framework (gfx/scene/ first, then
  // gfx/char/: the undersea mythril ore's). No state ('single'): the .set's first. Shared by every
  // prop of that class and state.
  async function propModel(packs, cls, out, state) {
    state = (state || 'single').toLowerCase();
    return packs.once('propmodel:' + cls.name + ':' + state, async () => {
      const bases = out.bases;
      const scene = (list) => (list || []).filter((p) => p.startsWith('gfx/scene/')).sort();
      const sceneFirst = (list) => (list || []).slice().sort((a, b) => (!a.startsWith('gfx/scene/')) - (!b.startsWith('gfx/scene/')) || cmp(a, b));
      for (const p of scene(bases.pmg.get(cls.name))) {
        const setPath = p.slice(0, -4) + '.set';
        const setD = packs.has(setPath) ? await packs.read(setPath) : null;
        const skels = setD ? setSkeletons(setD) : {};
        const skel = skels[state] || skels.single || Object.values(skels)[0] || null;
        if (skel) skel.path = setPath;
        const m = await convertMesh(packs, p, skel && skel.size ? skel : null, out, false, setD && setStatics(setD, state));
        if (m && m.meshes.length) return m;
      }
      for (const setPath of sceneFirst(bases.set.get(cls.name))) {
        const d = await packs.read(setPath), models = setModels(d, bases.pmg), states = Object.keys(models);
        const st = models[state] ? state : states[0];
        if (!st) continue;
        const skels = setSkeletons(d);
        const skel = skels[st] || skels.single || Object.values(skels)[0] || null;
        if (skel) skel.path = setPath;
        // Each model from the .set's own folder when it has one there.
        const dir = setPath.slice(0, setPath.lastIndexOf('/') + 1);
        const all = { file: null, meshes: [], hidden: 0, skeleton: skel && skel.path || null };
        const statics = setStatics(d, st);
        for (const name of models[st]) {
          const paths = sceneFirst(bases.pmg.get(name));
          const p = paths.find((q) => q.startsWith(dir) && !q.slice(dir.length).includes('/')) || paths[0];
          const m = p && await convertMesh(packs, p, skel && skel.size ? skel : null, out, false, statics);
          if (!m) continue;
          all.file = all.file || p;
          all.meshes.push(...m.meshes);
          all.hidden += m.hidden;
        }
        if (all.meshes.length) return all;
      }
      return null;
    });
  }

  // A prop page's model: the first of its class ids (db/propdb.xml ClassID) that has one, in its
  // first state. { id, name, file, meshes, hidden, textures, texinfo, dyes: [] }; meshes empty when
  // none of them has a model; null when none of them is a class.
  async function convertProp(packs, ids) {
    const classes = await propClasses(packs);
    const out = { textures: {}, texinfo: {}, bases: byBase(packs), mats: await materials(packs) };
    let found = null;
    for (const id of [].concat(ids).map((i) => parseInt(i, 10))) {
      const cls = classes.get(id);
      if (!cls || !cls.name) continue;
      if (!found) found = { id: String(id), name: cls.name };
      const m = await propModel(packs, cls, out, null);
      // Not kept for another visit (the viewer holds what it shows).
      packs.memo.delete('propmodel:' + cls.name + ':single');
      if (m && m.meshes.some((x) => x.tris)) { found = { id: String(id), name: cls.name, ...m }; break; }
    }
    if (!found) return null;
    return { meshes: [], hidden: 0, ...found, textures: out.textures, texinfo: out.texinfo, dyes: [] };
  }

  // A prop part's material, as the walker and the prop viewer draw it. Nearly every prop's material
  // doesn't take the main light (rs.mainLight false): no scene lighting at all, the vertex colors are
  // the light (baked into the model). The part's color is its vertex color times (the prop's color
  // + 1) / 512 (CRC_Mesh::ColorOverride: white halves it, the 0x80 grey most props carry quarters
  // it), passed per prop as the instance color. The render state's first stage mixes it with the
  // texture (D3DTOP_*: ADDSIGNED texture + color - 0.5, MODULATE2X / 4X twice / four times their
  // product); the second stage scales that by the material's gloss map (x4 or x2, read by the way
  // the surface faces the light: Gloss, or the view: Sphere), which is where the lit and the shaded
  // sides come from. A second stage on the shadow map counts as unshadowed (x1).
  // { mat, unlit }: unlit materials take the instance color (partColor).
  const STAGE_OPS = { ADDSIGNED: 0, MODULATE2X: 1, MODULATE: 2, SELECTARG1: 3, MODULATE4X: 4, ADD: 5, SELECTARG2: 6 };
  function propMaterial(T, rs, map, gloss) {
    if (!(rs && !rs.mainLight)) {
      // Lit parts (the ground pieces under buildings, terrain blocks: they take the main light like
      // the ground) carry white vertex colors where the ground's points carry 0x80: white is their
      // neutral, so the color is used as it is, not doubled (doubled, they drew twice as bright as
      // the ground around them).
      const mat = new T.MeshLambertMaterial({ map, vertexColors: true, side: T.DoubleSide, alphaTest: rs && rs.alphatest != null ? rs.alphatest : 0.5 });
      if (rs && rs.blend && !(rs.blend[0] === 'ONE' && rs.blend[1] === 'ZERO')) applyRS(T, mat, rs);
      // The lit ground pieces (terrain blocks): their texture's alpha is a gloss mask for the
      // specular stage (MODULATEALPHA_ADDCOLOR), not see-through; the alpha is the vertex's.
      if (rs && rs.op1 === 'MODULATEALPHA_ADDCOLOR') mat.alphaTest = 0;
      return { mat, unlit: false };
    }
    const op = rs.op in STAGE_OPS ? STAGE_OPS[rs.op] : 1;
    // The alpha is the texture's unless the state's alpha takes the vertex alpha.
    const texAlpha = !rs.vertexAlpha;
    gloss = rs.gloss ? gloss : null;
    const glossMode = gloss && (rs.op1 === 'MODULATE4X' || rs.op1 === 'MODULATE2X') ? (/sphere/i.test(rs.tc1) ? 1 : /gloss/i.test(rs.tc1) ? 2 : 0) : 0;
    const glossScale = rs.op1 === 'MODULATE4X' ? '4.0' : '2.0';
    const mat = new T.MeshLambertMaterial({ map, vertexColors: true, side: T.DoubleSide });
    mat.defines = { MW_OP: map ? op : 6 };
    if (glossMode) mat.defines.MW_GLOSS = glossMode;
    mat.customProgramCacheKey = () => 'mwr' + (map ? op : 6) + (texAlpha ? 't' : '') + glossMode + glossScale;
    mat.onBeforeCompile = (s) => {
      if (glossMode) s.uniforms.mwGloss = { value: gloss };
      s.fragmentShader = (glossMode ? 'uniform sampler2D mwGloss;\n' : '') + s.fragmentShader;
      s.fragmentShader = s.fragmentShader
        .replace('#include <color_fragment>', '#if defined( USE_COLOR_ALPHA )\n vec3 mwD = vColor.rgb;' + (texAlpha ? '' : ' diffuseColor.a *= vColor.a;') + '\n#else\n vec3 mwD = vec3( 0.5 );\n#endif')
        .replace('#include <normal_fragment_begin>', 'float faceDirection = gl_FrontFacing ? 1.0 : - 1.0;\nvec3 normal = normalize( vNormal );\nvec3 nonPerturbedNormal = normal;')
        .replace('#include <map_fragment>', '#ifdef USE_MAP\n vec4 mwTex = texture2D( map, vMapUv );\n diffuseColor.a *= mwTex.a;\n#endif')
        .replace('#include <opaque_fragment>', [
          'vec3 mwC = mwD;',
          '#ifdef USE_MAP',
          '#if MW_OP == 0', ' mwC = clamp( mwTex.rgb + mwD - 0.5, 0.0, 1.0 );',
          '#elif MW_OP == 1', ' mwC = clamp( 2.0 * mwTex.rgb * mwD, 0.0, 1.0 );',
          '#elif MW_OP == 2', ' mwC = mwTex.rgb * mwD;',
          '#elif MW_OP == 3', ' mwC = mwTex.rgb;',
          '#elif MW_OP == 4', ' mwC = clamp( 4.0 * mwTex.rgb * mwD, 0.0, 1.0 );',
          '#elif MW_OP == 5', ' mwC = clamp( mwTex.rgb + mwD, 0.0, 1.0 );',
          '#endif', '#endif',
          '#ifdef MW_GLOSS',
          '#if MW_GLOSS == 1',
          ' vec2 mwG = vec2( 0.51 + 0.49 * normal.x, 0.51 - 0.49 * normal.y );',
          '#else',
          // The world normal (the drawn space mirrors the game's y, so the light's z turns).
          ' vec3 mwN = normalize( ( vec4( normal, 0.0 ) * viewMatrix ).xyz );',
          ' vec2 mwG = vec2( 0.0, 0.5 + 0.49 * dot( mwN, normalize( vec3( -0.5, -1.0, 0.559 ) ) ) );',
          '#endif',
          ' mwC = clamp( ' + glossScale + ' * texture2D( mwGloss, mwG ).rgb * mwC, 0.0, 1.0 );',
          '#endif',
          'outgoingLight = mwC;',
          '#include <opaque_fragment>'].join('\n'));
    };
    applyRS(T, mat, rs);
    // Mirror floors (blend ONE, SRCALPHA: a castle hall's polished floor): the game draws the room
    // mirrored underneath first and adds it, darkened by the floor's alpha, to the floor's color.
    // With no mirrored room drawn, what is behind is the sky (or nothing): the floor stands alone.
    if (rs.blend && rs.blend[0] === 'ONE' && rs.blend[1] === 'SRCALPHA') {
      mat.transparent = false; mat.blending = T.NormalBlending; mat.depthWrite = true;
    }
    return { mat, unlit: true };
  }
  // The prop color a part takes: its color map's digit N picks the prop's Nth color (b1 to b9);
  // none, white. As (color + 1) / 512 per channel.
  function partColor(colormap, colors) {
    const m = /(\d)/.exec(colormap || ''), n = m ? +m[1] : 0;
    const c = n >= 1 && n <= 9 && colors && colors[n - 1] != null ? colors[n - 1] : 0xffffff;
    return [(((c >>> 16) & 255) + 1) / 512, (((c >>> 8) & 255) + 1) / 512, ((c & 255) + 1) / 512];
  }

  // ======================================================================================
  // Access to the packs: a folder the user picks, kept in IndexedDB
  // ======================================================================================

  function idb() {
    return new Promise((resolve, reject) => {
      const r = indexedDB.open('MabiWiki3D', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('kv');
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
  }
  async function idbGet(key) {
    if (!root.indexedDB) return null;
    const db = await idb();
    return new Promise((resolve, reject) => {
      const r = db.transaction('kv').objectStore('kv').get(key);
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
    });
  }
  async function idbSet(key, value) {
    if (!root.indexedDB) return;
    const db = await idb();
    return new Promise((resolve, reject) => {
      const tx = db.transaction('kv', 'readwrite');
      tx.objectStore('kv').put(value, key);
      tx.oncomplete = () => resolve();
      tx.onerror = () => reject(tx.error);
    });
  }

  let packsPromise = null; // this session's packs

  // The .pack files in a picked folder: its package\ folder, or the folder itself.
  async function packFilesIn(dir) {
    let pkg = dir;
    try { pkg = await dir.getDirectoryHandle('package'); } catch (e) { /* maybe the package folder itself */ }
    const files = [];
    for await (const [name, h] of pkg.entries()) {
      if (h.kind === 'file' && /\.pack$/i.test(name)) files.push(await h.getFile());
    }
    return files;
  }

  async function usePackFiles(files) {
    const list = [...files].filter((f) => /\.pack$/i.test(f.name));
    if (!list.length) throw new Error('nopacks');
    packsPromise = packsLib().then((lib) => lib.Packs.open(list));
    return packsPromise;
  }

  // Packs another part of the page already opened (MabiPacks.open: the wiki's preparer), so the
  // game's files aren't asked for twice in one visit.
  function usePacks(packs) {
    packsPromise = Promise.resolve(packs);
    return packsPromise;
  }

  let hostedTried = false;

  // Where access stands: 'ready' (packs open), 'allow' (a kept folder needs a click), 'choose'. A
  // hosted copy (MABIWIKI_MODELS) reads its own model files; without them (not exported with
  // models, or opened from disk) the reader's game folder still works.
  async function accessState() {
    if (packsPromise) return 'ready';
    if (root.MABIWIKI_MODELS && !hostedTried) {
      hostedTried = true;
      const hosted = await packsLib().then(() => packClasses().HostedPacks.open(root.MABIWIKI_MODELS)).catch(() => null);
      if (hosted) { packsPromise = Promise.resolve(hosted); return 'ready'; }
    }
    if (!root.showDirectoryPicker) return 'choose';
    const handle = await idbGet('folder').catch(() => null);
    if (!handle) return 'choose';
    const perm = await handle.queryPermission({ mode: 'read' }).catch(() => 'denied');
    if (perm === 'granted') {
      const files = await packFilesIn(handle).catch(() => []);
      if (files.length) { await usePackFiles(files); return 'ready'; }
      return 'choose';
    }
    return 'allow';
  }

  // From a click: ask for the kept folder again, or pick one.
  async function allowKept() {
    const handle = await idbGet('folder');
    if (!handle || (await handle.requestPermission({ mode: 'read' })) !== 'granted') return false;
    const files = await packFilesIn(handle);
    if (!files.length) return false;
    await usePackFiles(files);
    return true;
  }
  async function chooseFolder() {
    return useFolder(await root.showDirectoryPicker({ id: 'mabinogi', mode: 'read' }));
  }
  // A folder handle the page has (picked, or kept and allowed) as the game folder: kept for later.
  async function useFolder(handle) {
    const files = await packFilesIn(handle);
    if (!files.length) return false;
    await idbSet('folder', handle).catch(() => {});
    await usePackFiles(files);
    return true;
  }

  // ======================================================================================
  // A hosted copy's setup with 3D: the model files it reads (HostedPacks)
  // ======================================================================================
  // models: [{ kind: 'item' | 'race' | 'prop', id, row, ids (a prop's class ids) }], what the wiki's
  // pages show. Each goes through the converters (a race through its poses too, a prop through its
  // class ids until one has a model, db/propdb.xml included) as the viewer would, noting the files read. Those,
  // plus every skeleton and material define (looked up by name: what the converters find among
  // them must stay the same), go under dir (the site's MabiWiki/models/ folder, a
  // FileSystemDirectoryHandle, or a zipWriter where the browser can't write to a folder) with
  // index.json. motion.xml (~60 MB) doesn't: index.json carries motionIndex's list.
  // progress(stage ('models', 'files'), done, total).
  async function exportModels(models, dir, progress) {
    const packs = packsPromise && await packsPromise;
    if (!packs || isHosted(packs)) throw new Error('The game\'s files aren\'t open.');
    const rec = new (packClasses().RecordingPacks)(packs);
    let failed = 0;
    for (let i = 0; i < models.length; i++) {
      const m = models[i];
      try {
        if (m.kind === 'item') await convertItem(rec, m.id, m.row);
        else if (m.kind === 'prop') await convertProp(rec, m.ids || [m.id]);
        else {
          const model = await convertRace(rec, m.id, m.row, {});
          if (model) await racePoses(rec, model);
        }
      } catch (e) {
        failed++;
        if (root.console) console.warn('[MabiWikiViewer] export', m.kind, m.id, e);
      }
      if (i % 10 === 9 || i === models.length - 1) progress('models', i + 1, models.length);
    }
    for (const n of packs.entries.keys()) {
      const model = n.startsWith('gfx/char/') || n.startsWith('gfx/scene/');
      if ((n.startsWith('material/_define/') && n.endsWith('.xml')) || (model && n.endsWith('.frm'))) rec.seen.add(n);
    }
    const motions = [...await motionIndex(rec)];
    rec.seen.delete('db/motion.xml');

    // Folders below dir, made as needed (one promise each, so workers don't race).
    const dirs = new Map([['', Promise.resolve(dir)]]);
    const folder = (path) => {
      if (!dirs.has(path)) {
        const cut = path.lastIndexOf('/');
        dirs.set(path, folder(cut < 0 ? '' : path.slice(0, cut)).then((d) => d.getDirectoryHandle(path.slice(cut + 1), { create: true })));
      }
      return dirs.get(path);
    };
    const write = !dir.getDirectoryHandle ? (path, data) => dir.write(path, data) : async (path, data) => {
      const cut = path.lastIndexOf('/');
      const d = await folder(cut < 0 ? '' : path.slice(0, cut));
      const w = await (await d.getFileHandle(path.slice(cut + 1), { create: true })).createWritable();
      await w.write(data);
      await w.close();
    };
    // Kept as the pack names them when every folder and file name is plain (any OS, any host).
    const plain = (n) => n.split('/').every((s) => /^[a-z0-9_\-.()]+$/.test(s) && s !== '.' && s !== '..' && !s.endsWith('.'));

    const names = [...rec.seen].sort(cmp);
    const files = [];
    let next = 0, done = 0, bytes = 0;
    const worker = async () => {
      while (next < names.length) {
        const i = next++, n = names[i];
        const r = await packs.raw(n);
        if (r) {
          const path = plain(n) ? n : 'other/' + i + '.bin';
          await write(path, r.data);
          const e = packs.entries.get(n);
          files.push([n, path, e.seed >>> 0, e.size, r.compressed ? 1 : 0]);
          bytes += r.data.length;
        }
        if (++done % 25 === 0 || done === names.length) progress('files', done, names.length);
      }
    };
    await Promise.all(Array.from({ length: 6 }, worker));
    files.sort((a, b) => cmp(a[0], b[0]));
    const index = JSON.stringify({ version: 1, files, motions });
    await write('index.json', index);
    return { models: models.length, failed, files: files.length, bytes: bytes + index.length };
  }

  // ======================================================================================
  // three.js
  // ======================================================================================

  let threePromise = null;
  function ensureThree() {
    if (root.THREE) return Promise.resolve(root.THREE);
    if (!threePromise) {
      threePromise = new Promise((resolve, reject) => {
        const s = document.createElement('script');
        s.src = BASE() + 'lib/three-bundle.min.js';
        s.onload = () => (root.THREE ? resolve(root.THREE) : reject(new Error('three.js')));
        s.onerror = () => { threePromise = null; reject(new Error('three.js')); };
        document.head.appendChild(s);
      });
    }
    return threePromise;
  }

  // ======================================================================================
  // Dyes
  // ======================================================================================
  // A part's colormap names its color: a digit 1-3 is dye slot N (App_ColorN), whatever the
  // letters before it (c clothes, i dropped item, h head, s shoes, g gloves, w wings, tr/tl/tb
  // tools...). 4 and up, or no digit, are fixed: mid grey, the texture as it is. 'skin' and 'hair'
  // take the character's colors.
  function slotOf(colormap) {
    const c = (colormap || '').toLowerCase();
    if (c === 'skin' || c === 'hair') return c;
    const m = /(\d)/.exec(c);
    const n = m ? +m[1] : 0;
    return n >= 1 && n <= 3 ? n : 0;
  }
  // A color name nothing sets is white in the game (CRC_Mesh::ColorOverride: the vertex color
  // times (color + 1) / 512), and so is hair with no hair item.
  const FIXED = { 0: '#ffffff', skin: '#f2d4c0', hair: '#ffffff' };

  // The first texture stage of the part's render state: D3DTOP_* -> how the texture and the lit
  // color mix. Character materials use ADDSIGNED (texture + color - 0.5); _1x ones MODULATE2X.
  const OPS = { ADDSIGNED: 0, MODULATE2X: 1, MODULATE: 2, SELECTARG1: 3, MODULATE4X: 4, ADD: 5 };

  function hexRgb(hex) {
    const n = parseInt((hex || '#808080').slice(1), 16);
    return [((n >> 16) & 255) / 255, ((n >> 8) & 255) / 255, (n & 255) / 255];
  }

  // ======================================================================================
  // The viewer
  // ======================================================================================

  const CSS = `
.mwv{display:flex;flex-direction:column;gap:8px;font:inherit;color:inherit;min-height:340px;height:100%}
.mwv-bar{display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center;font-size:13px}
.mwv-bar label{display:inline-flex;gap:6px;align-items:center}
.mwv-bar select,.mwv-bar button{font:inherit;color:inherit;background:var(--panel,#fff);border:1px solid var(--line,#d0d4da);border-radius:6px;padding:3px 8px}
.mwv-bar button{cursor:pointer}
.mwv-dye{display:inline-flex;gap:4px;align-items:center}
.mwv-dye input[type=color]{width:30px;height:24px;padding:0;border:1px solid var(--line,#d0d4da);border-radius:4px;background:none;cursor:pointer}
.mwv-view{position:relative;flex:1;min-height:300px;border:1px solid var(--line,#d0d4da);border-radius:8px;overflow:hidden;background:var(--bg2,#eceef2)}
.mwv-view canvas{display:block;position:absolute;inset:0;width:100%;height:100%;cursor:grab}
.mwv-info{position:absolute;left:8px;bottom:6px;font-size:11px;color:var(--muted,#778);pointer-events:none}
.mwv-msg{position:absolute;inset:0;display:flex;flex-direction:column;gap:10px;align-items:center;justify-content:center;text-align:center;padding:20px;font-size:14px;line-height:1.5}
.mwv-msg p{margin:0;max-width:440px}
.mwv-msg small{color:var(--muted,#778)}
.mwv-msg button{font:inherit;cursor:pointer;padding:6px 14px;border-radius:8px;border:1px solid var(--accent,#4f6bed);background:var(--accent,#4f6bed);color:#fff}
.mwv-view canvas:focus{outline:none}
.mwv-view canvas:focus-visible{outline:2px solid var(--accent,#4f6bed);outline-offset:-2px}
.mwv-ico{width:16px;height:16px;flex:none;display:block}
.mwv-bar .mwv-fs{display:inline-flex;align-items:center;gap:5px}
.mwv-opts{position:absolute;left:12px;bottom:12px;z-index:2;display:flex;gap:8px;align-items:center;padding:4px 6px 4px 10px;border-radius:10px;border:1px solid var(--line,#d0d4da);background:color-mix(in srgb,var(--surface,#fff) 92%,transparent);font-size:12.5px;white-space:nowrap;max-width:calc(100% - 120px)}
.mwv-opts label{display:inline-flex;gap:6px;align-items:center}
.mwv-opts select,.mwv-opts button{font:inherit;color:inherit;background:var(--surface-2,#fff);border:1px solid var(--line,#d0d4da);border-radius:6px;padding:2px 8px;cursor:pointer}
.mwv-opts button:hover,.mwv-opts select:hover{border-color:var(--accent,#4f6bed)}
.mwv-opts .mwv-fs{display:inline-flex;align-items:center;gap:5px}
.mwv-view .mwv-info{bottom:54px;left:14px}
.mwv-full .mwv-hint{bottom:76px}
@media (max-width:560px){.mwv-opts .mwv-txt,.mwv-opts .mwv-fs span{display:none}.mwv-opts{max-width:none}}
.mwv-nav{position:absolute;right:12px;bottom:12px;z-index:2;display:flex;flex-direction:column;align-items:center;gap:6px}
.mwv-zoom{display:flex;flex-direction:column;border:1px solid var(--line,#d0d4da);border-radius:8px;overflow:hidden}
.mwv-pad{display:grid;grid-template-columns:repeat(3,28px);grid-template-rows:repeat(3,28px);gap:2px}
.mwv-nb{display:grid;place-items:center;width:28px;height:28px;padding:0;font:inherit;color:inherit;background:var(--panel,#fff);border:1px solid var(--line,#d0d4da);border-radius:6px;cursor:pointer}
.mwv-nb:hover{border-color:var(--accent,#4f6bed)}
.mwv-nb:focus-visible{outline:2px solid var(--accent,#4f6bed);outline-offset:1px}
.mwv-zoom .mwv-nb{border:0;border-radius:0}
.mwv-zoom .mwv-nb+.mwv-nb{border-top:1px solid var(--line,#d0d4da)}
.mwv-up{grid-area:1/2}.mwv-left{grid-area:2/1}.mwv-home{grid-area:2/2}.mwv-right{grid-area:2/3}.mwv-down{grid-area:3/2}
.mwv-hint{display:none;position:absolute;left:8px;bottom:26px;max-width:calc(100% - 130px);padding:4px 10px;border-radius:8px;background:rgba(0,0,0,.55);color:#fff;font-size:12px;line-height:1.4;pointer-events:none}
.mwv-full .mwv-hint{display:block}
.mwv-toast{position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);max-width:80%;padding:8px 14px;border-radius:8px;background:rgba(0,0,0,.72);color:#fff;font-size:13px;text-align:center;pointer-events:none;opacity:0}
.mwv-toast.on{animation:mwv-toast 1.8s ease forwards}
@keyframes mwv-toast{0%{opacity:0}10%{opacity:1}75%{opacity:1}100%{opacity:0}}
.mwv.mwv-full{background:var(--bg,#fff);height:100%;min-height:0}
.mwv.mwv-full.mwv-css{position:fixed;inset:0;z-index:2147483000;width:auto;height:auto;margin:0}
.mwv.mwv-full .mwv-view{border:0;border-radius:0;min-height:0}
.mwv.mwv-full .mwv-nav{bottom:12px}
html.mwv-lock,html.mwv-lock body{overflow:hidden!important}
.mwv-lift{z-index:auto!important;transform:none!important;filter:none!important;backdrop-filter:none!important;contain:none!important;will-change:auto!important;isolation:auto!important}
`;
  // Line icons (24 x 24, stroked in the text color).
  const ICONS = {
    full: '<path d="M4 9V4h5M20 9V4h-5M4 15v5h5M20 15v5h-5"/>',
    unfull: '<path d="M9 4v5H4M15 4v5h5M9 20v-5H4M15 20v-5h5"/>',
    plus: '<path d="M12 5v14M5 12h14"/>',
    minus: '<path d="M5 12h14"/>',
    up: '<path d="m6 15 6-6 6 6"/>',
    down: '<path d="m6 9 6 6 6-6"/>',
    left: '<path d="m15 6-6 6 6 6"/>',
    right: '<path d="m9 6 6 6-6 6"/>',
    home: '<path d="M3 12a9 9 0 1 0 2.6-6.4L3 8"/><path d="M3 3v5h5"/>',
  };
  const icon = (name) => '<svg class="mwv-ico" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" ' +
    'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + ICONS[name] + '</svg>';
  function addStyle() {
    if (document.getElementById('mwv-style')) return;
    const s = document.createElement('style');
    s.id = 'mwv-style';
    s.textContent = CSS;
    document.head.appendChild(s);
  }
  const el = (tag, attrs, ...kids) => {
    const e = document.createElement(tag);
    Object.entries(attrs || {}).forEach(([k, v]) => (k === 'text' ? (e.textContent = v) : k.startsWith('on') ? (e[k] = v) : e.setAttribute(k, v)));
    kids.forEach((k) => e.append(k));
    return e;
  };

  const STAT_NAMES = { r: 'Right hand', lr: 'Left hand', e: 'Sheathed (right)', le: 'Sheathed (left)', x1: 'Hat', y1: 'Hat (second form)' };
  const statName = (s) => STAT_NAMES[s] || (/^f(\d+)$/.test(s) ? 'Face ' + s.slice(1) : /^h(\d+)$/.test(s) ? 'Hair ' + s.slice(1) : s);

  function open(container, itemId, options) {
    options = options || {};
    const id = String(parseInt(itemId, 10));
    return mount(container, options, {
      what: 'items',
      load: (packs) => convertItem(packs, id, options.row),
      missing: 'This item isn\'t in your game\'s files.',
      // Effect-only accessories (the acc_hack relics): every part is an effect anchor, left out.
      empty: (model) => (!model.variants.length ? 'This item has no 3D model.'
        : !model.variants.some((v) => v.meshes.some((m) => m.tris)) ? 'This item is only an effect in the game: it has no model to show.' : null),
      make: (T, model, ui) => new Viewer(T, model, ui, options),
    });
  }

  // A race (monster, NPC) on its skeleton. options: row (its race.xml attributes; else looked up in
  // window.GAME_DATA's db/race.xml, else the packs), colors (['#rrggbb' x3] body colors: the
  // client has none for monsters), skin, pose ('bind', 'StandFriendly01' (the default, when the
  // race has it), ...), yaw, pitch, zoom,
  // spin. Returns { close(), setDyes(colors) }.
  function openRace(container, raceId, options) {
    options = options || {};
    const id = String(parseInt(raceId, 10));
    return mount(container, options, {
      what: 'monsters',
      load: (packs) => convertRace(packs, id, options.row, options),
      missing: 'This monster isn\'t in your game\'s files.',
      empty: (model) => (!model.parts.some((p) => p.visible && p.tris) ? 'This monster has no 3D model.' : null),
      make: (T, model, ui, packs) => new RaceViewer(T, model, ui, options, packs),
    });
  }

  // A prop (tree, rock, herb patch, building): the first of classIds (db/propdb.xml ClassIDs, or
  // one) with a model (convertProp). options: colors (the prop's colors b1-b9 as 0xRRGGBB; the
  // 0x80 grey most placed props carry by default), yaw, pitch, zoom, spin. Returns { close() }.
  function openProp(container, classIds, options) {
    options = options || {};
    return mount(container, options, {
      what: 'props',
      load: (packs) => convertProp(packs, classIds),
      missing: 'This prop isn\'t in your game\'s files.',
      empty: (model) => (!model.meshes.some((m) => m.tris) ? 'This prop has no 3D model.' : null),
      make: (T, model, ui) => new PropViewer(T, model, ui, options),
    });
  }

  function mount(container, options, kind) {
    addStyle();
    const ui = {
      bar: el('div', { class: 'mwv-bar' }),
      view: el('div', { class: 'mwv-view' }),
      msg: el('div', { class: 'mwv-msg' }),
      info: el('div', { class: 'mwv-info' }),
    };
    const box = ui.box = el('div', { class: 'mwv' }, ui.bar, ui.view);
    ui.view.append(ui.msg, ui.info);
    container.innerHTML = '';
    container.append(box);
    let closed = false, viewer = null, retried = false;

    const say = (html, ...kids) => {
      ui.msg.style.display = '';
      ui.msg.innerHTML = '';
      if (html) ui.msg.append(el('p', null, ...[].concat(html)));
      kids.forEach((k) => ui.msg.append(k));
    };

    const explain = () => el('small', { text: 'The 3D view reads the model straight from your game\'s files on this computer. ' +
      'Nothing is uploaded or changed.' });

    async function begin() {
      if (!root.DecompressionStream) { say('Your browser can\'t show the 3D view. Use a current Chrome, Edge, Firefox or Safari.'); return; }
      say('Loading...');
      let state;
      try { state = await accessState(); } catch (e) { state = 'choose'; }
      if (closed) return;
      if (state === 'ready') return show();
      if (state === 'allow') {
        say('Your browser needs your OK to read the game\'s files again.',
          el('button', { text: 'Allow', onclick: () => act(allowKept) }), explain());
        return;
      }
      chooser();
    }

    function chooser(problem) {
      if (root.showDirectoryPicker) {
        say([problem ? problem + ' ' : '', 'To see ' + kind.what + ' in 3D, choose your Mabinogi folder (the one with Client.exe and the package folder) once. ' +
          'Your browser remembers it.'], el('button', { text: 'Choose your Mabinogi folder', onclick: () => act(chooseFolder) }), explain());
      } else {
        const input = el('input', { type: 'file', webkitdirectory: '', multiple: '' });
        input.onchange = () => act(() => usePackFiles(input.files).then(() => true, () => false));
        say([problem ? problem + ' ' : '', 'To see ' + kind.what + ' in 3D, choose the package folder inside your Mabinogi folder. ' +
          'Your browser may say it\'s uploading the files; they stay on this computer.'], input, explain());
      }
    }

    async function act(fn) {
      let ok = false;
      try { ok = await fn(); } catch (e) { if (e && e.name === 'AbortError') return; }
      if (closed) return;
      if (ok) show();
      else chooser('That folder has no package folder with .pack files.');
    }

    async function show() {
      say('Reading the game\'s files...');
      try {
        const [packs, T] = await Promise.all([packsPromise, ensureThree()]);
        const model = await kind.load(packs);
        if (closed) return;
        if (!model) { say(isHosted(packs) ? 'This model isn\'t in this copy of the wiki.' : kind.missing); return; }
        const empty = kind.empty(model);
        if (empty) { say(empty); return; }
        ui.msg.style.display = 'none';
        viewer = kind.make(T, model, ui, packs);
      } catch (e) {
        if (root.console) console.error('[MabiWikiViewer]', e);
        if (e && e.name === 'NotReadableError' && !retried) { retried = true; packsPromise = null; return begin(); } // a patch replaced the packs
        if (!closed) say('The 3D view couldn\'t be shown: ' + (e && e.message ? e.message : e));
      }
    }

    begin();
    return {
      close() { closed = true; if (viewer) viewer.dispose(); box.remove(); },
      setDyes(dyes) { if (viewer) viewer.setDyes(dyes); },
    };
  }

  // ---------- frame rate ----------
  // The 3D views (these and region3d.js's walker) draw at most the reader's chosen number of frames a
  // second, kept in this browser: 60 by default, smooth and easy on the graphics card on fast
  // screens. 0 is "Monitor rate": a frame every time the screen refreshes, which is as often as a
  // browser ever draws (165 a second on a 165 Hz screen), so it's never truly uncapped.
  const FPS_KEY = 'wiki-3d-fps';
  const FPS_CHOICES = [[30, '30 fps'], [60, '60 fps'], [0, 'Monitor rate']];
  const fpsPickers = new Set();
  let fpsCap = (() => {
    try {
      const v = localStorage.getItem(FPS_KEY);
      if (v != null && FPS_CHOICES.some((c) => String(c[0]) === v)) return +v;
    } catch (e) { /* no storage (a private window): the default */ }
    return 60;
  })();
  const frameRate = {
    get: () => fpsCap,
    set(v) {
      fpsCap = +v;
      try { localStorage.setItem(FPS_KEY, String(fpsCap)); } catch (e) { /* kept for this visit only */ }
      fpsPickers.forEach((sel) => { sel.value = fpsCap; });
    },
    // A "Frame rate" picker for a view's toolbar, kept in step with the other open views'. Call
    // release() when the view closes.
    picker() {
      const sel = el('select', { title: '30 or 60 frames a second spare the graphics card; Monitor rate draws as often as your monitor refreshes' });
      FPS_CHOICES.forEach(([v, label]) => sel.add(new Option(label, v)));
      sel.value = fpsCap;
      sel.onchange = () => frameRate.set(sel.value);
      fpsPickers.add(sel);
      return { label: el('label', null, el('span', { class: 'mwv-txt' }, 'Frame rate'), sel), release: () => fpsPickers.delete(sel) };
    },
    // Calls draw(seconds since the last drawn frame, 0 the first time) at most at the chosen rate,
    // until stop(). Frames are kept to a steady cadence (each due one interval after the last was
    // due), so 60 on a 165 Hz screen averages 60 instead of rounding down to every third refresh;
    // 1 ms of slack lets a frame that comes a hair early count.
    loop(draw) {
      let id = 0, due = -1, prev = -1, stopped = false;
      const tick = (t) => {
        if (stopped) return;
        id = requestAnimationFrame(tick);
        const gap = fpsCap ? 1000 / fpsCap : 0;
        if (gap && due >= 0 && t < due - 1) return;
        // Fell behind (the tab was hidden, a slow frame): start the cadence again from now.
        due = gap && due >= 0 && t - due < gap ? due + gap : t + gap;
        const dt = prev < 0 ? 0 : Math.min(0.1, (t - prev) / 1000);
        prev = t;
        draw(dt);
      };
      id = requestAnimationFrame(tick);
      return { stop() { stopped = true; cancelAnimationFrame(id); } };
    },
  };

  class Viewer {
    constructor(T, model, ui, opts) {
      this.T = T; this.model = model; this.ui = ui; this.opts = opts;
      this.textures = new Map();
      this.defaults = model.dyes.map((d) => d.rgb || '#c0c0c0');
      this.dyes = (opts.dyes || []).length ? [0, 1, 2].map((i) => opts.dyes[i] || this.defaults[i]) : this.defaults.slice();
      const dark = document.documentElement.dataset.theme === 'dark' ||
        (document.documentElement.dataset.theme !== 'light' && matchMedia('(prefers-color-scheme: dark)').matches);

      // Colors are worked out in gamma space, like the game's fixed-function D3D9: no conversions.
      const r = this.renderer = new T.WebGLRenderer({ antialias: true, preserveDrawingBuffer: true });
      r.outputColorSpace = 'srgb-linear';
      r.setPixelRatio(Math.min(2, root.devicePixelRatio || 1));
      ui.view.insertBefore(r.domElement, ui.view.firstChild);
      this.scene = new T.Scene();
      this.scene.background = new T.Color().setRGB(...(dark ? [0.11, 0.13, 0.17] : [0.92, 0.93, 0.95]));
      this.camera = new T.PerspectiveCamera(35, 1, 1, 20000);
      this.controls = new T.OrbitControls(this.camera, r.domElement);
      this.controls.enableDamping = true;
      this.scene.add(new T.HemisphereLight(0xffffff, 0x8c8c8c, 1.9));
      const sun = new T.DirectionalLight(0xffffff, 1.4); sun.position.set(300, 500, 400); this.scene.add(sun);
      const back = new T.DirectionalLight(0xffffff, 0.45); back.position.set(-300, 200, -400); this.scene.add(back);

      this.buildBar();
      this.addViewControls();
      this.resizeObserver = new ResizeObserver(() => this.resize());
      this.resizeObserver.observe(ui.view);
      this.resize();
      this.build(true);
      // Nothing is drawn while the view is scrolled out of sight (the page keeps it running).
      this.onScreen = true;
      this.visibility = new IntersectionObserver((seen) => { this.onScreen = seen[seen.length - 1].isIntersecting; });
      this.visibility.observe(ui.view);
      // "Turn" spins the model 0.6 radians a second (a turn in about 10 seconds), whatever the frame rate.
      this.frames = frameRate.loop((dt) => {
        if (!this.onScreen) return;
        if (this.spin && this.spin.checked && this.root) this.root.rotation.y += 0.6 * dt;
        this.controls.update();
        this.clampTarget();
        this.renderer.render(this.scene, this.camera);
      });
    }

    // The view controls over OrbitControls (drag rotates, right/Shift-drag and two fingers move,
    // wheel and pinch zoom): a Full screen button at the end of the toolbar; zoom, move and reset
    // buttons in the view's corner; keys on the focused canvas (arrows move, Shift for bigger
    // steps, + and - zoom, 0 resets, F full screen). The wheel zooms in full screen; inline only
    // once the model has been clicked (until the pointer leaves the view) or with Ctrl held, so
    // scrolling the page past the viewer still scrolls the page; a plain wheel there shows a hint.
    addViewControls() {
      const { ui } = this, c = this.renderer.domElement, ctl = this.controls;
      const touch = !!(root.matchMedia && matchMedia('(pointer: coarse)').matches);
      c.tabIndex = 0;
      c.setAttribute('aria-label', '3D model: drag to rotate, right-drag to move, wheel to zoom. ' +
        'Arrow keys move, + and - zoom, 0 resets the view, F is full screen.');
      this.full = null;
      this.fsBtn = el('button', { type: 'button', class: 'mwv-fs' });
      this.fsBtn.onclick = () => this.setFull(!this.full);
      this.fsLabel();
      // The view's own settings sit in its bottom-left corner, apart from the model's in the toolbar
      // (buildBar), so neither runs onto a second line.
      this.fps = frameRate.picker();
      ui.view.append(el('div', { class: 'mwv-opts', role: 'group', 'aria-label': 'View settings' }, this.fps.label, this.fsBtn));
      const btn = (cls, label, name, fn) => {
        const b = el('button', { type: 'button', class: 'mwv-nb mwv-' + cls, title: label, 'aria-label': label });
        b.innerHTML = icon(name);
        b.onclick = fn;
        return b;
      };
      const step = 0.1;
      ui.view.append(
        el('div', { class: 'mwv-nav', role: 'group', 'aria-label': 'View controls' },
          el('div', { class: 'mwv-zoom' }, btn('in', 'Zoom in', 'plus', () => this.zoomBy(0.8)), btn('out', 'Zoom out', 'minus', () => this.zoomBy(1.25))),
          el('div', { class: 'mwv-pad' },
            btn('up', 'Move the view up', 'up', () => this.panBy(0, step)),
            btn('left', 'Move the view left', 'left', () => this.panBy(-step, 0)),
            btn('home', 'Reset the view', 'home', () => this.resetView()),
            btn('right', 'Move the view right', 'right', () => this.panBy(step, 0)),
            btn('down', 'Move the view down', 'down', () => this.panBy(0, -step)))),
        el('div', { class: 'mwv-hint', 'aria-hidden': 'true', text: touch ? 'Drag to rotate · Two fingers to move · Pinch to zoom'
          : 'Drag to rotate · Right-drag to move · Wheel to zoom · Esc to leave' }),
        this.toast = el('div', { class: 'mwv-toast', 'aria-hidden': 'true', text: 'Hold Ctrl and scroll to zoom, or click the model first' }));
      this.toast.addEventListener('animationend', () => this.toast.classList.remove('on'));

      this.engaged = false;
      c.addEventListener('pointerdown', () => { this.engaged = true; ctl.enableZoom = true; c.focus({ preventScroll: true }); });
      c.addEventListener('blur', () => { this.engaged = false; });
      ui.view.addEventListener('pointerleave', () => { this.engaged = false; });
      // Before OrbitControls' own wheel listener on the canvas: it zooms (and stops the page
      // scrolling) only while enableZoom is on.
      ui.view.addEventListener('wheel', (e) => {
        const ok = !!this.full || this.engaged || e.ctrlKey || e.metaKey;
        ctl.enableZoom = ok;
        if (!ok && e.target === c && !this.toast.classList.contains('on')) this.toast.classList.add('on');
      }, { capture: true, passive: true });
      c.addEventListener('keydown', (e) => {
        if (e.altKey || e.ctrlKey || e.metaKey) return;
        const k = e.key, s = e.shiftKey ? 0.25 : 0.08;
        if (k === 'ArrowUp') this.panBy(0, s);
        else if (k === 'ArrowDown') this.panBy(0, -s);
        else if (k === 'ArrowLeft') this.panBy(-s, 0);
        else if (k === 'ArrowRight') this.panBy(s, 0);
        else if (k === '+' || k === '=') this.zoomBy(0.8);
        else if (k === '-' || k === '_') this.zoomBy(1.25);
        else if (k === '0' || k === 'Home') this.resetView();
        else if (k === 'f' || k === 'F') this.setFull(!this.full);
        else return;
        e.preventDefault();
      });
      // The browser's full screen ends on its own (Esc, F11): follow it.
      this.onFsChange = () => {
        const fs = document.fullscreenElement || document.webkitFullscreenElement;
        if (fs === ui.box) { if (this.full) { this.full = 'api'; this.cssFull(false); } }
        else if (this.full === 'api') this.setFull(false);
      };
      document.addEventListener('fullscreenchange', this.onFsChange);
      document.addEventListener('webkitfullscreenchange', this.onFsChange);
      this.onFsKey = (e) => { if (e.key === 'Escape' && this.full === 'css') { e.preventDefault(); this.setFull(false); } };
    }

    fsLabel() {
      const on = !!this.full, b = this.fsBtn;
      b.innerHTML = icon(on ? 'unfull' : 'full') + '<span>' + (on ? 'Exit full screen' : 'Full screen') + '</span>';
      b.setAttribute('aria-pressed', on ? 'true' : 'false');
      b.title = on ? 'Exit full screen (Esc)' : 'Full screen';
    }

    // Full screen: the browser's (Fullscreen API on the viewer) where it's allowed, else the viewer
    // as a fixed layer over the whole window (the game's own browser, iframes, headless).
    setFull(on) {
      if (!!on === !!this.full) return;
      const box = this.ui.box, d = document;
      if (on) {
        this.full = 'pending';
        box.classList.add('mwv-full');
        const req = box.requestFullscreen || box.webkitRequestFullscreen;
        let p = null;
        if (req && d.fullscreenEnabled !== false) { try { p = req.call(box); } catch (e) { p = null; } }
        if (p && p.then) p.then(null, () => { if (this.full === 'pending') this.cssFull(true); });
        else if ((d.fullscreenElement || d.webkitFullscreenElement) !== box) this.cssFull(true);
      } else {
        const api = this.full === 'api';
        this.full = null;
        this.cssFull(false);
        box.classList.remove('mwv-full');
        if (api && (d.fullscreenElement || d.webkitFullscreenElement) === box) {
          const p = (d.exitFullscreen || d.webkitExitFullscreen).call(d);
          if (p && p.catch) p.catch(() => {});
        }
      }
      this.fsLabel();
    }

    // The fixed-layer full screen: page scroll locked, Esc leaves, and the viewer's ancestors lose
    // what would trap a fixed layer under the page (stacking contexts, transforms).
    cssFull(on) {
      const box = this.ui.box;
      if (on) this.full = 'css';
      box.classList.toggle('mwv-css', on);
      document.documentElement.classList.toggle('mwv-lock', on);
      for (let a = box.parentElement; a && a !== document.body; a = a.parentElement) a.classList.toggle('mwv-lift', on);
      if (on) document.addEventListener('keydown', this.onFsKey, true);
      else document.removeEventListener('keydown', this.onFsKey, true);
    }

    // Moves the camera and its target across the screen by fractions of the view's height (up and
    // right positive), kept near the model (clampTarget).
    panBy(fx, fy) {
      const T = this.T, cam = this.camera, ctl = this.controls;
      cam.updateMatrix();
      const h = 2 * cam.position.distanceTo(ctl.target) * Math.tan(cam.fov * Math.PI / 360);
      const d = new T.Vector3().setFromMatrixColumn(cam.matrix, 0).multiplyScalar(fx * h)
        .add(new T.Vector3().setFromMatrixColumn(cam.matrix, 1).multiplyScalar(fy * h));
      ctl.target.add(d);
      cam.position.add(d);
      this.clampTarget();
      ctl.update();
    }

    // Moves the camera toward (k < 1) or away from the target, within the controls' distances.
    zoomBy(k) {
      const cam = this.camera, ctl = this.controls;
      const off = cam.position.clone().sub(ctl.target);
      off.setLength(Math.min(ctl.maxDistance, Math.max(ctl.minDistance, off.length() * k)));
      cam.position.copy(ctl.target).add(off);
      ctl.update();
    }

    resetView() { if (this.root) this.frameCamera(this.root); }

    // The target stays inside the model's box (a quarter of its size around it), the camera moving
    // with it, so panning can't lose the model.
    clampTarget() {
      if (!this.bounds) return;
      const t = this.controls.target, c = this.bounds.clampPoint(t, this.clampTmp || (this.clampTmp = new this.T.Vector3()));
      if (c.equals(t)) return;
      this.camera.position.add(c.sub(t));
      t.add(c);
    }

    buildBar() {
      const { model, ui, opts } = this;
      ui.bar.innerHTML = '';
      this.variantSel = el('select');
      model.variants.forEach((v, i) => this.variantSel.add(new Option(v.label, i)));
      this.variantSel.value = Math.min(model.variants.length - 1, +opts.variant || 0);
      this.statSel = el('select');
      this.sizeSel = el('select');
      this.variantSel.onchange = () => { this.fillStats(); this.build(true); };
      this.statSel.onchange = () => this.build(false);
      this.sizeSel.onchange = () => this.build(false);
      this.dyeBox = el('span', { class: 'mwv-dye-box' });
      this.spin = el('input', { type: 'checkbox' });
      if (opts.spin) this.spin.checked = true;
      ui.bar.append(el('label', null, 'Model ', this.variantSel), this.statLabel = el('label', null, 'Pose ', this.statSel),
        this.sizeLabel = el('label', null, 'Size ', this.sizeSel), this.dyeBox, el('label', null, this.spin, 'Turn'));
      if (model.variants.length < 2) this.variantSel.parentNode.style.display = 'none';
      this.fillStats(opts.stat);
    }

    // The pose picker: "As worn" (the game's states for it: below) and each of the model's own
    // states alone; the size picker for gloves and shoes made in sizes (s with nothing else worn).
    fillStats(want) {
      const v = this.variant();
      const keys = [...new Set(v.meshes.map((m) => m.key1).filter((s) => s && !/^(fist|palm)|^(un)?cover$/.test(s)))];
      const sel = this.statSel;
      sel.innerHTML = '';
      sel.add(new Option('As worn', ''));
      keys.forEach((s) => sel.add(new Option(statName(s), s)));
      this.statLabel.style.display = keys.length ? '' : 'none';
      sel.value = want && keys.includes(want) ? want : '';
      const sizes = ['s', 'm', 'l'].filter((s) => v.meshes.some((m) => m.key2 === s));
      this.sizeSel.innerHTML = '';
      sizes.forEach((s) => this.sizeSel.add(new Option({ s: 'Small', m: 'Medium', l: 'Large' }[s], s)));
      this.sizeLabel.style.display = sizes.length > 1 ? '' : 'none';
      if (sizes.length) this.sizeSel.value = sizes[0];
    }

    // The states the game links for this model: hands open (fists for giants); as worn, the head
    // row of the item's App_WearType, a held tool in the right hand (else the left, else sheathed)
    // and a robe's hood down; or the one state picked.
    states(v) {
      const giant = /giant/i.test(v.field || v.label || ''), keys = new Set(v.meshes.map((m) => m.key1));
      const hands = giant ? ['fistl', 'fistr'] : ['palml', 'palmr'];
      const pick = this.statSel.value;
      if (pick) return [pick, 'uncover', ...hands];
      const tool = ['r', 'lr', 'e', 'le'].find((s) => keys.has(s));
      return [...headStates(this.model.wearType), 'uncover', ...hands].concat(tool ? [tool] : []);
    }

    variant() { return this.model.variants[+this.variantSel.value] || this.model.variants[0]; }

    // Dye pickers for the slots the shown model uses.
    fillDyes(visible) {
      const used = new Map();
      visible.forEach((m) => {
        // Hair takes the hair item's color 1, so it's dye 1 here.
        const s = slotOf(m.colormap) === 'hair' ? 1 : slotOf(m.colormap);
        if (typeof s === 'number' && s > 0) { if (!used.has(s)) used.set(s, new Set()); used.get(s).add(m.bone); }
      });
      const box = this.dyeBox;
      box.innerHTML = '';
      this.pickers = {};
      [...used.keys()].sort().forEach((s) => {
        const input = el('input', { type: 'color', value: this.dyes[s - 1], title: 'Dye ' + s + ': ' + [...used.get(s)].join(', ') });
        input.oninput = () => { this.dyes[s - 1] = input.value; this.applyDyes(); };
        this.pickers[s] = input;
        box.append(el('span', { class: 'mwv-dye' }, 'Dye ' + s, input), ' ');
      });
      if (used.size) {
        box.append(el('button', { type: 'button', text: 'Reset', title: 'The item\'s own colors',
          onclick: () => { this.setDyes(this.defaults); } }));
      }
    }

    setDyes(dyes) {
      [0, 1, 2].forEach((i) => { if (dyes && dyes[i]) this.dyes[i] = dyes[i]; });
      Object.entries(this.pickers || {}).forEach(([s, input]) => { input.value = this.dyes[s - 1]; });
      this.applyDyes();
    }

    applyDyes() {
      if (!this.root) return;
      this.root.traverse((o) => {
        if (!o.isMesh) return;
        const s = o.material.userData.slot;
        const hex = typeof s === 'number' && s > 0 ? this.dyes[s - 1] : s === 'hair' ? this.dyes[0] : FIXED[s];
        o.material.color.setRGB(...hexRgb(hex));
      });
    }

    // A texture as the client samples it: linear, no mipmaps (MipFilter NONE), addressed as the
    // render state's first sampler says (CLAMP on characters, monsters and items); gloss maps WRAP.
    texture(name) {
      const T = this.T, img = this.model.textures[name];
      if (!img) return null;
      if (!this.textures.has(name)) {
        const t = new T.DataTexture(img.px, img.w, img.h);
        const info = this.model.texinfo[name], addr = (info && info.rs && info.rs.addr) || ['WRAP', 'WRAP'];
        const wrap = (a) => (a === 'CLAMP' ? T.ClampToEdgeWrapping : a === 'MIRROR' ? T.MirroredRepeatWrapping : T.RepeatWrapping);
        t.wrapS = wrap(addr[0]); t.wrapT = wrap(addr[1]);
        t.magFilter = T.LinearFilter;
        t.minFilter = T.LinearFilter;
        t.generateMipmaps = false;
        t.flipY = false;    // D3D UVs: v = 0 is the first row
        t.needsUpdate = true;
        this.textures.set(name, t);
      }
      return this.textures.get(name);
    }

    material(m) {
      const T = this.T;
      const info = this.model.texinfo[m.texture], rs = info && info.rs;
      const map = this.texture(m.texture);
      const op = rs && rs.op in OPS ? OPS[rs.op] : 0;
      // Reflective glass (potions: rs_useGlossMapSphereAdd, added onto what's behind): its texture
      // is a mostly black highlight map read by the way the surface faces (a sphere map), so the
      // glass is clear but for a faint tint of its color and the highlights.
      const glass = !!(rs && /sphereadd/i.test(rs.rs || '') && rs.blend && rs.blend[1] === 'ONE');
      // The vertex alpha counts only where the render state's alpha takes it (rs.vertexAlpha:
      // effects, alpha-blend); elsewhere the alpha is the texture's alone, so parts with low vertex
      // alpha (potion bottles and corks, tights, ghost costumes) still show.
      let texAlpha = !(rs && rs.vertexAlpha);
      // Our own choice, not the game's render state: a part on an opaque state whose vertex color
      // is all black with alpha under 255 (the HP 300 potion's bottle, alpha 0~76 around the red
      // inside; 46 parts in all) is drawn see-through, in its color at its vertex alpha (at least
      // a little), so the potion shows inside its glass.
      const opaqueBlend = !(rs && rs.blend && !(rs.blend[0] === 'ONE' && rs.blend[1] === 'ZERO'));
      let shell = false;
      if (texAlpha && opaqueBlend && m.col && m.col.length) {
        let rgb = 0, a = 0;
        for (let i = 0; i < m.col.length; i += 4) { rgb = Math.max(rgb, m.col[i], m.col[i + 1], m.col[i + 2]); a = Math.max(a, m.col[i + 3]); }
        shell = rgb === 0 && a < 255;
      }
      if (shell) texAlpha = false;
      // The second stage: the material's gloss map, MODULATE4X, at Sphere coordinates (the view-space
      // normal) or Gloss ones (the world normal against the main light). Not on glass or shells.
      const gloss = !glass && !shell && rs && rs.op1 === 'MODULATE4X' && rs.gloss ? this.texture('gloss:' + rs.gloss) : null;
      if (gloss) { gloss.wrapS = gloss.wrapT = T.RepeatWrapping; }
      const glossMode = gloss ? (/sphere/i.test(rs.tc1) ? 1 : 2) : 0;
      const mat = new T.MeshLambertMaterial({ map, vertexColors: true, side: T.DoubleSide, alphaTest: 0.5 });
      mat.defines = { MW_OP: glass ? 6 : shell ? 7 : op };
      if (glossMode) mat.defines.MW_GLOSS = glossMode;
      mat.customProgramCacheKey = () => 'mw' + (glass ? 6 : shell ? 7 : op) + (texAlpha ? 't' : '') + glossMode;
      // Shaded as the client shades these parts, with no lights (their materials don't receive
      // the main light, and ambient is 0): the diffuse is the vertex color (an emissive) times the
      // dye, (color + 1) / 512 as CRC_Mesh::ColorOverride has it; the first stage mixes in the
      // texture; the gloss stage scales the result by 4 x the gloss map (0x80 is neutral).
      mat.onBeforeCompile = (s) => {
        if (gloss) s.uniforms.mwGloss = { value: gloss };
        s.fragmentShader = (gloss ? 'uniform sampler2D mwGloss;\n' : '') + s.fragmentShader;
        if (shell) {
          s.fragmentShader = s.fragmentShader.replace('#include <color_fragment>',
            '#if defined( USE_COLOR_ALPHA )\n diffuseColor.a *= max( vColor.a, 0.22 );\n#endif\nvec3 mwD = diffuseColor.rgb * 0.5;');
        } else {
          s.fragmentShader = s.fragmentShader.replace('#include <color_fragment>', [
            '#if defined( USE_COLOR_ALPHA )', ' vec3 mwV = vColor.rgb;' + (texAlpha ? '' : ' diffuseColor.a *= vColor.a;'),
            '#elif defined( USE_COLOR )', ' vec3 mwV = vColor;', '#else', ' vec3 mwV = vec3( 1.0 );', '#endif',
            'vec3 mwD = mwV * ( diffuseColor.rgb * 255.0 + 1.0 ) / 512.0;'].join('\n'));
        }
        // The stored normals as they are, as the game (D3D fixed function, cull none) uses them:
        // three.js turns the normal around on back faces.
        s.fragmentShader = s.fragmentShader.replace('#include <normal_fragment_begin>',
          'float faceDirection = gl_FrontFacing ? 1.0 : - 1.0;\nvec3 normal = normalize( vNormal );\nvec3 nonPerturbedNormal = normal;');
        s.fragmentShader = s.fragmentShader
          .replace('#include <map_fragment>', '#ifdef USE_MAP\n vec4 mwTex = texture2D( map, vMapUv );\n diffuseColor.a *= mwTex.a;\n#endif')
          .replace('#include <opaque_fragment>', [
            'vec3 mwC = mwD;',
            '#ifdef USE_MAP',
            '#if MW_OP == 6', ' mwC = clamp( mwD * 0.44 + texture2D( map, normal.xy * 0.5 + 0.5 ).rgb, 0.0, 1.0 );',
            '#elif MW_OP == 7', ' mwC = clamp( mwD, 0.0, 1.0 );',
            '#elif MW_OP == 0', ' mwC = clamp( mwTex.rgb + mwD - 0.5, 0.0, 1.0 );',
            '#elif MW_OP == 1', ' mwC = clamp( 2.0 * mwTex.rgb * mwD, 0.0, 1.0 );',
            '#elif MW_OP == 2', ' mwC = mwTex.rgb * mwD;',
            '#elif MW_OP == 3', ' mwC = mwTex.rgb;',
            '#elif MW_OP == 4', ' mwC = clamp( 4.0 * mwTex.rgb * mwD, 0.0, 1.0 );',
            '#else', ' mwC = clamp( mwTex.rgb + mwD, 0.0, 1.0 );',
            '#endif', '#endif',
            '#ifdef MW_GLOSS',
            '#if MW_GLOSS == 1',
            ' vec2 mwG = vec2( 0.51 + 0.49 * normal.x, 0.51 - 0.49 * normal.y );',
            '#else',
            ' vec3 mwN = normalize( ( vec4( normal, 0.0 ) * viewMatrix ).xyz );',
            ' vec2 mwG = vec2( 0.0, 0.5 + 0.49 * dot( mwN, normalize( vec3( -0.5, -1.0, -0.559 ) ) ) );',
            '#endif',
            ' mwC = clamp( 4.0 * texture2D( mwGloss, mwG ).rgb * mwC, 0.0, 1.0 );',
            '#endif',
            'outgoingLight = mwC;',
            '#include <opaque_fragment>'].join('\n'));
      };
      applyRS(T, mat, rs);
      if (shell) {
        mat.transparent = true;
        mat.blending = T.NormalBlending;
        mat.alphaTest = 0;
        mat.depthWrite = false;
      }
      mat.userData.slot = slotOf(m.colormap);
      return mat;
    }

    build(frame) {
      const T = this.T;
      if (this.root) {
        this.scene.remove(this.root);
        this.root.traverse((o) => { if (o.geometry) o.geometry.dispose(); if (o.material) o.material.dispose(); });
      }
      const v = this.variant();
      const root = this.root = new T.Group();
      root.scale.set(1, 1, -1); // D3D left-handed -> three.js right-handed (mirror Z)
      // The parts the game links for those states and size; a model none of whose subgroups
      // match shows its first.
      let show = pickSubgroups(v.meshes, this.states(v), this.sizeSel.value || '');
      if (!show.some(Boolean)) {
        const first = Math.min(...v.meshes.map((m) => (m.sub >= 0 ? m.sub : Infinity)));
        show = v.meshes.map((m) => m.sub === first || (first === Infinity && !m.key1));
      }
      const visible = v.meshes.filter((m, i) => show[i]);
      let tris = 0;
      visible.forEach((m) => {
        if (!m.tris) return;
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.BufferAttribute(m.pos, 3));
        g.setAttribute('normal', new T.BufferAttribute(m.nrm, 3));
        g.setAttribute('uv', new T.BufferAttribute(m.uv, 2));
        g.setAttribute('color', new T.BufferAttribute(m.col, 4, true));
        g.setIndex(new T.BufferAttribute(m.idx, 1));
        const o = new T.Mesh(g, this.material(m));
        o.name = m.name;
        root.add(o);
        tris += m.tris;
      });
      this.scene.add(root);
      this.fillDyes(visible);
      this.applyDyes();
      this.ui.info.textContent = tris ? visible.length + ' parts, ' + tris + ' triangles' + hiddenNote(v.hidden)
        : 'Nothing to show for this model: in the game it is only an effect.';
      if (frame) this.frameCamera(root);
    }

    // Points the camera at the model: yaw/pitch from the options, the model fitting the view.
    frameCamera(root) {
      const T = this.T;
      root.updateMatrixWorld(true);
      const o = this.opts, cam = this.camera;
      const box3 = new T.Box3().setFromObject(root), size = box3.getSize(new T.Vector3()), c = box3.getCenter(new T.Vector3());
      const r = Math.max(size.x, size.y, size.z) || 50;
      const yaw = (o.yaw != null ? +o.yaw : 35) * Math.PI / 180, pitch = (o.pitch != null ? +o.pitch : 15) * Math.PI / 180;
      const dist = (o.zoom ? +o.zoom : 1) * r / Math.tan(cam.fov * Math.PI / 360) * 0.75;
      cam.position.set(c.x + dist * Math.sin(yaw) * Math.cos(pitch), c.y + dist * Math.sin(pitch), c.z + dist * Math.cos(yaw) * Math.cos(pitch));
      cam.near = r / 100; cam.far = r * 100; cam.updateProjectionMatrix();
      this.bounds = box3.clone().expandByScalar(r * 0.25);
      this.controls.target.copy(c); this.controls.minDistance = r * 0.15; this.controls.maxDistance = r * 8; this.controls.update();
    }

    // On every size change of the view (the page's layout, full screen on and off), at the
    // screen's current pixel ratio (it changes when the window moves to another screen).
    resize() {
      const w = this.ui.view.clientWidth, h = this.ui.view.clientHeight;
      if (!w || !h) return;
      const ratio = Math.min(2, root.devicePixelRatio || 1);
      if (this.renderer.getPixelRatio() !== ratio) this.renderer.setPixelRatio(ratio);
      this.renderer.setSize(w, h, false);
      this.camera.aspect = w / h;
      this.camera.updateProjectionMatrix();
    }

    dispose() {
      this.setFull(false);
      document.removeEventListener('fullscreenchange', this.onFsChange);
      document.removeEventListener('webkitfullscreenchange', this.onFsChange);
      this.frames.stop();
      this.fps.release();
      this.visibility.disconnect();
      this.resizeObserver.disconnect();
      this.controls.dispose();
      if (this.root) this.root.traverse((o) => { if (o.geometry) o.geometry.dispose(); if (o.material) o.material.dispose(); });
      this.textures.forEach((t) => t.dispose());
      this.renderer.dispose();
    }
  }

  // A race: its own mesh plus what it wears, on its skeleton, in the bind pose or the first frame
  // of one of its animations. Color pickers for the body's colors (and skin) that its mesh uses.
  class RaceViewer extends Viewer {
    constructor(T, model, ui, opts, packs) {
      // Standing by default where the race has it: in the bind pose held weapons sit off the hand
      // (the ogres' hammers, Skeith's), and alternate bodies all show (the mini ghosts).
      opts = { ...opts, pose: opts.pose || 'StandFriendly01' };
      model.skinDefault = model.skin;
      model.poses = [{ element: 'bind', label: 'Bind pose', path: null }];
      super(T, model, ui, opts);
      this.packs = packs;
      // The poses need db/motion.xml (read once, then kept in IndexedDB): after the first frame.
      racePoses(packs, model).then((poses) => {
        if (this.disposed || !poses.length) return;
        model.poses.push(...poses);
        this.fillPoses();
        if (opts.pose !== 'bind' && poses.some((p) => p.element === opts.pose)) { this.poseSel.value = opts.pose; this.setPose(true); }
      }).catch((e) => root.console && console.warn('[MabiWikiViewer] poses', e));
    }

    buildBar() {
      const { ui, opts } = this;
      ui.bar.innerHTML = '';
      this.mats = null;
      this.poseSel = el('select');
      // A new pose can change the model's shape (wings spread or folded): frame it again.
      this.poseSel.onchange = () => this.setPose(true);
      this.dyeBox = el('span', { class: 'mwv-dye-box' });
      this.spin = el('input', { type: 'checkbox' });
      if (opts.spin) this.spin.checked = true;
      ui.bar.append(this.poseLabel = el('label', null, 'Pose ', this.poseSel), this.dyeBox, el('label', null, this.spin, 'Turn'));
      this.fillPoses();
    }

    fillPoses() {
      const sel = this.poseSel, v = sel.value;
      sel.innerHTML = '';
      this.model.poses.forEach((p) => sel.add(new Option(p.label, p.element)));
      sel.value = v || 'bind';
      this.poseLabel.style.display = this.model.poses.length > 1 ? '' : 'none';
    }

    async setPose(reframe) {
      const pose = this.model.poses.find((p) => p.element === this.poseSel.value);
      const mats = await loadPose(this.packs, this.model, pose);
      if (this.disposed) return;
      this.mats = mats;
      this.build(false);
      if (reframe === true) this.frameCamera(this.root);
    }

    // Pickers for the body's colors 1-3 and skin, as far as the race's own mesh uses them.
    fillDyes() {
      const used = new Set();
      this.model.parts.forEach((p) => {
        if (p.group !== 0 || !p.visible) return;
        const s = slotOf(p.colormap);
        if ((typeof s === 'number' && s > 0) || s === 'skin') used.add(s);
      });
      const box = this.dyeBox;
      box.innerHTML = '';
      this.pickers = {};
      if (this.skinValue == null) this.skinValue = this.opts.skin || this.model.skin;
      const slots = [...used].sort((a, b) => (a === 'skin') - (b === 'skin') || a - b);
      slots.forEach((s) => {
        const value = s === 'skin' ? this.skinValue : this.dyes[s - 1];
        const input = el('input', { type: 'color', value, title: s === 'skin' ? 'Skin color' : 'Body color ' + s });
        input.oninput = () => { if (s === 'skin') this.skinValue = input.value; else this.dyes[s - 1] = input.value; this.applyDyes(); };
        this.pickers[s] = input;
        box.append(el('span', { class: 'mwv-dye' }, s === 'skin' ? 'Skin' : 'Color ' + s, input), ' ');
      });
      if (slots.length) {
        box.append(el('button', { type: 'button', text: 'Reset', title: this.model.colorSource === 'none' ? 'The texture as it is (the game sets monster colors itself)' : 'The default colors',
          onclick: () => { this.skinValue = this.model.skinDefault; this.setDyes(this.defaults); } }));
      }
    }

    setDyes(dyes) {
      [0, 1, 2].forEach((i) => { if (dyes && dyes[i]) this.dyes[i] = dyes[i]; });
      Object.entries(this.pickers || {}).forEach(([s, input]) => { input.value = s === 'skin' ? this.skinValue : this.dyes[s - 1]; });
      this.applyDyes();
    }

    applyDyes() {
      if (!this.root) return;
      const groups = this.model.groups;
      this.root.traverse((o) => {
        if (!o.isMesh) return;
        const { slot, group } = o.material.userData;
        let hex;
        if (slot === 'skin') hex = this.skinValue;
        else if (slot === 'hair') hex = this.model.hair;
        else if (typeof slot === 'number' && slot > 0) hex = group === 0 ? this.dyes[slot - 1] : groups[group].colors[slot - 1];
        else hex = FIXED[0];
        o.material.color.setRGB(...hexRgb(hex));
      });
    }

    build(frame) {
      const T = this.T;
      if (this.root) {
        this.scene.remove(this.root);
        this.root.traverse((o) => { if (o.geometry) o.geometry.dispose(); if (o.material) o.material.dispose(); });
      }
      const root = this.root = new T.Group();
      root.scale.set(1, 1, -1); // D3D left-handed -> three.js right-handed (mirror Z)
      let tris = 0, parts = 0;
      // A pose hides parts by sending their bones far off (the mini ghosts' spare bodies go
      // thousands of units away): parts that end up over 10 model sizes from the bind-pose model
      // aren't drawn, so they can't shrink the view to a speck.
      const far = this.mats ? this.bindBox() : null;
      const all = poseModel(this.model, this.mats);
      this.model.parts.forEach((m, pi) => {
        if (!m.visible || !m.tris) return;
        const posed = all[pi];
        if (far) {
          let x = 0, y = 0, z = 0;
          for (let i = 0; i < posed.pos.length; i += 3) { x += posed.pos[i]; y += posed.pos[i + 1]; z += posed.pos[i + 2]; }
          const k = posed.pos.length / 3;
          if (Math.hypot(x / k - far.c[0], y / k - far.c[1], z / k - far.c[2]) > far.r * 10) return;
        }
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.BufferAttribute(posed.pos, 3));
        g.setAttribute('normal', new T.BufferAttribute(posed.nrm, 3));
        g.setAttribute('uv', new T.BufferAttribute(m.uv, 2));
        g.setAttribute('color', new T.BufferAttribute(m.col, 4, true));
        g.setIndex(new T.BufferAttribute(m.idx, 1));
        const mat = this.material(m);
        mat.userData.group = m.group;
        const o = new T.Mesh(g, mat);
        o.name = m.name;
        root.add(o);
        tris += m.tris; parts++;
      });
      this.scene.add(root);
      if (frame) this.fillDyes();
      this.applyDyes();
      this.ui.info.textContent = parts + ' parts, ' + tris + ' triangles' + hiddenNote(this.model.hidden);
      if (frame) this.frameCamera(root);
    }

    // The visible parts' centre and size in the bind pose (kept).
    bindBox() {
      if (this.bindBoxCache) return this.bindBoxCache;
      const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
      for (const m of this.model.parts) {
        if (!m.visible || !m.tris) continue;
        for (let i = 0; i < m.pos.length; i += 3) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], m.pos[i + k]); hi[k] = Math.max(hi[k], m.pos[i + k]); }
      }
      const r = Math.max(hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]);
      return (this.bindBoxCache = Number.isFinite(r) ? { c: [0, 1, 2].map((k) => (lo[k] + hi[k]) / 2), r: Math.max(r, 1) } : null);
    }

    dispose() { this.disposed = true; super.dispose(); }
  }

  // A prop, drawn as region3d.js's walker draws it (propMaterial; one placement, at the prop's
  // colors). No dyes; Turn in the toolbar. Standing props are framed a little wider than items (a
  // well's or a tree's base comes nearer the camera than its middle); flat ones (herb patches, ore
  // spots, farm plots) are seen more from above, so they don't show edge on, and closer.
  class PropViewer extends Viewer {
    constructor(T, model, ui, opts) {
      const lo = [Infinity, Infinity, Infinity], hi = [-Infinity, -Infinity, -Infinity];
      for (const m of model.meshes) for (let i = 0; i < m.pos.length; i += 3) for (let k = 0; k < 3; k++) { lo[k] = Math.min(lo[k], m.pos[i + k]); hi[k] = Math.max(hi[k], m.pos[i + k]); }
      const flat = hi[1] - lo[1] < 0.35 * Math.max(hi[0] - lo[0], hi[2] - lo[2]);
      opts = { ...opts, pitch: opts.pitch != null ? opts.pitch : flat ? 40 : 18, zoom: opts.zoom != null ? opts.zoom : flat ? 0.85 : 1.2 };
      super(T, model, ui, opts);
    }

    buildBar() {
      const { ui, opts } = this;
      ui.bar.innerHTML = '';
      this.spin = el('input', { type: 'checkbox' });
      if (opts.spin) this.spin.checked = true;
      ui.bar.append(el('label', null, this.spin, 'Turn'));
    }

    fillDyes() {}
    setDyes() {}
    applyDyes() {}

    // Mipmapped, as in the walker: props' textures tile, and a tree seen whole shrinks its leaves.
    texture(name) {
      const T = this.T, img = this.model.textures[name];
      if (!img) return null;
      if (!this.textures.has(name)) {
        const t = new T.DataTexture(img.px, img.w, img.h);
        t.wrapS = t.wrapT = T.RepeatWrapping;
        t.magFilter = T.LinearFilter; t.minFilter = T.LinearMipmapLinearFilter; t.generateMipmaps = true;
        t.anisotropy = Math.min(8, this.renderer.capabilities.getMaxAnisotropy());
        t.flipY = false;
        t.needsUpdate = true;
        this.textures.set(name, t);
      }
      return this.textures.get(name);
    }

    build(frame) {
      const T = this.T, model = this.model;
      if (this.root) {
        this.scene.remove(this.root);
        this.root.traverse((o) => { if (o.geometry) o.geometry.dispose(); if (o.material) o.material.dispose(); });
      }
      const root = this.root = new T.Group();
      root.scale.set(1, 1, -1); // D3D left-handed -> three.js right-handed (mirror Z)
      const colors = this.opts.colors || Array(9).fill(0x808080);
      let tris = 0, parts = 0;
      for (const m of model.meshes) {
        if (!m.tris) continue;
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.BufferAttribute(m.pos, 3));
        g.setAttribute('normal', new T.BufferAttribute(m.nrm, 3));
        g.setAttribute('uv', new T.BufferAttribute(m.uv, 2));
        g.setAttribute('color', new T.BufferAttribute(m.col, 4, true));
        g.setIndex(new T.BufferAttribute(m.idx, 1));
        const info = model.texinfo[m.texture], rs = info && info.rs;
        const { mat, unlit } = propMaterial(T, rs, this.texture(m.texture), rs && rs.gloss ? this.texture('gloss:' + rs.gloss) : null);
        // One placement (an instanced mesh, as the walker's), so the prop's color reaches the part
        // the way it does there (the instance color).
        const o = new T.InstancedMesh(g, mat, 1);
        o.setMatrixAt(0, new T.Matrix4());
        if (unlit) o.instanceColor = new T.InstancedBufferAttribute(Float32Array.from(partColor(m.colormap, colors)), 3);
        o.computeBoundingBox();
        o.computeBoundingSphere();
        o.name = m.name;
        root.add(o);
        tris += m.tris; parts++;
      }
      this.scene.add(root);
      this.ui.info.textContent = parts + ' parts, ' + tris + ' triangles' + hiddenNote(model.hidden);
      if (frame) this.frameCamera(root);
    }
  }

  // D3D9 render state (material/_define) -> three.js material settings.
  function applyRS(T, mat, rs) {
    if (!rs) return;
    const F = { ZERO: T.ZeroFactor, ONE: T.OneFactor, SRCCOLOR: T.SrcColorFactor, INVSRCCOLOR: T.OneMinusSrcColorFactor,
      SRCALPHA: T.SrcAlphaFactor, INVSRCALPHA: T.OneMinusSrcAlphaFactor, DESTALPHA: T.DstAlphaFactor,
      INVDESTALPHA: T.OneMinusDstAlphaFactor, DESTCOLOR: T.DstColorFactor, INVDESTCOLOR: T.OneMinusDstColorFactor };
    const OP = { ADD: T.AddEquation, SUBTRACT: T.SubtractEquation, REVSUBTRACT: T.ReverseSubtractEquation, MIN: T.MinEquation, MAX: T.MaxEquation };
    if (rs.blend && !(rs.blend[0] === 'ONE' && rs.blend[1] === 'ZERO')) {
      mat.transparent = true;
      mat.blending = T.CustomBlending;
      mat.blendSrc = F[rs.blend[0]] ?? T.SrcAlphaFactor;
      mat.blendDst = F[rs.blend[1]] ?? T.OneMinusSrcAlphaFactor;
      mat.blendEquation = OP[rs.blend[2]] ?? T.AddEquation;
    }
    mat.depthWrite = rs.zwrite;
    mat.depthTest = rs.ztest;
    mat.alphaTest = rs.alphatest != null ? rs.alphatest : 0;
    // Additive (dest ONE: potion glass, glows) mustn't hide what's behind it.
    if (mat.transparent && mat.blendDst === T.OneFactor) mat.depthWrite = false;
  }

  root.MabiWikiViewer = {
    open,
    openRace,
    openProp,
    usePackFiles,
    usePacks,
    // For the export page: whether the game's files are open ('ready', 'allow', 'choose'), the kept
    // game folder's handle, using a folder as the game folder, and writing the models.
    // packs(): this session's open packs (after state() says 'ready'); allow() and choose(): from a
    // click, the kept folder again or a new one (region3d.js uses these, the same folder as here).
    // Without showDirectoryPicker (canChoose() false) the page picks the package folder with
    // <input webkitdirectory> and gives its files to useFiles().
    access: { state: accessState, kept: () => idbGet('folder').catch(() => null), useFolder, packs: () => packsPromise, allow: allowKept, choose: chooseFolder,
      canChoose: () => !!root.showDirectoryPicker, useFiles: usePackFiles },
    exportModels,
    frameRate, // the frame rate the 3D views draw at (region3d.js uses it too)
    zip: (prefix) => P().zipWriter(prefix), // exportModels' dir where the browser can't write to a folder
    ready: packsLib, // packs.js loaded
    core: { get Packs() { return P().Packs; }, get HostedPacks() { return packClasses().HostedPacks; }, mtLowBytes, parsePmg, parseFrm, decodeDds, convertItem, findRow, slotOf, summarizeRenderState,
      convertRace, raceRow, parseRaceScript, parseSkeleton, parseAni, poseMatrices, posePart, poseModel, loadPose, racePoses, motionIndex, findMotion,
      convertMesh, byBase, materials, texture, applyRS, ensureThree, asText, propClasses, setSkeletons, setStatics, propModel, convertProp, propMaterial, partColor },
  };
})(typeof window !== 'undefined' ? window : globalThis);
