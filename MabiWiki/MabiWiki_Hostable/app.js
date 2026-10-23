// The wiki app: one generic renderer for whatever a data source gives it (meta, the lists of
// each type, pages, a search index). The data source is pluggable: by default the built JSON files (meta.json, search.json, index/<type>.json,
// pages/<type>/<id>.json); a page can bring its own as window.WIKI_SOURCE (the Mabinogi wiki
// builds its pages in the browser from the game's files, datasource.js). The wiki's name and its
// home page text come from the meta (name, tagline, badge, note).
// Hash routing: #/  #/<route>?cat=&q=&p=&s=&d=&v=&f_<key>=  #/<type>/<id>  #/search?q=. No dependencies.
//
// Everything below the basics is optional, so a data source that doesn't know a field still
// renders. The optional fields (all read defensively):
//   meta.home = {try: [link], featured: [group] | [tile], featuredTitle, panels: [{title, text, blocks}],
//     panelsTitle, searchHint, footnote}; a group is {id, title, icon, tiles: [tile]} (a heading
//     over its own grid; a flat [tile] still renders as one grid); a tile is {label, href |
//     t (+cat, f, q) | t + id, icon (image url) | glyph (icon name), count | sub}.
//   meta.sections = [{id, title, icon, types, count}]: the sidebar's headings, each collapsible
//     (remembered per section; the one holding the current page is always open).
//   meta.built = 'YYYY-MM-DD HH:MM' (shown under the sidebar as "Data from ..."), builtNote (its tooltip).
//   meta.types[t].catColumns = {catKey: [column]} and catFilters = {catKey: [filter]}: the nearest
//     ancestor of the listed category with an entry wins over columns / filters.
//   column: {key, title, num, sort: '<numeric key to sort by>', bar: true (range bar from
//     <key>Min / <key>Max), meter: ['lowest label', ..., 'highest label'], card: true (shown on
//     grid cards, at most 2), short: 'card label' ('' for none)}.
//   filter: {key, title, options, any, none, hint} (a dropdown) or {kind: 'min', key, title}
//     (a "≥ number" box).
//   page: {stats: [[label, value, small?]], more: {label, href | t (+cat)}, kind: 'type label',
//     capTables: true (every table scrolls in a short box)};
//     a section {heading, empty: 'text' | {title, text}} keeps a slot with an empty state;
//     a section with wide: true goes below the article and infobox, across the whole page.
//   values: {text, tone: 'gold'}, {text, tag: 'Hidden'}; links {t, id, label, icon, mark, sub}
//     ({mark} draws a rank badge instead of the icon) and list links {t, cat?, f?: {key: value},
//     q?, label} or {href, label}.
//   links block: style 'chips' (default) | 'list' | 'ranks' | 'rows'; items may carry count, tag,
//     glyph. ranks block: groups [[label, fromIndex, toIndex]], short [labels]. embed: caption.
//   tabs block: {label, tabs: [{label, blocks, count, locked}], start, remember: 'key', style}
//     (a switch between sets of blocks; with remember the pick is kept across pages). style
//     'line': an underlined strip (a shop's tabs) instead of the pill bar, with each tab's count
//     and a lock on a tab that only shows under a condition; the label is for screen readers only.
(() => {
  'use strict';

  // ---------- helpers ----------
  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => Array.from(el.querySelectorAll(s));
  const ESC = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };
  const esc = s => String(s == null ? '' : s).replace(/[&<>"']/g, c => ESC[c]);
  const nf = new Intl.NumberFormat('en-US', { maximumFractionDigits: 2 });
  const fmtNum = n => (Number.isFinite(n) ? nf.format(n) : String(n));
  const store = {
    get(k, d) { try { const v = localStorage.getItem('wiki-' + k); return v == null ? d : JSON.parse(v); } catch (e) { return d; } },
    set(k, v) { try { localStorage.setItem('wiki-' + k, JSON.stringify(v)); } catch (e) { /* private mode */ } },
  };
  const fetchJSON = url => fetch(url).then(r => { if (!r.ok) throw new Error(r.status + ' ' + url); return r.json(); });
  const safeId = id => String(id).replace(/[^\p{L}\p{N}\-_.]/gu, '_');
  const debounce = (fn, ms) => { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; };

  // ---------- data source ----------
  // {meta(), index(type), page(type, id), search()}, each a promise. index rows: {id, name, alt,
  // icon, cat, sub, tags, ...columns}; search rows: [type, id, name, alt, icon, sub, tags].
  const SRC = window.WIKI_SOURCE || {
    meta: () => fetchJSON('meta.json'),
    index: t => fetchJSON('index/' + encodeURIComponent(t) + '.json'),
    page: (t, id) => fetchJSON(`pages/${encodeURIComponent(t)}/${encodeURIComponent(safeId(id))}.json`),
    search: () => fetchJSON('search.json'),
  };
  const wikiName = () => (S.meta && S.meta.name) || 'Wiki';

  // ---------- icons (inline SVG, stroke style) ----------
  const P = {
    home: '<path d="M3 10.5 12 3l9 7.5"/><path d="M5 9.5V21h14V9.5"/><path d="M10 21v-6h4v6"/>',
    search: '<circle cx="11" cy="11" r="7"/><path d="m20 20-3.5-3.5"/>',
    bag: '<path d="M5 8h14l-1 13H6L5 8z"/><path d="M9 8V6a3 3 0 0 1 6 0v2"/>',
    sword: '<path d="M14.5 17.5 3 6V3h3l11.5 11.5"/><path d="m13 19 6-6"/><path d="m16 16 4 4"/><path d="m19 21 2-2"/>',
    hammer: '<path d="m14 10-9.5 9.5a2.1 2.1 0 0 1-3-3L11 7"/><path d="M12.5 5.5 15 3l6 6-2.5 2.5z"/><path d="m11 7 3 3"/>',
    shop: '<path d="M3 9 4.5 4h15L21 9"/><path d="M3 9h18v1.5a3 3 0 0 1-6 0 3 3 0 0 1-6 0 3 3 0 0 1-6 0V9z"/><path d="M5 13.5V21h14v-7.5"/><path d="M10 21v-4h4v4"/>',
    book: '<path d="M4 19.5A2.5 2.5 0 0 1 6.5 17H20V3H6.5A2.5 2.5 0 0 0 4 5.5v14z"/><path d="M4 19.5A2.5 2.5 0 0 0 6.5 22H20v-5"/>',
    skull: '<circle cx="9" cy="12" r="1"/><circle cx="15" cy="12" r="1"/><path d="M8 20v2h8v-2"/><path d="M16 20a2 2 0 0 0 1.56-3.25 8 8 0 1 0-11.12 0A2 2 0 0 0 8 20"/>',
    map: '<path d="M9 4 3 6v14l6-2 6 2 6-2V4l-6 2-6-2z"/><path d="M9 4v14M15 6v14"/>',
    pin: '<path d="M12 21s-7-6.2-7-11.5a7 7 0 0 1 14 0C19 14.8 12 21 12 21z"/><circle cx="12" cy="9.5" r="2.5"/>',
    user: '<circle cx="12" cy="8" r="4"/><path d="M4 21a8 8 0 0 1 16 0"/>',
    users: '<circle cx="9" cy="8" r="3.5"/><path d="M2.5 20a6.5 6.5 0 0 1 13 0"/><path d="M16 4.5a3.5 3.5 0 0 1 0 7M18 14a6.5 6.5 0 0 1 3.5 6"/>',
    scroll: '<path d="M8 21h11a2 2 0 0 0 2-2v-2H10v2a2 2 0 1 1-4 0V5a2 2 0 0 0-2-2 2 2 0 0 0-2 2v3h4"/><path d="M19 17V5a2 2 0 0 0-2-2H4"/>',
    star: '<path d="m12 3 2.8 5.7 6.2.9-4.5 4.4 1 6.2L12 17.3l-5.5 2.9 1-6.2L3 9.6l6.2-.9L12 3z"/>',
    award: '<circle cx="12" cy="9" r="6"/><path d="M8.5 14 7 22l5-3 5 3-1.5-8"/>',
    trophy: '<path d="M8 21h8M12 17v4M7 4h10v5a5 5 0 0 1-10 0V4z"/><path d="M17 5h3v2a3 3 0 0 1-3 3M7 5H4v2a3 3 0 0 0 3 3"/>',
    zap: '<path d="M13 2 3 14h9l-1 8 10-12h-9l1-8z"/>',
    leaf: '<path d="M11 20A7 7 0 0 1 4 13c0-6 7-10 16-10 0 9-4 16-9 17z"/><path d="M4 21c4-6 8-9 12-11"/>',
    castle: '<path d="M4 21V8l4-2v3l4-2 4 2V6l4 2v13z"/><path d="M10 21v-5a2 2 0 0 1 4 0v5"/>',
    heart: '<path d="M20.8 4.6a5.5 5.5 0 0 0-7.8 0L12 5.7l-1-1.1a5.5 5.5 0 0 0-7.8 7.8L12 21.2l8.8-8.8a5.5 5.5 0 0 0 0-7.8z"/>',
    pot: '<path d="M4 11h16v3a6 6 0 0 1-6 6h-4a6 6 0 0 1-6-6v-3z"/><path d="M2 11h20M9 7c0-1 1-1.5 1-3M14 7c0-1 1-1.5 1-3"/>',
    flask: '<path d="M9 3h6M10 3v6L4.5 18.5A2 2 0 0 0 6.3 21h11.4a2 2 0 0 0 1.8-2.5L14 9V3"/><path d="M7 15h10"/>',
    music: '<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>',
    shirt: '<path d="M8 3 3 6l2 5 3-1v11h8V10l3 1 2-5-5-3a4 4 0 0 1-8 0z"/>',
    gem: '<path d="M6 3h12l4 6-10 12L2 9z"/><path d="M2 9h20M12 21 8 9l4-6 4 6z"/>',
    paw: '<circle cx="5" cy="10" r="2"/><circle cx="9" cy="5.5" r="2"/><circle cx="15" cy="5.5" r="2"/><circle cx="19" cy="10" r="2"/><path d="M12 12c-3 0-6 4-6 6.5S8 21 12 19.5c4 1.5 6 .5 6-1S15 12 12 12z"/>',
    fish: '<path d="M6.5 12c2-4 6-6 10-5 2 .5 3.5 2.5 4.5 5-1 2.5-2.5 4.5-4.5 5-4 1-8-1-10-5z"/><path d="M6.5 12 2 8v8z"/><path d="M16 11h.01"/>',
    flag: '<path d="M4 22V4M4 4h13l-2 4 2 4H4"/>',
    globe: '<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3a14 14 0 0 1 0 18M12 3a14 14 0 0 0 0 18"/>',
    box: '<path d="M21 8 12 3 3 8v8l9 5 9-5V8z"/><path d="m3 8 9 5 9-5M12 13v8"/>',
    cube: '<path d="M21 8 12 3 3 8v8l9 5 9-5V8z"/><path d="m3 8 9 5 9-5M12 13v8"/>',
    tag: '<path d="M20.6 13.4 13.4 20.6a2 2 0 0 1-2.8 0L3 13V3h10l7.6 7.6a2 2 0 0 1 0 2.8z"/><circle cx="7.5" cy="7.5" r="1.5"/>',
    menu: '<path d="M4 6h16M4 12h16M4 18h16"/>',
    chev: '<path d="m9 6 6 6-6 6"/>',
    down: '<path d="m6 9 6 6 6-6"/>',
    grid: '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
    list: '<path d="M8 6h13M8 12h13M8 18h13M3.5 6h.01M3.5 12h.01M3.5 18h.01"/>',
    info: '<circle cx="12" cy="12" r="9"/><path d="M12 16v-4.5M12 8h.01"/>',
    warn: '<path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><path d="M12 9v4M12 17h.01"/>',
    shuffle: '<path d="M16 3h5v5M4 20 21 3M21 16v5h-5M15 15l6 6M4 4l5 5"/>',
    sun: '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/>',
    moon: '<path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/>',
    spark: '<path d="M12 3l1.9 5.1L19 10l-5.1 1.9L12 17l-1.9-5.1L5 10l5.1-1.9z"/><path d="M19 15l.8 2.2L22 18l-2.2.8L19 21l-.8-2.2L16 18l2.2-.8z"/>',
    clock: '<circle cx="12" cy="12" r="9"/><path d="M12 7v5l3 2"/>',
    hash: '<path d="M4 9h16M4 15h16M10 3 8 21M16 3l-2 18"/>',
    door: '<path d="M5 21V4a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1v17"/><path d="M3 21h18"/><path d="M15 12h.01"/>',
    crown: '<path d="M3 8l4.5 4L12 5l4.5 7L21 8l-2 11H5z"/>',
    person: '<circle cx="12" cy="6" r="3"/><path d="M12 9v7M8 12h8M9 22l3-6 3 6"/>',
    pulse: '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
    dot: '<circle cx="12" cy="12" r="3"/>',
    x: '<path d="M6 6l12 12M18 6 6 18"/>',
    check: '<path d="m5 12.5 4.5 4.5L19 7"/>',
    anvil: '<path d="M4 7h11a5 5 0 0 0 5 3v1h-6l-1 3h2v3H6v-3h2l-1-3H4z"/>',
    arrow: '<path d="M5 12h14M13 6l6 6-6 6"/>',
    sliders: '<path d="M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12"/><circle cx="16" cy="6" r="2"/><circle cx="10" cy="12" r="2"/><circle cx="18" cy="18" r="2"/>',
    expand: '<path d="M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7"/>',
    lock: '<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V7.5a4 4 0 0 1 8 0V11"/>',
  };
  const ALIAS = {
    item: 'bag', items: 'bag', weapon: 'sword', monster: 'skull', monsters: 'skull', npc: 'user', npcs: 'user',
    region: 'map', regions: 'map', maps: 'map', dungeon: 'castle', dungeons: 'castle', skill: 'zap', skills: 'zap',
    quest: 'scroll', quests: 'scroll', title: 'award', titles: 'award', achievement: 'trophy', achievements: 'trophy',
    recipe: 'hammer', recipes: 'hammer', crafting: 'hammer', craft: 'hammer', shop: 'shop', shops: 'shop', store: 'shop',
    race: 'users', races: 'users', arcana: 'spark', condition: 'heart', conditions: 'heart', gathering: 'leaf',
    cooking: 'pot', food: 'pot', alchemy: 'flask', instrument: 'music', clothes: 'shirt', armor: 'shirt', pet: 'paw',
    world: 'globe', place: 'pin', location: 'pin', prop: 'box', category: 'tag', enchant: 'scroll', enchants: 'scroll',
  };
  const svg = (name, cls = 'i') => `<svg class="${cls}" viewBox="0 0 24 24" aria-hidden="true">${P[name] || P[ALIAS[name]] || P.dot}</svg>`;
  const typeIcon = t => { const d = S.meta && S.meta.types[t]; return (d && d.icon) || t; };

  const ORN = '<div class="orn" role="presentation"><svg viewBox="0 0 46 14" aria-hidden="true"><path d="M23 1.5 28.5 7 23 12.5 17.5 7z"/><path d="M23 4.5 25.5 7 23 9.5 20.5 7z"/><circle cx="11" cy="7" r="1.3"/><circle cx="35" cy="7" r="1.3"/><circle cx="4" cy="7" r=".9"/><circle cx="42" cy="7" r=".9"/></svg></div>';

  // ---------- state ----------
  const S = {
    meta: null, idx: {}, idxP: {}, pages: new Map(), search: null, searchP: null,
    byRoute: {}, catT: {}, catKids: {}, typeSection: {},
  };

  function loadMeta() {
    return SRC.meta().then(m => {
      S.meta = m;
      m.types = m.types || {};
      m.sections = m.sections || [];
      for (const [t, d] of Object.entries(m.types)) {
        S.byRoute[d.route || t + 's'] = t;
        S.byRoute[t] = S.byRoute[t] || t;
      }
      for (const s of m.sections) for (const t of s.types || []) S.typeSection[t] = s;
      for (const [t, rows] of Object.entries(m.categories || {})) {
        const byKey = {}, kids = {};
        for (const c of rows) byKey[c.key] = c;
        for (const c of rows) {
          const p = c.parent && byKey[c.parent] ? c.parent : '';
          (kids[p] = kids[p] || []).push(c);
        }
        // A category's subcategories in A-Z order (the sidebar tree and a list page's bubbles), "Other..." last.
        for (const list of Object.values(kids)) list.sort(byTitle);
        S.catT[t] = byKey; S.catKids[t] = kids;
      }
    });
  }
  function typeTitle(t) { const d = S.meta.types[t]; return d ? d.title : t; }
  // A-Z by title, numbers in order ("Rank 2" before "Rank 10"); one marked first (meta: first: true, e.g.
  // the items' Equipment) before the rest, the catch-alls ("Other weapons", "Misc", "Uncategorized",
  // "No set town") last.
  function byTitle(a, b) {
    const last = x => (x.first ? -1 : /^(other|misc|uncategori[sz]ed|no )/i.test(String(x.title || '')) ? 1 : 0);
    return last(a) - last(b) || String(a.title || '').localeCompare(String(b.title || ''), 'en', { numeric: true, sensitivity: 'base' });
  }
  function typeRoute(t) { const d = S.meta.types[t]; return d ? (d.route || t + 's') : t + 's'; }
  function singular(t) { return t.replace(/_/g, ' ').replace(/^./, c => c.toUpperCase()); }
  // One of a type, from its title: "NPCs" -> "NPC", "Set effects" -> "Set effect", "Abilities" -> "Ability".
  function oneOf(t) {
    const s = String(typeTitle(t) || singular(t));
    return /ies$/.test(s) ? s.slice(0, -3) + 'y' : /[^s]s$/.test(s) ? s.slice(0, -1) : s;
  }
  const href = (t, id) => `#/${encodeURIComponent(t)}/${encodeURIComponent(id)}`;
  function listHref(t, params) {
    const q = new URLSearchParams();
    for (const [k, v] of Object.entries(params || {})) if (v !== undefined && v !== null && v !== '') q.set(k, v);
    const s = q.toString().replace(/%2F/g, '/');
    return '#/' + typeRoute(t) + (s ? '?' + s : '');
  }

  function loadIndex(t) {
    if (S.idx[t]) return Promise.resolve(S.idx[t]);
    if (!S.idxP[t]) {
      S.idxP[t] = SRC.index(t).catch(() => []).then(rows => {
        for (const r of rows) {
          const tags = Array.isArray(r.tags) ? r.tags.join(' ') : (r.tags || '');
          r._h = [r.name, r.alt, r.id, tags, r.sub, r.fx].filter(Boolean).join(' \u0001 ').toLowerCase();
          r._c = Array.isArray(r.cat) ? r.cat : (r.cat ? [r.cat] : []);
        }
        S.idx[t] = rows;
        return rows;
      });
    }
    return S.idxP[t];
  }
  function loadPage(t, id) {
    const k = t + '\t' + id;
    if (S.pages.has(k)) return S.pages.get(k);
    const p = SRC.page(t, id);
    S.pages.set(k, p);
    p.catch(() => S.pages.delete(k));
    return p;
  }
  function loadSearch() {
    if (S.search) return Promise.resolve(S.search);
    if (!S.searchP) {
      S.searchP = SRC.search().catch(() => []).then(rows => {
        // A row's effects (r[7]: an enchant's) are shown beside it and searched with its tags.
        S.search = rows.map(r => ({
          t: r[0], id: String(r[1]), name: r[2] || '', alt: r[3] || '', icon: r[4] || null, sub: r[5] || '', tags: r[6] || '', fx: r[7] || '',
          n: (r[2] || '').toLowerCase(), a: (r[3] || '').toLowerCase(), g: ((r[6] || '') + ' ' + (r[5] || '') + ' ' + (r[7] || '')).toLowerCase(),
        }));
        return S.search;
      });
    }
    return S.searchP;
  }

  // ---------- pixel icons: whole-number scaling of the classic art ----------
  // Item art sits on its inventory footprint (.foot): the wrapper takes the icon's size and draws
  // a grid of 24px cells times the scale, so a 1x4 sword shows 4 slots. Only at whole-number
  // scales of 24px-multiple icons.
  function fitPix(img) {
    const box = +img.dataset.box, w = img.naturalWidth, h = img.naturalHeight;
    if (!box || !w || !h) return;
    // A portrait standing in for a face (an NPC with no small face): a square of the box, cropped to
    // the head at the top.
    if (img.classList.contains('face')) {
      img.style.width = img.style.height = box + 'px';
      img.style.objectFit = 'cover'; img.style.objectPosition = '50% 8%';
      return;
    }
    let s = Math.min(box / w, box / h);
    // Item and skill icons are pixel art: whole steps up. The rest (portraits...) are
    // painted: fit smoothly, enlarged at most 2x.
    if (img.classList.contains('pix')) { if (s >= 1) s = Math.floor(s); }
    else s = Math.min(s, 2);
    img.style.width = Math.round(w * s) + 'px';
    img.style.height = Math.round(h * s) + 'px';
    img.style.maxWidth = 'none'; img.style.maxHeight = 'none';
    const f = img.parentElement;
    if (f && f.classList.contains('foot')) {
      const cells = s >= 1 && w % 24 === 0 && h % 24 === 0;
      f.classList.toggle('g', cells);
      if (cells) f.style.setProperty('--cell', 24 * s + 'px');
      const sz = cells && f.parentElement ? f.parentElement.querySelector(':scope > .sz') : null;
      if (sz) sz.textContent = `${w / 24}×${h / 24} slots`;
    }
  }
  document.addEventListener('load', e => { const t = e.target; if (t.tagName === 'IMG' && t.dataset.box) fitPix(t); }, true);
  document.addEventListener('error', e => {
    const t = e.target;
    if (t.tagName === 'IMG' && t.dataset.box) {
      const b = t.closest('.ibox');
      (t.parentElement && t.parentElement.classList.contains('foot') ? t.parentElement : t).remove();
      if (b) { b.classList.add('none'); b.classList.remove('skill'); b.innerHTML = svg('box'); }
    }
  }, true);
  function fitAll(root) { for (const img of $$('img[data-box]', root)) if (img.complete && img.naturalWidth) fitPix(img); }

  // Click an ID (anything with data-copy) to copy it. Inside a link (a grid
  // card) the click copies instead of opening the page.
  function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) return navigator.clipboard.writeText(text);
    // Over plain http from another machine there's no clipboard API.
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.setAttribute('readonly', '');
    ta.style.cssText = 'position:fixed;top:-1000px;opacity:0';
    document.body.appendChild(ta);
    ta.select();
    let ok = false;
    try { ok = document.execCommand('copy'); } catch (e) { ok = false; }
    ta.remove();
    return ok ? Promise.resolve() : Promise.reject(new Error('copy failed'));
  }
  let toastTimer = 0;
  function toast(msg) {
    let el = $('#toast');
    if (!el) { el = document.createElement('div'); el.id = 'toast'; el.className = 'toast'; el.setAttribute('role', 'status'); document.body.appendChild(el); }
    el.textContent = msg;
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => el.classList.remove('show'), 1400);
  }
  document.addEventListener('click', e => {
    const t = e.target.closest && e.target.closest('[data-copy]');
    if (!t) return;
    e.preventDefault();
    e.stopPropagation();
    const v = t.dataset.copy;
    copyText(v).then(() => toast('Copied ' + v), () => toast('Couldn’t copy ' + v));
  }, true);
  const copyAttr = v => `data-copy="${esc(v)}" title="Click to copy the ID"`;
  // 'item' (pixel art on an inventory footprint), 'skill' (pixel art in a round-lit well) or ''
  // (painted art, scaled smoothly).
  const iconKind = src => /\/icons\/items\//.test(src || '') ? 'item' : /\/icons\/skills\//.test(src || '') ? 'skill' : '';
  const img = (src, box, alt = '', foot = false) => {
    const k = iconKind(src);
    const face = !k && box <= 64 && /\/portraits\//.test(src || '');
    const i = `<img class="${k ? 'pix' : 'smooth'}${face ? ' face' : ''}" src="${esc(src)}" data-box="${box}" alt="${esc(alt)}" loading="lazy" decoding="async">`;
    return foot && k === 'item' ? `<span class="foot">${i}</span>` : i;
  };
  const ibox = (src, box, t, foot = false) => src
    ? `<span class="ibox${iconKind(src) === 'skill' ? ' skill' : ''}">${img(src, box, '', foot)}</span>`
    : `<span class="ibox none">${svg(typeIcon(t) || 'box')}</span>`;
  const emptyState = (e, icon = 'info') => {
    const o = e && typeof e === 'object' ? e : { title: e };
    return `<div class="empty">${svg(icon)}<div>${o.title ? `<b>${esc(o.title)}</b>` : ''}${o.text ? `<span>${esc(o.text)}</span>` : ''}</div></div>`;
  };

  // ---------- values ----------
  const isObj = v => v && typeof v === 'object' && !Array.isArray(v);
  const isLink = v => isObj(v) && 't' in v && 'id' in v;
  // A link to a list (or any address): {href} or {t, cat?, f?: {key: value}, q?}.
  const isGo = v => isObj(v) && (typeof v.href === 'string' || ('t' in v && !('id' in v) && ('cat' in v || 'f' in v || 'q' in v || 'list' in v)));
  function target(v) {
    if (!isObj(v)) return null;
    if (typeof v.href === 'string') return v.href;
    // sec: a link to one section of the page (?sec=, see goSection).
    if (isLink(v)) return v.x || isHere(v) ? null : href(v.t, v.id) + (v.sec ? '?sec=' + encodeURIComponent(v.sec) : '');
    if ('t' in v && S.meta && S.meta.types[v.t]) {
      const p = { cat: v.cat, q: v.q };
      for (const [k, x] of Object.entries(v.f || {})) p['f_' + k] = x;
      return listHref(v.t, p);
    }
    return null;
  }
  // The page being shown ({t, id}, set when a page renders, cleared on each route): links to it are
  // plain text, so no page links to itself.
  let HERE = null;
  const isHere = v => !!HERE && isLink(v) && v.t === HERE.t && String(v.id) === HERE.id;
  function linkInline(v) {
    const lab = v.label != null ? v.label : `${v.t} ${v.id}`;
    // An NPC with neither a face nor a portrait gets the NPC glyph, so a column of NPCs lines up.
    const ic = v.icon ? img(v.icon, 20) : v.t === 'npc' ? `<span class="noface">${svg('user')}</span>` : '';
    // Inline like words: the label wraps with the text around it, and the icon stays with its first word.
    const m = String(lab).match(/^(\S*)([\s\S]*)$/);
    const inner = ic ? `<span class="lnh">${ic}${esc(m[1])}</span>${esc(m[2])}` : esc(lab);
    // A target with no page (v.x): plain text, not a link to nothing; the page itself: plain text too.
    if (v.x) return `<span class="ln x" title="${esc(typeTitle(v.t))} (no page)">${inner}</span>`;
    if (isHere(v)) return `<span class="ln here">${inner}</span>`;
    // sec: a link to one section of the page (?sec=, see goSection).
    return `<a class="ln" href="${href(v.t, v.id)}${v.sec ? '?sec=' + encodeURIComponent(v.sec) : ''}" title="${esc(typeTitle(v.t))}">${inner}</a>`;
  }
  function chip(v) {
    if (!isLink(v) && !isGo(v)) return `<span class="chip plain"><span>${val(v)}</span></span>`;
    const lab = v.label != null ? v.label : isLink(v) ? `${v.t} ${v.id}` : '';
    const ic = v.mark != null && v.mark !== '' ? `<span class="rk sm">${esc(v.mark)}</span>`
      : v.icon ? `<span class="ci">${img(v.icon, 20)}</span>`
      : `<span class="ci">${svg(v.glyph || typeIcon(v.t))}</span>`;
    const inner = `${ic}<span>${esc(lab)}</span>${v.sub ? `<small>${esc(v.sub)}</small>` : ''}`;
    const to = target(v), tt = v.t && S.meta.types[v.t] ? esc(typeTitle(v.t)) : '';
    if (isHere(v)) return `<span class="chip here">${inner}</span>`;
    if (!to) return `<span class="chip x"${tt ? ` title="${tt} (no page)"` : ''}>${inner}</span>`;
    return `<a class="chip${v.mark != null && v.mark !== '' ? ' mk' : ''}" href="${esc(to)}"${tt ? ` title="${tt}"` : ''}>${inner}</a>`;
  }
  const isIdLabel = l => typeof l === 'string' && /(^|\s)(id|ids|class id)$/i.test(l.trim());
  const valRow = (label, v) => (isIdLabel(plain(label)) && typeof v === 'number') ? esc(String(v)) : val(v);
  // Values joined by sep, each separator kept on the line of the value before it: links are inline boxes,
  // and a line could otherwise break between one and its comma, leaving the comma alone on the next line.
  // A piece that starts with punctuation (", " given as its own piece) sticks to the value before it too.
  // A list of only links, comma-separated, has no commas: their icons set them apart, and they wrap as
  // evenly spaced boxes (.llist).
  function joinVals(list, sep) {
    const s = sep.replace(/\s+$/, ''), gap = sep.slice(s.length), lk = x => x != null && (isLink(x) || isGo(x));
    const links = list.filter(x => !(typeof x === 'string' && /^\s*,\s*$/.test(x)));
    if (links.length > 1 && links.every(lk) && (s === ',' || links.length < list.length)) return `<span class="llist">${links.map(val).join('')}</span>`;
    let html = '', prev = null;   // the last value, not yet written: what follows may stick to it
    const flush = tail => { if (prev != null) html += tail ? `<span class="nb">${prev}${esc(tail)}</span>` : prev; prev = null; };
    for (const x of list) {
      const p = prev != null && typeof x === 'string' && x.match(/^([,;:.)]+)(\s*)([\s\S]*)$/);
      if (p) { flush(p[1]); html += esc(p[2]); if (p[3]) prev = val(p[3]); }
      else { if (prev != null) { flush(s); html += esc(gap); } prev = val(x); }
    }
    flush('');
    return html;
  }
  function val(v) {
    if (v === null || v === undefined || v === '') return '<span class="muted">—</span>';
    if (Array.isArray(v)) return v.length ? joinVals(v, ', ') : '<span class="muted">—</span>';
    if (typeof v === 'number') return fmtNum(v);
    if (typeof v === 'boolean') return v ? 'Yes' : 'No';
    if (isLink(v)) return linkInline(v);
    if (isGo(v)) return `<a href="${esc(target(v) || '#/')}">${esc(v.label != null ? v.label : '')}</a>`;
    if (typeof v === 'object') {
      // A cell that folds: {fold: summary text, body: value}, shut at first.
      if ('fold' in v) return `<details class="vfold"><summary>${esc(v.fold)}</summary><div class="vfoldbody">${val(v.body)}</div></details>`;
      // One entry per line, an array's values joined by `sep`: {lines: [[a, b], c], sep: ' + '};
      // rule: a line between them (choices rather than notes).
      if (Array.isArray(v.lines)) {
        if (!v.lines.length) return '<span class="muted">—</span>';
        // Choices (rule): each one's items as evenly spaced boxes, no separator; the line between choices
        // sets them apart. stack: each choice's items one under another (materials, one per line).
        const line = l => (!Array.isArray(l) ? val(l) : v.rule ? `<span class="llist${v.stack ? ' stack' : ''}">${l.filter(x => x !== '' && x != null).map(x => `<span>${val(x)}</span>`).join('')}</span>`
          : joinVals(l.filter(x => x !== '' && x != null), v.sep != null ? v.sep : ', '));
        return v.lines.map(l => `<div class="vline${v.rule ? ' rule' : ''}${v.divide ? ' divide' : ''}">${line(l)}</div>`).join('');
      }
      if ('text' in v) {
        const t = esc(v.text).replace(/\n/g, '<br>');
        return (v.tone ? `<span class="tone-${esc(v.tone)}">${t}</span>` : t) + (v.tag ? `<span class="vtag">${esc(v.tag)}</span>` : '');
      }
      return esc(JSON.stringify(v));
    }
    return esc(v).replace(/\n/g, '<br>');
  }
  function plain(v) {
    if (v === null || v === undefined) return '';
    if (Array.isArray(v)) return v.map(plain).join(', ');
    if (isLink(v) || isGo(v)) return String(v.label != null ? v.label : (v.id != null ? v.id : ''));
    if (typeof v === 'object') {
      if (Array.isArray(v.lines)) return v.lines.map(l => (Array.isArray(l) ? l.map(plain).join(v.sep != null ? v.sep : ', ') : plain(l))).join('; ');
      if ('fold' in v) return String(v.fold);
      return v.text != null ? String(v.text) : '';
    }
    return String(v);
  }
  function sortVal(v) {
    if (typeof v === 'number') return v;
    if (Array.isArray(v) && v.length === 1) return sortVal(v[0]);
    const s = plain(v).trim();
    const m = /^[-+]?[\d,]*\.?\d+(?:e[-+]?\d+)?/i.exec(s.replace(/^[^\d+-]+/, ''));
    if (m && /^[\s\d,.+%\-~xG×]*(?:[a-zA-Z ]{0,6})$/.test(s) && /\d/.test(s)) return parseFloat(m[0].replace(/,/g, ''));
    return s.toLowerCase();
  }
  function cmp(a, b) {
    if (typeof a === 'number' && typeof b === 'number') return a - b;
    if (typeof a === 'number') return -1;
    if (typeof b === 'number') return 1;
    return a < b ? -1 : a > b ? 1 : 0;
  }
  const isGold = v => (isObj(v) && v.tone === 'gold') || (typeof v === 'string' && /\d\s?G$/.test(v.trim()));

  // ---------- layout ----------
  function shell() {
    document.getElementById('app').innerHTML = `
      <header class="top">
        <button class="iconbtn menubtn" id="menubtn" aria-label="Menu">${svg('menu')}</button>
        <a class="brand" href="#/"><span class="bt">${esc(wikiName())}</span><small>${esc((S.meta && S.meta.badge) || 'local')}</small></a>
        <button class="searchbtn" id="searchbtn">${svg('search')}<span>Search items, monsters, skills…</span><kbd>Ctrl K</kbd></button>
        <span class="spacer"></span>
        <button class="iconbtn searchicon" id="searchicon" aria-label="Search">${svg('search')}</button>
        <button class="iconbtn" id="themebtn" aria-label="Toggle theme"></button>
      </header>
      <nav class="side" id="side"></nav>
      <div class="scrim" id="scrim"></div>
      <main class="main"><div class="wrap" id="view"></div></main>`;
    $('#menubtn').onclick = () => document.body.classList.toggle('nav-open');
    $('#scrim').onclick = () => document.body.classList.remove('nav-open');
    $('#searchbtn').onclick = () => palette.open();
    $('#searchicon').onclick = () => palette.open();
    $('#themebtn').onclick = toggleTheme;
    paintTheme();
  }
  function effectiveTheme() {
    const t = document.documentElement.dataset.theme;
    if (t) return t;
    return matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
  }
  function paintTheme() { const b = $('#themebtn'); if (b) b.innerHTML = svg(effectiveTheme() === 'dark' ? 'sun' : 'moon'); }
  function toggleTheme() {
    const next = effectiveTheme() === 'dark' ? 'light' : 'dark';
    document.documentElement.dataset.theme = next;
    try { localStorage.setItem('wiki-theme', next); } catch (e) { /* ignore */ }
    paintTheme();
  }
  matchMedia('(prefers-color-scheme: dark)').addEventListener('change', paintTheme);

  const openCats = new Set(store.get('opencats', []));
  // Sidebar sections the reader collapsed (by section id); the current page's section stays open.
  const closedSecs = new Set([].concat(store.get('navclosed', [])).map(String));
  function sidebar(cur) {
    const m = S.meta;
    const curSec = cur.type && S.typeSection[cur.type] ? S.typeSection[cur.type].id : null;
    let h = `<a class="nav${cur.home ? ' on' : ''}" href="#/">${svg('home')}<span>Home</span></a>`;
    for (const s of m.sections) {
      const sid = String(s.id), open = sid === curSec || !closedSecs.has(sid), gid = 'navg-' + safeId(sid);
      h += `<button type="button" class="nav-h" data-sec="${esc(sid)}" aria-expanded="${open}" aria-controls="${esc(gid)}">${svg(s.icon || s.id)}<span>${esc(s.title)}</span>${svg('down', 'i chev')}</button><div class="nav-g" id="${esc(gid)}"${open ? '' : ' hidden'}>`;
      // A section's pages A-Z (the sections themselves keep their order).
      for (const t of (s.types || []).filter(x => m.types[x]).sort((a, b) => byTitle(m.types[a], m.types[b]))) {
        const d = m.types[t];
        const on = cur.type === t;
        h += `<a class="nav${on && !cur.cat && cur.list ? ' on' : ''}" href="${listHref(t)}"><span>${esc(d.title)}</span><span class="n">${fmtNum(d.count)}</span></a>`;
        if (on && S.catKids[t] && (S.catKids[t][''] || []).length) h += tree(t, '', cur.cat || pageCat(t, cur.id), 0);
      }
      h += '</div>';
    }
    // When the data was made (meta.built, "YYYY-MM-DD HH:MM"), so a wiki behind the game shows it.
    const built = /^(\d{4})-(\d\d)-(\d\d)(?: (\d\d):(\d\d))?/.exec(m.built || '');
    if (built) {
      const d = new Date(+built[1], built[2] - 1, +built[3], +(built[4] || 0), +(built[5] || 0));
      const when = d.toLocaleDateString('en-US', { year: 'numeric', month: 'short', day: 'numeric' }) +
        (built[4] ? ', ' + d.toLocaleTimeString('en-US', { hour: 'numeric', minute: '2-digit' }) : '');
      // boot.js (the page that sets itself up from the game's files): its update screen, #/setup.
      h += `<div class="side-foot"><span title="${esc(m.builtNote || '')}">Data from ${esc(when)}</span></div>${canUpdate() ? '<div class="side-foot side-up"><a href="#/setup" title="Make the wiki again from your game files, after a game update">Update from your game files</a></div>' : ''}`;
    } else if (canUpdate()) {
      h += '<div class="side-foot"><a href="#/setup">Set up from your game files</a></div>';
    }
    const side = $('#side');
    const st = side.scrollTop;
    side.innerHTML = h;
    side.scrollTop = st;
    for (const b of $$('.tog', side)) b.onclick = e => {
      e.preventDefault();
      const k = b.dataset.k, open = !openCats.has(k);
      open ? openCats.add(k) : openCats.delete(k);
      store.set('opencats', [...openCats].slice(-200));
      b.classList.toggle('open', open);
      const sub = b.parentElement.nextElementSibling;
      if (sub && sub.classList.contains('tree')) sub.hidden = !open;
    };
    for (const b of $$('.nav-h', side)) b.onclick = () => {
      const k = b.dataset.sec, open = b.getAttribute('aria-expanded') !== 'true';
      open ? closedSecs.delete(k) : closedSecs.add(k);
      store.set('navclosed', [...closedSecs].slice(-50));
      b.setAttribute('aria-expanded', String(open));
      const g = b.nextElementSibling;
      if (g && g.classList.contains('nav-g')) g.hidden = !open;
    };
  }
  function ancestors(t, key) {
    const out = [], byKey = S.catT[t] || {};
    let c = byKey[key], guard = 0;
    while (c && guard++ < 20) { out.unshift(c); c = c.parent ? byKey[c.parent] : null; }
    return out;
  }
  // A category that stands for one page (meta category .page, e.g. a tool) links straight to it, and
  // shows as the current one while that page is open.
  const catHref = (t, c) => (c.page ? '#/' + t + '/' + encodeURIComponent(c.page) : listHref(t, { cat: c.key }));
  const pageCat = (t, id) => { const c = id && Object.values(S.catT[t] || {}).find(x => x.page === id); return c ? c.key : ''; };
  function tree(t, parent, curCat, depth) {
    const inner = treeItems(t, parent, curCat, depth);
    return inner ? `<div class="tree">${inner}</div>` : '';
  }
  function treeItems(t, parent, curCat, depth) {
    const kids = (S.catKids[t] || {})[parent] || [];
    if (!kids.length || depth > 6) return '';
    const path = new Set(ancestors(t, curCat).map(c => c.key));
    let h = '';
    for (const c of kids) {
      const has = ((S.catKids[t] || {})[c.key] || []).length > 0;
      const open = has && (path.has(c.key) || openCats.has(t + ':' + c.key));
      h += `<div class="tw"><button class="tog${has ? (open ? ' open' : '') : ' leaf'}" data-k="${esc(t + ':' + c.key)}" aria-label="Expand">${svg('chev')}</button>` +
        `<a class="nav${curCat === c.key ? ' on' : ''}" href="${catHref(t, c)}"><span>${esc(c.title)}</span><span class="n">${fmtNum(c.count)}</span></a></div>`;
      if (has) h += `<div class="tree"${open ? '' : ' hidden'}>${treeItems(t, c.key, curCat, depth + 1)}</div>`;
      // A category marked divider (meta: divider: true, e.g. the enchants' Suffixes) has a light line under it.
      if (c.divider && c !== kids[kids.length - 1]) h += '<div class="tdiv" role="separator"></div>';
    }
    return h;
  }
  function crumbs(parts) {
    return `<nav class="crumbs" aria-label="Breadcrumbs"><a href="#/">Home</a>${parts.map(p => `<span class="sep">/</span>${p[1] ? `<a href="${p[1]}">${esc(p[0])}</a>` : `<span>${esc(p[0])}</span>`}`).join('')}</nav>`;
  }

  // The page sets its data up itself (boot.js) and may here: not a hosted copy opened from a web host.
  const canUpdate = () => !!window.MabiWikiBoot && window.MabiWikiBoot.canUpdate !== false;
  // ---------- router ----------
  function parseHash() {
    const h = location.hash.replace(/^#\/?/, '');
    const i = h.indexOf('?');
    const path = i < 0 ? h : h.slice(0, i);
    const q = new URLSearchParams(i < 0 ? '' : h.slice(i + 1));
    const parts = path.split('/').filter(Boolean).map(s => { try { return decodeURIComponent(s); } catch (e) { return s; } });
    return { parts, q };
  }
  let lastKey = '';
  function route() {
    const { parts, q } = parseHash();
    const view = $('#view');
    HERE = null;
    document.body.classList.remove('nav-open');
    document.body.classList.toggle('is-home', !parts.length);
    palette.close();
    const key = parts.join('/');
    const sameView = key === lastKey;
    lastKey = key;
    if (!parts.length) { sidebar({ home: true }); home(view); }
    else if (parts[0] === 'search') { sidebar({}); searchPage(view, q.get('q') || ''); }
    else if (parts[0] === 'export' && SRC.exportView) { sidebar({}); setTitle('Export for hosting'); SRC.exportView(view); }
    else if (parts[0] === 'setup' && canUpdate()) { sidebar({}); setTitle('Update from your game files'); window.MabiWikiBoot.setupView(view); }
    else if (parts.length === 1 && S.byRoute[parts[0]]) {
      const t = S.byRoute[parts[0]];
      sidebar({ type: t, cat: q.get('cat') || '', list: true });
      listPage(view, t, q);
    } else if (parts.length >= 2) {
      const t = S.byRoute[parts[0]] && S.meta.types[parts[0]] ? parts[0] : (S.byRoute[parts[0]] || parts[0]);
      sidebar({ type: t, id: parts.slice(1).join('/') });
      pageView(view, t, parts.slice(1).join('/'));
    } else { sidebar({}); notFound(view, 'No such page.'); }
    // A link to a section scrolls there (renderPage) rather than to the top.
    if (!sameView && !q.get('sec')) window.scrollTo(0, 0);
  }
  window.addEventListener('hashchange', route);
  // A search box (the header's palette), started with the missing route's words: #/monster/gray_wolf
  // searches "gray wolf"; a bare number (#/quest/1) starts empty.
  function notFound(view, msg) {
    let parts = [];
    try { parts = decodeURIComponent(location.hash.replace(/^#\/?/, '').split('?')[0]).split('/').filter(Boolean); } catch (e) { /* a malformed hash */ }
    if (parts.length > 1 && (S.byRoute[parts[0]] || (S.meta && S.meta.types[parts[0]]))) parts = parts.slice(1);
    let words = parts.join(' ').replace(/[_\-.+]+/g, ' ').replace(/\s+/g, ' ').trim();
    if (/^\d+$/.test(words)) words = '';
    view.innerHTML = `${crumbs([['Not found']])}<div class="notfound"><h1>Not found</h1><p class="muted">${esc(msg)}</p>
      <button class="hsearch nfsearch" id="nfsearch">${svg('search')}<span>${words ? `Search for “${esc(words)}”…` : 'Search…'}</span><kbd>Ctrl K</kbd></button>
      <p><a href="#/">Back to the home page</a></p></div>`;
    $('#nfsearch').onclick = () => palette.open(words);
  }
  function setTitle(s) { document.title = s ? `${s} · ${wikiName()}` : wikiName(); }

  // ---------- home ----------
  // The hero search, then meta.home's tiles and panels when the source gives them, else a card
  // per section.
  function home(view) {
    setTitle('');
    const m = S.meta, H = isObj(m.home) ? m.home : {};
    const recent = store.get('recent', []);
    const pages = m.stats && m.stats.pages;
    const tries = (H.try || []).filter(Boolean);
    const reg = [];
    let browse = '';
    if ((H.featured || []).length) {
      // Groups ({title, icon, tiles}) each get a heading over their own grid; a flat list of tiles is one grid.
      const groups = H.featured.filter(g => isObj(g) && Array.isArray(g.tiles) && g.tiles.length);
      const grid = groups.length ? `<div class="tgroups">${groups.map(g => `<section class="tgroup"><h3 class="tg-h">${g.icon ? svg(g.icon) : ''}<span>${esc(g.title || '')}</span></h3><div class="tiles">${g.tiles.map(tile).join('')}</div></section>`).join('')}</div>`
        : `<div class="tiles">${H.featured.map(tile).join('')}</div>`;
      browse = `<div class="shead"><h2>${esc(H.featuredTitle || 'Browse')}</h2></div>${grid}`;
    } else {
      let cards = '';
      for (const s of m.sections) {
        const ts = (s.types || []).filter(t => m.types[t]);
        const t0 = ts[0];
        const top = t0 ? ((S.catKids[t0] || {})[''] || []).slice().sort((a, b) => (b.count || 0) - (a.count || 0)).slice(0, 10) : [];
        cards += `<div class="card"><div class="ch"><span class="ic">${svg(s.icon || s.id)}</span><b>${ts.length === 1 ? `<a class="cl" href="${listHref(t0)}">${esc(s.title)}</a>` : esc(s.title)}</b><span class="n">${fmtNum(s.count)}</span></div>
          ${ts.length > 1 ? `<ul>${ts.map(t => `<li><a href="${listHref(t)}"><span>${esc(m.types[t].title)}</span><span>${fmtNum(m.types[t].count)}</span></a></li>`).join('')}</ul>` : ''}
          ${top.length ? `<div class="cats">${top.map(c => `<a href="${catHref(t0, c)}">${esc(c.title)}</a>`).join('')}</div>` : ''}</div>`;
      }
      browse = `<div class="shead"><h2>Browse</h2></div><div class="cards">${cards || emptyState('Nothing here yet.')}</div>`;
    }
    const panels = (H.panels || []).filter(Boolean);
    const quick = panels.length ? `<div class="shead"><h2>${esc(H.panelsTitle || 'Jump to')}</h2></div><div class="quick n${panels.length}">${panels.map(pn =>
      `<section class="panel"><h2>${esc(pn.title || '')}</h2>${pn.text ? `<p>${esc(pn.text)}</p>` : ''}${(pn.blocks || []).map(b => block(b, reg)).join('')}</section>`).join('')}</div>` : '';
    view.innerHTML = `
      <section class="hero">
        <h1>${esc(wikiName())}</h1>
        <p class="tag">${esc((m.tagline) || '')}</p>
        ${m.note ? `<div class="note hnote">${svg('info')}<div>${esc(m.note)}${m.noteLink ? ` <a href="${esc(m.noteLink.href)}">${esc(m.noteLink.label)}</a>` : ''}</div></div>` : ''}
        <button class="hsearch" id="herosearch">${svg('search')}<span>${esc(H.searchHint || (pages ? `Search ${fmtNum(pages)} pages…` : 'Search…'))}</span><kbd>Ctrl K</kbd></button>
        <div class="try">${tries.length ? `<span>Try</span>${tries.map(chip).join('')}<span class="sep"></span>` : ''}<button type="button" class="rnd" id="random">${svg('shuffle')}Random page</button></div>
      </section>
      ${ORN}
      ${browse}
      ${quick}
      ${recent.length ? `<section class="recent"><h3>Recently viewed</h3><div class="chips">${recent.map(r => chip({ t: r[0], id: r[1], label: r[2], icon: r[3] })).join('')}</div></section>` : ''}
      <p class="foot-note"><span>${esc(H.footnote || (m.built ? 'Built ' + m.built : ''))}</span><a href="#/search?q=">Browse all pages</a></p>`;
    $('#herosearch').onclick = () => palette.open();
    $('#random').onclick = () => loadSearch().then(rows => {
      if (!rows.length) return;
      const r = rows[Math.floor(Math.random() * rows.length)];
      location.hash = href(r.t, r.id);
    });
    bindBlocks(view, reg);
    fitAll(view);
  }
  function tile(f) {
    if (!isObj(f)) return '';
    const to = target(f) || (f.t && S.meta.types[f.t] ? listHref(f.t) : '#/');
    const k = iconKind(f.icon);
    const art = f.icon ? `<span class="ibox${k === 'skill' ? ' skill' : ''}">${img(f.icon, k === 'item' ? 48 : 40, '', true)}</span>`
      : `<span class="ibox sv">${svg(f.glyph || (f.t ? typeIcon(f.t) : 'dot'))}</span>`;
    const small = f.count != null ? fmtNum(f.count) : (f.sub || '');
    return `<a class="tile" href="${esc(to)}">${art}<span class="tx"><b>${esc(f.label || '')}</b>${small !== '' ? `<small>${esc(small)}</small>` : ''}</span></a>`;
  }

  // ---------- list pages ----------
  function catSet(t, cat) {
    if (!cat) return null;
    const out = new Set([cat]), kids = S.catKids[t] || {};
    const walk = k => { for (const c of kids[k] || []) if (!out.has(c.key)) { out.add(c.key); walk(c.key); } };
    walk(cat);
    return out;
  }
  function catTitle(t, key) { const c = (S.catT[t] || {})[key]; return c ? c.title : key; }
  // The nearest ancestor of `cat` (itself first) with an entry in `map`, else `dflt`.
  function perCat(t, cat, map, dflt) {
    if (isObj(map) && cat) {
      const anc = ancestors(t, cat);
      for (let i = anc.length - 1; i >= 0; i--) if (Array.isArray(map[anc[i].key])) return map[anc[i].key];
      if (!anc.length && Array.isArray(map[cat])) return map[cat];
    }
    return dflt || [];
  }

  const FACET_NONE = '__none';
  function listPage(view, t, q) {
    const d = S.meta.types[t];
    const cat = q.get('cat') || '';
    const cols = perCat(t, cat, d.catColumns, d.columns).filter(c => c && c.key);
    const facets = perCat(t, cat, d.catFilters, d.filters).filter(f => f && f.key);
    // q: what's in the filter box; keys: keywords kept by pressing Enter there (k, "|"-separated),
    // each narrowing the list further.
    const st = { q: q.get('q') || '', keys: (q.get('k') || '').split('|').map(s => s.trim()).filter(Boolean), p: +(q.get('p') || 1), s: q.get('s') || '', d: q.get('d') === '1',
      v: q.get('v') || store.get('view-' + t, d.view || 'table'), f: {} };
    // A filter's choice comes from the address, else what was last picked
    // for this list (kept in the browser), so it stays on across categories.
    for (const fc of facets) {
      const v = q.has('f_' + fc.key) ? q.get('f_' + fc.key) : store.get('facet-' + t + '-' + fc.key, '');
      if (v) st.f[fc.key] = v;
    }
    setTitle(cat ? catTitle(t, cat) : d.title);
    const anc = ancestors(t, cat);
    const kidsOf = k => (S.catKids[t] || {})[k] || [];
    // The category's own children; a category without any shows its siblings instead.
    let tabs = kidsOf(cat), tabOn = '';
    if (cat && !tabs.length) {
      const c = (S.catT[t] || {})[cat];
      tabs = kidsOf(c && c.parent && (S.catT[t] || {})[c.parent] ? c.parent : '');
      tabOn = cat;
    }
    const minF = facets.filter(fc => fc.kind === 'min'), selF = facets.filter(fc => fc.kind !== 'min');
    view.innerHTML = `
      ${crumbs([[d.title, cat ? listHref(t) : null], ...anc.map((c, i) => [c.title, i < anc.length - 1 ? listHref(t, { cat: c.key }) : null])])}
      <div class="lhead"><div class="t"><h1>${esc(cat ? catTitle(t, cat) : d.title)}</h1><p id="lcount">Loading…</p></div></div>
      ${tabs.length > 1 || (tabs.length && !tabOn) ? `<div class="subcats">${tabs.slice(0, 60).map(c => `<a class="${c.key === tabOn ? 'on' : ''}" href="${catHref(t, c)}"${c.key === tabOn ? ' aria-current="page"' : ''}>${esc(c.title)}<span class="n">${fmtNum(c.count)}</span></a>`).join('')}</div>` : ''}
      <div class="toolbar">
        <label class="filter">${svg('search')}<input id="lq" type="search" placeholder="${st.keys.length ? 'Narrow down…' : `Filter ${esc((cat ? catTitle(t, cat) : d.title).toLowerCase())}…`}" value="${esc(st.q)}" autocomplete="off" aria-label="Filter" title="Press Enter to keep a keyword and narrow down with another"></label>
        ${selF.map(fc => `<label class="facet" data-fk="${esc(fc.key)}"${fc.hint ? ` title="${esc(fc.hint)}"` : ''}><span>${esc(fc.title)}</span><b class="fv">${esc(fc.any || 'Any')}</b>${svg('down')}<select class="fsel" data-fk="${esc(fc.key)}" aria-label="${esc(fc.title)}"><option value="">${esc(fc.any || 'Any')}</option></select></label>`).join('')}
        ${minF.map(fc => `<label class="facet min" data-fk="${esc(fc.key)}"><span>${esc(fc.title)} ≥</span><input class="fmin" data-fk="${esc(fc.key)}" type="number" inputmode="decimal" placeholder="0" value="${esc(st.f[fc.key] || '')}" aria-label="${esc(fc.title)} at least"></label>`).join('')}
        <div class="seg" role="group" aria-label="View">
          <button data-v="table" class="${st.v === 'table' ? 'on' : ''}">${svg('list')}<span class="hide-sm">Table</span></button>
          <button data-v="grid" class="${st.v === 'grid' ? 'on' : ''}">${svg('grid')}<span class="hide-sm">Grid</span></button>
        </div>
      </div>
      ${selF.filter(fc => fc.hint).map(fc => `<p class="facet-hint">${svg('info')}<span>${esc(fc.hint)}</span></p>`).join('')}
      <div class="active" id="lact" hidden></div>
      <div id="lres" class="results"></div>`;
    // The box's hint: "Narrow down…" once there are keywords.
    const keyHint = () => {
      const i = $('#lq');
      if (i) i.placeholder = st.keys.length ? 'Narrow down…' : `Filter ${(cat ? catTitle(t, cat) : d.title).toLowerCase()}…`;
    };
    const sync = () => {
      const p = {};
      if (cat) p.cat = cat;
      if (st.keys.length) p.k = st.keys.join('|');
      if (st.q) p.q = st.q;
      if (st.p > 1) p.p = st.p;
      if (st.s) { p.s = st.s; if (st.d) p.d = 1; }
      for (const k in st.f) if (st.f[k]) p['f_' + k] = st.f[k];
      if (q.get('v')) p.v = st.v;
      history.replaceState(null, '', listHref(t, p));
    };
    const colOf = k => cols.find(c => c.key === k);
    // Numeric-looking columns sort highest first on the first click.
    const descFirst = k => { const c = colOf(k); return !!(c && (c.sort || c.bar || c.meter)); };
    const sortTitle = k => k === 'name' ? 'name' : k === 'id' ? 'ID' : k === 'cat' ? 'category' : k === 'sub' ? 'info' : ((colOf(k) || {}).title || k);
    const isNumSort = k => { const c = colOf(k); return k === 'id' || !!(c && (c.num || c.sort || c.bar || c.meter)); };
    loadIndex(t).then(rows => {
      if (!view.isConnected || !$('#lres')) return;
      const cs = catSet(t, cat);
      const base = cs ? rows.filter(r => r._c.some(c => cs.has(c))) : rows;
      let filtered = base;
      const run = () => {
        const toks = st.keys.concat(st.q).join(' ').toLowerCase().split(/\s+/).filter(Boolean);
        filtered = toks.length ? base.filter(r => toks.every(k => r._h.includes(k))) : base;
        for (const fc of facets) {
          const k = fc.key, v = st.f[k];
          if (!v) continue;
          if (fc.kind === 'min') {
            const n = parseFloat(v);
            if (Number.isFinite(n)) filtered = filtered.filter(r => { const x = sortVal(r[k]); return typeof x === 'number' && x >= n; });
          } else filtered = filtered.filter(r => v === FACET_NONE ? (r[k] == null || r[k] === '' || (Array.isArray(r[k]) && !r[k].length))
            : Array.isArray(r[k]) ? r[k].map(String).includes(v) : String(r[k]) === v);
        }
        if (st.s) {
          const k = st.s, dir = st.d ? -1 : 1, c = colOf(k);
          const key = k === 'name' ? r => (r.name || '').toLowerCase()
            : k === 'id' ? r => (isNaN(+r.id) ? r.id : +r.id)
            : k === 'cat' ? r => catTitle(t, r._c[0] || '').toLowerCase()
            : c && c.sort ? r => sortVal(r[c.sort])
            : c && Array.isArray(c.meter) ? r => { const i = c.meter.indexOf(plain(r[k])); return i < 0 ? '' : i; }
            : r => sortVal(r[k]);
          const blank = v => v === '' || v === null || v === undefined;
          filtered = filtered.map(r => [key(r), r]).sort((a, b) => (blank(a[0]) - blank(b[0])) || cmp(a[0], b[0]) * dir || 0).map(x => x[1]);
        }
        $('#lcount').textContent = `${filtered.length !== base.length ? fmtNum(filtered.length) + ' of ' : ''}${fmtNum(base.length)} ${base.length === 1 ? 'entry' : 'entries'}`;
        drawActive();
        draw();
      };
      // The filters in use, each removable, and the sort.
      const drawActive = () => {
        const box = $('#lact');
        if (!box) return;
        const parts = [];
        st.keys.forEach((k, i) => parts.push(`<button type="button" class="x" data-clear="k:${i}" aria-label="Remove ${esc(k)}">“${esc(k)}”${svg('x')}</button>`));
        for (const fc of facets) {
          const v = st.f[fc.key];
          if (!v) continue;
          let lab;
          if (fc.kind === 'min') lab = `${fc.title} ≥ ${v}`;
          else {
            const o = (fc.options || []).find(x => x[0] === v);
            lab = `${fc.title}: ${v === FACET_NONE ? (fc.none || 'None') : (o ? o[1] : v)}`;
          }
          parts.push(`<button type="button" class="x" data-clear="${esc(fc.key)}" aria-label="Remove ${esc(lab)}">${esc(lab)}${svg('x')}</button>`);
        }
        const srt = st.s ? `<span class="srt">Sorted by ${esc(String(sortTitle(st.s)).toLowerCase())}, ${isNumSort(st.s) ? (st.d ? 'highest first' : 'lowest first') : (st.d ? 'Z to A' : 'A to Z')}</span>` : '';
        box.hidden = !parts.length && !srt;
        box.innerHTML = parts.join('') + srt + (parts.length || srt ? '<button type="button" class="clr">Clear all</button>' : '');
        for (const b of $$('[data-clear]', box)) b.onclick = () => {
          const k = b.dataset.clear;
          if (k === 'q') { st.q = ''; $('#lq').value = ''; } else if (k.startsWith('k:')) { st.keys.splice(+k.slice(2), 1); keyHint(); } else setFacet(k, '');
          st.p = 1; sync(); run();
        };
        const clr = $('.clr', box);
        if (clr) clr.onclick = () => {
          st.q = ''; $('#lq').value = ''; st.keys = []; keyHint();
          for (const fc of facets) setFacet(fc.key, '');
          st.s = ''; st.d = false; st.p = 1; sync(); run();
        };
      };
      const setFacet = (k, v) => {
        st.f[k] = v;
        store.set('facet-' + t + '-' + k, v);
        const sel = $(`select.fsel[data-fk="${CSS.escape(k)}"]`, view), inp = $(`input.fmin[data-fk="${CSS.escape(k)}"]`, view);
        if (sel) { sel.value = v; showFacet(sel); }
        if (inp) inp.value = v;
        const lab = $(`.facet[data-fk="${CSS.escape(k)}"]`, view);
        if (lab) lab.classList.toggle('on', !!v);
      };
      // The facet shows its choice without the count (the open list has the counts).
      const showFacet = sel => {
        const fc = facets.find(x => x.key === sel.dataset.fk), fv = sel.parentElement.querySelector('.fv');
        if (!fc || !fv) return;
        const v = sel.value, o = (fc.options || []).find(x => x[0] === v);
        fv.textContent = !v ? (fc.any || 'Any') : v === FACET_NONE ? (fc.none || 'None') : (o ? o[1] : v);
      };
      const draw = () => {
        const per = st.v === 'grid' ? 120 : 100;
        const pages = Math.max(1, Math.ceil(filtered.length / per));
        if (st.p > pages) st.p = pages;
        const slice = filtered.slice((st.p - 1) * per, st.p * per);
        const box = $('#lres');
        if (!filtered.length) box.innerHTML = emptyState({ title: `Nothing matches${st.q ? ` “${st.q}”` : ''}.`, text: 'Try fewer words, or clear the filters.' });
        else box.innerHTML = (st.v === 'grid' ? gridView(t, slice, cols) : tableView(t, cols, slice, st)) + pager(st.p, pages);
        for (const th of $$('th.s', box)) th.onclick = () => {
          const k = th.dataset.k, first = descFirst(k);
          if (st.s === k) { if (st.d === first) st.d = !first; else { st.s = ''; st.d = false; } } else { st.s = k; st.d = first; }
          st.p = 1; sync(); run();
        };
        for (const b of $$('.pager button[data-p]', box)) b.onclick = () => {
          st.p = +b.dataset.p; sync(); draw();
          $('.toolbar').scrollIntoView({ block: 'start' }); window.scrollBy(0, -70);
        };
        fitAll(box);
      };
      // Facet dropdowns: the values in this list (with counts), plus "none".
      for (const sel of $$('select.fsel', view)) {
        const fc = facets.find(x => x.key === sel.dataset.fk);
        const n = new Map();
        let none = 0;
        // A list value (an enchant's effects) counts under each of its values.
        for (const r of base) {
          const v = r[fc.key];
          if (v == null || v === '' || (Array.isArray(v) && !v.length)) none++;
          else for (const x of Array.isArray(v) ? v : [v]) n.set(String(x), (n.get(String(x)) || 0) + 1);
        }
        // Declared options keep their order and labels; other values follow.
        const opts = (fc.options || []).map(([v, label]) => [v, n.get(v) || 0, label]);
        for (const [v, c] of [...n.entries()].sort((a, b) => b[1] - a[1])) if (!opts.some(o => o[0] === v)) opts.push([v, c, v]);
        if (fc.none) opts.push([FACET_NONE, none, fc.none]);
        sel.innerHTML = `<option value="">${esc(fc.any || 'Any')} (${fmtNum(base.length)})</option>` +
          opts.map(([v, c, label]) => `<option value="${esc(v)}"${c ? '' : ' disabled'}>${esc(label)} (${fmtNum(c)})</option>`).join('');
        if (st.f[fc.key] && !opts.some(o => o[0] === st.f[fc.key])) st.f[fc.key] = '';
        sel.value = st.f[fc.key] || '';
        sel.closest('.facet').classList.toggle('on', !!sel.value);
        showFacet(sel);
        sel.onchange = () => { setFacet(fc.key, sel.value); st.p = 1; sync(); run(); };
      }
      for (const inp of $$('input.fmin', view)) {
        const k = inp.dataset.fk;
        inp.closest('.facet').classList.toggle('on', !!st.f[k]);
        inp.oninput = debounce(() => { setFacet(k, inp.value.trim()); st.p = 1; sync(); run(); }, 200);
      }
      const inp = $('#lq');
      const typed = debounce(() => { st.q = inp.value.trim(); st.p = 1; sync(); run(); }, 140);
      inp.oninput = typed;
      // Enter keeps what's typed as a keyword and empties the box for the next one, which narrows
      // the list within it; Backspace in an empty box takes the last keyword back.
      inp.onkeydown = e => {
        if (e.key === 'Enter') {
          e.preventDefault();
          const v = inp.value.trim();
          if (!v) return;
          if (!st.keys.some(k => k.toLowerCase() === v.toLowerCase())) st.keys.push(v);
          inp.value = ''; st.q = ''; st.p = 1; keyHint(); sync(); run();
        } else if (e.key === 'Backspace' && !inp.value && st.keys.length) {
          e.preventDefault();
          inp.value = st.keys.pop(); st.q = inp.value; st.p = 1; keyHint(); sync(); run();
        }
      };
      for (const b of $$('.seg button', view)) b.onclick = () => {
        st.v = b.dataset.v; store.set('view-' + t, st.v);
        for (const x of $$('.seg button', view)) x.classList.toggle('on', x === b);
        draw();
      };
      run();
    });
  }
  // A column's cell: a range bar, a meter, or the plain value.
  function cell(c, r, top) {
    const v = r[c.key];
    if (v == null || v === '') return '<span class="muted">—</span>';
    if (c.bar) {
      const lo = +r[c.key + 'Min'], hi = +r[c.key + 'Max'];
      if (top > 0 && Number.isFinite(lo) && Number.isFinite(hi)) {
        const L = Math.max(0, Math.min(100, lo / top * 100)), R = Math.max(0, Math.min(100, 100 - hi / top * 100));
        return `<span class="rng"><span class="bar" aria-hidden="true"><i style="left:${L.toFixed(1)}%;right:${R.toFixed(1)}%"></i></span><b>${val(v)}</b></span>`;
      }
    }
    if (Array.isArray(c.meter)) {
      const i = c.meter.indexOf(plain(v));
      if (i >= 0) return `<span class="spd"><span class="pips" aria-hidden="true">${c.meter.map((_, j) => `<i class="${j <= i ? 'f' : ''}"></i>`).join('')}</span>${val(v)}</span>`;
    }
    // lines: a list, one to a line (an item's set effects), the amounts in color.
    if (c.lines && Array.isArray(v)) return v.map(x => `<div class="vline">${tonesHtml(String(x))}</div>`).join('');
    return val(v);
  }
  function tableView(t, cols, rows, st) {
    // The Info column only when it says something the rows don't all share, and isn't just each
    // row's category again (a quest's "Normal" next to Category "Normal").
    const hasCat = new Set(rows.map(r => r._c[0] || '')).size > 1;
    // Columns no row on this page fills aren't shown.
    cols = (cols || []).filter(c => rows.some(r => r[c.key] != null && r[c.key] !== ''));
    // Nor is Info when, in every row, it only repeats the category or another column (a dungeon's altar).
    const subRepeats = rows.every(r => !r.sub || plain(r.sub) === (r._c[0] ? catTitle(t, r._c[0]) : '') ||
      cols.some(c => r[c.key] != null && plain(r[c.key]) === plain(r.sub)));
    // A type's meta can put Category first (catFirst: after ID, before Name) and drop Info (noInfo).
    const td = S.meta.types[t] || {}, catFirst = !!td.catFirst;
    const hasSub = !td.noInfo && rows.some(r => r.sub) && !subRepeats && (rows.length === 1 || new Set(rows.map(r => plain(r.sub))).size > 1);
    const hasIcon = rows.some(r => r.icon);
    // Text ids (tircho_alby_dungeon) aren't numbers: left-aligned, and they wrap.
    const textIds = rows.some(r => !/^\d+$/.test(String(r.id)));
    const idCls = textIds ? 'idt hide-sm' : 'num hide-sm';
    const th = (k, label, cls) => `<th class="s${cls ? ' ' + cls : ''}${st.s === k ? ' on' : ''}" data-k="${esc(k)}"${st.s === k ? ` aria-sort="${st.d ? 'descending' : 'ascending'}"` : ''}>${esc(label)}${st.s === k ? `<span class="ar">${st.d ? '▼' : '▲'}</span>` : ''}</th>`;
    const tops = {};
    for (const c of cols) if (c.bar) tops[c.key] = rows.reduce((m, r) => Math.max(m, +r[c.key + 'Max'] || 0), 0);
    // noId: a type whose ids are only slugs (questlines) shows no ID column.
    const showId = !td.noId;
    let h = `<div class="tablebox"><div class="scroll"><table class="t"><thead><tr>${showId ? th('id', 'ID', idCls) : ''}${hasCat && catFirst ? th('cat', 'Category', 'hide-sm') : ''}${th('name', 'Name', 'nmh')}`;
    for (const c of cols) h += th(c.key, c.title || c.key, c.num || c.bar ? 'num' : '');
    if (hasSub) h += th('sub', 'Info');
    if (hasCat && !catFirst) h += th('cat', 'Category', 'hide-sm');
    h += '</tr></thead><tbody>';
    const on = k => (st.s === k ? ' on' : '');
    const catTd = r => `<td class="hide-sm${on('cat')}">${r._c[0] ? `<a href="${listHref(t, { cat: r._c[0] })}">${esc(catTitle(t, r._c[0]))}</a>` : ''}</td>`;
    for (const r of rows) {
      h += '<tr>' + (showId ? `<td class="${idCls} id copy${on('id')}" ${copyAttr(r.id)}><span class="idchip">${textIds ? esc(r.id).replace(/_/g, '_<wbr>') : esc(r.id)}</span></td>` : '');
      if (hasCat && catFirst) h += catTd(r);
      h += `<td class="nm${on('name')}"><span class="nmw">${hasIcon ? ibox(r.icon, 32, t) : ''}<span class="nmt"><a href="${href(td.linkAs || t, r.id)}">${esc(r.name || r.id)}</a>${r.alt && r.alt !== r.name ? `<small>${esc(r.alt)}</small>` : ''}</span></span></td>`;
      for (const c of cols) h += `<td class="${c.num || c.bar ? 'num' : ''}${c.bar || c.meter ? ' nw' : ''}${on(c.key)}">${r[c.key] == null || r[c.key] === '' ? '' : cell(c, r, tops[c.key])}</td>`;
      if (hasSub) h += `<td class="${on('sub').trim()}">${r.sub ? val(r.sub) : ''}</td>`;
      if (hasCat && !catFirst) h += catTd(r);
      h += '</tr>';
    }
    return h + '</tbody></table></div></div>';
  }
  // A card per row: the picture, the name, every stat the list's columns mark card (the ones the
  // game's tooltip shows: damage, critical, balance, speed, defense, protection...), then the set
  // effects the item gives (r.sets).
  function gridView(t, rows, cols) {
    const cards = (cols || []).filter(c => c.card);
    // linkAs: a list whose rows open another type's page (the Craftables list opens the items').
    const lt = (S.meta.types[t] || {}).linkAs || t;
    return `<div class="grid">${rows.map(r => {
      const kst = cards.filter(c => r[c.key] != null && r[c.key] !== '').map(c => {
        const lab = c.short != null ? c.short : c.title;
        return `<span>${lab ? `<em>${esc(lab)}</em>` : ''}${esc(plain(r[c.key]))}</span>`;
      });
      const under = kst.length ? `<span class="kst">${kst.join('<i>·</i>')}</span>`
        : r.sub ? `<small>${esc(plain(r.sub))}</small>` : (r.alt ? `<small>${esc(r.alt)}</small>` : '');
      // "Set", then each effect on a line of its own.
      const sets = r.sets && r.sets.length ? `<span class="gsets"><em>Set</em>${[].concat(r.sets).map(s => `<span>${tonesHtml(String(s))}</span>`).join('')}</span>` : '';
      return `<a class="gcard" href="${href(lt, r.id)}">${ibox(r.icon, 96, t, true)}<b>${esc(r.name || r.id)}</b>${under}${sets}${r.badge ? `<span class="gbadge">${esc(r.badge)}</span>` : ''}<span class="gid copy" ${copyAttr(r.id)}>${svg('hash')}${esc(r.id)}</span></a>`;
    }).join('')}</div>`;
  }
  function pager(p, n) {
    if (n <= 1) return '';
    const nums = new Set([1, n, p - 2, p - 1, p, p + 1, p + 2].filter(x => x >= 1 && x <= n));
    const arr = [...nums].sort((a, b) => a - b);
    let h = `<div class="pager"><button data-p="${p - 1}" ${p <= 1 ? 'disabled' : ''} aria-label="Previous">‹</button>`;
    let prev = 0;
    for (const x of arr) {
      if (x - prev > 1) h += '<span class="gap">…</span>';
      h += `<button data-p="${x}" class="${x === p ? 'on' : ''}"${x === p ? ' aria-current="page"' : ''}>${x}</button>`;
      prev = x;
    }
    return h + `<button data-p="${p + 1}" ${p >= n ? 'disabled' : ''} aria-label="Next">›</button></div>`;
  }

  // ---------- page view ----------
  function remember(t, id, name, icon) {
    let r = store.get('recent', []).filter(x => !(x[0] === t && x[1] === id));
    r.unshift([t, id, name, icon || null]);
    store.set('recent', r.slice(0, 10));
  }
  function pageView(view, t, id) {
    view.innerHTML = '<div class="boot">Loading…</div>';
    loadPage(t, id).then(p => renderPage(view, t, id, p), err => {
      // A bug in the data source (a page builder that throws) shouldn't pass for a missing page:
      // it goes to the console. A missing page ("no page file", a 404) is the normal case below.
      if (err instanceof TypeError || err instanceof ReferenceError || err instanceof RangeError || err instanceof SyntaxError) {
        console.error(`Building ${t} ${id} failed:`, err);
      }
      // no page file: show the entry from the index if there is one
      loadIndex(t).then(rows => {
        const e = rows.find(r => String(r.id) === String(id));
        if (!e) return notFound(view, `No ${singular(t).toLowerCase()} with the id ${id}.`);
        renderPage(view, t, id, { title: e.name || id, subtitle: e.alt, icon: e.icon, category: e._c,
          infobox: [['ID', String(e.id)]].concat(e.sub ? [['Info', e.sub]] : []), sections: [],
          summary: '', _entryOnly: true });
      });
    });
  }
  function renderPage(view, t, id, p) {
    if (!view.isConnected) return;
    HERE = { t, id: String(id) };
    setTitle(p.title);
    remember(t, id, p.title, p.icon);
    const cats = Array.isArray(p.category) ? p.category : (p.category ? [p.category] : []);
    const deep = cats.length ? cats.reduce((a, b) => (ancestors(t, b).length >= ancestors(t, a).length ? b : a)) : '';
    const anc = deep ? ancestors(t, deep) : [];
    const cr = [[typeTitle(t), listHref(t)], ...anc.map(c => [c.title, listHref(t, { cat: c.key })])];
    const secs = (p.sections || []).filter(s => s && ((s.blocks || []).length || s.empty));
    const blocks = [];
    // A section with wide: true (a table with many columns, like an item's upgrades) goes below the
    // article and the infobox, across the whole page, instead of beside the infobox.
    // Each section has an id from its heading ("How it's made" → sec-how-its-made), so a link can point at
    // it (#/item/40006?sec=how-its-made); the link button beside the heading copies that address.
    const secKeys = secs.map(s => secSlug(s.heading)).map((k, i, all) => (all.indexOf(k) < i ? `${k}-${i}` : k));
    const secHtml = (s, i) => `<section class="sec${s.wide ? ' wide' : ''}" id="sec-${secKeys[i]}"><h2><span class="orn-d"></span>${esc(s.heading || '')}` +
      `<a class="slink" href="${esc(href(t, id) + '?sec=' + secKeys[i])}" data-sec="${esc(secKeys[i])}" title="Link to this section" aria-label="Link to ${esc(s.heading || '')}">${svg('hash')}</a></h2>` +
      `${(s.blocks || []).map(b => block(b, blocks)).join('')}${s.empty ? `<div class="blk">${emptyState(s.empty)}</div>` : ''}</section>`;
    const sectionsHtml = secs.map((s, i) => (s.wide ? '' : secHtml(s, i))).join('');
    const wideHtml = secs.map((s, i) => (s.wide ? secHtml(s, i) : '')).join('');
    const info = p.infobox || [];
    const kind = iconKind(p.icon);
    const headIcon = p.icon && kind === 'skill' ? `<span class="ibox skill hic">${img(p.icon, 64, '')}</span>` : '';
    const meta = [`<span class="typ">${esc(p.kind || oneOf(t))}</span>`, `<button type="button" class="pill id copy" ${copyAttr(id)}>${svg('hash')}${esc(id)}</button>`]
      .concat((deep ? [deep] : []).concat(cats.filter(c => c !== deep).slice(0, 4)).map(c => `<a class="pill" href="${listHref(t, { cat: c })}">${esc(catTitle(t, c))}</a>`));
    let art = '';
    if (p.icon) {
      if (kind === 'item') art = `<div class="art item">${img(p.icon, 220, p.title, true)}<span class="sz"></span></div>`;
      else if (kind === 'skill') art = `<div class="art skill"><span class="ibox skill">${img(p.icon, 64, p.title)}</span></div>`;
      else art = `<div class="art paint">${img(p.icon, /\/icons\/npcs\//.test(p.icon) ? 320 : 260, p.title)}</div>`;
    }
    const more = isObj(p.more) && target(p.more) ? `<a class="more" href="${esc(target(p.more))}">${esc(p.more.label || 'More')} →</a>` : '';
    const aside = (info.length || art) ? `<div class="infobox">${art}${info.length ? `<dl>${info.map(r => `<div><dt>${esc(plain(r[0]))}</dt><dd${isGold(r[1]) ? ' class="gold"' : ''}>${valRow(r[0], r[1])}</dd></div>`).join('')}</dl>` : ''}${more}</div>` : '';
    const stats = (Array.isArray(p.stats) ? p.stats : []).filter(s => Array.isArray(s) && s.length >= 2);
    // Tags under the title (an item's Category tags: {href, label}), each a link to its list.
    const tagList = (Array.isArray(p.tags) ? p.tags : []).filter(x => isObj(x) && x.label);
    const ptags = tagList.length ? `<div class="ptags">${tagList.map(x => `<a class="ptag" href="${esc(x.href || '#/')}">${svg('tag')}${esc(x.label)}</a>`).join('')}</div>` : '';
    const statbar = stats.length ? `<div class="statbar lead1">${stats.map(s => `<div class="${plain(s[1]).length + (s[2] ? String(s[2]).length : 0) > 12 ? 'wide' : ''}"><span>${esc(plain(s[0]))}</span><b>${val(s[1])}${s[2] != null && s[2] !== '' ? `<small>${esc(s[2])}</small>` : ''}</b></div>`).join('')}</div>` : '';
    // Jump links to the sections (4 or more), in the order they show: the ones beside the infobox,
    // then the full-width ones below.
    const order = secs.map((s, i) => i).filter(i => !secs[i].wide).concat(secs.map((s, i) => i).filter(i => secs[i].wide));
    const jumps = secs.length >= 4 ? `<nav class="jumps" aria-label="On this page">${order.map(i => `<a href="${esc(href(t, id) + '?sec=' + secKeys[i])}" data-sec="${esc(secKeys[i])}">${esc(secs[i].heading || '')}</a>`).join('')}</nav>` : '';
    view.innerHTML = `${crumbs(cr)}
      <div class="phead">${headIcon}<div class="t"><h1>${esc(p.title)}</h1>${p.subtitle && p.subtitle !== p.title ? `<div class="sub">${esc(p.subtitle)}</div>` : ''}${p.noMeta ? '' : `<div class="meta">${meta.join('')}</div>`}${ptags}</div></div>
      ${statbar}${jumps}
      <div class="article${aside ? '' : ' solo'}${p.capTables ? ' capt' : ''}">
        <div class="body">
          ${p.summary ? `<div class="lead">${esc(p.summary)}</div>` : ''}
          ${sectionsHtml || wideHtml || (p.summary ? '' : emptyState(p._entryOnly ? { title: 'This entry has no page of its own.', text: 'What the list knows about it is in the box.' } : 'Nothing more is known about this yet.'))}
        </div>
        ${aside ? `<aside class="aside">${aside}</aside>` : ''}
      </div>
      ${wideHtml && sectionsHtml ? `<div class="wide-secs">${wideHtml}</div>` : ''}`;
    bindBlocks(view, blocks);
    fitAll(view);
    // A click on a section's heading (or its link button): the section's address goes in the address
    // bar and on the clipboard ("Link copied" shows briefly), and the page scrolls there.
    for (const a of $$('.slink', view)) {
      const h2 = a.closest('h2');
      h2.classList.add('linkable');
      h2.title = 'Copy a link to this section';
      h2.onclick = e => {
        e.preventDefault();
        history.replaceState(null, '', a.getAttribute('href'));
        try { navigator.clipboard.writeText(location.href); } catch (err) { /* no clipboard */ }
        h2.dataset.copied = 'Link copied';
        clearTimeout(h2._t);
        h2._t = setTimeout(() => { delete h2.dataset.copied; }, 1400);
        goSection(a.dataset.sec);
      };
    }
    // A jump link: its section's address in the bar, and the page scrolls there (no re-render).
    for (const a of $$('.jumps a', view)) a.onclick = e => { e.preventDefault(); history.replaceState(null, '', a.getAttribute('href')); goSection(a.dataset.sec); };
    // Opened at a section (?sec=): scroll to it once it's laid out.
    const want = parseHash().q.get('sec');
    if (want) requestAnimationFrame(() => goSection(want));
  }
  // Scroll to a page section by its key, opening it if it's folded; the top bar is left clear (scroll-margin).
  const secSlug = s => String(s || '').toLowerCase().replace(/['’]/g, '').replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '') || 'section';
  function goSection(key) {
    const el = document.getElementById('sec-' + key);
    if (!el) return;
    for (const d of $$('details', el)) if (!d.open && $$('details', el).length === 1) d.open = true;
    el.scrollIntoView({ block: 'start', behavior: 'smooth' });
  }

  // ---------- blocks ----------
  const MAX_ROWS = 60, MAX_CHIPS = 80;
  const isShortList = items => items.length > 0 && items.length <= 24 && items.every(x => (typeof x === 'string' || typeof x === 'number') && String(x).length <= 40 && !/\n/.test(String(x)));
  // Signed amounts in a text in color: "+30%" blue, "-5" red, the other way round after what's better
  // lower ("MP Usage -10%": blue). A sign only counts at the start of a word ("10-12%" is a range).
  // The words match datasource.js's DOWN_GOOD.
  const TONE_DOWN_GOOD = /usage|\bused\b|\buse\b|cost|consum|cooldown|cool ?time|casting time|delay|charge time|mana burn|wait/i;
  // A short part (between commas or lines) with one amount is colored whole ("Max Damage +5", "STR +1");
  // in a longer one only the amount is.
  const AMOUNT_RE = /(^|[\s(:,/])([+\-−]\s?\d[\d.,]*(?:\s?[~–-]\s?\d[\d.,]*)?(?:\s?%)?)/g;
  const toneGood = (amt, said) => (amt[0] === '+') !== (TONE_DOWN_GOOD.test(said) && !/reduc|decreas/i.test(said));
  function tonesHtml(s) {
    return String(s).split(/(,\s+|\n)/).map((part, i) => {
      if (i % 2) return esc(part);
      const ms = [...part.matchAll(AMOUNT_RE)];
      if (ms.length === 1 && part.trim().length <= 48) {
        const m = ms[0], st = m.index + m[1].length;
        return `<span class="tone-${toneGood(m[2], part.slice(0, st)) ? 'pos' : 'neg'}">${esc(part)}</span>`;
      }
      let out = '', at = 0;
      for (const m of ms) {
        const st = m.index + m[1].length, said = part.slice(Math.max(0, st - 40), st).split(/[.;]/).pop();
        out += esc(part.slice(at, st)) + `<span class="tone-${toneGood(m[2], said) ? 'pos' : 'neg'}">${esc(m[2])}</span>`;
        at = st + m[2].length;
      }
      return out + esc(part.slice(at));
    }).join('');
  }
  // A closed select shows its pick on one line and cuts it off when it's long (the open list shows it
  // whole). A select whose options have data-short gets a box over it showing the pick's short text
  // (the select itself is clear on top of the box: a click or a key still opens the list, with the
  // full texts). Any other pick that doesn't fit gets its full text under the select, and as its tooltip.
  let measureCtx = null;
  function fitSelects(root) {
    measureCtx = measureCtx || document.createElement('canvas').getContext('2d');
    for (const s of $$('select', root)) {
      if ([...s.options].some(o => o.dataset.short)) {
        const o = s.selectedOptions[0];
        const wrap = document.createElement('span');
        wrap.className = 'sbox';
        wrap.innerHTML = `<span class="sshow${o && o.value !== '' && o.value !== '0' ? '' : ' none'}">${esc(o ? o.dataset.short || o.textContent : '')}</span>`;
        s.replaceWith(wrap);
        wrap.appendChild(s);
        s.title = o ? o.textContent : '';
        continue;
      }
      const next = s.nextElementSibling;
      if (next && next.classList.contains('bfull')) next.remove();
      const o = s.selectedOptions[0], t = o && o.value !== '' && o.value !== '0' ? o.textContent : '';
      s.title = t;
      if (!t || !s.clientWidth) continue;
      const cs = getComputedStyle(s);
      measureCtx.font = `${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`;
      // The room: the box less its padding and the arrow.
      if (measureCtx.measureText(t).width > s.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight) - 18) s.insertAdjacentHTML('afterend', `<small class="bsub bfull">${esc(t)}</small>`);
    }
  }
  function block(b, reg, ctx) {
    if (!isObj(b)) return '';
    const title = b.title ? `<h3>${esc(b.title)}</h3>` : '';
    const k = b.kind;
    let body = '';
    if (k === 'text') {
      const txt = String(b.text || '').trim();
      // tones: signed amounts in color (a skill rank's effect).
      const tx = b.tones ? tonesHtml : esc;
      // Inside a rank, the rank's effect is always set in the effect style, whatever its length, so it
      // doesn't change from rank to rank (its lines kept); another one-line text there too.
      if (ctx === 'rank' && txt && (b.title === 'Effect' || (txt.length <= 40 && !/\n/.test(txt)))) body = `<div class="effect">${txt.split(/\r?\n/).map(l => tx(l.trim())).join('<br>')}</div>`;
      else body = `<div class="txt">${txt.split(/\n\s*\n/).map(p => `<p>${tx(p.trim())}</p>`).join('')}</div>`;
    } else if (k === 'kv') {
      const rows = (b.rows || []).filter(r => Array.isArray(r));
      const long = rows.some(r => plain(r[1]).length > 34 || plain(r[0]).length > 28);
      body = `<dl class="kv${long ? ' one' : ''}">${rows.map(r => `<div><dt>${val(r[0])}</dt><dd${isGold(r[1]) ? ' class="gold"' : ''}>${valRow(r[0], r[1])}</dd></div>`).join('')}</dl>`;
    } else if (k === 'formula') {
      // A formula or a short algorithm, as written: {text}.
      body = `<pre class="formula"><code>${esc(b.text || '')}</code></pre>`;
    } else if (k === 'details') {
      // A one-line summary that opens to the rest: {summary, blocks, open (open at first)}.
      body = `<details class="more"${b.open ? ' open' : ''}><summary>${esc(b.summary || 'More')}</summary><div class="morebody">${(b.blocks || []).map(x => block(x, reg)).join('')}</div></details>`;
    } else if (k === 'table') {
      const i = reg.push({ kind: 'table', b, sort: b.sort == null ? -1 : b.sort, dir: 1, all: false }) - 1;
      body = `<div class="tablebox" data-bid="${i}"></div>`;
    } else if (k === 'list') {
      const items = b.items || [];
      // Short facts as pills: a gold diamond each, or b.icon ('check', 'x') when the source says
      // which way the list reads.
      const mk = b.icon === 'check' || b.icon === 'x' ? svg(b.icon) : '<i class="dia"></i>';
      // ordered: numbered steps (a how-to), never the short-pill form.
      body = b.ordered ? `<ol class="lst steps">${items.map(x => `<li>${val(x)}</li>`).join('')}</ol>`
        : isShortList(items)
        ? `<ul class="ticks${b.icon === 'x' ? ' no' : ''}">${items.map(x => `<li>${mk}<span>${val(x)}</span></li>`).join('')}</ul>`
        : `<ul class="lst">${items.map(x => `<li>${val(x)}</li>`).join('')}</ul>`;
    } else if (k === 'links') {
      const items = b.items || [];
      const i = reg.push({ kind: 'links', b, all: !b.oneRow && items.length <= MAX_CHIPS }) - 1;
      body = `<div data-bid="${i}"></div>`;
    } else if (k === 'grid') {
      const items = b.items || [];
      body = `<div class="grid sm">${items.map(x => isLink(x)
        ? (x.x ? `<div class="gcard x">` : `<a class="gcard" href="${href(x.t, x.id)}">`) + `${ibox(x.icon, 60, x.t, true)}<b>${esc(x.label != null ? x.label : x.id)}</b>${x.sub ? `<small>${esc(x.sub)}</small>` : ''}` + (x.x ? `</div>` : `</a>`)
        : `<div class="gcard"><b>${val(x)}</b></div>`).join('')}</div>`;
    } else if (k === 'ranks') {
      const rows = b.rows || [];
      const n = Math.max(rows.length, (b.blocks || []).length);
      const start = b.start == null ? n - 1 : Math.min(+b.start, n - 1);
      const i = reg.push({ kind: 'ranks', b, cur: Math.max(0, start), all: false }) - 1;
      body = `<div class="ranks" data-bid="${i}"></div>`;
    } else if (k === 'builder') {
      const i = reg.push({ kind: 'builder', b, picks: (b.groups || []).map(g => Array(g.slots).fill('')) }) - 1;
      body = `<div class="builder" data-bid="${i}"></div>`;
    } else if (k === 'tabs') {
      const tabs = (b.tabs || []).filter(isObj);
      let cur = Math.min(Math.max(0, +b.start || 0), Math.max(0, tabs.length - 1));
      const saved = b.remember ? store.get('tab-' + b.remember, null) : null;
      const si = saved != null ? tabs.findIndex(t => t.label === saved) : -1;
      if (si >= 0) cur = si;
      const i = reg.push({ kind: 'tabs', b, cur }) - 1;
      body = `<div class="tabs${b.style === 'line' ? ' line' : ''}" data-bid="${i}"></div>`;
    } else if (k === 'embed') {
      // Drawn by the data source itself (WIKI_SOURCE.embed(name, element, args)): the Mabinogi
      // wiki's 3D viewer. The stage around it is ours.
      const i = reg.push({ kind: 'embed', b }) - 1;
      // plain: no 3D stage around it (a calculator).
      body = b.plain ? `<div class="embed plain" data-bid="${i}"></div>`
        : `<div class="stage"><span class="cube">${svg('cube')}</span><div class="embed" data-bid="${i}"></div>${b.caption ? `<small class="cap">${esc(b.caption)}</small>` : ''}</div>`;
    } else if (k === 'image') {
      // A picture (a region's minimap): {src, alt, caption, markers: [{x, y (percent of the
      // picture), label, kind: 'npc' | 'mob', t, id, items: [{label, t, id}]}]}. Markers are
      // buttons (an NPC a circle, a spawn area a diamond) with their name on hover; a tap or click
      // opens a popup naming it, or listing the spawn's monsters, linking to their pages (drawMap).
      // Above the map: a switch per layer, which is also the legend, and Expand map: the viewer
      // (mapView), over the whole screen, zooming and panning. A tap on the map opens it too.
      const marks = (b.markers || []).filter(m => m.x >= 0 && m.x <= 100 && m.y >= 0 && m.y <= 100);
      const img = `<img src="${esc(b.src)}" alt="${esc(b.alt || '')}" loading="lazy" decoding="async">`;
      const figcap = b.caption ? `<figcaption>${esc(b.caption)}</figcaption>` : '';
      if (!marks.length) body = `<figure class="figmap"><div class="figbox"><a href="${esc(b.src)}" target="_blank" rel="noopener">${img}</a></div>${figcap}</figure>`;
      else {
        const i = reg.push({ kind: 'image', b, marks }) - 1;
        const lays = [['npc', 'NPCs'], ['mob', 'Monsters']].map(([kd, name]) => {
          const n = marks.filter(m => m.kind === kd).length;
          return n ? `<button type="button" class="maplay" data-layer="${kd}" aria-pressed="true"><i class="mk ${kd}" aria-hidden="true"></i>${name} <small>${fmtNum(n)}</small></button>` : '';
        }).join('');
        body = `<figure class="figmap" data-bid="${i}"><div class="figbar"><div class="maplays" role="group" aria-label="Show on the map">${lays}</div>` +
          `<button type="button" class="figexp" aria-haspopup="dialog">${svg('expand')}Expand map</button></div><div class="figbox">${img}${mapPins(marks, true)}</div>${figcap}</figure>`;
      }
    } else if (k === 'note') {
      const warn = b.tone === 'warn';
      body = `<div class="note${warn ? ' warn' : ''}">${svg(warn ? 'warn' : 'info')}<div>${val(b.text)}</div></div>`;
    } else {
      body = `<div class="note warn">${svg('warn')}<div>Unknown block kind “${esc(k)}”.</div></div>`;
    }
    return `<div class="blk">${title}${body}</div>`;
  }
  function bindBlocks(root, reg) {
    for (const el of $$('[data-bid]', root)) {
      const r = reg[+el.dataset.bid];
      if (!r || r.el) continue;
      r.el = el; r.reg = reg;
      if (r.kind === 'table') drawTable(el, r);
      else if (r.kind === 'links') drawLinks(el, r);
      else if (r.kind === 'ranks') drawRanks(el, r);
      else if (r.kind === 'tabs') drawTabs(el, r);
      else if (r.kind === 'builder') drawBuilder(el, r);
      else if (r.kind === 'embed' && SRC.embed) SRC.embed(r.b.name, el, r.b.args || {});
      else if (r.kind === 'image') drawMap(el, r);
    }
  }
  // A map block's behaviour: its layer switches, a popup per marker (one open at a time on the
  // whole page; a click elsewhere or Esc closes it) and the viewer (mapView), which a tap on the
  // map or its Expand button opens. The popup goes right after its marker, so Tab from the marker
  // reaches its links.
  const mapPop = { pop: null, btn: null, n: 0, place: null };
  function closeMapPop(focus) {
    const { pop, btn } = mapPop;
    if (pop) pop.remove();
    if (btn) {
      btn.setAttribute('aria-expanded', 'false');
      btn.classList.remove('on');
      if (focus && btn.isConnected) btn.focus();
    }
    mapPop.pop = mapPop.btn = mapPop.place = null;
  }
  // A map's markers: buttons, an NPC a circle, a spawn area a diamond. data-to: the pages each
  // stands for, so a table row linking one can light it up. Inline they're placed by percent of
  // the picture; in the viewer, in screen space by its paint().
  function mapPins(marks, inline) {
    return marks.map((m, j) => {
      const to = [m].concat(m.items || []).filter(x => x.t).map(x => href(x.t, x.id)).join(' ');
      const what = m.kind === 'npc' ? 'NPC: ' : m.kind === 'mob' ? 'Monsters: ' : '';
      return `<button type="button" class="mark ${esc(m.kind || 'pin')}" data-m="${j}" data-to="${esc(to)}"${inline ? ` style="left:${+m.x}%;top:${+m.y}%"` : ''} ` +
        `title="${esc(m.label || '')}" aria-label="${esc(what + (m.label || ''))}" aria-expanded="false"></button>`;
    }).join('');
  }
  // One marker: an NPC's name and a link to its page, or a spawn's monsters. Several: the NPCs,
  // then the monsters, as lists of links.
  function mapPopHtml(ms) {
    const link = x => (x.t ? `<a href="${esc(href(x.t, x.id))}">${esc(x.label || '')}</a>` : esc(x.label || ''));
    const ul = (head, xs) => `<div class="mp"><b>${head}</b><ul>${xs.map(x => `<li>${link(x)}</li>`).join('')}</ul></div>`;
    const one = ms.length === 1 && ms[0], spawn = ms.filter(m => (m.items || []).length), who = ms.filter(m => !(m.items || []).length);
    if (one && who.length) return `<div class="mp"><b>${esc(one.label || '')}</b>${one.t ? `<a class="go" href="${esc(href(one.t, one.id))}">Open page →</a>` : ''}</div>`;
    return (who.length ? ul('NPCs here', who) : '') + (spawn.length ? ul('Monsters here', [].concat(...spawn.map(m => m.items))) : '');
  }
  // A marker's popup (or none, when it was the one open: a second click closes it). It shows the
  // markers on screen with it too: under the finger for a tap, overlapping its shape for a mouse
  // click, none for a key press (each one is reached by Tab). pos(marker) gives a marker's centre
  // in the layer (box) the popup is placed in: beside the marker, below it or above, whichever
  // side has room; when neither has, the list scrolls. `more`: html added under several.
  function openMapPop(e, btn, r, box, pos, more) {
    const was = mapPop.btn === btn;
    closeMapPop();
    if (was) return null;
    const reach = !e.detail ? 0 : e.pointerType === 'mouse' ? 6 : 12, [bx, by] = pos(btn);
    const dist = x => { const [x2, y2] = pos(x); return Math.hypot(x2 - bx, y2 - by); };
    const near = [btn].concat($$('.mark', box).filter(x => x !== btn && x.offsetParent && dist(x) < reach).sort((a, b) => dist(a) - dist(b)));
    const m = r.marks[+btn.dataset.m];
    const pop = document.createElement('div');
    pop.className = 'mappop';
    pop.id = 'mappop' + ++mapPop.n;
    pop.setAttribute('role', 'group');
    pop.setAttribute('aria-label', near.length > 1 ? `${near.length} markers here` : m.label || 'Marker');
    pop.innerHTML = `<button type="button" class="x" aria-label="Close">×</button><div class="mpin">${mapPopHtml(near.map(x => r.marks[+x.dataset.m]))}</div>${near.length > 1 && more ? more : ''}`;
    btn.after(pop);
    btn.setAttribute('aria-expanded', 'true');
    btn.setAttribute('aria-controls', pop.id);
    btn.classList.add('on');
    mapPop.pop = pop; mapPop.btn = btn;
    $('.x', pop).onclick = () => closeMapPop(true);
    const inner = $('.mpin', pop);
    mapPop.place = () => {
      const W = box.clientWidth, H = box.clientHeight, [cx, cy] = pos(btn);
      pop.style.maxWidth = Math.max(160, Math.min(300, W - 8)) + 'px';
      inner.style.maxHeight = '';
      let ph = pop.offsetHeight;
      const pw = pop.offsetWidth, down = H - cy - 18, up = cy - 18;
      const below = ph <= down || (ph > up && down >= up);
      const room = below ? down : up;
      if (ph > room) { inner.style.maxHeight = Math.max(64, room - (ph - inner.offsetHeight)) + 'px'; ph = pop.offsetHeight; }
      const left = Math.max(4, Math.min(cx - pw / 2, W - pw - 4));
      pop.style.left = left + 'px';
      pop.style.top = (below ? cy + 14 : cy - 14 - ph) + 'px';
      pop.style.setProperty('--ax', Math.max(10, Math.min(pw - 10, cx - left)) + 'px');
      pop.classList.toggle('down', below);
      pop.classList.toggle('up', !below);
    };
    mapPop.place();
    pop.marks = near.map(x => r.marks[+x.dataset.m]);
    return pop;
  }
  // A layer shown or hidden, on the map and in its viewer when that's open.
  function setMapLayer(r, kd, on) {
    for (const root of [r.el, mv && mv.r === r ? mv.dlg : null]) if (root) {
      root.classList.toggle('hide-' + kd, !on);
      for (const t of $$(`.maplay[data-layer="${kd}"]`, root)) t.setAttribute('aria-pressed', String(on));
    }
    if (!on && mapPop.btn && mapPop.btn.classList.contains(kd)) closeMapPop();
  }
  function drawMap(el, r) {
    const box = $('.figbox', el), exp = $('.figexp', el);
    for (const t of $$('.maplay', el)) t.onclick = () => setMapLayer(r, t.dataset.layer, t.getAttribute('aria-pressed') !== 'true');
    const pos = x => [x.offsetLeft + x.offsetWidth / 2, x.offsetTop + x.offsetHeight / 2];
    for (const btn of $$('.mark', box)) btn.onclick = e => {
      // Markers piled on one spot: the viewer, zoomed in on it, sets them apart.
      const pop = openMapPop(e, btn, r, box, pos, '<button type="button" class="linkbtn mvgo">Zoom in here</button>');
      const go = pop && $('.mvgo', pop);
      if (go) go.onclick = () => { const ms = pop.marks; closeMapPop(); mapView.open(r, exp, ms); };
    };
    // A tap on the picture opens the viewer, unless it's closing a popup.
    let hadPop = false;
    box.addEventListener('pointerdown', () => { hadPop = !!mapPop.pop; });
    $('img', box).onclick = () => { if (!hadPop) mapView.open(r, exp); };
    exp.onclick = () => mapView.open(r, exp);
  }
  // The map viewer: a dialog over the whole screen with the map fitted to it, zoomed and panned
  // with the wheel (around the pointer), a pinch, a drag, a double tap, its buttons or the keys
  // (+ − 0, arrows); from fit to 8× or twice the picture's own size, whichever is more. The
  // picture is one layer moved by a CSS transform (translate, scale); the markers sit over it in
  // screen space, placed from their percent each frame, so they keep their size and come apart as
  // it zooms in. The picture is the inline one's file, so the browser's cache serves it.
  let mv = null;
  const mapView = {
    open(r, back, at) {
      if (mv) mapView.close(false);
      closeMapPop();
      const b = r.b, dlg = document.createElement('div');
      dlg.className = ['mapview', 'figmap'].concat([...r.el.classList].filter(c => c.startsWith('hide-'))).join(' ');
      dlg.setAttribute('role', 'dialog');
      dlg.setAttribute('aria-modal', 'true');
      dlg.setAttribute('aria-label', b.alt || 'Map');
      const lays = $('.maplays', r.el);
      dlg.innerHTML = `<div class="mvbar"><b class="mvtitle">${esc(b.alt || 'Map')}</b><div class="mvtools">${lays ? lays.outerHTML : ''}` +
        `<div class="mvzoom" role="group" aria-label="Zoom"><button type="button" class="mvbtn" data-z="-1" aria-label="Zoom out" title="Zoom out (−)">−</button>` +
        `<span class="mvlvl" aria-hidden="true">1×</span><button type="button" class="mvbtn" data-z="1" aria-label="Zoom in" title="Zoom in (+)">+</button>` +
        `<button type="button" class="mvbtn wide" data-z="0" aria-label="Fit to the screen" title="Fit to the screen (0)">Fit</button></div></div>` +
        `<button type="button" class="mvbtn mvx" aria-label="Close the map" title="Close (Esc)">×</button></div>` +
        `<div class="mvstage" tabindex="0" aria-label="The map. Drag it or use the arrow keys to move it, + and − to zoom, 0 to fit it to the screen.">` +
        `<div class="mvpic"><img alt="${esc(b.alt || '')}" draggable="false"></div><div class="mvpins">${mapPins(r.marks, false)}</div>` +
        `<div class="mvload" role="status">Loading the map…</div></div>`;
      document.body.appendChild(dlg);
      const stage = $('.mvstage', dlg), pic = $('.mvpic', dlg), img = $('img', pic), pins = $('.mvpins', dlg);
      const v = mv = { r, dlg, img, back, s: 1, tx: 0, ty: 0, nw: 0, nh: 0, SW: 1, SH: 1, f0: 1, max: 8, raf: 0, inert: [], over: document.documentElement.style.overflow };
      // The page behind can't be reached or scrolled while it's open.
      for (const x of document.body.children) if (x !== dlg && !x.inert) { x.inert = true; v.inert.push(x); }
      document.documentElement.style.overflow = 'hidden';
      const local = e => { const R = stage.getBoundingClientRect(); return [e.clientX - R.left, e.clientY - R.top]; };
      const size = () => {
        v.SW = stage.clientWidth; v.SH = stage.clientHeight;
        v.f0 = Math.min(v.SW / v.nw, v.SH / v.nh); v.max = Math.max(8 * v.f0, 2);
      };
      // In range, and kept on screen: centred while smaller than the screen, else its edges no
      // further in than the screen's.
      const clamp = () => {
        v.s = Math.min(v.max, Math.max(v.f0, v.s));
        const w = v.nw * v.s, h = v.nh * v.s;
        v.tx = w <= v.SW ? (v.SW - w) / 2 : Math.min(0, Math.max(v.SW - w, v.tx));
        v.ty = h <= v.SH ? (v.SH - h) / 2 : Math.min(0, Math.max(v.SH - h, v.ty));
      };
      const off = p => p._x < 0 || p._y < 0 || p._x > v.SW || p._y > v.SH;
      const zin = $('[data-z="1"]', dlg), zout = $('[data-z="-1"]', dlg), lvl = $('.mvlvl', dlg), marks = $$('.mark', pins);
      const paint = () => {
        v.raf = 0;
        if (!v.nw || mv !== v) return;
        pic.style.transform = `translate(${v.tx}px, ${v.ty}px) scale(${v.s})`;
        const w = v.nw * v.s, h = v.nh * v.s;
        for (const p of marks) {
          const m = r.marks[+p.dataset.m];
          p._x = v.tx + m.x / 100 * w; p._y = v.ty + m.y / 100 * h;
          p.style.transform = `translate(${p._x}px, ${p._y}px)`;
        }
        const k = v.s / v.f0;
        lvl.textContent = (k < 10 ? Math.round(k * 10) / 10 : Math.round(k)) + '×';
        zout.setAttribute('aria-disabled', String(v.s <= v.f0 * 1.001));
        zin.setAttribute('aria-disabled', String(v.s >= v.max * 0.999));
        if (mapPop.btn && dlg.contains(mapPop.btn)) { if (off(mapPop.btn)) closeMapPop(); else mapPop.place(); }
      };
      const draw = () => { if (!v.raf) v.raf = requestAnimationFrame(paint); };
      const zoomAt = (s, x, y) => {
        if (!v.nw) return;
        const ns = Math.min(v.max, Math.max(v.f0, s));
        v.tx = x - (x - v.tx) * ns / v.s; v.ty = y - (y - v.ty) * ns / v.s; v.s = ns;
        clamp(); draw();
      };
      const fit = () => { v.s = v.f0; clamp(); draw(); };
      const centre = (m, s) => { v.s = s; v.tx = v.SW / 2 - m.x / 100 * v.nw * v.s; v.ty = v.SH / 2 - m.y / 100 * v.nh * v.s; clamp(); };
      // While it moves the picture is its own layer (will-change); after, it's drawn sharp again
      // at its new size.
      const moving = on => stage.classList.toggle('moving', on);
      img.onload = () => {
        if (mv !== v) return;
        (img.decode ? img.decode().catch(() => {}) : Promise.resolve()).then(() => {
          if (mv !== v) return;
          v.nw = img.naturalWidth; v.nh = img.naturalHeight;
          img.style.width = v.nw + 'px'; img.style.height = v.nh + 'px';
          size();
          // `at`: markers to open on, centred, zoomed until they're ~20px apart (from 2× on).
          if (at && at.length) {
            let d = Infinity;
            for (const a of at) for (const c of at) if (a !== c) d = Math.min(d, Math.hypot((a.x - c.x) / 100 * v.nw, (a.y - c.y) / 100 * v.nh));
            const mid = { x: at.reduce((t, m) => t + m.x, 0) / at.length, y: at.reduce((t, m) => t + m.y, 0) / at.length };
            centre(mid, Math.min(v.max, Math.max(v.f0 * 2, d > 0 && d < Infinity ? 20 / d : v.max)));
          } else { v.s = v.f0; clamp(); }
          paint();
          stage.classList.add('ready');
        });
      };
      img.onerror = () => { const l = $('.mvload', dlg); l.textContent = 'The map couldn’t be loaded.'; l.classList.add('err'); };
      img.src = b.src;
      // Toolbar.
      for (const t of $$('.maplay', dlg)) t.onclick = () => setMapLayer(r, t.dataset.layer, t.getAttribute('aria-pressed') !== 'true');
      for (const z of $$('[data-z]', dlg)) z.onclick = () => {
        if (z.getAttribute('aria-disabled') === 'true') return;
        const d = +z.dataset.z;
        if (!d) fit(); else zoomAt(v.s * (d > 0 ? 1.6 : 1 / 1.6), v.SW / 2, v.SH / 2);
      };
      $('.mvx', dlg).onclick = () => mapView.close(true);
      for (const p of marks) p.onclick = e => openMapPop(e, p, r, pins, x => [x._x, x._y]);
      // A marker reached by Tab off screen is brought to the middle.
      stage.addEventListener('focusin', e => {
        const p = e.target.closest('.mark');
        if (p && v.nw && off(p)) { centre(r.marks[+p.dataset.m], v.s); paint(); }
      });
      // Pointers: one drags (once it has moved a few pixels, so a tap stays a tap: a marker's
      // opens its popup), two pinch; either may start on a marker, since on a phone they cover
      // much of the map. A quick second tap off the markers zooms in there.
      const pts = new Map();
      let g = null, dragged = false, tap = null, onMark = false;
      const begin = () => {
        const p = [...pts.values()], moved = !!(g && g.moved);
        if (p.length === 1) g = { pinch: false, p0: p[0], t0: [v.tx, v.ty], moved };
        else {
          const [a, c] = p, m0 = [(a[0] + c[0]) / 2, (a[1] + c[1]) / 2];
          g = { pinch: true, d0: Math.hypot(a[0] - c[0], a[1] - c[1]) || 1, ip: [(m0[0] - v.tx) / v.s, (m0[1] - v.ty) / v.s], s0: v.s, moved: true };
          moving(true);
        }
      };
      stage.addEventListener('pointerdown', e => {
        dragged = false;
        if (e.button > 0 || !v.nw || e.target.closest('.mappop')) return;
        if (!pts.size) onMark = !!e.target.closest('.mark');
        pts.set(e.pointerId, local(e));
        if (pts.size > 2) return;
        if (pts.size === 2) for (const id of pts.keys()) { try { stage.setPointerCapture(id); } catch (x) { /* gone */ } }
        begin();
      });
      stage.addEventListener('pointermove', e => {
        if (!pts.has(e.pointerId) || !g) return;
        pts.set(e.pointerId, local(e));
        if (!g.pinch) {
          const [x, y] = pts.get(e.pointerId), dx = x - g.p0[0], dy = y - g.p0[1];
          if (!g.moved) {
            if (Math.hypot(dx, dy) < 5) return;
            g.moved = true;
            moving(true);
            try { stage.setPointerCapture(e.pointerId); } catch (x2) { /* gone */ }
          }
          v.tx = g.t0[0] + dx; v.ty = g.t0[1] + dy;
        } else {
          const [a, c] = [...pts.values()], d = Math.hypot(a[0] - c[0], a[1] - c[1]), m = [(a[0] + c[0]) / 2, (a[1] + c[1]) / 2];
          v.s = Math.min(v.max, Math.max(v.f0, g.s0 * d / g.d0));
          v.tx = m[0] - g.ip[0] * v.s; v.ty = m[1] - g.ip[1] * v.s;
        }
        clamp(); draw();
      });
      const end = e => {
        if (!pts.has(e.pointerId)) return;
        const at2 = pts.get(e.pointerId), moved = !!(g && g.moved);
        pts.delete(e.pointerId);
        if (pts.size) { begin(); g.moved = true; return; }
        g = null;
        moving(false);
        if (moved) { dragged = true; tap = null; return; }
        if (e.type !== 'pointerup' || onMark) { tap = null; return; }
        const now = performance.now();
        if (tap && now - tap.t < 350 && Math.hypot(at2[0] - tap.x, at2[1] - tap.y) < 30) { tap = null; zoomAt(v.s * 2, at2[0], at2[1]); }
        else tap = { t: now, x: at2[0], y: at2[1] };
      };
      stage.addEventListener('pointerup', end);
      stage.addEventListener('pointercancel', end);
      // A drag ends in a click: it doesn't close the popup.
      stage.addEventListener('click', e => { if (dragged) { dragged = false; e.stopPropagation(); } }, true);
      stage.addEventListener('wheel', e => {
        e.preventDefault();
        if (!v.nw) return;
        const [x, y] = local(e), dy = e.deltaMode === 1 ? e.deltaY * 16 : e.deltaMode === 2 ? e.deltaY * v.SH : e.deltaY;
        moving(true);
        clearTimeout(v.wheel);
        v.wheel = setTimeout(() => moving(false), 180);
        zoomAt(v.s * Math.exp(-dy * 0.0018), x, y);
      }, { passive: false });
      // Keys. The page's own (the search's) wait until it's closed; Tab stays inside.
      dlg.addEventListener('keydown', e => {
        e.stopPropagation();
        if (e.key === 'Escape') {
          e.preventDefault();
          if (mapPop.pop && dlg.contains(mapPop.pop)) closeMapPop(true); else mapView.close(true);
          return;
        }
        if (e.key === 'Tab') {
          const f = $$('button, a[href], [tabindex="0"]', dlg).filter(x => x.getClientRects().length);
          const i = f.indexOf(document.activeElement);
          if (e.shiftKey && i <= 0) { e.preventDefault(); f[f.length - 1].focus(); }
          else if (!e.shiftKey && i === f.length - 1) { e.preventDefault(); f[0].focus(); }
          return;
        }
        if (e.ctrlKey || e.metaKey || e.altKey || !v.nw) return;
        const step = 80, c = [v.SW / 2, v.SH / 2];
        if (e.key === '+' || e.key === '=') zoomAt(v.s * 1.6, c[0], c[1]);
        else if (e.key === '-' || e.key === '_') zoomAt(v.s / 1.6, c[0], c[1]);
        else if (e.key === '0') fit();
        else if (e.key.startsWith('Arrow')) {
          v.tx += e.key === 'ArrowLeft' ? step : e.key === 'ArrowRight' ? -step : 0;
          v.ty += e.key === 'ArrowUp' ? step : e.key === 'ArrowDown' ? -step : 0;
          clamp(); draw();
        } else return;
        e.preventDefault();
      });
      // A new screen size keeps the zoom and the middle; a new page (a popup's link) closes it.
      v.onresize = () => {
        if (!v.nw) return;
        const k = v.s / v.f0, ix = (v.SW / 2 - v.tx) / v.s, iy = (v.SH / 2 - v.ty) / v.s;
        size();
        v.s = k * v.f0; v.tx = v.SW / 2 - ix * v.s; v.ty = v.SH / 2 - iy * v.s;
        clamp(); draw();
      };
      v.onhash = () => mapView.close(false);
      addEventListener('resize', v.onresize);
      addEventListener('hashchange', v.onhash);
      stage.focus();
    },
    close(refocus) {
      const v = mv;
      if (!v) return;
      mv = null;
      if (mapPop.btn && v.dlg.contains(mapPop.btn)) closeMapPop();
      cancelAnimationFrame(v.raf);
      clearTimeout(v.wheel);
      removeEventListener('resize', v.onresize);
      removeEventListener('hashchange', v.onhash);
      for (const x of v.inert) x.inert = false;
      document.documentElement.style.overflow = v.over;
      v.img.removeAttribute('src');
      v.dlg.remove();
      if (refocus && v.back && v.back.isConnected) v.back.focus();
    },
  };
  document.addEventListener('click', e => {
    if (mapPop.pop && !mapPop.pop.contains(e.target) && !(mapPop.btn && mapPop.btn.contains(e.target))) closeMapPop();
  });
  document.addEventListener('keydown', e => {
    if (e.key === 'Escape' && mapPop.pop && mapPop.pop.isConnected) { e.preventDefault(); closeMapPop(true); }
  });
  // A table row (an NPC's, a monster's) on the same page lights up its markers while hovered or
  // focused: its first link against the markers' data-to.
  const mapRowLight = on => e => {
    const tr = e.target.closest && e.target.closest('.tablebox tr');
    const a = tr && $('a[href^="#/"]', tr);
    if (!a || !$('.figmap')) return;
    for (const mk of $$(`.figmap .mark[data-to~="${a.getAttribute('href').replace(/["\\]/g, '\\$&')}"]`)) mk.classList.toggle('hl', on);
  };
  document.addEventListener('mouseover', mapRowLight(true));
  document.addEventListener('mouseout', mapRowLight(false));
  document.addEventListener('focusin', mapRowLight(true));
  document.addEventListener('focusout', mapRowLight(false));
  // A builder block: {stats: [[key, label, unit]], base: {key: n}, groups: [{title, slots, options:
  // [{id, label, fx, mods: {key: n}, notes, min, max, ep, gold, gems: [[name, n]], artisan}]}], note}.
  // Each group has a picker per slot (an item's upgrades, in the order they're done); a slot offers
  // the options whose min..max includes the number of slots before it, and opens once the slot before
  // it is filled. An artisan upgrade picked (artisan: {counts: [{count, pct}], opts: [{id, label,
  // mods: {key: [low, high]}, pct}]}) adds pickers for what it gave, as many as it can give. Under
  // them, what the picks cost, and every stat the item has or the picks change: before, after (a
  // range where an artisan result's value is random) and the change; then other effects.
  function drawBuilder(el, r) {
    const b = r.b, groups = b.groups || [];
    r.art = r.art || {};
    const fmt = (v, unit) => (Math.round(v * 100) / 100) + (unit || '');
    const signed = (v, unit) => (v > 0 ? '+' : '') + fmt(v, unit);
    const span = (lo, hi, f) => (lo === hi ? f(lo) : `${f(lo)}~${f(hi).replace(/^\+/, '')}`);
    const pct = p => (Math.round(p * 10) / 10) + '%';
    const total = {}, notes = [], cost = { ep: 0, gold: 0, gems: {} }, arts = [];
    const add = (k, lo, hi) => { const t = total[k] || [0, 0]; total[k] = [t[0] + lo, t[1] + hi]; };
    groups.forEach((g, gi) => r.picks[gi].forEach((id, si) => {
      const o = id && g.options.find(x => x.id === id);
      if (!o) return;
      for (const [k, v] of Object.entries(o.mods || {})) add(k, v, v);
      for (const n of o.notes || []) notes.push(`${o.label}: ${n}`);
      cost.ep += o.ep || 0;
      cost.gold += o.gold || 0;
      for (const [name, n] of o.gems || []) cost.gems[name] = (cost.gems[name] || 0) + n;
      if (o.artisan) {
        // Its results' picks, kept by slot and upgrade.
        const key = `${gi}:${si}:${o.id}`, max = Math.max(...o.artisan.counts.map(c => c.count));
        const picks = r.art[key] = (r.art[key] || []).slice(0, max);
        while (picks.length < max) picks.push('');
        arts.push({ key, o, picks });
        for (const pid of picks) {
          const x = pid && o.artisan.opts.find(y => y.id === pid);
          if (x) for (const [k, v] of Object.entries(x.mods)) add(k, v[0], v[1]);
        }
      }
    }));
    // The special upgrade picked (special: {levels: [{level, gold, rate}], types: [{key, label, stone,
    // delta, bonus: [{key, label, unit, values}]}]}): an S or R weapon has its own type; a weapon with
    // S and R versions picks one (the first stone makes it that version at level 1, its base stats
    // changing by delta). Then the level's bonuses, and the cost when every try succeeds.
    const sp = b.special;
    if (sp && sp.types.length === 1) r.spType = sp.types[0].key;
    const spT = sp && sp.types.find(t => t.key === r.spType), spLv = spT ? +(r.special || 0) : 0;
    if (spT && spLv) {
      for (const [k, v] of Object.entries(spT.delta || {})) add(k, v, v);
      for (const x of spT.bonus) { const v = x.values[spLv - 1]; if (v) add(x.key, v, v); }
      for (const l of sp.levels) if (l.level <= spLv) cost.gold += l.gold;
      cost.gems[spT.stone] = (cost.gems[spT.stone] || 0) + spLv;
    }
    let h = groups.map((g, gi) => {
      const picks = r.picks[gi];
      const slots = picks.map((id, si) => {
        const open = si === 0 || !!picks[si - 1];
        const opts = g.options.filter(o => o.min <= si && si <= o.max), on = opts.find(o => o.id === id);
        // The list says each upgrade and its effect; the closed box only its name (data-short, see
        // fitSelects), with the effect under it.
        return `<label class="bslot${open ? '' : ' off'}"><span>${esc(g.slots > 1 ? `${g.title.replace(/s$/, '')} ${si + 1}` : g.title.replace(/s$/, ''))}</span>` +
          `<select data-g="${gi}" data-s="${si}"${open ? '' : ' disabled'}><option value="">—</option>` +
          opts.map(o => `<option value="${esc(o.id)}" data-short="${esc(o.label)}"${o.id === id ? ' selected' : ''}>${esc(o.label)}${o.fx ? ' · ' + esc(o.fx) : ''}</option>`).join('') + '</select>' +
          (on && on.fx ? `<small class="bsub">${tonesHtml(on.fx)}</small>` : '') + '</label>';
      }).join('');
      return `<div class="bgroup"><h4>${esc(g.title)} <small>${fmtNum(picks.filter(Boolean).length)} / ${fmtNum(g.slots)}</small></h4><div class="bslots">${slots}</div></div>`;
    }).join('');
    // An artisan upgrade's results: a picker each, offering what the others haven't taken.
    h += arts.map(a => {
      const c = a.o.artisan, n = c.counts.map(x => `${x.count} (${pct(x.pct)})`);
      const slots = a.picks.map((pid, si) => {
        const open = si === 0 || !!a.picks[si - 1], taken = new Set(a.picks.filter((p, j) => p && j !== si));
        return `<label class="bslot${open ? '' : ' off'}"><span>Result ${si + 1}</span><select data-a="${esc(a.key)}" data-s="${si}"${open ? '' : ' disabled'}><option value="">—</option>` +
          c.opts.filter(x => !taken.has(x.id)).map(x => `<option value="${esc(x.id)}" data-short="${esc(x.label)}"${x.id === pid ? ' selected' : ''}>${esc(x.label)} (${pct(x.pct)} to appear)</option>`).join('') + '</select></label>';
      }).join('');
      return `<div class="bgroup"><h4>${esc(a.o.label)} results <small>${fmtNum(a.picks.filter(Boolean).length)} / ${fmtNum(a.picks.length)}</small></h4>` +
        `<p class="bhint">It gives ${n.length > 1 ? n.slice(0, -1).join(', ') + ' or ' + n[n.length - 1] : n[0]} different results, each value random within its range: pick the ones it gave. "To appear" is how likely that result is to be among them.</p><div class="bslots">${slots}</div></div>`;
    }).join('');
    if (sp) {
      const many = sp.types.length > 1;
      const typeSel = many ? `<label class="bslot"><span>Version</span><select data-spt="1"><option value="">—</option>` +
        sp.types.map(t => `<option value="${esc(t.key)}" data-short="${esc(t.label)} · ${esc(t.stone)}"${t.key === r.spType ? ' selected' : ''}>${esc(t.label)} · ${esc(t.stone)}</option>`).join('') + '</select></label>' : '';
      // The list says each level's bonuses and odds; the closed box the level and its odds, the bonuses
      // under it.
      const lvText = l => spT.bonus.map(x => `${x.label} ${signed(x.values[l.level - 1] || 0, x.unit)}`).join(', ');
      const opts = spT ? sp.levels.map(l => `<option value="${l.level}" data-short="Level ${l.level} · ${l.rate}% to succeed"${l.level === spLv ? ' selected' : ''}>Level ${l.level} · ${esc(lvText(l))} (${l.rate}% to succeed)</option>`).join('') : '';
      const lvBonus = spT && spLv ? spT.bonus.map(x => `${x.label} ${signed(x.values[spLv - 1] || 0, x.unit)}`).join(', ') : '';
      const hint = many ? (spT ? `The first ${spT.stone} makes it ${spT.label} at level 1; each level after takes another.` : 'Pick the version a Blue (S) or Red (R) Upgrade Stone makes it.')
        : `Each level takes one ${spT.stone}.`;
      h += `<div class="bgroup"><h4>Special upgrade</h4><p class="bhint">${esc(hint)} The cost counts every try as a success.</p>` +
        `<div class="bslots">${typeSel}<label class="bslot${spT ? '' : ' off'}"><span>Level</span><select data-sp="1"${spT ? '' : ' disabled'}><option value="0">—</option>${opts}</select>${lvBonus ? `<small class="bsub">${tonesHtml(lvBonus)}</small>` : ''}</label></div></div>`;
    }
    const shown = (b.stats || []).filter(([k]) => b.base[k] !== undefined || (total[k] && (total[k][0] || total[k][1])));
    const rows = shown.map(([k, label, unit]) => {
      const has = b.base[k] !== undefined, before = b.base[k] || 0, [lo, hi] = total[k] || [0, 0], d = lo || hi;
      const after = has ? span(before + lo, before + hi, v => fmt(v, unit)) : span(lo, hi, v => signed(v, unit));
      // Blank, not "—", where there's nothing (no base number, no change).
      return `<tr class="${d ? 'ch' : ''}"><td>${esc(label)}</td><td class="num">${has ? fmt(before, unit) : ''}</td><td class="num"><b>${after}</b></td>` +
        `<td class="num ${lo >= 0 && hi > 0 ? 'up' : hi <= 0 && lo < 0 ? 'down' : ''}">${d ? span(lo, hi, v => signed(v, unit)) : ''}</td></tr>`;
    }).join('');
    const gems = Object.entries(cost.gems).map(([name, n]) => `${esc(name)} ×${fmtNum(n)}`).join(', ');
    // Laid out flat: a few rows, never a scroll box of its own.
    h += `<div class="bres"><div class="bcost"><span>Total cost</span><span>Proficiency <b>${fmtNum(cost.ep)}</b></span><span>Gold <b>${fmtNum(cost.gold)} G</b></span>` +
      `${gems ? `<span>Materials <b>${gems}</b></span>` : ''}</div>` +
      `<table class="t"><thead><tr><th>Stat</th><th class="num">Base</th><th class="num">After</th><th class="num">Change</th></tr></thead><tbody>${rows}</tbody></table>` +
      (notes.length ? `<ul class="lst">${notes.map(n => `<li>${esc(n)}</li>`).join('')}</ul>` : '') +
      `<div class="bfoot"><button type="button" class="linkbtn breset">Clear the upgrades</button>${b.note ? `<small>${esc(b.note)}</small>` : ''}</div></div>`;
    el.innerHTML = h;
    fitSelects(el);
    for (const s of $$('select[data-sp]', el)) s.onchange = () => { r.special = +s.value; drawBuilder(el, r); };
    // A version picked starts at level 1 (the stone that makes it); none clears the level.
    for (const s of $$('select[data-spt]', el)) s.onchange = () => { r.spType = s.value; r.special = s.value ? Math.max(1, +(r.special || 0)) : 0; drawBuilder(el, r); };
    for (const s of $$('select[data-a]', el)) s.onchange = () => {
      const p = r.art[s.dataset.a], si = +s.dataset.s;
      p[si] = s.value;
      for (let j = si + 1; j < p.length; j++) if (!p[j - 1]) p[j] = '';
      drawBuilder(el, r);
    };
    for (const s of $$('select[data-g]', el)) s.onchange = () => {
      const gi = +s.dataset.g, si = +s.dataset.s, picks = r.picks[gi], g = groups[gi];
      picks[si] = s.value;
      // Later picks stay while their slot still offers them and the slots before are filled.
      for (let j = si + 1; j < picks.length; j++) {
        if (!picks[j - 1]) { picks[j] = ''; continue; }
        const o = g.options.find(x => x.id === picks[j]);
        if (o && !(o.min <= j && j <= o.max)) picks[j] = '';
      }
      drawBuilder(el, r);
    };
    $('.breset', el).onclick = () => { r.picks = groups.map(g => Array(g.slots).fill('')); r.art = {}; r.special = 0; r.spType = ''; drawBuilder(el, r); };
  }
  // A tabs block: a switch between sets of blocks (a skill's ranks per race). With `remember`,
  // the tab picked (by its label) is kept for the next page with the same key. A ranks block in
  // the tab shown next keeps the rank the last one had picked. Keyboard: the chosen tab is the one
  // Tab stops on, and the arrow keys, Home and End pick another (the usual tab-list keys).
  let tabUid = 0;
  function drawTabs(el, r) {
    const tabs = (r.b.tabs || []).filter(isObj);
    if (!tabs.length) { el.innerHTML = ''; return; }
    const sub = [];
    const inner = (tabs[r.cur].blocks || []).map(x => block(x, sub)).join('');
    const line = r.b.style === 'line';
    if (r.uid == null) r.uid = ++tabUid;
    const id = i => `tab${r.uid}-${i}`, pid = `tabp${r.uid}`;
    const lab = r.b.label && !line ? `<span class="tablab">${esc(r.b.label)}</span>` : '';
    // A tab's own name for screen readers when it has more than its label (the lock, the count).
    const aria = t => (t.locked || t.count != null ? ` aria-label="${esc(t.label + (t.locked ? ', secret' : '') + (t.count != null ? `, ${t.count}` : ''))}"` : '');
    const inside = t => `<span class="tl">${esc(t.label)}</span>` + (t.locked ? `<span class="tlock" title="Secret: only shows under a condition">${svg('lock')}</span>` : '') +
      (t.count != null ? `<span class="tn">${fmtNum(t.count)}</span>` : '');
    const hadFocus = el.contains(document.activeElement) && document.activeElement.matches('.tab');
    el.innerHTML = `<div class="tabbar" role="tablist"${r.b.label ? ` aria-label="${esc(r.b.label)}"` : ''}>${lab}` +
      tabs.map((t, i) => `<button type="button" role="tab" id="${id(i)}" aria-controls="${pid}" data-i="${i}" class="tab${i === r.cur ? ' on' : ''}${t.locked ? ' locked' : ''}" aria-selected="${i === r.cur}" tabindex="${i === r.cur ? 0 : -1}"${aria(t)}>${inside(t)}</button>`).join('') +
      `</div><div class="tabbody" role="tabpanel" id="${pid}" aria-labelledby="${id(r.cur)}">${inner}</div>`;
    if (r.rank != null) for (const o of sub) if (o.kind === 'ranks') { const j = (o.b.labels || []).indexOf(r.rank); if (j >= 0) o.cur = j; }
    bindBlocks($('.tabbody', el), sub);
    const pick = j => {
      const o = sub.find(x => x.kind === 'ranks' && x.el);
      if (o) r.rank = (o.b.labels || [])[o.cur];
      r.cur = j;
      if (r.b.remember) store.set('tab-' + r.b.remember, tabs[r.cur].label);
      drawTabs(el, r);
    };
    for (const btn of $$('.tabbar .tab', el)) btn.onclick = () => pick(+btn.dataset.i);
    $('.tabbar', el).onkeydown = e => {
      const n = tabs.length, j = { ArrowRight: r.cur + 1, ArrowLeft: r.cur - 1, Home: 0, End: n - 1 }[e.key];
      if (j == null || !e.target.matches('.tab')) return;
      e.preventDefault();
      pick((j + n) % n);
    };
    // The bar is drawn anew on each pick: keep the keyboard on the chosen tab, and in a strip that
    // scrolls sideways (a phone), keep that tab in view.
    const on = $('.tab.on', el);
    if (hadFocus && on) { on.focus({ preventScroll: true }); on.scrollIntoView({ block: 'nearest', inline: 'nearest' }); }
  }
  function drawTable(el, r) {
    const cols = r.b.columns || [];
    let rows = (r.b.rows || []).slice();
    if (r.sort >= 0) {
      const k = r.sort;
      rows = rows.map(x => [sortVal(x[k]), x]).sort((a, b) => cmp(a[0], b[0]) * r.dir).map(x => x[1]);
    }
    const num = cols.map((_, i) => rows.length > 0 && rows.every(x => x[i] == null || x[i] === '' || typeof x[i] === 'number'));
    const shown = r.all ? rows : rows.slice(0, MAX_ROWS);
    // An ID column is shown first (indexes stay the data's, for sorting).
    const idc = cols.findIndex(c => /^(id|ids|class id)$/i.test(plain(c).trim()));
    // A column with nothing in any row (all blank or "—") isn't shown.
    const blank = v => v == null || v === '' || (Array.isArray(v) && !v.length) || /^\s*—?\s*$/.test(plain(v));
    const order = cols.map((_, i) => i).filter(i => rows.length === 0 || !rows.every(x => blank(x[i])));
    if (idc > 0 && order.includes(idc)) { order.splice(order.indexOf(idc), 1); order.unshift(idc); }
    // Three or more columns: on a phone each row is a card of "column: value" lines (data-label).
    const stack = order.length >= 3 ? ' stack' : '';
    // phone: {lead: [column indexes], fold: [column indexes], label}: on a phone's cards the lead columns
    // come first and the fold columns hide behind a "Show the N <label>" button (upgrade paths: the goal
    // and the final stats first, the upgrades folded). The desktop table is unchanged.
    const ph = r.b.phone, lead = new Set((ph && ph.lead) || []), fold = new Set((ph && ph.fold) || []);
    const pcls = i => (lead.has(i) ? ' pl' : '') + (fold.has(i) ? ' pf' : '');
    const ptoggle = x => (ph && stack ? `<td class="ptg"><button type="button" class="linkbtn ptgb">Show the ${order.filter(i => fold.has(i) && !blank(x[i])).length} ${esc(ph.label || 'more')}</button></td>` : '');
    el.innerHTML = `<div class="scroll"><table class="t${stack}${ph && stack ? ' phone-fold' : ''}"><thead><tr>${order.map(i => `<th class="s${num[i] ? ' num' : ''}${r.sort === i ? ' on' : ''}" data-i="${i}">${esc(cols[i])}${r.sort === i ? `<span class="ar">${r.dir < 0 ? '▼' : '▲'}</span>` : ''}</th>`).join('')}</tr></thead>` +
      `<tbody>${shown.map(x => `<tr>${order.map(i => `<td data-label="${esc(plain(cols[i]))}" class="${num[i] ? 'num' : ''}${x[i] != null && !Array.isArray(x[i]) && plain(x[i]).length <= 12 ? ' nw' : ''}${plain(x[i]).length > 22 ? ' long' : ''}${blank(x[i]) ? ' blank' : ''}${pcls(i)}">${blank(x[i]) ? '' : stack ? `<div class="cv">${valRow(cols[i], x[i])}</div>` : valRow(cols[i], x[i])}</td>`).join('')}${ptoggle(x)}</tr>`).join('')}</tbody></table></div>` +
      (rows.length > shown.length ? `<div class="tmore"><button class="linkbtn">Show all ${fmtNum(rows.length)} rows</button></div>` : '');
    for (const th of $$('th', el)) th.onclick = () => {
      const i = +th.dataset.i;
      if (r.sort === i) r.dir = -r.dir; else { r.sort = i; r.dir = 1; }
      drawTable(el, r);
    };
    for (const bt of $$('.ptgb', el)) bt.onclick = () => {
      const tr = bt.closest('tr'), open = tr.classList.toggle('open');
      bt.textContent = open ? bt.textContent.replace(/^Show/, 'Hide') : bt.textContent.replace(/^Hide/, 'Show');
    };
    const more = $('.tmore .linkbtn', el);
    if (more) more.onclick = () => { r.all = true; drawTable(el, r); };
    fitAll(el);
  }
  // Links as chips (default), or a list with icons ('list'), rank badges with counts ('ranks'),
  // or rows with a count on the right ('rows').
  function linkItem(style, v) {
    if (style === 'chips' || !isObj(v)) return chip(v);
    const to = target(v), lab = esc(v.label != null ? v.label : (v.id != null ? v.id : ''));
    const tag = (cls, inner) => to ? `<a class="${cls}" href="${esc(to)}">${inner}</a>` : `<span class="${cls} dim">${inner}</span>`;
    if (style === 'ranks') return tag('rkcell', `<span class="rk">${esc(v.mark != null ? v.mark : '')}</span><span>${v.count != null ? fmtNum(v.count) : lab}</span>`);
    const art = v.icon ? ibox(v.icon, 32, v.t) : `<span class="gl">${svg(v.glyph || (v.t ? typeIcon(v.t) : 'dot'))}</span>`;
    if (style === 'list') return tag('sk', `${art}<span>${lab}</span>`);
    const right = v.tag ? `<span class="soon">${esc(v.tag)}</span>` : v.count != null ? fmtNum(v.count) : (v.sub ? esc(v.sub) : '');
    return tag('rowl',`${art}<span class="tx">${lab}</span><span class="n">${right}</span>`);
  }
  function drawLinks(el, r) {
    const items = r.b.items || [];
    const style = ['list', 'ranks', 'rows'].includes(r.b.style) ? r.b.style : 'chips';
    const shown = r.all ? items : items.slice(0, r.b.oneRow ? 200 : MAX_CHIPS);
    const wrap = { chips: 'chips', list: 'sklist', ranks: 'rkgrid', rows: 'rowls' }[style];
    el.innerHTML = `<div class="${wrap}">${shown.map(v => linkItem(style, v)).join('')}</div>` +
      (items.length > shown.length ? `<div class="more"><button class="linkbtn">Show all ${fmtNum(items.length)}</button></div>` : '');
    const more = $('.linkbtn', el);
    if (more) more.onclick = () => { r.all = true; drawLinks(el, r); };
    fitAll(el);
    // oneRow: only the chips that fit on the first line, then "Show all". Measured once laid out, and
    // again when the window is resized.
    if (r.b.oneRow && !r.all && style === 'chips') {
      const cutRow = () => {
        if (r.all || !el.isConnected) return;
        const box = el.firstElementChild, kids = [...box.children];
        kids.forEach(k => { k.style.display = ''; });
        const top = kids.length ? kids[0].offsetTop : 0;
        const cut = kids.filter(k => k.offsetTop > top + 4);
        cut.forEach(k => { k.style.display = 'none'; });
        const old = $('.more', el);
        if (old) old.remove();
        if (cut.length) {
          el.insertAdjacentHTML('beforeend', `<div class="more"><button class="linkbtn">Show all ${fmtNum(items.length)}</button></div>`);
          $('.linkbtn', el).onclick = () => { r.all = true; drawLinks(el, r); };
        }
      };
      requestAnimationFrame(cutRow);
      if (!r.onResize) { r.onResize = () => requestAnimationFrame(cutRow); window.addEventListener('resize', r.onResize); }
    }
  }
  // A ranks block: a rank picker (optionally in `groups` [[label, from, to]], with `short`
  // button labels), the picked rank's row as cards, then (optional) `blocks[rank]`, any blocks
  // for that rank alone, and unless `table` is false a toggle for the table of every rank.
  // `start` is the rank picked first (default the last).
  function drawRanks(el, r) {
    const cols = r.b.columns || [], rows = r.b.rows || [], per = r.b.blocks || [];
    const n = Math.max(rows.length, per.length);
    const labels = r.b.labels || Array.from({ length: n }, (_, i) => String(i + 1));
    const short = Array.isArray(r.b.short) ? r.b.short : labels;
    if (!n) { el.innerHTML = '<p class="muted">No rank data.</p>'; return; }
    const cur = rows[r.cur] || [];
    const table = r.b.table !== false && rows.length && cols.length;
    const sub = [];
    const subHtml = (per[r.cur] || []).filter(b => b && b.kind !== 'ranks').map(b => block(b, sub, 'rank')).join('');
    const btn = i => i < n ? `<button type="button" data-i="${i}" class="rk${i === r.cur ? ' on' : ''}" title="${esc(labels[i] != null ? labels[i] : i + 1)}" aria-pressed="${i === r.cur}">${esc(short[i] != null ? short[i] : labels[i])}</button>` : '';
    const groups = Array.isArray(r.b.groups) ? r.b.groups.filter(g => Array.isArray(g) && g.length >= 3) : [];
    const range = (a, z) => { const o = []; for (let i = Math.max(0, +a); i <= Math.min(n - 1, +z); i++) o.push(i); return o; };
    const picker = groups.length
      ? `<div class="picker rpick" role="group" aria-label="Rank">${groups.map(g => `<div class="rgroup"><span>${esc(g[0])}</span><div>${range(g[1], g[2]).map(btn).join('')}</div></div>`).join('')}</div>`
      : `<div class="picker" role="group" aria-label="Rank">${labels.map((_, i) => btn(i)).join('')}</div>`;
    el.innerHTML = picker +
      // A value of several lines (the rank-up bonus, a stat to a line) is as wide as its longest line.
      (cols.length && rows.length ? `<div class="statbar rcards">${cols.map((c, i) => { const len = Math.max(0, ...plain(cur[i]).split('; ').map(s => s.length)); return `<div class="${len > 28 ? 'full' : len > 12 ? 'wide' : ''}"><span>${esc(c)}</span><b>${val(cur[i])}</b></div>`; }).join('')}</div>` : '') +
      (subHtml ? `<div class="rsub">${subHtml}</div>` : '') +
      (table ? `<button class="linkbtn">${r.all ? 'Hide' : 'Show'} the table of every rank</button>` : '') +
      (table && r.all ? `<div class="tablebox rtable"><div class="scroll"><table class="t"><thead><tr><th>Rank</th>${cols.map(c => `<th>${esc(c)}</th>`).join('')}</tr></thead><tbody>${rows.map((x, i) => `<tr class="${i === r.cur ? 'on' : ''}"><td><b>${esc(labels[i] || i + 1)}</b></td>${cols.map((_, j) => `<td>${val(x[j])}</td>`).join('')}</tr>`).join('')}</tbody></table></div></div>` : '');
    const rs = $('.rsub', el);
    if (rs) bindBlocks(rs, sub);
    fitAll(el);
    for (const b of $$('.picker button', el)) b.onclick = () => {
      // every ranks block on the page with the same rank label follows
      const lab = labels[+b.dataset.i];
      r.cur = +b.dataset.i;
      for (const o of r.reg || [r]) {
        if (o.kind !== 'ranks' || !o.el) continue;
        if (o !== r) {
          const ls = o.b.labels || (o.b.rows || []).map((_, i) => String(i + 1));
          const j = ls.indexOf(lab);
          if (j < 0) continue;
          o.cur = j;
        }
        drawRanks(o.el, o);
      }
    };
    const tog = table ? $(':scope > .linkbtn', el) : null;
    if (tog) tog.onclick = () => { r.all = !r.all; drawRanks(el, r); };
  }

  // ---------- search ----------
  // An exact name match on these types ranks above the same match on the rest (the Gray Wolf
  // monster before the Gray Wolf enchant).
  const PRIMARY_TYPES = new Set(['monster', 'item', 'npc', 'quest', 'region', 'dungeon', 'skill']);
  function score(r, q, toks) {
    const n = r.n;
    let s = 0;
    if (n === q) s = PRIMARY_TYPES.has(r.t) ? 1040 : 1000;
    else if (r.id === q) s = 950;
    else if (n.startsWith(q)) s = 800;
    else if ((' ' + n).includes(' ' + q)) s = 650;
    else if (n.includes(q)) s = 500;
    else if (r.a && r.a.includes(q)) s = 450;
    else if (r.id.startsWith(q)) s = 400;
    // The words together in the tags or effects ("max damage") before the words anywhere.
    else if (toks.length > 1 && r.g && r.g.includes(q)) s = 320;
    else if (toks.length > 1 && toks.every(k => n.includes(k) || r.a.includes(k) || r.g.includes(k))) s = 300;
    else if (r.g && r.g.includes(q)) s = 250;
    else if (toks.length > 1 && n.replace(/\s+/g, '').includes(q.replace(/\s+/g, ''))) s = 280;
    else if (q.length >= 2) {
      // fuzzy: the query's letters in order in the name
      let i = 0, gaps = 0, last = -1;
      for (let j = 0; j < n.length && i < q.length; j++) if (n[j] === q[i]) { if (last >= 0) gaps += j - last - 1; last = j; i++; }
      if (i === q.length && gaps <= q.length * 3) s = 100 - Math.min(80, gaps * 4);
    }
    if (!s) return 0;
    return s - Math.min(60, n.length * 0.4);
  }
  function searchRows(rows, query, limit) {
    const q = query.trim().toLowerCase();
    if (!q) return [];
    const toks = q.split(/\s+/).filter(Boolean);
    const out = [];
    for (const r of rows) { const s = score(r, q, toks); if (s > 0) out.push([s, r]); }
    out.sort((a, b) => b[0] - a[0]);
    return limit ? out.slice(0, limit).map(x => x[1]) : out.map(x => x[1]);
  }
  function hl(text, q) {
    const s = String(text || '');
    const i = q ? s.toLowerCase().indexOf(q.toLowerCase()) : -1;
    if (i < 0 || !q) return esc(s);
    return esc(s.slice(0, i)) + '<mark>' + esc(s.slice(i, i + q.length)) + '</mark>' + esc(s.slice(i + q.length));
  }
  // Every word of the query marked in the text (for effects: "max damage" marks both words).
  function hlAll(text, q) {
    const words = String(q || '').toLowerCase().split(/\s+/).filter(w => w.length > 1);
    let out = esc(text);
    for (const w of words) out = out.replace(new RegExp('(' + esc(w).replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + ')(?![^<]*>)', 'gi'), '<mark>$1</mark>');
    return out;
  }
  function searchPage(view, query) {
    setTitle(query ? `Search: ${query}` : 'Search');
    view.innerHTML = `${crumbs([['Search']])}<div class="lhead"><div class="t"><h1>Search</h1><p id="scount"></p></div></div>
      <div class="toolbar"><label class="filter">${svg('search')}<input id="sq" type="search" placeholder="Search everything…" value="${esc(query)}" autocomplete="off" aria-label="Search"></label></div><div id="sres" class="results"><div class="boot">Loading the index…</div></div>`;
    const inp = $('#sq');
    const run = () => loadSearch().then(rows => {
      const box = $('#sres');
      if (!box) return;
      const q = inp.value.trim();
      if (!q) { box.innerHTML = emptyState({ title: 'Type to search.', text: 'Names, Korean names, IDs and tags all count.' }, 'search'); $('#scount').textContent = ''; return; }
      const res = searchRows(rows, q, 3000);
      $('#scount').textContent = `${fmtNum(res.length)}${res.length >= 3000 ? '+' : ''} results`;
      const groups = new Map();
      for (const r of res) { if (!groups.has(r.t)) groups.set(r.t, []); groups.get(r.t).push(r); }
      let h = '';
      for (const [t, list] of groups) {
        h += `<div class="sgroup"><h2>${svg(typeIcon(t))}${esc(typeTitle(t))}<span class="n">${fmtNum(list.length)}</span>${list.length > 25 ? `<a class="all" href="${listHref(t, { q })}">See all</a>` : ''}</h2><div class="rows">` +
          list.slice(0, 25).map(r => `<a class="rowlink" href="${href(r.t, r.id)}">${ibox(r.icon, 32, r.t)}<span class="tx"><b>${hl(r.name || r.id, q)}</b><small>${hl(r.alt, q)}${r.sub ? (r.alt ? ' · ' : '') + esc(r.sub) : ''}</small></span>${r.fx ? `<span class="fx">${hlAll(r.fx, q)}</span>` : ''}<span class="meta">#${esc(r.id)}</span></a>`).join('') + '</div></div>';
      }
      box.innerHTML = h || emptyState({ title: `Nothing found for “${q}”.`, text: 'Try fewer letters, or another name.' }, 'search');
      fitAll(box);
    });
    inp.oninput = debounce(() => { history.replaceState(null, '', '#/search?q=' + encodeURIComponent(inp.value.trim())); palette.setLast(inp.value); run(); }, 160);
    if (query) palette.setLast(query);
    run();
    if (!query) inp.focus();
  }

  // ---------- command palette ----------
  const palette = (() => {
    // last: what was last searched, kept in the box when it opens again and shown in the header's
    // search button (its hint otherwise).
    let el = null, sel = 0, items = [], last = '';
    function setLast(q) {
      last = String(q || '').trim();
      const s = $('#searchbtn span');
      if (!s) return;
      if (s.dataset.hint === undefined) s.dataset.hint = s.textContent;
      s.textContent = last || s.dataset.hint;
      s.classList.toggle('typed', !!last);
    }
    function close() { if (el) { el.remove(); el = null; document.body.style.overflow = ''; } }
    function open(initial) {
      if (el) { $('input', el).select(); return; }
      el = document.createElement('div');
      el.className = 'pal-bg';
      el.innerHTML = `<div class="pal" role="dialog" aria-label="Search"><div class="pin">${svg('search')}<input type="text" placeholder="Search everything…" autocomplete="off" spellcheck="false" aria-label="Search"><kbd>Esc</kbd></div><div class="res"></div>
        <div class="pfoot"><span><kbd>↑</kbd><kbd>↓</kbd> move</span><span><kbd>↵</kbd> open</span><span><kbd>Ctrl</kbd><kbd>↵</kbd> all results</span><span><kbd>Esc</kbd> close</span></div></div>`;
      document.body.appendChild(el);
      el.addEventListener('mousedown', e => { if (e.target === el) close(); });
      const inp = $('input', el);
      inp.value = initial || last;
      inp.addEventListener('input', debounce(() => { setLast(inp.value); draw(inp.value); }, 60));
      inp.addEventListener('keydown', e => {
        if (e.key === 'ArrowDown') { e.preventDefault(); move(1); }
        else if (e.key === 'ArrowUp') { e.preventDefault(); move(-1); }
        else if (e.key === 'PageDown') { e.preventDefault(); move(8); }
        else if (e.key === 'PageUp') { e.preventDefault(); move(-8); }
        else if (e.key === 'Enter') {
          e.preventDefault();
          if (e.ctrlKey || e.metaKey) { go('#/search?q=' + encodeURIComponent(inp.value.trim())); return; }
          const it = items[sel]; if (it) go(it.h);
        } else if (e.key === 'Escape') { e.preventDefault(); close(); }
      });
      inp.focus();
      inp.select(); // the last search stays, ready to be typed over
      draw(inp.value);
      if (!S.search) loadSearch().then(() => { if (el) draw($('input', el).value); });
    }
    function go(h) { close(); if (location.hash === h) route(); else location.hash = h; }
    function move(d) {
      if (!items.length) return;
      sel = Math.max(0, Math.min(items.length - 1, sel + d));
      paint();
    }
    function paint() {
      for (const [i, n] of $$('.it', el).entries()) {
        n.classList.toggle('on', i === sel);
        if (i === sel) n.scrollIntoView({ block: 'nearest' });
      }
    }
    function draw(qv) {
      if (!el) return;
      const res = $('.res', el);
      const q = qv.trim();
      items = []; sel = 0;
      let h = '';
      if (!q) {
        const recent = store.get('recent', []);
        if (recent.length) {
          h += '<div class="h">Recent</div>';
          for (const r of recent.slice(0, 6)) items.push({ h: href(r[0], r[1]), html: row(r[3], r[0], esc(r[2]), '', typeTitle(r[0])) });
        }
        h += items.map((x, i) => itemHtml(x, i)).join('');
        const start = items.length;
        const jump = [];
        for (const s of S.meta.sections) for (const t of s.types || []) if (S.meta.types[t]) jump.push({ h: listHref(t), html: row(null, t, esc(S.meta.types[t].title), `${fmtNum(S.meta.types[t].count)} entries`, s.title, typeIcon(t)) });
        items.push(...jump);
        if (jump.length) h += '<div class="h">Browse</div>' + jump.map((x, i) => itemHtml(x, start + i)).join('');
      } else if (!S.search) {
        h = '<div class="pempty">Loading the search index…</div>';
      } else {
        const found = searchRows(S.search, q, 50);
        for (const r of found) items.push({ h: href(r.t, r.id), html: row(r.icon, r.t, hl(r.name || r.id, q), (r.alt && r.alt !== r.name ? hl(r.alt, q) : '') + (r.sub ? (r.alt && r.alt !== r.name ? ' · ' : '') + esc(r.sub) : '') + ` <span class="muted">#${esc(r.id)}</span>` + (r.fx ? `<span class="pfx">${hlAll(r.fx, q)}</span>` : ''), typeTitle(r.t)) });
        items.push({ h: '#/search?q=' + encodeURIComponent(q), html: row(null, 'search', `See all results for “${esc(q)}”`, 'Ctrl+Enter', '', 'search') });
        h = (found.length ? '' : '<div class="pempty">No matches.</div>') + items.map((x, i) => itemHtml(x, i)).join('');
      }
      res.innerHTML = h;
      for (const n of $$('.it', res)) {
        n.onclick = () => go(items[+n.dataset.i].h);
        n.onmousemove = () => { if (sel !== +n.dataset.i) { sel = +n.dataset.i; paint(); } };
      }
      paint();
      fitAll(res);
    }
    function row(icon, t, title, sub, badge, iconName) {
      const box = icon ? `<span class="ibox${iconKind(icon) === 'skill' ? ' skill' : ''}">${img(icon, 30)}</span>` : `<span class="ibox none">${svg(iconName || typeIcon(t))}</span>`;
      return `${box}<span class="tx"><b>${title}</b>${sub ? `<small>${sub}</small>` : ''}</span>${badge ? `<span class="ty">${esc(badge)}</span>` : ''}`;
    }
    function itemHtml(x, i) { return `<div class="it" data-i="${i}">${x.html}</div>`; }
    return { open, close, setLast, get isOpen() { return !!el; } };
  })();

  document.addEventListener('keydown', e => {
    if ((e.ctrlKey || e.metaKey) && (e.key === 'k' || e.key === 'K')) { e.preventDefault(); palette.isOpen ? palette.close() : palette.open(); return; }
    const tag = (e.target.tagName || '').toLowerCase();
    if (e.key === '/' && !palette.isOpen && tag !== 'input' && tag !== 'textarea' && tag !== 'select') { e.preventDefault(); palette.open(); }
  });

  // ---------- boot ----------
  // The frame is drawn before the meta arrives: its name, badge and search hint follow it.
  function brand() {
    const m = S.meta || {}, bt = $('.brand .bt'), badge = $('.brand small'), hint = $('#searchbtn span');
    if (bt) bt.textContent = wikiName();
    if (badge) {
      badge.textContent = m.badge || 'local';
      // A badge that only repeats the name ("Wiki" in "Mabinogi Wiki") isn't shown.
      badge.hidden = wikiName().toLowerCase().includes(badge.textContent.toLowerCase());
    }
    if (hint && m.searchHint) {
      hint.dataset.hint = m.searchHint;
      if (!hint.classList.contains('typed')) hint.textContent = m.searchHint;
    }
    document.title = wikiName();
  }
  shell();
  loadMeta().then(() => {
    brand();
    route();
    const pq = new URLSearchParams(location.search).get('palette');  // ?palette=text opens the palette (testing)
    if (pq !== null) palette.open(pq);
    // warm the search index once the first view is up
    (window.requestIdleCallback || (f => f()))(() => loadSearch());
  }, err => {
    $('#view').innerHTML = `<div class="notfound"><h1>The wiki isn't built</h1><p class="muted">${esc(err.message)}</p></div>`;
  });
})();
