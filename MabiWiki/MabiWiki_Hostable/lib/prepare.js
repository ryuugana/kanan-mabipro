/* Mabinogi Wiki: prepares the wiki's data from the game's packs, in the browser, the way Kanan does
 * when the game starts (MabiWiki.cpp prepare): the same files, the same folder layout and the same
 * stamps, so either one sees the other's work as current.
 *
 *   MabiWikiPrepare.run(packFiles, sink, options) -> Promise<build info>
 *
 * packFiles: the .pack Files (or options.packs: packs already open, MabiPacks.open). sink: one of
 * MabiWikiSinks (sinks.js); paths are relative to the MabiWiki folder. It writes, by category:
 *   data    data/game-data.js     window.GAME_DATA = {built, files: {name: text}, source: {name:
 *                                 {pack, size, version}}, version: 1}, keys sorted as Kanan's
 *   maps    data/maps.js, data/maps/<name>.jpg
 *   icons   data/item-icons.js, data/icons/items/*.png       (MabiImages, images.js)
 *   skills  data/skill-icons.js, data/icons/skills/*.png
 *   npcs    data/npc-images.js, data/icons/npcs/*.png, data/portraits/*.png
 * each with Kanan's stamp file (data/stamp.txt, maps-stamp.txt, icons-stamp.txt,
 * skillicons-stamp.txt, npcimages-stamp.txt), then data/build-info.js last:
 *   window.MABIWIKI_BUILD = {format, builder: 'page', built, packs: [{name, size, modified}],
 *     stamps: {category: Kanan's stamp}, complete, missing: [categories not there]}
 * build-info.js is also written right after the data (complete: false), so a build that stops
 * halfway leaves a text-only wiki that knows it isn't finished.
 *
 * options: only (the categories to make; default all), previous (the build info of what the sink
 * already holds, default window.MABIWIKI_BUILD: its stamps stand for the categories not made),
 * onProgress({ category, text, done, total, fraction }), images (default window.MabiImages:
 * without it the picture categories are left out), packs.
 *
 *   MabiWikiPrepare.stamps(packs) -> { data, icons, skills, npcs, maps }: Kanan's stamp strings
 *   MabiWikiPrepare.changed(packs, build) -> the categories whose stamp differs from build's
 *   MabiWikiPrepare.dataSource(packs) -> { names, source, stamp }: what game-data.js takes
 */
