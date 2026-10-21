// The wiki's loader (window.MabiWikiBoot). The page's data is the game's own files, made ready for the
// browser (game-data.js, the icon and image maps and the pictures under data/), and it can come from
// three places, tried in this order:
//
//   1. files next to the page: the plain <script src="data/..."> tags above this one (index.html;
//      MabiWiki/data/... in the single-file page) already ran, so window.GAME_DATA is set;
//   2. this browser's copy (IndexedDB, MabiWikiSinks.idbLoad, sinks.js): its scripts go in as blob:
//      URLs and window.MABIWIKI_URL turns a data path into its picture's blob: URL;
//   3. neither: the setup screen. The reader picks the game's folder (or its .pack files), says
//      whether the wiki shows 3D models and where its files go (next to the page, in this browser,
//      or a zip to extract next to the page), and clicks Process: nothing is read before that.
//      MabiWikiPrepare (prepare.js) then makes the files from the packs (MabiPacks, packs.js).
//
// The page can't read a folder the reader hasn't picked, but it can tell whether a package\ folder
// sits next to it (probePackage: a <link rel=prefetch> of it loads or fails in Chromium; Firefox
// fetches file:// listings), and the setup screen says so. A folder picked in a browser with
// showDirectoryPicker is kept (IndexedDB MabiWiki3D/kv 'folder', where the 3D viewer keeps it too).
//
// datasource.js and app.js are <script type="text/plain" data-run> blocks: they read the globals
// once when they start, so they run (runApp) only once the data is in place. Then a freshness check
// compares the packs the data came from (window.MABIWIKI_BUILD, data/build-info.js) with the game's
// and offers an update. #/setup (app.js) shows the setup screen inside the wiki.
//
// window.MabiWikiLib(name, fallback) gives a library's URL: the page's inline copy
// (<script type="text/plain" id="lib-NAME">) as a blob: URL, else the fallback path.
(() => {
  'use strict';

  const root = window;
  // The data's format this page reads and makes (prepare.js FORMAT). A build-info.js with a lower
  // one was made by an older page: its files are made again.
  const PAGE_FORMAT = (root.MabiWikiPrepare && root.MabiWikiPrepare.FORMAT) || 1;
  const BASE = root.MABIWIKI_BASE || '';
  const DATA_SCRIPTS = ['data/build-info.js', 'data/game-data.js', 'data/item-icons.js', 'data/skill-icons.js', 'data/npc-images.js', 'data/maps.js'];
  const ZIP_NAME = 'MabiWiki-data.zip';

  const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  const fmt = n => Number(n || 0).toLocaleString('en-US');
  const gb = n => (n / 1073741824).toFixed(1) + ' GB';
  const pageName = () => { try { return decodeURIComponent(location.pathname.split('/').pop() || ''); } catch (e) { return ''; } };
  // The page's folder as Windows shows it (file:///D:/Games/Mabinogi/x.html -> D:\Games\Mabinogi).
  const pageFolderText = () => {
    try {
      const p = decodeURIComponent(location.pathname).replace(/\/[^/]*$/, '');
      return location.protocol === 'file:' ? p.replace(/^\/([A-Za-z]:)/, '$1').replace(/\//g, '\\') : '';
    } catch (e) { return ''; }
  };
  const isPack = f => /\.pack$/i.test(f.name);

  // ---------- libraries: inline copies first ----------
  const libUrls = {};
  root.MabiWikiLib = (name, fallback) => {
    if (libUrls[name]) return libUrls[name];
    const el = document.getElementById('lib-' + name);
    const text = el && el.textContent;
    if (text && text.trim()) return (libUrls[name] = URL.createObjectURL(new Blob([text], { type: 'text/javascript' })));
    return fallback;
  };

  // ---------- IndexedDB MabiWiki3D/kv: the game folder (shared with the 3D viewer) and the page's folder ----------
  function kv() {
    return new Promise((ok, no) => {
      const r = indexedDB.open('MabiWiki3D', 1);
      r.onupgradeneeded = () => r.result.createObjectStore('kv');
      r.onsuccess = () => ok(r.result);
      r.onerror = () => no(r.error);
    });
  }
  async function kvGet(key) {
    if (!root.indexedDB) return null;
    const db = await kv();
    return new Promise((ok, no) => { const r = db.transaction('kv').objectStore('kv').get(key); r.onsuccess = () => ok(r.result); r.onerror = () => no(r.error); });
  }
  async function kvSet(key, value) {
    if (!root.indexedDB) return;
    const db = await kv();
    return new Promise((ok, no) => { const tx = db.transaction('kv', 'readwrite'); tx.objectStore('kv').put(value, key); tx.oncomplete = () => ok(); tx.onerror = () => no(tx.error); });
  }

  // ---------- running the wiki ----------
  let ran = false;
  function runApp() {
    if (ran) return;
    ran = true;
    // Made without the 3D models (the setup's "Include 3D models: No"): no 3D sections.
    const B = root.MABIWIKI_BUILD;
    if (B && B.models === false) root.MABIWIKI_NO3D = true;
    // The single-file page has the code inline (it runs as it's put in); index.html names the files
    // next to it, which run in order, and the last one's load says the wiki is up.
    let last = null;
    for (const el of document.querySelectorAll('script[data-run]')) {
      const s = document.createElement('script');
      if (el.textContent.trim()) s.text = el.textContent;
      else { s.src = el.getAttribute('src'); s.async = false; last = s; }
      document.body.appendChild(s);
    }
    const after = () => { banners(); check(); };
    if (last) last.addEventListener('load', after); else after();
  }

  function loadScript(src) {
    return new Promise(ok => {
      const s = document.createElement('script');
      s.src = src;
      s.async = false;
      s.onload = () => ok(true);
      s.onerror = () => { s.remove(); ok(false); };
      document.head.appendChild(s);
    });
  }

  // Data files held as Blobs (this browser's copy, or the zip just made): their scripts, then
  // MABIWIKI_URL for the pictures.
  async function fromBlobs(files, source) {
    if (!files || !files.has('data/game-data.js')) return false;
    const urls = new Map();
    const blobUrl = p => {
      if (!urls.has(p)) { const b = files.get(p); urls.set(p, b ? URL.createObjectURL(b instanceof Blob ? b : new Blob([b])) : null); }
      return urls.get(p);
    };
    for (const p of DATA_SCRIPTS) if (files.has(p)) await loadScript(blobUrl(p));
    if (!root.GAME_DATA) return false;
    root.MABIWIKI_URL = p => blobUrl(p) || BASE + p;
    Boot.source = source;
    return true;
  }

  // idbLoad's result as a Map(path -> Blob): it may give a Map, a plain object, or {files, meta}.
  function asFiles(loaded) {
    if (!loaded) return null;
    let files = loaded.files !== undefined ? loaded.files : loaded;
    if (!(files instanceof Map)) files = new Map(Object.entries(files || {}));
    return files.size ? files : null;
  }
  async function fromBrowser() {
    const Sinks = root.MabiWikiSinks;
    if (!Sinks || !Sinks.idbLoad || !root.indexedDB) return false;
    let files = null;
    try { files = asFiles(await Sinks.idbLoad()); } catch (e) { files = null; }
    return fromBlobs(files, 'browser');
  }

  // ---------- what the browser can do, and where the page is ----------
  const has = {
    folderPick: typeof root.showDirectoryPicker === 'function',
    dirInput: 'webkitdirectory' in document.createElement('input'),
    idb: !!root.indexedDB,
  };

  // Inside a zip that Explorer (or 7-Zip, WinRAR) opened: its temporary copy goes away.
  function inZip() {
    let p = '';
    try { p = decodeURIComponent(location.pathname); } catch (e) { p = location.pathname; }
    return /\/Temp\d*_[^/]*\.zip\//i.test(p) || /\/7zO[0-9A-F]+\//i.test(p) || /\/Rar\$EX/i.test(p) || /\.zip\/[^/]*$/i.test(p);
  }

  // Firefox's file:// folder listing ("201: name size date type" lines): the .pack files in it.
  function parseListing(text) {
    const out = [];
    for (const line of String(text).split('\n')) {
      const m = /^201:\s+(\S+)\s+(\d+)\s+(\S+)/.exec(line);
      if (!m) continue;
      let name = m[1], when = m[3];
      try { name = decodeURIComponent(name); when = decodeURIComponent(when); } catch (e) { /* as it is */ }
      if (/\.pack$/i.test(name)) out.push({ name, size: +m[2], modified: Date.parse(when) || 0 });
    }
    return out;
  }
  // Is there a package\ folder next to this page? {found, packs (Firefox: from its listing)}.
  let probed = null;
  function probePackage() {
    if (probed) return probed;
    if (location.protocol !== 'file:') return (probed = Promise.resolve({ found: false }));
    const dir = new URL('package/', location.href).href;
    probed = new Promise(ok => {
      let left = 0, done = false;
      const finish = (found, packs) => {
        if (done) return;
        if (found || --left <= 0) { done = true; ok({ found: !!found, packs: packs || null }); }
      };
      // Chromium: a prefetch of the folder loads (its listing) or fails. Firefox fires neither.
      left++;
      const l = document.createElement('link');
      l.rel = 'prefetch';
      l.href = dir;
      l.onload = () => { l.remove(); finish(true); };
      l.onerror = () => { l.remove(); finish(false); };
      document.head.appendChild(l);
      // Firefox reads file:// folders (a listing with the packs' sizes and dates); Chromium (which
      // has navigator.userAgentData, with or without a folder picker) can't, and logs the try.
      if (!has.folderPick && !navigator.userAgentData) {
        left++;
        fetch(dir).then(r => (r.ok ? r.text() : null)).then(t => (t === null ? finish(false) : finish(true, parseListing(t))), () => finish(false));
      }
      // Neither answered (Firefox without the folder): not found.
      setTimeout(() => { if (!done) { done = true; ok({ found: false, packs: null }); } }, 4000);
    });
    return probed;
  }

  // ---------- the setup screen ----------
  // What the reader chose: the packs (Files) and the folder they came from (a handle), or the
  // kept folder, the 3D choice and where the files go.
  const S = { files: null, gameDir: null, kept: null, keptPerm: '', busy: false };

  function card(el, html) {
    el.innerHTML = `<div class="mw-setup"><div class="mw-card">${html}</div></div>`;
    return el.querySelector('.mw-card');
  }

  async function packFilesIn(dir) {
    let pkg = dir;
    try { pkg = await dir.getDirectoryHandle('package'); } catch (e) { /* the package folder itself */ }
    const files = [];
    for await (const [name, h] of pkg.entries()) if (h.kind === 'file' && /\.pack$/i.test(name)) files.push(await h.getFile());
    return files;
  }
  function readError(e) {
    const n = e && e.name;
    if (n === 'NotReadableError' || n === 'NotFoundError') return 'Your game files changed while they were being read (a patch, or the game updating). Choose the folder again.';
    if (n === 'QuotaExceededError') return 'Your browser has no room left to keep the wiki\'s files. Keep them next to this page or download them instead.';
    if (n === 'NotAllowedError' || n === 'SecurityError') return 'Your browser needs a click to allow that. Click Process again.';
    return (e && e.message) || String(e);
  }

  function screenSetup(el, opts = {}) {
    if (inZip()) {
      card(el, `<h1>Extract the zip first</h1>
        <p>This page is open from inside a zip file, so anything it saves would be lost. Extract the zip (right-click it, <b>Extract All…</b>), then open <b>${esc(pageName() || 'the wiki page')}</b> from the extracted folder.</p>`);
      return;
    }
    const update = !!opts.update;
    const B = root.MABIWIKI_BUILD;
    const when = (B && B.built) || (root.GAME_DATA && root.GAME_DATA.built) || '';
    const shadowed = Boot.source === 'files';  // files next to the page win over this browser's copy
    // The copy for hosting keeps its data in its own folder (or a zip of it); the standalone page
    // also in this browser.
    const where = [];
    if (has.folderPick) where.push('folder');
    if (!HOSTABLE && has.idb && root.MabiWikiSinks && root.MabiWikiSinks.idb && !shadowed) where.push('browser');
    where.push('zip');
    const folderText = pageFolderText(), inFolder = folderText ? ` (<b>${esc(folderText)}</b>)` : '';
    const whereText = HOSTABLE ? {
      folder: ['Into this folder', `The data folder goes next to this page${inFolder}, and the folder is then ready to upload to your web host as it is. Your browser asks you to choose the folder.`],
      zip: ['Download the data', `As ${ZIP_NAME}, holding the data folder: extract it into the folder this page is in${inFolder}. The folder is then ready to upload.`],
    } : {
      folder: ['Next to this page', `In the folder this page is in${inFolder}. It opens instantly from then on, in any browser, and survives clearing your browser's data.`],
      browser: ['In this browser', 'Your browser keeps them. It opens instantly from then on, but clearing your browser\'s data (or a private window) means setting it up again.'],
      zip: ['Download the files', `As ${ZIP_NAME}, to extract into the folder this page is in${inFolder}. The wiki then opens instantly in any browser.`],
    };
    const no3d = HOSTABLE ? !!root.MABIWIKI_NO3D : B ? B.models === false : false;
    const yes3d = HOSTABLE ? 'Every model the pages show goes into data/models (about 520 MB in 24,000 files), so visitors can view items, monsters and regions in 3D once the site is hosted. Processing takes longer with them.'
      : `Items, monsters and regions can be viewed in 3D. The 3D views read the models from your game files when you open one${has.folderPick ? '' : ' (your browser asks for the folder once per visit)'}.`;
    const c = card(el, `
      <h1>${update ? 'Update from your game files' : 'Set up the wiki from your game files'}</h1>
      <p>The wiki is made from your own Mabinogi game files, on this computer. Nothing is uploaded.</p>
      ${update && when ? `<p class="mw-muted mw-small">The wiki's data is from ${esc(when)}.${B && B.complete === false ? ' Setting it up didn\'t finish last time.' : ''}</p>` : ''}
      <section class="mw-sec">
        <h2>Your game files</h2>
        <p class="mw-found" hidden></p>
        <p class="mw-chosen"></p>
        <div class="mw-row">
          <button type="button" class="btn mw-pick">Choose folder</button>
          <input type="file" class="mw-dir" webkitdirectory multiple hidden>
          <input type="file" class="mw-packs" accept=".pack" multiple hidden>
          <button type="button" class="linkbtn mw-packlink">or choose the .pack files</button>
        </div>
        ${!has.folderPick && has.dirInput ? '<p class="mw-muted mw-small">Pick the <b>package</b> folder inside your Mabinogi folder rather than the whole folder, so your browser doesn\'t list every game file.</p>' : ''}
      </section>
      <section class="mw-sec">
        <h2>Include 3D models</h2>
        <div class="mw-choices mw-inline" role="radiogroup">
          <label class="mw-choice"><input type="radio" name="mw-3d" value="yes"${no3d ? '' : ' checked'}><span><b>Yes</b><br><span class="mw-muted mw-small">${yes3d}</span></span></label>
          <label class="mw-choice"><input type="radio" name="mw-3d" value="no"${no3d ? ' checked' : ''}><span><b>No</b><br><span class="mw-muted mw-small">Text and pictures only: the pages have no 3D views.</span></span></label>
        </div>
      </section>
      <section class="mw-sec mw-where-sec">
        <h2>Where to keep the wiki's files</h2>
        <p class="mw-muted mw-small"><span class="mw-size"></span>${shadowed && !has.folderPick && !HOSTABLE ? ' The wiki already has files next to this page, so the update comes as a zip: extract it over them.' : ''}</p>
        <div class="mw-choices" role="radiogroup">
          ${where.map((k, i) => `<label class="mw-choice"><input type="radio" name="mw-where" value="${k}"${i ? '' : ' checked'}><span><b>${whereText[k][0]}</b>${!i && where.length > 1 ? ' <small class="mw-rec">recommended</small>' : ''}<br><span class="mw-muted mw-small">${whereText[k][1]}</span></span></label>`).join('')}
        </div>
      </section>
      <div class="mw-row mw-go-row"><button type="button" class="btn primary mw-go" disabled>Process</button><span class="mw-muted mw-small mw-gohint">Choose your game files first.</span></div>
      <p class="mw-msg" role="status"></p>`);
    const $ = s => c.querySelector(s);
    const pick = $('.mw-pick'), dir = $('.mw-dir'), packs = $('.mw-packs'), go = $('.mw-go'), msg = $('.mw-msg');
    const say = (t, warn) => { msg.innerHTML = t || ''; msg.classList.toggle('warn', !!warn); };
    let found = null;

    // The size follows the 3D choice: the hostable folder copies the models into data/models; the
    // standalone page reads them from the game files, so they add nothing here.
    const showSize = () => {
      const yes = c.querySelector('input[name="mw-3d"]:checked').value === 'yes';
      $('.mw-size').textContent = !yes ? 'About 175 MB of pictures and data.'
        : HOSTABLE ? 'About 695 MB: 175 MB of pictures and data, plus 520 MB of 3D models.'
        : 'About 175 MB of pictures and data. The 3D models aren\'t copied: they stay in your game files.';
    };
    c.querySelectorAll('input[name="mw-3d"]').forEach(r => r.addEventListener('change', showSize));
    showSize();

    const showChosen = () => {
      const chosen = $('.mw-chosen');
      if (S.files && S.files.length) {
        const size = S.files.reduce((a, f) => a + f.size, 0);
        chosen.innerHTML = `Chosen: <b>${S.files.length} game file${S.files.length === 1 ? '' : 's'}</b> (${gb(size)})${S.gameDir ? ` from <b>${esc(S.gameDir.name)}</b>` : ''}.`;
        pick.textContent = 'Choose another folder';
      } else if (S.kept) {
        chosen.innerHTML = S.keptPerm === 'granted'
          ? `Using your Mabinogi folder <b>${esc(S.kept.name)}</b>, remembered from last time.`
          : `Your Mabinogi folder <b>${esc(S.kept.name)}</b> is remembered. Your browser will ask you to allow it again when you click Process.`;
        pick.textContent = 'Choose another folder';
      } else {
        chosen.innerHTML = 'Choose your Mabinogi folder (or the <b>package</b> folder inside it).';
      }
      const ready = !!((S.files && S.files.length) || S.kept);
      go.disabled = !ready || S.busy;
      $('.mw-gohint').textContent = ready ? 'Nothing is read until you click Process.' : 'Choose your game files first.';
      const f = $('.mw-found');
      if (found && found.found && !(S.files && S.files.length) && !S.kept) {
        f.hidden = false;
        f.innerHTML = `<b>Found your game's package folder next to this page.</b> ` + (has.folderPick
          ? `Your browser still needs you to choose it once: click <b>Choose folder</b> and pick <b>${esc(folderText || 'this page\'s folder')}</b>. It's remembered after that.`
          : `Your browser still needs you to choose it: click <b>Choose folder</b> and pick <b>${esc(folderText ? folderText + '\\package' : 'the package folder')}</b>.`);
      } else f.hidden = true;
    };
    const useFiles = (files, gameDir) => {
      const list = [...files].filter(isPack);
      if (!list.length) { say('That folder has no Mabinogi packs. Choose your Mabinogi folder or the <b>package</b> folder inside it.', true); return; }
      say('');
      S.files = list; S.gameDir = gameDir || null; S.kept = null;
      showChosen();
    };
    pick.onclick = async () => {
      if (has.folderPick) {
        let h;
        try { h = await root.showDirectoryPicker({ id: 'mabinogi', mode: 'readwrite' }); } catch (e) { if (e && e.name !== 'AbortError') say('Your browser didn\'t open that folder. Try again, or choose the .pack files.', true); return; }
        try { useFiles(await packFilesIn(h), h); } catch (e) { say(esc(readError(e)), true); }
      } else if (has.dirInput) dir.click();
      else packs.click();
    };
    dir.onchange = () => useFiles(dir.files);
    packs.onchange = () => useFiles(packs.files);
    $('.mw-packlink').onclick = () => packs.click();
    go.onclick = () => processClick(el, c, opts, say);
    showChosen();

    probePackage().then(r => { found = r; showChosen(); });
    // A kept folder (Chromium): used without a picker, after one click to allow it when needed.
    if (has.folderPick) {
      kvGet('folder').then(async h => {
        if (!h || !h.queryPermission || (S.files && S.files.length)) return;
        S.kept = h;
        S.keptPerm = await h.queryPermission({ mode: 'read' }).catch(() => 'prompt');
        showChosen();
      }, () => {});
    }
  }

  // The kept game folder, when no packs were chosen this time: allowed again (mode 'read' or
  // 'readwrite') and its packs listed. False when it can't be used (said why).
  async function keptFiles(mode, say) {
    if ((S.files && S.files.length) || !S.kept) return true;
    let perm = await S.kept.queryPermission({ mode });
    if (perm !== 'granted') perm = await S.kept.requestPermission({ mode });
    if (perm !== 'granted') { say('Your browser didn\'t allow that folder. Click Process to try again, or choose the folder.', true); return false; }
    S.gameDir = S.kept;
    S.files = await packFilesIn(S.kept);
    if (!S.files.length) { S.files = null; say('That folder has no packs any more. Choose it again.', true); return false; }
    return true;
  }

  // Process: everything that needs the click first (the kept folder's permission, the page's folder
  // picker), then the packs are opened and the files made.
  async function processClick(el, c, opts, say) {
    if (S.busy) return;
    const where = c.querySelector('input[name="mw-where"]:checked').value;
    const models = c.querySelector('input[name="mw-3d"]:checked').value === 'yes';
    const go = c.querySelector('.mw-go');
    go.disabled = true;
    say('');
    try {
      // Read and write on the kept game folder when the files go next to the page, which is this
      // folder when the page sits in the game folder.
      if (!await keptFiles(where === 'folder' ? 'readwrite' : 'read', say)) return;
      let pageDir = null;
      if (where === 'folder') {
        pageDir = await pageFolder(say);
        if (!pageDir) return;
      }
      if (where === 'browser') {
        // Asked, not waited for: Firefox shows a prompt and answers only when the reader does.
        try { if (navigator.storage && navigator.storage.persist) navigator.storage.persist().catch(() => {}); } catch (e) { /* best effort */ }
      }
      await screenProgress(el, opts, where, pageDir, models);
    } catch (e) {
      say(esc(readError(e)), true);
    } finally { if (document.body.contains(go)) go.disabled = false; }
  }

  // The folder this page is in, writable: the game folder when the page is in it, else the one kept
  // from before, else picked (checked by the page's file name being there).
  async function pageFolder(say) {
    const name = pageName();
    const holdsPage = async d => { if (!name) return true; try { await d.getFileHandle(name); return true; } catch (e) { return false; } };
    const writable = async d => (await d.queryPermission({ mode: 'readwrite' })) === 'granted' || (await d.requestPermission({ mode: 'readwrite' })) === 'granted';
    if (S.gameDir && await holdsPage(S.gameDir) && await writable(S.gameDir)) return S.gameDir;
    const kept = await kvGet('pagefolder').catch(() => null);
    if (kept && await holdsPage(kept).catch(() => false) && await writable(kept)) return kept;
    const folderText = pageFolderText();
    say(`Choose the folder this page is in${folderText ? `: <b>${esc(folderText)}</b>` : ''}.`);
    let d;
    try { d = await root.showDirectoryPicker({ id: 'mabiwiki-page', mode: 'readwrite' }); } catch (e) {
      if (e && e.name === 'AbortError') { say('The files need the folder this page is in. Click Process to choose it, or keep the files somewhere else.'); return null; }
      throw e;
    }
    if (!await holdsPage(d)) {
      say(`That folder doesn't have <b>${esc(name)}</b> in it, so the wiki wouldn't find its files there. Click Process and choose the folder this page is in${folderText ? ` (<b>${esc(folderText)}</b>)` : ''}, or keep the files in this browser.`, true);
      return null;
    }
    await kvSet('pagefolder', d).catch(() => {});
    say('');
    return d;
  }

  // The sink with the 3D choice put into build-info.js (and the browser copy's meta), so later
  // opens know it (runApp sets MABIWIKI_NO3D).
  function withModels(sink, models) {
    const put = text => {
      const m = /^window\.MABIWIKI_BUILD = (\{[\s\S]*\});\s*$/.exec(text);
      if (!m) return text;
      try { const o = JSON.parse(m[1]); o.models = models; return 'window.MABIWIKI_BUILD = ' + JSON.stringify(o) + ';\n'; } catch (e) { return text; }
    };
    return new Proxy(sink, {
      get(t, k) {
        if (k === 'write') return (path, data) => t.write(path, path === 'data/build-info.js' && typeof data === 'string' ? put(data) : data);
        if (k === 'setMeta') return t.setMeta ? (meta => t.setMeta(Object.assign({}, meta, { models }))) : undefined;
        const v = t[k];
        return typeof v === 'function' ? v.bind(t) : v;
      },
    });
  }

  // The copy for hosting: its data made into its folder (or a zip of it), then the page again.
  async function screenHostable(el, opts, dir, models) {
    const c = card(el, `
      <h1>Making the wiki's data</h1>
      <p class="mw-step">Reading the game's files…</p>
      <div class="mw-bar"><div></div></div>
      <p class="mw-muted mw-small">Keep this page open until it's done.</p>
      <p class="mw-msg" role="status"></p>`);
    const step = c.querySelector('.mw-step'), bar = c.querySelector('.mw-bar > div'), msg = c.querySelector('.mw-msg');
    S.busy = true;
    try {
      const r = await makeHostable(dir, models, (t, f) => { step.textContent = t; if (typeof f === 'number') bar.style.width = Math.max(2, Math.round(f * 100)) + '%'; });
      bar.style.width = '100%';
      if (S.gameDir) await kvSet('folder', S.gameDir).catch(() => {});
      S.busy = false;
      const with3d = r.info ? ` with ${fmt(r.info.models)} 3D models` : '';
      if (dir) {
        card(el, `<h1>The wiki's data is ready</h1><p>The data${with3d} is in the data folder next to this page: the folder is ready to upload to your web host as it is (README.txt says how).</p><p>Opening the wiki…</p>`);
        Boot.reload();
        return;
      }
      const c2 = card(el, `<h1>The wiki's data is ready</h1>
        <p>Save <b>${ZIP_NAME}</b> and extract it into the folder this page is in${pageFolderText() ? ` (<b>${esc(pageFolderText())}</b>)` : ''}, letting it replace the data folder there. Then open this page again: the folder is ready to upload to your web host as it is (README.txt says how).</p>
        <div class="mw-row"><button type="button" class="btn primary mw-save">Save ${ZIP_NAME}</button></div>`);
      download(r.blob, ZIP_NAME);
      c2.querySelector('.mw-save').onclick = () => download(r.blob, ZIP_NAME);
    } catch (e) {
      S.busy = false;
      step.textContent = 'The wiki\'s data couldn\'t be made.';
      msg.textContent = readError(e);
      msg.classList.add('warn');
      const back = document.createElement('button');
      back.type = 'button'; back.className = 'btn'; back.textContent = 'Back';
      back.onclick = () => screenSetup(el, opts);
      c.appendChild(back);
    }
  }

  // Making the files, then done.
  async function screenProgress(el, opts, where, pageDir, models) {
    if (HOSTABLE) return screenHostable(el, opts, where === 'folder' ? pageDir : null, models);
    const c = card(el, `
      <h1>Making the wiki</h1>
      <p class="mw-step">Reading the game's files…</p>
      <div class="mw-bar"><div></div></div>
      <p class="mw-muted mw-small">Keep this page open until it's done.</p>
      <p class="mw-msg" role="status"></p>`);
    const step = c.querySelector('.mw-step'), bar = c.querySelector('.mw-bar > div'), msg = c.querySelector('.mw-msg');
    const Sinks = root.MabiWikiSinks, P = root.MabiWikiPrepare;
    let sink, target = null;
    S.busy = true;
    try {
      step.textContent = 'Reading the game\'s files…';
      const packs = await root.MabiPacks.open(S.files);
      if (!packs.has('db/itemdb.xml')) throw new Error('Those aren\'t Mabinogi\'s game files. Choose your Mabinogi folder or the package folder inside it.');
      if (where === 'folder') {
        target = BASE ? await pageDir.getDirectoryHandle(BASE.replace(/\/+$/, ''), { create: true }) : pageDir;
        sink = Sinks.folder(target);
      } else if (where === 'browser') sink = Sinks.idb();
      else sink = Sinks.zip(BASE || 'MabiWiki/');
      sink = withModels(sink, models);
      // Only what changed, when the same place already holds the rest (and the 3D choice is the same).
      const B = root.MABIWIKI_BUILD;
      const same = ((where === 'browser' && Boot.source === 'browser') || (where === 'folder' && Boot.source === 'files')) &&
        B && B.stamps && B.complete !== false && (B.format || 0) >= PAGE_FORMAT;
      const only = same ? P.changed(packs, B) : null;
      let info;
      if (only && !only.length) {
        // Nothing to make; a changed 3D choice only rewrites build-info.js.
        info = Object.assign({}, B, { models });
        if ((B.models !== false) !== models) {
          await sink.write('data/build-info.js', 'window.MABIWIKI_BUILD = ' + JSON.stringify(info) + ';\n');
          if (sink.flush) await sink.flush();
          if (sink.setMeta) await sink.setMeta(info);
        }
        step.textContent = 'The wiki is already up to date with your game files.';
      } else {
        info = await P.run(S.files, sink, {
          packs,
          only: only || undefined,
          previous: same ? B : null,
          onProgress: p => {
            const done = p && p.done, total = p && p.total;
            step.textContent = ((p && p.text) || '') + (total ? ` ${fmt(done)} / ${fmt(total)}` : '');
            if (p && typeof p.fraction === 'number') bar.style.width = Math.max(2, Math.round(p.fraction * 100)) + '%';
          },
        });
      }
      let zip = null;
      if (where === 'zip') zip = await finishZip(sink);
      else if (typeof sink.close === 'function') await sink.close();
      bar.style.width = '100%';
      // The game folder, for the 3D views (now and on later visits) and the next update.
      if (S.gameDir) await kvSet('folder', S.gameDir).catch(() => {});
      Boot.packFiles = models ? S.files : null;
      S.busy = false;
      await screenDone(el, opts, where, { info, zip, target });
    } catch (e) {
      S.busy = false;
      step.textContent = 'The wiki couldn\'t be made.';
      msg.textContent = readError(e);
      msg.classList.add('warn');
      const back = document.createElement('button');
      back.type = 'button'; back.className = 'btn'; back.textContent = 'Back';
      back.onclick = () => screenSetup(el, opts);
      c.appendChild(back);
    }
  }

  // The zip sink's file.
  async function finishZip(sink) {
    for (const f of ['finish', 'close', 'blob', 'end']) {
      if (typeof sink[f] === 'function') { const b = await sink[f](); if (b instanceof Blob) return b; }
    }
    return sink.result instanceof Blob ? sink.result : null;
  }
  const download = (blob, name) => root.MabiWikiSinks.download(blob, name);

  async function screenDone(el, opts, where, r) {
    const partly = r.info && r.info.complete === false;
    if (where === 'zip') {
      const folderText = pageFolderText();
      const c = card(el, `
        <h1>Your wiki's files are ready</h1>
        <p>Save <b>${ZIP_NAME}</b>, then extract it into the folder this page is in${folderText ? ` (<b>${esc(folderText)}</b>)` : ''}, so that the <b>${esc((BASE || 'data/').replace(/\/$/, ''))}</b> folder sits next to <b>${esc(pageName() || 'this page')}</b>. From then on the wiki opens instantly.</p>
        <div class="mw-row"><button type="button" class="btn primary mw-save">Save ${ZIP_NAME}</button>${!ran ? '<button type="button" class="btn mw-open">Open the wiki now</button>' : ''}</div>`);
      c.querySelector('.mw-save').onclick = () => r.zip && download(r.zip, ZIP_NAME);
      if (r.zip) download(r.zip, ZIP_NAME);
      const open = c.querySelector('.mw-open');
      if (open) open.onclick = async () => { open.disabled = true; if (await fromBlobs(await zipFiles(r.zip), 'zip')) start(); else open.textContent = 'Extract the zip, then open this page again'; };
      return;
    }
    if (ran) {
      // #/setup inside the running wiki: it read its data when it started, so it starts again.
      card(el, `<h1>${partly ? 'The wiki is partly made' : 'The wiki is up to date'}</h1><p>Reloading…</p>`);
      Boot.reload();
      return;
    }
    // First run: what was just made, then the wiki, right here.
    const ok = where === 'folder' ? await fromFolderAgain(r.target) : await fromBrowser();
    if (!ok) { card(el, '<h1>The wiki is made</h1><p>Open this page again to see it.</p>'); return; }
    start();
  }

  // The files just written next to the page: their script tags again (the query keeps a browser
  // from reusing the earlier miss), else read back through the folder handle.
  async function fromFolderAgain(target) {
    const v = '?v=' + Date.now();
    for (const p of DATA_SCRIPTS) await loadScript(BASE + p + v);
    if (root.GAME_DATA) { Boot.source = 'files'; return true; }
    if (!target) return false;
    const read = async p => {
      let d = target;
      const parts = p.split('/');
      for (const part of parts.slice(0, -1)) d = await d.getDirectoryHandle(part);
      return (await d.getFileHandle(parts[parts.length - 1])).getFile();
    };
    for (const p of DATA_SCRIPTS) { try { await loadScript(URL.createObjectURL(await read(p))); } catch (e) { /* missing */ } }
    if (!root.GAME_DATA) return false;
    Boot.source = 'files'; // the pictures by their path next to the page: they're there now
    return true;
  }

  // The entries of the zip just made (stored, or deflated), as Map(path below the page's data folder -> Blob).
  async function zipFiles(zip) {
    if (!zip) return null;
    const files = new Map();
    try {
      const tail = new DataView(await zip.slice(Math.max(0, zip.size - 65558)).arrayBuffer());
      let eocd = -1;
      for (let i = tail.byteLength - 22; i >= 0; i--) if (tail.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
      if (eocd < 0) return null;
      const n = tail.getUint16(eocd + 10, true), cdSize = tail.getUint32(eocd + 12, true), cdOff = tail.getUint32(eocd + 16, true);
      const cd = new DataView(await zip.slice(cdOff, cdOff + cdSize).arrayBuffer());
      const dec = new TextDecoder();
      for (let i = 0, at = 0; i < n; i++) {
        const method = cd.getUint16(at + 10, true), csize = cd.getUint32(at + 20, true);
        const nl = cd.getUint16(at + 28, true), xl = cd.getUint16(at + 30, true), cl = cd.getUint16(at + 32, true), lho = cd.getUint32(at + 42, true);
        const name = dec.decode(new Uint8Array(cd.buffer, cd.byteOffset + at + 46, nl));
        at += 46 + nl + xl + cl;
        const lh = new DataView(await zip.slice(lho, lho + 30).arrayBuffer());
        const begin = lho + 30 + lh.getUint16(26, true) + lh.getUint16(28, true);
        let blob = zip.slice(begin, begin + csize);
        if (method === 8) blob = await new Response(blob.stream().pipeThrough(new DecompressionStream('deflate-raw'))).blob();
        else if (method !== 0) continue;
        files.set(BASE && name.startsWith(BASE) ? name.slice(BASE.length) : name.replace(/^MabiWiki\//, ''), blob);
      }
    } catch (e) { return null; }
    return files;
  }

  // ---------- the copy for hosting (MabiWiki_Hostable) ----------
  // A folder laid out for hosting (hostable.html as its index.html, app.css, app.js, datasource.js,
  // sources.js, lib/ with the 3D viewer and this setup's scripts, README.txt, DEVELOPING.md). Its
  // data/ is made into the folder itself: game-data.js in parts of at most PART_BYTES, which
  // game-data.js loads (index.html names only it), the picture lists and pictures, site.js (the 3D
  // setting) and, with the 3D models, models/. Visitors of the hosted site only ever load it.
  const PART_BYTES = 16 * 1024 * 1024; // Cloudflare Pages takes files up to 25 MiB; the data is over 40 MB
  const HOSTABLE = !!root.MABIWIKI_HOSTED && BASE === './';
  const TOOLS = [['MabiPacks', 'packs.js'], ['MabiImages', 'images.js'], ['MabiWikiSinks', 'sinks.js'], ['MabiWikiPrepare', 'prepare.js']];

  // The setup's scripts (lib/), loaded only when the data is set up.
  async function ensureTools() {
    for (const [global, file] of TOOLS) if (!root[global]) await loadScript(BASE + 'lib/' + file);
    return TOOLS.every(([global]) => root[global]);
  }

  // A sink that writes game-data.js as a head that loads its parts, and the parts
  // (window.GAME_DATA.files[name] = text;) of at most PART_BYTES (a file bigger than that is a part
  // of its own). The data it wrote is kept
  // in .data (path -> the parsed GAME_DATA, or the list scripts' text), so the page can use it at once.
  function partsSink(sink) {
    const out = Object.create(sink);
    out.data = new Map();
    out.write = async (path, data) => {
      if (/^data\/(item-icons|skill-icons|npc-images|maps|build-info)\.js$/.test(path)) out.data.set(path, typeof data === 'string' ? data : await new Blob([data]).text());
      if (path !== 'data/game-data.js') return sink.write(path, data);
      const text = typeof data === 'string' ? data : await new Blob([data]).text();
      const all = JSON.parse(text.slice(text.indexOf('{'), text.lastIndexOf('}') + 1));
      const files = all.files || {};
      out.data.set(path, all);
      let part = [], size = 0, n = 0;
      const flush = async () => {
        if (!part.length) return;
        await sink.write('data/game-data-' + ++n + '.js', new Blob(part, { type: 'text/javascript' }));
        part = []; size = 0;
      };
      for (const name of Object.keys(files)) {
        const line = 'window.GAME_DATA.files[' + JSON.stringify(name) + '] = ' + JSON.stringify(files[name]) + ';\n';
        const bytes = new Blob([line]).size;
        if (part.length && size + bytes > PART_BYTES) await flush();
        part.push(line); size += bytes;
      }
      await flush();
      await sink.write(path, 'window.GAME_DATA = ' + JSON.stringify({ version: all.version, built: all.built, source: all.source, files: {} }) + ';\n' + partsLoader(n));
    };
    return out;
  }
  // The line that loads game-data.js's parts after it, from next to it.
  const partsLoader = n => '(function (n) { var s = document.currentScript, at = s ? s.src.replace(/[^/]*$/, \'\') : \'data/\';' +
    ' if (document.readyState === \'loading\') for (var i = 1; i <= n; i++) document.write(\'<script src="\' + at + \'game-data-\' + i + \'.js"><\\/script>\'); })(' + n + ');\n';

  const removeFrom = async (dir, name) => { try { await dir.removeEntry(name, { recursive: true }); } catch (e) { /* not there */ } };

  // The 3D viewer (three.js first: the page's own copies, else lib/), with the packs.
  async function viewerReady() {
    if (!root.MabiWikiViewer) {
      if (!root.THREE) await loadScript(root.MabiWikiLib('three', BASE + 'lib/three-bundle.min.js'));
      await loadScript(root.MabiWikiLib('viewer3d', BASE + 'lib/viewer3d.js'));
    }
    if (!root.MabiWikiViewer || !root.MabiWikiViewer.exportModels) throw new Error('The 3D viewer couldn\'t be loaded.');
    await root.MabiWikiViewer.usePackFiles(S.files);
  }

  // datasource.js again, on the data just made (the page's globals set from it), for the list of
  // the models its pages show.
  async function sourceOn(made) {
    root.GAME_DATA = made.get('data/game-data.js');
    for (const [path, text] of made) if (path !== 'data/game-data.js') new Function(text)(); // the list scripts set their globals
    const el = document.querySelector('script[data-run][src$="datasource.js"], script[data-run]#src-datasource');
    if (!el) throw new Error('This page has no datasource.js.');
    if (el.textContent.trim()) { const s = document.createElement('script'); s.text = el.textContent; document.body.appendChild(s); }
    else if (!await loadScript(el.getAttribute('src') + '?v=' + Date.now())) throw new Error('datasource.js couldn\'t be loaded.');
    return root.WIKI_SOURCE.models();
  }

  // Making this folder's data: the files, then with 3D the models (data/models/) and site.js. Into
  // the folder (dir) or, without a folder picker, a zip of data/ to extract into it.
  async function makeHostable(dir, models, progress) {
    const Sinks = root.MabiWikiSinks, P = root.MabiWikiPrepare;
    const packs = await root.MabiPacks.open(S.files);
    if (!packs.has('db/itemdb.xml')) throw new Error('Those aren\'t Mabinogi\'s game files. Choose your Mabinogi folder or the package folder inside it.');
    let z = null, base, data = null;
    if (dir) {
      // The old parts and models go (a new build may have fewer parts; No means no models).
      data = await dir.getDirectoryHandle('data', { create: true });
      const old = [];
      for await (const [name] of data.entries()) if (/^game-data(-\d+)?\.js$/.test(name)) old.push(name);
      for (const name of old) await removeFrom(data, name);
      await removeFrom(data, 'models');
      base = Sinks.folder(dir);
    } else base = z = Sinks.zip('');
    const sink = partsSink(base);
    await P.run(S.files, sink, {
      packs, previous: null,
      onProgress: p => progress(((p && p.text) || '') + (p && p.total ? ` ${fmt(p.done)} / ${fmt(p.total)}` : ''), p && typeof p.fraction === 'number' ? p.fraction * (models ? 0.4 : 0.95) : undefined),
    });
    let info = null;
    if (models) {
      progress('Finding the models the pages show…', 0.4);
      const list = await sourceOn(sink.data);
      await viewerReady();
      const out = data ? await data.getDirectoryHandle('models', { create: true }) : { write: (path, bytes) => z.write('data/models/' + path, bytes) };
      info = await root.MabiWikiViewer.exportModels(list, out, (stage, n, total) =>
        progress((stage === 'models' ? 'Reading the models: ' : 'Writing the models: ') + fmt(n) + ' of ' + fmt(total), 0.4 + 0.55 * (stage === 'models' ? n / total / 2 : 0.5 + n / total / 2)));
    }
    await base.write('data/site.js', models ? 'window.MABIWIKI_MODELS = "data/models/";\n' : 'window.MABIWIKI_NO3D = true;\n');
    const blob = z ? await z.close() : (await base.close(), null);
    return { info, blob };
  }

  // ---------- after the wiki is up: is its data current? ----------
  let bannerBox = null;
  function banner(key, html, actions) {
    try { if (sessionStorage.getItem('mw-banner-' + key)) return; } catch (e) { /* no storage */ }
    if (!bannerBox) {
      bannerBox = document.createElement('div');
      bannerBox.className = 'mw-banners';
      document.body.appendChild(bannerBox);
    }
    const old = bannerBox.querySelector(`[data-key="${key}"]`);
    if (old) old.remove();
    const b = document.createElement('div');
    b.className = 'mw-banner';
    b.dataset.key = key;
    b.setAttribute('role', 'status');
    b.innerHTML = `<div class="mw-btext">${html}</div><div class="mw-bact">${(actions || []).map((a, i) => `<button type="button" class="btn${i ? '' : ' primary'}" data-i="${i}">${esc(a[0])}</button>`).join('')}<button type="button" class="linkbtn mw-x">Not now</button></div>`;
    for (const btn of b.querySelectorAll('[data-i]')) btn.onclick = () => { b.remove(); actions[+btn.dataset.i][1](); };
    b.querySelector('.mw-x').onclick = () => { try { sessionStorage.setItem('mw-banner-' + key, '1'); } catch (e) { /* no storage */ } b.remove(); };
    bannerBox.appendChild(b);
  }
  const toSetup = () => { location.hash = '#/setup'; };

  function banners() {
    const B = root.MABIWIKI_BUILD;
    if (!B || !root.GAME_DATA) return;
    if (B.complete === false) banner('incomplete', 'Setting up the wiki didn\'t finish: some pictures may be missing.', [['Finish setting up', toSetup]]);
    else if ((B.format || 0) < PAGE_FORMAT) banner('format', 'This version of the wiki makes its pictures differently: update from your game files.', [['Update', toSetup]]);
  }

  // The game's packs ([{name, size, lastModified | modified}]) against the ones the data came from:
  // true (changed), false (the same), null (unknown: no build-info.js, an older Kanan's folder).
  function packsChanged(list) {
    const B = root.MABIWIKI_BUILD;
    if (!B || !Array.isArray(B.packs) || !B.packs.length) return null;
    const was = new Map(B.packs.map(p => [String(p.name).toLowerCase(), p]));
    const now = [...list].filter(isPack);
    if (now.length !== was.size) return true;
    for (const f of now) {
      const p = was.get(f.name.toLowerCase());
      const mod = f.lastModified !== undefined ? f.lastModified : f.modified;
      if (!p || +p.size !== +f.size || (p.modified && mod && Math.abs(+p.modified - mod) > 2000)) return true;
    }
    return false;
  }

  // Without a prompt: a kept game folder the browser still allows (Chromium), or the package folder
  // next to the page (Firefox lists it), compared by the packs' sizes and dates.
  async function check() {
    if (!root.GAME_DATA || banners.patched) return;
    let list = null;
    try {
      if (has.folderPick) {
        const h = await kvGet('folder');
        if (h && h.queryPermission && (await h.queryPermission({ mode: 'read' })) === 'granted') list = await packFilesIn(h);
      } else {
        const r = await probePackage();
        list = r.packs;
      }
    } catch (e) { list = null; }
    if (list && list.length && packsChanged(list) === true) {
      banners.patched = true;
      banner('patch', 'Your game was updated: update the wiki.', [['Update the wiki', toSetup]]);
    }
  }

  // ---------- start ----------
  function start() {
    const app = document.getElementById('app');
    if (app && app.querySelector('.mw-setup')) app.innerHTML = '<div class="boot">Loading the wiki…</div>';
    runApp();
  }

  const Boot = {
    format: PAGE_FORMAT,
    source: null,          // where the data came from: 'files' | 'browser' | 'zip' | null
    packFiles: null,       // the packs picked during setup, for the 3D views (taken once)
    // Whether this page can make its data again (app.js shows Update and #/setup only then): not a
    // hosted copy that visitors open from a web host.
    canUpdate: !(HOSTABLE && location.protocol !== 'file:'),
    setupView(el) {        // #/setup in the running wiki
      if (S.busy) { el.innerHTML = '<div class="mw-setup"><div class="mw-card"><h1>Making the wiki</h1><p>It\'s already being made.</p></div></div>'; return; }
      el.innerHTML = '<div class="boot">Loading…</div>';
      ensureTools().then(ok => (ok ? screenSetup(el, { update: true }) : card(el, '<h1>The setup couldn\'t be loaded</h1><p>The scripts in the lib folder next to this page are missing.</p>')));
    },
    // The page again, once its data was made where it loads it from (a test can stop it to look).
    reload: () => location.reload(),
    packsChanged,
    probePackage,
    parseListing,
    check,
    inZip,
  };
  root.MabiWikiBoot = Boot;

  async function boot() {
    if (root.GAME_DATA) { Boot.source = 'files'; runApp(); return; }
    const app = document.getElementById('app');
    if (HOSTABLE) {
      // A copy for hosting without its data: visitors of a web host get a note; opened from disk,
      // the setup makes the data into this folder.
      if (!Boot.canUpdate) {
        card(app, '<h1>This wiki isn\'t ready yet</h1><p>This copy of the wiki has no data yet: whoever hosts it needs to set it up from the game\'s files and upload its data folder.</p>');
        return;
      }
      if (!await ensureTools()) { card(app, '<h1>The setup couldn\'t be loaded</h1><p>The scripts in the lib folder next to this page are missing.</p>'); return; }
      screenSetup(app, {});
      return;
    }
    if (await fromBrowser()) { runApp(); return; }
    if (!root.MabiPacks || !root.MabiWikiPrepare || !root.MabiWikiSinks) { runApp(); return; } // no preparer: the wiki's own "no data" home
    screenSetup(app, {});
  }
  boot();
})();
