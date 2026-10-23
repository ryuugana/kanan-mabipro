/* Mabinogi Wiki: the game's packs (package\*.pack), read in the browser.
 *
 *   MabiPacks.open(files) -> Promise<packs>
 *
 * files are the .pack files as File objects (or File-likes: { name, size, slice(start, end) });
 * others are skipped. A pack is never loaded whole: the index and each entry are read by slice.
 * packs:
 *   has(name), entry(name)   name: lower case with '/' ('db/itemdb.xml'); the entry is
 *                            { name, seed, offset, packed, size, compressed, file, dataStart }
 *   names()                  every name, in byte order (the order Kanan's map keeps them in)
 *   read(name)               Promise<Uint8Array> of the file (un-XORed and inflated), or null
 *   raw(name)                Promise<{ data, compressed }>: un-XORed but not inflated
 *   text(name)               Promise<string>: read() as text (asText)
 *   files                    the packs read, in name order
 *   entries                  Map(name -> entry); once(key, make): a value made once per session
 *
 * Also here, for the 3D viewers (viewer3d.js, region3d.js) and the wiki's preparer (prepare.js):
 * readPackIndex, mtLowBytes, inflate, asText, decodeDds, decodeImage, crc32, zipWriter.
 *
 * A classic script (no modules from file://). In Node, require() gives the same object.
 */
