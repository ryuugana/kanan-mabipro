/* Mabinogi Wiki: walk a region in 3D (full screen, free flight).
 *
 *   MabiWikiRegion.open(regionFolder, options) -> { close() }
 *
 * regionFolder is the region's folder under data/world (uladh_main for Tir Chonaill). The ground
 * and the props come straight from the player's own packs, read in the browser with viewer3d.js's
 * pack reader and model converter (the same game folder the 3D models use); nothing is converted
 * ahead of time. options: x, y (where to start, in game units), title.
 *
 * The formats were read in the client's own code (the debug DLLs of the client build that runs a
 * server's NPCs): see the readers below.
 *
 * MabiWikiRegion.core: the readers and the ground builder without any page (used by the Node test).
 */
(function (root) {
  'use strict';

  const V = () => root.MabiWikiViewer;

  // ======================================================================================
  // .area files (data/world/<region>/<area>.area)
  // ======================================================================================
  // Read as the client reads them (_Terrain/Mabinogi_AreaContext_Load.cpp, CArea::LoadAreaPlaneInfo,
  // CPlaneLoader::Load and LoadDesc). Little-endian; strings are UTF-16 ending in a 0 character.
  //
  //   header   u16 version (0xCA), u16, u32 file size, u32 area id (region << 16 | area), server
  //            name, area name, u32 planes along x, u32 planes along y, u32, u32, u32 event count,
  //            u32 prop count, f32 lowest and f32 highest ground, u32 x 3, then the area's four
  //            corners (x, height, y) x 4: (0,0) (0,1) (1,1) (1,0).
  //   props    u32 version (0xCA), u32 count; each: u32 class id (db/propdb.xml ClassID), the
  //            entity part, u8 x 2, f32 scale, f32 direction (radians), f32 x 6 its bounding box
  //            (min x, height, y, max x, height, y), u32, u32 x 9 colors, title, state, parameters.
  //   events   (count in the header) each: the entity part, u32 type, parameters.
  //   planes   x-major (for each x, each y): see readPlane.
  //
  //   entity part:  u64 id, name, f32 x, y, height, u8 shape count, u32, then shapes of
  //                 f32 x 6 (direction x 4, half lengths x 2), u32, f32 x 6 (centre, box).
  //   parameters:   u8 count; each u8, u32 event type, u32 signal type, name, XML.
  //
  // Coordinates: x and y are the ground (the minimap's axes, y up the map); height is up. The
  // game's 3D space is (x, height, y), left-handed.

  const PLANE = 800;  // IArea::PlaneWidth
  const CELL = 200;   // a plane is 4 x 4 cells, so 5 x 5 height points

  class Reader {
    constructor(d) { this.d = d; this.v = new DataView(d.buffer, d.byteOffset, d.byteLength); this.p = 0; }
    u8() { return this.d[this.p++]; }
    u16() { const x = this.v.getUint16(this.p, true); this.p += 2; return x; }
    u32() { const x = this.v.getUint32(this.p, true); this.p += 4; return x; }
    // 64-bit ids as hex strings (a JavaScript number loses their low bits).
    u64() { const lo = this.v.getUint32(this.p, true), hi = this.v.getUint32(this.p + 4, true); this.p += 8; return hi.toString(16).padStart(8, '0') + lo.toString(16).padStart(8, '0'); }
    f32() { const x = this.v.getFloat32(this.p, true); this.p += 4; return x; }
    fs(n) { const a = []; for (let i = 0; i < n; i++) a.push(this.f32()); return a; }
    str() {
      let e = this.p;
      while (e + 1 < this.d.length && (this.d[e] || this.d[e + 1])) e += 2;
      const s = new TextDecoder('utf-16le').decode(this.d.subarray(this.p, e));
      this.p = e + 2;
      return s;
    }
  }

  function readEntity(r) {
    const e = { id: r.u64(), name: r.str(), pos: r.fs(3), shapes: [] };
    const n = r.u8(); r.u32();
    for (let i = 0; i < n; i++) { const a = r.fs(6); r.u32(); e.shapes.push(a.concat(r.fs(6))); }
    return e;
  }
  function readParams(r) {
    const out = [];
    for (let n = r.u8(); n > 0; n--) out.push({ flag: r.u8(), event: r.u32(), signal: r.u32(), name: r.str(), xml: r.str() });
    return out;
  }

  // A plane (CPlaneLoader::Load, LoadDesc):
  //   u8, u8, u8 x 4 its textures (tile ids, db/tileindex.data), u8 has a terrain block (then its
  //   u64 prop id: the ground there is that prop's mesh), u8 custom tiling, 16 bytes (custom: each
  //   cell's texture, 0 or 1, cell (i, j) at i * 4 + j), f32 lowest and highest point, 25 points of
  //   16 bytes (f32 height, f32 normal x, f32 normal z, u32 color; point (i, j) at i * 5 + j, i
  //   along x), then 8 x 8 pairs of bytes: a and b at [r][c] (r along x). Auto tiling: a marks
  //   each corner of each cell (r = 2i..2i+1, c = 2j..2j+1) as the second ground or the first.
  //   Custom: a picks the tile in the texture, b its rotation.
  //
  // The wide form (newer areas: the Drama Iria season 2 scenes) starts with a u32 (a running plane
  // number) and stores the four textures as u32s: 576 bytes, 16 more (planeFormat tells them apart).
  function readPlane(r, wide) {
    const p = { tex: [0, 0, 0, 0] };
    if (wide) r.u32();
    r.u8(); r.u8();
    for (let i = 0; i < 4; i++) p.tex[i] = wide ? r.u32() : r.u8();
    p.block = r.u8() ? r.u64() : '';
    p.custom = !!r.u8();
    p.cellTex = r.d.slice(r.p, r.p + 16); r.p += 16;
    p.lo = r.f32(); p.hi = r.f32();
    p.h = new Float32Array(25); p.nx = new Float32Array(25); p.nz = new Float32Array(25); p.col = new Uint32Array(25);
    for (let i = 0; i < 25; i++) { p.h[i] = r.f32(); p.nx[i] = r.f32(); p.nz[i] = r.f32(); p.col[i] = r.u32(); }
    p.a = new Uint8Array(64); p.b = new Uint8Array(64);
    for (let i = 0; i < 64; i++) { p.a[i] = r.u8(); p.b[i] = r.u8(); }
    return p;
  }

  // Which plane form the file uses (the header doesn't say): the narrow one (560 bytes, +8 with a
  // terrain block) or the wide one (576, +8); true for wide. The planes must fit in the file (a few
  // bytes, up to some hundred, follow the last one), and the right form reads as planes: flags of 0
  // or 1, the lowest point under the highest, heights between them (the first 16 planes counted; the
  // narrow form wins a tie, and a few planes of some areas break the height rule).
  function planeFormat(d, at, count) {
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    const odd = (wide) => {
      const size = wide ? 576 : 560, head = wide ? 22 : 6;
      let o = at, bad = 0;
      for (let i = 0; i < count; i++) {
        if (o + size > d.length) return Infinity;
        const blk = d[o + head];
        if (i < 16) {
          const c = o + head + 1 + (blk ? 8 : 0), lo = dv.getFloat32(c + 17, true), hi = dv.getFloat32(c + 21, true);
          if (blk > 1 || d[c] > 1 || !(lo <= hi)) bad += 25;
          else for (let k = 0; k < 25; k++) { const h = dv.getFloat32(c + 25 + k * 16, true); if (!(h >= lo - 1 && h <= hi + 1)) bad++; }
        }
        o += size + (blk ? 8 : 0);
      }
      return bad;
    };
    const narrow = odd(false), wide = odd(true);
    if (narrow === Infinity && wide === Infinity) throw new Error('unknown plane layout');
    return wide < narrow;
  }

  function readArea(d) {
    const r = new Reader(d);
    const h = { version: r.u16() };
    r.u16(); r.u32();
    h.id = r.u32(); h.server = r.str(); h.name = r.str();
    h.px = r.u32(); h.py = r.u32();
    r.u32(); r.u32();
    h.events = r.u32(); h.props = r.u32();
    h.lo = r.f32(); h.hi = r.f32();
    r.u32(); r.u32(); r.u32();
    h.corners = [r.fs(3), r.fs(3), r.fs(3), r.fs(3)];
    h.x0 = Math.min(...h.corners.map((c) => c[0])); h.y0 = Math.min(...h.corners.map((c) => c[2]));
    // The props' version: 0xCA, or 0xCB in the newer areas (same layout).
    const pv = h.version === 0xca ? r.u32() : 0;
    if (pv !== 0xca && pv !== 0xcb) throw new Error('not an area file');
    const count = r.u32();
    const props = [];
    for (let i = 0; i < count; i++) {
      const cls = r.u32(), e = readEntity(r);
      e.cls = cls; r.u8(); r.u8();
      e.scale = r.f32(); e.dir = r.f32(); e.box = r.fs(6); r.u32();
      e.colors = []; for (let k = 0; k < 9; k++) e.colors.push(r.u32());
      e.title = r.str(); e.state = r.str(); e.params = readParams(r);
      props.push(e);
    }
    const events = [];
    for (let i = 0; i < h.events; i++) { const e = readEntity(r); e.type = r.u32(); e.params = readParams(r); events.push(e); }
    const wide = planeFormat(d, r.p, h.px * h.py);
    const planes = [];
    for (let x = 0; x < h.px; x++) { const col = []; for (let y = 0; y < h.py; y++) col.push(readPlane(r, wide)); planes.push(col); }
    return { ...h, propList: props, eventList: events, planes };
  }

  // ======================================================================================
  // Ground textures (CPlaneTexDesc, CPlane::FillRenderMeshImmediate)
  // ======================================================================================
  // A plane's four textures are [first ground, first-to-second transition, second-to-? atlas,
  // second ground]; each transition texture is a 4 x 4 atlas of 128-pixel tiles: column 0 the first
  // ground, columns 1-3 edges, inside and outside corners; the four rows are variants.
  //
  // Auto tiling: when all 16 cells are alike, the plane shows one texture (the first ground, or
  // the second when every corner is marked) stretched over it, cell (i, j) taking atlas tile
  // (column i, row 3 - j). Otherwise each cell's corners make a 4-bit pattern (marked [2i+1][2j]
  // 8, [2i][2j] 4, [2i+1][2j+1] 2, [2i][2j+1] 1); 15 draws the third texture's column 0, the rest
  // the second texture: PATTERN gives the column and turn, the row is random (the game rolls it;
  // here a fixed seed per plane, so the ground doesn't change between visits). 6 and 9 (diagonal)
  // draw like 0 and 15 (the column-0 tile, a random turn).
  //
  // Custom tiling: each cell picks its texture (cellTex: 0 the first, 1 the fourth), its tile
  // (a: column [2i+1][2j] * 2 + [2i][2j], row [2i+1][2j+1] * 2 + [2i][2j+1]) and its turn (b: the
  // first marked of [2i][2j] 0, [2i+1][2j] 90, [2i+1][2j+1] 180, [2i][2j+1] 270 degrees).

  const PATTERN = [[1, -1], [2, 180], [2, 270], [3, 180], [2, 90], [3, 90], [1, -1], [4, 180],
    [2, 0], [1, -1], [3, 270], [4, 270], [3, 0], [4, 90], [4, 0], [1, -1]];
  const EPS = 1 / 1024;

  // A tile's four UV corners (corner k = [u, v]), turned: 90 takes each corner from the one before,
  // 180 from two on, 270 from the next.
  function tileUV(col, row, turn) {
    const uL = col * 0.25 + EPS, uR = (col + 1) * 0.25 - EPS, vA = row * 0.25 + EPS, vB = (row + 1) * 0.25 - EPS;
    const c = [[uL, vA], [uR, vA], [uR, vB], [uL, vB]];
    const s = turn === 90 ? 3 : turn === 180 ? 2 : turn === 270 ? 1 : 0;
    return [0, 1, 2, 3].map((k) => c[(k + s) % 4]);
  }

  // A small deterministic random number source (xorshift32), seeded per plane.
  function rng(seed) {
    let s = (seed >>> 0) || 0x9e3779b9;
    return (n) => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s % n; };
  }

  // A growing mesh in the compact form the ground is drawn from: pos f32 x 3 (game x, height, y),
  // nrm i8 x 4 (unit normal x 127, 0), uv u16 x 2 (x 65535), col u8 x 4 (the point's color, 0x80
  // neutral; alpha), idx u32. done() trims the arrays to what was used.
  class MeshBuf {
    constructor() { this.n = 0; this.t = 0; this.grow(256, 384); }
    grow(nv, ni) {
      const cp = (A, old, k) => { const a = new A(k); if (old) a.set(old.subarray(0, Math.min(old.length, k))); return a; };
      this.pos = cp(Float32Array, this.pos, nv * 3); this.nrm = cp(Int8Array, this.nrm, nv * 4);
      this.uv = cp(Uint16Array, this.uv, nv * 2); this.col = cp(Uint8Array, this.col, nv * 4);
      this.idx = cp(Uint32Array, this.idx, ni); this.cv = nv; this.ci = ni;
    }
    room(nv, ni) { if (this.n + nv > this.cv || this.t + ni > this.ci) this.grow(Math.max(this.cv * 2, this.n + nv), Math.max(this.ci * 2, this.t + ni)); }
    // A plane point (pt = i * 5 + j) at (x, y) with the given UV: its index.
    vert(p, pt, x, y, u, v) {
      const k = this.n++, c = p.col[pt], nx = p.nx[pt], nz = p.nz[pt], l = 127 / Math.sqrt(nx * nx + 1 + nz * nz);
      this.pos[k * 3] = x; this.pos[k * 3 + 1] = p.h[pt]; this.pos[k * 3 + 2] = y;
      this.nrm[k * 4] = Math.round(nx * l); this.nrm[k * 4 + 1] = Math.round(l); this.nrm[k * 4 + 2] = Math.round(nz * l);
      this.uv[k * 2] = Math.round(Math.min(1, Math.max(0, u)) * 65535); this.uv[k * 2 + 1] = Math.round(Math.min(1, Math.max(0, v)) * 65535);
      // The point's color (0x80 is neutral); an alpha byte of 1 means opaque.
      const a = c >>> 24;
      this.col[k * 4] = (c >>> 16) & 255; this.col[k * 4 + 1] = (c >>> 8) & 255; this.col[k * 4 + 2] = c & 255; this.col[k * 4 + 3] = a === 1 ? 255 : a;
      return k;
    }
    tri(a, b, c) { this.idx[this.t++] = a; this.idx[this.t++] = b; this.idx[this.t++] = c; }
    done() {
      return { pos: this.pos.slice(0, this.n * 3), nrm: this.nrm.slice(0, this.n * 4), uv: this.uv.slice(0, this.n * 2),
        col: this.col.slice(0, this.n * 4), idx: this.idx.slice(0, this.t), verts: this.n };
    }
  }

  // The ground of one area as meshes per texture: { tile id: { pos, nrm, uv, col, idx, verts } }
  // (MeshBuf's form, game coordinates). Planes standing on a terrain block are left out (the
  // block's mesh is the ground there), and so are tiles skip(tile id) says no to.
  //
  // A plane showing one stretched texture shares its 25 points between its cells (the UVs meet:
  // point (i, j) is at (i / 4, 1 - j / 4) of the texture), and when it is also flat, level and of
  // one color it is a single quad: the same picture with far fewer points (most of a big field).
  function buildGround(area, skip) {
    const bufs = new Map();
    const get = (t) => {
      let m = bufs.get(t);
      if (!m) bufs.set(t, (m = new MeshBuf()));
      return m;
    };
    const at = new Int32Array(25);
    for (let x = 0; x < area.px; x++) {
      for (let y = 0; y < area.py; y++) {
        const p = area.planes[x][y];
        if (p.block) continue;
        const ox = area.x0 + x * PLANE, oy = area.y0 + y * PLANE;
        // Auto: all cells alike (CountTileSet_Auto: 16 cells with no corner marked, or 16 with all four).
        // p.a holds the plane's 8 x 8 corner marks, row by row ([r][c] at r * 8 + c): cell (i, j) owns
        // the 2 x 2 block at rows 2i and 2i + 1, columns 2j and 2j + 1, so with e = [2i][2j] its marks
        // are e ([2i][2j]), e + 1 ([2i][2j+1]), e + 8 ([2i+1][2j]) and e + 9 ([2i+1][2j+1]). The same
        // e is used below for the tiling (the [..][..] names in the comment above the PATTERN table).
        let all = 0, none = 0;
        for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) {
          const e = 2 * i * 8 + 2 * j, n = p.a[e] + p.a[e + 1] + p.a[e + 8] + p.a[e + 9];
          if (n === 4) all++; else if (n === 0) none++;
        }
        if (!p.custom && (all === 16 || none === 16)) {
          const tex = all === 16 ? p.tex[3] : p.tex[0];
          if (!tex || (skip && skip(tex))) continue;
          const m = get(tex);
          let flat = true;
          for (let k = 1; k < 25 && flat; k++) flat = p.h[k] === p.h[0] && p.nx[k] === p.nx[0] && p.nz[k] === p.nz[0] && p.col[k] === p.col[0];
          if (flat && p.nx[0] === 0 && p.nz[0] === 0) {
            m.room(4, 6);
            const a = m.vert(p, 0, ox, oy, 0, 1), b = m.vert(p, 20, ox + PLANE, oy, 1, 1), c = m.vert(p, 24, ox + PLANE, oy + PLANE, 1, 0), d = m.vert(p, 4, ox, oy + PLANE, 0, 0);
            m.tri(a, d, b); m.tri(d, c, b);
            continue;
          }
          m.room(25, 96);
          for (let i = 0; i <= 4; i++) for (let j = 0; j <= 4; j++) at[i * 5 + j] = m.vert(p, i * 5 + j, ox + i * CELL, oy + j * CELL, i / 4, 1 - j / 4);
          for (let i = 0; i < 4; i++) for (let j = 0; j < 4; j++) {
            const q0 = at[i * 5 + j], q1 = at[(i + 1) * 5 + j], q2 = at[(i + 1) * 5 + j + 1], q3 = at[i * 5 + j + 1];
            m.tri(q0, q3, q1); m.tri(q3, q2, q1);
          }
          continue;
        }
        const roll = rng((area.id * 131 + x) * 977 + y);
        for (let i = 0; i < 4; i++) {
          for (let j = 0; j < 4; j++) {
            const e = 2 * i * 8 + 2 * j;
            let tex, uv;
            if (p.custom) {
              tex = p.cellTex[i * 4 + j] ? p.tex[3] : p.tex[0];
              const col = p.a[e + 8] * 2 + p.a[e], row = p.a[e + 9] * 2 + p.a[e + 1];
              const turn = p.b[e] ? 0 : p.b[e + 8] ? 90 : p.b[e + 9] ? 180 : p.b[e + 1] ? 270 : 0;
              uv = tileUV(col, 3 - row, turn);
            } else {
              const pat = p.a[e + 8] * 8 + p.a[e] * 4 + p.a[e + 9] * 2 + p.a[e + 1];
              const [kind, turn] = PATTERN[pat];
              tex = pat === 15 ? p.tex[2] : p.tex[1];
              const row = roll(4), t = turn < 0 ? roll(4) * 90 : turn;
              uv = tileUV(kind - 1, 3 - row, t);
            }
            if (!tex || (skip && skip(tex))) continue;
            const m = get(tex);
            m.room(4, 6);
            // Corners as the game builds them: (i, j), (i+1, j), (i+1, j+1), (i, j+1), with UV
            // corners 3, 2, 1, 0; triangles (0, 3, 1) and (3, 2, 1).
            const q0 = m.vert(p, i * 5 + j, ox + i * CELL, oy + j * CELL, uv[3][0], uv[3][1]);
            const q1 = m.vert(p, (i + 1) * 5 + j, ox + (i + 1) * CELL, oy + j * CELL, uv[2][0], uv[2][1]);
            const q2 = m.vert(p, (i + 1) * 5 + j + 1, ox + (i + 1) * CELL, oy + (j + 1) * CELL, uv[1][0], uv[1][1]);
            const q3 = m.vert(p, i * 5 + j + 1, ox + i * CELL, oy + (j + 1) * CELL, uv[0][0], uv[0][1]);
            m.tri(q0, q3, q1); m.tri(q3, q2, q1);
          }
        }
      }
    }
    const out = new Map();
    for (const [t, m] of bufs) out.set(t, m.done());
    return out;
  }

  // db/tileindex.data (a DDBINFILE2 table): tile id -> texture name (material/terrain/<name>/<name>.dds).
  function readTileIndex(d) {
    const out = new Map();
    if (!d || new TextDecoder('latin1').decode(d.subarray(0, 10)) !== 'DDBINFILE2') return out;
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    const schemaLen = dv.getUint32(16, true), poolSize = dv.getUint32(20, true), recSize = dv.getUint32(24, true);
    const schema = new TextDecoder('latin1').decode(d.subarray(40, 16 + schemaLen)).replace(/\0/g, '');
    const parts = schema.split('|');
    const [sname, ssize] = parts[0].split('%');
    const field = (name) => { const m = new RegExp(sname + '\\.' + name + '%(\\d+)').exec(schema); return m ? +m[1] : -1; };
    const fid = field('TileID'), fname = field('TileName'), size = +ssize;
    const pool = 16 + schemaLen, recs = pool + poolSize;
    if (fid < 0 || fname < 0 || !size) return out;
    for (let at = recs; at + size <= recs + recSize; at += size) {
      const id = dv.getInt32(at + fid, true), so = dv.getUint32(at + fname, true);
      let e = pool + so; while (e < d.length && d[e]) e++;
      out.set(id, new TextDecoder('latin1').decode(d.subarray(pool + so, e)).toLowerCase());
    }
    return out;
  }

  // A region id's folder: the .rgn whose header names it (u32 version, u32 size, u32 region id at
  // byte 8), else null. A folder name is passed through (an id may come as a number or as digits).
  // A few ids are in two folders (106: arenatest001 and iria_sw_beginner_dg_before; 60208:
  // tin_bamboofr_oriental and village_a): the one not named a test, then the one with more area
  // data, is the region.
  async function regionFolder(packs, region) {
    if (typeof region === 'string' && /^\d+$/.test(region.trim())) region = +region;
    if (typeof region !== 'number') return String(region);
    const map = await packs.once('rgnids', async () => {
      const out = new Map(), weight = (f) => {
        let n = /test/i.test(f) ? -1e12 : 0;
        for (const [k, e] of packs.entries) if (k.startsWith('world/' + f + '/') && k.endsWith('.area') && !k.slice(7 + f.length).includes('/')) n += e.size || 0;
        return n;
      };
      for (const n of packs.entries.keys()) {
        const m = /^world\/([^/]+)\/\1\.rgn$/.exec(n);
        if (!m) continue;
        const d = await packs.read(n);
        if (!d || d.length < 12) continue;
        const id = new DataView(d.buffer, d.byteOffset, d.byteLength).getUint32(8, true), old = out.get(id);
        if (!old || weight(m[1]) > weight(old)) out.set(id, m[1]);
      }
      return out;
    });
    return map.get(region) || null;
  }

  // The region's areas: data/world/<region>/*.area (the variant folders under it are other setups).
  function areaFiles(packs, region) {
    const dir = 'world/' + region.toLowerCase() + '/';
    return [...packs.entries.keys()].filter((n) => n.startsWith(dir) && n.endsWith('.area') && !n.slice(dir.length).includes('/')).sort();
  }

  // db/propdb.xml's classes, a prop's skeletons (its .set) and its model in a state: viewer3d.js's
  // (the prop pages' viewer reads them the same way).
  const propClasses = (packs) => V().core.propClasses(packs);
  const setSkeletons = (d) => V().core.setSkeletons(d);
  const propModel = (packs, cls, out, state) => V().core.propModel(packs, cls, out, state);

  // A region folder's id: the u32 at byte 8 of its .rgn, else null.
  async function regionIdOf(packs, folder) {
    const f = String(folder).toLowerCase(), d = await packs.read('world/' + f + '/' + f + '.rgn');
    return d && d.length >= 12 ? new DataView(d.buffer, d.byteOffset, d.byteLength).getUint32(8, true) : null;
  }

  // Where players arrive. An arrival is an area event with a name and a box (type 13 nearly always,
  // type 1 for a few: Tir Chonaill's moongate and tutorial exits) that something sends players to:
  //   db/portal.xml          each portal's event_zone "Region/Area/Event": where a moongate or a
  //                          teleport lands (a town's square: Uladh_main/town_TirChonaill/
  //                          TirChonaill_Spawn_A, a dungeon lobby's Outer_Spawn).
  //   db/regionlinkinfo.xml  every door between regions: <portal target_regionid exitid exit_x
  //                          exit_z>; exitid is the arrival event's 64-bit id in the target region
  //                          and exit_x, exit_z its position (x, y on the ground).
  // { zones: Set of "region/area/event" (lower case), links: target region id -> [{ id, x, y }] }.
  async function arrivalInfo(packs) {
    return packs.once('arrivals', async () => {
      const text = async (n) => (root.GAME_DATA && root.GAME_DATA.files && root.GAME_DATA.files[n]) || await packs.text(n) || '';
      const zones = new Set(), links = new Map();
      for (const m of (await text('db/portal.xml')).matchAll(/<event_zone\b[^>]*\bname="([^"]+)"/g)) zones.add(m[1].toLowerCase());
      for (const m of (await text('db/regionlinkinfo.xml')).matchAll(/<portal\b([^>]*)>/g)) {
        const a = {};
        for (const k of m[1].matchAll(/(\w+)="([^"]*)"/g)) a[k[1]] = k[2];
        const t = +a.target_regionid;
        if (!t) continue;
        let id = '';
        try { if (/^\d+$/.test(a.exitid || '') && a.exitid !== '0') id = BigInt(a.exitid).toString(16).padStart(16, '0'); } catch (e) { id = ''; }
        if (!links.has(t)) links.set(t, []);
        links.get(t).push({ id, x: +a.exit_x, y: +a.exit_z });
      }
      return { zones, links };
    });
  }

  // A prop's placement in the game's space (x, height, y): turned by its direction + 90 degrees about
  // the up axis (local x, z on the ground), scaled, then moved to its position. Found by fitting the
  // props' stored bounding boxes and the terrain blocks to the planes that use them.
  function propMatrix(p) {
    const a = p.dir + Math.PI / 2, c = Math.cos(a), s = Math.sin(a), k = p.scale || 1;
    // Row-major 4 x 4: world = M * [lx, ly, lz, 1]; world x = c*lx - s*lz, y (height) = ly, z = s*lx + c*lz.
    return [c * k, 0, -s * k, p.pos[0], 0, k, 0, p.pos[2], s * k, 0, c * k, p.pos[1], 0, 0, 0, 1];
  }

  // ======================================================================================
  // The view: full screen, free flight
  // ======================================================================================

  const CSS = `
.mwr{position:fixed;inset:0;z-index:2147483000;background:#9cc3e6;color:#fff;font:13px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif;user-select:none}
.mwr canvas{position:absolute;inset:0;width:100%;height:100%;display:block;cursor:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='24' height='24'%3E%3Cpath d='M12 2v7M12 15v7M2 12h7M15 12h7' stroke='black' stroke-width='4' stroke-linecap='round'/%3E%3Cpath d='M12 2v7M12 15v7M2 12h7M15 12h7' stroke='%23ff2a2a' stroke-width='2' stroke-linecap='round'/%3E%3Ccircle cx='12' cy='12' r='1.8' fill='%23ff2a2a' stroke='black'/%3E%3C/svg%3E") 12 12,crosshair}
.mwr-hud{position:absolute;left:14px;top:12px;max-width:min(420px,calc(100% - 28px));padding:8px 12px;border-radius:10px;background:rgba(10,14,20,.55);pointer-events:none}
.mwr-hud b{font-size:15px}
.mwr-hud .k{display:inline-block;min-width:16px;padding:0 5px;margin:0 1px;border-radius:4px;background:rgba(255,255,255,.18);text-align:center;font-size:12px}
.mwr-pos{position:absolute;left:14px;bottom:12px;padding:5px 10px;border-radius:8px;background:rgba(10,14,20,.55);font-size:12px;pointer-events:none;font-variant-numeric:tabular-nums}
.mwr-top{position:absolute;z-index:2;right:14px;top:12px;display:flex;flex-wrap:wrap;justify-content:flex-end;gap:8px;align-items:center}
.mwr-close,.mwr-top select{font:inherit;color:#fff;background:rgba(10,14,20,.6);border:1px solid rgba(255,255,255,.3);border-radius:8px;padding:6px 12px;cursor:pointer}
.mwr-top label{display:inline-flex;gap:6px;align-items:center;padding:0 2px 0 10px;border-radius:8px;background:rgba(10,14,20,.45)}
.mwr-top select{padding:5px 8px}
.mwr-top option{color:#000}
.mwr-close:hover,.mwr-top select:hover{background:rgba(10,14,20,.8)}
.mwr-close:focus-visible,.mwr-top select:focus-visible{outline:2px solid #fff;outline-offset:2px}
.mwr-msg{position:absolute;z-index:1;inset:0;display:flex;flex-direction:column;gap:12px;align-items:center;justify-content:center;text-align:center;padding:24px;background:rgba(10,14,20,.72);font-size:15px}
.mwr-msg p{margin:0;max-width:460px}
.mwr-msg small{opacity:.75}
.mwr-msg button{font:inherit;cursor:pointer;padding:7px 16px;border-radius:8px;border:0;background:#4f6bed;color:#fff}
.mwr-bar{width:min(320px,70vw);height:6px;border-radius:3px;background:rgba(255,255,255,.2);overflow:hidden}
.mwr-bar i{display:block;height:100%;width:0;background:#fff;transition:width .2s}
.mwr-cross{position:absolute;left:50%;top:50%;width:6px;height:6px;margin:-3px 0 0 -3px;border-radius:50%;background:#ff2a2a;box-shadow:0 0 0 1px #000;pointer-events:none;display:none}
.mwr.locked .mwr-cross{display:block}
html.mwr-lock,html.mwr-lock body{overflow:hidden!important}
`;

  const el = (tag, attrs, ...kids) => {
    const e = document.createElement(tag);
    Object.entries(attrs || {}).forEach(([k, v]) => (k === 'text' ? (e.textContent = v) : k === 'html' ? (e.innerHTML = v) : k.startsWith('on') ? (e[k] = v) : e.setAttribute(k, v)));
    kids.forEach((k) => e.append(k));
    return e;
  };

  function open(region, options) {
    options = options || {};
    if (!document.getElementById('mwr-style')) document.head.append(el('style', { id: 'mwr-style', text: CSS }));
    const box = el('div', { class: 'mwr', role: 'dialog', 'aria-label': (options.title || region) + ' in 3D' });
    const hud = el('div', { class: 'mwr-hud' });
    const pos = el('div', { class: 'mwr-pos' });
    const msg = el('div', { class: 'mwr-msg' });
    const close = el('button', { type: 'button', class: 'mwr-close', text: 'Close (Esc)' });
    // Top right: the frame rate picker (once the 3D code is loaded, in start) and Close.
    const top = el('div', { class: 'mwr-top' }, close);
    let fps = null;
    box.append(hud, pos, el('div', { class: 'mwr-cross' }), top, msg);
    document.body.append(box);
    document.documentElement.classList.add('mwr-lock');
    hud.innerHTML = `<b>${esc(options.title || region)}</b><br>Drag to look around, click a spot to go there. ` +
      '<span class="k">W</span><span class="k">A</span><span class="k">S</span><span class="k">D</span> fly, ' +
      '<span class="k">Space</span> up, <span class="k">Ctrl</span> down, <span class="k">Shift</span> faster, wheel changes speed, ' +
      '<span class="k">L</span> mouse look.';
    let view = null, closed = false;
    const say = (text, ...kids) => {
      msg.style.display = '';
      msg.innerHTML = '';
      if (text) msg.append(el('p', { text }));
      kids.forEach((k) => msg.append(k));
    };
    const done = (how) => {
      if (closed) return;
      closed = true;
      if (view) view.dispose();
      view = null;
      if (fps) fps.release();
      box.remove();
      document.documentElement.classList.remove('mwr-lock');
      document.removeEventListener('keydown', onKey, true);
      root.removeEventListener('popstate', onPop);
      // Closed some other way than the browser's Back: drop the history entry the view added.
      if (how !== 'back' && history.state && history.state.mwr) history.back();
      if (options.onClose) options.onClose();
    };
    // The browser's Back closes the view: it adds a history entry for the page it's on (same address,
    // so the page underneath doesn't change), and leaving that entry closes it.
    const onPop = () => done('back');
    try { history.pushState({ mwr: 1 }, '', location.href); root.addEventListener('popstate', onPop); } catch (e) { /* no history (sandboxed) */ }
    // Esc leaves the mouse look first (the browser does that), then closes.
    const onKey = (e) => { if (e.key === 'Escape' && !document.pointerLockElement) { e.preventDefault(); done(); } };
    document.addEventListener('keydown', onKey, true);
    close.onclick = done;
    close.focus();

    const explain = () => el('small', { text: 'The 3D view reads the region straight from your game\'s files on this computer. Nothing is uploaded or changed.' });
    async function begin() {
      if (!root.DecompressionStream || !V()) { say('Your browser can\'t show the 3D view. Use a current Chrome, Edge, Firefox or Safari.'); return; }
      say('Loading...');
      const A = V().access;
      let state;
      try { state = await A.state(); } catch (e) { state = 'choose'; }
      if (closed) return;
      if (state === 'ready') return start();
      if (state === 'allow') return say('Your browser needs your OK to read the game\'s files again.', el('button', { text: 'Allow', onclick: () => act(A.allow) }), explain());
      chooser();
    }
    // The same folder access as the 3D models (viewer3d.js): a kept folder where the browser has
    // showDirectoryPicker, else the package folder picked with <input webkitdirectory>.
    function chooser(problem) {
      const A = V().access, pre = problem ? problem + ' ' : '';
      if (A.canChoose ? A.canChoose() : root.showDirectoryPicker) {
        say(pre + 'To walk around regions, choose your Mabinogi folder (the one with Client.exe and the package folder) once. Your browser remembers it.',
          el('button', { text: problem ? 'Choose again' : 'Choose your Mabinogi folder', onclick: () => act(A.choose) }), explain());
        return;
      }
      const input = el('input', { type: 'file', webkitdirectory: '', multiple: '' });
      input.onchange = () => act(() => A.useFiles(input.files).then(() => true, () => false));
      say(pre + 'To walk around regions, choose the package folder inside your Mabinogi folder. Your browser may say it\'s uploading the files; they stay on this computer.',
        input, explain());
    }
    async function act(fn) {
      let ok = false;
      try { ok = await fn(); } catch (e) { if (e && e.name === 'AbortError') return; }
      if (closed) return;
      if (ok) start(); else chooser('That folder has no package folder with .pack files.');
    }
    async function start() {
      const bar = el('div', { class: 'mwr-bar' }, el('i'));
      say('Loading the region...', bar);
      try {
        const T = await V().core.ensureThree();
        const packs = await V().access.packs();
        if (closed) return;
        if (!fps) { fps = V().frameRate.picker(); top.prepend(fps.label); }
        view = new RegionView(T, box, packs, region, options, {
          progress: (f, text) => { bar.firstChild.style.width = Math.round(f * 100) + '%'; if (text) msg.querySelector('p').textContent = text; },
          ready: () => { msg.style.display = 'none'; },
          pos: (text) => { pos.textContent = text; },
          fail: (text) => say(text),
        });
        await view.load();
      } catch (e) {
        if (closed) return;
        if (root.console) console.error('[MabiWikiRegion]', e);
        say('This region couldn\'t be loaded: ' + (e && e.message || e));
      }
    }
    begin();
    const api = { close: done, get view() { return view; } };
    return api;
  }
  // terrain_white / terrain_black / terrain_whiteback: filler ground, not drawn (see load).
  const FILLER = /^terrain_(white|black)/;
  // Standing eye height after a click (a human is about 170 game units tall).
  const EYE = 260;
  const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

  // Vertex colors as the game's: twice the color (0x80 neutral), alpha left to the texture.
  function twiceColor(mat) {
    mat.onBeforeCompile = (s) => {
      s.fragmentShader = s.fragmentShader.replace('#include <color_fragment>',
        '#if defined( USE_COLOR_ALPHA )\n diffuseColor.rgb *= min( vColor.rgb * 2.0, vec3( 2.0 ) );\n#endif');
    };
    return mat;
  }
  // A prop part's material and the prop color a part takes: viewer3d.js's propMaterial and partColor.
  const partColor = (colormap, colors) => V().core.partColor(colormap, colors);

  // A vertex attribute the CPU doesn't need once it is on the GPU (normals, UVs, colors).
  function dropAfterUpload(a) {
    return a.onUpload(function () { this.array = null; });
  }
  // A texture's pixels, likewise (the GPU makes the mipmaps).
  function dropPixels(t) {
    t.onUpdate = () => { if (t.image) t.image = { data: null, width: t.image.width, height: t.image.height }; };
    return t;
  }

  class RegionView {
    constructor(T, box, packs, region, options, ui) {
      this.T = T; this.box = box; this.packs = packs; this.region = region; this.opts = options; this.ui = ui;
      // A logarithmic depth buffer: the far plane reaches across the whole region (no fog), and the
      // props standing on the ground still don't flicker through it.
      const r = this.renderer = new T.WebGLRenderer({ antialias: true, logarithmicDepthBuffer: true, preserveDrawingBuffer: !!options.test });
      r.outputColorSpace = 'srgb-linear';
      r.setPixelRatio(Math.min(2, root.devicePixelRatio || 1));
      box.insertBefore(r.domElement, box.firstChild);
      const sky = new T.Color().setRGB(0.61, 0.76, 0.9);
      this.scene = new T.Scene();
      this.scene.background = sky;
      this.camera = new T.PerspectiveCamera(60, 1, 10, 60000);
      this.scene.add(new T.HemisphereLight(0xffffff, 0x7a6a55, 1.6));
      const sun = new T.DirectionalLight(0xfff4e0, 1.3); sun.position.set(-0.5, 1, -0.56); this.scene.add(sun);
      // The game's space is left-handed (x, height, y): mirrored into three.js's right-handed one.
      this.world = new T.Group();
      this.world.scale.set(1, 1, -1);
      this.scene.add(this.world);
      // The stand-ins clicks are tested against (pickable): the same mirror, never drawn.
      this.pickRoot = new T.Group();
      this.pickRoot.scale.set(1, 1, -1);
      this.pickRoot.updateMatrixWorld(true);
      this.pickMat = new T.MeshBasicMaterial({ side: T.DoubleSide });
      this.textures = new Map();
      this.keys = new Set();
      this.yaw = 0; this.pitch = -0.25; this.speed = 1600;
      this.disposed = false;
      this.bindInput();
      this.resizeObs = new ResizeObserver(() => this.resize());
      this.resizeObs.observe(box);
      this.resize();
      // At the reader's frame rate (viewer3d.js frameRate: 60 a second unless they chose otherwise).
      this.frames = V().frameRate.loop((dt) => {
        if (this.disposed) return;
        this.step(dt);
        this.renderer.render(this.scene, this.camera);
      });
    }

    resize() {
      const w = this.box.clientWidth || 1, h = this.box.clientHeight || 1;
      this.renderer.setSize(w, h, false);
      this.camera.aspect = w / h;
      this.camera.updateProjectionMatrix();
    }

    // Like Street View: drag to look around, click a spot to fly there (a ring under the pointer shows
    // where); L captures the mouse for free looking (Esc lets it go); the keys fly anywhere.
    bindInput() {
      const T = this.T, c = this.renderer.domElement;
      c.tabIndex = 0;
      this.ray = new T.Raycaster();
      this.ring = new T.Mesh(new T.RingGeometry(55, 80, 40), new T.MeshBasicMaterial({ color: 0xff2a2a, transparent: true, opacity: 0.85, depthTest: false, side: T.DoubleSide }));
      this.ring.rotation.x = -Math.PI / 2;
      this.ring.renderOrder = 10;
      this.ring.visible = false;
      this.scene.add(this.ring);
      this.onLock = () => this.box.classList.toggle('locked', document.pointerLockElement === c);
      document.addEventListener('pointerlockchange', this.onLock);
      this.onMove = (e) => {
        if (document.pointerLockElement !== c) return;
        this.yaw -= e.movementX * 0.0025;
        this.pitch = Math.max(-1.55, Math.min(1.55, this.pitch - e.movementY * 0.0025));
      };
      document.addEventListener('mousemove', this.onMove);
      // Dragging looks around; a press that hardly moves is a click: fly to where it points.
      let drag = null, moved = 0;
      c.addEventListener('pointerdown', (e) => { if (!document.pointerLockElement && e.button === 0) { drag = [e.clientX, e.clientY]; moved = 0; c.setPointerCapture(e.pointerId); } });
      c.addEventListener('pointerup', (e) => {
        if (drag && moved < 5 && !document.pointerLockElement) { const hit = this.pick(e); if (hit) this.flyTo(hit); }
        drag = null;
      });
      this.onUp = () => { drag = null; };
      document.addEventListener('pointerup', this.onUp);
      c.addEventListener('pointermove', (e) => {
        if (document.pointerLockElement) return;
        if (drag) {
          moved += Math.abs(e.clientX - drag[0]) + Math.abs(e.clientY - drag[1]);
          this.moved = true;
          this.yaw -= (e.clientX - drag[0]) * 0.004; this.pitch = Math.max(-1.55, Math.min(1.55, this.pitch - (e.clientY - drag[1]) * 0.004));
          drag = [e.clientX, e.clientY];
          this.ring.visible = false;
          return;
        }
        this.hover = e;
      });
      c.addEventListener('pointerleave', () => { this.hover = null; this.ring.visible = false; });
      this.onKeyDown = (e) => {
        if (e.target && /input|select|textarea/i.test(e.target.tagName)) return;
        const k = e.code;
        if (k === 'KeyL' && c.requestPointerLock) { if (document.pointerLockElement) document.exitPointerLock(); else c.requestPointerLock(); e.preventDefault(); return; }
        if (/^(Key[WASDQE]|Space|ShiftLeft|ShiftRight|ControlLeft|ControlRight|Arrow\w+)$/.test(k)) { this.keys.add(k); e.preventDefault(); }
      };
      this.onKeyUp = (e) => this.keys.delete(e.code);
      this.onBlur = () => this.keys.clear();
      document.addEventListener('keydown', this.onKeyDown);
      document.addEventListener('keyup', this.onKeyUp);
      root.addEventListener('blur', this.onBlur);
      c.addEventListener('wheel', (e) => { e.preventDefault(); this.speed = Math.max(100, Math.min(20000, this.speed * (e.deltaY < 0 ? 1.2 : 1 / 1.2))); }, { passive: false });
    }

    // What the pointer is over: the nearest ground or prop surface, as a three.js point, or null.
    pick(e) {
      const rect = this.renderer.domElement.getBoundingClientRect();
      const ndc = new this.T.Vector2(((e.clientX - rect.left) / rect.width) * 2 - 1, -((e.clientY - rect.top) / rect.height) * 2 + 1);
      this.ray.setFromCamera(ndc, this.camera);
      const hits = this.ray.intersectObjects(this.pickRoot.children, false);
      return hits.length ? hits[0].point : null;
    }

    // Flies to a clicked point: there, at standing eye height, still looking the same way.
    flyTo(p) {
      const from = this.camera.position.clone();
      const to = new this.T.Vector3(p.x, p.y + EYE, p.z);
      const dist = from.distanceTo(to);
      this.flight = { from, to, t: 0, dur: Math.min(1.6, 0.35 + dist / 6000) };
      this.moved = true;
      // Looking down steeply from the sky, level out a little on the way.
      this.flight.pitch0 = this.pitch;
      this.flight.pitch1 = Math.max(-0.35, Math.min(0.2, this.pitch));
    }

    // Free flight: forward follows where the camera looks (pitch included).
    step(dt) {
      const k = this.keys, T = this.T;
      if (this.flight) {
        const f = this.flight;
        f.t = Math.min(1, f.t + dt / f.dur);
        const e = f.t < 0.5 ? 2 * f.t * f.t : 1 - Math.pow(-2 * f.t + 2, 2) / 2;  // ease in and out
        this.camera.position.lerpVectors(f.from, f.to, e);
        // An arc: up a little in the middle of a long hop.
        this.camera.position.y += Math.sin(Math.PI * f.t) * Math.min(1500, f.from.distanceTo(f.to) * 0.15);
        this.pitch = f.pitch0 + (f.pitch1 - f.pitch0) * e;
        if (f.t >= 1) this.flight = null;
      }
      // The ring under the pointer (a few times a second is enough for the raycast).
      if (this.hover && !this.flight && (this.hoverTick = (this.hoverTick || 0) + 1) % 3 === 0) {
        const p = this.pick(this.hover);
        this.ring.visible = !!p;
        if (p) {
          this.ring.position.set(p.x, p.y + 6, p.z);
          const s = Math.max(0.6, Math.min(8, this.camera.position.distanceTo(p) / 2500));
          this.ring.scale.set(s, s, s);
        }
      }
      const fwd = (k.has('KeyW') || k.has('ArrowUp') ? 1 : 0) - (k.has('KeyS') || k.has('ArrowDown') ? 1 : 0);
      const side = (k.has('KeyD') || k.has('ArrowRight') ? 1 : 0) - (k.has('KeyA') || k.has('ArrowLeft') ? 1 : 0);
      const up = (k.has('Space') || k.has('KeyE') ? 1 : 0) - (k.has('ControlLeft') || k.has('ControlRight') || k.has('KeyQ') ? 1 : 0);
      const cam = this.camera;
      cam.rotation.set(this.pitch, this.yaw, 0, 'YXZ');
      if (fwd || side || up) {
        this.flight = null;
        this.moved = true;
        const v = this.speed * (k.has('ShiftLeft') || k.has('ShiftRight') ? 4 : 1) * dt;
        const dir = new T.Vector3(); cam.getWorldDirection(dir);
        const right = new T.Vector3().crossVectors(dir, new T.Vector3(0, 1, 0)).normalize();
        cam.position.addScaledVector(dir, fwd * v).addScaledVector(right, side * v);
        cam.position.y += up * v;
      }
      if (this.ui.pos && this.areas) {
        const gx = cam.position.x, gy = -cam.position.z;
        const a = this.areas.find((q) => gx >= q.x0 && gx < q.x0 + q.px * PLANE && gy >= q.y0 && gy < q.y0 + q.py * PLANE);
        const t = `${Math.round(gx)}, ${Math.round(gy)}  \u00B7  height ${Math.round(cam.position.y)}${a ? '  \u00B7  ' + a.name : ''}  \u00B7  speed ${Math.round(this.speed)}`;
        if (t !== this.lastPos) { this.ui.pos(t); this.lastPos = t; }
      }
    }

    // Puts the camera at a game position (x, y on the ground; h height) looking along yaw/pitch (radians).
    setCamera(x, y, h, yaw, pitch) {
      this.camera.position.set(x, h, -y);
      if (yaw != null) this.yaw = yaw;
      if (pitch != null) this.pitch = pitch;
      this.step(0);
    }

    // A ground texture (material/terrain/<name>/<name>.dds): repeated, no mipmaps... the game's
    // terrain samples with mipmaps, so these get them (it keeps the far ground from shimmering).
    groundTexture(name) {
      if (!this.textures.has('g:' + name)) {
        this.textures.set('g:' + name, (async () => {
          const path = 'material/terrain/' + name + '/' + name + '.dds';
          const img = V().core.decodeDds(await this.packs.read(path));
          if (!img) return null;
          const T = this.T, t = new T.DataTexture(img.px, img.w, img.h);
          t.wrapS = t.wrapT = T.ClampToEdgeWrapping;
          t.magFilter = T.LinearFilter; t.minFilter = T.LinearMipmapLinearFilter; t.generateMipmaps = true;
          t.anisotropy = Math.min(8, this.renderer.capabilities.getMaxAnisotropy());
          t.flipY = false;
          t.needsUpdate = true;
          return dropPixels(t);
        })());
      }
      return this.textures.get('g:' + name);
    }

    propTexture(name, img) {
      if (!img) return null;
      if (!this.textures.has('p:' + name)) {
        const T = this.T, t = new T.DataTexture(img.px, img.w, img.h);
        t.wrapS = t.wrapT = T.RepeatWrapping;
        t.magFilter = T.LinearFilter; t.minFilter = T.LinearMipmapLinearFilter; t.generateMipmaps = true;
        t.flipY = false;
        t.needsUpdate = true;
        this.textures.set('p:' + name, dropPixels(t));
      }
      return this.textures.get('p:' + name);
    }

    async load() {
      const T = this.T, packs = this.packs;
      const folder = await regionFolder(packs, this.region);
      const names = folder ? areaFiles(packs, folder) : [];
      if (!names.length) { this.ui.fail('This region isn\'t in your game\'s files.'); return; }
      const tiles = readTileIndex(await packs.read('db/tileindex.data'));
      this.areas = [];
      for (const n of names) {
        let d = null;
        try { d = await packs.read(n); } catch (e) { d = null; }
        if (this.disposed) return;
        try { this.areas.push(readArea(d)); } catch (e) { if (root.console) console.warn('[MabiWikiRegion]', n, e); }
        this.ui.progress(this.areas.length / names.length * 0.15, 'Reading the region...');
      }
      if (!this.areas.length) { this.ui.fail('This region\'s ground couldn\'t be read.'); return; }
      this.tiles = tiles;
      // Everything is loaded at once, so the far plane covers the whole region from any corner.
      const span = (k, n) => [Math.min(...this.areas.map((a) => a[k])), Math.max(...this.areas.map((a) => a[k] + a[n] * PLANE))];
      const [x0, x1] = span('x0', 'px'), [y0, y1] = span('y0', 'py');
      this.camera.far = Math.max(60000, Math.hypot(x1 - x0, y1 - y0) * 1.5 + 20000);
      this.camera.updateProjectionMatrix();
      this.ray.far = this.camera.far;
      // Start where players arrive (startSpot), or at the given spot, standing, facing the most props.
      let start;
      if (this.opts.x != null && this.opts.y != null) start = { x: +this.opts.x, y: +this.opts.y, h: this.groundAt(+this.opts.x, +this.opts.y), why: 'given' };
      else {
        const info = await arrivalInfo(packs), id = await regionIdOf(packs, folder);
        if (this.disposed) return;
        start = this.startSpot(info, id, folder);
      }
      const sx = start.x, sy = start.y;
      this.start = start;
      this.setCamera(sx, sy, start.h + EYE, this.facing(sx, sy, (x0 + x1) / 2, (y0 + y1) / 2), -0.12);
      // The ground: a mesh per area and texture (a material per texture). Kept on the CPU only
      // what clicking needs (pickable: positions and triangles); the rest goes once uploaded.
      // terrain_white / terrain_black / terrain_whiteback: plain filler under ground nobody walks
      // (behind the border hills, beyond the region's edge, under a room's floor); left out like
      // the space past it.
      const skip = (t) => FILLER.test(tiles.get(t) || '');
      const mats = new Map();
      let i = 0, drawn = 0;
      for (const a of this.areas) {
        if (this.disposed) return;
        for (const [tile, m] of buildGround(a, skip)) {
          const name = tiles.get(tile);
          if (!mats.has(tile)) {
            const map = name ? await this.groundTexture(name) : null;
            if (this.disposed) return;
            const mat = new T.MeshLambertMaterial({ map, vertexColors: true, side: T.DoubleSide, color: map ? 0xffffff : 0x6b8f4e });
            twiceColor(mat);
            mats.set(tile, mat);
          }
          const g = new T.BufferGeometry();
          g.setAttribute('position', new T.BufferAttribute(m.pos, 3));
          g.setAttribute('normal', dropAfterUpload(new T.BufferAttribute(m.nrm, 4, true)));
          g.setAttribute('uv', dropAfterUpload(new T.BufferAttribute(m.uv, 2, true)));
          // The point colors: 0x80 is neutral (the game modulates by twice the color).
          g.setAttribute('color', dropAfterUpload(new T.BufferAttribute(m.col, 4, true)));
          g.setIndex(new T.BufferAttribute(m.verts > 65535 ? m.idx : Uint16Array.from(m.idx), 1));
          g.computeBoundingSphere();
          const o = new T.Mesh(g, mats.get(tile));
          o.matrixAutoUpdate = false;
          this.world.add(o);
          this.pickable(o);
          drawn++;
        }
        // The planes' tiling and colors are drawn now; their heights stay (groundAt).
        for (const col of a.planes) for (const p of col) { p.nx = p.nz = p.col = p.a = p.b = p.cellTex = null; }
        this.ui.progress(0.15 + (++i / this.areas.length) * 0.25, 'Building the ground...');
      }
      this.ui.ready();
      // Props, nearest the start first, a class at a time: one instanced mesh per part.
      const classes = await propClasses(packs);
      const out = { textures: {}, texinfo: {}, bases: V().core.byBase(packs), mats: await V().core.materials(packs) };
      if (this.disposed) return;
      const groups = new Map();
      for (const a of this.areas) for (const p of a.propList) {
        const cls = classes.get(p.cls);
        if (!cls || (cls.event && !this.opts.events)) continue;
        const key = cls.name + ':' + (p.state || 'single').toLowerCase();
        if (!groups.has(key)) groups.set(key, { cls, state: p.state, list: [], near: Infinity });
        const gr = groups.get(key);
        gr.list.push(p);
        gr.near = Math.min(gr.near, Math.hypot(p.pos[0] - sx, p.pos[1] - sy));
      }
      const order = [...groups.values()].sort((a, b) => a.near - b.near);
      let top = -Infinity;
      for (const a of this.areas) { top = Math.max(top, a.hi); for (const p of a.propList) top = Math.max(top, p.pos[2], p.box[4]); }
      this.top = Number.isFinite(top) ? top + 1000 : 20000;
      let n = 0, shown = 0;
      let settled = false;
      for (const gr of order) {
        if (this.disposed) return;
        // The props within 3000 of the start are in: stand and look (settle).
        if (!settled && gr.near > 3000) { this.settle(); settled = true; }
        let model = null;
        try { model = await propModel(packs, gr.cls, out, gr.state); } catch (e) { if (root.console) console.warn('[MabiWikiRegion]', gr.cls.name, e); }
        if (this.disposed) return;
        if (model) { this.addProps(model, gr.list, out); shown += gr.list.length; }
        // The model lives on in the scene (positions and triangles kept for clicking): the
        // converted copy isn't kept for another visit.
        packs.memo.delete('propmodel:' + gr.cls.name + ':' + (gr.state || 'single').toLowerCase());
        n++;
        if (this.ui.pos && n % 10 === 0) this.ui.pos(`Loading props: ${n} of ${order.length} kinds`);
      }
      // The decoded textures are on the GPU now: the shared cache lets go of them.
      for (const t of Object.keys(out.textures)) packs.memo.delete((t.startsWith('gloss:') ? '' : 'tex:') + t.toLowerCase());
      this.lastPos = '';
      this.stats = { areas: this.areas.length, kinds: order.length, props: shown };
      this.settle();
      // Houses and halls whose scenery is set up while the game runs keep nothing in their files.
      if (!shown && !drawn) this.ui.fail('This region has no ground or scenery in your game\'s files to show (it is filled in while the game runs).');
    }

    // Approximate ground height at a game position (the nearest plane point), for the start.
    // shown: only ground that is drawn (not filler, not a hole), else null.
    groundAt(x, y, shown) {
      for (const a of this.areas || []) {
        const px = Math.floor((x - a.x0) / PLANE), py = Math.floor((y - a.y0) / PLANE);
        if (px < 0 || py < 0 || px >= a.px || py >= a.py) continue;
        const p = a.planes[px][py], i = Math.min(4, Math.round((x - a.x0 - px * PLANE) / CELL)), j = Math.min(4, Math.round((y - a.y0 - py * PLANE) / CELL));
        if (shown && !this.planeShown(p)) return null;
        return p.h[i * 5 + j];
      }
      return shown ? null : 0;
    }

    // A plane draws something: a terrain block stands on it, or one of its textures isn't filler.
    planeShown(p) {
      return !!p.block || p.tex.some((t) => t && !FILLER.test((this.tiles && this.tiles.get(t)) || ''));
    }

    // Where the walk starts: the region's arrival (arrivalInfo), best first:
    //   1. an event a portal lands on (db/portal.xml: a town's square, a dungeon lobby's entrance);
    //   2. the event most doors from other regions lead to (db/regionlinkinfo.xml exitid);
    //   3. a named spot (type 13 or 1) called a spawn, a square, a start or an arrival, then any;
    //   ties go to the spot with the most props around it (the built-up part, not a corner field);
    //   4. a door's landing position with no event behind it;
    //   5. the busiest 2000 x 2000 square of props;
    //   6. the drawn ground nearest the middle of the largest area.
    // { x, y, h (the ground or floor there), why }.
    startSpot(info, regionId, folder) {
      const props = [];
      for (const a of this.areas) for (const p of a.propList) props.push(p.pos);
      const around = (x, y) => { let n = 0; for (const q of props) if (Math.abs(q[0] - x) < 3000 && Math.abs(q[1] - y) < 3000) n++; return n; };
      const links = (regionId != null && info.links.get(regionId)) || [];
      const linkCount = new Map();
      for (const l of links) if (l.id) linkCount.set(l.id, (linkCount.get(l.id) || 0) + 1);
      const f = String(folder).toLowerCase();
      let best = null;
      for (const a of this.areas) {
        for (const e of a.eventList) {
          const named = e.type === 13 || e.type === 1, zone = info.zones.has(f + '/' + a.name.toLowerCase() + '/' + e.name.toLowerCase()), n = linkCount.get(e.id) || 0;
          if (!named && !zone && !n) continue;
          const score = (zone ? 1000 : 0) + Math.min(n, 10) * 20 + (/spawn|square|start|arriv/i.test(e.name) ? 5 : /^_?from|inportal/i.test(e.name) ? 2 : 0) + (named ? 1 : 0);
          if (best && score < best.score) continue;
          const busy = around(e.pos[0], e.pos[1]);
          if (best && score === best.score && busy <= best.busy) continue;
          best = { score, busy, x: e.pos[0], y: e.pos[1], h: e.pos[2], why: 'event ' + a.name + '/' + e.name };
        }
      }
      if (best) {
        // The event's height is where players stand; a floor drawn above it (rare) wins.
        const g = this.groundAt(best.x, best.y, true);
        if (g != null && g > best.h) best.h = g;
        return best;
      }
      const inside = (x, y) => this.areas.some((a) => x >= a.x0 && x < a.x0 + a.px * PLANE && y >= a.y0 && y < a.y0 + a.py * PLANE);
      const door = links.find((l) => inside(l.x, l.y));
      if (door) return { x: door.x, y: door.y, h: this.groundAt(door.x, door.y), why: 'door' };
      if (props.length) {
        const cells = new Map();
        for (const q of props) {
          const k = Math.floor(q[0] / 2000) + ',' + Math.floor(q[1] / 2000);
          const c = cells.get(k) || { n: 0, x: 0, y: 0, h: 0 };
          c.n++; c.x += q[0]; c.y += q[1]; c.h += q[2];
          cells.set(k, c);
        }
        const c = [...cells.values()].sort((a, b) => b.n - a.n)[0], x = c.x / c.n, y = c.y / c.n;
        const g = this.groundAt(x, y, true);
        return { x, y, h: g != null ? g : c.h / c.n, why: 'props' };
      }
      const big = this.areas.slice().sort((a, b) => b.px * b.py - a.px * a.py)[0];
      const mx = big.x0 + big.px * PLANE / 2, my = big.y0 + big.py * PLANE / 2;
      let spot = null;
      for (const a of this.areas) {
        for (let x = 0; x < a.px; x++) for (let y = 0; y < a.py; y++) {
          if (!this.planeShown(a.planes[x][y])) continue;
          const cx = a.x0 + (x + 0.5) * PLANE, cy = a.y0 + (y + 0.5) * PLANE, d = Math.hypot(cx - mx, cy - my);
          if (!spot || d < spot.d) spot = { d, x: cx, y: cy };
        }
      }
      if (!spot) spot = { x: mx, y: my };
      return { x: spot.x, y: spot.y, h: this.groundAt(spot.x, spot.y), why: 'middle' };
    }

    // Once the props near the start are in (and again when all are), stand on what is really there
    // and look somewhere open, unless the walker has moved already:
    //   height: straight down from above the region, the upward face nearest the start's height (a
    //     room's floor over filler ground, a sky island over nothing, the ground; not a roof, a
    //     ceiling's top or an island's underside);
    //   facing: the 16 directions scored by the props that way (facing), each kept only as much as
    //     the view is open there (looking straight into a wall or a pillar counts for nothing); in a
    //     room (all the props within 6000), toward the middle of its props (by footprint, so the
    //     room's shell leads): a room's arrival is at
    //     its door, and its walls stand on two sides only (the game looks in from the open ones).
    settle() {
      if (this.moved || !this.start || this.disposed) return;
      const T = this.T, s = this.start, ray = new T.Raycaster();
      const objs = this.pickRoot.children;
      const m4 = new T.Matrix4(), mi = new T.Matrix4(), a = new T.Vector3(), b = new T.Vector3(), c = new T.Vector3();
      // A hit's face normal in the drawn space, from its winding there (the game's front faces turn
      // clockwise there: checked on floors and roofs, whose tops and undersides come in pairs);
      // props only: the ground's planes count from either side.
      const facesUp = (hit) => {
        const o = hit.object;
        if (!o.isInstancedMesh || !hit.face) return true;
        o.getMatrixAt(hit.instanceId, mi);
        m4.multiplyMatrices(o.matrixWorld, mi);
        const pa = o.geometry.attributes.position;
        a.fromBufferAttribute(pa, hit.face.a).applyMatrix4(m4); b.fromBufferAttribute(pa, hit.face.b).applyMatrix4(m4); c.fromBufferAttribute(pa, hit.face.c).applyMatrix4(m4);
        b.sub(a); c.sub(a); b.cross(c).normalize();
        return b.y < -0.3;
      };
      ray.set(new T.Vector3(s.x, this.top, -s.y), new T.Vector3(0, -1, 0));
      ray.far = Infinity;
      let h = null, flat = null;
      for (const hit of ray.intersectObjects(objs, false)) {
        const y = hit.point.y;
        if (flat == null || Math.abs(y - s.h) < Math.abs(flat - s.h)) flat = y;
        if (facesUp(hit) && (h == null || Math.abs(y - s.h) < Math.abs(h - s.h))) h = y;
      }
      if (h == null) h = flat;
      if (h == null) h = s.h;
      const eye = new T.Vector3(s.x, h + EYE, -s.y);
      const bins = this.bins(s.x, s.y), dir = new T.Vector3();
      let best = -1, bestScore = -1, open = -1, openYaw = this.yaw;
      ray.far = 8000;
      for (let i = 0; i < 16; i++) {
        const yaw = i * Math.PI / 8;
        dir.set(-Math.sin(yaw), 0, -Math.cos(yaw));
        ray.set(eye, dir);
        const hit = ray.intersectObjects(objs, false)[0];
        const d = hit ? hit.distance : 8000;
        if (d > open + 1) { open = d; openYaw = yaw; }
        // Nothing that way at all is a room's open side (the void past it): half as good as a view
        // across the room.
        const score = bins[i] * (!hit ? 0.5 : d < 300 ? 0 : d < 1000 ? 0.3 : 1);
        if (score > bestScore) { bestScore = score; best = yaw; }
      }
      let yaw = bestScore > 0 ? best : openYaw;
      let n = 0, mx = 0, my = 0, x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity;
      for (const a of this.areas) {
        for (const p of a.propList) {
          // Weighted by its footprint: the room's own shell (one big prop) leads.
          const b = p.box, w = Math.max(1, (b[3] - b[0]) * (b[5] - b[2]));
          n += w; mx += w * (b[0] + b[3]) / 2; my += w * (b[2] + b[5]) / 2;
          x0 = Math.min(x0, p.pos[0]); x1 = Math.max(x1, p.pos[0]); y0 = Math.min(y0, p.pos[1]); y1 = Math.max(y1, p.pos[1]);
        }
      }
      if (n && x1 - x0 < 6000 && y1 - y0 < 6000 && Math.hypot(mx / n - s.x, my / n - s.y) > 150) yaw = Math.atan2(-(mx / n - s.x), my / n - s.y);
      this.setCamera(s.x, s.y, h + EYE, yaw, this.pitch);
    }

    // The props around (x, y), 400 to 8000 away, by direction (16, yaw i * 22.5 degrees), each
    // counting its neighbours half.
    bins(x, y) {
      const raw = new Float64Array(16), out = new Float64Array(16);
      for (const a of this.areas) {
        for (const p of a.propList) {
          const dx = p.pos[0] - x, dy = p.pos[1] - y, d = Math.hypot(dx, dy);
          if (d < 400 || d > 8000) continue;
          raw[(Math.round(Math.atan2(-dx, dy) / (Math.PI / 8)) + 16) % 16]++;
        }
      }
      for (let i = 0; i < 16; i++) out[i] = raw[i] + 0.5 * (raw[(i + 1) % 16] + raw[(i + 15) % 16]);
      return out;
    }

    // The yaw that looks from (x, y) toward the most props 400 to 8000 away (16 directions, each
    // counting its neighbours half), else toward (cx, cy). Yaw 0 looks up the map (+y).
    facing(x, y, cx, cy) {
      const bins = this.bins(x, y);
      let bi = -1, bv = 0;
      for (let i = 0; i < 16; i++) if (bins[i] > bv) { bv = bins[i]; bi = i; }
      if (bi >= 0) return bi * Math.PI / 8;
      return Math.hypot(cx - x, cy - y) > 1 ? Math.atan2(-(cx - x), cy - y) : 0;
    }

    addProps(model, list, out) {
      const T = this.T;
      const mats = list.map((p) => new T.Matrix4().set(...propMatrix(p)));
      for (const m of model.meshes) {
        if (!m.tris) continue;
        const g = new T.BufferGeometry();
        g.setAttribute('position', new T.BufferAttribute(m.pos, 3));
        g.setAttribute('normal', dropAfterUpload(new T.BufferAttribute(m.nrm, 3)));
        g.setAttribute('uv', dropAfterUpload(new T.BufferAttribute(m.uv, 2)));
        g.setAttribute('color', dropAfterUpload(new T.BufferAttribute(m.col, 4, true)));
        g.setIndex(new T.BufferAttribute(m.idx, 1));
        const info = out.texinfo[m.texture], rs = info && info.rs;
        const map = this.propTexture(m.texture, out.textures[m.texture]);
        const gname = rs && rs.gloss ? 'gloss:' + rs.gloss : '';
        const { mat, unlit } = V().core.propMaterial(T, rs, map, gname ? this.propTexture(gname, out.textures[gname]) : null);
        const o = new T.InstancedMesh(g, mat, mats.length);
        o.name = m.texture || '';
        mats.forEach((M, i) => o.setMatrixAt(i, M));
        if (unlit) {
          const ic = new Float32Array(list.length * 3);
          list.forEach((p, i) => ic.set(partColor(m.colormap, p.colors), i * 3));
          o.instanceColor = new T.InstancedBufferAttribute(ic, 3);
        }
        o.instanceMatrix.needsUpdate = true;
        o.computeBoundingSphere();
        this.world.add(o);
        this.pickable(o);
      }
    }

    // Clicking hits a stand-in of each mesh that shares its positions, triangles and placements
    // but has no other attributes (the drawn meshes let go of theirs once uploaded).
    pickable(o) {
      const T = this.T, g = new T.BufferGeometry();
      g.setAttribute('position', o.geometry.attributes.position);
      g.setIndex(o.geometry.index);
      g.boundingSphere = o.geometry.boundingSphere;
      let q;
      if (o.isInstancedMesh) {
        q = new T.InstancedMesh(g, this.pickMat, o.count);
        q.instanceMatrix = o.instanceMatrix;
        q.boundingSphere = o.boundingSphere;
      } else q = new T.Mesh(g, this.pickMat);
      q.matrixAutoUpdate = false;
      this.pickRoot.add(q);
      q.updateMatrixWorld(true);
    }

    dispose() {
      this.disposed = true;
      this.frames.stop();
      if (document.pointerLockElement === this.renderer.domElement) document.exitPointerLock();
      document.removeEventListener('pointerlockchange', this.onLock);
      document.removeEventListener('mousemove', this.onMove);
      document.removeEventListener('pointerup', this.onUp);
      document.removeEventListener('keydown', this.onKeyDown);
      document.removeEventListener('keyup', this.onKeyUp);
      root.removeEventListener('blur', this.onBlur);
      this.resizeObs.disconnect();
      this.scene.traverse((o) => { if (o.geometry) o.geometry.dispose(); if (o.material) o.material.dispose(); });
      for (const t of this.textures.values()) Promise.resolve(t).then((x) => x && x.dispose && x.dispose());
      this.pickMat.dispose();
      this.renderer.dispose();
      this.renderer.domElement.remove();
      // Nothing of the region is kept (the page may hold on to the viewer object).
      this.world.clear(); this.pickRoot.clear(); this.scene.clear();
      this.textures.clear();
      this.areas = null; this.tiles = null; this.hover = null;
    }
  }

  root.MabiWikiRegion = Object.assign(root.MabiWikiRegion || {}, {
    open,
    core: { readArea, readPlane, buildGround, tileUV, readTileIndex, regionFolder, areaFiles, propClasses, propModel, setSkeletons, propMatrix, PLANE, CELL, PATTERN },
  });
})(typeof window !== 'undefined' ? window : globalThis);
