/*
 * make-standalone.js: make-standalone.html's code. It makes MabiWiki_Standalone.html, the wiki as
 * one file, from this folder (MabiWiki_Hostable), in the browser, so it works on any system.
 *
 * What it does to index.html (build() below; make-standalone.ps1, which Kanan's build runs, does the
 * same, so both make the same file byte for byte: keep the two the same):
 *   - MABIWIKI_BASE "./" with MABIWIKI_HOSTED becomes MABIWIKI_BASE "MabiWiki/" (the page's data is
 *     in MabiWiki/data/ next to it), and data/site.js becomes data/build-info.js (what made the
 *     data, written by the setup);
 *   - every "data/" script points into "MabiWiki/data/";
 *   - app.css goes in as a <style>, sources.js as a script, datasource.js and app.js as
 *     type="text/plain" data-run blocks (boot.js runs them once the data is in place);
 *   - lib/boot.js's tag becomes the 3D libraries as type="text/plain" id="lib-NAME" blocks
 *     (viewer3d.js, region3d.js, three.js and its license: loaded when a 3D view opens), then
 *     packs.js, images.js, sinks.js, prepare.js and boot.js, boot.js last.
 * "</script" in any file becomes "<\/script", so no block closes early. Only the files above are
 * read: data/ (the game's data, and models) never goes in. A file added to the wiki needs a line
 * here (and in make-standalone.ps1), and a tag in index.html written the way the others are.
 */
(function () {
  'use strict';

  // ---------- making the page (also used by the wiki's tests) ----------
  // A file's text as a script block that can't close early. "<!--" then "<script" (with no "-->"
  // between) puts the HTML parser in its "double escaped" state, where the real </script> no longer
  // closes the block: follow the parser's states and refuse such a file.
  function block(text, openTag) {
    text = text.replace(/<\/(?=script)/gi, '<\\/');
    let state = 'data';
    for (const m of text.matchAll(/<!--|-->|<script[\s/>]/gi)) {
      const s = m[0];
      if (s === '-->') state = 'data';
      else if (s === '<!--') { if (state === 'data') state = 'escaped'; }
      else if (state === 'escaped') state = 'double';
    }
    if (state === 'double') throw new Error('A file ends inside "<!-- <script", so its block would not close.');
    return openTag + '\n' + text + '\n</script>';
  }

  // read(path) gives a file of this folder as text ("lib/boot.js"); the page comes back as text.
  async function build(read) {
    let page = await read('index.html');
    const replaceOne = (oldText, newText) => {
      const i = page.indexOf(oldText);
      if (i < 0) throw new Error('index.html has no ' + oldText);
      page = page.slice(0, i) + newText + page.slice(i + oldText.length);
    };
    const plain = name => '<script type="text/plain" id="lib-' + name + '">';
    const run = name => '<script type="text/plain" data-run id="src-' + name + '">';
    const code = name => '<script id="src-' + name + '">';
    replaceOne('<script>window.MABIWIKI_BASE = "./"; window.MABIWIKI_HOSTED = true;</script>', '<script>window.MABIWIKI_BASE = "MabiWiki/";</script>');
    replaceOne('<script src="data/site.js"></script>', '<script src="data/build-info.js"></script>');
    page = page.split('<script src="data/').join('<script src="MabiWiki/data/');
    // Last tag first, so a tag's text in code already put in can't be taken for the tag.
    replaceOne('<script src="lib/boot.js"></script>', [
      block(await read('lib/viewer3d.js'), plain('viewer3d')),
      block(await read('lib/region3d.js'), plain('region3d')),
      block(await read('lib/three-bundle.min.js'), plain('three')),
      block(await read('lib/three-LICENSE.txt'), plain('three-license')),
      block(await read('lib/packs.js'), code('packs')),
      block(await read('lib/images.js'), code('images')),
      block(await read('lib/sinks.js'), code('sinks')),
      block(await read('lib/prepare.js'), code('prepare')),
      block(await read('lib/boot.js'), code('boot')),
    ].join('\n'));
    replaceOne('<script type="text/plain" data-run src="app.js"></script>', block(await read('app.js'), run('app')));
    replaceOne('<script type="text/plain" data-run src="datasource.js"></script>', block(await read('datasource.js'), run('datasource')));
    replaceOne('<script src="sources.js"></script>', block(await read('sources.js'), code('sources')));
    replaceOne('<link rel="stylesheet" href="app.css">', '<style id="src-app-css">\n' + await read('app.css') + '\n</style>');
    return page;
  }
  window.MabiWiki = { build };

  // ---------- the folder ----------
  const msg = document.getElementById('msg'), pick = document.getElementById('pick'), dir = document.getElementById('dir');
  const say = (text, bad) => { msg.textContent = text; msg.classList.toggle('bad', !!bad); };
  pick.addEventListener('click', () => dir.click());
  dir.addEventListener('change', async () => {
    const files = Array.from(dir.files || []);
    dir.value = '';
    if (!files.length) return;
    // Paths come as "ChosenFolder/lib/boot.js". The wiki is the folder with index.html and
    // lib/boot.js in it: the chosen folder itself, or one inside it.
    const byPath = new Map(files.map(f => [f.webkitRelativePath || f.name, f]));
    let base = null;
    for (const p of byPath.keys()) {
      if (!/(^|\/)index\.html$/.test(p)) continue;
      const b = p.slice(0, -'index.html'.length);
      if (byPath.has(b + 'lib/boot.js') && (base === null || b.length < base.length)) base = b;
    }
    if (base === null) { say('That folder doesn\'t have the wiki in it (index.html and lib/boot.js). Choose the MabiWiki_Hostable folder.', true); return; }
    pick.disabled = true;
    say('Making MabiWiki_Standalone.html…');
    try {
      const page = await build(async path => {
        const f = byPath.get(base + path);
        if (!f) throw new Error('The folder has no ' + path + '.');
        return f.text();
      });
      const url = URL.createObjectURL(new Blob([page], { type: 'text/html' }));
      const a = document.createElement('a');
      a.href = url; a.download = 'MabiWiki_Standalone.html';
      document.body.appendChild(a); a.click(); a.remove();
      setTimeout(() => URL.revokeObjectURL(url), 60000);
      say('Done: MabiWiki_Standalone.html is in your downloads (' + (page.length / 1048576).toFixed(1) + ' MB).');
    } catch (e) {
      say('It couldn\'t be made: ' + (e && e.message || e), true);
    } finally {
      pick.disabled = false;
    }
  });
})();