(function (root) {
  'use strict';

  const enc = new TextDecoder('latin1');
  const cmp = (a, b) => (a < b ? -1 : a > b ? 1 : 0);

  // ======================================================================================
  // Packs
  // ======================================================================================
  // PACK v2: a 512-byte header ("PACK"), a 32-byte package info (entry count, index size, blank
  // size, data size), then the index. Each entry: a length-coded name, seed, flags, offset,
  // compressed size, size, compressed flag (u32 each) and five FILETIMEs. Entry data is XORed with
  // the low byte of each output of MT19937 (init_genrand((seed << 7) ^ 0xA9C36DE1)), then zlib.
  // When packs hold the same file the game keeps the copy with the highest seed; on a tie the first
  // pack in name order (compared without case).

  async function bytesOf(file, start, end) {
    return new Uint8Array(await file.slice(start, end).arrayBuffer());
  }

  // The low bytes of the first n outputs of MT19937 seeded with init_genrand(seed).
  function mtLowBytes(seed, n) {
    const mt = new Uint32Array(624);
    mt[0] = seed >>> 0;
    for (let i = 1; i < 624; i++) {
      const p = mt[i - 1] ^ (mt[i - 1] >>> 30);
      mt[i] = (Math.imul(1812433253, p) + i) >>> 0;
    }
    const out = new Uint8Array(n);
    let k = 624;
    for (let o = 0; o < n; o++) {
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
      out[o] = y & 255;
    }
    return out;
  }

  // zlib (DecompressionStream's 'deflate' takes the zlib header).
  async function inflate(data) {
    const stream = new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }

  // Names as the game's files have them, lower case (A-Z only, as Kanan's tolower) with '/'.
  const lowerSlashes = (s) => s.replace(/[A-Z\\]/g, (c) => (c === '\\' ? '/' : String.fromCharCode(c.charCodeAt(0) + 32)));

  // One pack's index: [{ name, seed, offset, packed, size, compressed }], and where its data starts.
  async function readPackIndex(file) {
    const head = await bytesOf(file, 0, 544);
    if (head.length < 544 || enc.decode(head.subarray(0, 4)) !== 'PACK') return null;
    const hv = new DataView(head.buffer);
    const count = hv.getUint32(512, true), indexSize = hv.getUint32(516, true), blank = hv.getUint32(520, true);
    const index = await bytesOf(file, 544, 544 + indexSize);
    const dv = new DataView(index.buffer);
    const dataStart = 544 + indexSize + blank;
    const entries = [];
    let pos = 0;
    for (let i = 0; i < count && pos < index.length; i++) {
      const length = index[pos++];
      let size;
      if (length < 4) size = 0x10 * (length + 1) - 1;
      else if (length === 4) size = 0x60 - 1;
      else { if (pos + 4 > index.length) break; size = dv.getUint32(pos, true); pos += 4; }
      if (pos + size + 64 > index.length) break;
      let end = pos;
      while (end < pos + size && index[end] !== 0) end++;
      const name = lowerSlashes(enc.decode(index.subarray(pos, end)));
      pos += size;
      entries.push({ name, seed: dv.getUint32(pos, true), offset: dv.getUint32(pos + 8, true), packed: dv.getUint32(pos + 12, true),
        size: dv.getUint32(pos + 16, true), compressed: dv.getUint32(pos + 20, true) });
      pos += 64;
    }
    return { entries, dataStart };
  }

  class Packs {
    static async open(files) {
      const packs = new this();
      const sorted = [...files].filter((f) => /\.pack$/i.test(f.name)).sort((a, b) => cmp(a.name.toLowerCase(), b.name.toLowerCase()));
      const indexes = await Promise.all(sorted.map(readPackIndex));
      sorted.forEach((file, i) => {
        const idx = indexes[i];
        if (!idx) return;
        packs.files.push(file);
        for (const e of idx.entries) {
          const old = packs.entries.get(e.name);
          if (!old || e.seed > old.seed) {
            e.file = file;
            e.dataStart = idx.dataStart;
            packs.entries.set(e.name, e);
          }
        }
      });
      return packs;
    }

    constructor() {
      this.entries = new Map(); // lower-case name with '/': entry
      this.files = [];
      this.memo = new Map();    // per-session caches
    }

    has(name) { return this.entries.has(name); }
    entry(name) { return this.entries.get(name) || null; }
    names() { return this.once('names', () => [...this.entries.keys()].sort(cmp)); }

    async read(name) {
      const r = await this.raw(name);
      return r && (r.compressed ? inflate(r.data) : r.data);
    }

    // The entry as stored, un-XORed but not inflated: { data, compressed }.
    async raw(name) {
      const e = this.entries.get(name);
      if (!e) return null;
      const start = e.dataStart + e.offset;
      const data = await bytesOf(e.file, start, start + e.packed);
      const key = mtLowBytes(((e.seed << 7) ^ 0xA9C36DE1) >>> 0, data.length);
      for (let i = 0; i < data.length; i++) data[i] ^= key[i];
      return { data, compressed: !!e.compressed };
    }

    async text(name) {
      const d = await this.read(name);
      return d && asText(d);
    }

    // A value computed once per session.
    once(key, make) {
      if (!this.memo.has(key)) this.memo.set(key, make());
      return this.memo.get(key);
    }
  }

  // A file as text, without a byte order mark: UTF-16LE when it starts with FF FE (an odd last
  // byte is dropped, as Kanan does), else UTF-8.
  function asText(d) {
    if (d[0] === 0xff && d[1] === 0xfe) return new TextDecoder('utf-16le').decode(d.subarray(2, d.length - (d.length & 1)));
    if (d[0] === 0xef && d[1] === 0xbb && d[2] === 0xbf) return new TextDecoder('utf-8').decode(d.subarray(3));
    return new TextDecoder('utf-8').decode(d);
  }

  // ======================================================================================
  // .dds textures
  // ======================================================================================
  // As Kanan decodes them (MabiWiki.cpp decodeDds, decodeImage), so the pictures made here are the
  // same to the pixel.

  const cstr = (u8, at, n) => {
    let end = at;
    const stop = Math.min(at + n, u8.length);
    while (end < stop && u8[end] !== 0) end++;
    return enc.decode(u8.subarray(at, end));
  };

  // A DDS's top mip level as { w, h, px: RGBA, fmt }, or null: DXT1/3/5 or uncompressed channel
  // masks (A1R5G5B5, A4R4G4B4, R5G6B5, A8R8G8B8, A8B8G8R8...). An n-bit channel becomes 8 bits
  // rounded (v * 255 + top / 2) / top, or its top 8 bits when it has more.
  function decodeDds(d) {
    if (!d || d.length < 128 || cstr(d, 0, 4) !== 'DDS ') return null;
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    const flags = dv.getUint32(8, true), h = dv.getUint32(12, true), w = dv.getUint32(16, true), pitch = dv.getUint32(20, true);
    const pf = dv.getUint32(80, true), four = enc.decode(d.subarray(84, 88)), bits = dv.getUint32(88, true);
    if (!w || !h || w > 4096 || h > 4096) return null;
    const px = new Uint8Array(w * h * 4);
    const size = d.length - 128;
    if (pf & 4) {
      const dxt1 = four === 'DXT1', dxt3 = four === 'DXT3', dxt5 = four === 'DXT5';
      if (!dxt1 && !dxt3 && !dxt5) return null;
      const bs = dxt1 ? 8 : 16, bw = (w + 3) >> 2, bh = (h + 3) >> 2;
      if (size < bw * bh * bs) return null;
      const pal = [[0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0], [0, 0, 0, 0]], alpha = new Uint8Array(16), apal = new Uint8Array(8);
      const c565 = (c, o) => { const r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31; o[0] = (r << 3) | (r >> 2); o[1] = (g << 2) | (g >> 4); o[2] = (b << 3) | (b >> 2); };
      for (let by = 0; by < bh; by++) for (let bx = 0; bx < bw; bx++) {
        const at = 128 + (by * bw + bx) * bs, cAt = dxt1 ? at : at + 8;
        const c0 = dv.getUint16(cAt, true), c1 = dv.getUint16(cAt + 2, true), bitsC = dv.getUint32(cAt + 4, true);
        c565(c0, pal[0]); c565(c1, pal[1]);
        const four4 = c0 > c1 || !dxt1;
        for (let i = 0; i < 3; i++) {
          if (four4) { pal[2][i] = ((2 * pal[0][i] + pal[1][i]) / 3) | 0; pal[3][i] = ((pal[0][i] + 2 * pal[1][i]) / 3) | 0; }
          else { pal[2][i] = ((pal[0][i] + pal[1][i]) / 2) | 0; pal[3][i] = 0; }
        }
        pal[0][3] = pal[1][3] = pal[2][3] = 255; pal[3][3] = four4 ? 255 : 0;
        if (dxt3) {
          for (let i = 0; i < 16; i++) alpha[i] = ((d[at + (i >> 1)] >> ((i & 1) * 4)) & 15) * 17;
        } else if (dxt5) {
          const a0 = d[at], a1 = d[at + 1];
          apal[0] = a0; apal[1] = a1;
          if (a0 > a1) for (let i = 0; i < 6; i++) apal[2 + i] = (((6 - i) * a0 + (1 + i) * a1) / 7) | 0;
          else { for (let i = 0; i < 4; i++) apal[2 + i] = (((4 - i) * a0 + (1 + i) * a1) / 5) | 0; apal[6] = 0; apal[7] = 255; }
          let lo = d[at + 2] | (d[at + 3] << 8) | (d[at + 4] << 16), hi = d[at + 5] | (d[at + 6] << 8) | (d[at + 7] << 16);
          for (let i = 0; i < 8; i++) { alpha[i] = apal[lo & 7]; lo >>= 3; }
          for (let i = 8; i < 16; i++) { alpha[i] = apal[hi & 7]; hi >>= 3; }
        }
        for (let i = 0; i < 16; i++) {
          const x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
          if (x >= w || y >= h) continue;
          const c = pal[(bitsC >>> (2 * i)) & 3], o = (y * w + x) * 4;
          px[o] = c[0]; px[o + 1] = c[1]; px[o + 2] = c[2]; px[o + 3] = dxt1 ? c[3] : alpha[i];
        }
      }
      return { w, h, px, fmt: four };
    }
    if (!(pf & 0x40) || bits % 8 || !bits || bits > 32) return null;
    const bpp = bits >> 3, row = (flags & 8) && pitch ? pitch : w * bpp;
    if (size < row * (h - 1) + w * bpp) return null;
    const hasAlpha = !!(pf & 1);
    const chan = [92, 96, 100, 104].map((at, c) => {
      const m = c === 3 && !hasAlpha ? 0 : dv.getUint32(at, true);
      if (!m) return null;
      let shift = 0; while (!((m >>> shift) & 1)) shift++;
      let n = 0; while (shift + n < 32 && ((m >>> (shift + n)) & 1)) n++;
      return { shift, n, mask: n >= 32 ? 0xffffffff : (2 ** n) - 1, top: (2 ** n) - 1 };
    });
    for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
      const at = 128 + y * row + x * bpp;
      let v = 0;
      for (let k = bpp - 1; k >= 0; k--) v = v * 256 + d[at + k];
      const o = (y * w + x) * 4;
      for (let c = 0; c < 4; c++) {
        const ch = chan[c];
        if (!ch) { px[o + c] = 255; continue; }
        const q = Math.floor(v / 2 ** ch.shift) % (ch.mask + 1);
        px[o + c] = ch.n >= 8 ? Math.floor(q / 2 ** (ch.n - 8)) & 255 : Math.floor((q * 255 + Math.floor(ch.top / 2)) / ch.top);
      }
    }
    return { w, h, px, fmt: 'rgb' + bits };
  }

  // decodeDds, plus DX10 files of 8-bit BGRA or RGBA (DXGI 87/91 B8G8R8A8, 28/29 R8G8B8A8; four
  // high_ portraits are B8G8R8A8_SRGB).
  function decodeImage(d) {
    if (!d || d.length < 148 || cstr(d, 0, 4) !== 'DDS ' || enc.decode(d.subarray(84, 88)) !== 'DX10') return decodeDds(d);
    const dv = new DataView(d.buffer, d.byteOffset, d.byteLength);
    const h = dv.getUint32(12, true), w = dv.getUint32(16, true), format = dv.getUint32(128, true);
    const bgra = format === 87 || format === 91;
    if (!bgra && format !== 28 && format !== 29) return null;
    if (!w || !h || w > 4096 || h > 4096 || d.length < 148 + w * h * 4) return null;
    const px = d.slice(148, 148 + w * h * 4);
    if (bgra) for (let i = 0; i < px.length; i += 4) { const t = px[i]; px[i] = px[i + 2]; px[i + 2] = t; }
    return { w, h, px, fmt: 'dx10:' + format };
  }

  // ======================================================================================
  // .zip
  // ======================================================================================
  // A .zip made in the page, for browsers that can read a picked folder but not write to one (no
  // showDirectoryPicker). Entries are stored, or deflated when the caller deflates them
  // (writeDeflated). No zip64, so at most 65,535 files and 4 GB. Each file goes into a Blob as it
  // comes, so the browser may keep the bytes on disk instead of in memory.
  // zipWriter(prefix): write(path, bytes or string) under prefix; blob() ends it.
  let crcTable = null;
  function crc32(d, crc) {
    if (!crcTable) {
      crcTable = new Uint32Array(256);
      for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
        crcTable[n] = c;
      }
    }
    let c = crc === undefined ? -1 : ~crc;
    for (let i = 0; i < d.length; i++) c = crcTable[(c ^ d[i]) & 255] ^ (c >>> 8);
    return ~c >>> 0;
  }
  function zipWriter(prefix) {
    const utf8 = new TextEncoder(), parts = [], cd = [];
    const t = new Date(); // DOS time and date, as unzip shows them
    const time = (t.getHours() << 11) | (t.getMinutes() << 5) | (t.getSeconds() >> 1);
    const date = ((t.getFullYear() - 1980) << 9) | ((t.getMonth() + 1) << 5) | t.getDate();
    let at = 0;
    // The fields the local header and the central directory share, from "version needed" on.
    const common = (v, name, crc, packed, size, method) => {
      v.setUint16(0, 20, true); v.setUint16(2, 0, true); v.setUint16(4, method, true);
      v.setUint16(6, time, true); v.setUint16(8, date, true); v.setUint32(10, crc, true);
      v.setUint32(14, packed, true); v.setUint32(18, size, true); v.setUint16(22, name.length, true);
    };
    // data: the bytes as stored (a Uint8Array or a Blob), packed: their length.
    const add = (path, data, packed, crc, size, method) => {
      const name = utf8.encode(prefix + path);
      if (cd.length >= 0xFFFF || at + 30 + name.length + packed > 0xFFFFFFFF || size > 0xFFFFFFFF) throw new Error('The files are too big for a .zip.');
      const head = new Uint8Array(30 + name.length), hv = new DataView(head.buffer);
      hv.setUint32(0, 0x04034B50, true);
      common(new DataView(head.buffer, 4), name, crc, packed, size, method);
      head.set(name, 30);
      const ent = new Uint8Array(46 + name.length), ev = new DataView(ent.buffer);
      ev.setUint32(0, 0x02014B50, true); ev.setUint16(4, 20, true);
      common(new DataView(ent.buffer, 6), name, crc, packed, size, method);
      ev.setUint32(42, at, true);
      ent.set(name, 46);
      parts.push(new Blob([head, data]));
      cd.push(ent);
      at += head.length + packed;
    };
    return {
      write(path, data) {
        if (typeof data === 'string') data = utf8.encode(data);
        add(path, data, data.length, crc32(data), data.length, 0);
      },
      // A file deflated by the caller (raw deflate): its bytes, and the CRC and size before.
      writeDeflated(path, packed, crc, size) {
        add(path, packed, packed.size !== undefined ? packed.size : packed.length, crc, size, 8);
      },
      blob() {
        const size = cd.reduce((s, e) => s + e.length, 0);
        const end = new Uint8Array(22), v = new DataView(end.buffer);
        v.setUint32(0, 0x06054B50, true); v.setUint16(8, cd.length, true); v.setUint16(10, cd.length, true);
        v.setUint32(12, size, true); v.setUint32(16, at, true);
        return new Blob([...parts, ...cd, end], { type: 'application/zip' });
      },
    };
  }

  const api = { open: (files) => Packs.open(files), Packs, readPackIndex, mtLowBytes, inflate, asText, decodeDds, decodeImage, crc32, zipWriter, lowerSlashes, cmp };
  root.MabiPacks = api;
  if (typeof module === 'object' && module.exports) module.exports = api;
})(typeof window !== 'undefined' ? window : globalThis);