(function (root) {
  'use strict';

  const FORMAT = 1;
  const CATEGORIES = ['data', 'icons', 'skills', 'npcs', 'maps'];

  // The game's files the wiki reads (MabiWiki.cpp DB_FILES, TEXT_TABLES, TEXT_LOCALES; a test
  // checks they're the same). Text tables are local/xml/<table>.<locale>.txt, the first locale the
  // packs have.
  const DB_FILES = ['db/itemdb.xml', 'db/optionset.xml', 'db/title.xml', 'db/skill/skillinfo.xml', 'db/skill/skillleveldescription.xml', 'db/race.xml', 'db/collectingform.xml', 'db/itemupgradedb.xml', 'db/setitemdesc.xml', 'db/npcinfo.xml', 'db/charactercondition.xml',
    'db/pet/petracedesc.xml', 'db/pet/petagetable.xml', 'db/pet/petskilltable.xml', 'db/pet/petcolorset.xml', 'db/pet/petrebirthtable.xml',
    'db/vehicledesc.xml', 'db/aircraftdesc.xml', 'db/birdpetgiftitem.xml',
    'db/itemenhancecommon.xml',
    'db/egoweapon/egodesire.xml',
    'db/manualform.xml', 'db/production.xml', 'db/cookingaction.xml',
    'db/minimapinfo.xml',
    'db/ai/local/aidescdata_pet_bird.xml', 'db/ai/local/aidescdata_pet_bird_idle.xml', 'db/ai/local/aidescdata_pet_bordercollie.xml', 'db/ai/local/aidescdata_pet_cat.xml',
    'db/ai/local/aidescdata_pet_chicken.xml', 'db/ai/local/aidescdata_pet_cow.xml', 'db/ai/local/aidescdata_pet_crab.xml', 'db/ai/local/aidescdata_pet_dog.xml',
    'db/ai/local/aidescdata_pet_dog_m.xml', 'db/ai/local/aidescdata_pet_dragon.xml', 'db/ai/local/aidescdata_pet_elephant.xml', 'db/ai/local/aidescdata_pet_fox.xml',
    'db/ai/local/aidescdata_pet_griffin.xml', 'db/ai/local/aidescdata_pet_horse.xml', 'db/ai/local/aidescdata_pet_idle.xml', 'db/ai/local/aidescdata_pet_idle_bdog.xml',
    'db/ai/local/aidescdata_pet_idle_minibear.xml', 'db/ai/local/aidescdata_pet_idle_snake.xml', 'db/ai/local/aidescdata_pet_idle_spider.xml', 'db/ai/local/aidescdata_pet_imp.xml',
    'db/ai/local/aidescdata_pet_kangaroo_rat.xml', 'db/ai/local/aidescdata_pet_lazy_cat.xml', 'db/ai/local/aidescdata_pet_lizard.xml', 'db/ai/local/aidescdata_pet_mimic.xml',
    'db/ai/local/aidescdata_pet_minibear.xml', 'db/ai/local/aidescdata_pet_minighost.xml', 'db/ai/local/aidescdata_pet_miniglas.xml', 'db/ai/local/aidescdata_pet_miniskeleton.xml',
    'db/ai/local/aidescdata_pet_monkey.xml', 'db/ai/local/aidescdata_pet_music_bird.xml', 'db/ai/local/aidescdata_pet_music_ostrich.xml', 'db/ai/local/aidescdata_pet_ostrich.xml',
    'db/ai/local/aidescdata_pet_owl.xml', 'db/ai/local/aidescdata_pet_paladin_dog.xml', 'db/ai/local/aidescdata_pet_pig.xml', 'db/ai/local/aidescdata_pet_pinscher.xml',
    'db/ai/local/aidescdata_pet_pumpkin.xml', 'db/ai/local/aidescdata_pet_puppet.xml', 'db/ai/local/aidescdata_pet_puppetwc.xml', 'db/ai/local/aidescdata_pet_ratman.xml',
    'db/ai/local/aidescdata_pet_riding_bird.xml', 'db/ai/local/aidescdata_pet_riding_broom.xml', 'db/ai/local/aidescdata_pet_riding_tiger.xml', 'db/ai/local/aidescdata_pet_samoyed.xml',
    'db/ai/local/aidescdata_pet_seal.xml', 'db/ai/local/aidescdata_pet_sheep.xml', 'db/ai/local/aidescdata_pet_siren.xml', 'db/ai/local/aidescdata_pet_snake.xml',
    'db/ai/local/aidescdata_pet_spider.xml', 'db/ai/local/aidescdata_pet_sprite.xml', 'db/ai/local/aidescdata_pet_thunder_dog.xml', 'db/ai/local/aidescdata_pet_tiger.xml',
    'db/ai/local/aidescdata_pet_transform.xml', 'db/ai/local/aidescdata_pet_wolf.xml', 'db/ai/local/common_pet.xml'];
  const TEXT_TABLES = ['itemdb', 'optionset', 'title', 'skillinfo', 'skillleveldescription', 'race', 'itemupgradedb', 'setitemdesc', 'npcinfo', 'charactercondition', 'petcolorset', 'itemenhancecommon', 'manualform', 'production', 'cookingaction', 'minimapinfo'];
  const TEXT_LOCALES = ['english', 'usa', 'japan', 'korea'];

  // What each category writes: its list script, its stamp file and the folders it fills.
  const OUT = {
    data: { script: 'data/game-data.js', stamp: 'data/stamp.txt', folders: [] },
    icons: { script: 'data/item-icons.js', stamp: 'data/icons-stamp.txt', folders: ['data/icons/items'] },
    skills: { script: 'data/skill-icons.js', stamp: 'data/skillicons-stamp.txt', folders: ['data/icons/skills'] },
    npcs: { script: 'data/npc-images.js', stamp: 'data/npcimages-stamp.txt', folders: ['data/icons/npcs', 'data/portraits'] },
    maps: { script: 'data/maps.js', stamp: 'data/maps-stamp.txt', folders: ['data/maps'] },
  };
  // How long each takes, roughly, for the progress bar.
  const WEIGHT = { data: 8, maps: 4, icons: 45, skills: 3, npcs: 40 };
  const TEXT = { data: 'Reading the game\'s data…', maps: 'Copying the minimaps…', icons: 'Making the item icons…',
    skills: 'Making the skill icons…', npcs: 'Making the NPC faces and portraits…' };

  // Names compare by their bytes, as Kanan's std::map keeps them (they're Latin-1 here: one
  // character per byte).
  const cmp = (a, b) => (a < b ? -1 : a > b ? 1 : 0);
  const packName = (e) => e.file.name;

  // JSON as Kanan's (nlohmann) writes it: objects' keys sorted, no spaces.
  function sortedJson(v) {
    if (Array.isArray(v)) return '[' + v.map(sortedJson).join(',') + ']';
    if (v && typeof v === 'object') return '{' + Object.keys(v).sort(cmp).map((k) => JSON.stringify(k) + ':' + sortedJson(v[k])).join(',') + '}';
    return JSON.stringify(v === undefined ? null : v);
  }

  // Local time as Kanan writes it: 2026-10-21 14:03.
  function builtTime(t) {
    const p = (n) => String(n).padStart(2, '0');
    return t.getFullYear() + '-' + p(t.getMonth() + 1) + '-' + p(t.getDate()) + ' ' + p(t.getHours()) + ':' + p(t.getMinutes());
  }

  // ======================================================================================
  // Stamps (MabiWiki.cpp dataSource, iconStamp, skillIconStamp, npcImageStamp, mapsStamp)
  // ======================================================================================

  // The files game-data.js takes (DB_FILES, then the text tables the packs have), the source of
  // each ({pack, version, size}) and the data's stamp: name:pack:seed:size; for each.
  function dataSource(packs) {
    const names = [...DB_FILES];
    for (const t of TEXT_TABLES) {
      const n = TEXT_LOCALES.map((l) => 'local/xml/' + t + '.' + l + '.txt').find((x) => packs.has(x));
      if (n) names.push(n);
    }
    const source = {}, have = [];
    let stamp = '';
    for (const n of names) {
      const e = packs.entry(n);
      if (!e) continue;
      have.push(n);
      source[n] = { pack: packName(e), version: e.seed, size: e.size };
      stamp += n + ':' + packName(e) + ':' + e.seed + ':' + e.size + ';';
    }
    return { names: have, source, stamp };
  }

  // name:seed:size; for each name the test takes, in byte order.
  const listStamp = (packs, test) => packs.names().filter(test).map((n) => { const e = packs.entry(n); return n + ':' + e.seed + ':' + e.size + ';'; }).join('');

  const MAP_FOLDER = 'gfx/gui/map_jpg/';
  const isMapEntry = (n) => n.startsWith(MAP_FOLDER) && n.indexOf('/', MAP_FOLDER.length) < 0 && n.length > MAP_FOLDER.length + 4 && n.endsWith('.jpg');

  function stamps(packs) {
    // The item icons': "1;" count ":" the sum of seed * 31 + size (a 64-bit sum in Kanan).
    let sum = 0n, count = 0;
    for (const n of packs.names()) {
      if (n.startsWith('gfx/image/') || n.startsWith('color/') || n === 'db/colortable.data' || n === 'db/itemdb.xml') {
        const e = packs.entry(n);
        sum += BigInt(e.seed) * 31n + BigInt(e.size);
        count++;
      }
    }
    return {
      data: dataSource(packs).stamp,
      icons: '1;' + count + ':' + BigInt.asUintN(64, sum),
      skills: listStamp(packs, (n) => n === 'db/skill/skillinfo.xml' || n.startsWith('gfx/image/gui_icon_')),
      npcs: '1;' + listStamp(packs, (n) => n === 'db/npcinfo.xml' || n.startsWith('db/npc/') || n.startsWith('gfx/image/npcicon_') || n.startsWith('gfx/image/npc/')),
      maps: '1;' + listStamp(packs, isMapEntry),
    };
  }

  // The categories whose stamp differs from build's (a MABIWIKI_BUILD); all of them without one.
  function changed(packs, build) {
    const now = stamps(packs), was = (build && build.stamps) || {};
    return CATEGORIES.filter((k) => now[k] !== was[k]);
  }

  // ======================================================================================
  // game-data.js (MabiWiki.cpp writeData)
  // ======================================================================================
  // Each file's text goes into a Blob as soon as it's read (as JSON), so the 45 MB of text is never
  // held twice; the file is one Blob of those parts. Up to 8 files are read at once.
  async function writeData(packs, sink, built, progress) {
    const { source } = dataSource(packs);
    const names = Object.keys(source).sort(cmp);
    const parts = new Array(names.length);
    let next = 0, done = 0;
    const worker = async () => {
      while (next < names.length) {
        const i = next++, n = names[i];
        const d = await packs.read(n);
        if (!d) throw new Error('Couldn\'t read ' + n + ' from the game\'s files.');
        parts[i] = new Blob([(i ? ',' : '') + JSON.stringify(n) + ':' + JSON.stringify(root.MabiPacks.asText(d))]);
        progress(++done, names.length);
      }
    };
    await Promise.all(Array.from({ length: 8 }, worker));
    const src = sortedJson(source);
    await sink.write(OUT.data.script, new Blob(['window.GAME_DATA = {"built":' + JSON.stringify(built) + ',"files":{', ...parts, '},"source":', src, ',"version":1};\n'],
      { type: 'text/javascript' }));
    return names.length;
  }

  // ======================================================================================
  // Minimaps (MabiWiki.cpp makeMinimaps)
  // ======================================================================================
  // gfx/gui/map_jpg/<name>[_eng|_jpn|_kor|_chn|_twn].jpg, copied as they are to data/maps/<name>.jpg:
  // the English one where a map has several, else the one with no language, else any.

  function mapBase(entry) {
    let base = entry.slice(MAP_FOLDER.length, -4), rank = 1;
    for (const [suffix, r] of [['_eng', 0], ['_jpn', 2], ['_kor', 3], ['_chn', 3], ['_twn', 3]]) {
      if (base.length > suffix.length && base.endsWith(suffix)) { base = base.slice(0, -suffix.length); rank = r; break; }
    }
    return { base, rank };
  }
  // A name as a file name: lowercase letters, digits, "_", "-" and "."; anything else is "_".
  const fileName = (s) => s.replace(/[A-Z]/g, (c) => c.toLowerCase()).replace(/[^a-z0-9_.\-]/g, '_');
  // Nothing that leaves the folder or names something else.
  const safeName = (s) => !!s && !s.includes('..') && !/[\x00-\x1f/\\:*?"<>|]/.test(s);

  async function writeMaps(packs, sink, progress) {
    const best = new Map();
    for (const n of packs.names()) {
      if (!isMapEntry(n)) continue;
      const { base, rank } = mapBase(n), b = fileName(base);
      const old = best.get(b);
      if (!old || rank < old.rank) best.set(b, { rank, name: n });
    }
    const bases = [...best.keys()].sort(cmp), ok = new Array(bases.length).fill(false);
    let next = 0, done = 0;
    const worker = async () => {
      while (next < bases.length) {
        const i = next++, base = bases[i];
        const d = safeName(base) ? await packs.read(best.get(base).name) : null;
        if (d && d.length >= 4 && d[0] === 0xFF && d[1] === 0xD8) { // a JPEG
          await sink.write('data/maps/' + base + '.jpg', d);
          ok[i] = true;
        }
        progress(++done, bases.length);
      }
    };
    await Promise.all(Array.from({ length: 4 }, worker));
    const names = bases.filter((b, i) => ok[i]);
    await sink.write(OUT.maps.script, 'window.GAME_MAPS = ' + JSON.stringify(names) + ';\n');
    return names.length;
  }

  // ======================================================================================
  // The pictures (images.js: MabiImages), and their lists as Kanan writes them
  // ======================================================================================
  const PICTURES = {
    icons: { make: 'itemIcons', global: 'GAME_ICONS', count: (r) => r.count },
    skills: { make: 'skillIcons', global: 'GAME_SKILL_ICONS', count: (r) => r.count },
    npcs: { make: 'npcImages', global: 'GAME_NPC_IMAGES', count: (r) => (r.icons || 0) + (r.portraits || 0) || r.count },
  };

  async function writePictures(category, images, packs, sink, progress) {
    const P = PICTURES[category];
    // images.js writes paths below data/.
    const below = { write: (path, data) => sink.write('data/' + path, data) };
    const r = await images[P.make](packs, below, (done, total) => progress(done, total));
    if (!r || !r.map) throw new Error('No pictures were made.');
    await sink.write(OUT[category].script, 'window.' + P.global + ' = ' + sortedJson(r.map) + ';\n');
    const n = P.count(r);
    return n === undefined ? Object.keys(r.map).length : n;
  }

  // ======================================================================================
  // run
  // ======================================================================================

  async function run(packFiles, sink, options) {
    options = options || {};
    const started = Date.now();
    const onProgress = options.onProgress || (() => {});
    const images = options.images !== undefined ? options.images : root.MabiImages;
    const only = (options.only || CATEGORIES).filter((k) => CATEGORIES.includes(k));
    const previous = options.previous !== undefined ? options.previous : options.only ? root.MABIWIKI_BUILD : null;
    const times = {};

    onProgress({ category: 'index', text: 'Reading the game\'s files…', done: 0, total: 0, fraction: 0 });
    let t = Date.now();
    const packs = options.packs || await root.MabiPacks.open(packFiles);
    times.index = Date.now() - t;
    if (!packs.has('db/itemdb.xml')) throw new Error('These aren\'t Mabinogi\'s game files (no item list in them).');

    const now = stamps(packs);
    const built = builtTime(new Date());
    // The order: the data first (the wiki works with it alone), the minimaps, then the pictures.
    const order = ['data', 'maps', 'icons', 'skills', 'npcs'].filter((k) => only.includes(k));
    const missing = !images ? order.filter((k) => PICTURES[k]) : [];
    const todo = order.filter((k) => !missing.includes(k));
    const total = todo.reduce((s, k) => s + WEIGHT[k], 0) || 1;
    let before = 0;

    const kept = {};
    if (previous && previous.stamps) for (const k of CATEGORIES) if (!only.includes(k) && previous.stamps[k] !== undefined) kept[k] = previous.stamps[k];
    const made = {}, counts = {}, failed = {};
    const info = (complete) => {
      const st = Object.assign({}, kept, made);
      return {
        format: FORMAT,
        builder: 'page',
        built,
        packs: (packs.files || []).map((f) => ({ name: f.name, size: f.size, modified: f.lastModified || 0 })),
        stamps: st,
        complete: complete && CATEGORIES.every((k) => st[k] !== undefined),
        missing: CATEGORIES.filter((k) => st[k] === undefined),
      };
    };
    const writeInfo = (i) => sink.write('data/build-info.js', 'window.MABIWIKI_BUILD = ' + JSON.stringify(i) + ';\n');

    for (const k of todo) {
      const label = TEXT[k];
      const progress = (done, n) => onProgress({ category: k, text: label, done, total: n, fraction: (before + WEIGHT[k] * (n ? done / n : 0)) / total });
      progress(0, 0);
      t = Date.now();
      let count = 0;
      try {
        for (const f of OUT[k].folders) await sink.clear(f);
        if (k === 'data') count = await writeData(packs, sink, built, progress);
        else if (k === 'maps') count = await writeMaps(packs, sink, progress);
        else count = await writePictures(k, images, packs, sink, progress);
        await sink.flush();
      } catch (e) {
        if (k === 'data') throw e; // as in Kanan, nothing else is worth making without it
        failed[k] = String((e && e.message) || e);
        if (root.console) console.error('[MabiWikiPrepare]', k, e);
      }
      // As Kanan: the stamp only when something was made, so the next run tries again.
      if (count > 0 && !failed[k]) {
        await sink.write(OUT[k].stamp, now[k]);
        made[k] = now[k];
      }
      counts[k] = count;
      times[k] = Date.now() - t;
      before += WEIGHT[k];
      if (k === 'data' && sink.rewrites !== false) { await writeInfo(info(false)); await sink.flush(); }
    }

    const result = info(true);
    await writeInfo(result);
    await sink.flush();
    if (sink.setMeta) await sink.setMeta(result);
    onProgress({ category: 'done', text: 'Done.', done: 1, total: 1, fraction: 1 });
    // Not in build-info.js: how this run went.
    Object.defineProperty(result, 'run', { value: { counts, failed, times, ms: Date.now() - started, packs }, enumerable: false });
    return result;
  }

  root.MabiWikiPrepare = { FORMAT, CATEGORIES, DB_FILES, TEXT_TABLES, TEXT_LOCALES, OUT, stamps, changed, dataSource, run, sortedJson, builtTime, mapBase, fileName };
})(typeof window !== 'undefined' ? window : globalThis);
