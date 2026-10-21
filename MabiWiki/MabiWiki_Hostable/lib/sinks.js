/* Mabinogi Wiki: where the wiki's prepared files go (prepare.js writes them through one of these).
 *
 *   MabiWikiSinks.folder(dir)      a folder (FileSystemDirectoryHandle, readwrite): the MabiWiki
 *                                  folder next to the page, so paths land in MabiWiki\data\...
 *   MabiWikiSinks.idb()            this browser: IndexedDB 'MabiWikiData', store 'files'
 *                                  (path -> Blob) and store 'meta' ('build': the build info)
 *   MabiWikiSinks.zip(prefix)      a .zip (stored entries; big .js files deflated), its entries
 *                                  under prefix (default 'MabiWiki/'); close() gives the Blob
 *   MabiWikiSinks.idbLoad()        Promise<Map(path -> Blob)> of the browser's copy, with .meta
 *
 * Each sink: write(path, string | Uint8Array | Blob), clear(path) (a folder and all in it, before a
 * category is made again: Kanan deletes the last run's pictures), flush(), close(). Paths are
 * relative to the MabiWiki folder, with '/': 'data/game-data.js', 'data/icons/items/x.png'. A
 * write may return before the file is written (the folder sink keeps about 8 going at once); an
 * error comes out of a later write, flush() or close().
 */
(function (root) {
  'use strict';

  const utf8 = new TextEncoder();
  const toBlob = (data) => (data instanceof Blob ? data : new Blob([data]));

  // ======================================================================================
  // A folder (File System Access; also the browser's private folder, navigator.storage.getDirectory)
  // ======================================================================================
  function folder(dir, options) {
    const limit = (options && options.parallel) || 8;
    const dirs = new Map([['', Promise.resolve(dir)]]);
    // Folders below dir, made as needed (one promise each, so writes don't race to make one).
    const sub = (path) => {
      if (!dirs.has(path)) {
        const cut = path.lastIndexOf('/');
        const made = sub(cut < 0 ? '' : path.slice(0, cut)).then((d) => d.getDirectoryHandle(path.slice(cut + 1), { create: true }));
        made.catch(() => dirs.delete(path));
        dirs.set(path, made);
      }
      return dirs.get(path);
    };
    const running = new Set();
    let failed = null;
    async function put(path, data) {
      const cut = path.lastIndexOf('/');
      const d = await sub(cut < 0 ? '' : path.slice(0, cut));
      const w = await (await d.getFileHandle(path.slice(cut + 1), { create: true })).createWritable();
      try {
        await w.write(data);
        await w.close();
      } catch (e) {
        try { await w.abort(); } catch (e2) { /* already closed */ }
        throw e;
      }
    }
    const sink = {
      kind: 'folder',
      dir,
      async write(path, data) {
        while (running.size >= limit && !failed) await Promise.race(running);
        if (failed) throw failed;
        const p = put(path, data).catch((e) => { failed = failed || e; }).finally(() => running.delete(p));
        running.add(p);
      },
      async flush() {
        while (running.size) await Promise.all([...running]);
        if (failed) throw failed;
      },
      async clear(path) {
        await sink.flush();
        const cut = path.lastIndexOf('/');
        const parent = await sub(cut < 0 ? '' : path.slice(0, cut));
        try { await parent.removeEntry(path.slice(cut + 1), { recursive: true }); } catch (e) { if (e.name !== 'NotFoundError') throw e; }
        for (const k of [...dirs.keys()]) if (k === path || k.startsWith(path + '/')) dirs.delete(k);
      },
      // A file written before (or there already), as a File; null when it isn't there.
      async read(path) {
        await sink.flush();
        const parts = path.split('/'), name = parts.pop();
        try {
          let d = dir;
          for (const p of parts) d = await d.getDirectoryHandle(p);
          return await (await d.getFileHandle(name)).getFile();
        } catch (e) { return null; }
      },
      close: () => sink.flush(),
    };
    return sink;
  }

  // ======================================================================================
  // IndexedDB
  // ======================================================================================
  const DB = 'MabiWikiData';

  function openDb() {
    return new Promise((resolve, reject) => {
      const r = indexedDB.open(DB, 1);
      r.onupgradeneeded = () => {
        if (!r.result.objectStoreNames.contains('files')) r.result.createObjectStore('files');
        if (!r.result.objectStoreNames.contains('meta')) r.result.createObjectStore('meta');
      };
      r.onsuccess = () => resolve(r.result);
      r.onerror = () => reject(r.error);
      r.onblocked = () => reject(new Error('The browser\'s copy of the wiki is open in another tab.'));
    });
  }
  // One transaction on store: fill(store) queues the requests; resolves when it's committed.
  function tx(db, store, fill) {
    return new Promise((resolve, reject) => {
      const t = db.transaction(store, 'readwrite');
      fill(t.objectStore(store));
      t.oncomplete = () => resolve();
      t.onerror = () => reject(t.error);
      t.onabort = () => reject(t.error || new Error('aborted'));
    });
  }

  // Writes go in batches of about 200 files per transaction, one batch at a time.
  function idb(options) {
    const size = (options && options.batch) || 200;
    const db = openDb();
    let batch = [], chain = Promise.resolve(), failed = null;
    const commit = () => {
      const items = batch;
      batch = [];
      if (items.length) {
        chain = chain.then(async () => tx(await db, 'files', (s) => { for (const [k, v] of items) s.put(v, k); }))
          .catch((e) => { failed = failed || e; });
      }
      return chain.then(() => { if (failed) throw failed; });
    };
    const sink = {
      kind: 'idb',
      write(path, data) {
        if (failed) return Promise.reject(failed);
        batch.push([path, toBlob(data)]);
        return batch.length >= size ? commit() : Promise.resolve();
      },
      flush: commit,
      async clear(path) {
        await commit();
        await tx(await db, 'files', (s) => s.delete(IDBKeyRange.bound(path + '/', path + '/￿')));
      },
      // The build info, kept beside the files (idbLoad gives it back as .meta).
      async setMeta(info) {
        await commit();
        await tx(await db, 'meta', (s) => s.put(info, 'build'));
      },
      async close() {
        await commit();
        (await db).close();
      },
    };
    return sink;
  }

  // The browser's copy: Map(path -> Blob), with .meta the build info (or null). Empty when there's
  // none.
  async function idbLoad() {
    const files = new Map();
    files.meta = null;
    if (!root.indexedDB) return files;
    const db = await openDb();
    try {
      await new Promise((resolve, reject) => {
        const t = db.transaction(['files', 'meta'], 'readonly');
        const keys = t.objectStore('files').getAllKeys(), values = t.objectStore('files').getAll(), meta = t.objectStore('meta').get('build');
        t.oncomplete = () => {
          keys.result.forEach((k, i) => files.set(k, values.result[i]));
          files.meta = meta.result || null;
          resolve();
        };
        t.onerror = () => reject(t.error);
      });
    } finally { db.close(); }
    return files;
  }

  // ======================================================================================
  // A .zip download
  // ======================================================================================
  // .js and .txt files over 64 KB are deflated (game-data.js: about 45 MB to 6 MB); pictures are
  // stored (they're compressed already). options.download: a file name to download the zip as
  // when it's closed (otherwise the caller saves close()'s Blob).
  function zip(prefix, options) {
    const P = root.MabiPacks;
    const z = P.zipWriter(prefix === undefined ? 'MabiWiki/' : prefix);
    const deflate = !!root.CompressionStream && !(options && options.store);
    let chain = Promise.resolve(), result = null;
    const add = async (path, data) => {
      const bytes = data instanceof Blob ? new Uint8Array(await data.arrayBuffer()) : typeof data === 'string' ? utf8.encode(data) : data;
      if (deflate && bytes.length > 65536 && /\.(js|txt)$/i.test(path)) {
        const packed = await new Response(new Blob([bytes]).stream().pipeThrough(new CompressionStream('deflate-raw'))).blob();
        z.writeDeflated(path, packed, P.crc32(bytes), bytes.length);
      } else z.write(path, bytes);
    };
    const sink = {
      kind: 'zip',
      rewrites: false, // a path written twice would be in the zip twice
      write(path, data) {
        chain = chain.then(() => add(path, data));
        return chain;
      },
      flush: () => chain,
      clear: () => chain, // a new zip has nothing to clear
      async close() {
        await chain;
        if (!result) {
          result = z.blob();
          if (options && options.download) download(result, options.download);
        }
        return result;
      },
    };
    sink.blob = sink.close;
    return sink;
  }

  function download(blob, name) {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 60000);
  }

  root.MabiWikiSinks = { folder, idb, zip, idbLoad, download };
})(typeof window !== 'undefined' ? window : globalThis);
