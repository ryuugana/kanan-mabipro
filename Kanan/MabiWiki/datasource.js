// The Mabinogi wiki's data source (window.WIKI_SOURCE for app.js). It builds the wiki in the
// browser from the game's own files, which the page's setup reads from the game's packs (boot.js,
// prepare.js) and stores next to it in data/game-data.js (make_data.py does the same for the
// development index.html):
//
//   window.GAME_DATA = {version, built, source: {name: {pack, version, size}}, files: {name: text}}
//
// name is the path in the packs below data\, lowercase with "/" (db/itemdb.xml). What it reads:
//   db/itemdb.xml          items (Mabi_Item rows), text in local/xml/itemdb.<locale>.txt
//   db/optionset.xml       enchants (OptionSet rows), text in local/xml/optionset.<locale>.txt
//   db/title.xml           titles (Title rows), text in local/xml/title.<locale>.txt
//   db/skill/skillinfo.xml skills (Skill rows), text in local/xml/skillinfo.<locale>.txt
//   db/skill/skillleveldescription.xml  each skill's ranks: costs, effects, training methods and
//                          Dan advancement, text in local/xml/skillleveldescription.<locale>.txt
//                          (the packs' skill_leveldetail*.xml are older copies the client doesn't
//                          show: Smash rank 1's training texts in the skill window are this file's)
//   db/race.xml            monsters (Race rows), names in local/xml/race.<locale>.txt
//   db/collectingform.xml  what gathering from an animal gives (sheep: wool, hens: eggs)
//   db/charactercondition.xml  the conditions set effects turn on (names in charactercondition.<locale>.txt)
//   db/pet/*.xml, db/vehicledesc.xml, db/aircraftdesc.xml, db/birdpetgiftitem.xml and the pets' AI
//                          (db/ai/local/): pets (see "pets" below)
// Text references are _LT[xml.<table>.<line>]: line <line> of that table ("682<TAB>Wooden Stick").
// The game's client is the "japan" locale (its text tables are local/xml/*.japan.txt, in English
// with some lines left in Korean); rows marked Locale="korea" etc. aren't loaded by it.
//
// Pages show what the game's own files have and leave out what they don't.
(() => {
  'use strict';

  const D = window.GAME_DATA || null;
  const FILES = (D && D.files) || {};
  // Paths to files under data/ are relative to MABIWIKI_BASE: the single-file page
  // (MabiWiki_Standalone.html) sets it to "MabiWiki/", the copy for hosting to "./"; the
  // development index.html leaves it unset.
  const BASE = window.MABIWIKI_BASE || '';
  // A data file's URL (path under data/, e.g. "data/maps/x.jpg"). When the data is the browser's
  // own copy rather than files next to the page, boot.js sets MABIWIKI_URL to give its blob: URL.
  const url = path => (window.MABIWIKI_URL ? window.MABIWIKI_URL(path) : BASE + path);
  // Item icons made from the game's icon textures (images.js) (data/item-icons.js: {item id: file
  // in data/icons/items/}).
  const ICONS = window.GAME_ICONS || {};
  const itemIcon = id => (ICONS[id] ? url('data/icons/items/' + ICONS[id]) : null);
  // Skill icons cut from the skill window's icon atlases (images.js) (data/skill-icons.js:
  // {skill id: file in data/icons/skills/}).
  const SKILL_ICONS = window.GAME_SKILL_ICONS || {};
  const skillIcon = id => (SKILL_ICONS[id] ? url('data/icons/skills/' + SKILL_ICONS[id]) : null);
  const skillLink = (id, label) => ({ t: 'skill', id: String(id), label, icon: skillIcon(String(id)) });
  const itemLink = (id, label) => ({ t: 'item', id: String(id), label, icon: itemIcon(String(id)) });

  // ---------- reading the files ----------
  const ENT = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'" };
  const unesc = s => (s.indexOf('&') < 0 ? s : s.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (m, e) => {
    if (e[0] === '#') return String.fromCodePoint(e[1] === 'x' || e[1] === 'X' ? parseInt(e.slice(2), 16) : +e.slice(1));
    return ENT[e] !== undefined ? ENT[e] : m;
  }));

  // A file's text without its <!-- comments --> (they hold disabled rows and notes).
  const STRIPPED = {};
  const stripped = file => STRIPPED[file] !== undefined ? STRIPPED[file] : (STRIPPED[file] = (FILES[file] || '').replace(/<!--[\s\S]*?-->/g, ''));

  // Every <tag .../> (or <tag ...>) in a file, as {attribute: value}.
  function rows(file, tag) {
    const text = stripped(file), out = [];
    // A ">" inside a quoted value (an OptionDesc's "If Level is > 20") doesn't end the row.
    const re = new RegExp('<' + tag + '\\s((?:[^>"]|"[^"]*")*?)/?>', 'g'), attr = /(\w+)\s*=\s*"([^"]*)"/g;
    let m;
    while ((m = re.exec(text))) {
      const r = {};
      let a;
      attr.lastIndex = 0;
      while ((a = attr.exec(m[1]))) r[a[1]] = unesc(a[2]);
      out.push(r);
    }
    return out;
  }

  // The client's locale, from the text tables' names (local/xml/itemdb.japan.txt: "japan").
  const LOCALE = ((Object.keys(FILES).map(k => /^local\/xml\/itemdb\.(\w+)\.txt$/.exec(k)).find(Boolean) || [])[1] || '').toLowerCase();

  // Text tables: local/xml/<name>.<locale>.txt, "line<TAB>text".
  const TABLES = {};
  function table(name) {
    if (TABLES[name]) return TABLES[name];
    const file = Object.keys(FILES).find(k => k.indexOf('local/xml/' + name + '.') === 0 && /\.txt$/.test(k));
    const map = new Map();
    for (const line of (FILES[file] || '').split(/\r?\n/)) {
      const tab = line.indexOf('\t');
      if (tab > 0) map.set(line.slice(0, tab), line.slice(tab + 1));
    }
    return (TABLES[name] = map);
  }
  const words = s => String(s).replace(/_/g, ' ').replace(/(?<=[a-z])(?=[A-Z0-9])/g, ' ').replace(/\s+/g, ' ').trim();
  // Game text for display: escaped newlines, color tags and the client's placeholders.
  //   \n -> newline (a few lines slip with "\m" or a lone "\" before a capital: newline too);
  //   <color=N>...</color> -> the text; <hotkey name="UseFood"/> -> [Use Food] (the client shows
  //   the key bound to that action); <username/> -> [your name]; {0} -> … (filled in by the game).
  const clean = s => unesc(String(s || ''))
    .replace(/\\n/g, '\n').replace(/\\r/g, '').replace(/\r/g, '')
    .replace(/\\m(?=[A-Z[])/g, '\n').replace(/\\(?=[A-Z[가-힯])/g, '\n')
    .replace(/<hotkey\s+name\s*=\s*"+([^"]*)"+\s*\/?>/gi, (m, k) => '[' + words(k) + ']')
    .replace(/<username\s*\/?>/gi, '[your name]')
    .replace(/<\/?color(?:\s*=\s*[^>]*)?>/gi, '')
    .replace(/<\/?(?:b|i|u|font|size)(?:\s*=[^>]*)?>/gi, '')
    .replace(/\{\d+\}/g, '…')
    .replace(/[ \t]+\n/g, '\n').replace(/\n{3,}/g, '\n\n').trim();
  const tr = s => clean((s || '').replace(/_LT\[xml\.(\w+)\.(\d+)\]/g, (m, t, k) => table(t.toLowerCase()).get(k) || ''));
  const HANGUL = /[\u1100-\u11ff\u3130-\u318f\uac00-\ud7af]/;
  const english = s => !!s && !HANGUL.test(s);
  const num = v => { const n = Number(v); return Number.isFinite(n) ? n : 0; };
  // Thousands commas without toLocaleString (slow: the upgrade paths format tens of thousands).
  const gold = n => (Number.isInteger(n) ? String(n).replace(/\B(?=(\d{3})+(?!\d))/g, ',') : n.toLocaleString('en-US')) + ' G';
  const cap = s => String(s).replace(/^./, c => c.toUpperCase());
  const titleCase = s => String(s).replace(/(^|\s)([a-z])/g, (m, a, b) => a + b.toUpperCase());
  const truthy = v => /^true$/i.test(String(v || '').trim());
  // The attributes of an item's XML field ("<xml bagshape="7/5" />").
  const xmlAttrs = s => { const o = {}; String(s || '').replace(/(\w+)\s*=\s*"([^"]*)"/g, (m, k, v) => { o[k.toLowerCase()] = v; }); return o; };
  const seconds = n => {
    n = Math.round(n);
    if (n < 60) return n + ' s';
    const h = Math.floor(n / 3600), m = Math.floor(n % 3600 / 60), s = n % 60;
    return [h ? h + ' h' : '', m ? m + ' min' : '', s ? s + ' s' : ''].filter(Boolean).join(' ');
  };

  // ---------- ranks ----------
  // Skill and enchant ranks share one scale (the optionset.xml notes; the "Dan" titles' texts
  // confirm 16 = Dan 1 ... 24 = Dan 9): 0 Novice, 1 F ... 6 A, 7 rank 9 ... 15 rank 1, 16+ Dan 1+.
  const rankLabel = lv => lv <= 0 ? 'Novice' : lv <= 6 ? 'FEDCBA'[lv - 1] : lv <= 15 ? String(16 - lv) : 'Dan ' + (lv - 15);
  const skillRank = lv => lv >= 16 ? rankLabel(lv) : lv <= 0 ? 'Novice rank' : 'rank ' + rankLabel(lv);

  // ---------- stats and effects (enchants, titles) ----------
  // SetParamOnEquip(<stat>, <value>): the stat names, in any case.
  const STATS = {
    str: 'Strength', int: 'Intelligence', dex: 'Dexterity', will: 'Will', luck: 'Luck',
    lifemax: 'Max HP', manamax: 'Max MP', staminamax: 'Max Stamina',
    attmin: 'Min Damage', attmax: 'Max Damage', wattmin: 'Min Injury Rate', wattmax: 'Max Injury Rate',
    crit: 'Critical', critical: 'Critical', rate: 'Balance', def: 'Defense', defence: 'Defense', prot: 'Protection',
    magicattack: 'Magic Attack', magicdefence: 'Magic Defense', magicprotect: 'Magic Protection', combatpower: 'Combat Power',
    moreexp: 'Experience', magic_damage: 'Magic Damage', staminause_revised: 'Stamina used', manause_revised: 'Mana used',
    manaburn_revised: 'Mana Burn', frozen_blast_duraion: 'Frozen Blast duration', frozen_blast_degree: 'Frozen Blast strength',
    barrier_life: 'Barrier Spikes durability', casting_speed: 'Casting speed',
    fire: 'Fire', ice: 'Ice', lightning: 'Lightning', poison: 'Poison',
    alchemy_fire: 'Fire Alchemy', alchemy_water: 'Water Alchemy', alchemy_wind: 'Wind Alchemy', alchemy_earth: 'Earth Alchemy',
    synthesis: 'Synthesis', dissolution: 'Dissolution', crystal_making: 'Crystal Making',
  };
  // SetSetItemEffectOnEquip(<effect>, <level>): the "set item" effects (optionset.xml's list), by
  // name when setitemdesc.xml has no Element row for one (setEffectName).
  const SET_FX = {
    poison_immune: 'Poison Immunity', stone_immune: 'Petrification Immunity', mana_saving: 'Mana Saving', stamina_saving: 'Stamina Saving',
    bombexpert: 'Bomb Expert', stompdefence: 'Stomp Defense', mana_wasting: 'Mana Wasting', stamina_wasting: 'Stamina Wasting',
    fast_attack: 'Fast Attack', refine_enhance: 'Refining bonus', blacksmith_enhance: 'Blacksmithing bonus',
  };
  const PCT = { crit: 1, critical: 1, rate: 1, wattmin: 1, wattmax: 1 };
  const statName = s => STATS[String(s).toLowerCase()] || titleCase(words(s));
  // An effect amount ("+(2~5)", "-(10~12)", "8", "(-5)%") as "+2~5", "-10~12", "+8", "-5%".
  function amount(v, pct) {
    const raw = String(v).trim();
    let s = raw.replace(/\s+/g, ''), sign = '';
    const p = /%$/.test(s);
    if (p) s = s.slice(0, -1);
    if (/^[+-]/.test(s)) { sign = s[0]; s = s.slice(1); }
    s = s.replace(/^\((.*)\)$/, '$1');
    if (/^[+-]/.test(s)) { sign = sign === '-' && s[0] === '-' ? '+' : s[0]; s = s.slice(1); }
    if (!/^\d+(\.\d+)?(~\d+(\.\d+)?)?$/.test(s)) return raw;
    return (sign || '+') + s + (p || pct ? '%' : '');
  }

  const skillName = id => { const s = skillAll().get(String(id).trim()); return s ? s.name : 'A certain skill'; };
  // A condition's text, with the skill or title it names linked to its page: a cell ({lines, label: the
  // plain text}) when it names one, else the text.
  function conditionCell(c) {
    const text = condition(c), sk = /^\s*is(?:greater|less)equalskilllv\s*\(\s*(\d+)/i.exec(c || ''), ti = /^\s*isusingtitle\s*\(\s*(\d+)/i.exec(c || '');
    if (sk && skillById(sk[1])) { const n = skillName(sk[1]); return { lines: [[skillLink(sk[1], n), text.slice(n.length)]], sep: '', label: text }; }
    const t = ti && titleById(ti[1]);
    if (t && titles().includes(t)) return { lines: [['While using the title ', { t: 'title', id: t.id, label: t.name }]], sep: '', label: text };
    return text;
  }

  const LEVELS = { level: 'Level', cumulatedlevel: 'Cumulative level', explolv: 'Exploration level', age: 'Age' };
  const RACES = { 0: 'Humans', 1: 'Elves', 2: 'Giants' }; // IsSupportRace (optionset.xml's notes)
  const DAYS = ['Sundays', 'Mondays', 'Tuesdays', 'Wednesdays', 'Thursdays', 'Fridays', 'Saturdays'];
  // A condition ("IsGreaterEqualSkillLv(20002, 1)", "level>=10") in words.
  function condition(c) {
    c = (c || '').trim();
    if (!c) return '';
    let m = /^(\w+)\s*(<=|>=|<|>|==?)\s*(\d+)$/.exec(c);
    if (m) {
      const what = LEVELS[m[1].toLowerCase()] || cap(words(m[1])), n = m[3];
      return { '>=': `${what} ${n} or higher`, '>': `${what} above ${n}`, '<=': `${what} ${n} or lower`, '<': `${what} below ${n}` }[m[2]] || `${what} ${n}`;
    }
    m = /^(\w+)\s*\((.*?)\)+$/.exec(c);
    if (!m) return 'Special condition';
    const fn = m[1].toLowerCase(), a = m[2].split(',').map(s => s.trim());
    if (fn === 'isgreaterequalskilllv' && /^\d+$/.test(a[1] || '')) return `${skillName(a[0])} ${skillRank(+a[1])} or higher`;
    if (fn === 'islessequalskilllv' && /^\d+$/.test(a[1] || '')) return `${skillName(a[0])} ${skillRank(+a[1])} or lower`;
    if (fn === 'isusingtitle') { const t = titleById(a[0]); return t ? `While using the title "${t.name}"` : 'While using a certain title'; }
    if (fn === 'isincondition') return 'While under a certain condition';
    if (fn === 'issupportrace') return RACES[a[0]] ? `${RACES[a[0]]} only` : 'Certain races only';
    if (fn === 'hassummon') return /^barrier$/i.test(a[0]) ? 'While Barrier Spikes are out' : `While a ${words(a[0]).toLowerCase()} is summoned`;
    if (fn === 'istodaymonth' && DAYS[+a[0]] && /^\d$/.test(a[0])) return `On ${DAYS[+a[0]]}`; // 0 = Sunday (optionset.xml's notes; the "Weekend" enchant)
    if (fn === 'isbroken') return a[0] === '1' ? "While the item isn't broken" : 'While the item is broken';
    if (fn === 'isgreaterarbeitcount' && /^\d+$/.test(a[1] || '')) return `After more than ${a[1]} part-time job${a[1] === '1' ? '' : 's'}`;
    return 'Special condition';
  }

  // One effect call as [what, value] ([what, value, set effect key] for a set effect).
  function effect(fn, argText) {
    const args = argText.split(',').map(s => s.trim()), f = fn.toLowerCase(), key = String(args[0] || '').toLowerCase();
    const rest = args.slice(1).join(',');
    if (f === 'setparamonequip') return [statName(args[0]), amount(rest, PCT[key])];
    if (f === 'setstatusonequip') {
      if (key === 'chain_casting') {
        // .cell: the same with the skill linked (the text stays the plain name, for search and filters).
        const r = [`Chain casting: ${skillName(args[1])}`, ''];
        if (skillById(args[1])) r.cell = { lines: [['Chain casting: ', skillLink(args[1], skillName(args[1]))]], sep: '' };
        return r;
      }
      return [statName(args[0]) + (/^(fire|ice|lightning)$/.test(key) ? ' element' : ''), amount(rest)];
    }
    if (f === 'addbonusonalchemy') return [statName(args[0]) + ' bonus', amount(rest)];
    // The set effect's name from setitemdesc.xml (the third value: its key, for a link to its page).
    if (f === 'setsetitemeffectonequip') return [setEffectName(key) + ' (set effect)', amount(rest), key];
    if (f === 'setpersonalize') return [/^false$/i.test(args[0]) ? 'Unbinds the item' : 'Binds the item to its owner', ''];
    return [titleCase(words(fn)), args.join(', ')];
  }
  // An effect list (": condition : Effect(args);" ...) as [[what, value, condition(, set effect key)]].
  function effects(list) {
    const out = [];
    for (const part of (list || '').split(/;|\r?\n/)) {
      const p = part.trim();
      if (!p || p[0] !== ':') continue;
      const bits = p.slice(1).split(':');
      const cond = bits.length > 1 ? conditionCell(bits[0]) : '';
      const calls = bits.length > 1 ? bits.slice(1).join(':') : bits[0];
      for (const m of calls.matchAll(/(\w+)\s*\(((?:[^()]|\([^()]*\))*)\)/g)) {
        const e = effect(m[1], m[2]), [what, val, key] = e, f = key ? [what, val, cond, key] : [what, val, cond];
        if (e.cell) f.cell = e.cell;
        out.push(f);
      }
    }
    return out;
  }
  const fxText = f => `${f[0]}${f[1] ? ' ' + f[1] : ''}${f[2] ? ' (' + (f[2].label || f[2]) + ')' : ''}`;
  // A signed amount in color, blue for better and red for worse: up is better, except for what's
  // better lower (mana or stamina used, costs, cooldowns, casting time, Mana Burn). app.js colors the
  // skill effect texts by the same words (TONE_DOWN_GOOD there).
  const DOWN_GOOD = /usage|\bused\b|\buse\b|cost|consum|cooldown|cool ?time|casting time|delay|charge time|mana burn|wait/i;
  // ("Stamina Usage Reduction +3": a reduction of what's better lower is better up.)
  const toneOf = (label, value) => { const m = /^\s*([+\-−])\s?\d/.exec(String(value)); return m ? ((m[1] === '+') !== (DOWN_GOOD.test(label) && !/reduc|decreas/i.test(label)) ? 'pos' : 'neg') : null; };
  const toned = (label, value) => { const t = toneOf(label, value); return t ? { text: String(value), tone: t } : value; };
  // A text cell in a tone (a link keeps its own color).
  const tonedAs = (cell, tone) => (tone && typeof cell === 'string' ? { text: cell, tone } : cell);
  // "Strength +5" colored whole.
  const tonedLine = (label, value) => { const t = toneOf(label, value), s = `${label} ${value}`; return t ? { text: s, tone: t } : s; };
  // A finished effect line ("Max Damage +2", "Critical -1%", "Max HP +10 (Level 20 or higher)") colored
  // whole by its amount; anything else (a link, a line with no amount) as it is.
  const toneFx = s => {
    if (typeof s !== 'string') return s;
    // A sign only counts at the start of a word: "1-4 points" is a range, not a "-4".
    const m = /^(.*?)(?<![^\s(:,/])([+\-−]\s?\d[\d.,]*(?:\s?[~–-]\s?\d[\d.,]*)?\s?%?)(?=\s|$)/.exec(s);
    const t = m && toneOf(m[1], m[2]);
    return t ? { text: s, tone: t } : s;
  };
  // A kv row of a stat and its change, both in the change's color.
  const tonedRow = (label, value) => { const t = toneOf(label, value); return [tonedAs(label, t), t ? { text: String(value), tone: t } : value]; };
  // Effects one to a line, each colored (an upgrade's in a table cell).
  const fxLines = list => ({ lines: (list || []).map(f => [toneFx(f)]), sep: '' });

  // ---------- items ----------
  // Categories from the item's Category tags ("/equip/righthand/weapon/blunt/wood/..."). The
  // list is the display order; ITEM_MATCH is the order the tests are tried in (the first match).
  const has = (t, ...k) => k.some(x => t.has(x));
  const W = t => t.has('weapon') || t.has('ego_weapon');
  const bowish = t => t.has('bow') || [...t].some(x => /^bow\d+$|_bow$/.test(x));
  const ITEM_CATS = [
    // [key, title, parent, test]
    ['weapon', 'Weapons', '', null],
    ['weapon/sword', 'Swords', 'weapon', t => W(t) && has(t, 'edged', 'blade')],
    ['weapon/blunt', 'Blunt weapons', 'weapon', t => W(t) && has(t, 'blunt', 'twin_club', 'hammer', 'club')],
    ['weapon/axe', 'Axes', 'weapon', t => W(t) && t.has('axe')],
    ['weapon/lance', 'Lances', 'weapon', t => W(t) && has(t, 'lance', 'spear', 'javelin')],
    ['weapon/knuckle', 'Knuckles', 'weapon', t => W(t) && has(t, 'knuckle', 'glove_knuckle')],
    ['weapon/bow', 'Bows', 'weapon', t => W(t) && bowish(t)],
    ['weapon/crossbow', 'Crossbows', 'weapon', t => W(t) && t.has('crossbow')],
    ['weapon/atlatl', 'Atlatls', 'weapon', t => W(t) && t.has('atlatl')],
    ['weapon/wand', 'Wands', 'weapon', t => W(t) && t.has('wand')],
    ['weapon/staff', 'Staves', 'weapon', t => W(t) && t.has('staff')],
    ['weapon/cylinder', 'Cylinders', 'weapon', t => W(t) && [...t].some(x => x.indexOf('cylinder') >= 0)],
    ['weapon/other', 'Other weapons', 'weapon', t => W(t) && !t.has('tool')],
    ['ammo', 'Ammunition', '', t => has(t, 'arrow', 'bolt', 'ballista_bolt')],
    ['shield', 'Shields', '', t => t.has('shield')],
    ['armor', 'Clothing & Armor', '', null],
    ['armor/heavy', 'Heavy armor', 'armor', t => t.has('armor') && t.has('heavyarmor')],
    ['armor/light', 'Light armor', 'armor', t => t.has('armor') && t.has('lightarmor')],
    ['armor/cloth', 'Clothing', 'armor', t => t.has('armor')],
    ['robe', 'Robes', '', t => t.has('robe')],
    ['headgear', 'Headgear', '', t => has(t, 'head', 'headgear', 'helmet')],
    ['gloves', 'Gloves', '', t => has(t, 'glove', 'gauntlet') || (t.has('equip') && t.has('hand'))],
    ['footwear', 'Footwear', '', t => has(t, 'foot', 'shoes', 'armorboots')],
    ['accessory', 'Accessories', '', t => t.has('accessary')],
    ['wings', 'Wings', '', t => t.has('equip') && t.has('wing')],
    ['instrument', 'Instruments', '', t => t.has('instrument')],
    ['tool', 'Tools', '', t => t.has('tool')],
    ['equip', 'Other equipment', '', t => t.has('equip')],
    ['potion', 'Potions', '', t => t.has('potion')],
    ['food', 'Food', '', t => has(t, 'food', 'cooking')],
    ['enchant', 'Enchant scrolls', '', t => t.has('enchantscroll') || (t.has('enchant') && t.has('scroll'))],
    ['book', 'Books', '', t => has(t, 'book', 'readable', 'rarebook', 'skillbook', 'collectionbook')],
    ['scroll', 'Scrolls', '', t => t.has('scroll')],
    ['pass', 'Passes', '', t => has(t, 'dungeon_pass', 'ruin_pass', 'stagedungeon', 'shadowmission_pass')],
    ['key', 'Keys', '', t => t.has('key')],
    ['warp', 'Warp items', '', t => t.has('warp_scroll')],
    ['bag', 'Bags & pouches', '', t => has(t, 'pouch', 'bag', 'sac_item', 'herbsac')],
    ['material', 'Materials', '', t => has(t, 'material', 'metal', 'crystal', 'fur', 'leather', 'fish', 'jewel', 'ore', 'nectar')],
    ['monster', 'Monster drops', '', t => t.has('monster')],
    ['fomor', 'Fomor scrolls', '', t => t.has('evilscroll')],
    ['artifact', 'Artifacts', '', t => has(t, 'appraisal', 'recovered', 'relic', 'fossil_restoration')],
    ['house', 'Housing', '', t => has(t, 'house', 'hanging_picture', 'dynamic_prop')],
    ['delivery', 'Deliveries', '', t => t.has('delivery')],
    ['gift', 'Gifts', '', t => t.has('present')],
    ['coupon', 'Coupons', '', t => t.has('coupon')],
    ['pet', 'Pets', '', t => has(t, 'create_pet', 'create_lite_pet')],
    ['nao', "Nao's outfits", '', t => t.has('nao_dress')],
    // Hair, faces and beards: Attr_Type 100 (hair) and 101 (face) rows with no tags.
    ['look', 'Hair & faces', '', (t, r) => !t.size && (r.Attr_Type === '100' || r.Attr_Type === '101')],
    ['usable', 'Usable items', '', t => t.has('usable')],
    ['misc', 'Other items', '', () => true],
  ];
  const ITEM_MATCH = ['look', 'ammo',
    'weapon/knuckle', 'weapon/wand', 'weapon/staff', 'weapon/cylinder', 'weapon/crossbow', 'weapon/bow', 'weapon/atlatl', 'weapon/lance',
    'weapon/axe', 'weapon/sword', 'weapon/blunt', 'weapon/other',
    'shield', 'wings', 'armor/heavy', 'armor/light', 'armor/cloth', 'robe', 'headgear', 'gloves', 'footwear', 'accessory',
    'instrument', 'tool', 'potion', 'food', 'enchant', 'book', 'pass', 'pet', 'nao', 'equip',
    'scroll', 'key', 'warp', 'bag', 'fomor', 'artifact', 'monster', 'material', 'house', 'delivery', 'gift', 'coupon', 'usable', 'misc']
    .map(k => ITEM_CATS.find(c => c[0] === k));
  // Subcategories of the categories above, from the item's tags (and for accessories its name: the
  // tags don't tell a ring from a pendant): [key, title, test(tags, name)], the first match; each list
  // ends in a catch-all. An item's category is then "potion/recovery" and so on.
  const nm = re => (t, n) => re.test(n);
  const ITEM_SUBCATS = {
    headgear: [['helmet', 'Helmets', t => t.has('helmet')], ['face', 'Glasses & face', t => has(t, 'glasses', 'glasses01', 'glasses02', 'specialacc_eye', 'specialacc_mouth')],
      ['hat', 'Hats & other headgear', () => true]],
    footwear: [['boots', 'Armored boots', t => t.has('armorboots')], ['shoes', 'Shoes', () => true]],
    gloves: [['gauntlet', 'Gauntlets', t => t.has('gauntlet')], ['glove', 'Gloves & bracelets', () => true]],
    accessory: [['puppet', 'Puppets', (t, n) => t.has('puppet') || /\bpuppet\b/i.test(n)],
      ['halo', 'Halos & balloons', (t, n) => has(t, 'halo', 'ballon', 'balloon_revival_count') || /balloon/i.test(n)],
      ['neck', 'Necklaces & pendants', nm(/pendant|necklace|amulet|ankh|cross|torque|choker|locket|scarf/i)], ['earring', 'Earrings', nm(/earring/i)],
      ['ring', 'Rings', nm(/\bring\b/i)], ['brooch', 'Brooches & charms', nm(/brooch|\bpin\b|badge|buckle|charm|ribbon|corsage|talisman|seal|emblem|insignia/i)], ['other', 'Other accessories', () => true]],
    instrument: [['string', 'String instruments', t => has(t, 'string', 'string01', 'string02', 'string03', 'lyra', 'violin', 'battle_mandolin')],
      ['wind', 'Wind instruments', t => t.has('wind')], ['percussion', 'Bells & percussion', t => has(t, 'handbell', 'percussion')], ['other', 'Other instruments', () => true]],
    tool: [['cooking', 'Cooking tools', t => t.has('cooking')], ['training', 'Training batons', t => has(t, 'trainingbaton', 'training')],
      ['craft', 'Crafting tools', t => has(t, 'handicraft', 'kit', 'powder', 'enchant', 'manual')], ['other', 'Other tools', () => true]],
    potion: [['recovery', 'Recovery potions', t => has(t, 'hp', 'mana', 'stamina', 'wound', 'full_recovery_potion', 'hp100', 'stamina50')],
      ['boost', 'Boost potions', t => has(t, 'boost_potion', 'boost_attack_potion', 'fast_casting_potion', 'combat_double_exp', 'itemdrop_rate_up_real_time', 'production_rate_modify', 'shadow_mission_bonus', 'skill_exp_complete')],
      ['pet', 'Pet potions', t => has(t, 'pet_age_potion', 'pet_special_potion')], ['other', 'Other potions', () => true]],
    food: [['fish', 'Fish', t => t.has('fish')], ['sweets', 'Sweets & snacks', t => has(t, 'chocolate_npc', 'candy_npc', 'gum', 'honey', 'lotte', 'magicsongpeon')],
      ['festival', 'Festival food', t => t.has('festival_source')], ['other', 'Other food', () => true]],
    scroll: [['manual', 'Patterns & manuals', t => t.has('manual')], ['other', 'Other scrolls', () => true]],
    material: [['metal', 'Ore & metal', t => has(t, 'ore', 'metal', 'ingot')], ['crystal', 'Crystals & gems', t => has(t, 'crystal', 'jewel', 'alchemy_crystal')],
      ['fabric', 'Fabric & leather', t => has(t, 'tailor', 'leather', 'tailor_arbeit')], ['herb', 'Herbs & nectar', t => has(t, 'herb', 'nectar')],
      ['bait', 'Fishing bait', t => has(t, 'bait', 'bait01', 'fishing')], ['dye', 'Dyes', t => t.has('dyestuff')], ['other', 'Other materials', () => true]],
    monster: [['fur', 'Fur', t => t.has('fur')], ['claw', 'Claws', t => t.has('claw')], ['scale', 'Scales', t => t.has('scale')], ['hoof', 'Hooves', t => t.has('hoof')],
      ['gland', 'Poison glands', t => t.has('poisongland')], ['feather', 'Feathers', t => t.has('feather')], ['other', 'Other monster drops', () => true]],
    book: [['skill', 'Skill books', t => t.has('skillbook')], ['collection', 'Collection books', t => t.has('collectionbook')], ['rare', 'Rare books', t => t.has('rarebook')],
      ['mission', 'Mission papers', t => t.has('mission_paper')], ['other', 'Other books', () => true]],
    pass: [['dungeon', 'Dungeon passes', t => t.has('dungeon_pass')], ['ruin', 'Ruin passes', t => t.has('ruin_pass')], ['shadow', 'Shadow Mission passes', t => t.has('shadowmission_pass')],
      ['other', 'Other passes', () => true]],
    usable: [['dye', 'Dye ampoules', t => has(t, 'dye_ampul', 'steel_dye_ampul')], ['firework', 'Fireworks', t => t.has('hanabi')],
      ['event', 'Event items', t => [...t].some(x => /^event|^worldcup/.test(x))], ['other', 'Other usable items', () => true]],
    house: [['picture', 'Pictures', t => t.has('hanging_picture')], ['other', 'Other housing', () => true]],
  };
  const itemSubCat = (cat, t, name) => { const s = (ITEM_SUBCATS[cat] || []).find(x => x[2](t, name)); return s ? cat + '/' + s[0] : cat; };
  for (const [cat, subs] of Object.entries(ITEM_SUBCATS)) {
    const at = ITEM_CATS.findIndex(c => c[0] === cat);
    ITEM_CATS.splice(at + 1, 0, ...subs.map(s => [cat + '/' + s[0], s[1], cat, null]));
  }
  // The item list's top level: groups over the categories above, Equipment first (what players look
  // for most); the rest stand alone. Only the list and sidebar use them.
  const ITEM_GROUPS = [
    ['g/equip', 'Equipment', ['weapon', 'ammo', 'shield', 'armor', 'robe', 'headgear', 'gloves', 'footwear', 'accessory', 'wings', 'instrument', 'tool', 'equip']],
    ['g/use', 'Consumables', ['potion', 'food', 'scroll', 'warp', 'gift', 'coupon', 'usable']],
    ['g/craft', 'Crafting', ['material', 'monster', 'artifact']],
    ['g/dungeon', 'Dungeons', ['pass', 'key']],
    ['g/quest', 'Quests', ['fomor', 'delivery']],
    ['g/look', 'Looks', ['look', 'nao']],
    ['g/misc', 'Miscellaneous', ['pet', 'house', 'bag', 'misc']]];
  const itemGroupOf = k => (ITEM_GROUPS.find(g => g[2].includes(k)) || [''])[0];
  const ITEM_CAT_TITLE = Object.fromEntries(ITEM_CATS.map(c => [c[0], c[1]]));
  const itemCatPath = k => { const c = ITEM_CATS.find(x => x[0] === k); return c && c[2] ? ITEM_CAT_TITLE[c[2]] + ' › ' + c[1] : (c ? c[1] : k); };
  const tagsOf = r => new Set((r.Category || '').split('/').map(s => s.trim().toLowerCase()).filter(Boolean));
  const itemCat = (t, r) => ITEM_MATCH.find(c => c[3] && c[3](t, r))[0];

  // What the item's tags say, in words.
  // The item's yes/no tags, as infobox rows: [row label, [[tag, text]...]]. "Usable by" only when the
  // race filter didn't already give that row.
  const PROPS = [
    ['Repaired by', [['smith_repairable', 'Blacksmiths'], ['magicsmith_repairable', 'Magic craftsmen'], ['magic_school_repairable', 'Magic schools'],
      ['misc_repairable', 'General shops'], ['not_repairable', "Can't be repaired"], ['no_abrasion', "Doesn't wear down"]]],
    ['Trading', [['not_given', "Can't be traded"], ['not_dropable', "Can't be dropped"], ['nodrop_at_knockdown', 'Kept when knocked out']]],
    ['Customizing', [['not_enchantable', "Can't be enchanted"], ['not_dyeable', "Can't be dyed"], ['fixedcolor_dyeable', 'Fixed colors only'], ['not_bless', "Can't be blessed"]]],
    ['Usable by', [['human_only', 'Humans'], ['elf_only', 'Elves'], ['giant_only', 'Giants'], ['human_elf_only', 'Humans and elves'],
      ['human_giant_only', 'Humans and giants'], ['marriage_only', 'Married characters']]],
    ['Other', [['expiring', 'Expires'], ['destroyable', 'Can be destroyed'], ['not_ego', "Can't be fed to spirit weapons"], ['not_feed', "Can't be fed to pets"]]],
  ];
  // Par_AttackSpeed 0-4: the client's five labels (msg.item_attackspeed_* in client.japan.txt),
  // fastest first. Bows, crossbows and atlatls use values above 4 ("<speed> Ranged Weapon" in the
  // tooltip), and the game's files don't say which label those get, so they're left out.
  const SPEEDS = ['Very fast', 'Fast', 'Normal', 'Slow', 'Very slow'];
  // One item of a category, for the page's type label ("Sword").
  const ITEM_KIND = {
    'weapon/sword': 'Sword', 'weapon/blunt': 'Blunt weapon', 'weapon/axe': 'Axe', 'weapon/lance': 'Lance', 'weapon/knuckle': 'Knuckle',
    'weapon/bow': 'Bow', 'weapon/crossbow': 'Crossbow', 'weapon/atlatl': 'Atlatl', 'weapon/wand': 'Wand', 'weapon/staff': 'Staff',
    'weapon/cylinder': 'Cylinder', 'weapon/other': 'Weapon', ammo: 'Ammunition', shield: 'Shield', 'armor/heavy': 'Heavy armor',
    'armor/light': 'Light armor', 'armor/cloth': 'Clothing', robe: 'Robe', headgear: 'Headgear', gloves: 'Gloves', footwear: 'Footwear',
    accessory: 'Accessory', wings: 'Wings', instrument: 'Instrument', tool: 'Tool', equip: 'Equipment', potion: 'Potion', food: 'Food',
    enchant: 'Enchant scroll', book: 'Book', scroll: 'Scroll', pass: 'Dungeon pass', key: 'Key', warp: 'Warp item', bag: 'Bag',
    material: 'Material', monster: 'Monster drop', fomor: 'Fomor scroll', artifact: 'Artifact', house: 'Housing item', delivery: 'Delivery',
    gift: 'Gift', coupon: 'Coupon', pet: 'Pet item', nao: "Nao's outfit", look: 'Hair or face', usable: 'Usable item', misc: 'Item',
  };
  // The item lists' stat columns and filters per category (meta.types.item.catColumns /
  // catFilters; the nearest listed ancestor of a category wins). The values are itemStats'.
  const COL = {
    dmg: { key: 'dmg', title: 'Damage', num: true, sort: 'dmgMax', bar: true, card: true, short: 'Dmg' },
    crit: { key: 'crit', title: 'Critical', num: true, card: true, short: 'Crit' },
    bal: { key: 'bal', title: 'Balance', num: true, card: true, short: 'Bal' },
    speed: { key: 'speed', title: 'Speed', meter: SPEEDS.slice().reverse(), card: true, short: '' },
    hits: { key: 'hits', title: 'Hits per combo', num: true, card: true, short: 'Combo' },
    range: { key: 'range', title: 'Range', num: true, card: true },
    def: { key: 'def', title: 'Defense', num: true, card: true, short: 'Def' },
    prot: { key: 'prot', title: 'Protection', num: true, card: true, short: 'Prot' },
    restores: { key: 'restores', title: 'Restores', card: true, short: '' },
    hunger: { key: 'hunger', title: 'Fills hunger', num: true, card: true, short: 'Hunger' },
    price: { key: 'price', title: 'Shop price', num: true },
    // The set effects it gives (setsOf), one to a line; only where some item on the page has one.
    sets: { key: 'sets', title: 'Set effects', lines: true },
  };
  const MELEE = [COL.dmg, COL.crit, COL.bal, COL.speed, COL.hits, COL.sets, COL.price];
  const RANGED = [COL.dmg, COL.crit, COL.bal, COL.range, COL.sets, COL.price];
  const ARMOR = [COL.def, COL.prot, COL.sets, COL.price];
  const ITEM_COLS = { weapon: MELEE, 'weapon/bow': RANGED, 'weapon/crossbow': RANGED, 'weapon/atlatl': RANGED, ammo: [COL.dmg, COL.crit, COL.price],
    instrument: [COL.dmg, COL.crit, COL.bal, COL.price], shield: ARMOR, armor: ARMOR, robe: ARMOR, headgear: ARMOR, gloves: ARMOR, footwear: ARMOR,
    accessory: ARMOR, potion: [COL.restores, COL.price], food: [COL.hunger, COL.price] };
  const MIN = (key, title) => ({ kind: 'min', key, title });
  // The copy for hosting (MabiWiki_Hostable) sets MABIWIKI_HOSTED, and MABIWIKI_NO3D when made
  // without the models (no 3D sections then) or MABIWIKI_MODELS when made with them.
  const HOSTED = !!window.MABIWIKI_HOSTED;
  const NO3D = !!window.MABIWIKI_NO3D;
  const MODEL_CAPTION = window.MABIWIKI_MODELS ? '' : "Loads the model from your game's files";
  // Attr_RaceFilter ("/human/female/ | /elf/female/") in words.
  const RACE_WORDS = { human: 'Humans', elf: 'Elves', giant: 'Giants', nao: 'Nao', puppet: 'Puppets' };
  function raceFilter(s) {
    // "nil" is an empty filter (Captured Monster's), not a race.
    const alts = String(s || '').split('|').map(a => a.split('/').map(x => x.trim().toLowerCase()).filter(x => x && x !== 'nil')).filter(a => a.length);
    if (!alts.length) return '';
    const races = [], genders = new Set();
    for (const a of alts) {
      for (const x of a) {
        if (x === 'male' || x === 'female') genders.add(x);
        else if (!races.includes(RACE_WORDS[x] || cap(x))) races.push(RACE_WORDS[x] || cap(x));
      }
      if (!a.some(x => x === 'male' || x === 'female')) genders.add('any');
    }
    const g = genders.size === 1 && !genders.has('any') ? (genders.has('male') ? 'Male' : 'Female') : '';
    if (!races.length) return g ? g + ' characters' : '';
    const list = races.length > 1 ? races.slice(0, -1).join(', ') + ' and ' + races[races.length - 1] : races[0];
    return g ? g + ' ' + list.toLowerCase().replace(/\bnao\b/, 'Nao') : list;
  }
  // Food_Type: what Food_Amount restores (the potions' names say which).
  const RESTORES = { 1: ['HP'], 2: ['MP'], 3: ['Stamina'], 4: ['Wounds'], 5: ['HP', 'MP'], 6: ['HP', 'Stamina'] };

  let ITEMS = null;
  function items() {
    if (ITEMS) return ITEMS;
    ITEMS = new Map();
    for (const r of rows('db/itemdb.xml', 'Mabi_Item')) {
      if (!r.ID) continue;
      // Another locale's row isn't loaded by this client; this locale's row replaces the plain one.
      const loc = (r.Locale || '').trim().toLowerCase();
      if (loc && LOCALE && loc !== LOCALE) continue;
      const old = ITEMS.get(r.ID);
      if (old && !(loc && !(old.r.Locale || '').trim())) continue;
      const tags = tagsOf(r);
      const base = clean(r.Text_Name0).replace(/^((?:\([^)]*\)\s*)?)@/, '$1').trim();
      // A leading "@" (after a "(temp)" mark) is a marker the client doesn't show.
      let name = tr(r.Text_Name1).replace(/^((?:\([^)]*\)\s*)?)@/, '$1').trim(), local = '';
      if (!name) name = base;
      else if (!english(name) && english(base)) { local = name; name = base; }
      ITEMS.set(r.ID, { r, tags, name: name || 'Item ' + r.ID, local, base, cat: itemSubCat(itemCat(tags, r), tags, name || '') });
    }
    return ITEMS;
  }

  // An item's fighting and defense numbers, for its page (Combat, Defense, the stat bar) and the
  // lists' stat columns: {dmg: '19~34', dmgMin, dmgMax, wMin, wMax, crit: '5%', bal: '50%',
  // speed: 'Slow', hits, range, def, prot, mdef, mprot}, each only when the item has it.
  function itemStats(it) {
    const r = it.r, t = it.tags, o = {};
    if (it.stats) return it.stats;
    const par = r.Par_BlockUseFlag === undefined || truthy(r.Par_BlockUseFlag);
    // Combat: fighting gear only (an enchant scroll's row carries leftover weapon fields).
    const fights = /^(weapon|ammo|shield)/.test(it.cat) || W(t) || num(r.Par_AttackMax) > 0;
    if (par && fights) {
      const spd = r.Par_AttackSpeed === undefined ? -1 : num(r.Par_AttackSpeed);
      const ranged = bowish(t) || has(t, 'crossbow', 'atlatl');
      if (num(r.Par_AttackMax)) { o.dmgMin = num(r.Par_AttackMin); o.dmgMax = num(r.Par_AttackMax); o.dmg = `${o.dmgMin}~${o.dmgMax}`; }
      if (num(r.Par_WAttackMax)) { o.wMin = num(r.Par_WAttackMin); o.wMax = num(r.Par_WAttackMax); }
      if (num(r.Par_CriticalRate)) o.crit = num(r.Par_CriticalRate) + '%';
      if (num(r.Par_AttackBalance)) o.bal = num(r.Par_AttackBalance) + '%';
      if (Object.keys(o).length && W(t) && !/^ammo/.test(it.cat)) {
        if (!ranged && spd >= 0 && spd <= 4) o.speed = SPEEDS[spd];
        // The tooltip's "<speed> <N> Hit Weapon" (client.japan.txt): N is taken as DownHitCount + 1.
        // Inferred: the Bastard Sword (DownHitCount 1) says a triple combo is hard to pull off.
        if (!ranged && r.Par_DownHitCount !== undefined) o.hits = num(r.Par_DownHitCount) + 1;
        if (ranged && num(r.Par_EffectiveRange)) o.range = num(r.Par_EffectiveRange);
      }
    }
    const x = xmlAttrs(r.XML);
    if (par && num(r.Par_Defense)) o.def = num(r.Par_Defense);
    if (par && num(r.Par_ProtectRate)) o.prot = num(r.Par_ProtectRate);
    // Defensive gear shows both, a 0 too (as the tooltip does: most gloves and shoes have no protection),
    // when its row has either field.
    if (par && /^(armor|robe|shield|headgear|gloves|footwear)/.test(it.cat) && (r.Par_Defense !== undefined || r.Par_ProtectRate !== undefined)) {
      o.def = num(r.Par_Defense);
      o.prot = num(r.Par_ProtectRate);
    }
    if (num(x.magic_defense)) o.mdef = num(x.magic_defense);
    if (num(x.magic_protect)) o.mprot = num(x.magic_protect);
    // Food and potions (itemPage's "When used"): what Food_Amount fills or restores.
    if (r.Food_BlockUseFlag === undefined ? r.Food_Type !== undefined : truthy(r.Food_BlockUseFlag)) {
      const ft = num(r.Food_Type), amt = num(r.Food_Amount);
      if (amt && r.Food_Type !== undefined && ft === 0) o.hunger = amt;
      else if (amt && RESTORES[ft]) o.restores = RESTORES[ft].map(s => `${s} ${amt}`).join(', ');
    }
    return (it.stats = o);
  }
  // The stat bar under an item's title: Damage, Critical, Balance, Speed (with the combo's hits
  // under it) or Range, then Defense and Protection (and the magic ones) while the bar has room.
  // covers: the Combat / Defense rows it shows (they aren't repeated in the sections).
  function statBar(s) {
    const rows = [], covers = [];
    const add = (label, v, small, ...rowNames) => { rows.push(small != null ? [label, v, small] : [label, v]); covers.push(...rowNames); };
    if (s.dmg !== undefined) add('Damage', s.dmg, null, 'Damage');
    if (s.crit !== undefined) add('Critical', s.crit, null, 'Critical');
    if (s.bal !== undefined) add('Balance', s.bal, null, 'Balance');
    if (s.speed !== undefined) add('Speed', s.speed, s.hits !== undefined ? s.hits + '-hit' : null, 'Attack speed', 'Hits per combo');
    else if (s.hits !== undefined) add('Hits per combo', s.hits, null, 'Hits per combo');
    if (s.range !== undefined) add('Range', s.range, null, 'Range');
    for (const [k, label] of [['def', 'Defense'], ['prot', 'Protection'], ['mdef', 'Magic Defense'], ['mprot', 'Magic Protection']]) {
      if (s[k] !== undefined && rows.length < 6) add(label, s[k], null, label);
    }
    return { rows, covers };
  }
  // The stats an item's list row carries (the COL columns' keys, and the damage range's ends).
  const LIST_STATS = ['dmg', 'dmgMin', 'dmgMax', 'crit', 'bal', 'speed', 'hits', 'range', 'def', 'prot', 'restores', 'hunger'];
  const listStats = s => { const o = {}; for (const k of LIST_STATS) if (s[k] !== undefined) o[k] = s[k]; return o; };
  // Items the data marks as test, developer or placeholder rows: "(temp)", "temp", "test sword",
  // "Robe Test", "ArbaTest" and "(임시)" ("temporary") names, in the shown or the internal name
  // (Text_Name0: "Test Gold Combo Card1", "Developer's Super Evil Pass1"); "테스트" ("test") in the
  // Korean name; "(Not to be Distributed)"; "(dummy)"; the "testweapon" and "testbook" tags. Not
  // the advancement test items (real gear for the test). They keep their pages and stay in search,
  // but get no stat columns, so they don't rank in the stat lists, and the home page never
  // features them.
  // (A "(temp)" internal name alone doesn't count: many real items kept one, Raft Paddle, Snare Drum.)
  const TEST_WORDS = /\b(test|dev)\b|developer's/i;
  const TEST_NAME = /^\s*\(?\s*temp\b|\((temp|test)\)|임시|테스트|not to be (equipped or )?distributed|\(dummy\)|^character dummy/i;
  const TEST_OK = /\b(advancement|promotion) test\b/gi;
  const testItem = it => {
    if (it.test === undefined) {
      // "ArbaTest" -> "Arba Test" (but "Contest" stays).
      const words = s => !!s && TEST_WORDS.test(s.replace(TEST_OK, '').replace(/([a-z])(Test)\b/g, '$1 $2'));
      it.test = it.tags.has('testweapon') || it.tags.has('testbook') || /테스트/.test(it.local || '') ||
        TEST_NAME.test(it.name) || words(it.name) || words(it.base);
    }
    return it.test;
  };

  function itemPage(id) {
    const it = items().get(String(id));
    if (!it) return null;
    const r = it.r, t = it.tags, x = xmlAttrs(r.XML);
    const par = r.Par_BlockUseFlag === undefined || truthy(r.Par_BlockUseFlag);
    const info = [['Type', itemCatPath(it.cat)], ['Size', `${num(r.Inv_XSize) || 1}×${num(r.Inv_YSize) || 1}`]];
    const buy = num(r.Price_Buy), sell = num(r.Price_Sell) > 0 ? num(r.Price_Sell) : Math.floor(buy / 10);
    if (buy) info.push(['Shop price', gold(buy)]);
    // A shop pays Price_Sell when the row has one, else a tenth of the price.
    if (sell) info.push(['Sells for', gold(sell)]);
    if (num(r.Bundle_Max) > 1) info.push(r.Bundle_Type === '2' ? ['Holds up to', num(r.Bundle_Max)] : ['Stacks to', num(r.Bundle_Max)]);
    if (x.bagshape) {
      const b = x.bagshape.split('/'), slots = (b[2] || '').replace(/[^O]/gi, '').length;
      if (num(b[0]) && num(b[1])) info.push(['Bag size', `${num(b[0])}×${num(b[1])}` + (slots ? ` (${slots} slots)` : '')]);
    }
    if (par && num(r.Par_DurabilityMax)) info.push(['Durability', Math.round(num(r.Par_DurabilityMax) / 1000)]);
    if (par && num(r.Par_UpgradeMax)) info.push(['Upgrades', num(r.Par_UpgradeMax)]);
    if (par && num(r.Par_GemUpgradeMax)) info.push(['Gem upgrades', num(r.Par_GemUpgradeMax)]);
    const who = raceFilter(r.Attr_RaceFilter);
    if (who) info.push(['Usable by', who]);
    for (const [label, list] of PROPS) {
      const on = list.filter(([k]) => t.has(k)).map(p => p[1]);
      if (on.length && !(label === 'Usable by' && who)) info.push([label, on.length > 1 ? { lines: on } : on[0]]);
    }
    const sections = [];
    // The 3D model, read from the player's own packs when they ask (viewer3d.js).
    if (!NO3D && MESH_FIELDS.some(k => (r[k] || '').trim())) sections.push({ heading: '3D model', blocks: [{ kind: 'embed', name: 'model3d', args: { id: String(id) }, caption: MODEL_CAPTION }] });
    // Combat and Defense. The main numbers go in the stat bar under the title (page.stats); the
    // sections keep what the bar doesn't show (the injury rate).
    const s = itemStats(it), bar = statBar(s);
    const combat = [];
    if (s.dmgMax !== undefined) combat.push(['Damage', s.dmg]);
    if (s.wMax !== undefined) combat.push(['Injury rate', `${s.wMin}~${s.wMax}%`]);
    if (s.crit !== undefined) combat.push(['Critical', s.crit]);
    if (s.bal !== undefined) combat.push(['Balance', s.bal]);
    if (s.speed !== undefined) combat.push(['Attack speed', s.speed]);
    if (s.hits !== undefined) combat.push(['Hits per combo', s.hits]);
    if (s.range !== undefined) combat.push(['Range', s.range]);
    const defense = [];
    if (s.def !== undefined) defense.push(['Defense', s.def]);
    if (s.prot !== undefined) defense.push(['Protection', s.prot]);
    if (s.mdef !== undefined) defense.push(['Magic Defense', s.mdef]);
    if (s.mprot !== undefined) defense.push(['Magic Protection', s.mprot]);
    const covered = new Set(bar.covers);
    const combatLeft = combat.filter(c => !covered.has(c[0])), defenseLeft = defense.filter(c => !covered.has(c[0]));
    if (combatLeft.length) sections.push({ heading: 'Combat', blocks: [{ kind: 'kv', rows: combatLeft }] });
    if (defenseLeft.length) sections.push({ heading: 'Defense', blocks: [{ kind: 'kv', rows: defenseLeft }] });
    const hidden = hiddenStats(it);
    if (hidden.length) sections.push({ heading: 'Hidden stats', blocks: [{ kind: 'kv', rows: hidden }, { kind: 'note', text: 'Numbers the item has that its tooltip in the game doesn\'t show.' }] });
    // Food and potions: Food_Amount fills hunger (Food_Type 0) or restores what RESTORES says;
    // the stat and body fields change the character over time.
    if (r.Food_BlockUseFlag === undefined ? r.Food_Type !== undefined : truthy(r.Food_BlockUseFlag)) {
      const blocks = [], ft = num(r.Food_Type), amt = num(r.Food_Amount);
      const use = [];
      if (amt && r.Food_Type !== undefined && ft === 0) use.push(['Fills hunger by', amt]);
      else if (amt && RESTORES[ft]) for (const s of RESTORES[ft]) use.push([s === 'Wounds' ? 'Heals wounds' : 'Restores ' + s, amt]);
      if (num(r.Food_Toxic)) use.push(['Potion poisoning', '+' + num(r.Food_Toxic)]);
      if (use.length) blocks.push({ kind: 'kv', rows: use });
      const sign = n => (n > 0 ? '+' : '') + n;
      const stats = [['Food_Str', 'Strength'], ['Food_Int', 'Intelligence'], ['Food_Dex', 'Dexterity'], ['Food_Will', 'Will'], ['Food_Luck', 'Luck'],
        ['Food_Life', 'HP'], ['Food_Mana', 'MP'], ['Food_Stamina', 'Stamina']].filter(([k]) => num(r[k])).map(([k, l]) => tonedRow(l, sign(num(r[k]))));
      if (stats.length) blocks.push({ kind: 'kv', title: 'Stat changes', rows: stats });
      const body = [['Food_Fatness', 'Weight'], ['Food_Upper', 'Upper body'], ['Food_Lower', 'Lower body']]
        .filter(([k]) => num(r[k])).map(([k, l]) => [l, sign(num(r[k]))]);
      if (body.length) blocks.push({ kind: 'kv', title: 'Body changes', rows: body });
      if (blocks.length) sections.push({ heading: 'When used', blocks });
    }
    // FoodEffectXML: <buffer duration_sec="400"><effect param="lifemax" amount="+10"/>...
    if (r.FoodEffectXML) {
      const fe = r.FoodEffectXML, buff = [];
      for (const m of fe.matchAll(/<effect\b([^>]*)>/gi)) {
        const a = xmlAttrs(m[1]);
        if (a.param) buff.push(tonedRow(statName(a.param), amount(a.amount || '')));
      }
      const dur = /duration_sec\s*=\s*"(\d+)"/i.exec(fe);
      if (buff.length) sections.push({ heading: 'Temporary effect', blocks: [{ kind: 'kv', title: dur ? 'For ' + seconds(+dur[1]) : '', rows: buff }] });
    }
    // Fishing gear's fishing stats.
    const fishing = fishingStatsSection((fishingGearData().items || {})[String(id)]);
    if (fishing) sections.push(fishing);
    const setFx = itemSetEffects(id);
    if (setFx) sections.push(setFx);
    const upg = itemUpgrades(it);
    if (upg) sections.push(upg);
    // An item with upgrade slots that no upgrade in itemupgradedb.xml matches: the slot, empty.
    else if (par && num(r.Par_UpgradeMax)) sections.push({ heading: 'Upgrades', empty: { title: 'No upgrades are listed for this item.', text: "The game's upgrade list has none whose item filter matches it." } });
    // Every way to max out its upgrades, under them.
    const paths = upg && upgradePathsSection(it, upgradesFor((r.Category || '').toLowerCase()));
    if (paths) sections.push(paths);
    const special = specialUpgradeSection(id);
    if (special) sections.push(special);
    const repairs = repairSection(id);
    if (repairs) sections.push(repairs);
    const ench = enchants().filter(e => e.r.ScrollId === String(id));
    const rel = [];
    if (ench.length) rel.push({ kind: 'links', title: 'Enchant', items: ench.map(enchantLink) });
    const ml = monsterLinks(), from = ml.gathered.get(String(id)), worn = ml.gear.get(String(id));
    if (from) rel.push({ kind: 'links', title: 'Gathered from', items: monsterChips(from) });
    if (worn) rel.push({ kind: 'links', title: 'Monsters that carry it', items: monsterChips(worn) });
    const called = monsterById(x.raceid || x.sm_race || '');
    if (called) rel.push({ kind: 'links', title: 'Calls', items: [{ t: 'monster', id: called.id, label: called.name }] });
    rel.push(...itemPetBlocks(id));
    // Tempering potions: pet_training="lifemax:1~50, will:0~10" (the stat and its range).
    const training = String(x.pet_training || '').split(',').map(s => s.split(':').map(v => v.trim())).filter(a => a[0] && a[1]);
    if (training.length) sections.push({ heading: 'Pet training', blocks: [{ kind: 'kv', rows: training.map(([k, v]) => [statName(k), v]) }] });
    // A plain enchant scroll comes with whatever enchant its source gives: those are on the enchants' pages.
    if (it.cat === 'enchant' && !ench.length && !sourcesOf('items', id).length) {
      sections.push({ heading: 'Where to get it', blocks: [{ kind: 'note', text: 'Each enchant\'s page lists where its scroll comes from.' },
        { kind: 'links', style: 'rows', items: [{ t: 'enchant', label: 'All enchants', glyph: 'scroll' }] }] });
    } else if (sourcesOf('items', id).length || !(fishedAt(id).length || propsGiving(id).length)) sections.push(sourcesSection('items', id));
    const versions = variantsSection(id);
    if (versions) sections.push(versions);
    const fished = fishingSection(id);
    if (fished) sections.push(fished);
    const fromProps = propSection(id);
    if (fromProps) sections.push(fromProps);
    const book = collectionSection(id);
    if (book) sections.push(book);
    const craft = craftingSection(id);
    if (craft) sections.push(craft);
    sections.push(...randomUseSections(id));
    const turnin = itemTurninSection(id);
    if (turnin) sections.push(turnin);
    const asked = questRefs().need.get(String(id));
    if (asked && asked.size) sections.push({ heading: 'Quests', blocks: [questTable(asked, 'Quests that ask for it')] });
    const books = collectionsOf(id);
    if (books.length) rel.push({ kind: 'links', title: 'Collection books', items: books.filter(b => items().get(b)).map(b => itemLink(b, items().get(b).name)) });
    if (rel.length) sections.push({ heading: 'Related', blocks: rel });
    // Its tags (Category, in its order), under the title: each a link to the items list filtered by it.
    const tagList = (r.Category || '').split('/').map(s => s.trim()).filter(Boolean);
    const page = {
      title: it.name, subtitle: it.local || null, icon: itemIcon(String(id)), category: [it.cat], kind: ITEM_KIND[it.cat] || ITEM_KIND[it.cat.split('/')[0]] || 'Item',
      infobox: info, summary: tr(r.Text_Desc1), sections,
      tags: [...new Set(tagList)].map(g => ({ href: '#/items?k=' + encodeURIComponent(g.toLowerCase()), label: g })),
    };
    if (bar.rows.length) page.stats = bar.rows;
    return page;
  }

  // ---------- enchants ----------
  // Usage: 0 prefix, 1 suffix, 3 special (gauntlet and set-like options), 4 alchemy bonuses,
  // 7 Holy Fire, 8 artisan upgrades. Some rows spell it "usage"; a few custom ones have none.
  const USAGE = { 0: ['prefix', 'Prefixes', 'Prefix'], 1: ['suffix', 'Suffixes', 'Suffix'], 3: ['special', 'Special', 'Special'],
    4: ['alchemy', 'Alchemy bonuses', 'Alchemy bonus'], 7: ['holyfire', 'Holy Fire', 'Holy Fire'], 8: ['artisan', 'Artisan upgrades', 'Artisan upgrade'] };
  const OTHER_USAGE = ['other', 'Other', 'Other'];
  const usageOf = r => USAGE[(r.Usage !== undefined ? r.Usage : r.usage || '').trim()] || OTHER_USAGE;
  // An enchant's display name. LocalName is the shown name, but the English table has slips in
  // it (LocalName "Hamster Hunter" for Name "Hamster Hunter's"; "Ice Spear with 1 page completed"
  // copied onto every skill book page); LocalName2 has others ("Water Alchemy" for a wind one).
  // So: the localized name that matches the internal Name, else the one closest to it, else Name.
  const nameKey = s => String(s || '').toLowerCase().replace(/[^a-z0-9]/g, '');
  const nameWords = s => new Set(words(s).toLowerCase().replace(/'s\b/g, '').split(/[^a-z0-9]+/).filter(w => w && !/^\d+$/.test(w) && !['the', 'of', 'a'].includes(w)));
  function enchantName(r, sharedLocal) {
    const internal = (r.Name || '').trim(), key = nameKey(internal), iw = nameWords(internal);
    const cands = [tr(r.LocalName), tr(r.LocalName2)].filter(english);
    const exact = cands.find(c => nameKey(c) === key);
    if (exact) return exact;
    const score = c => { let n = 0; for (const w of nameWords(c)) if (iw.has(w)) n++; return n; };
    const ok = cands.filter(c => !(sharedLocal(c) && !score(c)));
    if (ok.length) return ok.reduce((a, b) => (score(b) > score(a) ? b : a));
    return internal ? cap(words(internal)) : '';
  }
  let ENCHANTS = null;
  function enchants() {
    if (ENCHANTS) return ENCHANTS;
    const list = rows('db/optionset.xml', 'OptionSet').filter(r => r.ID);
    // LocalName texts that several differently-named enchants share (copied slips).
    const owners = new Map();
    for (const r of list) {
      const l = tr(r.LocalName), stem = nameKey((r.Name || '').replace(/\d+$/, ''));
      if (!owners.has(l)) owners.set(l, new Set());
      owners.get(l).add(stem);
    }
    const shared = s => (owners.get(s) || new Set()).size > 1;
    ENCHANTS = list.map(r => {
      const u = usageOf(r), lv = num(r.Level);
      const name = enchantName(r, shared) || 'Enchant ' + r.ID;
      const alts = [...new Set([tr(r.LocalName), tr(r.LocalName2)].filter(s => english(s) && s !== name && !shared(s)))];
      return { id: r.ID, r, name, alt: alts.join(' / '), cat: u[0], rank: lv >= 1 && lv <= 15 ? rankLabel(lv) : '' };
    });
    return ENCHANTS;
  }
  let ENCH_BY_ID = null;
  const enchantById = id => (ENCH_BY_ID || (ENCH_BY_ID = new Map(enchants().map(e => [e.id, e])))).get(String(id).trim());
  // A chip for an enchant: its rank as the badge, its kind (Prefix, Suffix...) under the name.
  const enchantLink = e => Object.assign({ t: e.cat === 'artisan' ? 'upgrade' : 'enchant', id: e.id, label: e.name, sub: usageOf(e.r)[2] }, e.rank ? { mark: e.rank } : {});
  // AllowItem / BlockItem: tag patterns matched against an item's Category ("*/equip/*&*/weapon/*|*/robe/*":
  // "|" or, "&" and, "*" anything). In words, each "/a/b/" path as its tags' names.
  const TAG_WORDS = {
    equip: 'Equipment', weapon: 'Weapons', armor: 'Armor', heavyarmor: 'Heavy armor', lightarmor: 'Light armor', cloth: 'Clothing',
    accessary: 'Accessories', robe: 'Robes', bolt: 'Bolts', arrow: 'Arrows', powder: 'Powders', shield: 'Shields', head: 'Headgear',
    headgear: 'Headgear', helmet: 'Helmets', foot: 'Footwear', shoes: 'Shoes', hand: 'Gloves', glove: 'Gloves', gauntlet: 'Gauntlets',
    righthand: 'One-handed', twohand: 'Two-handed', lefthand: 'Left hand', guild: 'Guild', pendant: 'Pendants', fishing: 'Fishing',
    bait: 'Bait', bow: 'Bows', crossbow: 'Crossbows', edged: 'Swords', blunt: 'Blunt weapons', axe: 'Axes', lance: 'Lances',
    wand: 'Wands', staff: 'Staves', knuckle: 'Knuckles', claw: 'Claws', instrument: 'Instruments', tool: 'Tools', wing: 'Wings',
    usable: 'Usable items', steel: 'Metal', wood: 'Wooden', leather: 'Leather', armorboots: 'Armor boots', cylinder_turret: 'Cylinders',
    agelimit_cloth: 'Clothing (age-limited)', agelimit_robe: 'Robes (age-limited)', agelimit_shoes: 'Shoes (age-limited)',
    agelimit_glove: 'Gloves (age-limited)', agelimit_armorboots: 'Armor boots (age-limited)', agelimit_gauntlet: 'Gauntlets (age-limited)',
    inverse_transmutator: 'Inverse transmutation gear', no_elem: 'Items without elements', falias_treasure: 'Falias treasures',
    string: 'String', bracelet: 'Bracelets', cooking: 'Cooking tools', raid_dungeon: 'Raid dungeon',
  };
  const tagWord = s => TAG_WORDS[s.toLowerCase()] || cap(words(s.replace(/\d+$/, '')).toLowerCase());
  const patternAlts = p => String(p || '').split('|').map(a => a.replace(/[()]/g, '').trim()).filter(Boolean);
  function patterns(p) {
    return patternAlts(p).map(alt => {
      const paths = alt.split('&').map(x => x.split(/[/*]+/).filter(Boolean)).filter(x => x.length);
      if (!paths.length) return 'Anything';
      if (paths.length === 1 && paths[0].length === 1 && paths[0][0].toLowerCase() === 'equip') return 'Any equipment';
      // "*/equip/*&*/weapon/*" -> "Weapons": "equip" adds nothing next to a more specific path.
      const parts = paths.map(x => (x.length > 1 && x[0].toLowerCase() === 'equip' ? x.slice(1) : x))
        .filter((x, i, all) => !(all.length > 1 && x.length === 1 && x[0].toLowerCase() === 'equip'));
      return parts.map(x => x.map(tagWord).filter(Boolean).join(' › ')).join(' + ');
    });
  }
  // BlockItem parts that over half the enchants have (the usual exclusions).
  let COMMON_BLOCKS = null;
  function commonBlocks() {
    if (COMMON_BLOCKS) return COMMON_BLOCKS;
    const n = new Map(), all = enchants();
    for (const e of all) for (const b of new Set(patterns(e.r.BlockItem))) n.set(b, (n.get(b) || 0) + 1);
    return (COMMON_BLOCKS = new Set([...n].filter(([, c]) => c > all.length / 2).map(x => x[0])));
  }
  // The kinds of equipment an enchant's AllowItem pattern takes, for the enchant list's Equipment
  // filter. One that goes on any equipment is "Any equipment" (its own choice in the filter), not every
  // kind, so a kind's filter shows the enchants made for it.
  const ONTO_KINDS = [['Weapons', /weapon|edged|blunt|axe|lance|bow|crossbow|wand|staff|knuckle|cylinder|atlatl/], ['Armor & clothing', /armor|cloth|robe/],
    ['Headgear', /head|helmet/], ['Gloves', /hand|glove|gauntlet|bracelet/], ['Footwear', /foot|shoes|armorboots/], ['Accessories', /accessary|pendant/],
    ['Shields', /shield/], ['Instruments', /instrument/], ['Tools', /tool|fishing|cooking/]];
  const ANY_EQUIP = 'Any equipment';
  function ontoKinds(p) {
    const alts = patternAlts(p).map(a => a.toLowerCase());
    if (!alts.length) return [];
    if (alts.some(a => a.replace(/[^a-z_/&]/g, '').split('&').every(x => x.replace(/\//g, '') === 'equip'))) return [ANY_EQUIP];
    return ONTO_KINDS.filter(([, re]) => alts.some(a => re.test(a))).map(k => k[0]);
  }
  const reEsc = s => s.replace(/[.+?^${}()|[\]\\]/g, '\\$&');
  function matcher(p) {
    const alts = patternAlts(p).map(alt => alt.split('&').map(x => x.trim().toLowerCase().replace(/^\*+|\*+$/g, '')).filter(Boolean)
      .map(core => new RegExp(core.split('*').map(reEsc).join('.*'))));
    if (!alts.length) return null;
    return cat => alts.some(res => res.every(re => re.test(cat)));
  }
  function enchantPage(id) {
    const e = enchants().find(x => x.id === String(id));
    if (!e) return null;
    const r = e.r, u = usageOf(e.r);
    const info = [['Kind', u[2]]];
    if (e.rank) info.push(['Rank', e.rank]);
    const scroll = r.ScrollId && items().get(r.ScrollId);
    if (scroll) info.push(['Scroll', { t: 'item', id: r.ScrollId, label: scroll.name }]);
    // What the enchant does to the item, all in Effects (not the info box): its stats, then the repair
    // cost (RepairCostRate: extra percent, 100 = the description's "Repair Fee 2x"), always succeeding,
    // and working at any Enchant rank (IsIgnoreLevel).
    const fx = effects(r.OptionList);
    const sections = [];
    const fxCell = f => (f[3] && setElement(f[3]) ? { t: 'set_effect', id: setElement(f[3]).id, label: f[0] } : f.cell || f[0]);
    // A bonus's stat and value blue, a penalty's red (the stat too, as the enchant list does).
    const fxRows = fx.map(f => { const t = toneOf(f[0], f[1]); return [tonedAs(fxCell(f), t), toned(f[0], f[1]), f[2] || '—']; });
    const rc = num(r.RepairCostRate);
    if (rc) fxRows.push([{ text: 'Repair cost', tone: rc > 0 ? 'neg' : 'pos' }, { text: `×${+(1 + rc / 100).toFixed(2)}`, tone: rc > 0 ? 'neg' : 'pos' }, '—']);
    if (truthy(r.IsAlwaysSuccess)) fxRows.push(['Enchanting', 'Always succeeds', '—']);
    if (truthy(r.IsIgnoreLevel)) fxRows.push(['Enchanting', 'Works at any Enchant rank', '—']);
    if (fxRows.length) sections.push({ heading: 'Effects', blocks: [{ kind: 'table', columns: ['Stat', 'Value', 'Condition'], rows: fxRows }] });
    // What it goes on (the exceptions inside those kinds aren't listed: the list says enough).
    const allow = patterns(r.AllowItem);
    if (allow.length) sections.push({ heading: 'Applies to', blocks: [{ kind: 'list', items: allow }] });
    sections.push(sourcesSection('sets', e.id));
    // The game's description says what Effects and Applies to already show, so it's only the summary of
    // an enchant with neither.
    return { title: e.name, subtitle: e.alt || null, icon: null, category: [e.cat], infobox: info, summary: fxRows.length || allow.length ? '' : tr(r.OptionDesc), sections };
  }

  // Where an enchant's scroll or an item comes from. The game's files don't say, so it ships with
  // the wiki: window.GAME_SOURCES (sources.js) = {strings, sets: {enchant id: [source]}, items:
  // {item id: [source]}}, a source being {k: kind, t: 'monster' | 'item' | 'npc', id, n: name,
  // w: where, c: chance in percent, cn: how the chance works, p: price, a: amount}; n, w and cn are
  // indexes into strings. Things the game has that the list doesn't know (the game's own items and
  // enchants among them) say N/A.
  const SOURCE_KINDS = { monster: 'Monster drop', boss: 'Boss drop', shadow_mission: 'Shadow mission', dungeon: 'Dungeon drop',
    box: 'Box', gachapon: 'Gachapon', event: 'Event item', treasure: 'Treasure', pet_gift: 'Pet gift', collection: 'Collection book',
    chest: 'Dungeon chest', puzzle: 'Dungeon puzzle', shop: 'Shop', quest: 'Quest', npc_gift: 'NPC gift', other: 'Other' };
  const pctText = c => (c >= 1 ? +c.toFixed(2) : +c.toPrecision(2)) + '%';
  function sourcesOf(table, id) {
    const s = window.GAME_SOURCES, list = (s && s[table] && s[table][String(id)]) || [];
    const str = v => (typeof v === 'number' ? (s.strings || [])[v] || '' : v);
    return list.map(e => ({ ...e, n: str(e.n), w: str(e.w), cn: str(e.cn) }));
  }
  // Names in a note as links: enchants by name and slot ("Fox (prefix)", "with Fine prefix and
  // Swamp suffix"), and items, NPCs and monsters by their exact name. Quest, mission and shop tab
  // names stay text (a quest called "Malcolm's Ring" isn't the ring).
  let NOTE_NAMES = null;
  function noteNames() {
    if (NOTE_NAMES) return NOTE_NAMES;
    const ench = new Map(), byFirst = new Map(), kinds = new Map();
    for (const e of enchants()) {
      if (e.cat !== 'prefix' && e.cat !== 'suffix') continue;
      const k = nameKey(e.name) + '|' + e.cat;
      if (!ench.has(k)) ench.set(k, []);
      ench.get(k).push(e);
    }
    // name -> {item, npc, monster}; an item name several items share links nowhere.
    const add = (t, name, link) => {
      const n = String(name || '').replace(/\s+/g, ' ').trim();
      if (n.length < 3 || !/^[A-Z0-9]/.test(n)) return;
      if (!kinds.has(n)) {
        kinds.set(n, {});
        const w = n.split(' ')[0];
        if (!byFirst.has(w)) byFirst.set(w, []);
        byFirst.get(w).push(n);
      }
      const k = kinds.get(n);
      if (t in k) { if (t === 'item') k[t] = null; } else k[t] = link;
    };
    for (const [id, it] of items()) add('item', it.name, { t: 'item', id: String(id), label: it.name });
    for (const n of npcs()) add('npc', n.name, { t: 'npc', id: n.id, label: n.name, icon: npcFace(n.id) });
    for (const m of monsters()) add('monster', m.name, { t: 'monster', id: String(m.id), label: m.name });
    for (const d of dungeons().values()) add('dungeon', d.name, { t: 'dungeon', id: d.id, label: d.name });
    for (const l of byFirst.values()) l.sort((a, b) => b.length - a.length);
    return (NOTE_NAMES = { ench, byFirst, kinds });
  }
  // An enchant by the name a note gives it ("Snake Hunter's" is "Snake Hunter's" or "Snake Hunter").
  function enchantNamed(name, slot) {
    const { ench } = noteNames(), k = nameKey(name), other = slot === 'prefix' ? 'suffix' : 'prefix';
    const keys = [k, k.replace(/s$/, ''), k + 's'];
    // The slot a note gives first; then the other one ("the Rich (prefix)" is a suffix).
    const l = keys.map(x => ench.get(x + '|' + slot)).find(Boolean) || keys.map(x => ench.get(x + '|' + other)).find(Boolean) || [];
    // Same-named ones (other ranks or versions): the lowest rank, as drops come with.
    return l.slice().sort((a, b) => num(a.r.Level) - num(b.r.Level) || num(a.id) - num(b.id))[0] || null;
  }
  // Boss room chests: one locked chest per party member, and a key for each player that opens any of
  // them (the boss scripts' NewLockedChest and lock:chest key). An item only one particular chest gets
  // is still anyone's to find (the chests aren't marked), so that note goes; an item that chest never
  // gets can't be had solo, which is worth saying.
  const clarifyChest = s => String(s)
    .replace(/^(first chest only: )?in a party, only one of the chests has it$/i, '')
    .replace(/^chests \d+-\d+ only\b.*$/i, 'Not when solo');
  // Event codes in notes, as event names: "newyear2008_taiwan" -> "New Year 2008". The country in a code
  // is when that country's service ran it; here they're events, not regions.
  const EVENT_WORDS = { newyear: 'New Year', whiteday: 'White Day', valentine: "Valentine's", valentines: "Valentine's", stpatricksday: "St. Patrick's Day",
    liberationday: 'Liberation Day', childrensday: "Children's Day", dungeonevent: 'Dungeon', treasurebox: 'Treasure Box', christmas: 'Christmas',
    halloween: 'Halloween', thanksgiving: 'Thanksgiving', anniversary: 'Anniversary', worldcup: 'World Cup', harvestmoon: 'Harvest Moon',
    lunarnewyear: 'Lunar New Year', st: 'St.', patricks: "Patrick's", summer: 'Summer', winter: 'Winter', spring: 'Spring', autumn: 'Autumn', fishing: 'Fishing', golden: 'Golden' };
  const COUNTRY = '(?:korea|kor|taiwan|twn|china|chn|japan|jpn|usa|us|na|europe|eu|foreign|overseas|global)';
  const eventName = code => {
    const base = code.replace(new RegExp('_' + COUNTRY + '$', 'i'), '').replace(/_event$/i, '');
    const words = base.replace(/_/g, ' ').replace(/(?<=[a-z])(?=\d)|(?<=\d)(?=[a-z])/gi, ' ').replace(/(?<=[a-z])(?=[A-Z])/g, ' ').split(/\s+/).filter(Boolean);
    return words.map(w => EVENT_WORDS[w.toLowerCase()] || (w[0].toUpperCase() + w.slice(1))).join(' ');
  };
  const eventText = s => String(s)
    .replace(/\bserver event\b/gi, 'event')
    .replace(new RegExp('"?\\b([A-Za-z][A-Za-z0-9]*(?:_[A-Za-z0-9]+)*_' + COUNTRY + ')\\b"?', 'gi'), (m, code) => eventName(code))
    .replace(/\b(\w+) Event event\b/g, '$1 event');
  function noteLinks(text) {
    const s = eventText(text), spans = [], keep = [];
    // Enchants: the name is never anything else's, found or not.
    const enchantAt = (at, name, slot) => {
      keep.push([at, at + name.length]);
      const e = enchantNamed(name, slot);
      if (e) spans.push([at, at + name.length, { t: 'enchant', id: e.id, label: name }]);
    };
    for (const m of s.matchAll(/(^|: |, |\+ )([^,:+()]+?) \((prefix|suffix)\)/g)) enchantAt(m.index + m[1].length, m[2], m[3]);
    for (const m of s.matchAll(/\b([Ww]ith|and|or) ([A-Z][\w' -]*?) (prefix|suffix)\b/g)) enchantAt(m.index + m[1].length + 1, m[2], m[3]);
    // A pattern or manual: the manual's item, else the item it makes; nothing inside it otherwise.
    for (const m of s.matchAll(/\(((?:Tailoring|BlackSmith|Blacksmith) Manual - ((?:[^()]|\([^()]*\))*))\)/g)) {
      const at = m.index + 1, id = itemByName(m[1]) || itemByName(m[2]);
      keep.push([at, at + m[1].length]);
      if (id) spans.push([at, at + m[1].length, { t: 'item', id: String(id), label: m[1] }]);
    }
    // ...or the whole note, without brackets (a pattern shop's note once its tab is in the Kind column).
    const whole = /^(?:Tailoring|BlackSmith|Blacksmith) Manual - ([\s\S]+)$/.exec(s);
    if (whole) {
      const id = itemByName(s) || itemByName(whole[1]);
      keep.push([0, s.length]);
      if (id) spans.push([0, s.length, { t: 'item', id: String(id), label: s }]);
    }
    // Text that names something else: quest and mission titles, quoted names, shop tabs.
    for (const m of s.matchAll(/^(?:Quest(?: reward)?|Part-time job reward|Shadow Mission): ([^(]*)/g)) keep.push([m.index, m.index + m[0].length]);
    for (const m of s.matchAll(/"[^"]*"|, [^,()]+ tab\b|\bquest \d+|\bquest scroll: [^)]*|\bShadow [Mm]issions?\b/g)) keep.push([m.index, m.index + m[0].length]);
    const free = (a, b) => !keep.some(([x, y]) => a < y && b > x);
    // Items, NPCs and monsters, longest name first, whole names only: not part of a longer name
    // (a capitalised word right before or after: "Santa Claus", "Mysterious Cat").
    const { byFirst, kinds } = noteNames();
    for (let i = 0; i < s.length; i++) {
      if (i && /[\w']/.test(s[i - 1])) continue;
      const w = /^[^\s,;:()"]+/.exec(s.slice(i));
      const list = w && (byFirst.get(w[0]) || byFirst.get(w[0].replace(/[.']+$/, '')));
      if (!list) continue;
      const before = s.slice(0, i);
      if (/[A-Z][\w'-]* $/.test(before)) continue;
      const n = list.find(x => s.startsWith(x, i) && !/[\w]/.test(s[i + x.length] || '') && !/^ [A-Z]/.test(s.slice(i + x.length)) && free(i, i + x.length));
      if (!n) continue;
      const k = kinds.get(n), after = s.slice(i + n.length), many = n.includes(' ');
      // "Given by X": only an NPC. "X shop": the NPC first. Else the item (a one-word one only as
      // "for a X"), the monster (named in more than one word), the NPC.
      let link;
      if (/\bby $/.test(before)) link = k.npc;
      else if (/^ shop\b/.test(after)) link = k.npc || k.item;
      else link = k.dungeon || ((many || /\bfor an? $/.test(before)) && k.item) || (many && k.monster) || k.npc;
      if (!link) continue;
      spans.push([i, i + n.length, link]);
      keep.push([i, i + n.length]);
      i += n.length - 1;
    }
    if (!spans.length) return s;
    spans.sort((a, b) => a[0] - b[0]);
    const out = [];
    let at = 0;
    for (const [a, b, link] of spans) { if (a > at) out.push(s.slice(at, a)); out.push(link); at = b; }
    if (at < s.length) out.push(s.slice(at));
    return out;
  }
  // A long note's closing aside on a line of its own: "Gift from Owl pets (Eagle Owl, Snowy Owl) (while
  // the pet is age 14 or younger)" -> ["Gift from Owl pets (Eagle Owl, Snowy Owl)", "While the pet is age
  // 14 or younger"]. Only an aside in lower case (not a name, a manual, or "(prefix)").
  const asideLines = x => {
    const m = String(x).length > 60 && /^(.{15,}?\S)\s+\(([a-z][^()]{7,})\)$/.exec(String(x));
    return m ? [m[1], m[2].charAt(0).toUpperCase() + m[2].slice(1)] : [x];
  };
  // A source: a link when the wiki has its page (named by the source's own name when it has one: the
  // game's is sometimes left in Korean), else its name, else where it is.
  function sourceRef(s) {
    const m = s.t === 'monster' && monsterById(s.id), it = s.t === 'item' && items().get(String(s.id)), n = s.t === 'npc' && npcById(s.id);
    const q = s.t === 'quest' && questById(s.id), mi = s.t === 'quest' && missionById(s.id);
    if (q) return { t: 'quest', id: q.id, label: s.n || q.name };
    if (mi) return { t: 'mission', id: mi.id, label: mi.name };
    if (m) return { t: 'monster', id: String(s.id), label: s.n || m.name };
    if (it) return { t: 'item', id: String(s.id), label: s.n || it.name };
    if (n) return { t: 'npc', id: n.id, label: s.n || n.name, icon: npcFace(n.id) };
    const x = s.n || s.w;
    return x ? { lines: asideLines(x).map(noteLinks), sep: '' } : '—';
  }
  // A field boss's drop note, for item and monster pages. "Each player who fought it gets one item from
  // its list" is true of every boss line, so it isn't said (''). A boss whose time runs out leaves
  // undefeated, and everyone who fought it still gets one item from a second list (bossgroupdb
  // drop_items_escape): said as "if its time runs out", not "if it escapes".
  const bossNote = x => String(x)
    .replace(/^each player who fought it gets one item from its list(?:\s*\([^()]*\))?\.?$/i, '')
    .replace(/^also ([\d.]+%) per player if it escapes$/i, 'If its time runs out: $1 per player who fought it')
    .replace(/^only if it escapes: each player who fought it gets one item from its list(?:\s*\([^()]*\))?\.?$/i, 'Only if its time runs out: per player who fought it');
  // A Shadow Mission drop's chance on each difficulty ("Highest difficulty shown. Per kill, by difficulty:
  // Basic: 11.5%, Advanced: 1% (none on Hard)"), one difficulty a line: ["Basic: 11.5%", "Advanced: 1%",
  // "None on Hard"]; any other note as it is ([note]).
  const DIFF_NOTE = /^(?:Highest difficulty shown\. )?Per [\w ]+?, by difficulty: (.*?)(?:\s*\(none on ([^)]*)\))?\.?$/i;
  const diffNoteLines = x => {
    const m = DIFF_NOTE.exec(String(x).trim()), d = m ? [...m[1].matchAll(/([A-Za-z]+): ([\d.]+%)/g)].map(y => `${y[1]}: ${y[2]}`) : [];
    return d.length ? d.concat(m[2] ? [`None on ${m[2]}`] : []) : [x];
  };
  const isDiffLine = x => /^(?:[A-Za-z]+: [\d.]+%|None on .*)$/.test(x);
  // keep: an optional filter over the sources (a manual form's page keeps the shops selling that one).
  function sourcesSection(table, id, keep) {
    const all = keep ? sourcesOf(table, id).filter(keep) : sourcesOf(table, id);
    if (!all.length) return { heading: 'Where to get it', blocks: [{ kind: 'note', text: 'N/A: no known source.' }] };
    const cell = sourceRef;
    const placeIsSource = s => !s.n && !(s.t && s.id) && s.w;
    const SHOWN = 40;
    // The notes one to a line: the place, the amount, each note (the data joins them with "; "), the
    // price.
    const capFirst = x => x.charAt(0).toUpperCase() + x.slice(1);
    // The price: the shop's own when it sets one, else the item's (a shop sells one at the item's
    // price). Bundles without their own price show none (the item's price needn't be the bundle's);
    // jousting prizes cost points, which their place says.
    const own = table === 'items' && items().get(String(id)), std = own ? num(own.r.Price_Buy) : 0;
    const priceOf = s => (s.p ? num(s.p) : s.k === 'shop' && !s.a && std && !/jousting points/.test(s.w || '') ? std : 0);
    // A bare "1 in 3" says what the Chance column already does. What a line names is a link.
    // The source itself isn't named again: "Elen shop, Weapons tab" beside Elen is "Weapons tab".
    // A shop's tab goes in the Kind column ("Shop" over "Weapons tab"), not the notes.
    // "Patterns tab (Tailoring Manual - Bandana)": the tab is "Patterns tab", the manual stays a note.
    const TAB = /^([^()]*?\S tab)(?:\s+(\(.*\)))?$/i;
    const tabOf = s => { const p = s.k === 'shop' && String(s.w || '').split(/;\s+|,\s+/).map(x => x.trim()).find(x => TAB.test(x)); return p ? TAB.exec(p)[1] : ''; };
    const notSelf = (s, x) => {
      const c = cell(s), name = c && c.t && c.label, tab = tabOf(s);
      const self = name && new RegExp('^' + String(name).replace(/[.*+?^${}()|[\]\\]/g, '\\$&') + '(?:\'s)?(?: shop)?$', 'i');
      return x.split(/,\s+/).map(p => p.trim()).filter(p => !(self && self.test(p)))
        .map(p => { const m = tab && TAB.exec(p); return m && m[1] === tab ? (m[2] || '').replace(/^\((.*)\)$/, '$1') : p; }).filter(Boolean).join(', ');
    };
    // Draw weights ("weight 1000 of 378600") say what the Chance column already does: left out. A
    // weight note that also says the odds vary per player keeps that part.
    // Also "55 of 5479 per open (3 entries combined)" and a prop's "Group 20% per hit, 1 of 6".
    const noWeight = x => {
      x = x.replace(/\s*\((?:weight )?[\d,]+ of [\d,]+(?: weight)?\)/gi, '').replace(/\s*\([\d,]+ entries combined\)/gi, '');
      if (/^Group [\d.]+% per \w+, [\d,]+ of [\d,]+$/i.test(x)) return '';
      if (!/^(?:weight [\d,]+ of [\d,]+|[\d,]+ of [\d,]+)\b/i.test(x)) return x;
      const m = /\(([^()]*adjusted[^()]*)\)/i.exec(x);
      return m ? m[1] : '';
    };
    // "sum of 2 cases, each 20 in 826 + 4 in 826": the item is in the roll more than once and the Chance
    // column already adds them up; the pieces ("+ 4 in 826", "x 1 in 3") are how. Said in words, the
    // pieces left out (the "; " split leaves some on the next note).
    const sumNote = x => x.replace(/^sum of (\d+) cases?, each .*$/i, (m, n) => (+n > 1 ? `${n} separate rolls, added together` : ''))
      .replace(/\s+[+x]\s+[\d,]+ in [\d,]+/g, '');
    // A shadow mission's difficulties go in the Kind column, not the notes: "Highest difficulty shown. Per
    // kill, by difficulty: Intermediate: 0.25%, Advanced: 0.5% (none on Basic)" -> "Intermediate 0.25%,
    // Advanced 0.5%", or just the names when the chance is the same on each. Its "Shadow missions" place
    // says no more than the kind.
    // (Also "Per reward roll, by difficulty: ...", without the first sentence.)
    const DIFF = /^(?:Highest difficulty shown\. )?Per [\w ]+?, by difficulty: (.*?)(?:\s*\(none on [^)]*\))?\.?$/i;
    const diffsOf = s => {
      const m = s.k === 'shadow_mission' && String(s.cn || '').split(/;\s+/).map(x => DIFF.exec(x.trim())).find(Boolean);
      if (!m) return null;
      const d = [...m[1].matchAll(/([A-Za-z]+): ([\d.]+%)/g)].map(x => [x[1], x[2]]);
      return d.length ? (new Set(d.map(x => x[1])).size === 1 ? [d.map(x => x[0]).join(', ')] : d.map(x => `${x[0]}: ${x[1]}`)) : null;
    };
    // Each difficulty with its own chance on a line of its own.
    const kindOf = s => { const k = SOURCE_KINDS[s.k] || 'Other', d = diffsOf(s); return d ? { lines: [[k], ...d.map(x => [x])], sep: '' } : k; };
    const notesOf = (s, price = true) => ({ lines: [...new Set([...(placeIsSource(s) ? [] : String(s.w || '').split(/;\s+/)), s.a ? '×' + s.a : '', ...String(s.cn || '').split(/;\s+/), price && priceOf(s) ? priceOf(s).toLocaleString('en-US') + ' G' : '']
      .filter(x => !(diffsOf(s) && (DIFF.test(String(x).trim()) || /^shadow missions?$/i.test(String(x).trim()))))
      .map(x => bossNote(noWeight(sumNote(notSelf(s, String(x || '').trim()))).trim())))]
      .filter(x => x && !(s.c != null && /^[\d,]+ in [\d,]+$/.test(x)))
      .map(clarifyChest).filter(Boolean).flatMap(diffNoteLines).flatMap(asideLines).map(capFirst).map(noteLinks), sep: '' });
    // One table per way to get it: shops apart from drops, and so on. In each, the likeliest first (a
    // common item drops from hundreds of monsters), then the sources without a chance (a sure way).
    const GROUPS = [['Shops', ['shop']], ['Monster drops', ['monster', 'boss']], ['Dungeons and shadow missions', ['chest', 'puzzle', 'dungeon', 'shadow_mission']],
      ['Quests and gifts', ['quest', 'npc_gift', 'pet_gift', 'collection']], ['Boxes and gachapons', ['box', 'gachapon', 'treasure', 'event']]];
    const groupOf = s => (GROUPS.find(g => g[1].includes(s.k)) || ['Other'])[0];
    const blocks = [];
    let chances = false;
    for (const title of GROUPS.map(g => g[0]).concat('Other')) {
      const list = all.filter(s => groupOf(s) === title);
      if (!list.length) continue;
      const byChance = list.filter(s => s.c != null).sort((a, b) => b.c - a.c || (a.k || '').localeCompare(b.k || ''));
      const sure = list.filter(s => s.c == null).sort((a, b) => (a.k || '').localeCompare(b.k || ''));
      const shown = byChance.slice(0, SHOWN).concat(sure);
      if (byChance.length) chances = true;
      // Shops: the NPC, the shop tab, the price.
      const inner = [title === 'Shops' ? { kind: 'table', title, columns: ['Shop', 'Tab', 'Price', 'Notes'],
        rows: shown.map(s => [cell(s), tabOf(s) ? capFirst(tabOf(s)) : '—', priceOf(s) ? priceOf(s).toLocaleString('en-US') + ' G' : '—', notesOf(s, false)]) }
        : { kind: 'table', title, columns: ['Source', 'Kind', 'Chance', 'Notes'],
          rows: shown.map(s => [cell(s), kindOf(s), s.c != null ? pctText(s.c) : '—', notesOf(s)]) }];
      if (byChance.length > SHOWN) inner.push({ kind: 'note', text: `The ${SHOWN} likeliest of ${byChance.length.toLocaleString('en-US')} are shown.` });
      // Each kind of source folds away under its name and count, all shut at first; a manual's page (with
      // keep) opens its shops: who sells that manual is the point.
      inner[0].title = '';
      const open = !!keep && title === 'Shops';
      blocks.push({ kind: 'details', summary: `${title} (${list.length.toLocaleString('en-US')})`, open, blocks: inner, table: inner[0], group: title });
    }
    if (chances) blocks.push({ kind: 'note', text: 'Chances are per kill, opening or clear. Events and the game\'s settings can change what drops.' });
    return { heading: 'Where to get it', wide: true, blocks };
  }

  // Versions of an item that a source gives with other stats than the item's own: enchanted, made at a
  // set quality, with more max durability, or worn. GAME_SOURCES.variants = {item: [{e: [prefix, suffix]
  // (0 none), q: quality or [low, high], d: [base, max durability], c: [durability, of max], x: [[stat,
  // change]], xc: [[stat, change, condition]], s: [[kind, t, id, n, w, chance (-1 unknown), cn]]}]}
  // (strings indexes, -1 none). Sources that give the same version share it.
  const VAR_STATS = { Par_AttackMin: ['Min Damage'], Par_AttackMax: ['Max Damage'], Par_WAttackMin: ['Min Injury Rate', '%'], Par_WAttackMax: ['Max Injury Rate', '%'],
    Par_CriticalRate: ['Critical', '%'], Par_AttackBalance: ['Balance', '%'], Par_Defense: ['Defense'], Par_ProtectRate: ['Protection'],
    Str: ['STR'], Int: ['INT'], Dex: ['DEX'], Will: ['Will'], Luck: ['Luck'], MaxHP: ['Max HP'], MaxMP: ['Max MP'], MaxStamina: ['Max Stamina'],
    CombatPower: ['Combat Power'], MagicAttack: ['Magic Attack'], MagicDefense: ['Magic Defense'] };
  const signed = n => (n > 0 ? '+' : n < 0 ? '−' : '') + Math.abs(n);
  const deltaText = d => (Array.isArray(d) ? (d[0] === d[1] ? signed(d[0]) : `${signed(d[0])}~${Math.abs(d[1])}`) : signed(d));
  const condParts = c => { const x = conditionCell(c); return typeof x === 'string' ? [x] : x.lines[0]; };
  // The stats a version of an item has at a quality, from its manual's quality rule (null without one).
  function qualityStatsOf(itemId, q) {
    const Q = (((window.GAME_SOURCES || {}).crafting || {}).quality) || null;
    const r = Q && (recipes().byProduct.get(String(itemId)) || []).find(x => x.kind === 'manual' && Q.forms[x.form]);
    if (!r) return null;
    const [ri, base] = Q.forms[r.form], [ks, bands] = Q.rules[ri];
    const band = bands.find(([lo, hi]) => q >= lo && q <= hi);
    return band ? ks.map((k, i) => [Q.keys[k], base[i], base[i] + band[2][i]]) : null;
  }
  const Q_LABEL = { attack_min: 'Min Damage', attack_max: 'Max Damage', wound_min: 'Min Injury Rate', wound_max: 'Max Injury Rate', critical: 'Critical', balance: 'Balance',
    defense: 'Defense', protect: 'Protection', durability_max: 'Max durability', durability: 'Durability' };
  const Q_PCT = { wound_min: 1, wound_max: 1, critical: 1, balance: 1 };
  function variantsSection(id) {
    const S = window.GAME_SOURCES || {}, list = (S.variants || {})[String(id)];
    const it = items().get(String(id));
    if (!list || !list.length || !it) return null;
    const str = v => (v >= 0 ? (S.strings || [])[v] || '' : '');
    const r = it.r;
    // What's different: a part per enchant (the enchant, then its
    // effects under it), then what they make of the item's own numbers, the quality and its stats, the
    // durability; a line between parts.
    const line = parts => ({ lines: [parts], sep: '' });
    // A change in color: blue up, red down. "Balance 50% → 55%" (the new number colored) for the item's
    // own numbers; "STR +15" (the change colored) for the rest.
    const tone = up => (up ? 'pos' : 'neg');
    const arrow = (label, b, to, pct, up) => ({ text: `${label} ${b}${pct} → ${to}${pct}`, tone: tone(up) });
    const PAR_OF = { 'Min Damage': 'Par_AttackMin', 'Max Damage': 'Par_AttackMax', 'Min Injury Rate': 'Par_WAttackMin', 'Max Injury Rate': 'Par_WAttackMax',
      Critical: 'Par_CriticalRate', Balance: 'Par_AttackBalance', Defense: 'Par_Defense', Protection: 'Par_ProtectRate' };
    const AMT = /^([+\-−])?(\d+(?:\.\d+)?)(?:~(\d+(?:\.\d+)?))?(%)?$/;
    const fxLine = f => {
      const m = AMT.exec(String(f[1] || '').trim()), cond = f[2] ? [' (', ...(typeof f[2] === 'string' ? [f[2]] : f[2].lines ? f[2].lines[0] : [f[2]]), ')'] : [];
      if (!m) return f[2] ? line([`${f[0]}${f[1] ? ' ' + f[1] : ''}`, ...cond]) : `${f[0]}${f[1] ? ' ' + f[1] : ''}`;
      const down = m[1] === '-' || m[1] === '−', k = PAR_OF[f[0]];
      if (k && !f[2] && r[k] !== undefined) {
        const b = num(r[k]), lo = +m[2], hi = m[3] !== undefined ? +m[3] : lo, s = down ? -1 : 1, pct = (VAR_STATS[k] || [])[1] || '';
        const a = Math.max(0, b + s * lo), z = Math.max(0, b + s * hi);
        return arrow(f[0], b, a === z ? a : `${Math.min(a, z)}~${Math.max(a, z)}`, pct, !down);
      }
      return cond.length ? line([tonedLine(f[0], f[1]), ...cond]) : tonedLine(f[0], f[1]);
    };
    const what = v => {
      const secs = [];
      const ench = (v.e || []).map(e => e && enchantById(e)).filter(Boolean);
      for (const e of ench) {
        const kind = usageOf(e.r)[2];
        secs.push([line([{ t: 'enchant', id: e.id, label: e.name }, ` · ${kind}${e.rank ? ', rank ' + e.rank : ''}`]), ...effects(e.r.OptionList).map(fxLine)]);
      }
      // With two enchants, the item's own numbers with both on (unconditional effects only).
      const own = (v.x || []).filter(([k]) => /^Par_/.test(k) && r[k] !== undefined).map(([k, d]) => {
        const [label, pct = ''] = VAR_STATS[k] || [titleCase(words(k))];
        const b = num(r[k]), lo = Array.isArray(d) ? d[0] : d, hi = Array.isArray(d) ? d[1] : d;
        const to = lo === hi ? Math.max(0, b + lo) : `${Math.max(0, b + lo)}~${Math.max(0, b + hi)}`;
        return arrow(label, b, to, pct, lo + hi >= 0);
      });
      if (own.length && ench.length > 1) secs.push(['With both, the item\'s stats:', ...own]);
      if (v.q !== undefined) {
        const q = v.q, qs = !Array.isArray(q) && qualityStatsOf(id, q);
        // The stats the quality gives; the quality itself only when they aren't known (a random quality,
        // or an item no manual makes).
        const got = qs ? qs.filter(([k, b, to]) => b !== to && k !== 'durability').map(([k, b, to]) => arrow(Q_LABEL[k], b, to, Q_PCT[k] ? '%' : '', to > b)) : [];
        secs.push(got.length ? got : [Array.isArray(q) ? `Made with a random quality score from ${q[0]} to ${q[1]}` : `Made with a quality score of ${q}`]);
      }
      const dur = [];
      if (v.d) dur.push(arrow('Max durability', v.d[0], v.d[1], '', v.d[1] > v.d[0]));
      if (v.c) dur.push({ text: v.c[0] ? `Comes worn: ${v.c[0]} of ${v.c[1]} durability` : 'Comes broken (0 durability): repair it before use', tone: 'neg' });
      if (dur.length) secs.push(dur);
      return { lines: secs, rule: true, stack: true };
    };
    // Its sources, the likeliest first; a version from many places lists the first few.
    const FROM = 6;
    const best = v => Math.max(...v.s.map(s => (s[5] < 0 ? 101 : s[5])));
    const from = v => {
      const ss = v.s.slice().sort((a, b) => (b[5] < 0 ? 101 : b[5]) - (a[5] < 0 ? 101 : a[5]));
      const all = ss.map(([k, t, sid, n, w, c, cn]) => {
        const ref = sourceRef({ k, t: t || undefined, id: sid || undefined, n: str(n), w: str(w) });
        // (A source's aside, on a line of its own in its cell, goes after it here.)
        const kind = SOURCE_KINDS[k] || 'Other', refParts = ref && ref.lines ? [].concat(...ref.lines.map((l, i) => (i ? [' · '] : []).concat(l))) : [ref];
        // A shop's random enchanted stock: its tab, how often this one is rolled, and its price.
        if (k === 'shop') {
          const tab = /,\s*([^,]*\btab\b[^,]*)$/.exec(str(w)), price = /\(([\d,]+) gold\)/.exec(str(cn));
          return [...refParts, ` · ${tab ? tab[1].charAt(0).toUpperCase() + tab[1].slice(1) : 'Shop'}${c >= 0 && c < 100 ? ', ' + pctText(c) + ' of the rolls' : ''}${price ? ', ' + price[1] + ' G' : ''}`];
        }
        return [...refParts, ` · ${kind}${c >= 0 ? ', ' + pctText(c) : ''}`];
      });
      // Lines that read the same (two monster rows for one Giant Bear) once.
      const one = new Set(), uniq = all.filter(l => { const k = JSON.stringify(l); return !one.has(k) && one.add(k); });
      const lines = uniq.slice(0, FROM);
      if (uniq.length > FROM) lines.push([`and ${uniq.length - FROM} more`]);
      return { lines, sep: '' };
    };
    const rows = list.slice().sort((a, b) => best(b) - best(a)).map(v => [what(v), from(v)]);
    // The table folds away under its count.
    return { heading: 'Versions with other stats', wide: true, blocks: [{ kind: 'details', summary: `${rows.length} version${rows.length === 1 ? '' : 's'}`, blocks: [
      { kind: 'note', text: 'These sources give this item with stats that differ from the ones above. A range (1~2) is rolled when the item is made. Chances are per kill, opening or clear for that version.' },
      { kind: 'table', columns: ['What\'s different', 'From'], rows }] }] };
  }

  // Item Category strings and the items that have each (for matching tag patterns fast: many
  // items share a Category).
  let CAT_GROUPS = null;
  function catGroups() {
    if (CAT_GROUPS) return CAT_GROUPS;
    const m = new Map();
    for (const [id, it] of items()) {
      const c = (it.r.Category || '').toLowerCase();
      if (!c) continue;
      if (!m.has(c)) m.set(c, []);
      m.get(c).push(id);
    }
    return (CAT_GROUPS = [...m]);
  }

  // ---------- item upgrades ----------
  // db/itemupgradedb.xml: one <upgrade> per upgrade an NPC can make to an item:
  //   id; name (internal); localname and desc (text); effect ("modify(balance,1);modify(critical,-1)");
  //   need_ep and need_gold (the cost); item_filter (a pattern on the item's Category, read like an
  //   enchant's AllowItem); upgraded_min..upgraded_max (how many upgrades the item may already have);
  //   available_npc ("ferghus;edern": the NPCs' keys). Gem upgrades have need_gem ("<item>,<size in cm>;...")
  //   and gem_upgraded_min..max in place of upgraded_min..max.
  // An id can have two rows: the original and one marked rebalanced="true" (dagger_balance1: 1 EP and
  // 200 G at Ferghus and Edern; its rebalanced row: 5 EP and 500 G, Meles too). The rebalanced row is
  // shown, and the original only where an id has no rebalanced row: most upgrades (1,371 ids, all the
  // later ones such as the knuckles' artisan upgrades) exist only as rebalanced rows, so the rebalanced
  // set is the one in use, and a plain row with a rebalanced twin is the version that row replaced.
  //
  // available_npc keys, as the pages show them. NPC pages link them (npcRef).
  const UPG_STATS = {
    balance: 'Balance', critical: 'Critical', attack_max: 'Max Damage', attack_min: 'Min Damage', durability_max: 'Max Durability',
    attack_range: 'Range', wound_max: 'Max Injury Rate', wound_min: 'Min Injury Rate', defense: 'Defense', protect: 'Protection',
    collecting_speed: 'Gathering speed', collecting_bonus: 'Gathering bonus', splash_radius: 'Splash radius', splash_damage: 'Splash damage',
    // The descriptions: "increase defense against the melee attacks", "the auto-defense rate against
    // long-range attacks", "... against magic attacks".
    immune_melee: 'Melee auto-defense', immune_ranged: 'Ranged auto-defense', immune_magic: 'Magic auto-defense',
    // "Reduces the amount of mana used during magic charge" (manause_revised +4); "Reduces the amount of
    // mana vaporized during a Wand switch" (manaburn_revised +8).
    manause_revised: 'Mana use reduction', manaburn_revised: 'Mana Burn reduction', magic_damage: 'Magic Damage', casting_speed: 'Casting speed',
  };
  const UPG_PCT = { balance: 1, critical: 1, wound_min: 1, wound_max: 1 };

  // Artisan upgrades: luckyupgrade(<set>) adds a few of a set's results at random: artisan enchants
  // (Usage 8), each one's value random within its range. What a set gives isn't in the game's files:
  // sources.js has it (GAME_SOURCES.artisan = {set: {n: [[how many, rate]], o: [[enchant id,
  // rate]]}}). { counts: [{ count, pct }], opts: [{ e (the enchant), fx (its effects), mods
  // ({builder stat: [low, high]}), pct (its share of each pick) }] }, or null when the set isn't known.
  // A param's builder stat (BUILD_STATS): artisan results are mostly the character's own stats.
  const ART_KEY = { str: 'str', dex: 'dex', int: 'int', will: 'will', luck: 'luck', lifemax: 'hp', manamax: 'mp', staminamax: 'sp',
    def: 'def', defence: 'def', prot: 'prot', attmin: 'dmgMin', attmax: 'dmgMax', crit: 'crit', critical: 'crit', rate: 'bal',
    wattmin: 'wMin', wattmax: 'wMax' };
  // An amount ("+(2~5)", "-(1~2)", "+3") as [low, high]; null if it isn't one.
  function rangeOf(v) {
    const m = /^([+-])?\(?([+-])?(\d+(?:\.\d+)?)(?:~(\d+(?:\.\d+)?))?\)?%?$/.exec(String(v).replace(/\s+/g, ''));
    if (!m) return null;
    const a = +m[3], b = m[4] !== undefined ? +m[4] : a;
    return (m[1] === '-') !== (m[2] === '-') ? [-b, -a] : [a, b];
  }
  const ARTISAN = new Map();
  function artisanSet(n) {
    n = String(n).trim();
    if (ARTISAN.has(n)) return ARTISAN.get(n);
    const s = window.GAME_SOURCES && window.GAME_SOURCES.artisan && window.GAME_SOURCES.artisan[n];
    let out = null;
    if (s) {
      const nSum = s.n.reduce((t, c) => t + c[1], 0) || 1, oSum = s.o.reduce((t, o) => t + o[1], 0) || 1;
      out = { counts: s.n.map(([count, r]) => ({ count, pct: r * 100 / nSum })), opts: [] };
      for (const [id, r] of s.o) {
        const e = enchantById(id);
        if (!e) continue;
        const mods = {};
        for (const m of String(e.r.OptionList || '').matchAll(/SetParamOnEquip\s*\(\s*(\w+)\s*,\s*([^)]*\)?)\s*\)/gi)) {
          const k = ART_KEY[m[1].toLowerCase()], v = rangeOf(m[2]);
          if (k && v) mods[k] = mods[k] ? [mods[k][0] + v[0], mods[k][1] + v[1]] : v;
        }
        out.opts.push({ e, fx: effects(e.r.OptionList).map(fxText).join(', '), mods, pct: r * 100 / oSum, w: r });
      }
      // The chance each result is among what the upgrade gives (get, in %): how many it gives is
      // rolled first, then each is picked by weight from those not given yet (GameSystemD's pick skips
      // the ones already taken), so a result comes at most once.
      const appear = (ws, n) => {
        const acc = ws.map(() => 0);
        const walk = (left, depth, p) => {
          if (depth === n) return;
          const total = left.reduce((t, i) => t + ws[i], 0);
          if (!total) return;
          for (const i of left) {
            const q = p * ws[i] / total;
            acc[i] += q;
            walk(left.filter(j => j !== i), depth + 1, q);
          }
        };
        walk(ws.map((_, i) => i), 0, 1);
        return acc;
      };
      const ws = out.opts.map(o => o.w), get = out.opts.map(() => 0);
      for (const c of out.counts) appear(ws, c.count).forEach((p, i) => { get[i] += p * c.pct; });
      out.opts.forEach((o, i) => { o.get = get[i]; });
    }
    ARTISAN.set(n, out);
    return out;
  }
  // Special upgrades. db/itemenhancecommon.xml: <ItemSet N S R> (a weapon, its S version and its R
  // version) and <BonusDesc enhance_bonus local_name unit> (the bonuses' names). A Blue Upgrade
  // Stone (12283) turns the weapon into its S version or raises an S weapon's level, a Red one
  // (12284) the same for R, and a Conversion Stone (12286) switches S and R, keeping the level. The
  // server's side (costs, odds, each level's bonuses) is in sources.js: GAME_SOURCES.enhance =
  // { levels: [[level, gold, success %, destroyed %, level lost %]], convert: [[level, gold]],
  // bonus: {Attr_ItemEnhanceSet: [[bonus, [value at level 1..10]]]} } (merge.py says how it was read).
  const STONES = { S: '12283', R: '12284', X: '12286' };
  const ENHANCE_MAX = 3;
  let ENHANCE = null;
  function enhance() {
    if (ENHANCE) return ENHANCE;
    const byN = new Map(), bySR = new Map(), names = new Map();
    for (const r of rows('db/itemenhancecommon.xml', 'BonusDesc')) {
      if (r.enhance_bonus) names.set(r.enhance_bonus.toLowerCase(), { label: tr(r.local_name).trim() || titleCase(words(r.enhance_bonus)), unit: r.unit || '' });
    }
    for (const r of rows('db/itemenhancecommon.xml', 'ItemSet')) {
      if (!r.N || !r.S || !r.R) continue;
      byN.set(r.N, { S: r.S, R: r.R });
      bySR.set(r.S, { type: 'S', N: r.N, other: r.R });
      bySR.set(r.R, { type: 'R', N: r.N, other: r.S });
    }
    const src = (window.GAME_SOURCES && window.GAME_SOURCES.enhance) || {};
    // The server's table goes to level 10, but its upgrade manager stops at level 3: the constructor
    // sets the maximum to 3 and nothing changes it (GameSystemD 0x67dac168); an upgrade or conversion
    // past it is refused ("can't go to a higher level"), and even the GM command is held to it.
    const upTo = l => l[0] <= ENHANCE_MAX;
    return (ENHANCE = { byN, bySR, names, levels: (src.levels || []).filter(upTo), convert: (src.convert || []).filter(upTo), bonus: src.bonus || {} });
  }
  // An S or R weapon's bonuses: [{ key, label, unit, values }] (null when its set isn't known).
  function enhanceBonuses(it) {
    const E = enhance(), set = String(it.r.Attr_ItemEnhanceSet || '').trim(), list = E.bonus[set];
    if (!list) return null;
    return list.map(([key, values]) => Object.assign({ key, values }, E.names.get(key.toLowerCase()) || { label: titleCase(words(key)), unit: '' }));
  }
  // A special upgrade bonus's builder stat (BUILD_STATS), where it has one.
  const SPECIAL_KEY = { attackmax: 'dmgMax', attackmax_const: 'dmgMax', attackmin: 'dmgMin', attackmin_const: 'dmgMin', critical: 'crit', balance: 'bal',
    durabilitymax: 'dur', attackrange: 'range', magicdamage: 'magic', castingspeed: 'cast', woundmax: 'wMax', woundmin: 'wMin', defence: 'def', protect: 'prot',
    splashdamage: 'splashD', splashradius: 'splashR', manauserevised: 'manause', manaburnrevised: 'manaburn' };
  const bonusText = (b, v) => `${b.label} ${v > 0 ? '+' : ''}${v}${b.unit}`;
  const failText = l => (l[3] >= 100 ? 'The weapon breaks' : l[3] > 0 ? `${num(l[3])}% to break` : '') ||
    (l[4] >= 100 ? 'Loses a level' : l[4] > 0 ? `${num(l[4])}% to lose a level` : '—');
  // An item page's "Special upgrade" section, for a weapon that has S and R versions or is one.
  function specialUpgradeSection(id) {
    const E = enhance(), it = items().get(String(id)), stone = k => { const s = items().get(STONES[k]); return s ? itemLink(STONES[k], s.name) : 'Upgrade stone'; };
    const link = v => { const x = items().get(v); return x ? itemLink(v, x.name) : 'Item ' + v; };
    const n = E.byN.get(String(id)), sr = E.bySR.get(String(id));
    if (n) {
      return { heading: 'Special upgrade', blocks: [
        { kind: 'table', columns: ['Stone', 'Becomes'], rows: [[stone('S'), link(n.S)], [stone('R'), link(n.R)]] },
        { kind: 'note', text: 'Only a weapon that already has a gem upgrade can be special upgraded. The first stone makes it the S or R version at level 1; the levels after are on that version\'s page.' },
      ] };
    }
    if (!sr || !E.levels.length) return null;
    const bon = enhanceBonuses(it);
    const blocks = [{ kind: 'kv', rows: [['Type', sr.type], ['Raised with', stone(sr.type)], ['Converts to', link(sr.other)], ['Made from', link(sr.N)]] }];
    blocks.push({ kind: 'table', columns: ['Level', 'Bonus', 'Gold', 'Success', 'If it fails'],
      // Each bonus a line.
      rows: E.levels.map(l => [String(l[0]), bon ? { lines: bon.map(b => bonusText(b, b.values[l[0] - 1])), sep: '' } : '—', gold(l[1]), num(l[2]) + '%', l[2] >= 100 ? '—' : failText(l)]) });
    // The conversion's cost: "20,000 G times the level" when it's that, else each level's.
    const per = E.convert.length && E.convert[0][1] / E.convert[0][0];
    const conv = per && E.convert.every(c => c[1] === c[0] * per) ? `${gold(per)} times the level` : E.convert.map(c => `${gold(c[1])} at level ${c[0]}`).join(', ');
    blocks.push({ kind: 'note', text: (bon ? '' : "This weapon's bonuses at each level aren't known. ") +
      `It goes up to level ${ENHANCE_MAX}. Each try uses one ${sr.type === 'S' ? 'Blue' : 'Red'} Upgrade Stone. A Conversion Stone switches it to the ${sr.type === 'S' ? 'R' : 'S'} version at the same level, always successfully, for ${conv}.` });
    return { heading: 'Special upgrade', blocks };
  }

  // Repairs. GAME_SOURCES.repair (sources.js; the game's files don't have it): npcs {npc: [success
  // rate %, item filter...]}, and the cost of a point: the first of kinds ([[category tags ("&": both),
  // table]]) the item's category has picks a table, the NPC's rate (90-100) a row [b, f] in it, and
  // x = price * f + min(price * f, b); a point costs floor(x * 5 / sqrt(max(durability, 5))), at
  // least 1. Rates outside 90-100 use [0, 1]; categories in no table [1000000, 1]. Enchants with a
  // repair cost (RepairCostRate) change it further.
  function repairCostPerPoint(it, rate) {
    const R = (window.GAME_SOURCES && window.GAME_SOURCES.repair) || {}, cat = '/' + String(it.r.Category || '').toLowerCase().replace(/^\/+|\/+$/g, '') + '/';
    const kind = (R.kinds || []).find(([tags]) => tags.split('&').every(t => cat.includes('/' + t.replace(/^\/+|\/+$/g, '') + '/')));
    const row = kind && rate >= 90 && rate <= 100 ? R.tables[kind[1]][rate - 90] : rate >= 90 && rate <= 100 ? [1000000, 1] : [0, 1];
    const a = num(it.r.Price_Buy) * row[1], x = a + Math.min(a, row[0]);
    return Math.max(1, Math.floor(x * 5 / Math.sqrt(Math.max(Math.floor(num(it.r.Par_DurabilityMax) / 1000), 5))));
  }
  function repairSection(id) {
    const it = items().get(String(id)), R = window.GAME_SOURCES && window.GAME_SOURCES.repair;
    if (!it || !R || !Math.floor(num(it.r.Par_DurabilityMax) / 1000)) return null;
    const cat = (it.r.Category || '').toLowerCase();
    // No repair table where repairing means nothing: scrolls, manuals and patterns (their durability is
    // uses), items that never wear (no_abrasion) and ones the game won't repair (not_repairable).
    if (/\/(scroll|manual|no_abrasion|not_repairable)\//.test('/' + cat + '/')) return null;
    // One row per success rate and price (each rate has its own price per point; an NPC with a
    // discount, or Jenna's free repairs, is a row of its own), with the NPCs that repair at it.
    // npcs: {npc: {r: success rate, f: [item filters], p: price factor when not 1 (Jenna's 0: free),
    // m: met only inside a mission}}.
    const groups = new Map();
    for (const [npc, n] of Object.entries(R.npcs || {})) {
      if (!n.f.some(f => f === '*' || ((t => t && t(cat))(upgradeTest(f))))) continue;
      const price = Math.floor(repairCostPerPoint(it, n.r) * (n.p ?? 1)), k = n.r + '|' + price;
      if (!groups.has(k)) groups.set(k, { r: n.r, price, npcs: [] });
      const ref = npcRef(npc);
      groups.get(k).npcs.push(n.m ? (typeof ref === 'object' ? Object.assign({}, ref, { label: ref.label + ' (in a Shadow Mission)' }) : ref + ' (in a Shadow Mission)') : ref);
    }
    if (!groups.size) return null;
    const label = x => String(typeof x === 'object' ? x.label : x);
    const rows = [...groups.values()].sort((a, b) => b.r - a.r || b.price - a.price)
      .map(g => [g.r + '%', g.price ? gold(g.price) : 'Free', g.npcs.sort((a, b) => label(a).localeCompare(label(b)))]);
    // How a repair goes (GameSystemD's repair, the same for one point and for all of them): the gold
    // for every point is taken first, each point succeeds at the rate, and a failed point's gold is
    // given back but the item's max durability drops by a point. With a repair protection potion on
    // the item (GetSupportRepair > 0), any failures in the repair spend one use (the last use removes
    // it) and no failed point lowers the max durability. A blessed item fails half as often, and any
    // failure takes the blessing away once all the points are done.
    // The table first, a one-line tip, and the details behind "How repairs work".
    return { heading: 'Repair', blocks: [
      { kind: 'table', columns: ['Success rate', 'Gold per point', 'NPCs'], rows },
      { kind: 'note', text: 'Blessed with Holy Water, or protected by a Repair Protection Potion? Repair all at once. Otherwise it makes no difference.' },
      // One short fact a line, under a heading per case.
      { kind: 'details', summary: 'How repairs work', blocks: [
        { kind: 'list', title: 'Every repair', items: [
          { lead: 'Each point', text: 'is its own roll at the NPC\'s success rate.' },
          { lead: 'Failed points', text: 'are refunded, but each lowers the max durability by 1. You only pay for the points that succeed.' },
          { lead: 'One at a time or all at once', text: 'comes out the same for an item that isn\'t blessed or protected.' },
          { lead: 'Price:', text: 'a higher success rate costs more; an enchant with a repair cost changes it.' }] },
        { kind: 'list', title: 'Blessed with Holy Water', items: [
          { lead: 'Each point', text: 'fails half as often.' },
          { lead: 'Any failed point', text: 'removes the blessing.' },
          { lead: 'Repair all:', text: 'the blessing comes off only after every point has rolled, so all of them get the better odds.' },
          { lead: 'One point at a time:', text: 'the blessing is gone after the first failure; the points after it roll at the normal rate.' }] },
        { kind: 'list', title: 'Repair Protection Potion', items: [
          { lead: 'Failed points', text: 'don\'t lower the max durability.' },
          { lead: 'One use', text: 'is spent per repair that has any failures, however many failed.' },
          { lead: 'Repair all:', text: 'one use covers every failure.' },
          { lead: 'One point at a time:', text: 'a use for each point that fails.' }] },
      ] },
    ] };
  }

  // ---------- hidden stats ----------
  // An item's XML holds numbers its tooltip doesn't show. What each does was traced in the game
  // server's code; only the ones it confirms are shown.
  // The attributes of one tag of an item's XML (<xml ...> at the top, <defence_bonus .../> on shields).
  const tagAttrs = (xml, tag) => { const m = new RegExp('<' + tag + '\\b([^>]*)>', 'i').exec(String(xml || '')); return m ? xmlAttrs(m[1]) : {}; };
  const round2 = v => +(+v).toFixed(2);
  // Minutes as days, hours and minutes.
  const minutes = n => { const d = Math.floor(n / 1440), h = Math.floor(n % 1440 / 60), m = n % 60; return [d ? d + (d === 1 ? ' day' : ' days') : '', h ? h + ' h' : '', m ? m + ' min' : ''].filter(Boolean).join(' ') || '0 min'; };
  const ELEMENTS = { fire: 'Fire', water: 'Water', wind: 'Wind', earth: 'Earth' };
  function hiddenStats(it) {
    const r = it.r, t = it.tags, top = tagAttrs(r.XML, 'xml'), d = tagAttrs(r.XML, 'defence_bonus'), rows = [];
    const add = (label, value) => rows.push([label, value]);
    const weapon = String(it.cat || '').startsWith('weapon');
    // Shields (<defence_bonus>): added to the Defense skill when it blocks.
    if (num(d.melee_bonus)) add('Defense skill vs melee', `+${num(d.melee_bonus)} Defense`);
    if (num(d.default)) add('Defense skill vs ranged', `+${num(d.default)} Defense`);
    if (num(d.attacked_critical_bonus)) add('Blocked attacker\'s Critical', `−${num(d.attacked_critical_bonus)}%`);
    if (num(d.no_defence_melee_bonus) && num(d.melee_bonus)) add('Melee hits while not defending', `−${num(d.no_defence_melee_bonus)} damage`);
    // Armor: a chance to shrug off a hit's stun and knockback (value × 150%, pieces add up).
    for (const [k, w] of [['immune_melee', 'melee'], ['immune_ranged', 'ranged'], ['immune_magic', 'magic']]) {
      if (+top[k] > 0) add(`Resist stun and knockback (${w})`, Math.floor(+top[k] * 150) + '%');
    }
    if (num(top.mana_reduce_percent)) add('Mana cost', `−${num(top.mana_reduce_percent)}% (with a main-hand weapon)`);
    // Weapons: splash (normal hits also strike enemies around the target) and stamina per hit.
    if (weapon && +top.splash_damage > 0) {
      add('Splash damage', round2(+top.splash_damage * 100) + '%');
      if (num(top.splash_radius)) add('Splash radius', num(top.splash_radius));
      if (num(top.splash_angle)) add('Splash angle', `${num(top.splash_angle)}° each side`);
    }
    if (weapon && +top.stamina_usage > 0) add('Stamina per hit', round2(top.stamina_usage));
    // Staves: charge gained per bolt cast (at 30 / 60 / 100 charge, bolts deal +5 / +10 / +15%).
    for (const [k, s] of [['30101', 'Lightning Bolt'], ['30201', 'Firebolt'], ['30301', 'Icebolt']]) if (num(top[k])) add(`Charge per ${s}`, num(top[k]));
    // Cylinders: their element's alchemy ×(1 + 3 × bonus%), the others ×(1 − bonus%).
    const el = ELEMENTS[String(top.alchemyelemental || '').toLowerCase()], eb = num(top.alchemyelementalbonus);
    if (el && eb) { add(`${el} alchemy`, `+${3 * eb}%`); add('Other alchemy elements', `−${eb}%`); }
    // Ammunition.
    if (+top.hit_ratio_bonus) add('Aim', `+${round2(+top.hit_ratio_bonus * 100)}%`);
    if (+top.damage_ratio_bonus) add('Damage', `+${round2(+top.damage_ratio_bonus * 100)}%`);
    if (num(top.range_damage_bonus)) add('Ranged skill damage', `+${num(top.range_damage_bonus)}`);
    if (num(top.potion_poison_damage)) add('Poison coating', num(top.potion_poison_damage));
    if (num(top.poison_damage)) add('Poison (while coated)', `+${num(top.poison_damage)}`);
    // Time limits and trading.
    if (num(top.expire)) add('Expires', minutes(num(top.expire)) + ' after it\'s obtained');
    else if (/^\d{12}/.test(top.expire_date || '')) { const s = top.expire_date; add('Expires on', `${s.slice(0, 4)}-${s.slice(4, 6)}-${s.slice(6, 8)} ${s.slice(8, 10)}:${s.slice(10, 12)}`); }
    if (num(top.expireday) && t.has('create_lite_pet')) add('Pet lasts', num(top.expireday) + ' days');
    if (num(top.duration)) {
      if (t.has('combatpower_weaken')) add('Effect lasts', minutes(num(top.duration)));
      else if ([...t].some(x => /^(fast_casting_potion|boost_attack_potion|production_rate_modify|itemexp_rate_up|itemdrop_rate_up|fishingdrop_booster)/.test(x))) add('Effect lasts', seconds(num(top.duration)));
    }
    if (num(top.trade_limit)) add('Can be traded', `${num(top.trade_limit)} more time${num(top.trade_limit) === 1 ? '' : 's'}`);
    // Gear that only works with a skill at a rank.
    if (num(top.fit_skillid)) add('Works only with', `${skillName(top.fit_skillid)} ${skillRank(num(top.fit_skilllevel_min))} or higher`);
    // Production and skill bonuses.
    if (num(top.tool_bonus_rate) && num(top.tool_bonus_skill)) {
      const p = items().get(String(top.tool_bonus_production));
      add(`${skillName(top.tool_bonus_skill)} success`, `+${num(top.tool_bonus_rate)}%` + (p ? ` making ${p.name}` : ''));
    }
    const pb = tagAttrs(r.XML, 'production_bonus_rate');
    if (num(pb.default_abs)) add('Production success (this prop)', `+${num(pb.default_abs)}%`);
    if (num(top.exp_bonus)) add('Shadow Mission EXP', `+${num(top.exp_bonus)}%`);
    if (num(top.money_bonus)) add('Shadow Mission gold', `+${num(top.money_bonus)}%`);
    if (num(top.enchantbonus)) add('Enchant success', `+${num(top.enchantbonus)}%`);
    if (num(top.first_aid_bonus)) add('First Aid healing', `+${num(top.first_aid_bonus)} HP`);
    if (num(top.trainingbonus)) add('Animal Training success', `+${num(top.trainingbonus)}`);
    if (num(top.restoration_success)) add('Restoration chance', num(top.restoration_success) + '%');
    if (num(top.restoration_success_bonus)) add('Restoration chance', `+${num(top.restoration_success_bonus)}%`);
    if (num(top.collection_max)) add('Items to collect', num(top.collection_max));
    if (num(top.base_size)) add('Average size', num(top.base_size));
    if (num(top.mana_forming) && skillById(top.mana_forming)) add('Holds', skillLink(top.mana_forming, skillName(top.mana_forming)));
    if (num(top.direct_skill) && skillById(top.direct_skill)) add('Use', skillLink(top.direct_skill, skillName(top.direct_skill)));
    if (top.random_option_category === '1') add('Enchant', 'A random beginner enchant (1 of 28)');
    return rows;
  }

  // ---------- crafting ----------
  // db/manualform.xml: <Tailoring>, <Tailoring_Arbeit> (part-time job patterns) and <BlackSmith>, each
  // <ManualForm ManualItemID (the pattern or manual) FormID ProductItemID ManualNameLocal Essentials
  // ("<category>, <count> ; ...") EssentialDesc (their names, "; "-separated) CompleteEssentials /
  // CompleteDesc (to finish it, "(...)") Price>. db/production.xml: a section per skill (<Spinning>,
  // <Weaving>, <Refine>...), each <Production ProductItemId ProductionCount Essentials EssentialDesc
  // Additions AdditionDesc PrimeTool ManaRequired SuccessRate_0..15 (by skill rank: 0 Novice ... 15
  // rank 1)>.
  const CRAFT_SKILLS = { Tailoring: 'Tailoring', Tailoring_Arbeit: 'Tailoring', BlackSmith: 'Blacksmithing', Spinning: 'Weaving', Weaving: 'Weaving',
    Refine: 'Refining', Milling: 'Milling', PotionMaking: 'Potion Making', Handicraft: 'Handicraft', ManaForming: 'Magic Craft', CrystalMaking: 'Magic Craft',
    MetalExtraction: 'Metal Conversion', Carpentry: 'Carpentry', StageTicket: 'Make Theatre Mission Pass' };
  // (<MetalExtraction> is Metal Conversion's, the alchemy skill that turns ore into ingots, not
  // Metallurgy's: the game's handler for that section is Metal Conversion's.)
  // "Cheap Fabric; Common Finishing Thread" with "*/x/, 1 ; */y/, 2": [[name, count, item id, pattern]].
  // The pattern (an item category filter, as the game reads it) is what the recipe really takes: the
  // name text has slips ("Iron Ingot" for a copper finish's */ingot/copperingot/*) and names no item for
  // some ("Dragon Scale", "Prairie Dragon's Eye"). One item matching it is that item, named as it is.
  function materials(desc, list) {
    const names = String(tr(desc) || '').replace(/^\(|\)$/g, '').split(/\s*;\s*/).map(s => s.replace(/,$/, '').trim());
    const parts = String(list || '').replace(/^\(|\)$/g, '').split(';').map(s => s.trim()).filter(Boolean);
    const counts = parts.map(s => num((s.split(',')[1] || '').trim()) || 1);
    const pats = parts.map(s => (s.split(',')[0] || '').trim());
    return names.map((n, i) => {
      const ids = pats[i] ? patternItems(pats[i]) : [], one = ids.length === 1 ? items().get(ids[0]) : null;
      return [one ? one.name : n, counts[i] || 1, one ? ids[0] : null, pats[i] || ''];
    }).filter(m => m[0]);
  }
  // The items a material pattern takes (cached; recipes share patterns). Test items and duplicates of a
  // name (event copies) don't make it ambiguous: the first of a name stands for it.
  const PATTERN_ITEMS = new Map();
  function patternItems(p) {
    if (PATTERN_ITEMS.has(p)) return PATTERN_ITEMS.get(p);
    const test = upgradeTest(p), seen = new Map();
    if (test) for (const [c, list] of catGroups()) if (test(c)) for (const id of list) { const it = items().get(id); if (it && !seen.has(it.name)) seen.set(it.name, id); }
    const ids = [...seen.values()];
    PATTERN_ITEMS.set(p, ids);
    return ids;
  }
  // What finishes a manual's item: CompleteDesc's choices, one per line ("(Fine Leather; Braid)" then
  // "(Copper Ingot; Fine Leather; Braid)"... the ingots giving a colored finish), with CompleteEssentials'
  // counts, its choices in the same order ("(..., 2; ..., 1)\n(... - col1:773333)"). [[[name, count]]].
  function finishing(desc, list) {
    const split = s => String(s || '').split(/\)\s*(?:\\n|\r?\n)?\s*\(|\r?\n|\\n/).map(x => x.replace(/[()]/g, '').replace(/\s*-\s*col\d+:\w+/g, '').trim()).filter(Boolean);
    const names = split(tr(desc)), counts = split(list);
    return names.map((a, i) => materials(a, counts[i] || ''));
  }
  // Each way to finish on its own line, every material a link to its item.
  const finishCell = alts => ({ lines: alts.map(a => a.map(materialCell)), sep: ' + ', rule: true, stack: true });
  // A list of materials (or ingredients), one per line.
  const matsCell = cells => (cells.length ? { lines: cells.map(c => [c]) } : '—');
  let ITEM_BY_NAME = null;
  const itemByName = name => {
    if (!ITEM_BY_NAME) {
      ITEM_BY_NAME = new Map();
      for (const [id, it] of items()) { const k = it.name.toLowerCase(); ITEM_BY_NAME.set(k, ITEM_BY_NAME.has(k) ? null : id); }
    }
    return ITEM_BY_NAME.get(String(name).toLowerCase()) || null;
  };
  // A material as a link to its item (the one its pattern takes, else the one item with that name); a
  // pattern several items fit ("Any Dragon's Eye") links to the first and says how many others do.
  const materialCell = ([name, n, known, pat]) => {
    const label = n > 1 ? `${name} ×${n}` : name;
    const id = known || itemByName(name);
    if (id) return itemLink(id, label);
    const many = pat ? patternItems(pat) : [];
    return many.length ? { lines: [[itemLink(many[0], label), ` (or ${many.length - 1} other${many.length > 2 ? 's' : ''})`]], sep: '' } : label;
  };
  // Cooking ingredients: the recipe's amounts are weights (Mixed Fruit's 75, 50 and 40), each one's
  // share of the dish is its weight over their sum, in whole percents adding up to 100 (the largest
  // remainders round up).
  function ingredientCells(list) {
    const sum = list.reduce((t, x) => t + x[1], 0) || 1;
    const raw = list.map(x => x[1] * 100 / sum), pct = raw.map(Math.floor);
    let left = 100 - pct.reduce((t, x) => t + x, 0);
    for (const i of raw.map((v, i) => [v - Math.floor(v), i]).sort((a, b) => b[0] - a[0]).map(x => x[1])) { if (left-- <= 0) break; pct[i]++; }
    return list.map(([name, , id], i) => itemLink(id, `${name} ${pct[i]}%`));
  }
  // An optional extra: whether it makes the dish better or worse (its quality window).
  const extraCell = ([name, , id, lo, hi]) => itemLink(id, `${name} (${hi > 0 ? 'better' : lo < 0 ? 'worse' : 'no change'})`);
  let RECIPES = null;
  function recipes() {
    if (RECIPES) return RECIPES;
    const byProduct = new Map(), byManual = new Map(), byForm = new Map();
    const add = (m, k, v) => { if (!m.has(k)) m.set(k, []); m.get(k).push(v); };
    // A file's skill sections (CRAFT_SKILLS' keys), each with its rows.
    const sections = (file, tag) => {
      const t = String(FILES[file] || '').replace(/<!--[\s\S]*?-->/g, '');
      return Object.keys(CRAFT_SKILLS).map(sec => {
        const m = new RegExp('<' + sec + '>([\\s\\S]*?)</' + sec + '>').exec(t);
        return [sec, m ? [...m[1].matchAll(new RegExp('<' + tag + '\\b([^>]*)/?>', 'g'))].map(x => attrsOf(x[1])) : []];
      });
    };
    for (const [sec, rowsM] of sections('db/manualform.xml', 'ManualForm')) {
      for (const r of rowsM) {
        if (!r.ProductItemID) continue;
        // form: the FormID, this manual's own recipe (its page). Two forms can make one item from
        // different materials under the same name (Blacksmith Manual - Dagger 20002 and 20102).
        const rec = { kind: 'manual', skill: CRAFT_SKILLS[sec] || sec, job: sec === 'Tailoring_Arbeit', manual: r.ManualItemID, name: tr(r.ManualNameLocal).trim() || r.ManualNameEng || '',
          product: r.ProductItemID, mats: materials(r.EssentialDesc, r.Essentials), finish: finishing(r.CompleteDesc, r.CompleteEssentials), count: 1,
          form: r.FormID ? String(r.FormID) : '', price: num(r.Price) };
        add(byProduct, r.ProductItemID, rec);
        if (r.ManualItemID) add(byManual, r.ManualItemID, rec);
        if (rec.form && !byForm.has(rec.form)) byForm.set(rec.form, rec);
      }
    }
    for (const [sec, rowsP] of sections('db/production.xml', 'Production')) {
      for (const r of rowsP) {
        if (!r.ProductItemId) continue;
        const rates = Array.from({ length: 16 }, (_, i) => num(r['SuccessRate_' + i]));
        add(byProduct, r.ProductItemId, { kind: 'production', skill: CRAFT_SKILLS[sec] || titleCase(words(sec)), name: tr(r.Title).trim(), product: r.ProductItemId,
          mats: materials(r.EssentialDesc, r.Essentials).concat(materials(r.AdditionDesc, r.Additions)), count: num(r.ProductionCount) || 1,
          tool: String(r.PrimeTool || '').replace(/\//g, ' ').trim(), mana: num(r.ManaRequired), rates });
      }
    }
    // Synthesis and cooking: only the server has those (sources.js).
    const S = window.GAME_SOURCES || {};
    const nameOf = id => { const it = items().get(String(id)); return it ? it.name : 'Item ' + id; };
    for (const [result, count, mats, rates] of S.synthesis || []) {
      add(byProduct, String(result), { kind: 'synthesis', skill: 'Synthesis', product: String(result), count, rates, mats: mats.map(([id, n]) => [nameOf(id), n, String(id)]) });
    }
    const actions = new Map(rows('db/cookingaction.xml', 'action').map(a => [a.name, tr(a.localname).trim()]));
    for (const [result, action, ess, add2] of S.cooking || []) {
      add(byProduct, String(result), { kind: 'cooking', skill: 'Cooking', product: String(result), action: actions.get(action) || titleCase(words(action)),
        ess: ess.map(([id, pct, lo, hi]) => [nameOf(id), pct, String(id), lo, hi]), extra: add2.map(([id, pct, lo, hi]) => [nameOf(id), pct, String(id), lo, hi]) });
    }
    return (RECIPES = { byProduct, byManual, byForm });
  }
  // db/manualform.xml's rows by FormID (their English name ties a shop source line to its form).
  let ROWS_BY_FORM = null;
  const rowsByForm = () => ROWS_BY_FORM || (ROWS_BY_FORM = new Map(rows('db/manualform.xml', 'ManualForm').filter(r => r.FormID).map(r => [String(r.FormID), r])));
  // Who sells each manual form (GAME_SOURCES.shops rows carry it as x.mf): form -> [[npc, price]].
  let FORM_SELLERS = null;
  function formSellers() {
    if (FORM_SELLERS) return FORM_SELLERS;
    FORM_SELLERS = new Map();
    for (const [npc, s] of Object.entries((window.GAME_SOURCES || {}).shops || {})) {
      for (const [, rows] of s.t) for (const r of rows) {
        const f = r[4] && r[4].mf;
        if (!f) continue;
        const k = String(f);
        if (!FORM_SELLERS.has(k)) FORM_SELLERS.set(k, []);
        if (!FORM_SELLERS.get(k).some(x => x[0] === npc)) FORM_SELLERS.get(k).push([npc, r[1]]);
      }
    }
    return FORM_SELLERS;
  }
  const formName = r => r.name || (items().get(r.manual) || {}).name || 'Manual';
  const formLink = r => ({ t: 'manual', id: r.form, label: formName(r), icon: itemIcon(r.manual) });
  const sellerLinks = form => (formSellers().get(String(form)) || []).map(([npc]) => npcById(npc)).filter(Boolean).map(n => ({ t: 'npc', id: n.id, label: n.name, icon: npcFace(n.id) }));
  // The Manuals & patterns list: one entry per form.
  const MANUAL_CATS = [['blacksmithing', 'Blacksmithing manuals'], ['tailoring', 'Tailoring patterns'], ['tailoring-job', 'Part-time job patterns']];
  const manualCat = r => (r.skill === 'Blacksmithing' ? 'blacksmithing' : r.job ? 'tailoring-job' : 'tailoring');
  function manualRows() {
    const out = [];
    for (const [form, r] of recipes().byForm) {
      const p = items().get(r.product);
      if (!p) continue;
      const sold = (formSellers().get(form) || []).length;
      // diff: the recipe's difficulty as a rank ("Rank D"), sorted by diffLv; equip: the made item's
      // category, for the list's Equipment filter.
      const f = (((window.GAME_SOURCES || {}).crafting || {}).forms || {})[form];
      out.push({ id: form, name: formName(r), icon: itemIcon(r.manual), cat: [manualCat(r)], sub: `Makes ${p.name}`, makes: p.name, skill: r.skill,
        price: r.price || null, sold: sold ? 'Yes' : null, tags: p.name, diff: f ? 'Rank ' + CRAFT_RANKS[f[0]] : null, diffLv: f ? f[0] : null,
        equip: ITEM_CAT_TITLE[p.cat] || null });
    }
    return out;
  }
  // Making it: GAME_SOURCES.crafting = {table: {Tailoring | Blacksmith: 31 rows [worst, bad, good, best]},
  // forms: {form: [difficulty, max progress, first rank allowed, expected attempts per rank]}}. How the game
  // runs it (traced in the G13 server): each attempt uses
  // one set of the materials and adds max progress x a roll (best 1-1.5x, good 0.5-1x, bad 0-0.5x, worst
  // -0.5-0x: progress can drop); the odds come from the table's row clamp(rank - difficulty + 15, 0, 30).
  // Which materials and their quality don't matter. Blacksmithing below the item's rank always fails.
  const CRAFT_RANKS = ['Novice', 'F', 'E', 'D', 'C', 'B', 'A', '9', '8', '7', '6', '5', '4', '3', '2', '1', 'Dan 1', 'Dan 2', 'Dan 3'];
  function manualMakingBlocks(r) {
    const C = (window.GAME_SOURCES || {}).crafting || {}, f = (C.forms || {})[String(r.form)];
    if (!f) return [];
    const [lv, mp, gate, att] = f, rows = (C.table || {})[r.skill === 'Blacksmithing' ? 'Blacksmith' : 'Tailoring'] || [];
    // The item is done when progress reaches 100%. Each result as a plain range: one that always gets
    // there "finishes it in one try"; one that can, "+X% to 100% (can finish it)"; the worst takes away.
    const pct = (a, b) => { const lo = Math.round(a * mp * 100), hi = Math.round(b * mp * 100);
      return lo >= 100 ? 'finishes it in one try' : hi >= 100 ? `+${lo}% to +100% (can finish it)` : `+${lo}% to +${hi}%`; };
    const loss = Math.min(100, Math.round(mp * 50));
    const kv = [['Difficulty', 'Rank ' + CRAFT_RANKS[lv]], ['Progress per attempt', { lines: [[`Best: ${pct(1, 1.5)}`], [`Good: ${pct(0.5, 1)}`], [`Bad: ${pct(0, 0.5)}`],
      [loss ? `Worst: takes away up to ${loss}% (progress can go back, but not below 0%)` : 'Worst: no progress']], sep: '' }]];
    if (r.skill === 'Blacksmithing') kv.push(['Rank needed', gate ? `${CRAFT_RANKS[gate]} or higher (below that every attempt fails)` : 'Any']);
    const table = { kind: 'table', title: 'By your rank', columns: ['Your rank', 'Best', 'Good', 'Bad', 'Worst', 'Attempts to finish (about)'],
      rows: CRAFT_RANKS.map((name, k) => {
        const row = rows[Math.max(0, Math.min(30, k - lv + 15))] || [0, 0, 0, 0], a = att[k];
        if (r.skill === 'Blacksmithing' && k < gate) return [name, '—', '—', '—', '100%', 'Always fails'];
        return [name, row[3] + '%', row[2] + '%', row[1] + '%', row[0] + '%', a ? String(a) : 'Never (it goes backwards more than forwards)'];
      }) };
    return [{ kind: 'kv', rows: kv }, table,
      { kind: 'list', title: 'Good to know', items: [
        { lead: 'Finished', text: 'when its progress reaches 100%.' },
        { lead: 'Each attempt', text: 'uses one set of the materials.' },
        { lead: 'Materials', text: '(which ones, and their quality) don\'t change the progress.' },
        { lead: 'Dex above 20', text: 'turns some Good results into Best.' },
        { lead: 'Making Mastery', text: 'cuts the failures.' },
        { lead: 'Rain', text: 'adds a little to each attempt.' }] }];
  }
  // The finished item: GAME_SOURCES.crafting.quality = {keys, rules: [[[stat index], [[from, to, [change]]],
  // [[skill level, [change]]]]], forms: {form: [rule, [base]]}, play}. How the game runs it (traced in the
  // G13 server): the finishing minigame gives a quality from
  // -100 to 100 (each mark's distance; a mark can't count closer than the rank allows, so perfect hits average
  // about 68 at Novice..D, 74 at C..9, 82 at rank 8 and up), and the item's category picks the band table.
  // Blacksmith weapons and some armor also get a bonus from rank 5 up. The finishing material only colors.
  function qualityRule(form) {
    const Q = (((window.GAME_SOURCES || {}).crafting || {}).quality) || null, f = Q && Q.forms[String(form)];
    if (!f) return null;
    const [ks, bands, bonus] = Q.rules[f[0]];
    return { keys: ks.map(k => Q.keys[k]), base: f[1], bands, bonus, play: Q.play };
  }
  // A rule's stats at a quality (and skill level, for the rank bonus): {stat: value}.
  function qualityStats(q, quality, lv = -1) {
    const band = q.bands.find(([lo, hi]) => quality >= lo && quality <= hi) || q.bands[q.bands.length - 1];
    const add = q.bonus.filter(([l]) => lv >= l).pop();
    const o = {};
    q.keys.forEach((k, i) => { o[k] = q.base[i] + band[2][i] + (add ? add[1][i] : 0); });
    if (o.durability !== undefined && o.durability_max !== undefined) o.durability = Math.min(o.durability, o.durability_max);
    return o;
  }
  // The stats shown, each from the rule's own numbers or else the item's.
  const Q_GROUPS = [['Damage', ['attack_min', 'attack_max'], ['Par_AttackMin', 'Par_AttackMax']], ['Injury rate', ['wound_min', 'wound_max'], ['Par_WAttackMin', 'Par_WAttackMax'], '%'],
    ['Critical', ['critical'], ['Par_CriticalRate'], '%'], ['Balance', ['balance'], ['Par_AttackBalance'], '%'], ['Defense', ['defense'], ['Par_Defense']],
    ['Protection', ['protect'], ['Par_ProtectRate']], ['Durability', ['durability_max', 'durability'], ['Par_DurabilityMax', 'Par_DurabilityMax']]];
  function qualityCells(q, itemRow) {
    const groups = Q_GROUPS.filter(g => g[1].some(k => q.keys.includes(k)));
    const val = (o, g, i) => (o[g[1][i]] !== undefined ? o[g[1][i]] : g[2][i] === 'Par_DurabilityMax' ? Math.round(num(itemRow[g[2][i]]) / 1000) : num(itemRow[g[2][i]]));
    const text = (g, o) => {
      if (g[0] === 'Durability') { const mx = val(o, g, 0), cur = val(o, g, 1); return cur < mx ? `${mx} (comes worn to ${cur})` : String(mx); }
      return g[1].map((_, i) => val(o, g, i)).join('~') + (g[3] || '');
    };
    return { groups, text };
  }
  function manualQualityBlocks(r) {
    const q = qualityRule(r.form), p = items().get(r.product);
    if (!q || !p) return [];
    const { groups, text } = qualityCells(q, p.r);
    const smith = r.skill === 'Blacksmithing', play = (q.play || {})[smith ? 'Blacksmithing' : 'Tailoring'] || [];
    const good = play[2] ? Math.round(play[2][2]) : 82, top = Math.max(...q.bands.map(b => b[1]));
    const normal = qualityStats(q, 0), worst = qualityStats(q, -100), fine = qualityStats(q, good), best = qualityStats(q, top, 99);
    const blocks = [{ kind: 'table', columns: ['Stat', 'Normal', 'Worst', `Good (quality score ${good})`, q.bonus.length ? 'Best (rank 1)' : 'Best'],
      rows: groups.map(g => [g[0], text(g, normal), text(g, worst), text(g, fine), { text: text(g, best), tone: 'pos' }]) }];
    blocks.push({ kind: 'details', summary: 'Stats at each quality score', blocks: [{ kind: 'table', columns: ['Quality score', ...groups.map(g => g[0])],
      rows: q.bands.slice().reverse().map(([lo, hi]) => [lo === hi ? String(lo) : `${lo} to ${hi}`, ...groups.map(g => text(g, qualityStats(q, lo)))]) }] });
    if (q.bonus.length) {
      // One stat a line.
      const adds = ch => ({ lines: q.keys.map((k, i) => [k, ch[i]]).filter(([k, n]) => n && k !== 'durability').map(([k, n]) => `${Q_LABEL[k]} ${signed(n)}${Q_PCT[k] ? '%' : ''}`), sep: '' });
      blocks.push({ kind: 'table', title: 'Rank bonus (on top of the quality score)', columns: ['Your rank', 'Adds'],
        rows: q.bonus.map(([lv, ch], i) => ['Rank ' + CRAFT_RANKS[lv] + (i < q.bonus.length - 1 ? '' : CRAFT_RANKS[lv] === '1' ? ' and Dan' : ' and up'), adds(ch)]) });
    }
    // How the quality score comes about: a short list, the best a rank can do as a small table.
    const marks = smith ? 'hammer mark' : 'stitch';
    const tiers = [['Novice to D', play[0]], ['C to 9', play[1]], ['Rank 8 and up', play[2]]].filter(t => t[1]).map(([name, x]) => [name, `About ${Math.round(x[2])}`]);
    blocks.push({ kind: 'list', title: 'How the quality score comes about', items: [
      { lead: 'The finishing minigame:', text: `the closer each ${marks} lands, the higher the quality score (-100 to 100).` },
      { lead: 'Your rank', text: `limits how close a ${marks} can count, so even perfect hits only reach the quality score below.` },
      { lead: 'Quality score 100', text: 'needs every one perfect, and luck.' },
      { lead: 'Normal', text: 'is a quality score of -20 to 19: the item\'s own stats.' },
      { lead: 'The finishing material', text: 'only changes the color.' },
      { lead: 'Your name', text: 'goes on the item at a quality score of 80 or more, made at rank 9 or higher.' }] });
    if (tiers.length) blocks.push({ kind: 'table', title: 'Perfect hits give', columns: ['Your rank', 'Quality score'], rows: tiers });
    return blocks;
  }
  // A manual or pattern form's page: what it makes, what it takes, what it costs and who sells it; the
  // other forms that make the same item, so two "Blacksmith Manual - Dagger"s can be told apart.
  function manualPage(id) {
    const r = recipes().byForm.get(String(id));
    if (!r) return null;
    const p = items().get(r.product), m = items().get(r.manual);
    const info = [['Skill', skillByName(r.skill)], ['Makes', p ? itemLink(r.product, p.name) : 'Item ' + r.product]];
    if (m) info.push(['Item', itemLink(r.manual, m.name)]);
    if (r.price) info.push(['Price', { text: gold(r.price), tone: 'gold' }]);
    info.push(['Form ID', String(id)]);
    const sections = [{ heading: 'What it takes', blocks: [{ kind: 'kv', rows: [['Materials (each attempt)', matsCell(r.mats.map(materialCell))]].concat(r.finish.length ? [['To finish', finishCell(r.finish)]] : []) }] }];
    const making = manualMakingBlocks(r);
    if (making.length) sections.push({ heading: 'Making it', blocks: making });
    const finished = manualQualityBlocks(r);
    if (finished.length) sections.push({ heading: 'The finished item', blocks: finished });
    // Where to get it: the manual item's sources (GAME_SOURCES.items), keeping the shop lines that sell
    // this one by name ("Gilmore shop, Blacksmithing tab (BlackSmith Manual - Dagger)"; two forms with one
    // name share them) and every drop: a monster or chest drops the manual item, and the data doesn't say
    // which manual it is.
    // A shop line counts only when that NPC sells this very form (the shop list knows the form; two
    // forms can share a name), at the price the shop list gives.
    const eng = String((rowsByForm().get(String(id)) || {}).ManualNameEng || '').toLowerCase();
    const sold = new Map((formSellers().get(String(id)) || []).map(([npc, p]) => [npc, p]));
    const where = r.manual && sourcesSection('items', r.manual, s => s.k !== 'shop' || (sold.has(s.id) && eng && String(s.w || '').toLowerCase().includes('(' + eng + ')')));
    const drops = where && sourcesOf('items', r.manual).some(s => s.k !== 'shop');
    const sellers = sellerLinks(id);
    if (where && where.blocks.some(b => b.table)) {
      const shopT = (where.blocks.find(b => b.group === 'Shops') || {}).table;
      if (shopT) shopT.rows.forEach(row => { const npc = row[0] && row[0].id, p = sold.get(npc); if (p) row[2] = gold(p); });
      if (drops) where.blocks.unshift({ kind: 'note', text: `Drops and rewards give a ${(m && m.name) || 'manual'} whose recipe isn't fixed in the data; ${sold.size ? 'the shops below sell this one.' : 'no shop sells this one.'}` });
      sections.push(Object.assign(where, { wide: false }));
    } else sections.push({ heading: 'Where to get it', blocks: sellers.length ? [{ kind: 'links', title: 'Sold by', items: sellers }]
      : [{ kind: 'note', text: 'No shop sells this one; it comes from elsewhere (a quest, an event or another player).' }] });
    const others = (recipes().byProduct.get(r.product) || []).filter(x => x.kind === 'manual' && x.form && x.form !== String(id));
    if (others.length) sections.push({ heading: 'Other manuals for this item', blocks: [{ kind: 'table', columns: ['Manual', 'Materials', 'Sold by'],
      rows: others.map(x => [formLink(x), matsCell(x.mats.map(materialCell)), sellerLinks(x.form).length ? sellerLinks(x.form) : '—']) }] });
    return { title: formName(r), subtitle: p ? `Makes ${p.name}` : null, icon: itemIcon(r.manual), category: [manualCat(r)], kind: r.skill === 'Blacksmithing' ? 'Manual' : 'Pattern',
      infobox: info, summary: '', sections, more: p ? { label: 'The item it makes', href: '#/item/' + r.product } : undefined };
  }
  // A skill's link by its name, when the wiki has that skill.
  const skillByName = name => { const s = skills().find(x => x.name.toLowerCase() === String(name).toLowerCase()); return s ? skillLink(s.id, s.name) : name; };
  // Success by rank: the first rank where it's over 0 and rank 1.
  function successText(rates) {
    const lo = rates.findIndex(v => v > 0);
    if (lo < 0) return '—';
    return lo === 15 || rates[lo] === rates[15] ? `${rates[15]}%` : `${rates[lo]}% (${skillRank(lo)}) to ${rates[15]}% (rank 1)`;
  }
  function craftingSection(id) {
    const { byProduct, byManual } = recipes(), list = byProduct.get(String(id)) || [], blocks = [];
    const of = k => list.filter(r => r.kind === k), man = of('manual'), prod = of('production'), syn = of('synthesis'), cook = of('cooking');
    if (man.length) {
      // Each manual links to its own form's page (two can share a name), with who sells that one.
      // Difficulty: the recipe's rank (the manual's page has the odds and attempts by your rank).
      const diff = r => { const f = (((window.GAME_SOURCES || {}).crafting || {}).forms || {})[String(r.form)]; return f ? 'Rank ' + CRAFT_RANKS[f[0]] : '—'; };
      blocks.push({ kind: 'table', title: 'From a pattern or manual', columns: ['Skill', 'Pattern or manual', 'Difficulty', 'Materials', 'To finish', 'Sold by'],
        rows: man.map(r => {
          const m = items().get(r.manual), sold = r.form ? sellerLinks(r.form) : [];
          return [skillByName(r.skill), r.form ? formLink(r) : m ? itemLink(r.manual, r.name || m.name) : r.name, diff(r), matsCell(r.mats.map(materialCell)), finishCell(r.finish), sold.length ? sold : '—'];
        }) });
      // What it comes out as, from the worst quality to the best (with rank 1's bonus); forms with the
      // same result once.
      const it = items().get(String(id)), seen = new Set();
      for (const r of man) {
        const q = r.form && qualityRule(r.form);
        if (!q || !it) continue;
        const { groups, text } = qualityCells(q, it.r), top = Math.max(...q.bands.map(b => b[1]));
        const worst = qualityStats(q, -100), best = qualityStats(q, top, 99), normal = qualityStats(q, 0);
        // Durability: the max's range; a worst one that comes worn says so on its own line.
        const maxOnly = (g, o) => (g[0] === 'Durability' ? String(o.durability_max !== undefined ? o.durability_max : text(g, o)) : text(g, o));
        const rows = groups.map(g => {
          const lines = [[`${maxOnly(g, worst)} to `, { text: maxOnly(g, best), tone: 'pos' }, maxOnly(g, normal) !== maxOnly(g, best) ? ` (normally ${maxOnly(g, normal)})` : '']];
          if (g[0] === 'Durability' && worst.durability !== undefined && worst.durability < worst.durability_max) lines.push([`At the worst quality score it comes worn: ${worst.durability} of ${worst.durability_max} durability`]);
          return [g[0] === 'Durability' ? 'Max durability' : g[0], { lines, sep: '' }];
        });
        const key = JSON.stringify(rows);
        if (seen.has(key)) continue;
        seen.add(key);
        blocks.push({ kind: 'kv', title: `Made by hand: worst to best${seen.size > 1 || man.filter(x => x.form && qualityRule(x.form)).length > 1 ? ' (' + formName(r) + ')' : ''}`, rows });
      }    }
    if (prod.length) {
      blocks.push({ kind: 'table', title: 'Production', columns: ['Skill', 'Recipe', 'Materials', 'Makes', 'Success'],
        rows: prod.map(r => [skillByName(r.skill), r.name || '—', matsCell(r.mats.map(materialCell)), String(r.count), successText(r.rates)]) });
    }
    if (syn.length) {
      blocks.push({ kind: 'table', title: 'Synthesis', columns: ['Skill', 'Materials', 'Makes', 'Success'],
        rows: syn.map(r => [skillByName(r.skill), matsCell(r.mats.map(materialCell)), String(r.count), successText(r.rates)]) });
    }
    if (cook.length) {
      blocks.push({ kind: 'table', title: 'Cooking', columns: ['Skill', 'Method', 'Ingredients', 'Optional'],
        rows: cook.map(r => [skillByName(r.skill), r.action, matsCell(ingredientCells(r.ess)), matsCell(r.extra.map(extraCell))]) });
      blocks.push({ kind: 'note', text: 'Each ingredient\'s share of the dish: the closer the mix is to it, the better the dish. An optional extra added makes it better or worse.' });
    }
    // What the producing skills can give at random (random synthesis, Fragmentation, finds).
    blocks.push(...randomItemBlocks(id));
    // What a pattern or manual makes, each item once (some have several forms).
    const makes = [...new Map((byManual.get(String(id)) || []).map(r => [r.product, r])).values()];
    if (makes.length) {
      blocks.push({ kind: 'links', title: `Makes (${makes.length})`, items: makes.map(r => { const p = items().get(r.product); return p ? itemLink(r.product, p.name) : 'Item ' + r.product; }) });
    }
    return blocks.length ? { heading: makes.length && !list.length ? 'Crafting' : 'How it\'s made', blocks } : null;
  }

  // The Crafting list: everything a recipe makes, by skill (an item made several ways is under each).
  const CRAFT_CATS = [['tailoring', 'Tailoring'], ['blacksmithing', 'Blacksmithing'], ['weaving', 'Weaving'], ['refining', 'Refining'], ['milling', 'Milling'],
    ['potion-making', 'Potion Making'], ['handicraft', 'Handicraft'], ['magic-craft', 'Magic Craft'], ['metal-conversion', 'Metal Conversion'], ['carpentry', 'Carpentry'],
    ['synthesis', 'Synthesis'], ['cooking', 'Cooking'], ['theatre-mission-pass', 'Make Theatre Mission Pass']];
  const craftCat = skill => (CRAFT_CATS.find(c => c[1] === skill) || [slug(skill)])[0];
  function craftRows() {
    const out = [], at = new Map();
    for (const [id, list] of recipes().byProduct) {
      const it = items().get(id);
      if (!it) continue;
      const sk = [...new Set(list.map(r => r.skill))];
      at.set(id, out.push({ id, name: it.name, alt: it.local, icon: itemIcon(id), cat: sk.map(craftCat), sub: sk.join(', '), skills: sk.join(', '),
        recipes: list.length, tags: it.base }) - 1);
    }
    // What the producing skills can give at random (random synthesis, Fragmentation, finds), under their skill.
    const R = randomIndex();
    for (const [id, hits] of R.made) {
      const it = items().get(id);
      if (!it) continue;
      const sk = [...new Set(hits.map(h => skillName(h.t.s)).filter(Boolean))];
      if (at.has(id)) {
        const row = out[at.get(id)], all = [...new Set(row.skills.split(', ').concat(sk))];
        Object.assign(row, { cat: [...new Set(row.cat.concat(sk.map(craftCat)))], sub: all.join(', '), skills: all.join(', ') });
      } else out.push({ id, name: it.name, alt: it.local, icon: itemIcon(id), cat: sk.map(craftCat), sub: sk.join(', '), skills: sk.join(', '), recipes: 0, tags: it.base });
    }
    return out;
  }
  // How many Craftables a skill (by name) has in its list.
  const craftCount = name => cached('craftCount', () => {
    const n = new Map();
    for (const r of craftRows()) for (const c of [].concat(r.cat)) n.set(c, (n.get(c) || 0) + 1);
    return n;
  }).get(craftCat(name)) || 0;
  // GAME_SOURCES.randomOutputs = {t: [{k, s: skill, title, how, c: [column], r: [[item, [value], qty, note]]} or
  // {..., g: [[from item, [[item, [value], qty, note]]]]}], rules: [[skill, text]]}: what a producing skill can
  // give at random. made: item -> [{t, row}] (it can come out); from: item -> [{t, rows}] (using the skill on
  // it gives these); gives: item -> [{t, from, row}] (it comes out of using the skill on another).
  const randomIndex = () => cached('randomIndex', () => {
    const R = (window.GAME_SOURCES || {}).randomOutputs || {}, made = new Map(), from = new Map(), gives = new Map();
    const add = (m, k, v) => { if (!m.has(k)) m.set(k, []); m.get(k).push(v); };
    for (const t of R.t || []) {
      for (const row of t.r || []) add(made, row[0], { t, row });
      for (const [src, rows, uses] of t.g || []) { add(from, src, { t, rows, uses }); for (const row of rows) { add(gives, row[0], { t, from: src, row, uses }); add(made, row[0], { t, row, from: src }); } }
    }
    return { tables: R.t || [], rules: R.rules || [], made, from, gives };
  });
  const rowQty = row => (row[2] && row[2] !== '1' ? ' ×' + row[2] : '');
  // A list's "how" text: one sentence, a note; several, a list of them, one a line.
  const howBlock = how => { const v = sentenceLines(how); return typeof v === 'string' ? { kind: 'note', text: how } : { kind: 'list', title: 'Good to know', items: v.lines }; };
  // An item's page: where the random lists can give it, and what using a skill on it gives.
  function randomItemBlocks(id) {
    const R = randomIndex(), k = String(id), out = [];
    const flat = (R.made.get(k) || []).filter(h => !h.from);
    for (const t of [...new Set(flat.map(h => h.t))]) {
      const rows = flat.filter(h => h.t === t);
      out.push({ kind: 'table', title: t.title, columns: ['Skill', ...t.c, ...(rows.some(h => h.row[3]) ? ['Note'] : [])],
        rows: rows.map(h => [skillLink(t.s, skillName(t.s)), ...h.row[1].map(v => v || '—'), ...(rows.some(x => x.row[3]) ? [h.row[3] || '—'] : [])]) });
      if (t.how) out.push(howBlock(t.how));
    }
    const got = R.gives.get(k) || [];
    for (const t of [...new Set(got.map(h => h.t))]) {
      const rows = got.filter(h => h.t === t);
      out.push({ kind: 'table', title: `From ${t.title}`, columns: ['Using it on', ...t.c],
        rows: rows.map(h => [h.uses > 1 ? { lines: [[itemRef(h.from), ` (${h.uses} at a time)`]], sep: '' } : itemRef(h.from), ...h.row[1].map(v => v || '—')]) });
    }
    return out;
  }
  // An item's page: what using a skill on it gives (Fragmentation), a section per skill.
  function randomUseSections(id) {
    return (randomIndex().from.get(String(id)) || []).map(({ t, rows, uses }) => ({ heading: t.title, blocks: [
      ...(uses > 1 ? [{ kind: 'text', text: `It takes ${uses} at a time.` }] : []),
      { kind: 'table', columns: ['You get', ...t.c], rows: rows.map(row => [{ lines: [[itemRef(row[0]), rowQty(row)]], sep: '' }, ...row[1].map(v => v || '—')]) },
      ...(t.how ? [howBlock(t.how)] : [])] }));
  }
  // A skill's page: the random lists it draws from, each folded, and its rules.
  function randomSkillSection(id) {
    const R = randomIndex(), k = String(id), tabs = R.tables.filter(t => String(t.s) === k), rules = R.rules.filter(r => String(r[0]) === k);
    if (!tabs.length && !rules.length) return null;
    const blocks = [];
    if (rules.length) blocks.push({ kind: 'list', items: rules.map(r => r[1]) });
    for (const t of tabs) {
      const inner = [];
      if (t.how) inner.push(howBlock(t.how));
      if (t.g) inner.push({ kind: 'table', columns: ['Using it on', 'You get'], rows: t.g.map(([src, rows, uses]) => [uses > 1 ? { lines: [[itemRef(src), ` (${uses} at a time)`]], sep: '' } : itemRef(src),
        { lines: rows.map(row => [itemRef(row[0]), rowQty(row), row[1].filter(Boolean).length ? ` (${row[1].filter(Boolean).join(', ')})` : '']), sep: '' }]) });
      else inner.push({ kind: 'table', columns: ['Item', ...t.c], rows: t.r.map(row => [{ lines: [[itemRef(row[0]), rowQty(row)]], sep: '' }, ...row[1].map(v => v || '—')]) });
      blocks.push({ kind: 'details', summary: `${t.title} (${(t.g || t.r).length.toLocaleString('en-US')})`, blocks: inner });
    }
    return { heading: 'What it can give', blocks };
  }
  // The Craftables list opens each item's own page (its recipes are under "How it's made" there); an
  // old #/craft/<id> link shows the item's page too.
  const craftPage = id => itemPage(id);

  const artisanOf = u => { const m = /luckyupgrade\s*\(\s*(\d+)/i.exec(u.r.effect || ''); return m ? { set: m[1], s: artisanSet(m[1]) } : null; };
  // One upgrade's effects as cells: text, or a link (an enchant, a skill, an item).
  function upgradeFx(text) {
    const out = [];
    for (const m of String(text || '').matchAll(/(\w+)\s*\(((?:[^()]|\([^()]*\))*)\)/g)) {
      const f = m[1].toLowerCase(), a = m[2].split(',').map(s => s.trim()), key = (a[0] || '').toLowerCase();
      if (f === 'modify' && key === 'chain_casting') {
        // modify(chain_casting, <skill>, <n>): "allow an additional 1 Lightning Bolt to be charged".
        const s = skillById(a[1]), label = `Chain casting: ${s ? s.name : 'a certain skill'} ${amount(a[2] || '')}`;
        out.push(s ? skillLink(a[1], label) : label);
      } else if (f === 'modify') {
        out.push(`${UPG_STATS[key] || cap(words(key).toLowerCase())} ${amount(a.slice(1).join(','), UPG_PCT[key])}`);
      } else if (f === 'use_optionset') {
        const e = enchantById(a[0]);
        out.push(e ? { t: e.cat === 'artisan' ? 'upgrade' : 'enchant', id: e.id, label: 'Enchant: ' + e.name } : 'Adds a certain enchant');
      } else if (f === 'set' && key === 'collecting_bonus_product') {
        // "chopping sometimes yields Average Firewood as well" (with collecting_bonus).
        const it = items().get(a[1]);
        out.push(it ? itemLink(a[1], 'Gathering bonus product: ' + it.name) : 'Gathering bonus product');
      } else if (f === 'luckyupgrade') {
        const s = artisanSet(a[0]), lo = s && Math.min(...s.counts.map(c => c.count)), hi = s && Math.max(...s.counts.map(c => c.count));
        out.push(s ? `Artisan upgrade: ${lo === hi ? lo : lo + '–' + hi} random bonuses` : "Artisan upgrade (what it can give isn't known)");
      } else if (f === 'personalize') {
        out.push('Binds the item to its owner');
      } else {
        out.push(`${titleCase(words(m[1]))}${a.join('') ? ' (' + a.join(', ') + ')' : ''}`);
      }
    }
    return out;
  }
  const cellText = c => (c && typeof c === 'object' ? c.label : String(c));
  const stepRange = (a, b) => (a === undefined && b === undefined ? '' : num(a) === num(b) ? String(num(a)) : `${num(a)}–${num(b)}`);

  let UPGRADES = null, UPG_BY_ID = null;
  function upgrades() {
    if (UPGRADES) return UPGRADES;
    const by = new Map();
    for (const r of rows('db/itemupgradedb.xml', 'upgrade')) {
      // Test rows (test_upgrade_00 at the "devcat" NPC, testweaponupg) aren't shown.
      if (!r.id || /^test/i.test(r.name || '')) continue;
      const old = by.get(r.id);
      if (old && (truthy(old.rebalanced) || !truthy(r.rebalanced))) continue; // the rebalanced row wins (above)
      by.set(r.id, r);
    }
    UPGRADES = [];
    UPG_BY_ID = new Map();
    for (const r of by.values()) {
      const local = tr(r.localname);
      // Some rows' text is only in Korean: the internal name in words, the text under it.
      const own = english(local) ? local : '';
      const u = { id: r.id, r, name: own || titleCase(words(r.name || '')) || 'Upgrade ' + r.id, local: own ? '' : local,
        fx: upgradeFx(r.effect), npcs: [...new Set(String(r.available_npc || '').split(';').map(s => s.trim().toLowerCase()).filter(Boolean))],
        gems: String(r.need_gem || '').split(';').map(g => g.split(',').map(s => s.trim())).filter(g => /^\d+$/.test(g[0])).map(g => [g[0], num(g[1])]) };
      u.steps = u.gems.length ? stepRange(r.gem_upgraded_min, r.gem_upgraded_max) : stepRange(r.upgraded_min, r.upgraded_max);
      UPGRADES.push(u);
      UPG_BY_ID.set(u.id, u);
    }
    return UPGRADES;
  }
  const upgradeById = id => (upgrades(), UPG_BY_ID.get(String(id)));
  const UPG_TEST = new Map();
  const upgradeTest = f => { if (!UPG_TEST.has(f)) UPG_TEST.set(f, matcher(f)); return UPG_TEST.get(f); };
  // The items an upgrade's item_filter matches (cached by filter: many upgrades share one).
  const UPG_TARGETS = new Map();
  function upgradeTargets(u) {
    const f = u.r.item_filter || '';
    if (UPG_TARGETS.has(f)) return UPG_TARGETS.get(f);
    const test = upgradeTest(f), ids = [];
    if (test) for (const [c, list] of catGroups()) if (test(c)) ids.push(...list);
    UPG_TARGETS.set(f, ids);
    return ids;
  }
  // An upgrade's list category: the category most of its items are in.
  function upgradeCat(u) {
    if (u.cat !== undefined) return u.cat;
    const counts = {};
    for (const id of upgradeTargets(u)) { const c = items().get(id).cat; counts[c] = (counts[c] || 0) + 1; }
    const best = Object.entries(counts).sort((a, b) => b[1] - a[1])[0];
    return (u.cat = best ? best[0] : 'none');
  }
  const upgradesFor = cat => (cat ? upgrades().filter(u => { const t = upgradeTest(u.r.item_filter || ''); return t && t(cat); }) : []);
  const npcCells = u => u.npcs.map(npcRef);
  // need_gem's numbers are sizes: the game writes the list as "<gem> <n>cm, ..." (StandardD's
  // "%s %.1fcm, ") followed by "or bigger" (pli_upgradeboxitem.20). The upgrade takes EVERY gem listed,
  // one bag item per entry: SItemUpgradeDesc::IsAvailableGem (StandardD 0x10296822) walks need_gem (a
  // multimap size -> gem id, built by BuildDesc 0x10296a90), takes for each entry the smallest bag item
  // of that id at size - 0.005 or more, and passes only when every entry found one; the upgrade
  // (GameSystemD 0x67dfef40) then deletes each of them (unicorn canary: 3,008 cases, 0 mismatches;
  // docs/mabipro_wiki.md "Gem upgrades take every gem listed"). Gems (and fish) are unstackable and each
  // one has its own size (base_size: the average); stackables (Cat's Eye, Bamboo Pearl, Magic Diamond)
  // and parts such as the Bronze Blade show as ×1 (one is taken; every such entry's number is 1).
  const gemSized = gid => {
    const g = items().get(gid);
    return !!g && num(tagAttrs(g.r.XML, 'xml').base_size) > 0 && num(g.r.Bundle_Max) <= 1 && /^\/jewel\/|\/fish\//.test(g.r.Category || '');
  };
  const gemName = gid => { const g = items().get(gid); return g ? g.name : 'Item ' + gid; };
  const gemRef = gid => (items().get(gid) ? itemLink(gid, gemName(gid)) : gemName(gid));
  // Every gem with its quantity (one of each), so no row is left without one: "4cm ×1", or "×1".
  const gemAmount = (gid, n) => (gemSized(gid) ? `${fmtN(n)}cm ×1` : '×1');
  // "Topaz 4cm ×1 / Ruby 2cm ×1 / Diamond 1cm ×1", one per line, the gems linked (all of them are needed).
  const gemCell = u => ({ lines: u.gems.map(([gid, n]) => [gemRef(gid), ' ' + gemAmount(gid, n)]), sep: '' });
  const GEM_SIZE_NOTE = 'At the size shown or bigger.';
  // The builder's materials ([[name, count]], added up across the upgrades picked): each gem its own
  // material, one per upgrade ("Topaz 4cm", "Ruby 2cm"); an unsized item by name.
  const builderGems = u => u.gems.map(([gid, n]) => [gemName(gid) + (gemSized(gid) ? ` ${fmtN(n)}cm` : ''), 1]);
  const UPG_STEPS_NOTE = 'Steps: how many upgrades the item may already have for this one to be offered (0: none yet).';

  function upgradePage(id) {
    const u = upgradeById(id);
    if (!u) return null;
    const r = u.r, cat = upgradeCat(u), info = [];
    info.push(['Upgrades', cat === 'none' ? 'No matching items' : ITEM_CAT_TITLE[cat]]);
    info.push(['Proficiency', num(r.need_ep)]);
    info.push(['Gold', gold(num(r.need_gold))]);
    if (u.steps) info.push([u.gems.length ? 'Gem upgrade steps' : 'Upgrade steps', u.steps]);
    if (u.npcs.length) info.push(['NPCs', npcCells(u)]);
    if (r.hidden_id !== undefined) info.push(['Hidden upgrade', 'Yes']);
    const sections = [];
    if (u.fx.length) sections.push({ heading: 'Effects', blocks: [{ kind: 'list', items: u.fx.map(toneFx) }] });
    const art = artisanOf(u);
    if (art && art.s) {
      sections.push({ heading: 'Possible results', blocks: [
        { kind: 'table', title: 'How many', columns: ['Results', 'Chance'], rows: art.s.counts.map(c => [String(c.count), pctText(c.pct)]) },
        { kind: 'table', title: 'Which', columns: ['Result', 'Effect', 'Chance to appear'], rows: art.s.opts.map(o => [enchantLink(o.e), toneFx(o.fx), pctText(o.get)]) },
        { kind: 'note', text: 'Chance to appear: how likely that result is to be among what the upgrade gives. It gives that many different results (never the same one twice); each value is random within its range.' },
      ] });
    } else if (art) {
      sections.push({ heading: 'Possible results', blocks: [{ kind: 'note', text: "What this artisan upgrade can give isn't known." }] });
    }
    if (u.gems.length) {
      sections.push({ heading: 'Gems needed', blocks: [{ kind: 'table', columns: ['Gem', 'Size', 'Count'],
        rows: u.gems.map(([gid, n]) => (gemSized(gid) ? [gemRef(gid), fmtN(n) + 'cm', 1] : [gemRef(gid), '—', 1])) }]
        .concat(u.gems.some(([gid]) => gemSized(gid)) ? [{ kind: 'note', text: GEM_SIZE_NOTE }] : []) });
    }
    const b = [];
    const pats = patterns(r.item_filter);
    if (pats.length) b.push({ kind: 'list', title: 'Items whose category matches', items: pats });
    if (b.length) sections.push({ heading: 'Applies to', blocks: b });
    if (u.steps) sections.push({ heading: 'Steps', blocks: [{ kind: 'note', text: UPG_STEPS_NOTE }] });
    return { title: u.name, subtitle: u.local || null, icon: null, category: [cat], infobox: info, summary: tr(r.desc).trim(), sections };
  }
  // The upgrade builder's numbers (app.js "builder" block): an upgrade's modify(<stat>, <n>) by the
  // builder's stat keys. Other effects (an enchant, binding, chain casting) are listed as notes.
  const BUILD_KEY = { attack_min: 'dmgMin', attack_max: 'dmgMax', critical: 'crit', balance: 'bal', wound_min: 'wMin', wound_max: 'wMax',
    durability_max: 'dur', defense: 'def', protect: 'prot', attack_range: 'range', magic_damage: 'magic', casting_speed: 'cast',
    manause_revised: 'manause', manaburn_revised: 'manaburn', immune_melee: 'imMelee', immune_ranged: 'imRanged', immune_magic: 'imMagic',
    splash_radius: 'splashR', splash_damage: 'splashD', collecting_speed: 'gSpeed', collecting_bonus: 'gBonus' };
  // [key, label, unit]: the order the builder shows them in.
  const BUILD_STATS = [['dmgMin', 'Min Damage'], ['dmgMax', 'Max Damage'], ['crit', 'Critical', '%'], ['bal', 'Balance', '%'],
    ['wMin', 'Min Injury Rate', '%'], ['wMax', 'Max Injury Rate', '%'], ['dur', 'Max Durability'], ['def', 'Defense'], ['prot', 'Protection'],
    ['range', 'Range'], ['magic', 'Magic Damage'], ['cast', 'Casting speed'], ['manause', 'Mana use reduction'], ['manaburn', 'Mana Burn reduction'],
    ['imMelee', 'Melee auto-defense'], ['imRanged', 'Ranged auto-defense'], ['imMagic', 'Magic auto-defense'], ['splashR', 'Splash radius'],
    ['splashD', 'Splash damage'], ['gSpeed', 'Gathering speed'], ['gBonus', 'Gathering bonus'],
    // Artisan results.
    ['str', 'Strength'], ['dex', 'Dexterity'], ['int', 'Intelligence'], ['will', 'Will'], ['luck', 'Luck'],
    ['hp', 'Max HP'], ['mp', 'Max MP'], ['sp', 'Max Stamina']];
  // One upgrade as the builder offers it: mods ({stat: n}), notes (other effects), its cost (ep,
  // gold, gems: [[name, count]]) and, for an artisan upgrade, artisan ({ counts, opts: [{ id, label,
  // mods: {stat: [low, high]}, pct }] }, or null when what it gives isn't known).
  function buildOption(u) {
    const mods = {}, notes = [];
    for (const m of String(u.r.effect || '').matchAll(/(\w+)\s*\(([^()]*)\)/g)) {
      const a = m[2].split(',').map(s => s.trim()), k = BUILD_KEY[(a[0] || '').toLowerCase()];
      if (m[1].toLowerCase() === 'modify' && k && /^-?\d+(\.\d+)?$/.test(a[1] || '')) mods[k] = (mods[k] || 0) + num(a[1]);
    }
    const art = artisanOf(u);
    for (const c of u.fx) if (!art && (typeof c === 'object' || !/^[A-Z][\w -]* [+-]?\d/.test(String(c)))) notes.push(cellText(c));
    const g = u.gems.length > 0, lo = g ? u.r.gem_upgraded_min : u.r.upgraded_min, hi = g ? u.r.gem_upgraded_max : u.r.upgraded_max;
    const o = { id: u.id, label: u.name, fx: u.fx.map(cellText).join(', '), mods, notes, min: lo === undefined ? 0 : num(lo), max: hi === undefined ? 99 : num(hi),
      ep: num(u.r.need_ep), gold: num(u.r.need_gold), gems: builderGems(u) };
    if (art) {
      o.artisan = art.s && { counts: art.s.counts, opts: art.s.opts.map(x => ({ id: x.e.id, label: x.fx || x.e.name, mods: x.mods, pct: x.get })) };
      if (!art.s) notes.push("what it can give isn't known");
    }
    return o;
  }
  // An item's numbers as the builder counts them (its base stats).
  function builderBase(it) {
    const r = it.r, s = itemStats(it), base = {};
    for (const k of ['dmgMin', 'dmgMax', 'wMin', 'wMax', 'def', 'prot', 'range']) if (s[k] !== undefined) base[k] = num(s[k]);
    if (s.crit !== undefined) base.crit = num(String(s.crit).replace('%', ''));
    if (s.bal !== undefined) base.bal = num(String(s.bal).replace('%', ''));
    if (num(r.Par_DurabilityMax)) base.dur = Math.round(num(r.Par_DurabilityMax) / 1000);
    return base;
  }
  function upgradeBuilder(it, list) {
    const r = it.r, base = builderBase(it);
    const groups = [];
    const plain = list.filter(u => !u.gems.length), gem = list.filter(u => u.gems.length);
    if (num(r.Par_UpgradeMax) && plain.length) groups.push({ title: 'Upgrades', slots: num(r.Par_UpgradeMax), options: plain.map(buildOption) });
    if (num(r.Par_GemUpgradeMax) && gem.length) groups.push({ title: 'Gem upgrades', slots: num(r.Par_GemUpgradeMax), options: gem.map(buildOption) });
    // Special upgrades: { levels: [{ level, gold, rate }], types: [{ key ('S' / 'R'), label, stone,
    // delta ({stat: n}: the version's base stats less this item's), bonus: [{ key (a builder stat),
    // label, unit, values }] }] }. An S or R weapon has its own type; a weapon that has S and R
    // versions has both (the first stone makes it that version, at level 1). Bonuses with no builder
    // stat of their own get one.
    const E = enhance(), sr = E.bySR.get(String(r.ID)), n = E.byN.get(String(r.ID));
    const type = (key, vid) => {
      const v = items().get(vid), bon = v && enhanceBonuses(v), stone = items().get(STONES[key]);
      if (!bon) return null;
      const vb = builderBase(v), delta = {};
      for (const k of new Set(Object.keys(vb).concat(Object.keys(base)))) if ((vb[k] || 0) !== (base[k] || 0)) delta[k] = (vb[k] || 0) - (base[k] || 0);
      return { key, label: v.name, stone: stone ? stone.name : key + ' Upgrade Stone', delta,
        bonus: bon.map(b => ({ key: SPECIAL_KEY[b.key.toLowerCase()] || 'enh_' + b.key.toLowerCase(), label: b.label, unit: b.unit, values: b.values })) };
    };
    const types = (sr ? [type(sr.type, String(r.ID))] : n ? [type('S', n.S), type('R', n.R)] : []).filter(Boolean);
    let special = null, stats = BUILD_STATS;
    if (types.length && E.levels.length) {
      special = { levels: E.levels.map(l => ({ level: l[0], gold: l[1], rate: l[2] })), types };
      const extra = [];
      for (const t of types) for (const b of t.bonus) if (!BUILD_STATS.some(s => s[0] === b.key) && !extra.some(x => x[0] === b.key)) extra.push([b.key, b.label, b.unit]);
      stats = BUILD_STATS.concat(extra);
    }
    if (!groups.length && !special) return null;
    return { kind: 'builder', title: 'Try upgrades', stats, base, groups, special,
      note: 'Pick upgrades in the order they are done: each one is offered when the item already has the number of upgrades its Steps allow.' };
  }
  // Upgrade paths: every way to fill all of an item's upgrade slots (Par_UpgradeMax). The nth upgrade
  // (n upgrades already done) can be any one whose steps (upgraded_min..max) take n; an upgrade can be
  // picked again. Picks that differ only in order end the same, so each set of upgrades is one path, in
  // the first order that works. Artisan upgrades (random results) are left out unless a step has nothing
  // else. Stats can't go below 0.
  // PATH_CAP: the most paths worked out (the best ones come from all of them); PATH_LIST: the most the
  // "All paths" table lists.
  const PATH_CAP = 300000, PATH_LIST = 25000;
  function upgradePaths(it, list) {
    const slots = num(it.r.Par_UpgradeMax);
    if (!slots) return null;
    const opts = list.filter(u => !u.gems.length).map((u, ix) => Object.assign(buildOption(u), { u, ix, art: !!artisanOf(u) }));
    const fits = (o, k) => k >= o.min && k <= o.max;
    // An item with no plain upgrade for its first slot (only gem upgrades) has no paths; one whose
    // upgrades stop before its last slot (none offered at 4 of 5) fills as many as it can.
    let states = new Map([['', [[], []]]]), stuck = -1;
    for (let k = 0; k < slots; k++) {
      let can = opts.filter(o => fits(o, k) && !o.art && (Object.keys(o.mods).length || o.notes.length));
      if (!can.length) can = opts.filter(o => fits(o, k));
      if (!can.length && !k) return null;
      if (!can.length) { stuck = k; break; }
      // A set of upgrades is its options' indexes, sorted; the path is only built for a new set.
      const next = new Map();
      for (const [ix, seq] of states.values()) for (const o of can) {
        const j = o.ix, nix = ix.slice();
        let p = nix.length;
        while (p > 0 && nix[p - 1] > j) p--;
        nix.splice(p, 0, j);
        const key = nix.join(',');
        if (!next.has(key)) next.set(key, [nix, seq.concat(o)]);
        if (next.size > PATH_CAP) return { slots, paths: [], tooMany: true };
      }
      states = next;
    }
    const base = builderBase(it);
    const paths = [...states.values()].map(([, seq]) => {
      const st = Object.assign({}, base);
      for (const o of seq) for (const [k, v] of Object.entries(o.mods)) st[k] = (st[k] || 0) + v;
      for (const k of Object.keys(st)) st[k] = Math.max(0, st[k]);
      return { seq, st, ep: seq.reduce((t, o) => t + o.ep, 0), gold: seq.reduce((t, o) => t + o.gold, 0) };
    });
    return { slots, filled: stuck < 0 ? slots : stuck, paths, base };
  }
  // The stats a path table shows: the item's own ones that some path changes, in the builder's order.
  const PATH_STATS = [['dmg', 'Damage'], ['crit', 'Critical', '%'], ['bal', 'Balance', '%'], ['wound', 'Injury Rate', '%'], ['dur', 'Durability'], ['def', 'Defense'], ['prot', 'Protection'],
    ['range', 'Range'], ['magic', 'Magic Damage'], ['cast', 'Casting speed'], ['manause', 'Mana use reduction'], ['manaburn', 'Mana Burn reduction'],
    ['imMelee', 'Melee auto-defense'], ['imRanged', 'Ranged auto-defense'], ['imMagic', 'Magic auto-defense'], ['splashR', 'Splash radius'], ['splashD', 'Splash damage'],
    ['gSpeed', 'Gathering speed'], ['gBonus', 'Gathering bonus']];
  const pathVal = (st, k) => (k === 'dmg' ? [st.dmgMin || 0, st.dmgMax || 0] : k === 'wound' ? [st.wMin || 0, st.wMax || 0] : st[k] || 0);
  // (Rounded: added-up fractions leave float dust, "0.6000000000000001".)
  const pathText = (st, k, unit = '') => { const v = pathVal(st, k); return Array.isArray(v) ? `${round2(v[0])}~${round2(v[1])}${unit}` : round2(v) + unit; };
  function upgradePathsSection(it, list) {
    const P = upgradePaths(it, list);
    if (!P) return null;
    if (!P.paths.length) {
      return { heading: 'Upgrade paths', blocks: [{ kind: 'note', text: `There are over ${PATH_CAP.toLocaleString('en-US')} ways to fill its ${P.slots} upgrades; the upgrade builder in Upgrades tries any of them.` }] };
    }
    const shown = PATH_STATS.filter(([k]) => P.paths.some(p => JSON.stringify(pathVal(p.st, k)) !== JSON.stringify(pathVal(P.base, k))) || (k === 'dmg' && P.base.dmgMax !== undefined));
    // The final cell: the item's main numbers always, others only where this path changes them, one
    // stat a line, then the proficiency and gold.
    const MAIN = new Set(['dmg', 'crit', 'bal', 'dur', 'def', 'prot']);
    const cells = new Map();
    const stepCell = o => cells.get(o) || cells.set(o, { lines: [[{ t: 'upgrade', id: o.id, label: o.label }], [`${o.ep} proficiency`], [gold(o.gold)]], sep: '' }).get(o);
    const finalCell = p => ({ lines: [...shown.filter(([k]) => MAIN.has(k) || JSON.stringify(pathVal(p.st, k)) !== JSON.stringify(pathVal(P.base, k)))
      .map(([k, l, u]) => [`${l} ${pathText(p.st, k, u)}`]), [{ text: `${p.ep} proficiency`, tone: 'gold' }], [{ text: gold(p.gold), tone: 'gold' }]], sep: '' });
    const steps = Array.from({ length: P.filled }, (_, i) => `Upgrade ${i + 1}`);
    // The best path for each goal: the stat it's for first, then damage, then the cheaper one. Paths
    // that win several goals are one row named for each. Min damage counts up to the max (a path with
    // more min than max isn't the best for it).
    const minDmg = p => Math.min(p.st.dmgMin || 0, p.st.dmgMax || 0);
    const dmg = p => minDmg(p) + (p.st.dmgMax || 0);
    const GOALS = [['Highest damage', p => dmg(p)], ['Highest max damage', p => p.st.dmgMax || 0], ['Highest min damage', minDmg],
      ['Highest critical', p => p.st.crit || 0], ['Highest balance', p => p.st.bal || 0], ['Most durability', p => p.st.dur || 0],
      ['Highest defense', p => p.st.def || 0], ['Highest protection', p => p.st.prot || 0], ['Highest injury rate', p => p.st.wMax || 0],
      ['Highest magic damage', p => p.st.magic || 0], ['Fastest casting', p => p.st.cast || 0], ['Cheapest', p => -p.gold]];
    const best = new Map();
    // The goals some path changes (Cheapest always).
    const goals = GOALS.filter(([name, f]) => { if (name === 'Cheapest') return true; let lo = Infinity, hi = -Infinity; for (const p of P.paths) { const v = f(p); if (v < lo) lo = v; if (v > hi) hi = v; } return hi > lo; });
    // Each path's score for each goal and its damage, worked out once (the sorts below compare them a lot).
    for (const p of P.paths) { p.g = goals.map(g => g[1](p)); p.dm = dmg(p); }
    const order = gi => (a, b) => b.g[gi] - a.g[gi] || b.dm - a.dm || a.gold - b.gold;
    for (const [gi, [name]] of goals.entries()) {
      const by = order(gi);
      let top = P.paths[0];
      for (const p of P.paths) if (by(p, top) < 0) top = p;
      if (!best.has(top)) best.set(top, []);
      best.get(top).push(name);
    }
    // Both tables fold: the best paths (open at first) and every path (shut).
    // On a phone: the goal and the final stats first, the upgrades behind a button (phone).
    const nSteps = steps.length, stepIdx = (from) => Array.from({ length: nSteps }, (_, i) => from + i);
    const blocks = [{ kind: 'details', summary: `Best paths (${best.size})`, open: true, blocks: [{ kind: 'table', columns: ['For', ...steps, 'Final stats · Proficiency / Gold'],
      phone: { lead: [0, nSteps + 1], fold: stepIdx(1), label: 'upgrades' },
      rows: [...best].map(([p, names]) => [{ lines: names.map(n => [n]), sep: '' }, ...p.seq.map(stepCell), finalCell(p)]) }] }];
    // Every path, laid out like the best ones, under a tab per goal that orders them by it (the same
    // rows, built once).
    const all = P.paths;
    if (all.length <= PATH_LIST) {
      const row = new Map(all.map(p => [p, [...p.seq.map(stepCell), finalCell(p)]]));
      const tabs = goals.map(([name], gi) => ({ label: name.replace(/^Highest /, '').replace(/^Most /, '').replace(/^./, c => c.toUpperCase()), blocks: [{ kind: 'table', columns: [...steps, 'Final stats · Proficiency / Gold'],
        phone: { lead: [nSteps], fold: stepIdx(0), label: 'upgrades' },
        rows: all.slice().sort(order(gi)).map(p => row.get(p)) }] }));
      blocks.push({ kind: 'details', summary: `All ${all.length.toLocaleString('en-US')} path${all.length === 1 ? '' : 's'}`, blocks: [{ kind: 'tabs', style: 'line', label: 'Sort by', tabs }] });
    } else blocks.push({ kind: 'note', text: `There are ${all.length.toLocaleString('en-US')} paths, too many to list; the best for each goal are above, and the upgrade builder tries any of them.` });
    const notes = [P.filled < P.slots ? `The item has ${P.slots} upgrade slots, but no upgrade is offered once it has ${P.filled}, so each path is ${P.filled} upgrades, in an order the game offers them.`
      : 'Each path fills all the item\'s upgrades, in an order the game offers them.', 'Proficiency is what each upgrade needs on the item; gold is its price.'];
    if (list.some(u => !u.gems.length && artisanOf(u))) notes.push('Artisan upgrades give random results, so they aren\'t in these paths.');
    if (all.some(p => p.seq.some(o => o.notes.length))) notes.push('Upgrades with other effects (an enchant, binding) count only their stats here; their pages say the rest.');
    // One note a line.
    blocks.push({ kind: 'list', title: 'Good to know', items: notes });
    return { heading: 'Upgrade paths', wide: true, blocks };
  }
  // An item page's "Upgrades" section: the builder, then every upgrade.
  function itemUpgrades(it) {
    const list = upgradesFor((it.r.Category || '').toLowerCase());
    if (!list.length) return null;
    const gems = list.some(u => u.gems.length);
    const rowsU = list.map(u => [{ t: 'upgrade', id: u.id, label: u.name }, fxLines(u.fx), num(u.r.need_ep), num(u.r.need_gold), u.steps || '—', npcCells(u)]
      .concat(gems ? [u.gems.length ? gemCell(u) : '—'] : []));
    const builder = upgradeBuilder(it, list);
    // The upgrades first, the builder to try them under them.
    return { heading: 'Upgrades', wide: true, blocks: [
      { kind: 'table', columns: ['Upgrade', 'Effect', 'Proficiency', 'Gold', 'Steps', 'NPCs'].concat(gems ? ['Gems'] : []), nowrap: ['Gems'], fit: true, rows: rowsU },
      { kind: 'note', text: UPG_STEPS_NOTE + (gems ? ' Gem upgrades count gem upgrades. Gems: ' + GEM_SIZE_NOTE.toLowerCase() : '') },
    ].concat(builder ? [builder] : []) };
  }

  // ---------- set effects ----------
  // db/setitemdesc.xml (its notes are in Korean; what they say):
  //   <Element name localname threshold/>: an effect that equipment adds points to while it's worn.
  //     threshold "points:condition": the points it needs and the character condition it turns on
  //     (a CharacterCondition.data number). There can be several pairs ("15:36 10:37 -15:38 -10:39"),
  //     and then only the first condition counts. A negative threshold is a penalty (mana_wasting -10).
  //     localname is a text reference or, on later rows, plain text.
  //   <Item id element="name:points ..."> the points an item adds ("1~4": a range), with
  //     <quality threshold bonus="name:points ..."/> children: more points at that quality or higher.
  // Enchants add points too: SetSetItemEffectOnEquip(<name>, <points>) in their effects.
  let SET_EL = null, SET_ITEMS = null;
  function setElements() {
    if (SET_EL) return SET_EL;
    SET_EL = new Map();
    for (const r of rows('db/setitemdesc.xml', 'Element')) {
      const key = (r.name || '').trim().toLowerCase();
      if (!key || SET_EL.has(key)) continue;
      const name = tr(r.localname).trim();
      const th = String(r.threshold || '').trim().split(/\s+/).map(p => p.split(':')).filter(p => /^-?\d+$/.test(p[0])).map(p => ({ pts: num(p[0]), cond: p[1] || '' }));
      SET_EL.set(key, { id: key, r, name: english(name) ? name : SET_FX[key] || titleCase(words(key)), th, penalty: th.length > 0 && th[0].pts < 0 });
    }
    return SET_EL;
  }
  const setElement = key => setElements().get(String(key || '').trim().toLowerCase());
  const setEffectName = key => { const e = setElement(key); return e ? e.name : SET_FX[String(key).toLowerCase()] || titleCase(words(key)); };
  const parsePoints = s => String(s || '').trim().split(/\s+/).map(p => p.split(':')).filter(p => p[0] && p.length > 1).map(p => [p[0].toLowerCase(), p[1]]);
  // {item id: {el: [[key, points]], quality: [{at, bonus: [[key, points]]}]}}, in the file's order.
  function setItems() {
    if (SET_ITEMS) return SET_ITEMS;
    SET_ITEMS = new Map();
    for (const m of stripped('db/setitemdesc.xml').matchAll(/<Item\s([^>]*?)(?:\/>|>([\s\S]*?)<\/Item>)/g)) {
      const a = xmlAttrs(m[1]);
      if (!a.id || SET_ITEMS.has(a.id)) continue;
      const quality = [...String(m[2] || '').matchAll(/<quality\s([^>]*?)\/?>/g)].map(q => xmlAttrs(q[1]))
        .map(q => ({ at: num(q.threshold), bonus: parsePoints(q.bonus) })).filter(q => q.bonus.length);
      SET_ITEMS.set(a.id, { el: parsePoints(a.element), quality });
    }
    return SET_ITEMS;
  }
  const thresholdText = e => (e.th.length ? e.th[0].pts + ' points' : '');
  // db/charactercondition.xml: <CharacterCondition ConditionID ConditionEngName ConditionLocalName
  // Description .../>, text in local/xml/charactercondition.<locale>.txt. A threshold's condition
  // number is a ConditionID there. Its name where the text is English, else the set effect's own
  // name (the enhancement conditions' names are only in Korean); its Description where English.
  let CONDS = null;
  function conditions() {
    if (CONDS) return CONDS;
    CONDS = new Map();
    for (const r of rows('db/charactercondition.xml', 'CharacterCondition')) {
      const id = String(r.ConditionID || '').trim();
      if (id && !CONDS.has(id)) CONDS.set(id, { r, name: tr(r.ConditionLocalName).trim(), desc: tr(r.Description).trim() });
    }
    return CONDS;
  }
  function setCondition(e, t) {
    const c = conditions().get(String(t.cond || '').trim());
    if (!c) return null;
    return { name: c.name && english(c.name) ? c.name : e.name, desc: c.desc && english(c.desc) ? c.desc : '' };
  }
  // Enchants that add to a set effect: [[enchant, points, condition]].
  function setEnchants(key) {
    const out = [];
    for (const e of enchants()) for (const f of effects(e.r.OptionList)) if (f[3] && f[3].toLowerCase() === key) out.push([e, f[1], f[2]]);
    return out;
  }
  const setItemIds = key => [...setItems()].filter(([, s]) => s.el.some(p => p[0] === key) || s.quality.some(q => q.bonus.some(p => p[0] === key))).map(x => x[0]);

  // GAME_SOURCES.setEffects = {effect key: [[points, what (with its numbers; text or [main, details...]), inferred]]}: what each set
  // effect does at each threshold (its _rules, how points add up, aren't shown).
  const setEffectFx = key => ((window.GAME_SOURCES || {}).setEffects || {})[String(key).toLowerCase()] || [];
  // A text of several sentences (also split at "; ") one a line, for a table cell or a kv value; one
  // sentence stays as it is.
  const sentenceLines = s => {
    const parts = String(s || '').split(/(?<=\.)\s+(?=[A-Z(])|(?<=;)\s+/).map(x => x.replace(/;$/, '.').trim()).filter(Boolean).map(x => x[0].toUpperCase() + x.slice(1));
    return parts.length > 1 ? { lines: parts, sep: '' } : String(s || '');
  };
  // A set effect's what: [main line, details...] (the details muted under it), or one text.
  const setFxLines = what => (Array.isArray(what)
    ? { lines: [[what[0]], ...what.slice(1).map(d => [{ text: d, tone: 'muted' }])], sep: '' }
    : sentenceLines(what));
  function setEffectPage(id) {
    const e = setElement(id);
    if (!e) return null;
    const info = [['Kind', e.penalty ? 'Penalty' : 'Bonus']];
    if (e.th.length) info.push(['Activates at', thresholdText(e)]);
    const first = e.th.length ? setCondition(e, e.th[0]) : null;
    if (e.th.length === 1 && first) info.push(['Condition', first.name]);
    const sections = [];
    // What it does in numbers (setEffectFx, traced in the server code), each threshold its own row.
    // Without the numbers, the game's own description alone.
    const fx = setEffectFx(e.id);
    if (fx.length) {
      // One threshold: just what it does (the info box says when it turns on); several: each under its points.
      const blocks = fx.length === 1 ? [{ kind: 'text', value: setFxLines(fx[0][1]) }]
        : [{ kind: 'kv', stack: true, rows: fx.map(([pts, what]) => [`At ${pts} points${e.penalty ? ' or fewer' : ''}`, setFxLines(what)]) }];
      if (fx.length > 1) blocks.push({ kind: 'note', text: 'With several thresholds, the first one in the list that the total reaches is the one that applies.' });
      sections.push({ heading: 'What it does', blocks });
    } else if (first && first.desc) sections.push({ heading: 'What it does', blocks: [{ kind: 'text', text: first.desc }] });
    const ids = setItemIds(e.id).filter(i => items().has(i));
    if (ids.length) {
      const rowsI = ids.map(i => {
        const s = setItems().get(i), own = s.el.filter(p => p[0] === e.id).map(p => amount(p[1]));
        const q = s.quality.map(x => x.bonus.filter(p => p[0] === e.id).map(p => `${amount(p[1])} at a quality score of ${x.at}+`)).flat();
        return [itemLink(i, items().get(i).name), own.join(', ') || '—', q.join(', ') || '—'];
      });
      sections.push({ heading: 'Items', blocks: [{ kind: 'table', columns: ['Item', 'Points', 'Quality score bonus'], rows: rowsI }] });
    }
    const ench = setEnchants(e.id);
    if (ench.length) {
      sections.push({ heading: 'Enchants', blocks: [{ kind: 'table', columns: ['Enchant', 'Points', 'Condition'],
        rows: ench.map(([x, p, c]) => [{ t: 'enchant', id: x.id, label: x.name }, p, c || '—']) }] });
    }
    const summary = e.th.length ? `Worn equipment adds points to this ${e.penalty ? 'penalty' : 'effect'}; it turns on at ${e.th[0].pts} points.` : '';
    return { title: e.name, subtitle: null, icon: null, category: [e.penalty ? 'penalty' : 'bonus'], infobox: info, summary, sections };
  }
  // The set effects an item gives, for its list row (the grid's cards show them): {sets: ["Mana Saving
  // +5", ...]}, or {} when it gives none. One at 0 points (its points come only at a higher quality)
  // isn't shown.
  const setsOf = id => {
    const s = setItems().get(String(id)), l = s ? s.el.filter(([, p]) => !/^\s*[+-]?0\s*$/.test(String(p))).map(([k, p]) => `${setEffectName(k)} ${amount(p)}`) : [];
    return l.length ? { sets: l } : {};
  };
  // An item page's "Set effects" section.
  function itemSetEffects(id) {
    const s = setItems().get(String(id));
    if (!s) return null;
    const keys = [];
    for (const [k] of s.el.concat(...s.quality.map(q => q.bonus))) if (!keys.includes(k)) keys.push(k);
    const link = k => (setElement(k) ? { t: 'set_effect', id: k, label: setEffectName(k) } : setEffectName(k));
    // What each one does once it's on, in numbers (its first threshold's).
    const does = k => { const f = setEffectFx(k)[0]; return f ? setFxLines(f[1]) : '—'; };
    const blocks = [{ kind: 'table', columns: ['Effect', 'Points', 'Activates at', 'What it does'],
      rows: keys.map(k => [link(k), toned(setEffectName(k), s.el.filter(p => p[0] === k).map(p => amount(p[1])).join(', ') || '—'), setElement(k) ? thresholdText(setElement(k)) || '—' : '—', does(k)]) }];
    const q = s.quality.map(x => `With a quality score of ${x.at} or higher: ` + x.bonus.map(([k, p]) => `${setEffectName(k)} ${amount(p)}`).join(', '));
    if (q.length) blocks.push({ kind: 'list', title: 'Quality score bonuses', items: q });
    blocks.push({ kind: 'note', text: 'The points of all worn equipment add up; an effect turns on when its total reaches its threshold.' });
    // The other items adding to each effect, one block for effects with the same items (a set's pieces).
    const byList = new Map();
    for (const k of keys) {
      const others = setItemIds(k).filter(i => i !== String(id) && items().has(i));
      if (!others.length) continue;
      const sig = others.join(',');
      if (!byList.has(sig)) byList.set(sig, { names: [], others });
      byList.get(sig).names.push(setEffectName(k));
    }
    for (const { names, others } of byList.values()) {
      const what = names.length > 1 ? names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1] : names[0];
      blocks.push({ kind: 'links', title: `Other items with ${what}`, items: others.map(i => itemLink(i, items().get(i).name)), oneRow: true });
    }
    return { heading: 'Set effects', blocks };
  }

  // ---------- titles ----------
  // Type: 2 part-time job titles, 4 transformation titles (Paladin, Dark Knight, Falcon, Beast),
  // 5 pet titles, 7 secondary titles (worn next to a main title: the Dan titles, "the Courteous");
  // 1, 3 and 6 aren't told apart in the data. Titles the data reserves for monsters are their own
  // group; placeholders ("none"), the format example, the GM and the developer test titles are hidden.
  const TITLE_TYPES = { 2: 'parttime', 4: 'transform', 5: 'pet', 7: 'second' };
  // [key, list title, one title's kind]
  const TITLE_CATS = [['general', 'General', 'General'], ['second', 'Secondary titles', 'Secondary title'], ['parttime', 'Part-time jobs', 'Part-time job'],
    ['transform', 'Transformations', 'Transformation'], ['pet', 'Pet titles', 'Pet title'], ['monster', 'Monster titles', 'Monster title']];
  const titleHidden = (r, name, desc) => !name || /^(none|gm|\(test title\))$/i.test(name) || /Game Master|데브캣|게임 내에서 사용되지 않는다/.test(desc);
  let TITLES = null, TITLE_BY_ID = null;
  function titles() {
    if (TITLES) return TITLES;
    TITLES = [];
    TITLE_BY_ID = new Map();
    for (const r of rows('db/title.xml', 'Title')) {
      if (!r.ID) continue;
      const names = [tr(r.DefaultName), tr(r.UniName), tr(r.MaleName)];
      const name = (names.find(english) || names.find(Boolean) || '').replace(/…/g, '[name]');
      const desc = tr(r.Desc), hint = tr(r.Hint);
      const fx = effects(r.Effect);
      const monster = /reserved for (event )?monsters/i.test(desc) || /몬스터 전용/.test(hint);
      const t = { id: r.ID, r, name: name || 'Title ' + r.ID, fx, cat: monster ? 'monster' : TITLE_TYPES[r.Type] || 'general' };
      TITLE_BY_ID.set(r.ID, t);
      if (!titleHidden(r, name, desc)) TITLES.push(t);
    }
    return TITLES;
  }
  const titleById = id => (titles(), TITLE_BY_ID.get(String(id).trim()));
  function titlePage(id) {
    const t = titles().find(x => x.id === String(id));
    if (!t) return null;
    const r = t.r, info = [['Kind', (TITLE_CATS.find(c => c[0] === t.cat) || ['', '', 'General'])[2]]];
    const male = tr(r.MaleName).replace(/…/g, '[name]'), female = tr(r.FemaleName).replace(/…/g, '[name]');
    if (male && female && male !== female) { info.push(['Male', male]); info.push(['Female', female]); }
    if (RACES[r.EnableSupportRace] && r.EnableSupportRace !== '0') info.push(['Race', RACES[r.EnableSupportRace] + ' only']);
    const sections = [];
    const desc = tr(r.Desc), hint = tr(r.Hint);
    if (hint && hint !== desc && (english(hint) || !english(desc))) sections.push({ heading: 'How to get it', blocks: [{ kind: 'text', text: hint }] });
    // The Fire Arrow (88): its text doesn't say; the server gives it for a killing blow that is a critical
    // hit with an arrow lit at a campfire.
    else if (t.id === '88') {
      sections.push({ heading: 'How to get it', blocks: [{ kind: 'text', text: 'Defeat an enemy with a critical hit from an arrow lit at a campfire: shoot with a bow within 500 of a fire made with the Campfire skill or a Campfire Kit, or inside a Camp Kit camp.' },
        { kind: 'links', items: [FIRE_GUIDE] },
        { kind: 'note', text: 'While a human uses this title, Aranwen offers the Arrow Revolver quest.' }] });
    }
    if (t.fx.length) sections.push({ heading: 'Effects', blocks: [{ kind: 'list', items: t.fx.map(f => toneFx(fxText(f))) }] });
    else {
      const effectText = tr(r.EffectDescription);
      if (effectText) sections.push({ heading: 'Effects', blocks: [{ kind: 'text', text: effectText }] });
    }
    // OptionUsage 1: a secondary title worn next to one of the master titles in OptionAllow
    // ("<title id> : <condition>;"); 2: next to any title.
    if (r.OptionUsage === '2') sections.push({ heading: 'Use', blocks: [{ kind: 'text', text: 'Can be used together with another title.' }] });
    const allow = [];
    for (const part of (r.OptionAllow || '').split(/;|\r?\n/)) {
      const m = /^\s*(\d+)\s*:\s*(.*?)\s*$/.exec(part);
      if (!m) continue;
      const mt = titleById(m[1]);
      allow.push([mt ? { t: 'title', id: m[1], label: mt.name } : 'Title ' + m[1], conditionCell(m[2]) || '—']);
    }
    if (allow.length) sections.push({ heading: 'Combines with', blocks: [{ kind: 'table', columns: ['Master title', 'Requires'], rows: allow }] });
    // Enchants with a bonus while this title is in use.
    const re = new RegExp('IsUsingTitle\\(\\s*' + t.id + '\\s*\\)', 'i');
    const ench = enchants().filter(e => re.test(e.r.OptionList || ''));
    if (ench.length) sections.push({ heading: 'Related', blocks: [{ kind: 'links', title: 'Enchants with a bonus for this title', items: ench.map(enchantLink) }] });
    return { title: t.name, subtitle: null, icon: null, category: [t.cat], infobox: info, summary: desc, sections };
  }

  // ---------- skills ----------
  // SkillCategory: the data's groups. Named after the skill window's tabs where the client has
  // one (interface.<locale>.txt: Life, Combat, Magic, Alchemy, Transform, Demigod); the rest by
  // what's in them (100: Hide, Performance, Rock Throwing...; 102: the Theatre Mission skills).
  // [key, list title, SkillCategory]
  const SKILL_CATS = [['life', 'Life', '1'], ['combat', 'Combat', '2'], ['magic', 'Magic', '3'], ['alchemy', 'Alchemy', '4'],
    ['transform', 'Transformations', '5'], ['special', 'Special', '6'], ['other', 'Other', '7'],
    ['action', 'Actions', '100'], ['theatre', 'Theatre Mission', '102'], ['none', 'Uncategorized', '']];
  const SKILL_CAT_BY_NUM = Object.fromEntries(SKILL_CATS.filter(c => c[2]).map(c => [c[2], c[0]]));
  const SKILL_CAT_TITLE = Object.fromEntries(SKILL_CATS.map(c => [c[0], c[1]]));
  // The game puts the Demigod skills (45001-45009, and Nuadha's versions 23047-23051) in one category
  // (6) with the special-use ones (kits, Dye, item seals, passes, hidden item skills): the wiki lists the
  // Demigod skills with the transformations, and the rest as Special.
  const isDemigodSkill = id => (+id >= 45001 && +id <= 45009) || (+id >= 23047 && +id <= 23051);
  const skillCat = (id, r) => (isDemigodSkill(id) ? 'transform' : SKILL_CAT_BY_NUM[(r.SkillCategory || '').trim()] || 'none');
  // One skill of a category, for the page's type label.
  const SKILL_KIND = { life: 'Life skill', combat: 'Combat skill', magic: 'Magic skill', alchemy: 'Alchemy skill', transform: 'Transformation skill',
    action: 'Action', theatre: 'Theatre Mission skill' };
  // Skills the data marks as unused: "(사용안함)" ("not used") in the name, or the example skills
  // (SkillEngName "Example ...", HowToGetDesc "Example Skill").
  const skillUnused = (r, local) => /사용안함/.test(local) || /^example\b/i.test(r.SkillEngName || '') || /^example skill$/i.test(r.HowToGetDesc || '');
  let SKILL_ALL = null, SKILL_LIST = null;
  // Every Skill row by id (the first row of an id wins: a few ids have a second, later row).
  function skillAll() {
    if (SKILL_ALL) return SKILL_ALL;
    SKILL_ALL = new Map();
    for (const r of rows('db/skill/skillinfo.xml', 'Skill')) {
      const id = (r.SkillID || '').trim();
      if (!id || SKILL_ALL.has(id)) continue;
      const local = tr(r.SkillLocalName).replace(/\s*\n\s*/g, ' ');
      const eng = (r.SkillEngName || '').trim();
      const name = english(local) ? local : eng || local;
      SKILL_ALL.set(id, { id, r, name: name || 'Skill ' + id, local: name !== local && local ? local : '',
        cat: skillCat(id, r), hidden: truthy(r.IsHidden), unused: skillUnused(r, local) });
    }
    return SKILL_ALL;
  }
  const skills = () => SKILL_LIST || (SKILL_LIST = [...skillAll().values()].filter(s => !s.unused));
  const skillById = id => { const s = skillAll().get(String(id).trim()); return s && !s.unused ? s : null; };

  // skillleveldescription.xml: <DescName race="base|elf|giant"><SkillLevelDetail SkillLevel=.../>...,
  // one group per skill (its skillinfo DescName) and race; elves and giants use their race's
  // group where there is one. A level can have several rows (also across groups of the same
  // name and race): the one with the highest Version is used (none = 0; the first on a tie).
  // That's the one the skill window shows: Smash's Version 1 rows carry the screenshot's rank 1
  // texts and its Dan step, the older unversioned ones don't.
  let LEVELS_BY = null;
  function skillLevels(desc) {
    if (!LEVELS_BY) {
      LEVELS_BY = new Map();
      const text = stripped('db/skill/skillleveldescription.xml');
      const re = /<(\/?)([\w.-]+)((?:\s+[\w.-]+\s*=\s*"[^"]*")*)\s*\/?>/g, attr = /([\w.-]+)\s*=\s*"([^"]*)"/g;
      let m, grp = null;
      while ((m = re.exec(text))) {
        if (m[1]) continue;
        const a = {};
        let x;
        attr.lastIndex = 0;
        while ((x = attr.exec(m[3]))) a[x[1]] = unesc(x[2]);
        if (m[2] === 'SkillLevelDetail') {
          if (!grp || a.SkillLevel === undefined) continue;
          const lv = num(a.SkillLevel), old = grp.get(lv);
          if (!old || num(a.Version) > num(old.Version)) grp.set(lv, a);
        } else if (!/^Skill_LevelDetail/i.test(m[2])) {
          const key = m[2].toLowerCase(), race = (a.race || 'base').toLowerCase();
          if (!LEVELS_BY.has(key)) LEVELS_BY.set(key, {});
          const g = LEVELS_BY.get(key);
          grp = g[race] || (g[race] = new Map());
        }
      }
    }
    return LEVELS_BY.get(String(desc || '').toLowerCase()) || {};
  }
  // Level -> label: 0 Novice, 1-6 F-A, 7-15 rank 9-1, 16+ Dan 1+ (rankLabel; the rows' own
  // descriptions agree: level 15 "Rank 1", level 16 "Dan 1").
  const levelList = map => [...map.keys()].sort((a, b) => a - b);
  function rankRange(s) {
    const g = skillLevels(s.r.DescName), lv = levelList(g.base || g.elf || g.giant || new Map());
    return lv.length ? (lv.length > 1 ? rankLabel(lv[0]) + ' – ' + rankLabel(lv[lv.length - 1]) : rankLabel(lv[0])) : '';
  }

  // Conditions: 'add(points, times, text[, false]);' per training method, the text with or
  // without quotes (some have one quote, a few &quot; or a stray ")." at the end).
  // The optional last argument false marks the methods the skill window doesn't name until
  // they've been trained: it shows them as "Unknown Training Method" (the client's
  // etc.skill.tooltip.invisible_condition text); the user's screenshot shows such a method, done,
  // with its text. Promotion: '(times, points, text)', the times first ("(1, 100, Pass an
  // advancement test.)" is +100 once in the skill window; "(4,50, ...)", "(6,34, ...)").
  const unquote = s => s.replace(/[\s,;]+$/, '').replace(/^["“”']+|["“”']+$/g, '').trim();
  // Placeholder texts some ranks carry instead of a method.
  const PLACEHOLDER = /^(temp|임시)$/i;
  function trainingMethods(v) {
    const out = [];
    for (let ch of tr(v).split(/add\s*\(/i).slice(1)) {
      ch = ch.trim().replace(/;+$/, '').trim();
      const close = ch.lastIndexOf(')');
      if (close >= 0) ch = ch.slice(0, close);
      const m = /^\s*(-?[\d.]+)\s*,\s*(-?[\d.]+)\s*(?:,([\s\S]*))?$/.exec(ch);
      if (!m) continue;
      let text = (m[3] || '').trim(), shown = true;
      const f = /,\s*(true|false)\s*$/i.exec(text);
      if (f) { shown = f[1].toLowerCase() !== 'false'; text = text.slice(0, f.index); }
      text = unquote(text);
      if (!text || PLACEHOLDER.test(text)) continue;
      out.push({ text, pts: num(m[1]), times: num(m[2]), shown });
    }
    return out;
  }
  function promotionSteps(v) {
    const out = [];
    for (let part of tr(v).split(/;|\n/)) {
      part = part.trim().replace(/^\(/, '');
      const close = part.lastIndexOf(')');
      if (close >= 0) part = part.slice(0, close);
      const m = /^\s*([\d.]+)\s*,\s*([\d.]+)\s*(?:,([\s\S]*))?$/.exec(part);
      if (!m) continue;
      const text = unquote(m[3] || '');
      if (text && !PLACEHOLDER.test(text)) out.push({ text, pts: num(m[2]), times: num(m[1]), shown: true });
    }
    return out;
  }
  // Points as the skill window shows them: "+0.50", "+100.00" (more decimals when there are).
  const points = v => (v < 0 ? '' : '+') + (Math.round(v * 100) === v * 100 ? v.toFixed(2) : String(v));
  const METHOD_COLS = ['Method', 'Points each', 'Times', 'Points in all'];
  const methodRows = list => list.map(x => [x.shown ? x.text : { text: x.text, tag: 'Hidden' }, points(x.pts), x.times, +(x.pts * x.times).toFixed(4)]);

  // A rank's card values: [column, value].
  const BONUS = [['BonusLife', 'HP'], ['BonusMana', 'MP'], ['BonusStamina', 'Stamina'], ['BonusSTR', 'STR'], ['BonusINT', 'INT'],
    ['BonusDEX', 'DEX'], ['BonusWill', 'Will'], ['BonusLuck', 'Luck']];
  const RANK_COLS = ['AP', 'Stamina', 'Mana', 'Prepare time', 'Combat Power', 'Rank-up bonus'];
  function rankCard(x) {
    // The rank-up stats, each change in color.
    const got = BONUS.filter(([k]) => num(x[k]));
    // One stat to a line, no commas, in the card's own (plain) color.
    const bonus = got.length ? { lines: got.map(([k, l]) => [`${l} ${num(x[k]) > 0 ? '+' : ''}${num(x[k])}`]), sep: '' } : '';
    const prep = num(x.PrepareTime); // milliseconds
    return [num(x.AbilityNecessary) || '', num(x.StaminaNecessary) || '', num(x.ManaNecessary) || '',
      prep ? +(prep / 1000).toFixed(2) + ' s' : '', num(x.CombatPower) || '', bonus];
  }
  // A rank's Var value as its label says: "+360", "12%", "1.5 s", "60°"; a Dark Knight's HP and MP
  // roll between the value and twice it each time ("120–240").
  const SKILL_VARS = () => (window.GAME_SOURCES && window.GAME_SOURCES.skillVars) || {};
  function varValue(x, [key, label, unit]) {
    const v = num(x[key]);
    if (!v) return '';
    if (/\(random\)/.test(label)) return `${fmtN(v)}–${fmtN(2 * v)}`;
    if (unit === 'ms') return +(v / 1000).toFixed(2) + ' s';
    if (unit === 's') return fmtN(v) + ' s';
    if (unit === 'deg') return fmtN(v) + '°';
    return fmtN(v) + (unit === '%' ? '%' : '');
  }
  const fmtN = v => (Number.isInteger(v) ? v.toLocaleString('en-US') : String(+v.toFixed(2)));
  // How the transformation skills' stats work, from the server code.
  const TRANSFORM_NOTES = [
    [/^4[01234]\d{3}$/, 'These stats apply only while transformed.'],
    [/^(40011|40012|40013|43021|43022|43023)$/, 'Each time you transform, the defensive skills are rolled: one you have is always picked (weighted by its Activation chance plus the main transformation skill\'s bonus), and each of the others turns on with that chance.'],
    [/^(40012|41022|43022|44022)$/, 'The block chance is the value ÷ 100 × (150 − the knockdown gauge), in percent.'],
    [/^4101[123]$/, 'Each time you transform, the three Chaos skills are put in order by Pick-order weight: the first gives 1.8 times its values, the second 1 time, the third 0.6 times.'],
    [/^41001$/, 'Max HP and Max MP are each rolled between the value and twice it every time you transform. Max Stamina is the pool minus the HP and MP rolled.'],
    [/^4(0001|0002|0003|0004|1001|101[123])$/, 'A Paladin or Dark Knight pet transforms with you and gets the "pet gets" share of these stats.']];
  // description, effect, training methods and Dan advancement.
  // sv: what the rank's Var values do, for the skills that have them (GAME_SOURCES.skillVars: the
  // transformation skills' stats, traced in the server code), shown as more columns.
  // Where a rank's description disagrees with the rank's own numbers (GAME_SOURCES.skillValues[skill].rf =
  // {all | humans | elves | giants: {rank: [what the numbers give, inferred]}}), a note on the rank card
  // under its Effect line.
  function applyRankFixes(rb, fixes) {
    if (!rb || !fixes) return rb;
    rb.labels.forEach((label, i) => {
      const f = fixes[label];
      if (!f) return;
      const sub = rb.blocks[i], at = sub.findIndex(b => b.title === 'Effect');
      // The game's description and the rank's own numbers disagree here. Which one applies in play
      // isn't certain (the game may follow either), so the note says so rather than calling one wrong.
      sub.splice(at + 1, 0, { kind: 'note', text: `This effect doesn't match the rank's own numbers, which give ${f[0]}. In play it may follow either one.` });
    });
    return rb;
  }
  function rankBlock(map, title, sv = []) {
    const lvs = levelList(map), cards = [], per = [];
    for (const lv of lvs) {
      const x = map.get(lv), sub = [];
      cards.push(rankCard(x).concat(sv.map(v => varValue(x, v))));
      // The rank's description first, then its effect (the same text style), then how to train it.
      const desc = tr(x.LevelDescription);
      if (desc) sub.push({ kind: 'text', title: 'Description', text: desc });
      const eff = tr(x.EffectDescription);
      if (eff) sub.push({ kind: 'text', title: 'Effect', text: eff, tones: true });
      const methods = trainingMethods(x.Conditions);
      if (methods.length) sub.push({ kind: 'table', title: 'Training methods', columns: METHOD_COLS, rows: methodRows(methods) });
      const promo = promotionSteps(x.Promotion);
      if (promo.length) sub.push({ kind: 'table', title: 'How to advance Dan', columns: METHOD_COLS, rows: methodRows(promo) });
      if (methods.length || promo.length) {
        sub.push({ kind: 'note', text: 'The rank can advance at 100 training points. Each method counts up to its number of times.' +
          (methods.some(m => !m.shown) ? ' Methods marked Hidden show in the skill window as “Unknown Training Method” until they have been trained.' : '') });
      }
      per.push(sub);
    }
    const cols = RANK_COLS.concat(sv.map(v => v[1]));
    const keep = cols.map((_, j) => j).filter(j => cards.some(r => r[j] !== '' && r[j] !== 0));
    return { kind: 'ranks', title: title || undefined, columns: keep.map(j => cols[j]), rows: cards.map(r => keep.map(j => r[j])),
      labels: lvs.map(rankLabel), blocks: per, table: false, start: 0, ...rankGroups(lvs) };
  }
  // The rank picker's groups ([label, first index, last index]: Novice, Rank F – A, Rank 9 – 1,
  // Dan) and its short button labels (N, F…A, 9…1, and the Dan number).
  function rankGroups(lvs) {
    const groups = [];
    const span = (name, test, fmt) => {
      const idx = lvs.map((lv, i) => (test(lv) ? i : -1)).filter(i => i >= 0);
      if (!idx.length) return;
      const a = idx[0], z = idx[idx.length - 1];
      groups.push([fmt(rankLabel(lvs[a]), rankLabel(lvs[z]), a === z), a, z]);
    };
    span('novice', lv => lv <= 0, () => 'Novice');
    span('letters', lv => lv >= 1 && lv <= 6, (a, z, one) => (one ? 'Rank ' + a : `Rank ${a} – ${z}`));
    span('numbers', lv => lv >= 7 && lv <= 15, (a, z, one) => (one ? 'Rank ' + a : `Rank ${a} – ${z}`));
    span('dan', lv => lv >= 16, () => 'Dan');
    return { groups, short: lvs.map(lv => (lv <= 0 ? 'N' : lv >= 16 ? String(lv - 15) : rankLabel(lv))) };
  }
  const RACE_PLURAL = { base: 'Humans', elf: 'Elves', giant: 'Giants' };
  // Same rank data? (to show a race's block only where it differs)
  const sameRanks = (a, b) => JSON.stringify([a.rows, a.labels, a.blocks]) === JSON.stringify([b.rows, b.labels, b.blocks]);
  // Totals by rank: the AP spent and the rank-up stats gained from the first rank up to each rank.
  // A rank's AbilityNecessary and Bonus* are what reaching that rank costs and gives (Novice: 0 AP;
  // Smash's rank F: 1 AP, Combat Mastery's: 0 AP and STR +1; Smash's rank 1: 30 AP, STR +1, Will +1).
  // Stat columns: the stats the skill ever gives. [] when it gives none and costs no AP.
  function rankTotals(map) {
    if (!map || !map.size) return [];
    const lvs = levelList(map), stats = BONUS.filter(([k]) => lvs.some(lv => num(map.get(lv)[k])));
    if (!stats.length && !lvs.some(lv => num(map.get(lv).AbilityNecessary))) return [];
    const sum = stats.map(() => 0);
    let ap = 0;
    const rowsT = lvs.map(lv => {
      const x = map.get(lv);
      ap += num(x.AbilityNecessary);
      stats.forEach(([k], j) => { sum[j] += num(x[k]); });
      return [rankLabel(lv), ap].concat(sum.map((v, j) => (v ? toned(stats[j][1], (v > 0 ? '+' : '') + v) : '')));
    });
    return [{ kind: 'table', title: 'Totals by rank', columns: ['Rank', 'AP spent'].concat(stats.map(s => s[1])), rows: rowsT },
      { kind: 'note', text: 'Each row adds up the AP and rank-up stats of every rank up to and including that one.' }];
  }

  // Fishing: GAME_SOURCES.fishingGear = {items: {item: [[label, value, unit, note (text or [lines]), inferred]]}, pets: {race:
  // same}, rules: [[label, text | [steps], example]], ranks: [[rank, size bonus, bite %, auto %, auto size limit %,
  // Making Mastery bonus]]} (the auto-fishing and bite rules traced in the server code, checked by running it).
  const FISHING_SKILL = 10023;
  const fishingGearData = () => (window.GAME_SOURCES && window.GAME_SOURCES.fishingGear) || {};
  // A piece of fishing gear's (or a seal pet's) fishing stats: one row each, its note under it.
  // The value keeps its unit ("1-4 points", "40%", "30-40 s"), except a multiplier ("x1.5") and a
  // unit its label already says ("Durability used per catch attempt").
  const FISH_UNITS = { seconds: ' s', minutes: ' min', '%': '%' };
  function fishingStatsSection(rows) {
    if (!rows || !rows.length) return null;
    const kv = rows.map(([label, value, unit, note]) => {
      const word = String(unit || '').replace(/[^\w ]/g, ''), said = !!word && new RegExp('\\b' + word + '\\b', 'i').test(label);
      const v = `${value}${unit && !said && !/^x/.test(String(value)) ? (FISH_UNITS[unit] || ' ' + unit) : ''}`;
      // The note: a sentence or short lines, muted under the value.
      const lines = [[toneFx(String(v))]], notes = (Array.isArray(note) ? note : [note]).filter(Boolean);
      notes.forEach((n, i) => lines.push([{ text: n, tone: 'muted' }]));
      return [label, { lines, sep: '' }];
    });
    return { heading: 'Fishing', blocks: [{ kind: 'kv', rows: kv },
      { kind: 'links', title: 'How fishing works', items: [{ t: 'skill', id: String(FISHING_SKILL), label: skillName(FISHING_SKILL), sec: 'how-fishing-works' }] }] };
  }
  // The Fishing skill's page: how fishing works, step by step, and what each rank gives.
  // F.rules = [[label, text | [steps], example]]; the page lays the known ones out (one cast at a glance,
  // auto fishing step by step, fish size as a formula, the rest to know) and lists any other in order.
  function fishingRulesSection(id) {
    const F = fishingGearData();
    if (String(id) !== String(FISHING_SKILL) || !(F.rules || []).length) return null;
    const rule = label => F.rules.find(r => r[0] === label), used = new Set();
    const take = label => { const r = rule(label); if (r) used.add(label); return r; };
    const ranks = F.ranks || [], lo = ranks[0], hi = ranks[ranks.length - 1];
    const blocks = [];
    // One cast, in order. The waits are the bites' own rules (Wait between bites, Reeling time); the
    // bite and auto chances come from the rank table.
    take('Wait between bites'); take('Reeling time');
    if (lo && hi) {
      blocks.push({ kind: 'text', text: 'Every cast goes the same way:' });
      blocks.push({ kind: 'sum', op: '→', terms: [
        { label: 'Wait', value: '30–40 s', note: '5–25 s with a Bait Feeder' },
        { label: 'Bite', value: `${lo[2]}% to ${hi[2]}%`, note: 'By rank; a seal adds +20. A miss waits again, bait kept' },
        { label: 'Struggle', value: '3–10 s', note: 'Reel in early and it gets away' },
        { label: 'Land it', value: `${lo[3]}% to ${hi[3]}%`, note: 'Auto fishing, by rank (gear adds more). By hand: how you play' },
      ] });
    }
    const auto = take('Auto-fishing success chance');
    if (auto && Array.isArray(auto[1])) {
      blocks.push({ kind: 'list', title: 'Auto fishing\'s chance, step by step', ordered: true, items: auto[1] });
      if (auto[2]) blocks.push({ kind: 'note', text: 'For example: ' + auto[2] });
    }
    const size = take('Fish size');
    if (size) {
      const m = /^([^=.]{2,48}?)\s=\s([\s\S]+)$/.exec(size[1]);
      blocks.push({ kind: 'formulas', items: [{ name: m ? m[1].trim() : 'Fish size', expr: m ? m[2] : size[1] }] });
    }
    // The rest, one line each with what it's about in bold.
    const LEADS = { 'Reeling in yourself': 'Reeling in yourself:', 'What you catch': 'What you catch:', 'Rod wear': 'Rod wear:',
      'Bite chance': 'Bites:', 'Things that don\'t change fishing': 'Makes no difference:' };
    const rest = F.rules.filter(r => !used.has(r[0]) && r[0] !== 'Bite chance' && typeof r[1] === 'string');
    if (rest.length) blocks.push({ kind: 'list', title: 'Good to know', items: rest.map(([label, text]) => ({ lead: LEADS[label] || label + ':', text })) });
    // Any other stepwise rule, as it is.
    for (const [label, text, example] of F.rules.filter(r => !used.has(r[0]) && Array.isArray(r[1]))) {
      blocks.push({ kind: 'list', title: label, ordered: true, items: text });
      if (example) blocks.push({ kind: 'note', text: 'For example: ' + example });
    }
    const fishCols = ['Auto-fishing success', 'Bite chance', 'Fish size bonus', 'Auto size limit', 'Making Mastery bonus'];
    if (ranks.length) blocks.push({ kind: 'details', summary: byRankTitle(fishCols, ranks.length), blocks: [{ kind: 'table',
      columns: ['Rank', ...fishCols],
      rows: ranks.map(([rank, sz, bite, a, limit, mm]) => [rank, a + '%', bite + '%', (sz > 0 ? '+' : '') + sz, limit + '%', mm ? '+' + mm : '—']) }] });
    return { heading: 'How fishing works', blocks };
  }
  // The ways to learn a skill: GAME_SOURCES.skillSources = {skill: [[kind, quest, npc, item, text, cond, rank,
  // gen, temporary]]} (strings indexes; quest rewards, the story's grants, NPCs, books, items, equipment). text:
  // a short how, details on lines after "\n"; cond: conditions split by "; ".
  // Story grants first by generation, then quests, books, NPCs, items, the rest.
  const WAY_KINDS = { quest: 'Quest', story: 'Story', book: 'Book', npc: 'NPC', item: 'Item', equip: 'While equipped', auto: 'Automatic', event: 'Event', other: 'Other' };
  const WAY_ORDER = ['story', 'quest', 'book', 'npc', 'item', 'equip', 'auto', 'event', 'other'];
  // A way's condition with what it names linked: a quest after "finishing" (or any quest named in three
  // words or more), a title in quotes after "title", a skill by its whole name (two words or more).
  let COND_NAMES = null;
  function condNames() {
    if (COND_NAMES) return COND_NAMES;
    const m = new Map();
    for (const q of quests().values()) if (q.name && !/^Quest \d/.test(q.name) && !m.has(q.name)) m.set(q.name, { t: 'quest', id: q.id, label: q.name });
    for (const s of skills()) if (s.name && s.name.includes(' ')) m.set(s.name, skillLink(s.id, s.name));
    return (COND_NAMES = { map: m, names: [...m.keys()].sort((a, b) => b.length - a.length) });
  }
  function condLinks(text, self) {
    const s = String(text || ''), spans = [];
    for (const mt of s.matchAll(/\b[Tt]itle "([^"]+)"/g)) {
      const t = titles().find(x => x.name === mt[1]), at = mt.index + 'title "'.length;
      if (t) spans.push([at, at + mt[1].length, { t: 'title', id: t.id, label: mt[1] }]);
    }
    const { map, names } = condNames();
    for (const n of names) {
      for (let at = s.indexOf(n); at >= 0; at = s.indexOf(n, at + n.length)) {
        const end = at + n.length, ref = map.get(n);
        if (/\w/.test(s[at - 1] || '') || /\w/.test(s[end] || '') || spans.some(([a, b]) => at < b && end > a)) continue;
        if (ref.t === 'quest' && n.split(' ').length < 3 && !/finish(?:ing)?\s*$/i.test(s.slice(0, at))) continue;
        if (ref.t === 'skill' && ref.id === String(self)) continue;  // the page's own skill: no link to itself
        spans.push([at, end, ref]);
      }
    }
    if (!spans.length) return [s];
    spans.sort((a, b) => a[0] - b[0]);
    const parts = [];
    let at = 0;
    for (const [a, b, ref] of spans) { if (a > at) parts.push(s.slice(at, a)); parts.push(ref); at = b; }
    if (at < s.length) parts.push(s.slice(at));
    return parts;
  }
  function skillWays(id) {
    const S = window.GAME_SOURCES || {}, list = (S.skillSources || {})[String(id)];
    if (!list || !list.length) return [];
    const str = v => (v >= 0 ? (S.strings || [])[v] || '' : '');
    const genNum = g => { const m = /(\d+)/.exec(String(g || '')); return m ? +m[1] : 0; };
    const kindOf = w => (w[7] && w[0] === 'quest' ? 'story' : w[0]);
    const rows = list.slice().sort((a, b) => WAY_ORDER.indexOf(kindOf(a)) - WAY_ORDER.indexOf(kindOf(b)) || genNum(a[7]) - genNum(b[7]))
      .map(w => {
        const [k, quest, npc, item, text, cond, rank, gen, temp] = w;
        const from = quest ? sourceRef({ t: 'quest', id: String(quest) }) : npc ? sourceRef({ t: 'npc', id: String(npc) }) : item ? sourceRef({ t: 'item', id: String(item) }) : null;
        const kind = gen ? `Generation ${genNum(gen)} story` : WAY_KINDS[k] || 'Other';
        const fromCell = from && from !== '—' ? { lines: [[from], [kind]], sep: '' } : kind;
        // How: its first line, any further lines muted under it. Condition: one a line (split at "; ").
        const how = str(text).split('\n'), conds = str(cond).split(/\n|;\s+/).filter(Boolean).map(x => cap(x.trim()));
        conds.forEach((x, i) => { conds[i] = x.replace(/\.$/, ''); });  // one a line, so no full stops
        const howCell = how.length > 1 ? { lines: [[how[0]], ...how.slice(1).map(d => [{ text: d, tone: 'muted' }])], sep: '' } : how[0];
        // Each condition one a line, the quests, titles and skills it names linked.
        return [fromCell, howCell, (str(rank) || '—') + (temp ? ' (temporary)' : ''), conds.length ? { lines: conds.map(c => condLinks(c, id)), sep: '' } : '—'];
      });
    // The same quest under two ids (one per region or race) reads the same: once.
    const seen = new Set(), uniq = rows.filter(r => { const k = JSON.stringify(r); return !seen.has(k) && seen.add(k); });
    // What only completes a rank's "learn from people or books" training goes on that rank's card
    // (skillTraining); any the cards can't take stay here in a table of their own (trainRest).
    const isTrain = r => /training/i.test(String(r[2]));
    const learn = uniq.filter(r => !isTrain(r)), train = uniq.filter(isTrain);
    const fold = (title, rows) => ({ kind: 'details', summary: `${title} (${rows.length})`, blocks: [{ kind: 'table', columns: ['From', 'How', 'Rank', 'Condition'], rows }] });
    const out = learn.length ? [fold('Ways to learn it', learn)] : [];
    out.trainRest = ranks => { const rest = train.filter(r => !ranks.has(String(r[2]).replace(/^Rank\s+|\s+training.*$/gi, ''))); return rest.length ? fold('Training from people or books', rest) : null; };
    return out;
  }
  // A skill's "learn better methods through other people or books" training, by rank ({rank label:
  // [{kind: book | quest | npc | other, ref, how, conds}]}): the book to read, the quest to finish or
  // the person to talk to for that rank (skillSources rows whose rank is "Rank E training").
  function skillTraining(id) {
    const S = window.GAME_SOURCES || {}, list = (S.skillSources || {})[String(id)] || [], out = {};
    const str = v => (v >= 0 ? (S.strings || [])[v] || '' : '');
    for (const [k, quest, npc, item, text, cond, rank] of list) {
      const m = /^Rank\s+(.+?)\s+training/i.exec(str(rank));
      if (!m) continue;
      const ref = item ? sourceRef({ t: 'item', id: String(item) }) : quest ? sourceRef({ t: 'quest', id: String(quest) }) : npc ? sourceRef({ t: 'npc', id: String(npc) }) : null;
      const kind = item ? 'book' : quest ? 'quest' : npc ? 'npc' : 'other';
      // The rank card says the rank already: "At rank E" isn't repeated.
      const conds = str(cond).split(/\n|;\s+/).map(x => x.trim()).filter(x => x && !/^at rank\b/i.test(x));
      const e = { kind, ref, how: str(text).split('\n')[0], conds };
      if (!(out[m[1]] || (out[m[1]] = [])).some(x => x.kind === e.kind && JSON.stringify(x.ref) === JSON.stringify(e.ref))) out[m[1]].push(e);
    }
    return out;
  }
  // The rank cards' part of it: the training method "Learn better methods through other people or
  // books." names what that rank needs ("Read Resting Guide"), and a note under the table says it's
  // needed to advance. Returns the ranks it put on a card.
  function applyTraining(rb, train) {
    const done = new Set();
    if (!rb) return done;
    rb.labels.forEach((label, i) => {
      const list = train[label];
      if (!list || !list.length) return;
      const sub = rb.blocks[i], t = sub.findIndex(b => b.kind === 'table' && b.title === 'Training methods');
      const step = e => (e.kind === 'book' ? ['Read ', e.ref] : e.kind === 'quest' ? ['Finish ', e.ref] : [e.how.replace(/\.$/, '')]);
      // The method row says it all (no note repeating it): each step, any condition past the rank muted
      // under it ("At least 3 successful healer part-time jobs").
      let placed = false;
      if (t >= 0) {
        for (const row of sub[t].rows) {
          const text = typeof row[0] === 'string' ? row[0] : (row[0] && row[0].text) || '';
          if (!/other people or books/i.test(text)) continue;
          const hidden = typeof row[0] === 'object' && row[0].tag;
          row[0] = { lines: list.flatMap((e, j) => [step(e).concat(hidden && !j ? [{ text: '', tag: row[0].tag }] : []),
            ...e.conds.map(c => condLinks(cap(c.replace(/\.$/, ''))).map(p => (typeof p === 'string' ? { text: p, tone: 'muted' } : p)))]), sep: '' };
          placed = true;
        }
      }
      if (placed) { done.add(label); return; }
      // No method row to put it in: a note says what's needed, with any condition past the rank.
      const books = list.filter(e => e.kind === 'book'), others = list.filter(e => e.kind !== 'book');
      const lines = [];
      if (books.length) lines.push(books.length === 1 ? ['You need the book ', books[0].ref, ' to advance this rank.']
        : ['You need one of these books to advance this rank: ', ...books.flatMap((e, j) => (j ? [j === books.length - 1 ? ' or ' : ', ', e.ref] : [e.ref])), '.']);
      for (const e of others) lines.push(e.kind === 'quest' ? ['You need to finish ', e.ref, ' to advance this rank.']
        : ['To advance this rank: ', e.how.replace(/\.$/, ''), '.']);
      for (const e of list) for (const c of e.conds) lines.push(condLinks(cap(c.replace(/\.$/, '')) + '.'));
      sub.splice(t >= 0 ? t + 1 : sub.length, 0, { kind: 'note', text: { lines, sep: '' } });
      done.add(label);
    });
    return done;
  }
  // Arrows lit at a campfire (traced in the G13 server, checked by running its code): a skill readied with
  // a bow (weapon type 4) in the right hand while inside a campfire's area (500 around a Camp Fire skill
  // or Campfire Kit fire, or anywhere in a Camp Kit camp; no world fire props) shoots a lit arrow: damage
  // ×1.5 after defense, protection, the critical bonus and the skill's own multiplier; the element P2/F7
  // in place of the shooter's; critical chance ×1.1 (cap 30). The arrow isn't checked: every arrow
  // lights. Decided when the skill finishes readying. Skills that use it, and bow skills that don't:
  const FIRE_SKILLS = { 21001: '', 21002: 'skill', 21003: '', 21004: '', 21006: '', 22011: 'splash', 23030: 'final', 23041: '' };
  const FIRE_IGNORED = { 21007: 1 };
  const FIRE_MAKERS = { 10008: 1, 50007: 1 };
  // [monster's element, damage against it compared with a plain arrow (fire's rate × 1.5)].
  const FIRE_RATES = [['Physical only', 1.5], ['A third lightning', 1.5], ['A third ice', 2], ['Two thirds ice', 2.5], ['All ice', 2.67],
    ['A third fire', 1], ['All fire', 0.33]];
  // A skill page's part: one line on what the fire does for this skill, and the guide for the rest
  // (the rules are the same for every bow skill).
  const FIRE_GUIDE = { t: 'guide', id: 'fire-arrows', label: 'Fire Arrows guide' };
  function fireArrowSection(id) {
    const k = String(id), kind = FIRE_SKILLS[k];
    const text = FIRE_MAKERS[k] ? 'A bow shot readied within 500 of this fire (the same units as a bow\'s range) is lit: ×1.5 damage, a fire element and a little more critical chance.'
      : FIRE_IGNORED[k] ? 'Arrows lit at a campfire do nothing extra for this skill: it keeps its own damage and element.'
      : kind === undefined ? null
      : 'Ready this skill with a bow beside a campfire and its shot is lit: ×1.5 damage' + (kind === 'skill' || kind === 'splash' ? ' on top of the skill\'s own multiplier' : '') + ', a fire element and a little more critical chance.'
        + (kind === 'splash' ? ' The splash around the target most likely burns too.' : kind === 'final' ? ' Its shots use the fire when the bow skill doing the shooting would.' : '');
    return text ? { heading: 'Fire arrows', blocks: [{ kind: 'text', text }, { kind: 'links', items: [FIRE_GUIDE] }] } : null;
  }
  // The Fire Arrows guide: how to light an arrow at a campfire, what it does, against which monsters
  // it helps, and the bow skills that use it.
  function fireArrowGuide(g) {
    const fireTitle = titleById('88');
    const skills = ids => ids.map(id => skillById(id)).filter(Boolean).map(s => ({ t: 'skill', id: s.id, label: s.name }));
    const light = [
      ['How', 'Ready the skill with a bow within 500 of a fire made with the Campfire skill or a Campfire Kit (the same units as a bow\'s range), or anywhere inside a Camp Kit camp. You don\'t need to face the fire.'],
      ['Fires that count', 'Only campfires players make (the Campfire skill, a Campfire Kit) and Camp Kit camps. Braziers, torches and other fires in the world don\'t light arrows.'],
      ['Arrows', 'All arrows.'],
      ['Weapons', 'Bows only. Crossbows (even ones that load arrows) and atlatls never light.'],
      ['When it lights', 'As the skill finishes readying. A shot readied by the fire stays lit if you walk away before shooting. Walking up to a fire after readying doesn\'t light that shot; the next one lights.'],
    ];
    const does = [
      ['Damage', { lines: [[{ text: '×1.5', tone: 'pos' }, ', on top of defense, protection, critical damage and the skill\'s own multiplier']], sep: '' }],
      ['Element', 'Fire (mostly fire, a little physical). It takes the place of your equipment\'s element for that shot: elemental crystals and enchants don\'t count.'],
      ['Critical chance', { lines: [[{ text: '×1.1', tone: 'pos' }, ' (a tenth of your Critical added), still at most 30%']], sep: '' }],
      ['No effect on', 'Burning over time, stun, knockback, hit chance, wounds, arrows used or durability.'],
    ];
    const extra = [];
    if (fireTitle) extra.push({ lines: [['Defeating an enemy with a critical hit from an arrow lit at a campfire earns the title ', { t: 'title', id: fireTitle.id, label: fireTitle.name }, '.']], sep: '' });
    extra.push('Special arrows (Flare, Frostbite, Exorcism, Sniper...) light like any other arrow; how their own effects add to the fire isn\'t known.');
    const on = skills(Object.keys(FIRE_SKILLS)), off = skills(Object.keys(FIRE_IGNORED)), fires = skills(Object.keys(FIRE_MAKERS));
    return { title: g.name, subtitle: null, icon: null, category: [g.cat], kind: 'Guide', noMeta: true, infobox: [], summary: g.sub + '.',
      sections: [
        { heading: 'How to light them', blocks: [{ kind: 'kv', stack: true, rows: light }, ...(fires.length ? [{ kind: 'links', title: 'Fires that light arrows', items: fires }] : [])] },
        { heading: 'What a lit arrow does', blocks: [{ kind: 'kv', stack: true, rows: does }] },
        { heading: 'Against monster elements', blocks: [
          { kind: 'table', lead: true, columns: ['Monster\'s element', 'Damage compared with a plain arrow'],
            rows: FIRE_RATES.map(([n, x]) => [n, { text: `×${x}`, tone: x > 1 ? 'pos' : x < 1 ? 'neg' : undefined }]) },
          { kind: 'note', text: 'Each monster\'s page shows its own figure in its Elements table ("Arrows lit at a campfire"). Against a monster that is a third fire it comes out even; against mostly-fire monsters a plain arrow is better.' }] },
        { heading: 'Skills', blocks: [{ kind: 'links', title: 'Bow skills that shoot lit arrows', items: on },
          ...(off.length ? [{ kind: 'links', title: 'Bow skills the fire does nothing for', items: off }] : [])] },
        { heading: 'Good to know', blocks: [{ kind: 'list', items: extra }] },
      ] };
  }
  // Taming Wild Animals (traced in the game's code and checked by running it): GAME_SOURCES.taming = {skill,
  // text (the formula in words), r: [[rank, bonus, seconds tamed, range]], cp: [[combat power, the creature's
  // value]], canes: [[item, bonus]], baits: [item], dark: [[Control of Darkness rank, adds]], e: [[HP rule,
  // time rule, while tamed]], races: {race: [rule, value or null]}, notes: {bait, duration, untamable, hp_fail}}.
  // One roll of 1-100 against: the rank's bonus + the creature's value (from its combat power) + twice the
  // cane's bonus (the bait's own is never read) + a third of Control of Darkness's rank, kept to 0-100.
  const tamingData = () => (window.GAME_SOURCES || {}).taming || {};
  // A part of the chance, in percent (the parts add up to the chance itself).
  const tameAdds = n => ({ text: (n > 0 ? '+' : '') + n + '%', tone: n > 0 ? 'pos' : n < 0 ? 'neg' : undefined });
  // The canes that exist as items, with their bonus.
  const tamingCanes = () => (tamingData().canes || []).filter(([it]) => items().get(String(it)));
  // Familiar creatures from easy to hard, for the skill page's examples (their own values; every tamable
  // monster's page has its own).
  const TAME_EXAMPLES = ['40101', '50001', '20001', '70001', '40203', '360102'];
  const tameRank = k => ((tamingData().r || []).find(r => r[0] === k) || [k, 0])[1];
  const tameChance = (bonus, value, cane) => Math.max(0, Math.min(100, bonus + value + 2 * cane));
  function tamingSection(id) {
    const T = tamingData();
    if (!T.skill || String(id) !== String(T.skill)) return null;
    const N = T.notes || {}, ranks = T.r || [], range = ranks[0] ? ranks[0][3] : 0;
    const bonuses = tamingCanes().map(c => c[1]).filter(b => b > 0);
    const basic = bonuses.length ? Math.min(...bonuses) : 0, best = bonuses.length ? Math.max(...bonuses) : 0;
    const top = ranks.length ? ranks[ranks.length - 1] : null, r1 = ranks.find(r => r[0] === '1');
    const cps = T.cp || [], easy = cps.length ? cps[0][1] : 0, hard = cps.length ? cps[cps.length - 1][1] : 0;
    const dark = (T.dark || []).map(d => d[1]);
    const blocks = [];
    // The chance as a sum: what each part is, and how far it goes.
    blocks.push({ kind: 'text', text: 'Every try is one roll. Your chance is three parts added up:' });
    blocks.push({ kind: 'sum',
      terms: [
        { label: 'Your rank', value: `0% to +${top ? top[1] : 0}%`, note: `Novice 0 · rank 1 +${r1 ? r1[1] : 0}${top && top[0] !== '1' ? ` · ${top[0]} +${top[1]}` : ''}` },
        { label: 'The creature', value: `+${easy}% to ${hard}%`, note: 'Weak creatures add a lot; strong ones take away' },
        { label: 'Your Taming Cane', value: `+${2 * basic}% to +${2 * best}%`, note: 'Its bonus counts twice' },
      ],
      result: { label: 'Chance to tame', value: '0% to 100%', note: 'Kept within 0 and 100' },
      foot: dark.length ? `Knowing Control of Darkness adds a little more: a third of its rank (+${Math.min(...dark)} to +${Math.max(...dark)}).` : '' });
    // Familiar creatures, so the creature's part means something.
    const ex = TAME_EXAMPLES.map(i => [i, (T.races || {})[i]]).filter(([i, h]) => h && h[1] != null && monsterById(i));
    if (ex.length) {
      const at = ['Novice', '1', top ? top[0] : 'Dan 5'];
      blocks.push({ kind: 'table', title: 'For example', columns: ['Creature', 'Its part', ...at.map(k => (/^\d+$/.test(k) ? 'Rank ' + k : k))],
        rows: ex.map(([i, h]) => [{ t: 'monster', id: i, label: monsterById(i).name }, tameAdds(h[1]), ...at.map(k => tameChance(tameRank(k), h[1], basic) + '%')]) });
      blocks.push({ kind: 'note', text: `With the basic Taming Cane (+${basic}, counting ${2 * basic}). Every tamable monster's page shows its own part and chances.` });
    }
    // What to have and do first.
    blocks.push({ kind: 'list', title: 'Before you try', icon: 'check', items: [
      { lead: 'Hold a Taming Cane and load Taming Bait.', text: 'Each try uses one bait, whether it works or not.' },
      { lead: 'Weaken it first.', text: 'Each creature has an HP limit (on its page); above it, the try fails.' },
      { lead: `Get within ${range}.`, text: 'The same units as a bow\'s range.' },
      { lead: 'Not as a Dark Knight.', text: 'Taming doesn\'t work while you are transformed into one.' },
    ] });
    blocks.push({ kind: 'list', title: 'Good to know', items: [
      { lead: 'The bait doesn\'t change the chance.', text: 'Your cane\'s bonus is what counts, twice.' },
      { lead: 'Nothing else changes it:', text: 'your level, stats, titles and how many times you have tried make no difference.' },
      N.duration && { lead: 'How long it stays:', text: `${seconds(ranks[0] ? ranks[0][2] : 300)}${top && top[2] !== ranks[0][2] ? ` (${seconds(top[2])} at ${top[0]})` : ''}, until it or you die or you change regions. Taming another releases the first.` },
      N.untamable && { lead: 'Some can\'t be tamed:', text: 'bosses, and creatures that are marked as impossible to tame.' },
    ].filter(Boolean) });
    // The details, folded.
    const canes = tamingCanes();
    if (canes.length) blocks.push({ kind: 'details', summary: `Taming Canes (${canes.length})`, blocks: [{ kind: 'table', columns: ['Cane', 'Bonus', 'Adds to the chance'],
      rows: canes.map(([it, b]) => [itemRef(it), b ? '+' + b : '0', tameAdds(2 * b)]) }] });
    if (ranks.length) blocks.push({ kind: 'details', summary: byRankTitle(['Taming chance bonus', 'Time tamed'], ranks.length), blocks: [{ kind: 'table', columns: ['Rank', 'Adds to the chance', 'Stays tamed'],
      rows: ranks.map(([rank, b, s]) => [rank, tameAdds(b), seconds(s)]) }] });
    if (cps.length) blocks.push({ kind: 'details', summary: 'How the creature\'s part is worked out', blocks: [
      { kind: 'text', text: 'It comes from the creature\'s combat power, a hidden strength rating: weak creatures have a low one, bosses a very high one. The stronger the creature, the smaller its part, and past about 1,000 it starts taking away.' },
      { kind: 'table', columns: ['Combat power', 'The creature\'s part'], rows: cps.map(([cp, v]) => [cp.toLocaleString('en-US'), tameAdds(v)]) }] });
    if ((T.dark || []).length) blocks.push({ kind: 'details', summary: `Control of Darkness (${T.dark.length})`, blocks: [{ kind: 'table', columns: ['Control of Darkness rank', 'Adds to the chance'],
      rows: T.dark.map(([rank, add]) => [rank, tameAdds(add)]) }] });
    return { heading: 'How taming works', blocks };
  }
  // A monster's page: whether it can be tamed, its value, and the chance at a few ranks.
  const TAME_RANKS = ['Novice', 'A', '1', 'Dan 5'];
  function monsterTamingSection(id) {
    const T = tamingData(), hit = (T.races || {})[String(id)];
    if (!hit) return null;
    const [rule, value] = hit, [hp, when, buffs] = (T.e || [])[rule] || [];
    const blocks = [];
    if (value == null) blocks.push({ kind: 'text', text: 'It can be tamed; the chance for it isn\'t known.' });
    else {
      // The weakest cane that adds anything, and the best one.
      const bonuses = tamingCanes().map(c => c[1]).filter(b => b > 0);
      const basic = bonuses.length ? Math.min(...bonuses) : 0, best = bonuses.length ? Math.max(...bonuses) : 0;
      const pct = (bonus, cane) => Math.max(0, Math.min(100, bonus + value + 2 * cane)) + '%';
      const ranks = TAME_RANKS.map(k => (T.r || []).find(r => r[0] === k)).filter(Boolean);
      blocks.push({ kind: 'kv', rows: [['Its part in your chance', tameAdds(value)]] });
      blocks.push({ kind: 'table', title: 'Chance to tame', columns: ['Your rank', `Cane +${basic}`, `Cane +${best}`],
        rows: ranks.map(([rank, bonus]) => [rank, pct(bonus, basic), pct(bonus, best)]) });
    }
    // "While it stays tamed, you get Max HP +3, STR +3": the stats one a line.
    const gets = /^While it stays tamed, you get (.*?)\.?$/i.exec(String(buffs || ''));
    const rows = [['Its HP', hp], ['When', when], gets ? ['While tamed, you get', { lines: gets[1].split(/,\s*|\s+and\s+/), sep: '' }] : ['While tamed', buffs]].filter(r => r[1]);
    if (rows.length) blocks.push({ kind: 'kv', rows });
    blocks.push({ kind: 'links', items: [{ t: 'skill', id: String(T.skill), label: 'How taming works', sec: 'how-taming-works' }] });
    return { heading: 'Taming', blocks };
  }
  // A rank table's fold says what it holds: "Splash damage by rank", "Bursts, reach, width and 4 more by
  // rank" (units in brackets left out; later names lower-cased unless they're abbreviations like MP).
  function byRankTitle(cols, n) {
    const names = cols.map(c => String(c).replace(/\s*\([^)]*\)$/, '').trim()).filter(Boolean)
      .map((c, i) => (i && /^[A-Z][a-z]/.test(c) ? c[0].toLowerCase() + c.slice(1) : c));
    const shown = names.length > 3 ? names.slice(0, 3) : names, more = names.length - shown.length;
    const list = more ? `${shown.join(', ')} and ${more} more` : shown.length > 1 ? `${shown.slice(0, -1).join(', ')} and ${shown[shown.length - 1]}` : shown[0] || 'Values';
    return `${list} by rank (${n})`;
  }
  // A skill's rules in numbers, past what its rank tables say: GAME_SOURCES.skillValues = {skill: {t: {c:
  // [column], r: [[rank, [value]]]}, f: [[fact, inferred]], m: [[formula, inferred]], x: [[correction,
  // inferred]]}}. What was worked out rather than seen happen is shown the same as the rest.
  function skillValuesSection(id) {
    const v = ((window.GAME_SOURCES || {}).skillValues || {})[String(id)];
    if (!v) return null;
    const blocks = [];
    // A formula "Success chance = (...) + ..." as a card: what's worked out above, the working below.
    if ((v.m || []).length) blocks.push({ kind: 'formulas', items: v.m.map(([text]) => {
      const m = /^([^=.]{2,48}?)\s=\s([\s\S]+)$/.exec(text);
      return { name: m ? m[1].trim() : '', expr: m ? m[2] : text };
    }) });
    // Values that read as a table (chances by tune difficulty...), the condition leading each row.
    for (const [title, columns, rows] of v.tb || []) blocks.push({ kind: 'table', lead: true, title, columns, rows });
    if ((v.f || []).length) blocks.push({ kind: 'list', title: 'Good to know', items: v.f.map(([text]) => text) });
    // Where the game's own descriptions don't match what the skill does, apart from the rules.
    if ((v.x || []).length) blocks.push({ kind: 'note', tone: 'warn', text: { lines: v.x.map(([text]) => [text]), sep: '' } });
    if (v.t) blocks.push({ kind: 'details', summary: byRankTitle(v.t.c, v.t.r.length), blocks: [{ kind: 'table', columns: ['Rank', ...v.t.c],
      rows: v.t.r.map(([rank, vals]) => [rank, ...vals.map(x => (x === '' || x == null ? '—' : x))]) }] });
    // The one place the wiki says where its numbers come from (the user's choice, 2026-10-22).
    return blocks.length ? { heading: 'Hidden details', tip: 'Derived from the server files: numbers the game doesn\'t show.', blocks } : null;
  }
  function skillPage(id) {
    const s = skillById(id);
    if (!s) return null;
    const r = s.r, info = [['Category', SKILL_CAT_TITLE[s.cat]]];
    const range = rankRange(s);
    if (range) info.push(['Ranks', range]);
    if (s.hidden) info.push(['Skill window', 'Not listed']);
    const mt = r.MasterTitle && titleById(r.MasterTitle);
    if (mt && titles().includes(mt)) info.push(['Master title', { t: 'title', id: mt.id, label: mt.name }]);
    // A skill that makes things: its list under Craftables (recipes and what it can give at random).
    const made = craftCount(s.name);
    if (made) info.push(['Craftables', { t: 'craft', cat: craftCat(s.name), label: `${made.toLocaleString('en-US')} item${made === 1 ? '' : 's'}` }]);
    const sections = [];
    const how = [tr(r.ClosedDesc), clean(r.HowToGetDesc)].filter(Boolean), ways = skillWays(s.id);
    // Above the ranks, each table folded under its name and count. The game's own text says the same as
    // the tables, so it shows only for a skill with none. A rank's book or teacher goes on its rank card
    // (applyTraining, below); any the cards don't take get a table here.
    const train = skillTraining(s.id);
    const howText = ways.length || Object.keys(train).length ? [] : how.map(t => ({ kind: 'text', text: t }));
    const obtain = { heading: 'Obtaining the Skill', blocks: howText.concat(ways) };
    sections.push(obtain);
    // How the skill works (its rules in numbers, and the skills with a whole guide of their own), above
    // the ranks: what it does first, then what each rank gives.
    for (const sec of [skillValuesSection(s.id), tamingSection(s.id), fishingRulesSection(s.id), fireArrowSection(s.id), randomSkillSection(s.id)]) if (sec) sections.push(sec);
    // Ranks: the base group (humans, and any race without its own), and the elves' and giants'
    // own groups where they differ. When they do (20 skills), a Race switch with all three races
    // puts which race the ranks are for up front; a race on the base group says so.
    const g = skillLevels(r.DescName), sv = SKILL_VARS()[String(s.id)] || [];
    const base = g.base && g.base.size ? rankBlock(g.base, undefined, sv) : null, own = {};
    for (const k of ['elf', 'giant']) {
      if (!g[k] || !g[k].size) continue;
      const b = rankBlock(g[k], undefined, sv);
      if (!base || !sameRanks(b, base)) own[k] = b;
    }
    // The rank text fixes: on every race's cards for "all", else on that race's own (a race that shares
    // the humans' ranks gets its own copy for its fix, so the fix doesn't show for the others).
    const rf = (((window.GAME_SOURCES || {}).skillValues || {})[String(s.id)] || {}).rf;
    if (rf && base) {
      const RACE_FIX = { base: 'humans', elf: 'elves', giant: 'giants' };
      for (const k of ['elf', 'giant']) if (rf[RACE_FIX[k]] && !own[k] && g[k] && g[k].size) own[k] = rankBlock(g[k], undefined, sv);
      for (const [k, rb] of [['base', base], ...Object.entries(own)]) applyRankFixes(rb, Object.assign({}, rf.all, rf[RACE_FIX[k]]));
    }
    const onCards = new Set();
    for (const rb of [base, ...Object.values(own)]) for (const l of applyTraining(rb, train)) onCards.add(l);
    const rest = ways.trainRest ? ways.trainRest(onCards) : null;  // none for a skill without sources
    if (rest) obtain.blocks.push(rest);
    if (!obtain.blocks.length) sections.splice(sections.indexOf(obtain), 1);
    if (Object.keys(own).length) {
      const tabs = ['base', 'elf', 'giant'].filter(k => k === 'base' ? base : own[k] || base).map(k => {
        const label = RACE_PLURAL[k], shared = k !== 'base' && !own[k];
        return { label, blocks: (shared ? [{ kind: 'note', text: `${label} use the same ranks as humans.` }] : []).concat(own[k] || base, rankTotals(own[k] ? g[k] : g.base)) };
      });
      sections.push({ heading: 'Ranks', blocks: [{ kind: 'tabs', label: 'Race', remember: 'race', tabs }] });
    } else if (base) {
      sections.push({ heading: 'Ranks', blocks: [{ kind: 'note', text: 'The same for every race that can learn it.' }, base].concat(rankTotals(g.base)) });
    }
    // Enchants and titles whose effects depend on this skill's rank.
    const re = new RegExp('Is(?:Greater|Less)EqualSkillLv\\(\\s*' + s.id + '\\s*,', 'i');
    const ench = enchants().filter(e => re.test(e.r.OptionList || ''));
    const rel = [];
    if (ench.length) rel.push({ kind: 'links', title: 'Enchants with a bonus for this skill', items: ench.map(enchantLink), oneRow: true });
    const mons = monsterLinks().skill.get(s.id);
    if (mons) rel.push({ kind: 'links', title: 'Monsters with this skill', items: monsterChips(mons), oneRow: true });
    const pl = petsBySkill().get(s.id);
    if (pl) rel.push({ kind: 'links', title: 'Pets with this skill', items: petChips(pl), oneRow: true });
    if (rel.length) sections.push({ heading: 'Related', blocks: rel });
    // How a transformation skill's stats work, at the top of its ranks.
    const ranksSec = sv.length && sections.find(x => x.heading === 'Ranks');
    const tnotes = TRANSFORM_NOTES.filter(([re]) => re.test(String(s.id))).map(n => n[1]);
    if (ranksSec && tnotes.length) ranksSec.blocks.unshift({ kind: 'list', items: tnotes });
    return { title: s.name, subtitle: s.local || null, icon: skillIcon(s.id), category: [s.cat], kind: SKILL_KIND[s.cat] || 'Skill', infobox: info, summary: tr(r.SkillDesc), sections };
  }

  // ---------- monsters ----------
  // db/race.xml: one <Race> row per race (players, NPCs, pets, props and monsters alike), its name
  // in local/xml/race.<locale>.txt. What a race is comes from the row itself:
  //   IsGoodNPC="false"  hostile: the game treats it as an enemy (wolves, goblins, Black Knights);
  //   StringID tags      "/pc/" playable races and RP/transformation bodies, "/pet/" pets,
  //                      "/prop/" props, "/untouchable/" things that can't be hit (helpers,
  //                      controllers, cutscene copies of bosses), "/data_bug/" broken rows;
  //   "/animal/"         field animals, hostile or not (sheep, cows, hens: IsGoodNPC="true").
  // A monster is a hostile race, or an /animal/ that isn't an /npc/ or a /vehicle/, and none of the
  // excluded tags. Hidden too: rows with no name, names marked temporary ("(임시)"), and test rows
  // (a "test" tag in the StringID, ClassName "Test_..." or "...tester").
  // The client's race file has no levels, HP, experience or drops; pages show what it does have.
  //
  // Categories: RaceDesc's first part is the race's animation family ("wolf/none": coyotes,
  // jackals and hyenas share the wolves' family), grouped below; bosses (the "boss" tag) are also
  // their own category.
  const MON_GROUPS = [['animal', 'Animals'], ['insect', 'Spiders & insects'], ['reptile', 'Snakes & reptiles'], ['undead', 'Undead'],
    ['humanoid', 'Goblins & other humanoids'], ['people', 'Humans, elves & giants'], ['spirit', 'Spirits & elementals'],
    ['construct', 'Golems & constructs'], ['plant', 'Plants'], ['dragon', 'Dragons & great beasts'], ['other', 'Other'], ['boss', 'Bosses']];
  // [category key, title, families]
  const MON_FAMILIES = [
    ['animal/wolf', 'Wolves & wild dogs', 'wolf'], ['animal/dog', 'Dogs & hounds', 'dog petdog petdog2'], ['animal/fox', 'Foxes & mongooses', 'fox'],
    ['animal/bear', 'Bears', 'bear'], ['animal/cattle', 'Cattle', 'cow'], ['animal/boar', 'Boars & pigs', 'pig'], ['animal/sheep', 'Sheep & goats', 'sheep'],
    ['animal/bird', 'Birds', 'chicken ostrich petowl petbird thunderbird penguin'], ['animal/rat', 'Rats', 'rat'], ['animal/raccoon', 'Raccoons', 'raccoon'],
    ['animal/rabbit', 'Rabbits', 'rabbit'], ['animal/cat', 'Cats & lions', 'lion giantlion petcat petlazycat'],
    ['animal/horse', 'Horses & deer', 'pethorse jousthorse_giant jousthorse_human petdeer'], ['animal/bat', 'Bats', 'bat'],
    ['animal/other', 'Other animals', 'mammoth hippopotamus anteater'],
    ['insect/spider', 'Spiders', 'spider jellyspider'], ['insect/moth', 'Moths, wasps & beetles', 'moth'], ['insect/dragonfly', 'Dragonflies & hornets', 'dragonfly'],
    ['insect/scorpion', 'Scorpions', 'scorpion'], ['insect/worm', 'Worms & larvae', 'worm'],
    ['reptile/snake', 'Snakes', 'snake'], ['reptile/lizard', 'Lizards, alligators & turtles', 'lizard crocodile giantcrocodile turtle archelon'],
    ['undead/skeleton', 'Skeletons', 'skeleton'], ['undead/zombie', 'Zombies', 'zombie'], ['undead/ghost', 'Ghosts', 'ghost ghostarmor'],
    ['undead/ghoul', 'Ghouls', 'ghoul'], ['undead/lich', 'Liches & witches', 'lich'], ['undead/wight', 'Wights', 'wight'],
    ['undead/dullahan', 'Dullahans & black knights', 'dullahan'], ['undead/reaper', 'Grim reapers', 'grimreaper'],
    ['humanoid/goblin', 'Goblins', 'goblin'], ['humanoid/kobold', 'Kobolds & sahuagin', 'kobold sahuagin2'], ['humanoid/ogre', 'Ogres & cyclopes', 'ogre'],
    ['humanoid/troll', 'Trolls', 'troll'], ['humanoid/lycanthrope', 'Gnolls, werewolves & rat men', 'lycanthrope'], ['humanoid/imp', 'Imps', 'imp'],
    ['humanoid/gargoyle', 'Gargoyles & gremlins', 'gargoyle'], ['humanoid/ghast', 'Ghasts', 'cyclo'], ['humanoid/leprechaun', 'Leprechauns', 'leprechaun'],
    ['people/human', 'Humans', 'human'], ['people/elf', 'Elves', 'elf'], ['people/giant', 'Giants', 'giant'],
    ['people/warrior', 'Fighters, lancers & archers', 'warrior lancer archer shadowknight'], ['people/succubus', 'Succubi & incubi', 'succubus succubusqueen succubusfiend'],
    ['people/siren', 'Sirens', 'siren'], ['people/knight', 'Knights', 'paladin darkknight darkknight2'],
    ['people/hero', 'Gods, heroes & disciples', 'nuadha cichol neamhain morrighan2 femaledisciple disciple disciple_rudraige dnao zebach'],
    ['spirit/elemental', 'Sprites & elementals', 'elemental'], ['spirit/wisp', 'Wisps & masks', 'wisp'], ['spirit/slime', 'Slimes', 'slime'],
    ['spirit/other', 'Other spirits', 'beholder snowball bigsnowball mirrorwitch petsprite petfairy dhwander grief'],
    ['construct/golem', 'Golems', 'golem yellow_golem tabhartas'], ['construct/mimic', 'Mimics', 'mimic'], ['construct/sword', 'Flying swords', 'swordp'],
    ['construct/book', 'Flying books', 'flyingbook'], ['construct/other', 'Other objects', 'propnpc ragdoll petbroom door falias_rock'],
    ['plant', 'Plants', 'plant plantpod treeboss'],
    ['dragon/dragon', 'Dragons & wyverns', 'dragon red_dragon black_dragon wyvern petdragon'], ['dragon/serpent', 'Great serpents', 'pythonstone'],
    ['dragon/other', 'Other great beasts', 'glasgavelen crommcruaich balrog balor gorgo claimhsolas miniglas octopus octopusboss neaghboss petgriffin beasthead'],
  ];
  const MON_FAMILY = {};
  for (const [key, , fams] of MON_FAMILIES) for (const f of fams.split(' ')) MON_FAMILY[f] = key;
  const MON_CATS = []; // [key, title, parent] in display order
  for (const [g, title] of MON_GROUPS) {
    MON_CATS.push([g, title, '']);
    for (const [key, t] of MON_FAMILIES) if (key.indexOf(g + '/') === 0) MON_CATS.push([key, t, g]);
  }
  const MON_CAT_TITLE = Object.fromEntries(MON_CATS.map(c => [c[0], c[1]]));
  const familyOf = r => String(r.RaceDesc || '').split('/')[0].trim().toLowerCase();
  const raceTags = r => String(r.StringID || '').split('/').map(s => s.trim().toLowerCase()).filter(Boolean);

  // Tags that say where or how a race is used, shown next to its name ("Boss · Hard mode").
  const MON_VARIANTS = [['boss', 'Boss'], ['giantfieldboss', 'Field boss'], ['hardmode', 'Hard mode'], ['shadow', 'Shadow'],
    ['shadowmission', 'Shadow Mission'], ['summoned', 'Summoned'], ['towerdefence', 'Tower defense'], ['stagedungeon', 'Stage dungeon'],
    ['event', 'Event'], ['halloween', 'Halloween']];
  // Tags (and the row's Blow/Shove flags) whose meaning is plain from their names.
  const MON_TRAITS = [['undead', 'Undead'], ['winged', 'Winged'], ['ranged', 'Attacks from range'], ['use_dash', 'Can dash'],
    ['not_move', "Doesn't move"], ['not_down', "Can't be knocked down"], ['no_hitmotion', "Doesn't flinch when hit"],
    ['not_freeze', "Can't be frozen"], ['unable_tame', "Can't be tamed"], ['no_finish', "Can't be finished off"],
    ['unable_sharpmind', "Sharp Mind doesn't show its skills"], ['sketchable', 'Can be sketched'], ['always_sketchable', 'Can be sketched']];
  const raceName = s => String(s || '').replace(/<mini>([^<]*)<\/mini>\s*/gi, '').trim();
  const raceMini = s => { const m = /<mini>([^<]*)<\/mini>/i.exec(String(s || '')); return m ? m[1].trim() : ''; };

  let RACE_ALL = null, MONSTERS = null, MON_BY_ID = null;
  function raceRows() {
    if (RACE_ALL) return RACE_ALL;
    RACE_ALL = new Map();
    for (const r of rows('db/race.xml', 'Race')) if (r.ID && !RACE_ALL.has(r.ID)) RACE_ALL.set(r.ID, r);
    return RACE_ALL;
  }
  function monsterHidden(r, t, name) {
    if (!name || /임시/.test(name)) return true;
    if (t.some(x => /test/.test(x))) return true;
    return /^test_|tester$/i.test(String(r.ClassName || '').trim()) || /^test_|tester$/i.test(String(r.EnglishName || '').trim());
  }
  function monsters() {
    if (MONSTERS) return MONSTERS;
    MONSTERS = [];
    MON_BY_ID = new Map();
    for (const [id, r] of raceRows()) {
      const t = raceTags(r), ts = new Set(t);
      if (['pc', 'pet', 'prop', 'untouchable', 'data_bug'].some(k => ts.has(k))) continue;
      if (petRaceDesc().has(id)) continue; // pets without the tag (604020 Hell Steed)
      const hostile = /^false$/i.test(String(r.IsGoodNPC || '').trim());
      if (!hostile && !(ts.has('animal') && !ts.has('npc') && !ts.has('vehicle'))) continue;
      const raw = tr(r.LocalName), eng = String(r.EnglishName || '').trim();
      let name = raceName(raw), local = '';
      if (!english(name) && english(eng)) { local = name; name = eng; }
      if (monsterHidden(r, t, name)) continue;
      const mini = raceMini(raw);
      if (mini) name += ` (${mini})`;
      const fam = familyOf(r), cat = MON_FAMILY[fam] || 'other';
      const m = { id, r, t, ts, name, local, hostile, fam, cat, boss: ts.has('boss'),
        variant: MON_VARIANTS.filter(([k]) => ts.has(k)).map(v => v[1]).join(' · ') };
      MONSTERS.push(m);
      MON_BY_ID.set(id, m);
    }
    return MONSTERS;
  }
  const monsterById = id => (monsters(), MON_BY_ID.get(String(id).trim()));

  // FirstCreateStript: setmonsterskill(<skill>, <rank>) (and addskill) give the race its skills
  // (the later call of the same skill wins); additemex(id:<item> ...) gives it an item to wear or hold;
  // setbody(h: f: u: l:) scales its height, weight, upper and lower body.
  function raceScript(r) {
    // Calls are split by ";" or an escaped newline (a literal backslash-n).
    const s = String(r.FirstCreateStript || '').replace(/\\[nrt]/g, ' '), skills = new Map(), gear = [];
    for (const m of s.matchAll(/(?<![a-z])(?:setmonsterskill|addskill)\s*\(\s*(\d+)\s*,\s*(\d+)\s*\)/gi)) {
      skills.delete(m[1]);
      skills.set(m[1], num(m[2]));
    }
    for (const m of s.matchAll(/(?<![a-z])additemex\s*\(([^)]*)\)/gi)) {
      const id = /(?:^|\s)id\s*:\s*(\d+)/i.exec(m[1]);
      if (id && !gear.includes(id[1])) gear.push(id[1]);
    }
    const b = /(?<![a-z])setbody\s*\(([^)]*)\)/i.exec(s), body = {};
    if (b) for (const m of b[1].matchAll(/(\w+)\s*:\s*([\d.]+)/g)) body[m[1].toLowerCase()] = num(m[2]);
    return { skills, gear, body };
  }
  // ExtraData: <xml attr="..."> with optional children (weaken_skills, deprotect_weapons,
  // enlarge_skills, vehicle, joint_attack...), its attributes quoted with " or '.
  const attrsQ = t => { const o = {}; String(t || '').replace(/(\w+)\s*=\s*(["'])(.*?)\2/g, (m, k, q, v) => { o[k.toLowerCase()] = v; }); return o; };
  function raceExtra(r) {
    const s = String(r.ExtraData || ''), root = /<xml\b([^>]*?)\/?>/i.exec(s);
    const out = { attrs: root ? attrsQ(root[1]) : {}, weaken: [], deprotect: [], enlarge: [], vehicle: '', joints: [] };
    for (const m of s.matchAll(/<weaken_skill\b([^>]*)>/gi)) { const a = attrsQ(m[1]); if (a.id) out.weaken.push([a.id, a.dmg_rate]); }
    for (const m of s.matchAll(/<deprotect_weapon\b([^>]*)>/gi)) { const a = attrsQ(m[1]); if (a.id && !out.deprotect.some(d => d[0] === a.id)) out.deprotect.push([a.id, a.value]); }
    for (const m of s.matchAll(/<enlarge_skill\b([^>]*)>/gi)) { const a = attrsQ(m[1]); if (a.id) out.enlarge.push([a.id, a.scale]); }
    const en = /<enlarge_skills\b([^>]*)>/i.exec(s);
    out.enlargeRange = en ? attrsQ(en[1]) : {};
    const v = /<vehicle\b([^>]*)>/i.exec(s);
    if (v) out.vehicle = attrsQ(v[1]).type || '?';
    for (const m of s.matchAll(/<joint\b([^>]*)>/gi)) { const a = attrsQ(m[1]); if (a.name) out.joints.push(a); }
    return out;
  }
  // Root attributes of ExtraData shown under their own label; the rest are listed by name.
  const EXTRA_LABELS = { music: 'Battle music', reflect_rate: 'Damage reflection rate', demigod_boost_rate: 'Demigod boost rate',
    stone_rate: 'Petrification rate', stone_time: 'Petrification time', not_interrupt_prob: 'Chance not to be interrupted',
    dash_distance: 'Dash distance', dash_attack_width: 'Dash attack width', windmill_penalty: 'Windmill penalty',
    windmill_additional_range: 'Extra Windmill range', ranged_attack_bonus_range: 'Extra ranged attack range',
    barrier_protect: 'Barrier protection', down_duration: 'Knockdown duration', min_height: 'Lowest flying height',
    max_height: 'Highest flying height', height_adjust: 'Height adjustment', scale: 'Scale', body_scale: 'Body scale',
    head_scale: 'Head scale', leg_scale: 'Leg scale', respawn_tick: 'Respawn tick', rooting_time: 'Rooting time',
    rainy_damage_rate: 'Damage rate in rain', downdelay_add: 'Extra knockdown delay', hunt_point_count: 'Hunt points' };
  const EXTRA_SKIP = new Set(['enablealphaout', 'mate_stringid', 'effect_itemid', 'id', 'stop_rain_effect', 'default_joint']);
  // The units of the root attributes that have one: the times are milliseconds (shown in seconds),
  // stone_rate is a percent (the server rolls rand(100) < it), not_interrupt_prob a chance from 0 to 1,
  // and the damage rates multiply. The rest are shown as they are.
  const EXTRA_UNITS = { stone_time: 'ms', down_duration: 'ms', downdelay_add: 'ms', stone_rate: '%', not_interrupt_prob: 'p',
    demigod_boost_rate: '×', rainy_damage_rate: '×' };
  const extraValue = (k, v) => {
    const u = EXTRA_UNITS[k], n = Number(String(v).trim());
    if (!u || String(v).trim() === '' || !Number.isFinite(n)) return v;
    return u === 'ms' ? +(n / 1000).toFixed(2) + ' s' : u === 'p' ? +(n * 100).toFixed(2) + '%' : u === '%' ? n + '%' : '×' + n;
  };

  // db/collectingform.xml: what gathering from a target gives. TargetId is a StringID pattern
  // ("/animal/sheep/cattle/"): a race matches when it has all its tags. Products: "item[,weight];...".
  let FORMS = null;
  function collectingForms() {
    if (FORMS) return FORMS;
    FORMS = rows('db/collectingform.xml', 'CollectingForm').map(r => ({
      r, target: String(r.TargetId || '').split('/').map(s => s.trim().toLowerCase()).filter(Boolean),
      products: String(r.Products || '').split(';').map(p => p.split(',').map(s => s.trim())).filter(p => /^\d+$/.test(p[0]) && p[0] !== '0')
        .map(p => [p[0], p.length > 1 ? num(p[1]) : 1]),
    })).filter(f => f.target.length && f.products.length);
    return FORMS;
  }
  const formsFor = m => collectingForms().filter(f => f.target.every(x => m.ts.has(x)));
  const toolWords = s => String(s || '').split('/').filter(Boolean).map(x => x === 'barehand' ? 'Bare hands' : cap(words(x).toLowerCase())).join(', ');

  // Skills, items and races -> the monsters that use them (built once, for those pages' links).
  let MON_LINKS = null;
  function monsterLinks() {
    if (MON_LINKS) return MON_LINKS;
    MON_LINKS = { skill: new Map(), gear: new Map(), gathered: new Map(), summon: new Map() };
    const add = (map, k, m) => { if (!map.has(k)) map.set(k, []); const l = map.get(k); if (!l.includes(m)) l.push(m); };
    for (const m of monsters()) {
      const sc = raceScript(m.r);
      for (const id of sc.skills.keys()) add(MON_LINKS.skill, id, m);
      for (const id of sc.gear) add(MON_LINKS.gear, id, m);
      for (const f of formsFor(m)) for (const [id] of f.products) add(MON_LINKS.gathered, id, m);
    }
    // Items whose XML names a race (raceid="..." / sm_race="...": summoning crystals and whistles).
    for (const [id, it] of items()) {
      const x = xmlAttrs(it.r.XML), rid = x.raceid || x.sm_race;
      if (rid && monsterById(rid)) add(MON_LINKS.summon, rid, { id, it });
    }
    return MON_LINKS;
  }
  // Monster links, one per name (races often share a name: the first of each is linked).
  function monsterChips(list) {
    const seen = new Set(), out = [];
    for (const m of list) {
      if (seen.has(m.name)) continue;
      seen.add(m.name);
      out.push({ t: 'monster', id: m.id, label: m.name });
    }
    return out;
  }
  // ---------- monster behaviour ----------
  // GAME_SOURCES.ai: the monster AI files summarized by the pipeline (G13's, by race ClassName). t: per
  // file, sense [sight, angle, hearing, leash radius] (numbers or %variables), ch (<character> settings), att
  // [[race tags or ClassName, attitude, 'stringid'|'name']], opt [[option, attrs]], st [[state, type, attrs,
  // actions, events, target weights]]; r: {classname: [file, {variable: value}]}. An action is [kind, value,
  // chance % in one run of the state's pattern, condition]. How the engine reads these (traced in the AI
  // engine's own code): a doubt rule makes it wary, hate hostile; hate_nearby
  // turns wary into hostile after `duration` near it (battlemode: only while the player is in battle
  // stance); being attacked always makes it hostile; target_type one/two/three: it won't attack someone
  // fewer than 1/2/3 others... already attack (within 3,000), party: no other of its race, none: never; a
  // fear rule doesn't make it run, it just won't pick targets while that race is sensed; it gives up past
  // the leash radius (3,600 by default) when not hit for 20 s, unless return_to_startpoint="false"; it
  // forgets someone 30 s after last sensing them. States: idle = neutral, alert = doubt, attack = hate.
  const AI_STATE_OF = { idle: 'idle', neutral: 'idle', alert: 'wary', doubt: 'wary', attack: 'battle', hate: 'battle', support: 'support', love: 'love' };
  const AI_STATES = { wary: 'When wary', battle: 'In battle', support: 'When it holds back', love: 'Near those it likes' };
  // Events a state reacts to (the ones the engine raises); the first hit before a fight doesn't run one.
  const AI_EVENTS = { hit: 'When hit', hit_magic: 'When hit by magic', blowhit: 'When knocked down', blowhit_critical: 'When knocked down by a critical hit',
    defence: 'When it blocks with Defense', countered: 'When knocked down by Counterattack', critical_hit: 'When it takes a critical hit' };
  const AI_MOVES = { move_around: 'Circles its target', move_against: 'Backs away', follow: 'Follows its target', chase: 'Chases its target',
    move_random: 'Moves about', move_random_by_cur_pos: 'Moves about', move_to_circle: 'Moves into a circle', circle: 'Circles', warp_random: 'Teleports',
    warp_to_target: 'Teleports to its target', fly_to_target_attack: 'Swoops at its target', dragon_dash: 'Charges', jump: 'Jumps', escape_now: 'Flees',
    skill_relax: 'Rests', switch_weaponset: 'Switches weapons', move_waypoint: 'Patrols', infinite_slash: 'Infinite Slash', say: 'Says something' };
  // Race tags ("*/pc/*", "*/wolf/*&*/boss/*") in words.
  // Race tags in words, capitalized as names ("*/wolf/*" Wolf); the game's "ahchemy" typo fixed.
  const AI_TAGS = { pc: 'Players', npc: 'NPCs', pet: 'Pets', human: 'Humans', elf: 'Elves', giant: 'Giants', monster: 'Monsters', animal: 'Animals',
    ahchemy_golem: 'Alchemy Golems', alchemy_golem: 'Alchemy Golems' };
  const tagWords = s => [...new Set(String(s || '').split(/[|&]/).map(x => x.replace(/[*\/!\s]/g, ' ').trim()).filter(Boolean)
    .map(x => AI_TAGS[x.toLowerCase().replace(/\s+/g, '_')] || titleCase(words(x).toLowerCase())))].join(' and ');
  // A rule's condition as a cell: each part a line, the ones after the first with "and" (a part said
  // twice, once).
  function aiCondCell(c) {
    const parts = [...new Set(aiCondWords(c).split(', and '))].filter(Boolean);
    return parts.length > 1 ? { lines: parts.map((p, i) => (i ? 'and ' + p : cap(p))), sep: '' } : parts.length ? cap(parts[0]) : '—';
  }
  function aiCondWords(c) {
    if (!c) return '';
    return c.split(' & ').map(tok => {
      const not = tok.startsWith('not '), t = not ? tok.slice(4) : tok;
      let m;
      if ((m = /^life<=([\d.]+)$/.exec(t))) return `its life is ${not ? 'above' : 'at or below'} ${m[1]}%`;
      if ((m = /^life<([\d.]+)$/.exec(t))) return `its life is ${not ? 'at least' : 'below'} ${m[1]}%`;
      if ((m = /^life>([\d.]+)$/.exec(t))) return `its life is ${not ? 'at most' : 'above'} ${m[1]}%`;
      if ((m = /^dist>(\d+)$/.exec(t))) return `its target is ${not ? 'within' : 'farther than'} ${num(m[1]).toLocaleString('en-US')}`;
      if ((m = /^mana>(\d+)$/.exec(t))) return `its Mana is ${not ? 'at most' : 'above'} ${m[1]}`;
      if ((m = /^(can|prep):(\d+)$/.exec(t))) return `${skillName(m[2])} is ${not ? 'not ' : ''}ready`;
      if ((m = /^tcan:(\d+)$/.exec(t))) return `its target ${not ? "can't" : 'can'} use ${skillName(m[1])}`;
      if ((m = /^tlife<(\d+)$/.exec(t))) return `its target's life is ${not ? 'at least' : 'below'} ${m[1]}`;
      if ((m = /^cond:(\d+)$/.exec(t))) return `it's ${not ? 'not ' : ''}under a certain condition`;
      return not ? 'otherwise' : 'in some situations';
    }).join(', and ');
  }
  function aiActionCell(kind, val) {
    const sk = id => (skillById(id) ? skillLink(id, skillById(id).name) : 'Skill ' + id);
    if (kind === 'skill') return val ? sk(val) : 'A skill';
    if (kind === 'melee' || kind === 'auto') {
      // combo: how many attacks ("1|2|3": one of them at random); auto_attack shoots when it has a bow.
      const hits = String(val || '').split('|').filter(Boolean).map(Number), lo = Math.min(...hits), hi = Math.max(...hits);
      const n = hits.length ? ` (${lo === hi ? hi : lo + '–' + hi} hit${hi > 1 ? 's' : ''})` : '';
      return kind === 'auto' ? `Attacks${n}, shooting if it has a ranged weapon` : `Attacks${n}`;
    }
    if (kind === 'ranged') return 'Shoots';
    if (kind === 'magic') return val ? { lines: [['Casts ', sk(val)]], sep: '' } : 'Casts magic';
    return AI_MOVES[kind] || cap(words(kind).toLowerCase());
  }
  // Races by ClassName (lowercase), for the AI's race names.
  const raceByClass = () => cached('raceByClass', () => new Map(rows('db/race.xml', 'Race').filter(x => x.ClassName).map(x => [x.ClassName.trim().toLowerCase(), x.ID])));
  function monsterBehaviour(r, skills) {
    const A = (window.GAME_SOURCES && window.GAME_SOURCES.ai) || {}, ref = (A.r || {})[String(r.ClassName || '').trim().toLowerCase()];
    const t = ref && (A.t || {})[ref[0]];
    if (!t) return [];
    const vars = ref[1] || {}, raw = v => String(v || '').replace(/%(\w+)/g, (x, k) => vars[k] || ''), val = v => num(raw(v));
    const dist = n => n.toLocaleString('en-US'), secs = d => { const [a, b] = raw(d).split('~').map(x => num(x) / 1000); return b && b !== a ? `${a}–${b} seconds` : `${a} second${a === 1 ? '' : 's'}`; };
    // A rule's race: a ClassName as that race's name (a link when it's a monster), tags in words.
    const who = (s, kind) => {
      if (kind === 'name') { const id = raceByClass().get(String(s).trim().toLowerCase()); const mm = id && monsterById(id); return mm ? raceLink(id) : titleCase(words(s).toLowerCase()); }
      return /(^|[^a-z])pc([^a-z]|$)/i.test(s) ? 'Players and their pets' : tagWords(s);
    };
    const list = xs => (xs.length ? { lines: [[].concat(...xs.map((x, i) => (i ? [', '] : []).concat([x])))], sep: '' } : null);
    const kv = [];
    if (t.sense) {
      const [sight, angle, hear] = t.sense.map(val);
      const parts = [];
      if (sight) parts.push(`sees ${dist(sight)} away${angle ? ` in a ${angle}° arc in front of it` : ''}`);
      if (hear) parts.push(`hears ${dist(hear)} away in any direction`);
      if (parts.length) kv.push(['Notices you', parts.length > 1 ? { lines: parts.map(cap), sep: '' } : cap(parts[0])]);
    }
    const ch = t.ch || {}, opt = t.opt || [], optOf = n => opt.find(o => o[0] === n);
    const rulesOf = type => t.att.filter(a => a[1] === type).map(a => who(a[0], a[2]));
    // With no battle state, a hostile race only makes it wary (the engine falls back to that state).
    const fights = t.st.some(s => AI_STATE_OF[s[0]] === 'battle');
    if (!fights) {
      const wary = rulesOf('hate').concat(rulesOf('doubt'));
      if (wary.length) kv.push(['Wary of', list(wary)]);
      kv.push(['Fights', 'Never starts a fight']);
    } else if (ch.always_hate === 'true') kv.push(['Hostile to', 'Everyone it notices']);
    else if (rulesOf('hate').length) kv.push(['Hostile to', list(rulesOf('hate'))]);
    if (fights && rulesOf('doubt').length) {
      const near = optOf('hate_nearby'), stance = optOf('hate_nearby_battlemode');
      kv.push(['Wary of', list(rulesOf('doubt'))]);
      kv.push(['Turns hostile', near ? `After you've been near it for ${secs(near[1].duration || '5000')}`
        : stance ? `After you've been near it in battle stance for ${secs(stance[1].duration || '5000')}` : 'Only when attacked']);
      // The Wolf Hunter and Bear Hunter titles stop the timer for wolves and beasts.
      const sid = String(r.StringID || '').toLowerCase(), title = (near || stance) && (/wolf/.test(sid) ? titleById(66) : /beast/.test(sid) ? titleById(67) : null);
      if (title && titles().includes(title)) kv.push(['Stays calm toward', { lines: [['Anyone using the title ', { t: 'title', id: title.id, label: title.name }]], sep: '' }]);
    }
    if (rulesOf('fear').length) kv.push(["Won't start a fight near", list(rulesOf('fear'))]);
    if (rulesOf('love').length) kv.push(['Stays near', list(rulesOf('love'))]);
    // Joining in: an attack on one of its kind, or on a race it guards, that it senses. (A stringid
    // written as a glob never matched in this engine, so only race names count.)
    const guards = [];
    if (optOf('hate_party_attacked')) guards.push('Another of its kind');
    for (const [n, a] of opt) if (n === 'hate_race_attacked' && a.name) guards.push(who(a.name, 'name'));
    if (guards.length) kv.push(['Joins in when you attack', list(guards)]);
    const TT = { one: 'Anyone another monster is already fighting', two: 'Anyone two monsters are already fighting', three: 'Anyone three monsters are already fighting',
      party: 'Anyone another of its kind is already fighting', none: 'Anyone. It only fights back.' };
    const tt = ch.target_type || t.tt;
    if (TT[tt]) kv.push(["Won't go after", TT[tt] + (tt !== 'none' ? ' (within 3,000)' : '')]);
    const states = t.st.map(s => [AI_STATE_OF[s[0]] || s[0]].concat(s.slice(1)));
    const idle = states.find(s => s[0] === 'idle');
    if (idle) {
      const wander = idle[3].find(a => /^move_random/.test(a[0]));
      kv.push(['When idle', idle[1] === 'idle_fake_sleep' ? 'Pretends to sleep, and only wakes once it\'s attacked or turns hostile' : wander ? `Wanders${val(wander[1]) ? ` within ${dist(val(wander[1]))} of where it appeared` : ''}` : 'Stays put']);
    }
    const atk = states.find(s => s[0] === 'battle' && s[5]);
    const w = atk && atk[5], picks = w ? [['dmg/cleanhit', 'melee'], ['dmg/magic', 'magic'], ['dmg/ranged', 'ranged attacks']]
      .filter(([k]) => num(w[k])).sort((a, b) => num(w[b[0]]) - num(w[a[0]])).map(([, l]) => l) : [];
    // A <target> list: a weighted random pick among those who've hurt it, by the kind of damage (the
    // weights over damage shares, so the amount mostly cancels out); re-picked every `update` ms.
    // One fact a line.
    if (fights) kv.push(['Who it attacks', picks.length ? { lines: ['Picks at random among those who\'ve hurt it', `Picks again every ${num(w.update) / 1000} seconds`,
      `The kind of damage counts more than the amount: ${picks.join(', then ')}`], sep: '' }
      : 'Whoever hit it most recently, then whoever is nearest']);
    // A hit with no reaction in its state: a 3-hit counter on the attacker, unless ignore_hitevent.
    if (!(atk || states.find(s => s[0] === 'battle') || [])[4]?.some(e => e[0] === 'hit' && !e[2]))
      kv.push(['When hit', ch.ignore_hitevent === 'true' ? 'Carries on with what it was doing' : 'Hits back (up to 3 hits)']);
    kv.push(['Gives up', ch.return_to_startpoint === 'false' ? 'Never. It doesn\'t return home.'
      : `When it's more than ${dist(val(t.sense && t.sense[3]) || 3600)} from where it appeared and hasn't been hit for 20 seconds`]);
    kv.push(['Forgets you', '30 seconds after it last noticed you']);
    // Every value under its label, so the list reads one way (most are sentences).
    const blocks = [{ kind: 'kv', stack: true, rows: kv }];
    // What it does in each state and when something happens to it. Skills it doesn't have fail, so
    // they're left out.
    const usable = a => a[0] !== 'skill' && a[0] !== 'magic' || !a[1] || skills.has(String(a[1]));
    const table = acts => acts.filter(a => a[2] > 0 && usable(a)).sort((a, b) => (a[3] ? 1 : 0) - (b[3] ? 1 : 0) || b[2] - a[2])
      .map(([kind, v, pct, cond]) => [aiActionCell(kind, v), (Math.round(pct * 10) / 10) + '%', cond ? aiCondCell(cond) : '—']);
    const more = [];
    for (const s of states) {
      if (!AI_STATES[s[0]]) continue;
      const rowsS = table(s[3]);
      if (rowsS.length) more.push({ kind: 'table', title: AI_STATES[s[0]], columns: ['Does', 'Chance', 'When'], rows: rowsS });
      for (const [ev, acts, onlySkill] of s[4]) {
        // An event with a skill_id fires only for that attacker skill; critical_hit fires only with one.
        if (ev === 'critical_hit' && !onlySkill) continue;
        const rowsE = AI_EVENTS[ev] ? table(acts) : [];
        if (rowsE.length) more.push({ kind: 'table', title: `${AI_STATES[s[0]]}: ${AI_EVENTS[ev].toLowerCase()}${onlySkill ? ' by ' + skillName(onlySkill) : ''}`, columns: ['Does', 'Chance', 'When'], rows: rowsE });
      }
    }
    if (more.length) blocks.push({ kind: 'details', summary: 'What it does', blocks: [...more,
      { kind: 'note', text: 'Each chance is how often it does that each time it decides what to do next, and one decision can include several of these. When it\'s hit before a fight starts, it just turns on the attacker.' }] });
    return blocks;
  }
  // ---------- elements ----------
  // GAME_SOURCES.elements = {race id: [physical, fire, ice, lightning]}: a monster's element as the server
  // keeps it (out of 9; fire and ice already cancelled). Traced in the G13 server (StandardD
  // CalcElementalDmgRate 0x100a4a64, checked against the DLL on 67,780 inputs):
  //   - an element is [physical, fire, ice, lightning]; a character is 9/0/0/0, and each equipped item with
  //     an elemental crystal adds 1 of its element and takes 1 from physical (no cap);
  //   - melee and normal ranged attacks use the attacker's element; spells use a fixed one (Firebolt and
  //     the other fire spells fire 9, the ice spells ice 9, the lightning ones lightning 9), an arrow lit at a campfire
  //     physical 2 / fire 7;
  //   - damage x elementRate(attacker, defender).
  const ELEMENT_NAMES = ['Physical', 'Fire', 'Ice', 'Lightning'];
  const elementOf = id => ((window.GAME_SOURCES && window.GAME_SOURCES.elements) || {})[String(id)] || null;
  // The damage multiplier: the same elements cancel (the larger side keeps the difference), then
  // physical + lightning (+ the defender's ice / 3) + fire (+ up to the defender's ice again) + ice
  // (+ the defender's lightning / 3, + up to the defender's fire again), over 9. The defender's physical
  // isn't read, so an all-physical attacker always does x1.
  function elementRate(att, dfn) {
    const a = att.slice(), d = dfn.slice();
    for (const k of [1, 2, 3]) { const diff = d[k] - a[k]; if (diff > 0) { a[k] = 0; d[k] = diff; } else { a[k] = -diff; d[k] = 0; } }
    let r = a[0];
    if (a[3] > 0) r += a[3] + Math.trunc(d[2] / 3);
    if (a[1] > 0) r += a[1] + Math.min(a[1], d[2]);
    if (a[2] > 0) { const t = a[2] + Math.trunc(d[3] / 3); r += t + Math.min(t, d[1]); }
    return r / 9;
  }
  // A character with f fire, i ice and l lightning crystals in their equipment.
  const gearElement = (f, i, l) => [9 - f - i - l, f, i, l];
  // [attack, its element, its damage multiplier]: an arrow lit at a campfire also does ×1.5 (FARROW).
  const SPELL_ELEMENTS = [['Fire spells (Firebolt, Fireball...)', [0, 9, 0, 0]], ['Ice spells (Icebolt, Ice Spear...)', [0, 0, 9, 0]],
    ['Lightning spells (Lightning Bolt, Thunder...)', [0, 0, 0, 9]], ['Arrows lit at a campfire (the ×1.5 counted)', [2, 7, 0, 0], 1.5]];
  // The crystals (up to 9, one per worn item) that do the most damage to a monster, fewest first on a tie;
  // and the ones that take the least from its own attacks.
  function bestGear(el) {
    let best = null, safe = null;
    for (let f = 0; f <= 9; f++) for (let i = 0; f + i <= 9; i++) for (let l = 0; f + i + l <= 9; l++) {
      const g = gearElement(f, i, l), n = f + i + l, out = elementRate(g, el), inn = elementRate(el, g);
      if (!best || out > best.rate + 1e-9 || (Math.abs(out - best.rate) < 1e-9 && n < best.n)) best = { f, i, l, n, rate: out };
      if (!safe || inn < safe.rate - 1e-9 || (Math.abs(inn - safe.rate) < 1e-9 && n < safe.n)) safe = { f, i, l, n, rate: inn };
    }
    return { best, safe };
  }
  const gearText = g => (g.n ? [[g.f, 'Fire'], [g.i, 'Ice'], [g.l, 'Lightning']].filter(x => x[0]).map(([n, e]) => `${n} ${e}`).join(' + ') : 'No crystals');
  const pctOf = r => `×${(Math.round(r * 1000) / 1000).toString()}`;
  // A monster's element for its page: [[name, % of 9]] for the parts it has besides physical (largest
  // first), [['Physical only', ...]] when it has none, or null when the race isn't listed.
  function monsterElements(id) {
    const v = elementOf(id);
    if (!v || !v.some(Boolean)) return null;
    const parts = [1, 2, 3].filter(k => v[k] > 0).map(k => [ELEMENT_NAMES[k], Math.round(100 * v[k] / 9)]).sort((a, b) => b[1] - a[1]);
    return parts.length ? parts : [['Physical only', null]];
  }
  const elementsText = els => els.map(([n, p]) => (p == null ? n : `${n} ${p}%`)).join(' · ');
  // The "Elements" section of a monster with any fire, ice or lightning: what each kind of attack does to
  // it, the crystals that hurt it most, and the ones that make its attacks hurt you least.
  function monsterElementBlocks(id) {
    const el = elementOf(id);
    if (!el || !(el[1] || el[2] || el[3])) return [];
    const { best, safe } = bestGear(el);
    const rows = SPELL_ELEMENTS.map(([n, a, m = 1]) => [n, pctOf(elementRate(a, el) * m)]);
    rows.push(['Melee and ranged with no elemental crystals', '×1']);
    if (best.n) rows.push([{ lines: [`Melee and ranged with ${gearText(best)} crystals`, 'The most damage'], sep: '' }, pctOf(best.rate)]);
    return [{ kind: 'table', columns: ['Attack', 'Damage'], rows },
      { kind: 'kv', rows: [['Crystals that hurt it most', `${gearText(best)} (${pctOf(best.rate)})`],
        ['Crystals that soften its own hits on you', `${gearText(safe)} (its damage ${pctOf(safe.rate)})`]] },
      { kind: 'links', title: 'How elements work', items: [{ t: 'guide', id: 'elements', label: 'Elements guide' }] }];
  }
  function monsterPage(id) {
    const m = monsterById(id);
    if (!m) return null;
    const r = m.r, ts = m.ts, sc = raceScript(r), ex = raceExtra(r);
    const info = [['Kind', m.boss ? 'Boss' : 'Monster'], ['Disposition', m.hostile ? 'Hostile' : 'Peaceful']];
    if (MON_CAT_TITLE[m.cat] && m.cat !== 'other') info.push(['Family', MON_CAT_TITLE[m.cat]]);
    const variant = MON_VARIANTS.filter(([k]) => k !== 'boss' && ts.has(k)).map(v => v[1]).join(' · ');
    if (variant) info.push(['Variant', variant]);
    const g = String(r.Gender || '').trim();
    if (g === '1' || g === '2') info.push(['Gender', g === '1' ? 'Female' : 'Male']);
    if (ex.vehicle) info.push(['Can be ridden', 'Yes']);
    const els = monsterElements(id);
    // Linked to the elements guide, which explains the parts.
    if (els) info.push(['Elements', { t: 'guide', id: 'elements', label: elementsText(els) }]);
    if (r.ClassName) info.push(['Internal name', r.ClassName]);
    const sections = [];
    // The 3D model: the race's mesh and skeleton, read from the packs when the reader asks.
    if (!NO3D && String(r.DefaultMesh || '').trim()) sections.push({ heading: '3D model', blocks: [{ kind: 'embed', name: 'race3d', args: { id: String(id) }, caption: MODEL_CAPTION }] });

    const combat = [];
    if (num(r.MeleeAttackRange)) combat.push(['Melee range', num(r.MeleeAttackRange)]);
    // DefaultAttackSpeed and DefaultDownHitCount: read like a weapon's Par_AttackSpeed (the
    // client's five speed labels) and Par_DownHitCount (hits per combo = count + 1).
    if (r.DefaultAttackSpeed !== undefined) {
      const spd = num(r.DefaultAttackSpeed);
      combat.push(['Attack speed', spd >= 0 && spd <= 4 && /^\d+$/.test(String(r.DefaultAttackSpeed).trim()) ? SPEEDS[spd] : String(r.DefaultAttackSpeed)]);
    }
    if (r.DefaultDownHitCount !== undefined) combat.push(['Hits per combo', num(r.DefaultDownHitCount) + 1]);
    if (num(r.AttackMax)) combat.push(['Damage', `${num(r.AttackMin)}~${num(r.AttackMax)}`]);
    if (num(r.WAttackMax)) combat.push(['Injury rate', `${num(r.WAttackMin)}~${num(r.WAttackMax)}%`]);
    if (num(r.Critical)) combat.push(['Critical', num(r.Critical) + '%']);
    if (num(r.Rate)) combat.push(['Balance', num(r.Rate) + '%']);
    if (num(r.SplashDamage)) combat.push(['Splash attack', `radius ${num(r.SplashRadius)}, ${num(r.SplashAngle)}° arc, damage ${num(r.SplashDamage)}`]);
    if (num(r.PoisonDamage)) combat.push(['Poison damage', num(r.PoisonDamage)]);
    if (r.PoisonImmune !== undefined && /^\d+$/.test(String(r.PoisonImmune).trim())) combat.push(['Poison immunity', num(r.PoisonImmune)]);
    if (num(r.PoisonDamageRatio1) || num(r.PoisonDamageRatio2)) combat.push(['Poison damage ratios', `${num(r.PoisonDamageRatio1)} / ${num(r.PoisonDamageRatio2)}`]);
    // LifeRecoveryRate and RunSpeedFactor multiply the usual rate (1 is the usual one).
    if (r.LifeRecoveryRate !== undefined && num(r.LifeRecoveryRate) !== 1) combat.push(['Life recovery rate', '×' + num(r.LifeRecoveryRate)]);
    if (r.CriticalResist !== undefined && num(r.CriticalResist)) combat.push(['Critical resistance', num(r.CriticalResist)]);
    if (r.RunSpeedFactor !== undefined && String(r.RunSpeedFactor).trim()) combat.push(['Run speed factor', '×' + num(r.RunSpeedFactor)]);
    const weak = String(r.WeakPoints || '').trim();
    if (weak && weak[0] !== '<') combat.push(['Weak points', weak.split(';').map(s => s.trim()).filter(Boolean).join(', ')]);
    if (combat.length) sections.push({ heading: 'Combat', blocks: [{ kind: 'kv', rows: combat }] });
    const elBlocks = monsterElementBlocks(id);
    if (elBlocks.length) sections.push({ heading: 'Elements', blocks: elBlocks });
    const behave = monsterBehaviour(r, new Set([...sc.skills.keys()].map(String)));
    if (behave.length) sections.push({ heading: 'Behavior', blocks: behave });
    const taming = monsterTamingSection(id);
    if (taming) sections.push(taming);
    const drops = monsterDropsSection(id);
    if (drops) sections.push(drops);

    // Skills from its starting script, with the rank each is set to.
    if (sc.skills.size) {
      const list = [...sc.skills].map(([sid, lv]) => {
        const s = skillById(sid);
        return [s ? skillLink(sid, s.name) : 'Skill ' + sid, lv >= 16 || lv <= 0 ? rankLabel(lv) : 'Rank ' + rankLabel(lv)];
      });
      // Folded: some monsters have dozens.
      sections.push({ heading: 'Skills', blocks: [{ kind: 'details', summary: `${list.length} skill${list.length === 1 ? '' : 's'}`, blocks: [{ kind: 'table', columns: ['Skill', 'Rank'], rows: list }] }] });
    }
    // Damage it takes from certain skills (dmg_rate, in percent) and weapons that lower its protection.
    const dmg = [];
    if (ex.weaken.length) {
      dmg.push({ kind: 'table', title: 'Damage taken from skills', columns: ['Skill', 'Damage'],
        rows: ex.weaken.map(([sid, rate]) => { const s = skillById(sid); return [s ? skillLink(sid, s.name) : 'Skill ' + sid, rate !== undefined ? num(rate) + '%' : '']; }) });
    }
    if (ex.deprotect.length) {
      dmg.push({ kind: 'table', title: 'Weapons that change its protection', columns: ['Weapon', 'Protection'],
        rows: ex.deprotect.map(([iid, v]) => { const it = items().get(iid); return [it ? { t: 'item', id: iid, label: it.name } : 'Item ' + iid, v !== undefined ? (num(v) > 0 ? '+' : '') + num(v) : '']; }) });
    }
    if (ex.enlarge.length) {
      const rng = ex.enlargeRange;
      dmg.push({ kind: 'table', title: 'Grows when hit by' + (rng.scale_min || rng.scale_max ? ` (scale ${rng.scale_min || '?'} to ${rng.scale_max || '?'})` : ''), columns: ['Skill', 'Scale added per hit'],
        rows: ex.enlarge.map(([sid, s]) => { const k = skillById(sid); return [k ? skillLink(sid, k.name) : (sid === 'any' ? 'Any attack' : 'Skill ' + sid), s || '']; }) });
    }
    if (ex.joints.length) {
      dmg.push({ kind: 'table', title: 'Body parts', columns: ['Part', 'HP'],
        rows: ex.joints.map(a => [a.name, a.life ? num(a.life) : (truthy(a.use_parent_param) ? 'Shared with the body' : '')]) });
    }
    if (dmg.length) sections.push({ heading: 'Damage', blocks: dmg });

    // Body: setbody's proportions and ExtraData's scales.
    const size = [['h', 'Height'], ['f', 'Weight'], ['u', 'Upper body'], ['l', 'Lower body']].filter(([k]) => sc.body[k] !== undefined && sc.body[k] !== 1)
      .map(([k, l]) => [l, '×' + sc.body[k]]);
    for (const k of ['scale', 'body_scale', 'head_scale', 'leg_scale']) if (ex.attrs[k] !== undefined) size.push([EXTRA_LABELS[k], '×' + ex.attrs[k]]);
    if (size.length) sections.push({ heading: 'Size', blocks: [{ kind: 'kv', rows: size }] });

    // Gathering (collectingform.xml).
    const forms = formsFor(m);
    if (forms.length) {
      const rowsG = [];
      for (const f of forms) {
        const total = f.products.reduce((a, p) => a + p[1], 0) || 1;
        for (const [iid, w] of f.products) {
          const it = items().get(iid);
          rowsG.push([it ? { t: 'item', id: iid, label: it.name } : 'Item ' + iid, f.products.length > 1 ? +(w * 100 / total).toFixed(1) + '%' : '—',
            [toolWords(f.r.PrimeTool), toolWords(f.r.SecondTool)].filter(Boolean).filter((x, i, a) => a.indexOf(x) === i).join(' or ')]);
        }
      }
      sections.push({ heading: 'Gathering', blocks: [{ kind: 'table', columns: ['Product', 'Share of results', 'Tool'], rows: rowsG }] });
    }

    const hunted = questRefs().kill.get(String(id));
    if (hunted && hunted.size) sections.push({ heading: 'Quests', blocks: [questTable(hunted, 'Quests to defeat it')] });
    const met = dungeonsOfRace(id).map(dungeonLink).filter(x => typeof x === 'object').sort((a, b) => a.label.localeCompare(b.label));
    const inMissions = missionLinks(missionRefs().race.get(String(id)) || []);
    const appears = [];
    // Field spawns: each region and spot, how many at most, how soon they come back, and when.
    const spawnRows = [], bossRows = [];
    for (const [rid, rg] of Object.entries(regionData())) {
      // Spots alike (same place, respawn and time) are one row, their counts added up.
      const alike = new Map();
      for (const [race, n, lo, hi, place, when] of rg.s || []) if (String(race) === String(id)) {
        const row = [regionLink(rid), place || '—', n, hi ? (lo === hi ? `${lo} s` : `${lo}–${hi} s`) : '—', when ? cap(eventText(when)) : 'Always'];
        const k = [place, row[3], row[4]].join('|');
        if (alike.has(k)) alike.get(k)[2] += n; else { alike.set(k, row); spawnRows.push(row); }
      }
      for (const b of rg.b || []) if (b.r.map(String).includes(String(id)))
        bossRows.push([regionLink(rid), [...new Set(b.z.map(z => z[2]).filter(Boolean))].join(', ') || '—', bossWhen(b),
          b.c != null ? pctText(b.c) : '—', leavesAfter(b),
          b.r.length > 1 ? { lines: [[].concat(...b.r.filter(x => String(x) !== String(id)).map((x, i) => (i ? [', '] : []).concat([raceLink(x)])))], sep: '' } : '—']);
    }
    // The same spawn listed for several of the boss's groups is one row.
    const uniqueBoss = [...new Map(bossRows.map(r => [r.map(plain0).join('|'), r])).values()];
    if (bossRows.length) appears.push({ kind: 'table', title: 'As a field boss', columns: ['Region', 'Where', 'When it appears', BOSS_CHANCE, 'Leaves after', 'Appears with'], rows: uniqueBoss });
    // The formulas for working out its next appearance, said once per distinct schedule; the places and
    // events sharing one (the same event in 2007, 2008, 2009) are a small table above it.
    if (bossRows.length) {
      const groups = new Map();
      for (const rg of Object.values(regionData())) for (const b of rg.b || []) if (b.r.map(String).includes(String(id))) {
        const f = bossFormula(b), k = JSON.stringify(f);
        if (!groups.has(k)) groups.set(k, { f, code: bossFormulaCode(b), wh: [] });
        const wh = [rg.n, b.ev ? eventName(b.ev) : ''];
        if (!groups.get(k).wh.some(w => w[0] === wh[0] && w[1] === wh[1])) groups.get(k).wh.push(wh);
      }
      const gs = [...groups.values()].filter(g => g.f.length), many = gs.length > 1;
      const whText = w => w[0] + (w[1] ? ` (${w[1]} event)` : '');
      // Where a schedule applies in a line: its regions per event ("Tir Chonaill, Rano (Halloween event)").
      const whLine = wh => { const by = new Map(); for (const [r, ev] of wh) by.set(ev, (by.get(ev) || []).concat(r)); return [...by].map(([ev, rs]) => rs.join(', ') + (ev ? ` (${ev} event)` : '')).join('; '); };
      const blocks = [];
      for (const g of gs) {
        // One schedule in several places or events: a table of them above it. Several schedules: each
        // titled with where it applies.
        if (!many && g.wh.length > 1) { const ev = g.wh.some(w => w[1]); blocks.push({ kind: 'table', title: 'Where this applies', columns: ev ? ['Region', 'Event'] : ['Region'], rows: g.wh.map(w => (ev ? [w[0], w[1] || '—'] : [w[0]])) }); }
        blocks.push({ kind: 'list', title: many ? whLine(g.wh) : '', items: g.f });
      }
      const code = gs.map(g => (many || g.wh.length > 1 ? g.wh.map(w => `// ${whText(w)}\n`).join('') : '') + g.code).filter(c => c.trim()).join('\n\n') +
        '\n\n// Field Boss Info Scroll, per monster defeated in the region:\nchance = 1 / (5 × (minutes_left − 10))   if minutes_left > 10\nchance = 1                              otherwise';
      if (blocks.length) appears.push({ kind: 'details', summary: 'How to work out when it appears', blocks: blocks.concat([{ kind: 'note', text: INFO_SCROLL_NOTE },
        { kind: 'formula', title: 'As a formula', text: code }]) });
    }
    if (spawnRows.length) appears.push({ kind: 'table', title: 'In the field', columns: ['Region', 'Where', 'Most at once', 'Respawn', 'When'], rows: spawnRows },
      { kind: 'note', text: 'Most at once: how many can be out at the same time (usually about half are). Respawn: how long after one is defeated another appears.' });
    if (met.length) appears.push({ kind: 'links', title: 'Dungeons', items: met });
    if (inMissions.length) appears.push({ kind: 'links', title: 'Missions', items: inMissions });
    if (appears.length) sections.push({ heading: 'Where it appears', blocks: appears });

    const traits = MON_TRAITS.filter(([k]) => ts.has(k)).map(x => x[1]).filter((x, i, a) => a.indexOf(x) === i);
    if (/^false$/i.test(String(r.BlowAwayEnable || ''))) traits.push("Can't be knocked back");
    if (/^false$/i.test(String(r.ShovedEnable || ''))) traits.push("Can't be shoved");
    if (traits.length) sections.push({ heading: 'Traits', blocks: [{ kind: 'list', items: traits }] });

    const other = [];
    for (const [k, v] of Object.entries(ex.attrs)) {
      if (EXTRA_SKIP.has(k) || ['scale', 'body_scale', 'head_scale', 'leg_scale'].includes(k)) continue;
      other.push([EXTRA_LABELS[k] || cap(words(k).toLowerCase()), extraValue(k, v)]);
    }
    if (other.length) sections.push({ heading: 'Other data', blocks: [{ kind: 'kv', rows: other }] });
    // Related: items it wears, its mate, items that summon it, races of the same name.
    const rel = [];
    const gear = sc.gear.filter(i => items().has(i));
    if (gear.length) rel.push({ kind: 'links', title: 'Equipment', items: gear.map(i => ({ t: 'item', id: i, label: items().get(i).name })) });
    const eff = ex.attrs.effect_itemid && items().get(ex.attrs.effect_itemid);
    if (eff) rel.push({ kind: 'links', title: 'Effect item', items: [{ t: 'item', id: ex.attrs.effect_itemid, label: eff.name }] });
    if (ex.attrs.mate_stringid) {
      const want = String(ex.attrs.mate_stringid).split('/').map(s => s.trim().toLowerCase()).filter(Boolean);
      const mates = monsters().filter(o => o !== m && want.length && want.every(x => o.ts.has(x)));
      if (mates.length) rel.push({ kind: 'links', title: 'Mate', items: monsterChips(mates) });
    }
    const summon = monsterLinks().summon.get(m.id);
    if (summon) rel.push({ kind: 'links', title: 'Items that call it', items: summon.map(x => ({ t: 'item', id: x.id, label: x.it.name })) });
    const same = monsters().filter(o => o !== m && o.name === m.name);
    if (same.length) rel.push({ kind: 'links', title: 'Other races with this name', items: same.map(o => ({ t: 'monster', id: o.id, label: `${o.id}${o.variant ? ' · ' + o.variant : ''}` })) });
    if (rel.length) sections.push({ heading: 'Related', blocks: rel });
    return { title: m.name, subtitle: m.local || null, icon: null, category: [m.cat].concat(m.boss ? ['boss'] : []), infobox: info, summary: '', sections, tags: headTags('monsters', m.t) };
  }

  // ---------- NPCs ----------
  // db/npcinfo.xml: one <NpcInfo> per NPC, keyed by GeneralName: LocalName (text reference or plain
  // text), Town (a free-text label, spelled several ways: TOWNS), IconImageName/IconX/IconY (the
  // face in an npcicon atlas), Music (the NPC's theme). Rows in comments aren't used. The setup cuts
  // the faces and the portraits' base frames out of the packs (data/npc-images.js:
  // {lowercased GeneralName: {icon, portrait}}, files in data/icons/npcs/ and data/portraits/).
  // Pages don't show where an NPC stands, what it sells or says: the client's files don't say.
  const NPC_IMAGES = window.GAME_NPC_IMAGES || {};
  const npcImage = (key, kind) => { const x = NPC_IMAGES[key]; const f = x && x[kind]; return f ? url((kind === 'icon' ? 'data/icons/npcs/' : 'data/portraits/') + f) : null; };
  // An NPC's picture: its portrait (app.js crops a small one to the head), else the portrait of the
  // NPC it's a variant of (the one with its name: edern_ptj is Edern), else none (the NPC glyph). The
  // small faces aren't used, so every NPC looks alike.
  let PORTRAIT_BY_NAME = null;
  function npcFace(key) {
    const own = npcImage(key, 'portrait');
    if (own) return own;
    if (!PORTRAIT_BY_NAME) {
      PORTRAIT_BY_NAME = new Map();
      for (const x of npcs()) {
        const p = npcImage(x.id, 'portrait'), k = x.name.toLowerCase();
        if (p) PORTRAIT_BY_NAME.set(k, PORTRAIT_BY_NAME.has(k) && PORTRAIT_BY_NAME.get(k) !== p ? null : p);
      }
    }
    const n = npcById(key);
    return (n && PORTRAIT_BY_NAME.get(n.name.toLowerCase())) || null;
  }
  // Town labels (lowercase, without spaces and "_") -> the town's name. "Variable", "NONE" and blank
  // rows have no town.
  const TOWNS = {
    tirchonaill: 'Tir Chonaill', dunbarton: 'Dunbarton', bangor: 'Bangor', emainmacha: 'Emain Macha', emain: 'Emain Macha',
    taillteann: 'Taillteann', tailltean: 'Taillteann', tara: 'Tara', tararath: 'Tara Rath', tararathe: 'Tara Rath',
    cobh: 'Cobh', portcobh: 'Cobh', nekojima: 'Nekojima', avon: 'Avon', iria: 'Iria', courcle: 'Courcle', connous: 'Connous',
    connouus: 'Connous', physis: 'Physis', mabiland: 'MabiLand', dugaldaisle: 'Dugald Aisle', blago: 'Blago', postoffice: 'Post Office',
    tirnanog: 'Tir na Nog', senmag: 'Sen Mag', dungeon: 'Dungeon', qillabasecamp: 'Qilla Base Camp', reoite: 'Reoite',
    arcanelibrary: 'Arcane Library', belltower: 'Belltower', gairech: 'Gairech', beginnertutorial: 'Beginner Tutorial',
  };
  const townKey = s => String(s || '').toLowerCase().replace(/[\s_]+/g, '');
  const townOf = s => { const k = townKey(s); return !k || k === 'variable' || k === 'none' ? '' : TOWNS[k] || titleCase(words(s)); };
  const slug = s => s.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '');
  // An NPC with no town of its own ("Variable"): the region its quests send you to (sources.js
  // npcRegions, where every quest agrees), by the name db/minimapinfo.xml gives the region.
  let REGION_NAMES = null;
  function regionTown(id) {
    const reg = window.GAME_SOURCES && window.GAME_SOURCES.npcRegions && window.GAME_SOURCES.npcRegions[id];
    if (!reg) return '';
    if (!REGION_NAMES) REGION_NAMES = new Map(rows('db/minimapinfo.xml', 'MinimapInfo').map(r => [String(r.RegionID), tr(r.MapLocalName).trim()]));
    const name = REGION_NAMES.get(String(reg)) || '';
    return TOWNS[townKey(name)] || name;
  }
  // Helpers and props: names like these, and names three or more rows share that all use the
  // atlas's first face cell (npcicon_0 0,0: Party Guest, Guardsman, Mailbox, Race Usher...).
  const NPC_PROP = /mailbox|bookshelf|crystal ball|altar|telescope|viewscope|scarecrow|mortar and pestle|mail writing|helper|usher|party guest/i;
  const firstCell = r => /pcicon_0\.dds$/i.test(String(r.IconImageName || '').trim()) && num(r.IconX) === 0 && num(r.IconY) === 0;

  let NPCS = null, NPC_BY_ID = null;
  function npcs() {
    if (NPCS) return NPCS;
    NPCS = [];
    NPC_BY_ID = new Map();
    const list = rows('db/npcinfo.xml', 'NpcInfo').filter(r => r.GeneralName);
    // A name the English text doesn't have (Marcellus's is only in Korean): the internal name in words.
    const named = list.map(r => { const n = tr(r.LocalName).replace(/\s+/g, ' ').trim(); return [r, n && !english(n) ? titleCase(words(r.GeneralName)) : n]; }).filter(([, n]) => n);
    const shared = {};
    for (const [r, n] of named) if (firstCell(r)) shared[n] = (shared[n] || 0) + 1;
    const offers = upgradesByNpc(), races = goodRaces();
    for (const [r, name] of named) {
      const id = r.GeneralName.trim().toLowerCase();
      if (NPC_BY_ID.has(id)) continue;
      const town = townOf(r.Town) || regionTown(id), cats = [];
      const prop = NPC_PROP.test(name) || (firstCell(r) && shared[name] >= 3) || (name === r.GeneralName && /\d/.test(name));
      if (prop) cats.push('other');
      else {
        if (town) cats.push('town/' + slug(town));
        if (offers.has(id)) cats.push('upgrade');
        if (/^mp_/i.test(r.GeneralName)) cats.push('custom');
        if (!cats.length) cats.push('none');
      }
      // Named from its internal name: the English text has none (or only Korean), or the "name" is the
      // internal one ("Cokegirl 2 02").
      const local = tr(r.LocalName).replace(/\s+/g, ' ').trim();
      const internal = !english(local) || (/\d/.test(name) && nameKey(name) === nameKey(r.GeneralName));
      const n = { id, gn: r.GeneralName.trim(), r, name, town, cats, race: races.match(id, name), internal };
      NPCS.push(n);
      NPC_BY_ID.set(id, n);
    }
    // Story characters: portraits with no npcinfo row, named by the one race of that name
    // (ClassName or LocalName). Portraits with no such race have no name and aren't shown.
    for (const key of Object.keys(NPC_IMAGES)) {
      const id = key.toLowerCase();
      if (NPC_BY_ID.has(id) || !(NPC_IMAGES[key] || {}).portrait) continue;
      const race = races.match(id, null);
      if (!race) continue;
      const n = { id, gn: key, r: null, name: raceName(tr(race.LocalName)) || id, town: '', cats: ['story'], race, story: true };
      NPCS.push(n);
      NPC_BY_ID.set(id, n);
    }
    return NPCS;
  }
  const npcById = id => (npcs(), NPC_BY_ID.get(String(id || '').trim().toLowerCase()));
  // An available_npc key as pages show it: a link to the NPC's page, else the key capitalised.
  function npcRef(key) {
    const n = npcById(key);
    return n ? { t: 'npc', id: n.id, label: n.name, icon: npcFace(n.id) } : cap(words(key));
  }
  // Races the game treats as friendly (IsGoodNPC="true"), to find an NPC's body: by ClassName, else by
  // name when one race alone has it.
  let GOOD_RACES = null;
  function goodRaces() {
    if (GOOD_RACES) return GOOD_RACES;
    const byClass = new Map(), byName = new Map();
    const nameKey = s => String(s || '').toLowerCase().replace(/[^a-z0-9]/g, '');
    for (const r of raceRows().values()) {
      if (!/^true$/i.test(String(r.IsGoodNPC || '').trim())) continue;
      const c = String(r.ClassName || '').trim().toLowerCase();
      if (c && !byClass.has(c)) byClass.set(c, r);
      const n = raceName(tr(r.LocalName));
      if (n) { if (!byName.has(n)) byName.set(n, []); byName.get(n).push(r); }
      const nk = nameKey(n);
      if (nk) { const k = '#' + nk; if (!byName.has(k)) byName.set(k, []); byName.get(k).push(r); }
    }
    const one = l => (l && l.length === 1 ? l[0] : null);
    GOOD_RACES = { match: (id, name) => byClass.get(id) || (name ? one(byName.get(name)) : one(byName.get('#' + nameKey(id)))) || null };
    return GOOD_RACES;
  }
  // available_npc key -> the upgrades it offers.
  let UPG_NPC = null;
  function upgradesByNpc() {
    if (UPG_NPC) return UPG_NPC;
    UPG_NPC = new Map();
    for (const u of upgrades()) for (const k of u.npcs) { if (!UPG_NPC.has(k)) UPG_NPC.set(k, []); UPG_NPC.get(k).push(u); }
    return UPG_NPC;
  }
  // Titles that name the NPC: "is a friend of Ferghus".
  let FRIEND_TITLES = null;
  function friendTitles(name) {
    if (!FRIEND_TITLES) {
      FRIEND_TITLES = new Map();
      for (const t of titles()) {
        const m = /\bfriend of\s+(.+?)\s*$/i.exec(t.name);
        if (!m) continue;
        if (!FRIEND_TITLES.has(m[1])) FRIEND_TITLES.set(m[1], []);
        FRIEND_TITLES.get(m[1]).push(t);
      }
    }
    return FRIEND_TITLES.get(name) || [];
  }
  const NPC_CATS = [['town', 'Towns', ''], ['upgrade', 'Upgrade NPCs', ''], ['custom', 'Custom NPCs', ''], ['story', 'Story characters', ''],
    ['none', 'No set town', ''], ['other', 'Helpers & props', '']];

  // ---------- gifts ----------
  // GAME_SOURCES.gifts = {npc: {k: kind, w: weights, b: best [[item, point]], x: worst, s: special}}.
  // How the game decides (traced in the G13 server): every item
  // has 11 traits (itemdb Taste_*), every NPC weighs them 2 (loves), 1 (likes), -1 (dislikes) or not at
  // all; the gift's point (-10..10) is 7+ love, 4-6 like, -3..3 neutral, -4 or less dislike; one item
  // raises their liking by the point / 2 or 3 (a stack: far less). One gift per conversation.
  const GIFT_TRAITS = ['Beauty', 'Individuality', 'Luxury', 'Toughness', 'Usefulness', 'Rarity', 'Meaning', 'Adult appeal', 'Collector appeal', 'Anime style', 'Sex appeal'];
  const giftReaction = p => (p >= 7 ? 'Loves it' : p >= 4 ? 'Likes it' : p <= -4 ? 'Dislikes it' : 'Neutral');
  const giftFavor = p => {
    const s = x => (x > 0 ? '+' : '') + x, [a, b] = [Math.trunc(p / 3), Math.trunc(p / 2)].sort((x, y) => x - y);
    return a === b ? s(a) : `${s(a)} to ${s(b)}`;
  };
  function npcGiftBlocks(id) {
    const g = ((window.GAME_SOURCES && window.GAME_SOURCES.gifts) || {})[id];
    if (!g) return [];
    const blocks = [];
    if (g.k === 'taste') {
      const of = w => GIFT_TRAITS.filter((t, i) => g.w[i] === w).join(', ');
      const kv = [['Loves', of(2)], ['Likes', of(1)], ['Dislikes', of(-1)]].filter(r => r[1]);
      if (kv.length) blocks.push({ kind: 'kv', rows: kv });
      const rows = list => list.map(([it, p]) => [itemRef(it), giftReaction(p), giftFavor(p)]);
      if ((g.b || []).length) blocks.push({ kind: 'table', title: 'Best gifts', columns: ['Item', 'Reaction', 'Liking'], rows: rows(g.b) });
      if ((g.x || []).length) blocks.push({ kind: 'table', title: 'Gifts to avoid', columns: ['Item', 'Reaction', 'Liking'], rows: rows(g.x) });
      blocks.push({ kind: 'note', text: 'Liking is for one item. A whole stack counts for far less (at most +2), and they take the whole stack. Most items are neutral to everyone. One gift per conversation.' });
    } else if (g.k === 'flat') blocks.push({ kind: 'note', text: 'Takes any gift with a thank-you, but it doesn\'t change how much they like you.' });
    // One who doesn't take gifts (refuses, ignores) gets no Gifts section at all, unless a special gift.
    if ((g.s || []).length) blocks.push({ kind: 'list', title: 'Special gifts', items: g.s });
    return blocks;
  }
  // ---------- friendship ----------
  // GAME_SOURCES.affection = {npc: {t: 1/0 talking can / can't raise liking, j: usual | harsh part-time
  // job, g: [special scenes]}}; secretShops = {npc: {u: [kind, detail], tabs, i: [[item, price]]}}.
  // What's particular to an NPC's friendship: what the guide (linked with their gifts) doesn't already
  // say for everyone. Talking raising liking and the usual part-time job's liking are everyone's rules.
  const FRIEND_GUIDE = { kind: 'text', tone: 'muted', parts: ['How gifts and liking work: ', { t: 'guide', id: 'npc-friendship', label: 'NPC Friendship guide' }] };
  function npcFriendLines(id) {
    const a = ((window.GAME_SOURCES && window.GAME_SOURCES.affection) || {})[id];
    if (!a) return [];
    const items = [];
    if (a.t === 0) items.push('Talking never makes them like you more; use gifts or their part-time job.');
    if (a.j === 'harsh') items.push('Their part-time job is strict: perfect 0 to +3 liking, good 0 to +1, poor 0 to −2, fail 0 to −5.');
    for (const g of a.g || []) items.push(g);
    return items;
  }
  // The secret tabs as the shop list names them (the two lists name a few tabs differently).
  const SECRET_TAB_NAMES = { 'High-Grade Weapons': 'Fine Weapons', 'High-Grade Armor': 'Fine Armor' };
  // GAME_SOURCES.shops = {npc: {t: [[tab, [[item, price, count, cond]], rolls]], d: discount %}},
  // shopConds = {code: when}: what an NPC sells (their scripts restock it every Erinn day). price is for
  // one purchase of count (a stackable's price is for the whole stack: 100 arrows); rolls are random
  // daily stock [[items], min %, max % of the price, how many]. secretShops (above) says which tabs
  // only show under a condition; those tabs get a lock, and an item sold only there a Secret tag.
  function npcShopBlocks(id) {
    const GS = window.GAME_SOURCES || {}, ss = (GS.secretShops || {})[id];
    const s = (GS.shops || {})[id] || { t: [] };
    if (!s.t.length && !ss) return [];
    const conds = GS.shopConds || {};
    const gold = p => (p == null ? '—' : p.toLocaleString('en-US') + ' G');
    const tabs = s.t.map(([name, rows, rolls]) => ({ name, rows, rolls: rolls || [], locked: false }));
    // Each way in (secretShops' own, then x: more, like Aeira's coupon tab) locks its own tabs.
    for (const g of ss ? [ss, ...(ss.x || [])] : []) {
      const names = g.tabs.map(t => SECRET_TAB_NAMES[t] || t), mine = [];
      for (const nm of names) {
        // Liked players (Stewart, Nele) and club members (Lucas) get a second tab of the same name in
        // place of the first; the script lists it after the normal one, so the last match is the secret one.
        const hit = tabs.filter(t => t.name.replace(/ \(.*\)$/, '') === nm).pop();
        if (hit) { hit.locked = true; mine.push(hit); } else { const t = { name: nm, rows: [], rolls: [], locked: true }; tabs.push(t); mine.push(t); }
      }
      // Secret stock its own tabs don't list yet goes into this way's first tab (Stewart's Talismans
      // sell a scroll his other tabs have too).
      const listed = new Set(mine.flatMap(t => t.rows.map(r => r[0])));
      const extra = g.i.filter(r => !listed.has(r[0]));
      if (extra.length) (mine[0] || tabs[tabs.push({ name: 'Secret', rows: [], rolls: [], locked: true }) - 1]).rows.push(...extra);
    }
    // An item is secret only when no normal tab sells it too: a liked player's tab repeats the usual
    // stock with a few extras, and only the extras are secret. A pattern or score scroll is one item id
    // for many designs (a tab lists it several times, one price each), so those go by id and price;
    // anything else by id alone (a club member's 20%-off dish is still the usual dish).
    const variant = new Set(tabs.flatMap(t => t.rows.filter((r, i) => t.rows.some((x, j) => j !== i && x[0] === r[0] && x[2] === r[2] && x[1] !== r[1])).map(r => r[0])));
    const key = (it, p) => (variant.has(it) ? it + '/' + p : String(it));
    const normal = new Set(tabs.filter(t => !t.locked).flatMap(t => [...t.rows.map(r => key(r[0], r[1])), ...t.rolls.flatMap(r => r[0].map(String))]));
    const isSecret = (t, it, p) => t.locked && !normal.has(key(it, p));
    const SECRET_TAG = { text: '', tag: 'Secret' };
    const out = [{ kind: 'tabs', style: 'line', label: 'Shop tabs', tabs: tabs.filter(t => t.rows.length || t.rolls.length).map(t => {
      const blocks = [];
      // "When" only for a tab with stock that comes and goes (an empty column isn't drawn).
      const timed = t.rows.some(r => r[3]);
      if (t.rows.length) blocks.push({ kind: 'table', columns: ['Item', 'Price', 'Amount', 'When'], rows: t.rows.map(([it, p, n, c, x]) => [
        { lines: [[...shopItemBits(it, x), ...(isSecret(t, it, p) ? [SECRET_TAG] : [])]], sep: '' },
        x && x.jp ? `${x.jp.toLocaleString('en-US')} jousting points` : gold(p), n > 1 ? '×' + n.toLocaleString('en-US') : '1',
        !timed ? '' : c ? cap(conds[c] || 'sometimes') : 'Always']) });
      for (const [items, lo, hi, n] of t.rolls) blocks.push({ kind: 'list', title: 'Random daily stock', items: [
        { lines: [[`Each Erinn day, ${n > 1 ? n + ' of' : 'one of'} `, ...items.flatMap((it, i) => (i ? [', ', itemRef(it)] : [itemRef(it)])), `, with a random enchant, at ${lo === hi ? lo : lo + '–' + hi}% of its price.`,
          ...(items.every(it => isSecret(t, it)) ? [SECRET_TAG] : [])]], sep: '' }] });
      // The count on the tab: its rows, plus each random-stock item it doesn't list as a row.
      const rollOnly = new Set(t.rolls.flatMap(r => r[0]).filter(it => !t.rows.some(x => x[0] === it)));
      return { label: t.name, count: t.rows.length + rollOnly.size, locked: t.locked, blocks };
    }) }];
    for (const how of secretShopHow(id)) out.push({ kind: 'note', text: how });
    const notes = ['Prices are for the amount shown: a stack\'s price is for the whole stack, so buying fewer costs less. The shop restocks every Erinn day.'];
    if (s.d) notes.push(`Humans who support this NPC's race get ${s.d}% off here.`);
    out.push({ kind: 'list', items: notes });
    if (!out[0].tabs.length) out.shift();
    return out;
  }
  // A shop row's item, with what a generic one really is (x, from the shop's script): a manual or
  // pattern "for" what it makes, a quest scroll's quest, a score's tune, an enchanted item's enchants,
  // a set quality. Without these a Blacksmithing tab is forty rows of "Blacksmith Manual".
  function shopItemBits(it, x) {
    // A manual or pattern links to its own form's page (what that one takes), then the item it makes.
    const form = x && x.mf && recipes().byForm.get(String(x.mf));
    const out = [form ? formLink(form) : itemRef(it)];
    if (!x) return out;
    // (A form's name already says what it makes: "Blacksmith Manual - Dagger".)
    if (!form && x.m && items().get(String(x.m))) out.push(' for ', itemRef(x.m));
    else if (x.mp) out.push(': ' + String(x.mp).replace(/^[^-]*-\s*/, ''));
    if (x.q) { const q = questById(x.q); out.push(': ', q ? { t: 'quest', id: q.id, label: q.name } : x.qn || 'a quest'); }
    if (x.sn) out.push(': ' + x.sn);
    const en = [x.pf, x.sf].map(e => e && enchantById(e)).filter(Boolean);
    if (en.length) out.push(', with ', ...en.flatMap((e, i) => (i ? [' and ', { t: 'enchant', id: e.id, label: e.name }] : [{ t: 'enchant', id: e.id, label: e.name }])));
    if (x.ql) out.push(` (quality score ${x.ql})`);
    return out;
  }
  // How an NPC's secret tabs open, in a player's words: one note per way in ([] when they have none),
  // for under their shop. A secret tab that shares its name with a normal one replaces it (the liked
  // player's copy).
  function secretShopHow(id) {
    const s = ((window.GAME_SOURCES || {}).secretShops || {})[id];
    return s ? [s, ...(s.x || [])].map((g, i) => secretGateHow(id, g, i ? 'Also: ' : 'Secret shop (tabs with a lock): ')) : [];
  }
  function secretGateHow(id, g, head) {
    const GS = window.GAME_SOURCES || {};
    const [kind, detail] = g.u, names = g.tabs.map(t => SECRET_TAB_NAMES[t] || t);
    const tabs = names.length > 1 ? names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1] : names[0] || 'secret';
    const shop = ((GS.shops || {})[id] || { t: [] }).t.map(t => t[0].replace(/ \(.*\)$/, ''));
    const twin = names.length === 1 && shop.filter(t => t === names[0]).length > 1;
    return kind === 'affection' ? `${head}${twin ? `a fuller ${tabs} tab replaces the usual one` : `the ${tabs} tab appears`} once they know you well and like you very much (a while past "likes you a lot"). Wait a minute or two after talking, then press Shop straight away instead of Talk: a greeting tires them, and a tired NPC keeps it closed.`
      : kind === 'item' ? { lines: [[`${head}the ${tabs} tab appears while you carry `, itemRef(detail), '. Opening the shop uses it up, so one coupon gives one visit.']], sep: '' }
      : kind === 'keyword' ? `${head}finishing their secret part-time job with the best result unlocks the ${tabs} tab for your next visit${detail ? ' (on Mondays and Tuesdays)' : ''}. It holds one random enchanted item a day, priced well above its usual price.`
      : kind === 'quest' ? (questById(detail) ? { lines: [[`${head}the ${tabs} tab appears while you're on `, { t: 'quest', id: String(detail), label: questById(detail).name }, ', with what that quest needs.']], sep: '' }
        : `${head}the ${tabs} tab appears while you're on the quest that needs what it sells.`)
      : kind === 'membership' ? `${head}members of their club get their own ${tabs} tabs (better drinks, 20% off food, and suspicious goods that change by day).`
      : kind === 'skill' ? { lines: [[`${head}the ${tabs} tab appears for anyone who can use `, skillById(detail) ? skillLink(detail, skillName(detail)) : 'a special skill', '.']], sep: '' }
      : `${head}the ${tabs} tab appears under a special condition.`;
  }
  // Repairs, for an NPC's info box (GAME_SOURCES.repair, as repairSection reads it): what they repair
  // and their success rate (a point's price depends on the item; its page has the Repair table).
  // The words for each filter's last tag; age-limited clothing (agelimit_cloth, _robe...) is one phrase.
  const REPAIR_WORDS = { smith_repairable: ['weapons', 'armor', 'shields'], magic_school_repairable: ['wands', 'staves'],
    misc_repairable: ['instruments', 'glasses', 'tools'], magicsmith_repairable: ['some armor', 'some weapons'], cloth: ['clothes'],
    glove: ['gloves'], bracelet: ['bracelets'], shoes: ['shoes'], headgear: ['hats'], headband: ['hats'], robe: ['robes'], bag: ['bags'],
    accessary: ['accessories'], inverse_transmutator: ['cylinders'], cylinder_turret: ['cylinders'] };
  function npcRepairRows(id) {
    const R = (window.GAME_SOURCES || {}).repair, n = R && (R.npcs || {})[id];
    if (!n) return [];
    const what = n.f.includes('*') ? ['anything'] : [...new Set(n.f.flatMap(patternAlts).flatMap(a => {
      const t = (a.split(/[/*]+/).filter(Boolean).pop() || '').toLowerCase();
      return REPAIR_WORDS[t] || [t.startsWith('agelimit_') ? 'age-limited clothing' : tagWord(t).toLowerCase()];
    }))];
    const said = cap(what.length > 1 ? what.slice(0, -1).join(', ') + ' and ' + what[what.length - 1] : what[0]);
    // Jenna repairs for free (price factor 0); any other factor is noted the same way.
    const p = n.p ?? 1, cost = !p ? ' (free)' : p !== 1 ? ` (${p}× the usual price)` : '';
    return [['Repairs', n.m ? said + ' (only inside a Shadow Mission)' : said], ['Repair success', n.r + '%' + cost]];
  }
  // Items handed in to NPCs for rewards: GAME_SOURCES.turnins = [{k, npc, title, how, tips, cond, repeat, i: [[give
  // [[item, count]], get [[kind (item | gold | exp | text), value, count]], chance, note]]}].
  const turnins = () => (window.GAME_SOURCES || {}).turnins || [];
  const tiGive = give => ({ lines: give.map(([it, n]) => [itemRef(it, n > 1 ? n : undefined)]), sep: '' });
  // Gold and EXP rewards get their unit unless the text already names it ("... exploration EXP").
  const withUnit = (v, unit) => (new RegExp(`\\b${unit}\\b`, 'i').test(v) ? v : `${v} ${unit}`);
  // "Enchant Scroll: Wyvern" links its enchant; "Title: the Wine Hero or ..." each title (a listed one).
  const scrollLine = v => {
    const m = /^Enchant Scroll: (.+)$/.exec(v), e = m && enchantNamed(m[1], 'prefix');
    if (e) return ['Enchant Scroll: ', { t: 'enchant', id: e.id, label: m[1] }];
    const tm = /^Title: (.+)$/.exec(v);
    if (!tm) return [v];
    return ['Title: ', ...tm[1].split(' or ').flatMap((name, i) => {
      const t = titles().find(x => x.name === name);
      return [...(i ? [' or '] : []), t ? { t: 'title', id: t.id, label: name } : name];
    })];
  };
  const tiGet = get => ({ lines: get.map(([k, v, n]) => (k === 'item' ? [itemRef(v, +n > 1 ? +n : undefined)] : k === 'gold' ? [withUnit(v, 'gold')] : k === 'exp' ? [withUnit(v, 'EXP')] : scrollLine(v))), sep: '' });
  const tiNpc = x => { const n = npcById(x.npc); return n ? { t: 'npc', id: n.id, label: n.name } : x.npc || 'An NPC'; };
  const tiLink = x => ({ t: 'tradein', id: x.k, label: x.title });
  const tiTown = x => { const n = npcById(x.npc); return (n && n.town) || 'Elsewhere'; };
  // Text with what it names linked: quests, titles and skills (condLinks), then items, NPCs and monsters
  // (noteLinks), as one line of words and links.
  const richLine = s => ({ lines: [condLinks(s).flatMap(p => (typeof p === 'string' ? [].concat(noteLinks(p)) : [p]))], sep: '' });
  // How the items handed in are earned (x.earn, an event like Threat in Renes): a line on what it is, how
  // to get in, the tiers as a table (party level -> points), and the rules of a run.
  function earnBlocks(x) {
    const e = x.earn;
    if (!e) return [];
    const t = e.table;
    return [...(e.intro ? [{ kind: 'text', parts: [richLine(e.intro)] }] : []),
      ...((e.enter || []).length ? [{ kind: 'list', title: 'How to get in', items: e.enter.map(richLine) }] : []),
      ...(t && (t.rows || []).length ? [{ kind: 'table', lead: true, title: t.title || '', columns: t.columns, rows: t.rows }] : []),
      ...((e.rules || []).length ? [{ kind: 'list', title: 'During a run', items: e.rules.map(richLine) }] : [])];
  }
  // A trade-in's own page: who takes what, how, and what you get; then how its items are earned.
  function tradeinPage(id) {
    const x = turnins().find(t => t.k === id);
    if (!x) return null;
    const earn = earnBlocks(x);
    return { title: x.title, subtitle: null, icon: null, category: [slug(tiTown(x))], kind: 'Trade-in', noMeta: true,
      infobox: [['NPC', tiNpc(x)], ['Town', tiTown(x)], ['Items', x.i.length]], summary: '',
      sections: [{ heading: 'How it works', blocks: turninBlocks(x) }, ...(earn.length ? [{ heading: `Earning it: ${x.earn.title}`, blocks: earn }] : [])] };
  }
  // A "how" text's sentences (also split at "; "), the formulas among them ("EXP = 20 × ...") apart.
  function splitHow(text) {
    // Two formulas in one sentence ("Gold = ... and exploration EXP = ...") are two.
    const parts = String(text || '').split(/(?<=;)\s+|(?<=\.)\s+(?=[A-Z(])|\s+and\s+(?=[^=.;]{2,40}?\s=\s)/)
      .map(s => s.replace(/;$/, '.').trim()).filter(Boolean).map(s => s[0].toUpperCase() + s.slice(1));
    const isFormula = s => /^[^=.]{2,40}?\s=\s/.test(s);
    // A per-item list ("Dish weights: Fruit Juice 10, Fried Egg 12, ...") becomes a column: {'Dish weight': {name: 10}}.
    const perItem = {}, isList = s => {
      const m = /^([A-Z][a-z]+?(?: [a-z]+?)*?)s?: ((?:[^,.]+? [\d.,]+(?:, |\.?$))+)$/.exec(s);
      if (m) perItem[m[1]] = { text: s, of: Object.fromEntries(m[2].replace(/\.$/, '').split(', ').map(p => [p.replace(/ [\d.,]+$/, ''), p.match(/[\d.,]+$/)[0]])) };
      return !!m;
    };
    const steps = parts.filter(s => !isFormula(s) && !isList(s));
    return { steps, formulas: parts.filter(isFormula).map(s => s.replace(/\.$/, '')), perItem };
  }
  // One exchange as blocks: how to do it (its formulas as cards), what to bring, then a table of what
  // you get: a tier first when there are tiers, the notes quieter than the values.
  function turninBlocks(x) {
    // What's handed in, when it's the same on every row, is said once above the table, not repeated.
    // An exchange that takes nothing (stars from a raft ride are points on the character) has no Bring.
    const noGive = x.i.every(r => !r[0].length);
    const gives = new Set(x.i.map(r => JSON.stringify(r[0]))), oneGive = !noGive && gives.size === 1 && x.i.length > 1;
    const how = splitHow(x.how);
    // Tiers for several items (Glewyas's dishes): the item leads, said once over its tiers, with its
    // per-item values from How or a tip ("Dish weights: ...") beside it; a list that doesn't fit the items
    // stays where it was.
    const grouped = !noGive && !oneGive && x.i.some(r => r[4]);
    const giveName = give => (items().get(String(give[0][0])) || {}).name;
    const tipLists = (x.tips || []).map(t => [t, splitHow(t).perItem]);
    const lists = Object.entries(Object.assign({}, how.perItem, ...tipLists.map(([, p]) => p))).filter(([, l]) => grouped && x.i.every(r => giveName(r[0]) in l.of));
    const used = l => lists.some(([, u]) => u === l);
    const steps = [...how.steps, ...Object.values(how.perItem).filter(l => !used(l)).map(l => l.text)].join(' ');
    // (No How often row: a limit worth knowing goes in Good to know, below.)
    const rows = [['How', steps], ...(oneGive ? [['Bring', tiGive(x.i[0][0])]] : []), ['Needs', x.cond]].filter(r => r[1]);
    // A tip that's a formula ("Liking = ...") is a card with the others.
    const isFormula = s => /^[^=.]{2,40}?\s=\s/.test(s);
    const tips = tipLists.filter(([t, p]) => !isFormula(t) && !Object.values(p).some(used)).map(([t]) => t);
    // A real limit on how often ("Once per contest", "Once a day") leads Good to know; "Every time." says nothing.
    if (x.repeat && !/^every time\.?$/i.test(x.repeat.trim())) tips.unshift(x.repeat);
    how.formulas.push(...(x.tips || []).filter(isFormula).map(s => s.replace(/\.$/, '')));
    const cards = how.formulas.length ? [{ kind: 'formulas', items: how.formulas.map(s => { const m = /^(.+?)\s=\s([\s\S]+)$/.exec(s); return { name: m[1], expr: m[2] }; }) }] : [];
    // A chance the same on every row ("appraisal value + 11%") is the exchange's rule, said once in
    // How: no column repeating it. Tiered rows (r[4], e.g. a quality score band) lead with their tier.
    const chances = new Set(x.i.map(r => r[2] || ''));
    const extra = { when: x.i.some(r => r[4]), give: !noGive && !oneGive, chance: x.i.some(r => r[2]) && chances.size > 1, note: x.i.some(r => r[3]) };
    const giveCells = (give, i) => (i && JSON.stringify(give) === JSON.stringify(x.i[i - 1][0]) ? ['', ...lists.map(() => '')]
      : [tiGive(give), ...lists.map(([, l]) => l.of[giveName(give)])]);
    const giveCols = ['Bring', ...lists.map(([label]) => label)];
    const head = [...(rows.length ? [{ kind: 'kv', stack: true, rows }] : []), ...cards], foot = tips.length ? [{ kind: 'list', title: 'Good to know', items: tips }] : [];
    // Numbered choices (the raft and balloon star rewards: notes "Choice 3" and "Choice 3, extra chance"):
    // one row per choice with its whole bundle, the sure item first and each extra chance after it with
    // its chance; the tier (the star band) once over its choices, a line between tiers.
    // What every choice gets as well (the Chief Mate title, "On top of your choice, from 20 stars") is
    // an "Also" row of its tier, its condition muted after it.
    const CHOICE = /^Choice (\d+)(, extra chance)?$/, ALSO = /^On top of your choice(?:, (.+))?$/;
    if (x.i.length && x.i.every(r => CHOICE.test(r[3] || '') || ALSO.test(r[3] || '')) && x.i.some(r => CHOICE.test(r[3]))) {
      const byKey = new Map();
      for (const [, get, chance, note, when] of x.i) {
        const c = CHOICE.exec(note), a = !c && ALSO.exec(note), n = c ? c[1] : 'Also', k = (when || '') + '|' + n;
        if (!byKey.has(k)) byKey.set(k, { when: when || '', n, lines: [] });
        const tail = c && c[2] ? ` · ${chance || 'a'} chance` : a && a[1] ? ` · ${a[1]}` : '';
        byKey.get(k).lines.push(...tiGet(get).lines.map(l => (tail ? l.concat([{ text: tail, tone: 'muted' }]) : l)));
      }
      const choices = [...byKey.values()], hasWhen = choices.some(c => c.when);
      return [...head, { kind: 'table', lead: hasWhen, grouped: hasWhen, columns: [...(hasWhen ? ['When'] : []), 'Choice', 'You get'],
        rows: choices.map((c, i) => [...(hasWhen ? [i && c.when === choices[i - 1].when ? '' : c.when] : []), c.n, { lines: c.lines, sep: '' }]) }, ...foot];
    }
    // Every value under its label, the same way on every trade-in.
    return [...head,
      { kind: 'table', lead: extra.when, grouped, columns: [...(grouped ? giveCols : []), ...(extra.when ? ['When'] : []), ...(extra.give && !grouped ? ['Bring'] : []), 'You get', ...(extra.chance ? ['Chance of this result'] : []), ...(extra.note ? ['Note'] : [])],
        rows: x.i.map(([give, get, chance, note, when], i) => [...(grouped ? giveCells(give, i) : []), ...(extra.when ? [when || '—'] : []), ...(extra.give && !grouped ? [tiGive(give)] : []), tiGet(get),
          ...(extra.chance ? [chance || '—'] : []), ...(extra.note ? [note ? { text: note, tone: 'muted' } : '—'] : [])]) },
      ...foot];
  }
  // An NPC's page: what they take in, each exchange under its name.
  function npcTurninSection(id) {
    const list = turnins().filter(x => npcById(x.npc) && npcById(x.npc).id === id);
    if (!list.length) return null;
    const blocks = list.length === 1 ? turninBlocks(list[0]).map((b, i) => (i === 0 && list[0].title ? Object.assign({ title: list[0].title }, b) : b))
      // The first (the main one, e.g. Voight's prices) open; the rest folded.
      : list.map((x, i) => ({ kind: 'details', summary: `${x.title} (${x.i.length})`, open: i === 0, blocks: turninBlocks(x) }));
    return { heading: 'Trade-ins', blocks };
  }
  // An item's page: who takes it in (and for what), and the turn-ins that give it.
  function itemTurninSection(id) {
    const k = String(id), take = [], give = [];
    for (const x of turnins()) for (const r of x.i) {
      if (r[0].some(g => g[0] === k)) take.push([x, r]);
      if (r[1].some(g => g[0] === 'item' && g[1] === k)) give.push([x, r]);
    }
    if (!take.length && !give.length) return null;
    const blocks = [];
    if (take.length) blocks.push({ kind: 'table', title: 'Turn it in', columns: ['To', 'Bring', 'You get'], rows: take.map(([x, r]) => [{ lines: [[tiNpc(x)], [tiLink(x)]], sep: '' }, tiGive(r[0]), tiGet(r[1])]) });
    if (give.length) blocks.push({ kind: 'table', title: 'Get it by turning in', columns: ['To', 'Bring', 'You get'], rows: give.map(([x, r]) => [{ lines: [[tiNpc(x)], [tiLink(x)]], sep: '' }, tiGive(r[0]), tiGet(r[1])]) });
    return { heading: 'Trade-ins', blocks };
  }
  function npcPage(id) {
    const n = npcById(id);
    if (!n) return null;
    const r = n.r || {}, info = [];
    if (n.town) info.push(['Town', n.town]);
    const stands = [...(regionRefs().npc.get(n.id) || [])].map(regionLink).filter(x => typeof x === 'object');
    if (stands.length) info.push(['Region', stands.length === 1 ? stands[0] : stands]);
    if (String(r.Music || '').trim()) info.push(['Theme song', r.Music.trim()]);
    info.push(['Internal name', n.gn]);
    const sections = [];
    // The race's model; not the bare human/elf/giant bodies ("male_underwear_bss") some NPC races use.
    const mesh = n.race ? String(n.race.DefaultMesh || '').trim() : '';
    if (!NO3D && mesh && !/underwear/i.test(mesh)) sections.push({ heading: '3D model', blocks: [{ kind: 'embed', name: 'race3d', args: { id: String(n.race.ID) }, caption: MODEL_CAPTION }] });
    // Order: what they sell, gifts and friendship, then what they upgrade, then quests. Upgrades and
    // quests can run long, so they're folded.
    const shb = npcShopBlocks(n.id);
    if (shb.length) sections.push({ heading: 'Shop', blocks: shb });
    // Gifts, folded under the same name on every NPC when there's a list (a one-line answer stays open):
    // the gifts, then what's particular to this NPC's friendship, and a quiet line to the friendship guide
    // at the foot. One who takes no gifts gets a Friendship section only for what's particular to them.
    const gb = npcGiftBlocks(n.id), gw = (((window.GAME_SOURCES || {}).gifts || {})[n.id] || {}).w;
    const fl = npcFriendLines(n.id), friend = [...(fl.length ? [{ kind: 'list', title: 'Friendship', items: fl }] : []), FRIEND_GUIDE];
    if (gb.length) sections.push({ heading: 'Gifts', blocks: gw ? [{ kind: 'details', summary: 'Gifts they like and dislike', blocks: gb.concat(friend) }] : gb.concat(friend) });
    else if (fl.length) sections.push({ heading: 'Friendship', blocks: friend });
    const ti = npcTurninSection(n.id);
    if (ti) sections.push(ti);
    const offers = upgradesByNpc().get(n.id) || [];
    if (offers.length) {
      const groups = new Map();
      for (const u of offers) { const c = upgradeCat(u); if (!groups.has(c)) groups.set(c, []); groups.get(c).push(u); }
      const order = ITEM_CATS.map(c => c[0]).concat(['none']);
      const blocks = [...groups].sort((a, b) => order.indexOf(a[0]) - order.indexOf(b[0])).map(([c, list]) => ({
        kind: 'table', title: `${c === 'none' ? 'No matching items' : itemCatPath(c)} (${list.length})`, columns: ['Upgrade', 'Effect', 'Proficiency', 'Gold', 'Steps'],
        rows: list.map(u => [{ t: 'upgrade', id: u.id, label: u.name }, fxLines(u.fx), num(u.r.need_ep), num(u.r.need_gold), u.steps || '—']),
      }));
      blocks.push({ kind: 'note', text: UPG_STEPS_NOTE });
      sections.push({ heading: 'Item upgrades offered', blocks: [{ kind: 'details', summary: `${offers.length.toLocaleString('en-US')} upgrade${offers.length === 1 ? '' : 's'}`, blocks }] });
    }
    const qr = questRefs(), gives = qr.given.get(n.id), part = qr.talk.get(n.id);
    const qb = [];
    if (gives && gives.size) qb.push({ kind: 'details', summary: `Quests given (${gives.size.toLocaleString('en-US')})`, blocks: [questTable(gives, '')] });
    const others = part ? [...part].filter(q => !(gives && gives.has(q))) : [];
    if (others.length) qb.push({ kind: 'details', summary: `In other quests (${others.length.toLocaleString('en-US')})`, blocks: [questTable(others, '')] });
    if (qb.length) sections.push({ heading: 'Quests', blocks: qb });
    const rel = [];
    const tl = friendTitles(n.name);
    if (tl.length) rel.push({ kind: 'links', title: 'Titles', items: tl.map(t => ({ t: 'title', id: t.id, label: t.name })) });
    if (n.race && monsterById(n.race.ID)) rel.push({ kind: 'links', title: 'Race', items: [{ t: 'monster', id: n.race.ID, label: monsterById(n.race.ID).name }] });
    const same = npcs().filter(o => o !== n && o.name === n.name);
    if (same.length) rel.push({ kind: 'links', title: 'Other NPCs with this name', items: same.map(o => ({ t: 'npc', id: o.id, label: `${o.name} (${o.gn})`, icon: npcFace(o.id) })) });
    if (rel.length) sections.push({ heading: 'Related', blocks: rel });
    if (n.internal) info.push(['Name', 'From its internal name (the game has no English one)']);
    // Then their repairs, for the NPCs who repair (what, how well, and a point's price).
    info.push(...npcRepairRows(n.id));
    // Last in the box: whether they keep a secret shop, for the NPCs whose shop is known.
    const GS = window.GAME_SOURCES || {}, hasSecret = !!(GS.secretShops || {})[n.id];
    if (hasSecret || (GS.shops || {})[n.id]) info.push(['Secret shop', hasSecret ? 'Yes' : 'No']);
    // capTables: every table here scrolls in a short box (shops and quest lists run long).
    return { title: n.name, subtitle: null, icon: npcFace(n.id), category: n.cats, infobox: info, summary: '', sections, capTables: true };
  }

  // ---------- quests ----------
  // The game's files have no quests; sources.js ships them (GAME_SOURCES.quests): {id: {n: name |
  // nj: the name in its original text (no English one), c: category, g: generation, s: steps
  // [[kind, {npc, item, race, skill, count, ...; x: the step in words where ids can't say it; m:
  // region}]], r: rewards [{kind, value | item + count (+ prefix, suffix) | skill | quest; choice:
  // reward group, result: good/poor (part-time jobs)}], gv: who gives it [{npc, owl, event, when,
  // shop, tab, ptj, board, cost, quest, fishing, item, req}], rq: requirements, rp: repeatable,
  // sc: its scroll item, pr: price, nq: can't be given up, pj: part-time job {job, starttime,
  // reporttime, endtime, grade}, ch: part-time job reward choices {range, always}}.
  const QUEST_CATS = [['main', 'Main stream', ''], ['normal', 'Normal', ''], ['level', 'Level-up', ''], ['party', 'Party', ''],
    ['hunting', 'Fomor Scroll hunting', ''], ['skill', 'Skill', ''], ['ministory', 'Mini-stories', ''], ['guild', 'Guild', ''],
    ['escort', 'Escort', ''], ['event', 'Event', ''], ['exploration', 'Iria exploration', ''], ['ptj', 'Part-time jobs', ''],
    ['ptj_reward', 'Part-time job bonuses', '']];
  const QUEST_CAT_TITLE = Object.fromEntries(QUEST_CATS.map(c => [c[0], c[1]]));
  const questData = () => (window.GAME_SOURCES && window.GAME_SOURCES.quests) || {};
  let QUESTS = null;
  function quests() {
    if (QUESTS) return QUESTS;
    QUESTS = new Map();
    for (const [id, q] of Object.entries(questData())) QUESTS.set(id, Object.assign({ id, name: q.n || questFallbackName(q) || q.nj || 'Quest ' + id }, q));
    return QUESTS;
  }
  const questById = id => quests().get(String(id));
  const questLink = id => { const q = questById(id); return q ? { t: 'quest', id: q.id, label: q.name } : 'Quest ' + id; };
  // A quest with no English name: a one-step hunting or collecting quest is named by its step.
  function questFallbackName(q) {
    const s = q.s || [];
    if (s.length !== 1) return '';
    const [k, d] = s[0];
    const n = d.count > 1 ? ' ×' + d.count : '';
    if (k === 'kill' && monsterById(d.race)) return `Defeat ${monsterById(d.race).name}${n}`;
    if ((k === 'collect' || k === 'gather') && items().get(String(d.item))) return `Collect ${items().get(String(d.item)).name}${n}`;
    return '';
  }
  const regionName = id => {
    if (!REGION_NAMES) REGION_NAMES = new Map(rows('db/minimapinfo.xml', 'MinimapInfo').map(r => [String(r.RegionID), tr(r.MapLocalName).trim()]));
    return REGION_NAMES.get(String(id)) || '';
  };
  const raceLink = id => { const m = monsterById(id); if (m) return { t: 'monster', id: m.id, label: m.name }; const r = raceRows().get(String(id)); return r ? raceName(tr(r.LocalName)) || 'a monster' : 'a monster'; };
  const countOf = n => (n > 1 ? ` ×${n.toLocaleString('en-US')}` : '');
  const itemRef = (id, n) => { const it = items().get(String(id)); return it ? itemLink(id, it.name + countOf(n)) : `item ${id}${countOf(n)}`; };
  const skillRef = id => { const s = skillById(id); return s ? skillLink(s.id, s.name) : 'a skill'; };
  // A step as a line of text and links.
  function questStep([k, d]) {
    const npc = d.npc ? npcRef(d.npc) : 'an NPC', it = d.item ? itemRef(d.item, d.count) : null;
    switch (k) {
      case 'talk': return ['Talk to ', npc];
      case 'kill': return d.anyMonster ? [`Defeat any monster${countOf(d.count)}`] : ['Defeat ', raceLink(d.race), countOf(d.count)];
      case 'collect': return it ? ['Collect ', it] : [d.x || 'Collect items'];
      case 'gather': return it ? ['Gather ', it] : [d.x || 'Gather items'];
      case 'fish': return it ? ['Catch ', it] : [d.x || 'Fish'];
      case 'give': return it ? ['Give ', it, ' to ', npc, d.minSize ? ` (${d.minSize} cm or bigger)` : '', d.quality ? ` (quality score ${d.quality} or better)` : ''] : ['Give an item to ', npc];
      case 'receive': return it ? ['Receive ', it, ' from ', npc] : ['Receive an item from ', npc];
      case 'craft': return [it ? 'Make ' : 'Make an item', it || '', d.skill ? ' (' : '', d.skill ? skillRef(d.skill) : '', d.skill ? ')' : ''];
      case 'learn': return ['Learn ', d.skill ? skillRef(d.skill) : 'a skill', d.rank ? ` (rank ${d.rank} or higher)` : ''];
      case 'exchange': return ['Trade ', it || 'an item', ' with ', npc, ' for ', d.get === 2000 ? gold(num(d.getCount)) : d.get ? itemRef(d.get, d.getCount) : 'a reward'];
      default: return [d.x || (d.xj ? 'A story step' : cap(words(k)))];
    }
  }
  // A reward as a line.
  function questReward(r) {
    const n = num(r.value);
    switch (r.kind) {
      case 'exp': return [`${n.toLocaleString('en-US')} exp`];
      case 'gold': return [gold(n)];
      case 'ap': return [`${n} AP`];
      case 'exploration_exp': return [`${n.toLocaleString('en-US')} exploration exp`];
      case 'royal_alchemist_points': return [`${n} Royal Alchemist points`];
      case 'item': {
        const out = [itemRef(r.item, r.count)];
        for (const slot of ['prefix', 'suffix']) if (r[slot]) { const e = enchantById(r[slot]); out.push(e ? ', ' : '', e ? { t: 'enchant', id: e.id, label: e.name } : '', e ? ` (${slot})` : ''); }
        if (r.onStart) out.push(' (when the quest starts)');
        return out;
      }
      case 'skill': return ['Skill: ', skillRef(r.skill), r.rank ? ` (rank ${r.rank})` : ''];
      case 'quest': return ['Next quest: ', questLink(r.quest)];
      case 'stat': return [String(r.stat || 'A stat')];
      default: return [cap(words(r.kind))];
    }
  }
  const lineCell = parts => ({ lines: [parts.filter(x => x !== '')], sep: '' });
  const RESULTS = { undefined: 'Perfect', good: 'Good', poor: 'Poor' };
  // Who gives it, as a line.
  function questGiver(g) {
    const out = [];
    if (g.npc) out.push(npcRef(g.npc));
    if (g.shop) out.push(g.npc ? ' sells its scroll' : 'Sold as a scroll', g.tab ? ` (${g.tab} tab)` : '');
    else if (g.ptj) out.push(' (part-time job)');
    else if (g.board) out.push('Iria quest board', g.cost ? `, ${gold(num(g.cost))}` : '');
    else if (g.owl) out.push(g.npc ? ' (by owl)' : 'By owl', g.when ? `, on ${g.when}` : '');
    else if (g.quest) out.push('After ', questLink(g.quest));
    else if (g.fishing) out.push('Fished up');
    else if (g.item) out.push('Reading ', itemRef(g.item));
    else if (g.escort) out.push(' (escort)');
    else if (!g.npc) out.push(g.when ? cap(g.when) : 'A game event');
    return out;
  }
  // Requirements as lines.
  function questReq(rq) {
    const out = [];
    if (!rq) return out;
    if (rq.level) out.push([rq.levelExact ? `Level ${rq.level} exactly` : `Level ${rq.level} or higher`]);
    if (rq.totalLevel) out.push([`Total level ${rq.totalLevel} or higher`]);
    if (rq.age) out.push([`Age ${rq.age} or older`]);
    if (rq.exploLevel) out.push([`Exploration level ${rq.exploLevel} or higher`]);
    if (rq.race) out.push([[].concat(rq.race).map(cap).join(' or ') + ' only']);
    if (rq.gender) out.push([cap(rq.gender) + ' characters only']);
    for (const id of [].concat(rq.afterQuest || [])) out.push(['After completing ', questLink(id)]);
    for (const id of [].concat(rq.quests || [])) out.push(['Completed ', questLink(id)]);
    for (const [s, rank] of rq.skillRank || []) out.push([skillRef(s), rank ? ` at rank ${rank} or higher` : '']);
    for (const id of [].concat(rq.hasItem || [])) out.push(['Has ', itemRef(id)]);
    return out;
  }
  // Quests by what they involve, for the other pages' links: npc -> given, race -> kill, item -> asked for.
  let QUEST_REFS = null;
  function questRefs() {
    if (QUEST_REFS) return QUEST_REFS;
    const add = (m, k, id) => { k = String(k); if (!m.has(k)) m.set(k, new Set()); m.get(k).add(id); };
    const given = new Map(), kill = new Map(), need = new Map(), talk = new Map(), next = new Map();
    for (const q of quests().values()) {
      for (const g of q.gv || []) { if (g.npc) add(given, g.npc, q.id); if (g.quest) add(next, g.quest, q.id); }
      for (const id of [].concat((q.rq || {}).afterQuest || [])) add(next, id, q.id);
      for (const [k, d] of q.s || []) {
        if (k === 'kill' && d.race) add(kill, d.race, q.id);
        if (d.item && ['collect', 'give', 'gather', 'fish', 'exchange', 'craft'].includes(k)) add(need, d.item, q.id);
        if (d.npc && (k === 'talk' || k === 'give' || k === 'receive')) add(talk, d.npc, q.id);
      }
      for (const r of q.r || []) if (r.kind === 'quest') add(next, q.id, r.quest);
    }
    QUEST_REFS = { given, kill, need, talk, next };
    return QUEST_REFS;
  }
  // A table of quests, for NPC, monster and item pages.
  const questSum = (q, k) => (q.r || []).filter(r => r.kind === k && r.choice == null && !r.result).reduce((a, r) => a + num(r.value), 0) || null;
  const questTable = (ids, title) => ({ kind: 'table', title, columns: ['Quest', 'Category', 'Exp', 'Gold'],
    rows: [...ids].map(questById).filter(Boolean).sort((a, b) => a.name.localeCompare(b.name) || (questSum(a, 'exp') || 0) - (questSum(b, 'exp') || 0))
      .map(q => [{ t: 'quest', id: q.id, label: q.name }, QUEST_CAT_TITLE[q.c] || 'Other', questSum(q, 'exp'), questSum(q, 'gold') ? gold(questSum(q, 'gold')) : null]) });
  function questPage(id) {
    const q = questById(id);
    if (!q) return null;
    const info = [['Category', QUEST_CAT_TITLE[q.c] || 'Other']];
    if (q.g) info.push(['Story', q.g === 'JG' ? 'Cat Island' : 'Generation ' + q.g.slice(1)]);
    if (q.pj) {
      if (q.pj.job) info.push(['Job', q.pj.job]);
      if (q.pj.starttime != null && q.pj.endtime != null) info.push(['Hours', `${q.pj.starttime}:00 to ${q.pj.endtime}:00 (Erinn time)`]);
      if (q.pj.reporttime) info.push(['Report from', `${q.pj.reporttime}:00`]);
    }
    if (q.sc && items().get(String(q.sc))) info.push(['Quest scroll', itemRef(q.sc)]);
    if (q.pr) info.push(['Scroll price', gold(num(q.pr))]);
    if (q.rp != null) info.push(['Repeatable', q.rp ? 'Yes' : 'No']);
    if (q.nq) info.push(['Can be given up', 'No']);
    const sections = [];
    // Its questline first: where it sits in the story, and what comes before and after.
    const line = questlineBlocks(q.id);
    if (line.length) sections.push({ heading: 'Questline', blocks: line });
    const req = questReq(q.rq);
    if (req.length) sections.push({ heading: 'Requirements', blocks: [{ kind: 'table', columns: ['Requirement'], rows: req.map(l => [lineCell(l)]) }] });
    if ((q.s || []).length) {
      const rows = q.s.map((s, i) => [i + 1, lineCell(questStep(s)), s[1].m && regionName(s[1].m) ? regionName(s[1].m) : '—']);
      sections.push({ heading: 'Clear conditions', blocks: [{ kind: 'table', columns: ['Step', 'What to do', 'Where'], rows }] });
    }
    const rw = q.r || [];
    if (rw.length) {
      const blocks = [];
      if (q.c === 'ptj' || rw.some(r => r.choice != null)) {
        // Part-time jobs: the reward depends on how well the job went; the player picks one of the
        // offered reward groups.
        const rows = rw.map(r => [RESULTS[r.result] || cap(r.result), r.choice != null ? r.choice : '—', lineCell(questReward(r))]);
        blocks.push({ kind: 'table', columns: ['Result', 'Choice', 'Reward'], rows });
        const ch = q.ch || {};
        blocks.push({ kind: 'note', text: `You're offered ${ch.range ? ch.range.replace('~', ' to ') : 'some'} of the choices and take one${(ch.always || []).length ? `; choice ${ch.always.join(', ')} is always offered` : ''}. A better result gives the better rewards.` });
      } else {
        blocks.push({ kind: 'table', columns: ['Reward'], rows: rw.map(r => [lineCell(questReward(r))]) });
      }
      sections.push({ heading: 'Rewards', blocks });
    }
    const gv = q.gv || [];
    if (gv.length) sections.push({ heading: 'Given by', blocks: [{ kind: 'table', columns: ['From', 'Requirements'], rows: gv.map(g => {
      const r = questReq(g.req).filter(l => !req.some(x => JSON.stringify(x) === JSON.stringify(l)));
      return [lineCell(questGiver(g)), r.length ? { lines: r, sep: '' } : '—'];
    }) }] });
    const nx = questRefs().next.get(q.id);
    if (nx && nx.size) sections.push({ heading: 'Related', blocks: [{ kind: 'links', title: 'Leads to', items: [...nx].map(questLink).filter(x => typeof x === 'object') }] });
    return { title: q.name, subtitle: null, icon: null, category: q.c, infobox: info, summary: '', sections };
  }

  // ---------- questlines ----------
  // GAME_SOURCES.questChains = [{id, t: title, g: group, n: generation, s: summary, st: [[quest, name, step,
  // how (text or [sub-steps]), cond (text or [lines]), branch, note, alts, details]]}]: quests in play order, from how the game links them (a quest that needs
  // another done first, one sent when another ends, the story's chapters). Steps sharing a number are
  // a branch (Paladin or Dark Knight, elf or giant): the branch says which.
  const QL_GROUPS = [['main', 'Main stream'], ['transform', 'Transformations'], ['beginner', 'Beginner questlines'], ['side', 'Side stories'],
    ['iria', 'Iria exploration'], ['event', 'Events'], ['other', 'Other']];
  const qlGroup = g => (QL_GROUPS.find(x => x[1] === g) || ['other'])[0];
  const questChains = () => (window.GAME_SOURCES && window.GAME_SOURCES.questChains) || [];
  const qlLink = c => ({ t: 'questline', id: c.id, label: c.t });
  // A quest as a link when the wiki has its page, else its name.
  const qlQuest = (q, name) => (questById(q) ? { t: 'quest', id: String(q), label: name || questById(q).name } : name || 'Quest ' + q);
  function questlinePage(id) {
    const c = questChains().find(x => x.id === id);
    if (!c) return null;
    const info = [['Group', c.g], ['Quests', new Set(c.st.map(s => s[0])).size]];
    if (c.n) info.push(['Generation', c.n]);
    // A step's other versions (for the other gender, an easier copy...) under its quest: "or X (when)".
    const questCell = (q, name, alts) => (alts && alts.length ? { lines: [[qlQuest(q, name)], ...alts.map(([aq, an, when]) => ['or ', qlQuest(aq, an), ` (${when})`])], sep: '' } : qlQuest(q, name));
    // How: one sentence, or short sub-steps in order (numbered); then its details (muted) and any doubt (red).
    // Conditions: one a line.
    const howCell = (how, detail, note) => {
      const steps = Array.isArray(how) ? how : [how], after = [];
      for (const d of detail || []) after.push([{ text: d, tone: 'muted' }]);
      if (note) after.push([{ text: note, tone: 'neg' }]);
      const rest = after.length ? { lines: after, sep: '' } : null;
      if (steps.length > 1) return rest ? { steps, after: rest } : { steps };
      return rest ? { lines: [[steps[0]], ...after], sep: '' } : steps[0];
    };
    const condCell = cond => (Array.isArray(cond) ? (cond.length > 1 ? { lines: cond, sep: '' } : cond[0] || '—') : cond || '—');
    // A line with a path per race (Generation 6: elves and giants) is a table per race, the steps both
    // share in each, numbered again from 1; any other choice stays in a Path column.
    const RACE_PATHS = [['Human', 'Humans'], ['Elf', 'Elves'], ['Giant', 'Giants']];
    const races = RACE_PATHS.filter(([b]) => c.st.some(s => s[5] === b));
    const isRace = b => RACE_PATHS.some(([r]) => r === b);
    const stepTable = (steps, title) => {
      const paths = steps.some(s => s[5] && !isRace(s[5])), order = [...new Set(steps.map(s => s[2]))];
      const rows = steps.map(([q, name, step, how, cond, branch, note, alts, detail]) => [races.length > 1 ? order.indexOf(step) + 1 : step, questCell(q, name, alts), howCell(how, detail, note), condCell(cond)]
        .concat(paths ? [branch && !isRace(branch) ? branch : '—'] : []));
      return [{ kind: 'table', title, columns: ['Step', 'Quest', 'How to get it', 'Conditions'].concat(paths ? ['Path'] : []), rows },
        ...(paths ? [{ kind: 'note', text: 'Steps with the same number are different paths: take the one the Path column says applies to you.' }] : [])];
    };
    const blocks = races.length > 1 ? races.flatMap(([b, title]) => stepTable(c.st.filter(s => s[5] === b || !isRace(s[5])), title)) : stepTable(c.st);
    return { title: c.t, subtitle: null, icon: null, category: [qlGroup(c.g)], kind: 'Questline', infobox: info, summary: c.s, sections: [{ heading: 'Steps', wide: true, blocks }] };
  }
  // A quest page's place in its questlines: the line, its step of how many, the quests before and after.
  function questlineBlocks(id) {
    const out = [];
    for (const c of questChains()) {
      const here = c.st.filter(s => String(s[0]) === String(id));
      if (!here.length) continue;
      const step = here[0][2], last = Math.max(...c.st.map(s => s[2]));
      const at = n => c.st.filter(s => s[2] === n).map(s => qlQuest(s[0], s[1]));
      const rows = [['Questline', qlLink(c)], ['Step', `${step} of ${last}`]];
      if (at(step - 1).length) rows.push(['Before it', at(step - 1)]);
      if (at(step + 1).length) rows.push(['Next', at(step + 1)]);
      out.push({ kind: 'kv', rows });
    }
    return out;
  }

  // ---------- dungeons ----------
  // GAME_SOURCES.dungeons = {p: room types [[kind, what the room does]], d: {key: {n: name, r: [region
  // id, name] (the hall with its altar), pv: private, en: ways in [{a: altar (dungeon key), ru: Iria
  // ruin, i: items to drop on the altar, p: party size tests, t: other conditions}], v: other dungeons
  // its altar sends to, f: floors [{n, s: [w, h], c: critical path, x: sections [{c: [min, max] rooms,
  // p: the room pool [[weight, room type, monsters [[race, count, chance % (-1: depends)]]]]}]}], b:
  // bosses [{f: floor, m: [[race, count]], c: chests}], w: rewards [[item, count, chance % (-1:
  // unknown), where, note, prefix, suffix]]}}}. A section places min to max rooms, each a different
  // pool entry picked at random (the first one a room that locks a door).
  const dungeonData = () => (window.GAME_SOURCES && window.GAME_SOURCES.dungeons) || { p: [], d: {} };
  let DUNGEONS = null;
  function dungeons() {
    if (DUNGEONS) return DUNGEONS;
    const all = Object.entries(dungeonData().d), uses = {};
    for (const [, d] of all) uses[d.n] = (uses[d.n] || 0) + 1;
    // Versions that share a name (17 "Alby Dungeon"s) are told apart by the pass that leads there, else
    // by their internal name; the altar's own dungeon keeps the plain name. A name only in Korean: the
    // internal name in words.
    const keyWords = k => titleCase(words(k.replace(/_dungeon$/i, '')));
    const named = ([k, d]) => {
      if (!english(d.n)) return keyWords(k);
      if (uses[d.n] < 2 || (d.en || []).some(e => e.a === k && !(e.i || []).length)) return d.n;
      const pass = (d.en || []).map(e => (e.i || [])[0]).find(i => i && items().get(String(i)));
      return `${d.n} (${pass ? items().get(String(pass)).name : keyWords(k)})`;
    };
    DUNGEONS = new Map(all.map(e => [e[0], Object.assign({ id: e[0], name: named(e) }, e[1])]));
    return DUNGEONS;
  }
  // Notes name dungeons by their internal names ("not in RP_Ranald_Dunbarton_Rabbie_Dungeon").
  const dungeonNote = s => eventText(String(s).replace(/\b\w+_dungeon\b/gi, m => (dungeonById(m) ? dungeonById(m).name : m)));
  const dungeonById = id => dungeons().get(String(id).toLowerCase());
  const dungeonLink = id => { const d = dungeonById(id); return d ? { t: 'dungeon', id: d.id, label: d.name } : cap(words(String(id))); };
  const dungeonRegion = d => (d.r ? d.r[1] || regionName(d.r[0]) : '');
  const dungeonCat = d => ((d.en || []).some(e => e.ru) ? 'ruin' : (d.en || []).length ? 'dungeon' : 'unknown');
  const DUNGEON_CATS = [['dungeon', 'Dungeons', ''], ['ruin', 'Iria ruins', ''], ['unknown', 'No known way in', '']];
  // Races met in each dungeon (rooms and bosses): race id -> dungeon keys.
  let DUNGEON_RACES = null;
  function dungeonsOfRace(race) {
    if (!DUNGEON_RACES) {
      DUNGEON_RACES = new Map();
      const add = (r, k) => { r = String(r); if (!DUNGEON_RACES.has(r)) DUNGEON_RACES.set(r, new Set()); DUNGEON_RACES.get(r).add(k); };
      for (const d of dungeons().values()) {
        for (const f of d.f || []) for (const s of f.x || []) for (const [, , ms] of s.p) for (const [r] of ms) if (r) add(r, d.id);
        for (const b of d.b || []) for (const [r] of b.m) if (r) add(r, d.id);
      }
    }
    return [...(DUNGEON_RACES.get(String(race)) || [])];
  }
  const monsterLine = ([race, n, ch]) => [raceLink(race), countOf(n), ch >= 0 && ch < 100 ? ` (${pctText(ch)})` : ''];
  // Notes joined with "; " one a line, each capitalized.
  const noteLines = s => { const p = [...new Set(String(s).split(/;\s+/).map(x => x.trim()).filter(Boolean))].map(cap); return p.length > 1 ? { lines: p, sep: '' } : p[0] || '—'; };
  const enchantBits = (prefix, suffix) => [].concat(...[['prefix', prefix], ['suffix', suffix]].filter(x => x[1]).map(([k, id]) => { const e = enchantById(id); return e ? [', ', { t: 'enchant', id: e.id, label: e.name }, ` (${k})`] : []; }));
  function dungeonPage(id) {
    const d = dungeonById(id);
    if (!d) return null;
    const info = [];
    if (dungeonRegion(d)) info.push(['Altar', d.r && regionData()[String(d.r[0])] ? regionLink(d.r[0]) : dungeonRegion(d)]);
    info.push(['Floors', (d.f || []).length]);
    if (d.pv) info.push(['Private', 'Only the party that opens it']);
    const sections = [];
    const en = d.en || [];
    if (en.length) {
      // The altar: this dungeon's own (where its hall is), or another dungeon's, as one link.
      const altarOf = a => { const x = dungeonById(a); return a === d.id ? "This dungeon's altar" : x ? { t: 'dungeon', id: x.id, label: x.name + ' altar' } : cap(words(a)) + ' altar'; };
      const rows = en.map(e => [e.ru ? 'Iria ruin: ' + e.ru : e.a ? altarOf(e.a) : '—',
        (e.i || []).length ? { lines: e.i.map(x => [itemRef(x)]), sep: '' } : 'Any other item',
        (e.p || []).length ? e.p.map(p => 'party ' + p.replace(/^==\s*/, '= ')).join(', ') : 'Any', e.t ? noteLines(eventText(e.t)) : '—']);
      sections.push({ heading: 'How to get in', blocks: [{ kind: 'table', columns: ['Where', 'Item to offer', 'Party size', 'Notes'], rows },
        { kind: 'note', text: 'Drop an item on the altar to enter: some items (dungeon passes) send you to another version of the dungeon.' }] });
    }
    // The other versions this altar sends to, sorted: the usual difficulties in order, then story and
    // quest versions, events, and the rest; each with the item that picks it and the party size.
    const vs = (d.v || []).map(dungeonById).filter(v => v && v.id !== d.id);
    if (vs.length) {
      const DIFF = ['beginner', 'basic|low', 'intermediate|middle', 'advanced|adv\\.|high', 'hard'];
      const DIFF_NAME = ['Beginner', 'Basic', 'Intermediate', 'Advanced', 'Hard mode'];
      const EVENT = /event|white ?day|christmas|halloween|valentine|pc ?bang|pc cafe|premium|golden rat|anniversary|cookie|jealous|single|couple|magic bean|four-leaf|clover|lunar|summer|hong bao|whiteday/i;
      const WORDS = { one: 1, two: 2, three: 3, four: 4, five: 5, six: 6, seven: 7, eight: 8 };
      const groups = { diff: [], story: [], event: [], other: [] };
      for (const v of vs) {
        const ways = (v.en || []).filter(e => e.a === d.id);
        const its = [...new Set([].concat(...ways.map(e => e.i || [])))];
        const names = its.map(i => (items().get(String(i)) || {}).name || '');
        const note = [...new Set(ways.map(e => e.t).filter(Boolean))].join('; ');
        const text = [v.name, v.id.replace(/_/g, ' '), ...names].join(' ');
        // Hard Mode versions say their base difficulty too ("Hard Mode Alby Basic Dungeon").
        const hard = /\bhard ?mode\b/i.test(text);
        const di = DIFF.findIndex(re => new RegExp('\\b(' + re + ')', 'i').test(hard ? text.replace(/\bhard ?mode\b/ig, '') : text));
        // Party size: the altar's test (== 1, <= 3), else the pass's name (for Two, 3-person).
        const tests = [...new Set([].concat(...ways.map(e => e.p || [])))].map(p => p.replace(/^==\s*/, '').replace(/^<=\s*/, 'up to ').replace(/^>=\s*/, 'at least '));
        const named = names.map(n => (/\bfor (one|two|three|four|five|six|seven|eight|\d)\b/i.exec(n) || /\b(\d)-person\b/i.exec(n) || [])[1]).filter(Boolean)
          .map(x => String(WORDS[x.toLowerCase()] || x));
        const party = [...new Set(tests.concat(named))].join(', ') || 'Any';
        // Entered with something that isn't a pass (a quest item: Tarlach's glasses, the Bard's earring)
        // is a story version whatever its internal name says.
        const byPass = names.some(n => /\bpass\b|통행증/i.test(n));
        const kind = EVENT.test(text + ' ' + note) ? 'event' : its.length && !byPass ? 'story' : di >= 0 || hard ? 'diff' : its.length ? 'story' : 'other';
        groups[kind].push({ v, its, party, note, di: di < 0 ? 1 : di, hard, n: +((/\d/.exec(party) || [9])[0]) });
      }
      // The dungeon's own name (the pass is in its own column); a pass with no English name says so.
      const passRef = i => { const it = items().get(String(i)); return it && !english(it.name) ? { t: 'item', id: String(i), label: 'Pass (no English name)', icon: itemIcon(String(i)) } : itemRef(i); };
      const rowOf = (x, diff) => [{ t: 'dungeon', id: x.v.id, label: english(x.v.n) ? x.v.n : x.v.name }].concat(diff ? [DIFF_NAME[x.di] + (x.hard ? ' · Hard mode' : '')] : [],
        [x.its.length ? { lines: x.its.map(i => [passRef(i)]), sep: '' } : '—', x.party, x.note ? noteLines(eventText(x.note)) : '—']);
      const blocks = [];
      if (groups.diff.length) blocks.push({ kind: 'table', title: 'Difficulties', columns: ['Version', 'Difficulty', 'Enter with', 'Party', 'Notes'],
        rows: groups.diff.sort((a, b) => a.hard - b.hard || a.di - b.di || a.n - b.n || a.v.name.localeCompare(b.v.name)).map(x => rowOf(x, true)) });
      for (const [k, title] of [['story', 'Story and quests'], ['event', 'Events'], ['other', 'Other']]) {
        if (groups[k].length) blocks.push({ kind: 'table', title, columns: ['Version', 'Enter with', 'Party', 'Notes'], rows: groups[k].sort((a, b) => a.v.name.localeCompare(b.v.name)).map(x => rowOf(x, false)) });
      }
      sections.push({ heading: 'Versions from this altar', blocks });
    }
    // Each floor's monster groups: a room brings one of them (rooms are picked at random).
    const fl = d.f || [];
    if (fl.length) {
      const rows = fl.map(f => {
        const seen = new Set(), lines = [];
        for (const s of f.x) for (const [, , ms] of s.p) {
          const key = ms.map(m => m[0] + 'x' + m[1]).sort().join(',');
          if (!ms.length || seen.has(key)) continue;
          seen.add(key);
          lines.push([].concat(...ms.map((m, i) => (i ? [', '] : []).concat([raceLink(m[0]), countOf(m[1])]))));
        }
        return [f.n, lines.length ? { lines, sep: '', divide: true } : 'No monster rooms'];
      });
      sections.push({ heading: 'Floors', blocks: [{ kind: 'table', columns: ['Floor', 'Monster groups you may meet'], rows },
        { kind: 'note', text: 'Each monster room brings one of its floor\'s groups, picked at random when the floor is made.' }] });
    }
    if ((d.b || []).length) sections.push({ heading: 'Boss', blocks: [{ kind: 'table', columns: ['Floor', 'Monsters', 'Chests'],
      rows: d.b.map(b => [b.f || '—', { lines: b.m.map(m => monsterLine([m[0], m[1], -1]).concat(m[2] ? [` (${dungeonNote(m[2])})`] : [])), sep: '' }, b.c || '—']) }] });
    if ((d.w || []).length) {
      const rewardCell = ([it, n, , , , pre, suf]) => lineCell([itemRef(it, typeof n === 'number' ? n : 1), typeof n === 'string' && n !== '1' ? ` ×${n.replace('~', '–')}` : '', ...enchantBits(pre, suf)]);
      const otherNotes = note => String(note || '').split(/;\s+/).map(clarifyChest).filter(Boolean);
      // The boss scripts put one picked-at-random reward in one chest every clear (most often an
      // enchant scroll or an enchanted item): rows of that chest whose chances add up to 100% under
      // the same conditions are a sure reward, shown first and on their own.
      const sureGroups = new Map();
      for (const w of d.w) {
        if (!/first chest only/i.test(w[4] || '') || !(w[2] > 0) || w[3] !== 'boss room chest') continue;
        // The enchanted reward is one pick of its own: enchanted rows group apart from the chest's other rolls.
        if (!(w[5] || w[6])) continue;
        const k = otherNotes(w[4]).join('; ');
        if (!sureGroups.has(k)) sureGroups.set(k, []);
        sureGroups.get(k).push(w);
      }
      const sure = new Set(), blocks = [];
      for (const [cond, list] of sureGroups) {
        if (Math.abs(list.reduce((a, w) => a + w[2], 0) - 100) > 0.5) continue;
        list.forEach(w => sure.add(w));
        blocks.push({ kind: 'table', title: 'Every clear gives one of these' + (cond ? ` (${cap(dungeonNote(cond)).replace(/\.$/, '')})` : ''),
          columns: ['Reward', 'Chance'], rows: list.slice().sort((a, b) => b[2] - a[2]).map(w => [rewardCell(w), list.length > 1 ? pctText(w[2]) : 'Always']) });
      }
      const rows = d.w.filter(w => !sure.has(w)).sort((a, b) => b[2] - a[2]).map(w => [rewardCell(w),
        w[2] >= 0 ? pctText(w[2]) : '—', cap(w[3] || '—'), otherNotes(w[4]).length ? { lines: otherNotes(w[4]).map(x => noteLinks(cap(dungeonNote(x)))), sep: '' } : '—']);
      if (blocks.length) blocks.push({ kind: 'note', text: 'One per party: it\'s in one of the boss room\'s chests.' });
      if (rows.length) blocks.push({ kind: 'table', title: blocks.length ? 'Other rewards' : '', columns: ['Item', 'Chance', 'Where', 'Notes'], rows });
      blocks.push({ kind: 'note', text: 'The boss room has a chest for each party member (unless the Boss section says otherwise). Chances are per chest.' });
      // At a glance, first: what every clear gives, then the kinds of items the rest can be.
      const sureItems = [...new Set([...sure].map(w => (items().get(String(w[0])) || {}).name).filter(Boolean))];
      const glance = [];
      const sureText = sureItems.length === 1 ? (/scroll/i.test(sureItems[0]) ? 'An ' : 'An enchanted ') + sureItems[0]
        : 'One enchanted reward: ' + rewardKinds([...sure].map(w => w[0])).join(' or ');
      if (sureItems.length) glance.push('Every clear: ' + sureText.replace(/^./, c => c.toLowerCase()));
      glance.push(...rewardKinds(d.w.filter(w => !sure.has(w)).map(w => w[0])));
      if (glance.length) blocks.unshift({ kind: 'list', title: 'At a glance', items: glance });
      sections.push({ heading: 'Rewards', blocks });
      if (sureItems.length) info.push(['Every clear', sureText]);
    }
    return { title: d.name, subtitle: null, icon: null, category: dungeonCat(d), infobox: info, summary: '', sections };
  }

  // ---------- fishing ----------
  // GAME_SOURCES.fishing = {f: fish sizes {item: [min, max] cm}, g: fishing grounds {key: {n: name, k:
  // spot | event | conditional, w: where [[region, name, spots]], t: when it applies, b: bait it needs,
  // c: catches [[item, chance %, category (fish | item | quest | treasure), quest]]}}}. Each catch first
  // picks a category by its weight, then an item in it; every day each spot's category weights shift
  // by up to 10% either way, so the chances are the usual ones.
  const fishingData = () => (window.GAME_SOURCES && window.GAME_SOURCES.fishing) || { f: {}, g: {} };
  const FISH_CATS = { fish: 'Fish', item: 'Item', quest: 'Quest', treasure: 'Treasure' };
  const FISHING_CATS = [['spot', 'Fishing spots', ''], ['event', 'Events', ''], ['conditional', 'Special bait', '']];
  let FISHING = null;
  function fishingGrounds() {
    if (FISHING) return FISHING;
    FISHING = new Map(Object.entries(fishingData().g).map(([k, g]) => [k.toLowerCase(), Object.assign({ id: k.toLowerCase(), name: g.n }, g)]));
    return FISHING;
  }
  // item id -> [[ground, chance]].
  let FISH_BY_ITEM = null;
  function fishedAt(id) {
    if (!FISH_BY_ITEM) {
      FISH_BY_ITEM = new Map();
      for (const g of fishingGrounds().values()) for (const [it, ch] of g.c) { const k = String(it); if (!FISH_BY_ITEM.has(k)) FISH_BY_ITEM.set(k, []); FISH_BY_ITEM.get(k).push([g, ch]); }
    }
    return FISH_BY_ITEM.get(String(id)) || [];
  }
  const groundPlaces = g => [...new Set((g.w || []).map(w => w[1] || regionName(w[0])).filter(Boolean))].join(', ');
  const FISHING_NOTE = 'Each catch picks a kind (fish, item, quest, treasure) by its share, then one of that kind. Every day each spot shifts the kinds\' shares by up to 10% either way. Fishing has to succeed first (the skill\'s rank sets how often).';
  function fishingPage(id) {
    const g = fishingGrounds().get(String(id).toLowerCase());
    if (!g) return null;
    const info = [['Kind', (FISHING_CATS.find(c => c[0] === g.k) || ['', 'Fishing'])[1]]];
    if (groundPlaces(g)) info.push(['Where', groundPlaces(g)]);
    const spots = (g.w || []).reduce((a, w) => a + (w[2] || 0), 0);
    if (spots) info.push(['Fishing spots', spots]);
    const sections = [];
    if (g.t || (g.b || []).length || (g.rd || []).length) {
      const rows = [];
      if (g.t) rows.push([g.t]);
      for (const r of g.rd || []) rows.push([lineCell(['Rod: ', itemRef(r)])]);
      for (const b of g.b || []) rows.push([lineCell(['Bait: ', itemRef(b)])]);
      sections.push({ heading: 'When', blocks: [{ kind: 'table', columns: ['Condition'], rows }] });
    }
    const fish = fishingData().f;
    const rows = g.c.slice().sort((a, b) => b[1] - a[1]).map(([it, ch, cat, q]) => [
      lineCell([itemRef(it), q && questById(q) ? ' (starts ' : '', q && questById(q) ? questLink(q) : '', q && questById(q) ? ')' : '']),
      FISH_CATS[cat] || cap(cat || '—'), pctText(ch), fish[it] ? `${fish[it][0]}–${fish[it][1]} cm` : '—']);
    sections.push({ heading: 'Catches', blocks: [{ kind: 'table', columns: ['Catch', 'Kind', 'Chance', 'Size'], rows }, { kind: 'note', text: FISHING_NOTE }] });
    return { title: g.name, subtitle: null, icon: null, category: g.k, infobox: info, summary: '', sections };
  }
  // The item page's fishing section: where it's caught, and its size.
  function fishingSection(id) {
    const at = fishedAt(id);
    if (!at.length) return null;
    const size = fishingData().f[String(id)];
    // The places fold away under their count, shut at first, like the tables in Where to get it.
    const table = { kind: 'table', columns: ['Where', 'Chance', 'When'], rows: at.sort((a, b) => b[1] - a[1]).map(([g, ch]) =>
      [{ t: 'fishing', id: g.id, label: g.name }, pctText(ch), g.t || (g.k === 'spot' ? 'Always' : '—')]) };
    const blocks = [{ kind: 'details', summary: `Where it's caught (${at.length.toLocaleString('en-US')})`, blocks: [table], table }];
    if (size) blocks.push({ kind: 'note', text: `Caught at ${size[0]} to ${size[1]} cm.` });
    return { heading: 'Fishing', blocks };
  }

  // What a monster drops: the item sources (GAME_SOURCES.items and .sets, the enchant scrolls) turned
  // around: race id -> [{kind, item | enchant, source}]. Kinds: monster (its kills), boss (as a field
  // boss), shadow_mission (killed inside a Shadow Mission, by difficulty).
  let DROPS_BY_RACE = null;
  function dropsOfRace(race) {
    if (!DROPS_BY_RACE) {
      DROPS_BY_RACE = new Map();
      const S = window.GAME_SOURCES || {};
      const add = (s, x) => { if (s.t !== 'monster' || !s.id) return; const k = String(s.id); if (!DROPS_BY_RACE.has(k)) DROPS_BY_RACE.set(k, []); DROPS_BY_RACE.get(k).push(x); };
      for (const id of Object.keys(S.items || {})) for (const s of sourcesOf('items', id)) add(s, { item: id, s });
      for (const id of Object.keys(S.sets || {})) for (const s of sourcesOf('sets', id)) add(s, { enchant: id, s });
    }
    return DROPS_BY_RACE.get(String(race)) || [];
  }
  const DROP_KINDS = [['monster', 'Drops'], ['boss', 'As a field boss'], ['shadow_mission', 'In Shadow Missions']];
  function monsterDropsSection(race) {
    const all = dropsOfRace(race);
    if (!all.length) return null;
    const blocks = [];
    for (const [kind, title] of DROP_KINDS) {
      const list = all.filter(x => x.s.k === kind);
      if (!list.length) continue;
      // Notes every row shares are said once, under the table.
      const notesOf = s => String(s.w || '').split(/;\s+/).filter(x => x && !/^shadow missions?$/i.test(x.trim()) && x.trim() !== (monsterById(race) || {}).name)
        .concat(String(s.cn || '').split(/;\s+/)).map(x => bossNote(String(x).trim())).map(clarifyChest).filter(x => x && !/^[\d,]+ in [\d,]+$/.test(x)).flatMap(diffNoteLines).map(capFirst0);
      const shared = list.length > 1 ? notesOf(list[0].s).filter(n => list.every(x => notesOf(x.s).includes(n)) && !/weight \d/.test(n)) : [];
      // Each difficulty's chance shared by every row: a table of its own under this one.
      const sharedDiff = shared.filter(isDiffLine), sharedText = shared.filter(n => !isDiffLine(n));
      const anyDiff = list.some(x => notesOf(x.s).some(isDiffLine));
      const rows = list.sort((a, b) => (b.s.c ?? -1) - (a.s.c ?? -1)).map(({ item, enchant, s }) => {
        let what;
        if (enchant) { const e = enchantById(enchant); what = lineCell([itemRef(s.si || 62005), e ? ', ' : '', e ? { t: 'enchant', id: e.id, label: e.name } : '', e ? ` (${(usageOf(e.r)[2] || '').toLowerCase()})` : '']); }
        else what = lineCell([itemRef(item), s.a ? ` ×${String(s.a).replace(/~/g, '–')}` : '']);
        // The place line (enchants it comes with: "with Topaz suffix"), then the chance notes.
        const notes = notesOf(s).filter(n => !shared.includes(n));
        return [what, s.c != null ? pctText(s.c) : '—', notes.length ? { lines: notes.map(x => noteLinks(x)), sep: '' } : '—'];
      });
      blocks.push({ kind: 'table', title: blocks.length || DROP_KINDS.some(d => d[0] !== kind && all.some(x => x.s.k === d[0])) ? title : '', columns: ['Item', 'Chance', 'Notes'], rows });
      if (sharedDiff.length) blocks.push({ kind: 'table', title: 'Chance per kill on each difficulty', columns: ['Difficulty', 'Chance per kill'],
        rows: sharedDiff.flatMap(n => { const m = /^([A-Za-z]+): (.*)$/.exec(n); return m ? [[m[1], m[2]]] : n.replace(/^None on /, '').split(/,\s*|\s+and\s+/).map(d => [d, 'None']); }) });
      // (The Chance column has the best of the difficulties' chances, not always the hardest's.)
      if (anyDiff) blocks.push({ kind: 'note', text: 'Chance: the best of its difficulties.' + (sharedDiff.length ? '' : ' Notes: each difficulty\'s chance per kill.') });
      if (sharedText.length) blocks.push({ kind: 'note', text: sharedText.join('. ') + '.' });
    }
    blocks.push({ kind: 'note', text: 'Chances are per kill. Events and the game\'s settings can change what drops.' });
    return { heading: 'Drops', blocks };
  }
  const capFirst0 = x => x.charAt(0).toUpperCase() + x.slice(1);
  // A cell as plain text, for telling rows apart.
  const plain0 = v => (v == null ? '' : typeof v === 'object' ? JSON.stringify(v) : String(v));
  // A field boss's schedule in words, from how the server runs it:
  // regen: a wait of min-max after the last one ends (a day or longer: each channel keeps a fixed cycle
  // of about that length); an Erinn start time: every Erinn day at that time; period: every hour on the
  // hour; fixed: one day a week in a time window, once per week. Plus the event it needs.
  const DAY_NAMES = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  // Seconds as "44 h 14 min".
  // (Rounded to the minute first, so 21,599 s reads "6 h", not "5 h 60 min".)
  const hm = s => { s = Math.round(s / 60) * 60; const h = Math.floor(s / 3600), m = Math.round((s % 3600) / 60); return h ? `${h} h${m ? ` ${m} min` : ''}` : `${m} min`; };
  const dur = s => (s >= 86400 && s % 3600 === 0 ? `${+(s / 86400).toFixed(1)} days`.replace(/^1 days/, '1 day')
    : s >= 3600 ? `${+(s / 3600).toFixed(1)} hours`.replace(/^1 hours/, '1 hour') : `${Math.round(s / 60)} minutes`.replace(/^1 minutes/, '1 minute'));
  function bossWhen(b) {
    const s = b.s || [];
    let t;
    if (s[0] === 'period') t = 'Every hour, on the hour (server time)';
    else if (s[0] === 'fixed') t = `Every ${DAY_NAMES[s[1]] || 'week'} between ${s[2]} and ${s[3]} (server time), once a week`;
    else if (s[0] === 'regen' && s[3]) t = `Every Erinn day at ${s[3]} (Erinn time)`;
    else if (s[0] === 'regen') {
      // "2–5 hours" when both ends are whole hours under a day, "52–56 minutes" under an hour, else each
      // end in its own unit.
      const hrs = x => x >= 3600 && x < 86400 && x % 3600 === 0;
      const span = s[1] === s[2] ? (hrs(s[1]) ? `${s[1] / 3600} hour${s[1] === 3600 ? '' : 's'}` : dur(s[1]))
        : hrs(s[1]) && hrs(s[2]) ? `${s[1] / 3600}–${s[2] / 3600} hours`
        : s[2] < 3600 ? `${Math.round(s[1] / 60)}–${Math.round(s[2] / 60)} minutes` : `${dur(s[1])}–${dur(s[2])}`;
      t = `${s[2] >= 86400 && s[1] !== s[2] ? `${hm(s[1])}–${hm(s[2])}` : span} after the last one leaves or is defeated`;
    } else t = cap(eventText(String(b.w || '—')));
    // The event it needs on a line of its own.
    return b.ev ? { lines: [t, `During the ${eventName(b.ev)} event`], sep: '' } : t;
  }
  // How long a field boss stays when nobody defeats it.
  const leavesAfter = b => (b.lt ? (b.lt >= 60 ? `${Math.round(b.lt / 60)} min` : `${b.lt} s`) : '—');
  const BOSS_CHANCE = 'Chance it\'s this boss';
  // How to work out a field boss's next appearance, with its own numbers:
  // one entry per kind of schedule. "Gone" = when its time ran out, or 1 minute after its last boss fell.
  function bossFormula(b) {
    // Short items, each led by what it answers ("Next appearance:", "After a restart:").
    const s = b.s || [], out = [], L = (lead, text) => out.push({ lead, text });
    // "52–56 minutes" when both ends are under an hour, else each end in its own unit.
    // Ends that round to the same minute are given in seconds.
    const range = (a, z) => (Math.round(a / 60) === Math.round(z / 60) ? `${a}–${z} seconds`
      : z < 3600 ? `${Math.round(a / 60)}–${Math.round(z / 60)} minutes` : z < 86400 ? `${hm(a)} to ${hm(z)}` : `${dur(a)} to ${dur(z)}`);
    if (s[0] === 'regen' && s[3]) {
      L('Next appearance:', `every Erinn day at ${s[3]} Erinn time, so every 36 real minutes.`);
      L('Erinn time:', '1 Erinn day = 36 real minutes; 1 Erinn minute = 1.5 real seconds.');
    } else if (s[0] === 'regen' && s[2] < 86400) {
      if (s[1] === s[2]) L('Next appearance:', `${dur(s[1])} after the last one left or was defeated.`);
      else {
        L('Next appearance:', `a random ${range(s[1], s[2])} after the last one left or was defeated.`);
        const avg = (s[1] + s[2]) / 2;
        L('Odds:', `every time in that range is equally likely; ${s[2] < 3600 ? dur(avg) : hm(avg)} on average.`);
      }
      L('After maintenance or a restart:', s[1] === s[2] ? `${dur(s[1])} after the server comes back.` : `a random ${range(s[1], s[2])} after the server comes back.`);
    } else if (s[0] === 'regen' && s[1] === s[2]) {
      // A fixed length: the cycle is that length on every channel, and later waits are exactly it.
      const P = s[1] * 1000, first = P - (Math.floor(Date.now() / 1000) % P);
      L('Next appearance:', `exactly ${dur(s[1])} after the previous one is gone, on every channel.`);
      L('After maintenance or a restart:', `${dur(s[1])} − (the Unix time in seconds when the server started, mod ${P.toLocaleString('en-US')}) milliseconds.`);
      L('Unix time:', 'the game counts the Unix seconds as if they were milliseconds.');
      L('Started right now:', `the first one would come after ${dur(Math.round(first / 1000))}.`);
    } else if (s[0] === 'regen') {
      // A cycle P per channel (s[4] = [Channel 1, Channel 2] seconds). The first wait after a restart comes
      // from the clock; later waits stay about that long (plus the minimum when the first was shorter than
      // it), about 2 minutes shorter each time, never over the maximum.
      const ps = s[4] || [], mn = s[1], mx = s[2];
      const firstWait = P => { const ms = P * 1000; return (ms - (Math.floor(Date.now() / 1000) % ms)) / 1000; };
      const laterWait = w => Math.min(w < mn ? w + mn : w, mx);
      if (mn < 86400) L('Usually:', `about ${pctText(100 * (mx - 86400) / (mx - mn))} of the time the rules below apply; otherwise a random ${hm(mn)}–24 h after the previous one is gone.`);
      L('After maintenance or a restart:', 'P − (the Unix time in seconds when the server started, mod P in milliseconds).');
      L('P (the channel\'s cycle):', ps.length ? ps.map((p, i) => `Channel ${i + 1} ${hm(p)}`).join(', ') + '.' : 'it differs per channel.');
      L('Unix time:', 'the game counts the Unix seconds as if they were milliseconds.');
      L('After that:', 'each gap after the previous one is gone is about as long as that first wait, about 2 minutes shorter each time.');
      L('Limits:', `a first wait under ${hm(mn)} is ${hm(mn)} longer; never more than ${hm(mx)}.`);
      out.push('So the gap stays roughly the same until the next restart.');
      if (ps.length) for (const [i, p] of ps.entries()) { const f = firstWait(p); L(`Restarted just now, Channel ${i + 1}:`, `first in ${hm(f)}, then about every ${hm(laterWait(f))}.`); }
    } else if (s[0] === 'fixed') {
      L('Next appearance:', `once a week on ${DAY_NAMES[s[1]] || 'its day'}, at one minute between ${s[2]} and ${s[3]} (server time).`);
      L('The minute:', 'picked fresh each week, but the same minute on every channel.');
      L('Once per server start:', 'if it has already been and gone, the next one is the following week.');
      L('Restarted that day after its time:', 'the next one is the following week too.');
    } else if (s[0] === 'period') {
      L('Next appearance:', 'every hour, on the hour (server time), while its event is on.');
    }
    return out;
  }
  // The same as a formula / short algorithm, with the boss's numbers (times in milliseconds where the
  // game counts in them). "gone" = when its time ran out, or the last boss's defeat + 1 minute.
  function bossFormulaCode(b) {
    const s = b.s || [], ms = x => (x * 1000).toLocaleString('en-US'), L = [];
    const goneLine = 'gone = when its time ran out, or when it was defeated + 1 min\n       (with other bosses beside it: when the last of them was defeated + 1 min)\n// each channel runs this separately';
    if (s[0] === 'period') {
      L.push('next = the next hh:00 on the server clock   (while its event is on)');
    } else if (s[0] === 'fixed') {
      L.push(`each ${DAY_NAMES[s[1]] || 'week'}:`, `  r      = random(${s[2].replace(':', '')}, ${s[3].replace(':', '')})   // seeded by world name + month + day: same on every channel`,
        '  hour   = floor(r / 100)', '  minute = r mod 60', '  next   = that day at hour:minute (server time)', 'once per server start; if that time has passed on the day, next week');
    } else if (s[0] === 'regen' && s[3]) {
      L.push('erinn_minutes(t) = floor(ms since 1601-01-01 / 1500)        // 1 Erinn minute = 1.5 s', `next = the next t where erinn_minutes(t) mod 1440 = ${(+s[3].split(':')[0]) * 60 + (+s[3].split(':')[1] || 0)}   // Erinn ${s[3]}; every 36 min`);
    } else if (s[0] === 'regen' && s[2] < 86400) {
      L.push(goneLine, `next = gone + random(${hm(s[1])}, ${hm(s[2])})          // uniform`, `after a restart: next = server_start + random(${hm(s[1])}, ${hm(s[2])})`);
    } else if (s[0] === 'regen' && s[1] === s[2]) {
      L.push(goneLine, `P = ${ms(s[1])} ms   // ${hm(s[1])}`, 'after a restart: next = server_start + (P − (unix_seconds(server_start) mod P)) ms', `later:           next = gone + ${hm(s[1])}`);
    } else if (s[0] === 'regen') {
      const ps = s[4] || [];
      L.push(goneLine, `P   = ${ps.length ? ps.map((p, i) => `${ms(p)} ms on Channel ${i + 1}`).join(', ') : "the channel's cycle"}`,
        `min = ${ms(s[1])} ms   // ${hm(s[1])}`, `max = ${ms(s[2])} ms   // ${hm(s[2])}`, 'wait(t) = P − (unix_seconds(t) mod P)          // ms (seconds used as ms)');
      if (s[1] < 86400) L.push('', `r = random(min, max)`, 'if r < 86,400,000:  next = gone + r ms   // under a day: a plain random wait', 'else:');
      const ind = s[1] < 86400 ? '  ' : '';
      L.push(`${ind}after a restart: next = server_start + wait(server_start)`, `${ind}later:           w = wait(gone)`, `${ind}                 if w < min: w = min(w + min, max)`, `${ind}                 if w > max: w = max`, `${ind}                 next = gone + w`);
    }
    return L.join('\n');
  }
  const INFO_SCROLL_NOTE = 'Field Boss Info Scroll: while the boss is more than 10 minutes away, each monster you defeat in its region has a 1 in (5 × the minutes left over 10) chance to drop it, e.g. 1 in 50 with 20 minutes to go. In the last 10 minutes every kill drops one.';

  // Rewards at a glance: the kinds of items (Weapons, Potions, Enchant scrolls...), most common first.
  // The boss room's chest key is handed to everyone and isn't a reward.
  function rewardKinds(ids) {
    const n = new Map();
    for (const id of ids) {
      if (String(id) === '70028') continue;
      const it = items().get(String(id));
      const k = String(id) === '2000' ? 'Gold' : it ? ITEM_CAT_TITLE[String(it.cat).split('/')[0]] || 'Other items' : null;
      if (k) n.set(k, (n.get(k) || 0) + 1);
    }
    return [...n].sort((a, b) => b[1] - a[1]).map(x => x[0]);
  }

  // ---------- minimaps ----------
  // db/minimapinfo.xml <MinimapInfo RegionID MapLocalName MapFile MapWidth MapHeight MapOffsetX
  // MapOffsetY>: each region's minimap (MapFile, through the text table: data/gfx/gui/map_jpg/
  // minimap_tirchonaill_eng.jpg) and the part of the world it shows. The setup copies the pictures to
  // data/maps/<name>.jpg without the language ending (data/maps.js: window.GAME_MAPS lists them).
  const MAP_FILES = new Set(window.GAME_MAPS || []);
  let MINIMAPS = null;
  function minimapOf(region) {
    if (!MINIMAPS) MINIMAPS = new Map(rows('db/minimapinfo.xml', 'MinimapInfo').map(r => [String(r.RegionID).trim(), r]));
    const r = MINIMAPS.get(String(region));
    if (!r) return null;
    const file = String(tr(r.MapFile) || '').toLowerCase().replace(/\\/g, '/').split('/').pop()
      .replace(/\.jpg$/, '').replace(/_(eng|jpn|kor|chn|twn)$/, '').replace(/[^a-z0-9_.-]/g, '_');
    if (!file || !MAP_FILES.has(file)) return null;
    return { src: url('data/maps/' + file + '.jpg'), w: num(r.MapWidth), h: num(r.MapHeight), ox: num(r.MapOffsetX), oy: num(r.MapOffsetY), name: tr(r.MapLocalName).trim() };
  }
  // A world position on a minimap, in percent from its left and top (world y grows northward).
  const mapPoint = (m, x, y) => (m.w && m.h ? { x: (x - m.ox) * 100 / m.w, y: 100 - (y - m.oy) * 100 / m.h } : null);

  // ---------- regions ----------
  // GAME_SOURCES.regions = {id: {n: name, a: area (Uladh, Iria...), k: kind (field, interior / sub-area,
  // dungeon_lobby, arena...), tw: a town, e3: nothing to draw in 3D, l: regions it connects to, s: field spawns [[race, count,
  // respawn min s, max s, place, when, x, y]] (counts are the most at once), np: NPCs [[npc, x, y,
  // place, event 0/1]], b: field bosses [{r: races, w: when, c: chance %, lt: stays s, z: [[x, y,
  // place]]}]}}. Each region page also gathers what other data puts there: props, fishing grounds,
  // dungeon altars, missions, quest steps.
  const regionData = () => (window.GAME_SOURCES && window.GAME_SOURCES.regions) || {};
  const REGION_KIND = { field: 'Field', 'interior / sub-area': 'Building or sub-area', dungeon_lobby: 'Dungeon lobby', arena: 'Arena', tutorial: 'Tutorial',
    event: 'Event', 'ship / sea': 'Ship or sea', private: 'Private (housing, camps)', test: 'Unused or test', login_stage: 'Login screen', template: 'Template' };
  const UNUSED_KINDS = new Set(['test', 'login_stage', 'template']);
  const regionCat = r => (r.tw ? 'town' : r.k === 'field' ? 'field' : r.k === 'dungeon_lobby' ? 'lobby' : UNUSED_KINDS.has(r.k) ? 'unused' : 'other');
  const REGION_CATS = [['town', 'Towns', ''], ['field', 'Fields', ''], ['lobby', 'Dungeon lobbies', ''], ['other', 'Other places', ''], ['unused', 'Unused and test regions', '']];
  const regionLink = id => { const r = regionData()[String(id)]; return r && r.n ? { t: 'region', id: String(id), label: r.n } : (regionName(id) || 'Region ' + id); };
  // What other data puts in each region: race -> regions, npc -> region.
  let REGION_REFS = null;
  function regionRefs() {
    if (REGION_REFS) return REGION_REFS;
    const race = new Map(), npc = new Map(), steps = new Map();
    const add = (m, k, v) => { k = String(k); if (!m.has(k)) m.set(k, new Set()); m.get(k).add(v); };
    for (const [id, r] of Object.entries(regionData())) {
      for (const s of r.s || []) add(race, s[0], id);
      for (const b of r.b || []) for (const x of b.r) add(race, x, id);
      for (const n of r.np || []) if (!n[4]) add(npc, n[0], id);
    }
    for (const q of quests().values()) for (const [, d] of q.s || []) if (d.m) add(steps, d.m, q.id);
    REGION_REFS = { race, npc, quests: steps };
    return REGION_REFS;
  }
  function regionPage(id) {
    const r = regionData()[String(id)];
    if (!r) return null;
    const info = [['Kind', r.tw && !r.tn ? 'Town' : REGION_KIND[r.k] || cap(r.k || 'Region')]];
    if (r.tn) info.push(['Town', r.tn]);
    if (r.a) info.push(['Area', r.a]);
    const sections = [];
    // The minimap, with its NPCs (blue) and spawn areas (gold) pinned.
    const mm = minimapOf(id);
    if (mm) {
      const markers = [];
      for (const [n, x, y, place, ev] of r.np || []) {
        const p = mapPoint(mm, x, y), who = npcById(n);
        if (p && !ev) markers.push(Object.assign({ kind: 'npc', label: who ? who.name : cap(words(n)) }, p, who ? { t: 'npc', id: who.id } : {}));
      }
      const zones = new Map();
      for (const s of r.s || []) { const k = s[6] + ',' + s[7]; if (!zones.has(k)) zones.set(k, []); zones.get(k).push(s); }
      for (const list of zones.values()) {
        const p = mapPoint(mm, list[0][6], list[0][7]);
        // A spawn area: its monsters, each linking to its page when there is one.
        const items = list.map(s => { const m = monsterById(s[0]); return Object.assign({ label: (m ? m.name : 'Monster') + countOf(s[1]) }, m ? { t: 'monster', id: m.id } : {}); });
        if (p) markers.push(Object.assign({ kind: 'mob', label: items.map(x => x.label).join(', '), items }, p));
      }
      sections.push({ heading: 'Map', blocks: [{ kind: 'image', src: mm.src, alt: r.n + ' minimap', markers, caption: 'Circles: NPCs. Diamonds: where monsters spawn. Tap or click a marker to see who is there; tap the map to open it full screen and zoom in.' }] });
    }
    // Walking the region in 3D (region3d.js, from the reader's own game files; not on a hosted copy,
    // whose model files don't include regions, nor where there's nothing to draw: e3).
    if (!NO3D && !window.MABIWIKI_MODELS && !r.e3) sections.push({ heading: 'In 3D', blocks: [{ kind: 'embed', name: 'region3d', args: { id: String(id), title: r.n }, caption: "Opens full screen and loads the region from your game's files" }] });
    if ((r.np || []).length) {
      // The usual NPCs, then the ones placed only for events.
      const npcName = n => { const x = npcById(n); return x ? x.name : String(n); };
      const rowsOf = list => list.sort((a, b) => npcName(a[0]).localeCompare(npcName(b[0]))).map(([n, , , place]) => [npcRef(n), place || '—']);
      const blocks = [], usual = r.np.filter(n => !n[4]), ev = r.np.filter(n => n[4]);
      if (usual.length) blocks.push({ kind: 'table', columns: ['NPC', 'Where'], rows: rowsOf(usual) });
      if (ev.length) blocks.push({ kind: 'table', title: 'During events', columns: ['NPC', 'Where'], rows: rowsOf(ev) });
      sections.push({ heading: 'NPCs', blocks });
    }
    if ((r.s || []).length) {
      const rows = r.s.slice().sort((a, b) => String(a[4]).localeCompare(String(b[4])) || b[1] - a[1]).map(([race, n, lo, hi, place, when]) => [
        raceLink(race), n, place || '—', hi ? (lo === hi ? `${lo} s` : `${lo}–${hi} s`) : '—', when ? cap(when) : '—']);
      sections.push({ heading: 'Monsters', blocks: [{ kind: 'table', columns: ['Monster', 'Most at once', 'Where', 'Respawn', 'When'], rows },
        { kind: 'note', text: 'Counts are the most there at once; usually about half are out.' }] });
    }
    if ((r.b || []).length) sections.push({ heading: 'Field bosses', blocks: [{ kind: 'table', columns: ['Boss', 'Where', BOSS_CHANCE, 'When it appears', 'Leaves after'],
      rows: [...new Map(r.b.map(b => [{ lines: [[].concat(...b.r.map((x, i) => (i ? [', '] : []).concat([raceLink(x)])))], sep: '' }, [...new Set(b.z.map(z => z[2]).filter(Boolean))].join(', ') || '—',
        b.c != null ? pctText(b.c) : '—', bossWhen(b), leavesAfter(b)]).map(row => [row.map(plain0).join('|'), row])).values()] }] });
    // Props that stand here.
    const here = [...propPages().values()].map(p => [p, (p.w.find(w => String(w[0]) === String(id)) || [])[1]]).filter(x => x[1]);
    if (here.length) sections.push({ heading: 'Props', blocks: [{ kind: 'table', columns: ['Prop', 'Times placed', 'Gives'],
      rows: here.sort((a, b) => b[1] - a[1]).map(([p, n]) => [{ t: 'prop', id: p.id, label: p.name }, n,
        { lines: [[].concat(...[...new Set(p.r.filter(x => !x[8] || String(x[8]) === String(id)).map(x => x[0]))].slice(0, 6).map((it, i) => (i ? [', '] : []).concat([itemRef(it)])))], sep: '' }]) }] });
    const rel = [];
    const fish = [...fishingGrounds().values()].filter(g => (g.w || []).some(w => String(w[0]) === String(id)));
    if (fish.length) rel.push({ kind: 'links', title: 'Fishing', items: fish.map(g => ({ t: 'fishing', id: g.id, label: g.name })) });
    // The altar's own dungeon only: its page lists every other version the altar sends to.
    const alt = [...dungeons().values()].filter(d => d.r && String(d.r[0]) === String(id) && ((d.en || []).some(e => e.a === d.id) || (d.v || []).length));
    if (alt.length) rel.push({ kind: 'links', title: alt.length === 1 ? 'Dungeon' : 'Dungeons', items: alt.map(d => ({ t: 'dungeon', id: d.id, label: d.name })) });
    const ms = Object.keys(missionData()).map(missionById).filter(m => m.r && String(m.r[0]) === String(id));
    if (ms.length) rel.push({ kind: 'links', title: 'Missions', items: ms.map(m => ({ t: 'mission', id: m.id, label: m.name })) });
    const links = (r.l || []).map(regionLink).filter(x => typeof x === 'object');
    if (links.length) rel.push({ kind: 'links', title: 'Leads to', items: links });
    if (rel.length) sections.push({ heading: 'Related', blocks: rel });
    const qs = regionRefs().quests.get(String(id));
    if (qs && qs.size) sections.push({ heading: 'Quests', blocks: [questTable(qs, 'Quests with a step here')] });
    return { title: r.n, subtitle: null, icon: null, category: regionCat(r), infobox: info, summary: '', sections };
  }

  // ---------- shadow and theatre missions ----------
  // GAME_SOURCES.missions = {id: {n, t: shadow | theatre, d: description, o: objective, r: [region,
  // name], gv: givers [{type: board | npc | pass_item | prop | script | item, npc, item, level,
  // prop_name}], p: party [min, max], tm: time limit (s), lv: total level, rq: items needed [[item,
  // difficulty]], w: spawns [[stage or wave, times, options [[chance, monsters [[race, count, boss]]]]]],
  // df: difficulties [{n, lv: total level, x: exp, g: gold, s: [monster hp ×, attack ×], i: rewards
  // [[item, chance (-1), count, prefix, suffix, chest (per player | shared | given), when]]}]}}.
  const missionData = () => (window.GAME_SOURCES && window.GAME_SOURCES.missions) || {};
  const MISSION_CATS = [['shadow', 'Shadow Missions', ''], ['theatre', 'Theatre Missions', '']];
  const missionById = id => { const m = missionData()[String(id)]; return m ? Object.assign({ id: String(id), name: m.n }, m) : null; };
  const missionCat = m => (m.t === 'shadow' ? 'shadow' : 'theatre');
  // Theatre missions are all played in Avon (their m.r is the stage's own region): that's where they are.
  const missionWhere = m => (m.t === 'shadow' ? (m.r ? m.r[1] || regionName(m.r[0]) || null : null) : 'Avon');
  // Missions by race (their waves) and item (their rewards), for the other pages.
  let MISSION_REFS = null;
  function missionRefs() {
    if (MISSION_REFS) return MISSION_REFS;
    const race = new Map(), item = new Map(), add = (m, k, id) => { k = String(k); if (!m.has(k)) m.set(k, new Set()); m.get(k).add(id); };
    for (const [id, m] of Object.entries(missionData())) {
      for (const [, , opts] of m.w || []) for (const [, ms] of opts) for (const [r] of ms) if (r) add(race, r, id);
      for (const d of m.df || []) for (const [it] of d.i || []) add(item, it, id);
    }
    MISSION_REFS = { race, item };
    return MISSION_REFS;
  }
  const missionLinks = ids => [...ids].map(missionById).filter(Boolean).sort((a, b) => a.name.localeCompare(b.name)).map(m => ({ t: 'mission', id: m.id, label: m.name }));
  function missionGiver(g) {
    if (g.type === 'board') return ['The mission board'];
    if (g.type === 'npc' && g.npc) return [npcRef(g.npc)];
    if (g.item) return [itemRef(g.item), g.type === 'pass_item' && g.level != null ? ` (level ${g.level})` : ''];
    if (g.prop_name) return [cap(g.prop_name)];
    return [cap(words(g.type || 'script'))];
  }
  function missionPage(id) {
    const m = missionById(id);
    if (!m) return null;
    const info = [['Kind', m.t === 'shadow' ? 'Shadow Mission' : 'Theatre Mission']];
    // A theatre mission has no Where (they're all in Avon).
    if (m.t === 'shadow' && m.r) info.push(['Where', regionData()[String(m.r[0])] ? regionLink(m.r[0]) : m.r[1] || regionName(m.r[0])]);
    if (m.p) info.push(['Party', m.p[0] === m.p[1] ? String(m.p[0]) : `${m.p[0]}–${m.p[1]}`]);
    if (m.tm) info.push(['Time limit', m.tm >= 60 ? `${Math.round(m.tm / 60)} min` : `${m.tm} s`]);
    if (m.lv) info.push(['Total level', `${m.lv.toLocaleString('en-US')} or higher`]);
    const sections = [];
    if (m.o) sections.push({ heading: 'Objective', blocks: [{ kind: 'text', text: m.o }] });
    if ((m.gv || []).length || (m.rq || []).length) {
      const rows = (m.gv || []).map(g => ['Taken from', lineCell(missionGiver(g))]).concat((m.rq || []).map(([it, f]) => ['Needs', lineCell([itemRef(it), f ? ` (for ${f})` : ''])]));
      sections.push({ heading: 'How to take it', blocks: [{ kind: 'table', columns: ['', 'What'], rows }] });
    }
    if ((m.w || []).length) {
      const rows = m.w.map(([stage, n, opts]) => [typeof stage === 'number' ? 'Wave ' + stage : cap(words(String(stage).replace(/^On/, ''))), n,
        { lines: opts.map(([ch, ms]) => [opts.length > 1 ? pctText(ch) + ': ' : '', ...[].concat(...ms.map(([r, c, boss], i) => (i ? [', '] : []).concat([raceLink(r), countOf(c), boss ? ' (boss)' : ''])))]), sep: '' }]);
      sections.push({ heading: 'Monsters', blocks: [{ kind: 'table', columns: ['Spawn', 'Times', 'Monsters'], rows }, { kind: 'note', text: 'Where a spawn has several versions, one is picked at random by its percent. Higher difficulties make the same monsters stronger.' }] });
    }
    const df = m.df || [];
    if (df.length) {
      const tabs = df.map(d => {
        const kv = [['Exp', d.x.toLocaleString('en-US')], ['Gold', gold(d.g)]];
        // lv: [from, to] (to empty on the hardest), or one number.
        const lvs = [].concat(d.lv || []).filter(x => x != null && x !== ''), lvn = x => num(x).toLocaleString('en-US');
        if (lvs.length) kv.unshift(['Total level', lvs.length > 1 ? `${lvn(lvs[0])}–${lvn(lvs[1])}` : `${lvn(lvs[0])} or higher`]);
        if (d.s) kv.push(['Monsters', `${d.s[0]}× life, ${d.s[1]}× attack`]);
        const blocks = [{ kind: 'kv', rows: kv }];
        // "When" comes in words (rank, entry hour, quest step); anything still in the script's own terms is masked.
        if ((d.i || []).length) blocks.push({ kind: 'table', columns: ['Item', 'Chance', 'Chest', 'When'], rows: d.i.slice().sort((a, b) => b[1] - a[1]).map(([it, ch, n, pre, suf, chest, when]) =>
          [lineCell([itemRef(it, typeof n === 'number' ? n : 1), typeof n === 'string' && n !== '1' ? ` ×${n}` : '', ...enchantBits(pre, suf)]), ch >= 0 ? pctText(ch) : '—', chest ? cap(chest) : '—', when ? (/cChar\.|_player\.|==|`/i.test(when) ? 'Depends on your progress' : cap(eventText(when))) : '—']) });
        return { label: d.n || 'Normal', blocks };
      });
      // At a glance: exp and gold from Basic to the hardest, and the kinds of items its chests hold.
      const xs = df.map(d => d.x).filter(Boolean), gs = df.map(d => d.g).filter(Boolean);
      const range = (l, f) => (l.length ? (Math.min(...l) === Math.max(...l) ? f(Math.min(...l)) : `${f(Math.min(...l))} to ${f(Math.max(...l))}`) : '');
      const glance = [];
      if (xs.length) glance.push(`${range(xs, v => v.toLocaleString('en-US'))} exp`);
      if (gs.length) glance.push(range(gs, gold));
      glance.push(...rewardKinds([].concat(...df.map(d => (d.i || []).map(x => x[0])))));
      const blocks = glance.length ? [{ kind: 'list', title: 'At a glance', items: glance }] : [];
      sections.push({ heading: 'Rewards', blocks: blocks.concat([{ kind: 'tabs', label: 'Difficulty', remember: 'mission-difficulty', tabs }, { kind: 'note', text: 'Each reward chest gives one item; "per player" chests are one for each player.' }]) });
    }
    return { title: m.name, subtitle: null, icon: null, category: missionCat(m), infobox: info, summary: m.d || '', sections };
  }

  // ---------- props ----------
  // GAME_SOURCES.props = {p: {key: {n: name, k: hit | gather | hit+gather..., ids: prop class ids,
  // w: where it stands [[region, how many]], r: results [[item, chance %, min, max, how (hit |
  // gather), tool, success % (-1: none), condition, region (0: anywhere)]]}}, g: region names}. A hit
  // rolls once per hit; gathering takes the tool in hand and succeeds at its rate (Herbalism and the
  // like change herbs' rates).
  const propData = () => (window.GAME_SOURCES && window.GAME_SOURCES.props) || { p: {}, g: {} };
  const propRegion = id => propData().g[String(id)] || regionName(id) || 'Region ' + id;
  const PROP_CATS = [['hit', 'Hit for drops', ''], ['gather', 'Gathering', ''], ['both', 'Both', '']];
  const propCat = p => (/hit/.test(p.k) && /gather/.test(p.k) ? 'both' : /gather/.test(p.k) ? 'gather' : 'hit');
  let PROP_PAGES = null;
  function propPages() {
    if (PROP_PAGES) return PROP_PAGES;
    PROP_PAGES = new Map(Object.entries(propData().p).map(([k, p]) => [k, Object.assign({ id: k, name: p.n }, p)]));
    return PROP_PAGES;
  }
  // item id -> props that give it.
  let PROP_BY_ITEM = null;
  function propsGiving(id) {
    if (!PROP_BY_ITEM) {
      PROP_BY_ITEM = new Map();
      for (const p of propPages().values()) for (const r of p.r) { const k = String(r[0]); if (!PROP_BY_ITEM.has(k)) PROP_BY_ITEM.set(k, new Map()); const m = PROP_BY_ITEM.get(k); if (!m.has(p.id)) m.set(p.id, [p, r]); }
    }
    return [...((PROP_BY_ITEM.get(String(id)) || new Map()).values())];
  }
  const propResultsTable = rows => ({ kind: 'table', columns: ['Item', 'Chance', 'Amount', 'How', 'Tool', 'Success', 'Condition'],
    rows: rows.slice().sort((a, b) => b[1] - a[1]).map(([it, ch, lo, hi, how, tool, ok, cond]) => [itemRef(it), pctText(ch), lo === hi ? lo : `${lo}–${hi}`,
      cap(how.replace('(script)', '').trim()), tool ? cap(tool) : '—', ok >= 0 ? pctText(ok) : '—', cond ? cap(cond) : '—']) });
  function propPage(id) {
    const p = propPages().get(String(id));
    if (!p) return null;
    const info = [['Kind', PROP_CATS.find(c => c[0] === propCat(p))[1]], ['Prop ids', p.ids.join(', ')]];
    if (p.nf) info.push(['Name', 'From its internal name (the game has no English one)']);    const placed = p.w.reduce((a, w) => a + w[1], 0);
    if (placed) info.push(['Placed', `${placed.toLocaleString('en-US')} in ${p.w.length} region${p.w.length === 1 ? '' : 's'}`]);
    const sections = [];
    // The 3D model: the first of its class ids with one (viewer3d.js openProp, from the packs).
    if (!NO3D && p.ids.length) sections.push({ heading: '3D model', blocks: [{ kind: 'embed', name: 'prop3d', args: { id: p.id, ids: p.ids.join(',') }, caption: MODEL_CAPTION }] });
    // Results that differ by region: a tab per region (with the ones every region shares).
    const regs = [...new Set(p.r.map(r => r[8]).filter(Boolean))];
    if (regs.length) {
      const shared = p.r.filter(r => !r[8]);
      sections.push({ heading: 'Drops and gathering', blocks: [{ kind: 'tabs', label: 'Region', tabs: regs.map(g => ({ label: propRegion(g), blocks: [propResultsTable(shared.concat(p.r.filter(r => r[8] === g)))] })) },
        { kind: 'note', text: 'What it gives depends on the region it stands in.' }] });
    } else sections.push({ heading: 'Drops and gathering', blocks: [propResultsTable(p.r), { kind: 'note', text: 'Hitting it rolls once per hit. Gathering needs the tool in hand and succeeds at the rate shown.' }] });
    if (p.w.length) sections.push({ heading: 'Where it is', blocks: [{ kind: 'table', columns: ['Region', 'Times placed'],
      rows: p.w.map(([g, n]) => [regionById(g) ? { t: 'region', id: String(g), label: propRegion(g) } : propRegion(g), n]) }] });
    return { title: p.name, subtitle: null, icon: null, category: propCat(p), infobox: info, summary: '', sections };
  }
  // The item page's props section.
  function propSection(id) {
    const list = propsGiving(id);
    if (!list.length) return null;
    return { heading: 'From props', blocks: [{ kind: 'table', columns: ['Prop', 'How', 'Chance', 'Where'], rows: list.sort((a, b) => b[1][1] - a[1][1]).map(([p, r]) => [
      { t: 'prop', id: p.id, label: p.name }, cap(r[4].replace('(script)', '').trim()), pctText(r[1]),
      // The first few regions one a line.
      r[8] ? propRegion(r[8]) : p.w.length ? { lines: p.w.slice(0, 3).map(w => propRegion(w[0])).concat(p.w.length > 3 ? [`and ${p.w.length - 3} more`] : []), sep: '' } : '—']) }] };
  }
  // Regions get their own pages once the region data ships (GAME_SOURCES.regions).
  const regionById = id => (window.GAME_SOURCES && window.GAME_SOURCES.regions && window.GAME_SOURCES.regions[String(id)]) || null;

  // ---------- collection books ----------
  // GAME_SOURCES.collections: {book item id: {e: slots [[item | 0, condition, {prefix, suffix} |
  // {race}]], r: rewards [{kind: item | skill | title | exp | manual, item, count, prefix, suffix,
  // chance (a random pick: one of them), ...}], b: treasure boxes {n, i: [{item, count, chance}]}}}.
  const collectionData = () => (window.GAME_SOURCES && window.GAME_SOURCES.collections) || {};
  let COLL_BY_ITEM = null;
  // item id -> the books that collect it.
  function collectionsOf(id) {
    if (!COLL_BY_ITEM) {
      COLL_BY_ITEM = new Map();
      for (const [bid, b] of Object.entries(collectionData())) for (const [it] of b.e) if (it) { const k = String(it); if (!COLL_BY_ITEM.has(k)) COLL_BY_ITEM.set(k, new Set()); COLL_BY_ITEM.get(k).add(bid); }
    }
    return [...(COLL_BY_ITEM.get(String(id)) || [])];
  }
  function collectionReward(r) {
    if (r.kind === 'item') return questReward(Object.assign({}, r, { kind: 'item' }));
    if (r.kind === 'manual') return ['Manual for ', r.product ? itemRef(r.product) : 'an item', r.item ? ' (' : '', r.item ? itemRef(r.item) : '', r.item ? ')' : ''];
    if (r.kind === 'skill') return ['Skill: ', skillRef(r.skill)];
    if (r.kind === 'title') { const t = titles().find(x => x.id === String(r.title)); return ['Title: ', t ? { t: 'title', id: t.id, label: t.name } : String(r.title)]; }
    if (r.kind === 'exp') return [`${num(r.amount).toLocaleString('en-US')} exploration exp`];
    return [cap(words(r.kind))];
  }
  function collectionSection(id) {
    const b = collectionData()[String(id)];
    if (!b) return null;
    const blocks = [];
    blocks.push({ kind: 'table', title: 'Slots', columns: ['Slot', 'Item', 'Condition'], rows: b.e.map(([it, cond, x], i) => {
      let c = cond || '—';
      if (x && (x.prefix || x.suffix)) c = lineCell([].concat(...['prefix', 'suffix'].filter(k => x[k]).map((k, j) => { const e = enchantById(x[k]); return [j ? ' and ' : 'Enchanted with ', e ? { t: 'enchant', id: e.id, label: e.name } : String(x[k]), ` (${k})`]; }), [' (or an Enchant Scroll of it)']));
      else if (x && x.race) c = lineCell(['A sketch of ', raceLink(x.race)]);
      return [i + 1, it ? itemRef(it) : 'Any item that fits', c];
    }) });
    const rw = b.r || [];
    if (rw.length) {
      const random = rw.some(r => r.chance != null);
      blocks.push({ kind: 'table', title: 'Reward', columns: random ? ['Reward', 'Chance'] : ['Reward'], rows: rw.map(r => random ? [lineCell(collectionReward(r)), r.chance != null ? pctText(r.chance) : 'Always'] : [lineCell(collectionReward(r))]) });
      if (random) blocks.push({ kind: 'note', text: 'One of the rewards with a chance is given.' });
    }
    if (b.b) {
      blocks.push({ kind: 'table', title: `Treasure boxes (${b.b.n})`, columns: ['Item', 'Chance per box'],
        rows: b.b.i.slice().sort((x, y) => y.chance - x.chance).map(r => [lineCell(questReward(Object.assign({ kind: 'item' }, r))), pctText(r.chance)]) });
    }
    return { heading: 'Collection', blocks };
  }

  // ---------- pets ----------
  // A pet is a db/race.xml race with the "pet" tag, or a race db/pet/petracedesc.xml describes (one
  // <Pet> row each: AI_Normal and AI_Idle name its AI, Init_* its starting stats, Color1..3 its
  // colours; 604020 Hell Steed has a row and no tag). A pet race whose ExtraData names another pet
  // race as subraceid ("<xml subraceid=..."), is that race's alternate form, shown on its page.
  // The other files:
  //   db/pet/petagetable.xml     <Pet ID><AgeDesc Age Aging_* LevelUp_* Food_Min Stamina_Recovery
  //                              Body_Scale Head_Scale Leg_Scale Buffer_*/>, one per age. Buffer_*
  //                              aren't explained by the file and aren't shown.
  //   db/pet/petskilltable.xml   <Pet ID><Skill Skill_ID Lv0_T ... Lv15_1/>: a number for each rank
  //                              (Lv<rank>_<label>) of each skill, 999 for none. The file doesn't
  //                              say what the numbers count, so pages show them as they are.
  //   db/pet/petcolorset.xml     <Pet id><Colors type name><Color name value default/>: the colours
  //                              an owner can pick for each part
  //   db/pet/petrebirthtable.xml <Card ID><Rebirth Pet_ID/>: the races a pet can be reborn as
  //   db/vehicledesc.xml         <vehicle_desc id giant_ridable rideon><seats><seat/>...
  //                              <usableskills><skill id usable_race only_driver_moving_usable/>:
  //                              a mount (ExtraData <vehicle type> is its id)
  //   db/aircraftdesc.xml        <aircraft_desc id><aviation velocity rising drop rotation/>: the race
  //                              flies (the file's notes: m/s, rotation in rad/s)
  //   db/birdpetgiftitem.xml     <gift_item_table id><product id rate max_age option/>: the gifts a
  //                              pet brings (ExtraData gift_item_table_id names its table)
  //   db/ai/local/*.xml          the AI files (petAi below)
  // Groups, by the data's own marks, the first that applies: an aircraftdesc.xml row (flying
  // mounts), an ExtraData <vehicle> (ground mounts), the partner, puppet, lite_pet,
  // attachable_pet and prop tags; the rest are companions, by RaceDesc family.
  const PET_GROUPS = [['flying', 'Flying mounts', 'Flying mount'], ['mount', 'Ground mounts', 'Ground mount'], ['partner', 'Partners', 'Partner'],
    ['puppet', 'Puppets', 'Puppet'], ['lite', 'Lite pets', 'Lite pet'], ['attachable', 'Pets that ride on you', 'Rides on its owner'],
    ['prop', 'Home props', 'Home prop'], ['companion', 'Companions', 'Companion']];
  const PET_GROUP = Object.fromEntries(PET_GROUPS.map(g => [g[0], g]));
  // [category key, title, RaceDesc families] for companions.
  const PET_FAMILIES = [
    ['companion/dog', 'Dogs & wolves', 'petdog petdog2 petdog9 petflyingdog wolf'],
    ['companion/cat', 'Cats & big cats', 'petcat petlazycat lion pettiger petcheetah'],
    ['companion/farm', 'Farm animals', 'pig cow sheep chicken'],
    ['companion/bear', 'Bears & pandas', 'petbear bear petredpanda'],
    ['companion/small', 'Small animals', 'rat fox rabbit petmonkey'],
    ['companion/bird', 'Birds', 'petbird petowl penguin ostrich petostrich thunderbird pelican phoenix'],
    ['companion/reptile', 'Spiders, snakes & lizards', 'snake lizard spider'],
    ['companion/water', 'Sea animals', 'petfurseal petwhale petcrab'],
    ['companion/big', 'Large animals', 'petelephant hippopotamus pethorse petdeer reindeer'],
    ['companion/spirit', 'Spirits & monsters', 'petsprite petminighost imp petjackolantern hornwort transpet petskeleton miniglas mimic02'],
    ['companion/dragon', 'Dragons & griffins', 'petdragon petgriffin'],
    ['companion/people', 'People', 'human elf siren'],
    ['companion/other', 'Other', ''],
  ];
  const PET_FAMILY = {};
  for (const [key, , fams] of PET_FAMILIES) for (const f of fams.split(' ').filter(Boolean)) PET_FAMILY[f] = key;
  const PET_CATS = [];
  for (const [g, title] of PET_GROUPS) {
    PET_CATS.push([g, title, '']);
    if (g === 'companion') for (const [key, t] of PET_FAMILIES) PET_CATS.push([key, t, g]);
  }
  const PET_CAT_TITLE = Object.fromEntries(PET_CATS.map(c => [c[0], c[1]]));

  // Attributes with either quote, names as written.
  const attrsOf = s => { const o = {}; for (const a of String(s || '').matchAll(/([\w:]+)\s*=\s*(["'])(.*?)\2/g)) o[a[1]] = unesc(a[3]); return o; };
  // Every <tag ...>body</tag> (or <tag .../>) in a file: [{a: attributes, body}].
  function nested(file, tag) {
    const re = new RegExp('<' + tag + '\\s([^>]*?)(?:/>|>([\\s\\S]*?)</' + tag + '\\s*>)', 'g');
    return [...stripped(file).matchAll(re)].map(m => ({ a: attrsOf(m[1]), body: m[2] || '' }));
  }
  const childRows = (body, tag) => [...String(body || '').matchAll(new RegExp('<' + tag + '\\s([^>]*?)/?>', 'g'))].map(m => attrsOf(m[1]));
  // {id: value} of the first block of each id.
  function byId(list, key) {
    const m = new Map();
    for (const b of list) { const id = String(b.a[key] || '').trim(); if (id && !m.has(id)) m.set(id, b); }
    return m;
  }
  const CACHE = {};
  const cached = (k, fn) => (CACHE[k] !== undefined ? CACHE[k] : (CACHE[k] = fn()));
  const petRaceDesc = () => cached('prd', () => { const m = new Map(); for (const r of rows('db/pet/petracedesc.xml', 'Pet')) { const id = String(r.ID || '').trim(); if (id && !m.has(id)) m.set(id, r); } return m; });
  const aircraft = () => cached('air', () => { const m = new Map(); for (const [id, b] of byId(nested('db/aircraftdesc.xml', 'aircraft_desc'), 'id')) { const av = childRows(b.body, 'aviation')[0]; if (av) m.set(id, av); } return m; });
  const vehicleDescs = () => cached('veh', () => {
    const m = new Map();
    for (const [id, b] of byId(nested('db/vehicledesc.xml', 'vehicle_desc'), 'id')) {
      const part = tag => (new RegExp('<' + tag + '>([\\s\\S]*?)</' + tag + '>').exec(b.body) || ['', ''])[1];
      m.set(id, { a: b.a, seats: childRows(part('seats'), 'seat'), skills: childRows(part('usableskills'), 'skill'), regions: childRows(part('regions'), 'region') });
    }
    return m;
  });
  const petAges = () => cached('ages', () => { const m = new Map(); for (const [id, b] of byId(nested('db/pet/petagetable.xml', 'Pet'), 'ID')) m.set(id, childRows(b.body, 'AgeDesc').sort((x, y) => num(x.Age) - num(y.Age))); return m; });
  const petSkillRows = () => cached('pskills', () => { const m = new Map(); for (const [id, b] of byId(nested('db/pet/petskilltable.xml', 'Pet'), 'ID')) m.set(id, childRows(b.body, 'Skill').filter(s => String(s.Skill_ID || '').trim())); return m; });
  const petColorSets = () => cached('colors', () => { const m = new Map(); for (const [id, b] of byId(nested('db/pet/petcolorset.xml', 'Pet'), 'id')) m.set(id, nested_in(b.body, 'Colors').map(c => ({ a: c.a, colors: childRows(c.body, 'Color') }))); return m; });
  const nested_in = (body, tag) => [...String(body || '').matchAll(new RegExp('<' + tag + '\\s([^>]*?)>([\\s\\S]*?)</' + tag + '\\s*>', 'g'))].map(m => ({ a: attrsOf(m[1]), body: m[2] }));
  const petRebirths = () => cached('rebirth', () => { const m = new Map(); for (const [id, b] of byId(nested('db/pet/petrebirthtable.xml', 'Card'), 'ID')) m.set(id, childRows(b.body, 'Rebirth').map(r => String(r.Pet_ID || '').trim()).filter(Boolean)); return m; });
  const giftTables = () => cached('gifts', () => { const m = new Map(); for (const [id, b] of byId(nested('db/birdpetgiftitem.xml', 'gift_item_table'), 'id')) m.set(id, childRows(b.body, 'product')); return m; });

  // A race's name: its text, or EnglishName where the text is only in Korean (then the text is
  // the subtitle).
  function raceNames(r) {
    const raw = tr(r.LocalName), eng = String(r.EnglishName || '').trim();
    let name = raceName(raw), local = '';
    if (!english(name) && english(eng)) { local = name; name = eng; }
    if (!name) name = eng || String(r.ClassName || '').trim() || 'Race ' + r.ID;
    const mini = raceMini(raw);
    return { name: mini ? `${name} (${mini})` : name, local };
  }

  let PETS = null, PET_BY_ID = null, PET_OF_RACE = null;
  function pets() {
    if (PETS) return PETS;
    PETS = [];
    PET_BY_ID = new Map();
    PET_OF_RACE = new Map();
    const all = new Map();
    for (const [id, r] of raceRows()) if (petRaceDesc().has(id) || raceTags(r).includes('pet')) all.set(id, r);
    const formOf = new Map(); // alternate form -> main race
    for (const [id, r] of all) {
      const sub = String(raceExtra(r).attrs.subraceid || '').trim();
      if (sub && sub !== id && all.has(sub) && !formOf.has(sub)) formOf.set(sub, id);
    }
    for (const [id, r] of all) {
      if (formOf.has(id)) continue;
      const t = raceTags(r), ts = new Set(t), ex = raceExtra(r);
      const group = aircraft().has(id) ? 'flying' : ex.vehicle ? 'mount' : ts.has('partner') ? 'partner' : ts.has('puppet') ? 'puppet'
        : ts.has('lite_pet') ? 'lite' : ts.has('attachable_pet') ? 'attachable' : ts.has('prop') ? 'prop' : 'companion';
      const fam = familyOf(r);
      const p = { id, r, t, ts, ex, ...raceNames(r), group, fam, cat: group === 'companion' ? PET_FAMILY[fam] || 'companion/other' : group, forms: [] };
      PETS.push(p);
      PET_BY_ID.set(id, p);
      PET_OF_RACE.set(id, p);
    }
    for (const [form, main] of formOf) {
      const p = PET_BY_ID.get(main);
      if (p && !PET_BY_ID.has(form)) { p.forms.push(form); PET_OF_RACE.set(form, p); }
    }
    return PETS;
  }
  const petById = id => (pets(), PET_BY_ID.get(String(id).trim()));
  // The pet a race id belongs to (an alternate form's main pet).
  const petOfRace = id => (pets(), PET_OF_RACE.get(String(id).trim()));
  const petLink = p => ({ t: 'pet', id: p.id, label: p.name });
  function petChips(list) {
    const seen = new Set(), out = [];
    for (const p of list) if (p && !seen.has(p.id)) { seen.add(p.id); out.push(petLink(p)); }
    return out;
  }
  // The skills a pet has: its skill table's (rows not all 999) and its starting script's.
  const skillRowUsed = s => Object.keys(s).some(k => /^Lv\d+_/.test(k) && num(s[k]) !== 999);
  const petSkillIds = p => cached('psk' + p.id, () => new Set([...(petSkillRows().get(p.id) || []).filter(skillRowUsed).map(s => String(s.Skill_ID).trim()),
    ...raceScript(p.r).skills.keys()]));

  // Items that name pet races (raceid="<race>;<race>"): summoning items (create_pet,
  // create_lite_pet), training kits (pet_training_kit), tempering potions (pet_special_potion)
  // and age potions (pet_age_potion). {item id: {kind, pets}} and {pet id: [[kind, item id]]}.
  const PET_ITEM_KINDS = [['create_pet', 'summon'], ['create_lite_pet', 'summon'], ['pet_training_kit', 'kit'], ['pet_special_potion', 'potion'], ['pet_age_potion', 'age']];
  const petItems = () => cached('pitems', () => {
    const byItem = new Map(), byPet = new Map();
    for (const [iid, it] of items()) {
      const k = PET_ITEM_KINDS.find(x => it.tags.has(x[0]));
      if (!k) continue;
      const list = petChipsRaw(String(xmlAttrs(it.r.XML).raceid || '').split(';').map(petOfRace));
      if (!list.length) continue;
      byItem.set(iid, { kind: k[1], pets: list });
      for (const p of list) { if (!byPet.has(p.id)) byPet.set(p.id, []); byPet.get(p.id).push([k[1], iid]); }
    }
    return { byItem, byPet };
  });
  const petChipsRaw = list => list.filter((p, i) => p && list.indexOf(p) === i);
  // Skill id -> the pets whose skill table has a rank of it (not all 999).
  const petsBySkill = () => cached('pbs', () => {
    const m = new Map();
    for (const p of pets()) {
      for (const s of petSkillRows().get(p.id) || []) {
        const sid = String(s.Skill_ID).trim();
        if (!skillRowUsed(s)) continue;
        if (!m.has(sid)) m.set(sid, []);
        if (!m.get(sid).includes(p)) m.get(sid).push(p);
      }
    }
    return m;
  });

  // ---------- pet AI ----------
  // db/ai/local/<file>.xml: <aidescdata><template> with <imports> (common_pet.xml: the pet commands),
  // <var_decl> defaults, <patterns> (<pattern name> of <cmd name .../> steps, <if expr>, random
  // <switch>es) and <strategy><idle type max_radius follow_radius> naming the patterns it runs
  // (wander, attack, support, pick_item...; the file's note: when the pet gets max_radius away
  // from its owner it comes back to within follow_radius), then <definition race="..."> blocks:
  // the names petracedesc.xml's AI_Normal and AI_Idle give. Patterns named "command:<name>" are
  // what the pet does when told to (common_pet.xml maps the skill window's commands to them).
  // aidescdata_pet_idle.xml and its kind say (in a note) they're the AI a pet gets 30 seconds
  // after its owner logs in as it.
  const aiDefs = () => cached('aidefs', () => {
    const m = new Map();
    for (const f of Object.keys(FILES)) {
      if (!/^db\/ai\/local\/.*\.xml$/.test(f)) continue;
      for (const d of stripped(f).matchAll(/<definition\s+race\s*=\s*"([^"]*)"/g)) if (!m.has(d[1].trim().toLowerCase())) m.set(d[1].trim().toLowerCase(), f);
    }
    return m;
  });
  function aiPatterns(file) {
    return cached('aip' + file, () => {
      const m = new Map();
      for (const p of stripped(file).matchAll(/<pattern\s+name\s*=\s*"([^"]+)"\s*>([\s\S]*?)<\/pattern>/g)) if (!m.has(p[1])) m.set(p[1], p[2]);
      return m;
    });
  }
  function aiDoc(def) {
    const file = aiDefs().get(String(def || '').trim().toLowerCase());
    if (!file) return null;
    const text = stripped(file), pats = new Map(aiPatterns(file));
    for (const im of text.matchAll(/<import\s+file\s*=\s*"([^"]+)"/g)) {
      const f = im[1].toLowerCase().replace(/\\/g, '/').replace(/^data\//, '');
      if (FILES[f] !== undefined) for (const [k, v] of aiPatterns(f)) if (!pats.has(k)) pats.set(k, v);
    }
    const idle = /<idle\s([^>]*?)\/?>/.exec(text);
    const strategy = (/<strategy>([\s\S]*?)<\/strategy>/.exec(text) || ['', ''])[1];
    const runs = new Set([...strategy.matchAll(/<pattern\b[^>]*?\bname\s*=\s*"([^"]+)"/g)].map(m => m[1]));
    const events = [...strategy.matchAll(/<event\s+name\s*=\s*"([^"]+)"\s*>([\s\S]*?)<\/event>/g)].map(m => [m[1], (/name\s*=\s*"([^"]+)"/.exec(m[2]) || [])[1]]);
    return { file, pats, idle: idle ? attrsOf(idle[1]) : {}, runs, events, loginIdle: /30초 후에 붙는 idle AI/.test(FILES[file] || '') };
  }
  const skillsIn = body => [...new Set([...String(body || '').matchAll(/(?:skill_id\s*=\s*"|@is_(?:usable|preparable)_skill\()\s*(\d+)/g)].map(m => m[1]))];
  // Command names (command:<name>) in words where there's no skill to name it by.
  const AI_COMMANDS = { cute_trick: 'Cute trick', sitdown: 'Sit down', standup: 'Stand up', call: 'Come (call)', stop: 'Stop', pet_stop: 'Stop',
    report_box: 'Report treasure boxes', enable_collecting: 'Start gathering', disable_collecting: 'Stop gathering',
    search_item: 'Search personal shops for an item' }; // common_pet.xml's note: "search items at personal shop"
  const INSTRUMENT_KINDS = { string: 'string instruments', wind: 'wind instruments', instrument: 'any instrument' };
  // What a wander pattern does, in words.
  function wanderWords(body, own) {
    const out = [];
    const b = String(body || '');
    const food = /(\d+)\s*>\s*@food|@food\s*<\s*(\d+)/.exec(unesc(b));
    if (food) out.push(`Asks for food when its food is below ${food[1] || food[2]}`); // the file's note: "when hungry, asks for food"
    if (/name\s*=\s*"set_emotion"/.test(b)) out.push('Shows its emotions with motions');
    if (/name\s*=\s*"skill_relax"/.test(b) && (!own || own.has('10004'))) out.push('Sits down to rest');
    const radii = [...b.matchAll(/name\s*=\s*"move_random"[^>]*?radius\s*=\s*"(\d+)/g)].map(m => num(m[1]));
    if (radii.length) out.push(`Wanders around (up to ${Math.max(...radii)} away)`);
    if (/name\s*=\s*"circle"/.test(b)) out.push('Circles around');
    if (/name\s*=\s*"warp_random"/.test(b)) out.push('Teleports about');
    if (/name\s*=\s*"follow_target"/.test(b) && /@master_movestate/.test(b)) out.push('Walks or runs after its owner when the owner moves');
    return out;
  }
  // The behaviour section's blocks for a pet, from its AI files.
  function petBehaviour(p) {
    const prd = petRaceDesc().get(p.id) || {};
    const ai = aiDoc(prd.AI_Normal);
    if (!ai) return [];
    const own = petSkillIds(p).size ? petSkillIds(p) : null;
    const hasAny = list => !own || !list.length || list.some(s => own.has(s));
    const sk = sid => { const s = skillById(sid); return s ? skillLink(sid, s.name) : null; };
    const blocks = [], general = [];
    const it = ai.idle;
    if (it.max_radius && it.follow_radius) general.push(`Follows its owner: when it gets ${num(it.max_radius)} away, it comes back to within ${num(it.follow_radius)}`);
    const t = String(it.type || '');
    if (/dance/.test(t)) {
      const k = String(it.instrument || '').split('/').filter(Boolean)[0] || '';
      general.push(`Dances to music${INSTRUMENT_KINDS[k] ? ' played on ' + INSTRUMENT_KINDS[k] : ''}`);
    } else if (/concert/.test(t)) general.push('Joins in concerts');
    else if (/^idle_wander/.test(t)) general.push('Wanders on its own rather than following its owner');
    // support: the skills it uses on its owner, and the owner's life it waits for.
    if (ai.runs.has('support')) {
      const body = ai.pats.get('support') || '';
      const at = /([\d.]+)\s*>\s*@master_life\s*\/\s*@master_life_max/.exec(unesc(body));
      const list = skillsIn(body).filter(s => !own || own.has(s)).map(skillById).filter(Boolean);
      if (at && list.length) general.push(`Uses ${list.map(s => s.name).join(' and ')} on its owner when the owner's life is below ${+(num(at[1]) * 100).toFixed(1)}%`);
    }
    // Picking up items and reporting boxes: the files' notes say they're for smart pets (the tag).
    const smart = p.ts.has('smart');
    if (ai.runs.has('pick_item') && smart) general.push('Picks up items near it');
    if (general.length) blocks.push({ kind: 'list', items: general });
    // Commands: its command:<name> patterns (its own and imported ones). One that uses skills is
    // named by them, and left out when the pet has none of them.
    const cmds = [], seen = new Set();
    for (const [name, body] of ai.pats) {
      if (!/^command:/.test(name)) continue;
      const key = name.slice(8), ids = skillsIn(body).filter(s => skillById(s));
      if (key === 'report_box' && !smart) continue;
      if (ids.length) {
        if (!hasAny(ids)) continue;
        for (const s of ids) if ((!own || own.has(s)) && !seen.has('s' + s)) { seen.add('s' + s); cmds.push(sk(s)); }
      } else {
        const label = AI_COMMANDS[key] || cap(words(key).toLowerCase());
        if (!seen.has(label)) { seen.add(label); cmds.push(label); }
      }
    }
    if (cmds.length) blocks.push({ kind: 'list', title: 'Commands it knows', items: cmds });
    // In battle: the attack pattern's moves.
    if (ai.runs.has('attack')) {
      const body = ai.pats.get('attack') || '', moves = [];
      if (/name\s*=\s*"melee_attack"/.test(body)) moves.push('Melee attacks');
      for (const s of skillsIn(body)) if ((!own || own.has(s)) && sk(s)) moves.push(sk(s));
      if (moves.length) blocks.push({ kind: 'list', title: 'In battle', items: moves });
    }
    const EVENTS = { blowhit: 'When knocked back', defence: 'After blocking with Defense', hit: 'When hit' };
    for (const [ev, pat] of ai.events) {
      const body = ai.pats.get(pat) || '', moves = [];
      if (/name\s*=\s*"melee_attack"/.test(body)) moves.push('Melee attack');
      for (const s of skillsIn(body)) if ((!own || own.has(s)) && sk(s)) moves.push(sk(s));
      if (moves.length && EVENTS[ev]) blocks.push({ kind: 'list', title: EVENTS[ev], items: moves });
    }
    if (ai.runs.has('wander')) {
      const w = wanderWords(ai.pats.get('wander'), own);
      if (w.length) blocks.push({ kind: 'list', title: 'While idle', items: w });
    }
    // The AI it gets when its owner logs in as it.
    if (prd.AI_Idle && prd.AI_Idle !== prd.AI_Normal) {
      const idle = aiDoc(prd.AI_Idle);
      if (idle && idle.runs.has('wander')) {
        const w = wanderWords(idle.pats.get('wander'), own);
        if (w.length) blocks.push({ kind: 'list', title: idle.loginIdle ? 'When you log in as it (after 30 seconds)' : 'Idle AI', items: w });
      }
    }
    return blocks;
  }

  // Traits from the tags and ExtraData, in words.
  const PET_TRAITS = [['race_transform', 'Changes into another form'], ['smart', 'Smart'], ['gift_item', 'Brings gifts'],
    ['singing_pet', 'Sings'], ['dancer', 'Dances'], ['ensemble', 'Performs with its ensemble partner'], ['collecting_pet', 'Gathers'],
    ['collect_mushroom', 'Gathers mushrooms'], ['support_fishing_pet', 'Helps with fishing'], ['giant_ridable', 'Giants can ride it'],
    ['giants_ridable', 'Giants can ride it'], ['vip_pet', 'VIP pet'], ['big_pet', 'Big pet'], ['color_variation', 'Comes in colour variations'],
    ['pet_windmill_enable', 'Can use Windmill'], ['talkable', 'Talks'], ['unable_tame', "Can't be tamed"], ['not_freeze', "Can't be frozen"],
    ['no_hitmotion', "Doesn't flinch when hit"], ['not_down', "Can't be knocked down"], ['undead', 'Undead']];
  const TRANSFORM_KINDS = { paladin: 'Paladin', darkknight: 'Dark Knight' };
  const RIDER_RACES = { all: 'Everyone', human: 'Humans', elf: 'Elves', giant: 'Giants', npc: 'NPCs' };
  const riderRaces = s => [...new Set(String(s || '').split(/[\s,]+/).filter(Boolean).map(x => RIDER_RACES[x.toLowerCase()] || cap(words(x).toLowerCase())))].join(', ');
  const ignoreConsole = r => { const m = /<ignore_pet_console\b([^>]*)>/i.exec(String(r.ExtraData || '')); return m ? attrsQ(m[1]) : null; };
  const transformOf = r => { const m = /<transform\b([^>]*)>/i.exec(String(r.ExtraData || '')); return m ? attrsQ(m[1]) : null; };
  function petTraits(p) {
    const out = [PET_GROUP[p.group][2]];
    for (const [k, l] of PET_TRAITS) if (p.ts.has(k) && !out.includes(l)) out.push(l);
    const tf = transformOf(p.r);
    if (tf && tf.type) out.push(`Has a ${TRANSFORM_KINDS[tf.type.toLowerCase()] || titleCase(words(tf.type))} form`);
    const ig = ignoreConsole(p.r);
    if (ig && num(ig.rate)) {
      const list = String(ig.commands || '').split(';').map(s => s.trim()).filter(Boolean).map(c => AI_COMMANDS[c] || cap(words(c).toLowerCase()));
      out.push(`Ignores commands ${num(ig.rate)}% of the time` + (list.length ? ` (${list.join(', ')})` : ''));
      if (/^disregard$/i.test(ig.socialmotion || '')) out.push('Disregards social motions');
    }
    return out;
  }
  const isPetMount = p => p.group === 'flying' || p.group === 'mount';
  const petSeats = p => { const v = p.ex.vehicle && vehicleDescs().get(p.ex.vehicle); return v ? v.seats.length : null; };

  // A mount's rows and skills table (for a pet or one of its forms).
  function mountBlocks(id, r) {
    const ex = raceExtra(r), v = ex.vehicle && vehicleDescs().get(ex.vehicle), air = aircraft().get(id), kv = [], out = [];
    const ts = new Set(raceTags(r));
    if (v) {
      kv.push(['Passengers', v.seats.length]);
      if (truthy(v.a.giant_ridable) || ts.has('giant_ridable') || ts.has('giants_ridable')) kv.push(['Giants can ride', 'Yes']);
      const on = String(v.a.rideon || '').toLowerCase();
      if (on === 'sled') {
        // regions: the sled per region type; "default" is every region not named (the file's note).
        const types = v.regions.map(x => String(x.type || '').toLowerCase()).filter(Boolean);
        kv.push(['Riders sit', 'In the sled it pulls' + (types.length && !types.includes('default') ? ` (in ${types.join(', ')} regions)` : '')]);
      } else if (on === 'inside') kv.push(['Riders sit', 'Inside it']);
    }
    if (air) {
      // aircraftdesc.xml's notes: velocity, rising and drop in m/s, rotation in rad/s.
      if (air.velocity !== undefined) kv.push(['Flying speed', num(air.velocity) + ' m/s']);
      if (air.rising !== undefined) kv.push(['Climbing speed', num(air.rising) + ' m/s']);
      if (air.drop !== undefined) kv.push(['Descending speed', Math.abs(num(air.drop)) + ' m/s']);
      if (air.rotation !== undefined) kv.push(['Turning speed', num(air.rotation) + ' rad/s']);
    }
    if (kv.length) out.push({ kind: 'kv', rows: kv });
    if (v && v.skills.length) {
      // only_driver_moving_usable: the file's note says only the owner can use it while moving.
      const anyOwner = v.skills.some(s => truthy(s.only_driver_moving_usable));
      out.push({ kind: 'table', title: 'Skills usable while riding', columns: ['Skill', 'Usable by'].concat(anyOwner ? ['Owner only, while moving'] : []),
        rows: v.skills.filter(s => s.id).map(s => { const k = skillById(s.id); return [k ? skillLink(s.id, k.name) : 'A certain skill', riderRaces(s.usable_race) || 'Everyone']
          .concat(anyOwner ? [truthy(s.only_driver_moving_usable) ? 'Yes' : '—'] : []); }) });
    }
    return out;
  }

  const PET_STATS = [['Init_Life', 'HP'], ['Init_Mana', 'MP'], ['Init_Stamina', 'Stamina'], ['Init_STR', 'STR'], ['Init_INT', 'INT'],
    ['Init_DEX', 'DEX'], ['Init_WILL', 'Will'], ['Init_LUCK', 'Luck'], ['Init_Defence', 'Defense'], ['Init_Protect', 'Protection']];
  const AGE_STATS = [['Life', 'HP'], ['Mana', 'MP'], ['Stamina', 'Stamina'], ['STR', 'STR'], ['INT', 'INT'], ['DEX', 'DEX'], ['WILL', 'Will'], ['LUCK', 'Luck']];
  const SKILL_RANK_KEYS = s => Object.keys(s).filter(k => /^Lv\d+_\w+$/.test(k)).sort((a, b) => num(/\d+/.exec(a)[0]) - num(/\d+/.exec(b)[0]));
  // Columns that aren't all empty.
  function trimColumns(columns, rowsT, keep = 1) {
    const used = columns.map((_, j) => j < keep || rowsT.some(r => r[j] !== '' && r[j] !== null && r[j] !== undefined));
    return { columns: columns.filter((_, j) => used[j]), rows: rowsT.map(r => r.filter((_, j) => used[j])) };
  }

  function petPage(id) {
    const p = petById(id);
    if (!p) return null;
    const r = p.r, prd = petRaceDesc().get(p.id), ts = p.ts;
    const info = [['Group', PET_GROUP[p.group][1]]];
    if (p.group === 'companion' && p.cat !== 'companion/other') info.push(['Family', PET_CAT_TITLE[p.cat]]);
    const g = String(r.Gender || '').trim();
    if (g === '1' || g === '2') info.push(['Gender', g === '1' ? 'Female' : 'Male']);
    if (isPetMount(p) && petSeats(p) !== null) info.push(['Passengers', petSeats(p)]);
    if (prd && prd.Exp_Factor !== undefined) info.push(['Experience factor', '×' + num(prd.Exp_Factor)]);
    if (r.ClassName) info.push(['Internal name', r.ClassName]);
    const sections = [];
    if (!NO3D && String(r.DefaultMesh || '').trim()) sections.push({ heading: '3D model', blocks: [{ kind: 'embed', name: 'race3d', args: { id: p.id }, caption: MODEL_CAPTION }] });
    const mount = mountBlocks(p.id, r);
    if (mount.length) sections.push({ heading: p.group === 'flying' ? 'Flying mount' : 'Mount', blocks: mount });
    sections.push({ heading: 'Traits', blocks: [{ kind: 'list', items: petTraits(p) }] });
    // Fishing helpers: their ExtraData values, by their names in words (the file doesn't explain them).
    if (ts.has('support_fishing_pet')) {
      const root = /<xml\b([^>]*?)\/?>/i.exec(String(r.ExtraData || ''));
      const fk = Object.entries(attrsOf(root ? root[1] : '')).filter(([k]) => /prof$|^fish_|^recovery_/i.test(k)).map(([k, v]) => [cap(words(k).toLowerCase()), num(v)]);
      if (fk.length) sections.push({ heading: 'Fishing help', blocks: [{ kind: 'kv', rows: fk }] });
    }
    if (prd) {
      const st = PET_STATS.filter(([k]) => prd[k] !== undefined).map(([k, l]) => [l, num(prd[k])]);
      if (prd.Init_Age !== undefined) st.unshift(['Age', num(prd.Init_Age)]);
      if (st.length) sections.push({ heading: 'Starting stats', blocks: [{ kind: 'kv', rows: st }] });
    }
    // Growth by age (petagetable.xml).
    const ages = petAges().get(p.id) || [];
    if (ages.length) {
      const stat = pre => trimColumns(['Age'].concat(AGE_STATS.map(s => s[1])),
        ages.map(a => [num(a.Age)].concat(AGE_STATS.map(([k]) => (a[pre + k] === undefined ? '' : num(a[pre + k]))))));
      const ag = stat('Aging_'), lv = stat('LevelUp_');
      const bodyCols = [['Body_Scale', 'Body scale'], ['Head_Scale', 'Head scale'], ['Leg_Scale', 'Leg scale'], ['Food_Min', 'Food min'], ['Stamina_Recovery', 'Stamina recovery']];
      // The scales are sizes against the model's own (×1), as on the monster pages.
      const body = trimColumns(['Age'].concat(bodyCols.map(c => c[1])), ages.map(a => [num(a.Age)].concat(bodyCols.map(([k]) => (a[k] === undefined ? '' : /_Scale$/.test(k) ? '×' + num(a[k]) : num(a[k]))))));
      // Each long table folded under its name and count, shut at first.
      const fold = (title, t) => ({ kind: 'details', summary: `${title} (${t.rows.length})`, blocks: [{ kind: 'table', columns: t.columns, rows: t.rows }] });
      sections.push({ heading: 'Growth by age', blocks: [
        fold('Stats gained at each age', ag),
        fold('Stats gained per level at each age', lv),
        fold('Body and upkeep at each age', body),
      ] });
    }
    // Skills (petskilltable.xml): each rank's number, 999s left out.
    const sks = (petSkillRows().get(p.id) || []).filter(s => skillById(s.Skill_ID) && skillRowUsed(s));
    if (sks.length) {
      const keys = [...new Set(sks.flatMap(SKILL_RANK_KEYS))].sort((a, b) => num(/\d+/.exec(a)[0]) - num(/\d+/.exec(b)[0]));
      const t = trimColumns(['Skill'].concat(keys.map(k => rankLabel(num(/\d+/.exec(k)[0])))),
        sks.map(s => [skillLink(s.Skill_ID, skillById(s.Skill_ID).name)].concat(keys.map(k => (s[k] === undefined || num(s[k]) === 999 ? '' : num(s[k]))))));
      sections.push({ heading: 'Skills', blocks: [{ kind: 'details', summary: `${t.rows.length} skill${t.rows.length === 1 ? '' : 's'}`, blocks: [{ kind: 'table', columns: t.columns, rows: t.rows },
        { kind: 'note', text: "Each rank's number in the pet skill table; ranks with 999 there are left blank." }] }] });
    }
    const beh = petBehaviour(p);
    if (beh.length) sections.push({ heading: 'Behaviour', blocks: beh });
    // Colour choices (petcolorset.xml).
    const colors = petColorSets().get(p.id) || [];
    if (colors.length) {
      // Names left in Korean in the text table aren't shown: the part as its number, the colour as its value.
      const en = s => { const t = tr(s); return english(t) ? t : ''; };
      sections.push({ heading: 'Colour choices', blocks: colors.map(c => ({ kind: 'table', title: en(c.a.name) || `Part ${num(c.a.type) + 1}`, columns: ['Colour', 'Value', 'Default'],
        rows: c.colors.map(x => [en(x.name) || '—', '#' + String(x.value || '').trim().replace(/^ff(?=[0-9a-f]{6}$)/i, '').toUpperCase(), truthy(x.default) ? 'Yes' : '']) })) });
    }
    // Gifts (birdpetgiftitem.xml).
    const gift = giftTables().get(String(p.ex.attrs.gift_item_table_id || '').trim());
    if (gift && gift.length) {
      const anyAge = gift.some(x => x.max_age !== undefined), anyCount = gift.some(x => /count\s*:/.test(x.option || ''));
      const rowsG = gift.filter(x => items().has(String(x.id).trim())).sort((a, b) => num(b.rate) - num(a.rate)).map(x => {
        const iid = String(x.id).trim(), c = /count\s*:\s*(\d+)/.exec(x.option || '');
        return [itemLink(iid, items().get(iid).name), num(x.rate)].concat(anyCount ? [c ? num(c[1]) : ''] : [], anyAge ? [x.max_age !== undefined ? num(x.max_age) : ''] : []);
      });
      if (rowsG.length) sections.push({ heading: 'Gifts', blocks: [{ kind: 'table', columns: ['Item', 'Rate'].concat(anyCount ? ['Count'] : [], anyAge ? ['Max age'] : []), rows: rowsG }] });
    }
    // Alternate forms and rebirth.
    const forms = [];
    for (const fid of p.forms) {
      const fr = raceRows().get(fid), b = [];
      const kv = [];
      if (fr.ClassName) kv.push(['Internal name', fr.ClassName]);
      for (const m of mountBlocks(fid, fr).filter(x => x.kind === 'kv')) kv.push(...m.rows);
      b.push({ kind: 'kv', title: raceNames(fr).name, rows: kv });
      if (!NO3D && String(fr.DefaultMesh || '').trim()) b.push({ kind: 'embed', name: 'race3d', args: { id: fid }, caption: MODEL_CAPTION });
      forms.push(...b);
    }
    if (forms.length) sections.push({ heading: p.forms.length > 1 ? 'Alternate forms' : 'Alternate form', blocks: forms });
    const reb = [...new Set([p.id].concat(p.forms).flatMap(x => petRebirths().get(x) || []))];
    if (reb.some(x => x !== p.id && !p.forms.includes(x))) {
      sections.push({ heading: 'Rebirth', blocks: [{ kind: 'links', title: 'Can be reborn as', items: petChips(reb.map(petOfRace)) }] });
    }
    // Items: the ones that summon it, and kits and potions that list it.
    const rel = [], mine = petItems().byPet.get(p.id) || [];
    const ITEM_TITLES = [['summon', 'Summoned by'], ['kit', 'Training kits'], ['potion', 'Tempering potions'], ['age', 'Age potions']];
    for (const [k, title] of ITEM_TITLES) {
      const list = mine.filter(x => x[0] === k).map(x => itemLink(x[1], items().get(x[1]).name));
      if (list.length) rel.push({ kind: 'links', title, items: list });
    }
    if (rel.length) sections.push({ heading: 'Items', blocks: rel });
    const other = [];
    if (p.ex.attrs.mate_stringid) {
      const want = String(p.ex.attrs.mate_stringid).split('/').map(s => s.trim().toLowerCase()).filter(Boolean);
      const mates = pets().filter(o => o !== p && want.length && want.every(x => o.ts.has(x)));
      if (mates.length) other.push({ kind: 'links', title: 'Ensemble partner', items: petChips(mates) });
    }
    const eff = p.ex.attrs.effect_itemid && items().get(String(p.ex.attrs.effect_itemid).trim());
    if (eff && !/N\/A/.test(eff.name)) other.push({ kind: 'links', title: 'Effect item', items: [itemLink(String(p.ex.attrs.effect_itemid).trim(), eff.name)] });
    const gear = raceScript(r).gear.filter(i => items().has(i));
    if (gear.length) other.push({ kind: 'links', title: 'Equipment', items: gear.map(i => itemLink(i, items().get(i).name)) });
    const same = pets().filter(o => o !== p && o.name === p.name);
    if (same.length) other.push({ kind: 'links', title: 'Other pets with this name', items: same.map(o => ({ t: 'pet', id: o.id, label: `${o.name} (${PET_GROUP[o.group][2]})` })) });
    // A seal's fishing help.
    const fishing = fishingStatsSection((fishingGearData().pets || {})[String(p.id)]);
    if (fishing) sections.push(fishing);
    if (other.length) sections.push({ heading: 'Related', blocks: other });
    return { title: p.name, subtitle: p.local || null, icon: null, category: [p.cat], infobox: info, summary: '', sections, tags: headTags('pets', p.t) };
  }
  // A page's tags under its title (page.tags): each one in words, linking to its list filtered by it
  // (the tag kept as a keyword).
  const headTags = (route, tags) => [...new Set((tags || []).filter(Boolean))].map(x => ({ href: `#/${route}?k=` + encodeURIComponent(String(x).toLowerCase()), label: words(x).toLowerCase() }));
  // An item page's pet links: what it summons, or the pets a kit or potion lists.
  function itemPetBlocks(id, x) {
    const e = petItems().byItem.get(String(id));
    const out = [];
    if (e) out.push({ kind: 'links', title: e.kind === 'summon' ? 'Summons' : 'Pets', items: petChips(e.pets) });
    return out;
  }

  // ---------- search subtitles for shared names ----------
  // Three monsters named "Gray Wolf" would show alike in search. Where a type has more than one entry
  // of a name, each one's search subtitle adds what tells them apart: the first of the type's details
  // below that tells more of them apart than the subtitle does, then the next, until all differ (or
  // none left). An entry whose name is its own keeps its list subtitle.
  // An internal name (ClassName, NPC GeneralName, dungeon id) as the words that differ: the entry's
  // name and the words they all start or end with are dropped (Tutorial_GrayWolf → "Tutorial").
  // Where that leaves two alike, theirs is the whole internal name in words.
  function distinctWords(strs, name) {
    // underscores and camel case split; digits stay on their word (g2jump, event1)
    const split = s => String(s || '').replace(/_/g, ' ').replace(/(?<=[a-z])(?=[A-Z])/g, ' ').replace(/\s+/g, ' ').trim();
    const nameT = new Set(String(name || '').toLowerCase().split(/[^a-z0-9]+/).filter(Boolean));
    const compact = String(name || '').toLowerCase().replace(/[^a-z0-9]/g, '');
    const toks = strs.map(s => split(s).split(' ').filter(Boolean).map(w => {
      const l = w.toLowerCase();
      if (nameT.has(l) || l === compact) return '';
      return compact && l.startsWith(compact) ? w.slice(compact.length) : w; // lornaevent1 → event1
    }).filter(Boolean));
    const same = at => toks.every(t => t.length && toks[0].length && t[at(t)].toLowerCase() === toks[0][at(toks[0])].toLowerCase());
    while (same(() => 0)) for (const t of toks) t.shift();
    while (same(t => t.length - 1)) for (const t of toks) t.pop();
    const out = toks.map(t => t.join(' ')), n = {};
    for (const s of out) n[s] = (n[s] || 0) + 1;
    return out.map((s, i) => n[s] > 1 && strs[i] ? split(strs[i]) : s);
  }
  // The values that differ from the group's most common one (that one left blank): a family shown
  // only on the odd one out.
  const oddOnes = vals => {
    const n = {};
    for (const v of vals) if (v) n[v] = (n[v] || 0) + 1;
    const top = Object.keys(n).sort((a, b) => n[b] - n[a])[0];
    return vals.map(v => (v === top ? '' : v));
  };
  // A quest's first reward other than exp, in words (two quests alike but for what they give).
  function questFirstReward(id) {
    const x = ((questById(id) || {}).r || []).find(w => w.kind !== 'exp');
    if (!x) return '';
    if (x.kind === 'gold') return x.value ? `${x.value} Gold` : '';
    if (x.item) { const it = items().get(String(x.item)); return it ? (x.count > 1 ? `${x.count} × ` : '') + it.name : ''; }
    return '';
  }
  // Per type: each detail takes the group (the INDEX rows sharing a name) and gives one value a row.
  // The list subtitle comes first: a monster's variant (Boss, Hard mode), an NPC's town, a quest's
  // category, a dungeon's altar region.
  const SEARCH_DETAIL = {
    monster: [
      g => oddOnes(g.map(r => MON_CAT_TITLE[(monsterById(r.id) || {}).cat])),
      g => distinctWords(g.map(r => ((monsterById(r.id) || {}).r || {}).ClassName), g[0].name),
    ],
    npc: [g => distinctWords(g.map(r => (npcById(r.id) || {}).gn), g[0].name)],
    quest: [
      g => g.map(r => {
        if (r.given) return 'From ' + r.given;
        const v = (questById(r.id) || {}).gv || [];
        return v.some(x => x.fishing) ? 'Fishing' : v.some(x => x.owl) ? 'By owl' : v.some(x => x.item) ? 'From an item' : '';
      }),
      g => g.map(r => questFirstReward(r.id)),
    ],
    dungeon: [g => distinctWords(g.map(r => r.id), g[0].name)],
    prop: [g => g.map(r => r.where && r.where.replace(/ \+\d+$/, ''))],
  };
  // The rows of one type whose search subtitle differs from their list one: Map(row → subtitle).
  function searchSubs(t, list) {
    const out = new Map(), details = SEARCH_DETAIL[t];
    if (!details) return out;
    const groups = new Map();
    for (const r of list) {
      const k = String(r.name || '').toLowerCase();
      if (!groups.has(k)) groups.set(k, []);
      groups.get(k).push(r);
    }
    const kinds = parts => new Set(parts.map(p => p.join('\u0001').toLowerCase())).size;
    for (const g of groups.values()) {
      if (g.length < 2) continue;
      let parts = g.map(r => (r.sub ? [String(r.sub)] : []));
      for (const d of details) {
        if (kinds(parts) === g.length) break;
        const vals = d(g);
        const next = parts.map((p, i) => {
          const v = vals[i] == null ? '' : String(vals[i]).trim();
          return v && !p.some(x => x.toLowerCase() === v.toLowerCase()) ? p.concat([v]) : p;
        });
        if (kinds(next) > kinds(parts)) parts = next;
      }
      g.forEach((r, i) => { const s = parts[i].join(' · '); if (s !== (r.sub || '')) out.set(r, s); });
    }
    return out;
  }

  // ---------- the source app.js reads ----------
  function catRows(list) {
    const counts = {};
    for (const x of list) {
      const keys = new Set();
      for (const c of [].concat(x.cat)) for (let k = c; k; k = k.indexOf('/') > 0 ? k.slice(0, k.lastIndexOf('/')) : '') keys.add(k);
      for (const k of keys) counts[k] = (counts[k] || 0) + 1;
    }
    return counts;
  }
  let META = null, INDEX = null;
  function build() {
    if (META) return;
    const its = Array.from(items().entries()), ens = enchants(), tis = titles(), sks = skills();
    INDEX = {
      // Items with their stat columns (itemStats; none for test items, so they don't rank).
      item: its.map(([id, it]) => Object.assign({
        id, name: it.name, alt: it.local, icon: itemIcon(id), cat: it.cat,
        sub: ITEM_CAT_TITLE[it.cat], price: num(it.r.Price_Buy) || null, size: `${num(it.r.Inv_XSize) || 1}×${num(it.r.Inv_YSize) || 1}`,
        tags: [it.base].concat(Array.from(it.tags)).join(' '),
      }, testItem(it) ? {} : listStats(itemStats(it)), setsOf(id))),
      // Enchants carry their effects: fx (shown in the list and in search results, and searched:
      // "max damage", "fire alchemy") and effects (each stat's name, for the Effect filter).
      // The list's columns: what it goes on, its effects (a bonus blue, a penalty red, each with its
      // condition first) and where its scroll comes from; onto: the kinds of equipment, for the filter.
      // Exclusions most enchants share (bolts, arrows, robes...) aren't repeated in the list; the
      // enchant's page lists them all.
      enchant: ens.map(e => {
        const u = usageOf(e.r), fx = effects(e.r.OptionList), allow = patterns(e.r.AllowItem), block = patterns(e.r.BlockItem).filter(b => !commonBlocks().has(b));
        const rc = num(e.r.RepairCostRate);
        // A line's pieces: the condition (its skill or title linked), then the effect (a set effect or a
        // chain-cast skill linked); the plain text in the bonus/penalty colour.
        const fxLines = fx.map(f => {
          const tone = /^\s*-/.test(String(f[1])) ? 'neg' : 'pos', tx = s => ({ text: s, tone });
          const parts = c => (c && c.lines ? c.lines[0].map(p => (typeof p === 'string' ? tx(p) : p)) : [tx(String(c))]);
          const what = f[3] && setElement(f[3]) ? [{ t: 'set_effect', id: setElement(f[3]).id, label: f[0] }] : parts(f.cell || f[0]);
          return (f[2] ? parts(f[2]).concat([tx(': ')]) : []).concat(what, f[1] ? [tx(' ' + f[1])] : []);
        }).concat(rc ? [[{ text: `Repair cost ×${+(1 + rc / 100).toFixed(2)}`, tone: rc > 0 ? 'neg' : 'pos' }]] : []);
        const src = sourcesOf('sets', e.id).sort((a, b) => (a.c != null) - (b.c != null) || (b.c || 0) - (a.c || 0));
        const seen = new Set(), from = [];
        for (const s of src) { const ref = sourceRef(s), k = cellText(ref); if (k && k !== '—' && !seen.has(k)) { seen.add(k); from.push(ref); } }
        return { id: e.id, name: e.name, alt: e.alt, icon: null, cat: e.cat, sub: (e.rank ? 'Rank ' + e.rank + ' ' : '') + u[2].toLowerCase(), rank: e.rank,
          tags: (e.r.Name || ''), fx: fx.map(fxText).join('\n'),
          onto: allow.length ? allow.join(', ') + (block.length ? ` (not ${block.join(', ')})` : '') : null, ontoKinds: ontoKinds(e.r.AllowItem),
          fxv: fxLines.length ? { lines: fxLines, sep: '' } : null,
          from: from.length ? { lines: from.slice(0, 3).concat(from.length > 3 ? [`and ${from.length - 3} more`] : []), sep: '' } : null };
      }),
      title: tis.map(t => ({ id: t.id, name: t.name, icon: null, cat: t.cat, sub: (TITLE_CATS.find(c => c[0] === t.cat) || ['', '', ''])[2], stats: t.fx.length ? fxLines(t.fx.map(f => `${f[0]} ${f[1]}`.trim())) : null })),
      skill: sks.map(s => ({ id: s.id, name: s.name, alt: s.local, icon: skillIcon(s.id), cat: s.cat, sub: SKILL_CAT_TITLE[s.cat] + ' skill',
        tags: [s.r.SkillEngName, s.r.DescName].filter(Boolean).join(' ') })),
      monster: monsters().map(m => ({ id: m.id, name: m.name, alt: m.local, icon: null, cat: [m.cat].concat(m.boss ? ['boss'] : []),
        sub: m.variant, hostile: m.hostile ? 'Hostile' : 'Peaceful', kind: m.boss ? 'Boss' : 'Monster', skills: raceScript(m.r).skills.size || null,
        tags: [m.r.EnglishName, m.r.ClassName, m.fam].concat(m.t).filter(Boolean).join(' ') })),
    };
    const upgs = upgrades(), sets = [...setElements().values()], nps = npcs();
    INDEX.upgrade = upgs.map(u => ({ id: u.id, name: u.name, alt: u.local, icon: null, cat: upgradeCat(u), sub: u.fx.map(cellText).join(', '),
      ep: num(u.r.need_ep), gold: num(u.r.need_gold), steps: u.steps, tags: [u.r.name].concat(u.npcs).join(' ') }))
      // Artisan upgrades (optionset Usage 8, the results an artisan upgrade rolls) are listed with the
      // upgrades, not the enchants; their pages are the enchant pages, served under upgrade.
      .concat(INDEX.enchant.filter(e => e.cat === 'artisan').map(e => ({ id: e.id, name: e.name, alt: e.alt, icon: null, cat: 'artisan', sub: (e.fx || '').replace(/\n/g, ', '), tags: e.tags })));
    INDEX.enchant = INDEX.enchant.filter(e => e.cat !== 'artisan');
    INDEX.set_effect = sets.map(e => {
      const n = setItemIds(e.id).filter(i => items().has(i)).length, en = setEnchants(e.id).length;
      return { id: e.id, name: e.name, icon: null, cat: e.penalty ? 'penalty' : 'bonus', sub: e.th.length ? 'Activates at ' + thresholdText(e) : 'Set effect',
        at: e.th.length ? e.th[0].pts : null, items: n || null, enchants: en || null, tags: e.id };
    });
    INDEX.npc = nps.map(x => ({ id: x.id, name: x.name, icon: npcFace(x.id), cat: x.cats,
      sub: x.town || (NPC_CATS.find(c => c[0] === x.cats[0]) || ['', ''])[1], town: x.town || null,
      upgrades: (upgradesByNpc().get(x.id) || []).length || null, tags: x.gn }));
    INDEX.pet = pets().map(p => ({ id: p.id, name: p.name, alt: p.local, icon: null, cat: p.cat, group: PET_GROUP[p.group][1],
      sub: petTraits(p).slice(1, 4).join(' · '), seats: isPetMount(p) ? petSeats(p) : null,
      skills: (petSkillRows().get(p.id) || []).filter(s => skillById(s.Skill_ID)).length || null,
      tags: [p.r.EnglishName, p.r.ClassName, p.fam].concat(p.t).filter(Boolean).join(' ') }));
    INDEX.craft = craftRows();
    INDEX.manual = manualRows();
    INDEX.quest = [...quests().values()].map(q => {
      const g = (q.gv || []).find(x => x.npc && npcById(x.npc));
      return { id: q.id, name: q.name, icon: null, cat: q.c, sub: QUEST_CAT_TITLE[q.c] || 'Quest', given: g ? npcById(g.npc).name : null,
        exp: questSum(q, 'exp'), gold: questSum(q, 'gold'), tags: q.nj || '' };
    });
    INDEX.dungeon = [...dungeons().values()].map(d => {
      const boss = (d.b || [])[0], bm = boss && boss.m[0] && monsterById(boss.m[0][0]);
      return { id: d.id, name: d.name, icon: null, cat: dungeonCat(d), sub: dungeonRegion(d) || DUNGEON_CATS.find(c => c[0] === dungeonCat(d))[1],
        region: dungeonRegion(d) || null, floors: (d.f || []).length || null, boss: bm ? bm.name : null, tags: d.id };
    });
    INDEX.fishing = [...fishingGrounds().values()].map(g => ({ id: g.id, name: g.name, icon: null, cat: g.k, sub: groundPlaces(g) || (FISHING_CATS.find(c => c[0] === g.k) || ['', 'Fishing'])[1],
      where: groundPlaces(g) || null, catches: g.c.length, tags: g.id }));
    INDEX.prop = [...propPages().values()].map(p => ({ id: p.id, name: p.name, icon: null, cat: propCat(p), sub: PROP_CATS.find(c => c[0] === propCat(p))[1],
      where: p.w.length ? propRegion(p.w[0][0]) + (p.w.length > 1 ? ` +${p.w.length - 1}` : '') : null, gives: [...new Set(p.r.map(r => r[0]))].length, tags: p.ids.join(' ') }));
    INDEX.mission = Object.keys(missionData()).map(missionById).map(m => ({ id: m.id, name: m.name, icon: null, cat: missionCat(m), sub: missionWhere(m) || (m.t === 'shadow' ? 'Shadow Mission' : 'Theatre Mission'),
      where: missionWhere(m), party: m.p ? (m.p[0] === m.p[1] ? String(m.p[0]) : `${m.p[0]}–${m.p[1]}`) : null, exp: (m.df || [])[0] ? m.df[0].x || null : null }));
    INDEX.region = Object.entries(regionData()).filter(([, r]) => r.n).map(([id, r]) => ({ id, name: r.n, icon: null, cat: regionCat(r), sub: [r.tw ? 'Town' : REGION_KIND[r.k], r.a].filter(Boolean).join(' · '),
      area: r.a || null, npcs: (r.np || []).filter(n => !n[4]).length || null, monsters: new Set((r.s || []).map(s => s[0])).size || null, map: minimapOf(id) ? 'Yes' : null, tags: r.a || '' }));
    INDEX.tool = TOOLS.map(x => ({ id: x.id, name: x.name, icon: null, cat: x.cat, sub: x.sub }));
    INDEX.guide = GUIDES.map(x => ({ id: x.id, name: x.name, icon: null, cat: x.cat, sub: x.sub }));
    // Trade-ins: what NPCs take in for rewards, by the NPC's town.
    // A trade-in is found by its other names too (aka: the event that earns its items), shown beside it.
    INDEX.tradein = turnins().map(x => { const n = npcById(x.npc); return { id: x.k, name: x.title, alt: (x.aka || []).join(' · '), icon: null, cat: slug(tiTown(x)), sub: n ? n.name : '', npc: n ? n.name : null, items: x.i.length,
      tags: [n ? n.name : '', ...(x.aka || []), ...x.i.flatMap(r => r[0].map(g => (items().get(g[0]) || {}).name || ''))].join(' ') }; });
    INDEX.questline = questChains().map((c, i) => ({ id: c.id, name: c.t, icon: null, cat: qlGroup(c.g), sub: c.g, quests: new Set(c.st.map(s => s[0])).size, order: i,
      tags: c.st.map(s => s[1]).join(' ') }));
    // How many rows each item and enchant category holds (a parent counts its children's rows too).
    const itemCounts = catRows(INDEX.item), enchantCounts = catRows(INDEX.enchant);
    // A type's categories in the order given, leaving out the empty ones: each with its key, title,
    // parent (the definition's third entry when the type nests its categories, else none), place in
    // the list and how many rows it holds.
    const catList = (defs, type, nested) => {
      const counts = catRows(INDEX[type]);
      return defs.filter(c => counts[c[0]]).map((c, i) => ({ key: c[0], title: c[1], parent: nested ? c[2] : '', order: i, count: counts[c[0]] }));
    };
    const SET_CATS = [['bonus', 'Bonuses'], ['penalty', 'Penalties']];
    // NPC categories: the towns (in name order) under "Towns", then the rest.
    const towns = [...new Set(nps.map(x => x.town).filter(Boolean))].sort().map(t => ['town/' + slug(t), t, 'town']);
    const npcCats = [NPC_CATS[0]].concat(towns, NPC_CATS.slice(1));
    const cats = {
      // The groups (each item is in one top category, so a group's count is the sum of its own), then
      // the categories under them.
      item: ITEM_GROUPS.map(g => ({ key: g[0], title: g[1], parent: '', count: g[2].reduce((n, k) => n + (itemCounts[k] || 0), 0), first: g[0] === 'g/equip' || undefined }))
        .filter(g => g.count).concat(ITEM_CATS.filter(c => itemCounts[c[0]]).map(c => ({ key: c[0], title: c[1], parent: c[2] || itemGroupOf(c[0]), count: itemCounts[c[0]] })))
        .map((c, i) => Object.assign(c, { order: i })),
      // Prefixes and Suffixes first (what most people look for), a line under them, then the rest A-Z.
      enchant: Object.values(USAGE).concat([OTHER_USAGE]).filter(u => enchantCounts[u[0]]).map((u, i) => Object.assign({ key: u[0], title: u[1], parent: '', order: i, count: enchantCounts[u[0]] },
        u[0] === 'prefix' ? { first: true } : u[0] === 'suffix' ? { first: true, divider: true } : {})),
      upgrade: catList(ITEM_CATS.concat([['none', 'No matching items', ''], ['artisan', 'Artisan upgrades', '']]), 'upgrade', true),
      set_effect: catList(SET_CATS, 'set_effect'),
      title: catList(TITLE_CATS, 'title'),
      skill: catList(SKILL_CATS, 'skill'),
      monster: catList(MON_CATS, 'monster', true),
      npc: catList(npcCats, 'npc', true),
      pet: catList(PET_CATS, 'pet', true),
      craft: catList(CRAFT_CATS, 'craft'),
      manual: catList(MANUAL_CATS, 'manual'),
      quest: catList(QUEST_CATS, 'quest'),
      dungeon: catList(DUNGEON_CATS, 'dungeon'),
      fishing: catList(FISHING_CATS, 'fishing'),
      prop: catList(PROP_CATS, 'prop'),
      mission: catList(MISSION_CATS, 'mission'),
      region: catList(REGION_CATS, 'region'),
      tool: [{ key: 'spirit', title: 'Spirit Weapons Exp', parent: '', order: 0, page: 'spirit-weapon-exp', count: INDEX.tool.filter(x => x.cat === 'spirit').length }],
      guide: GUIDES.map((g, i) => ({ key: g.cat, title: g.name, parent: '', order: i, page: g.id, count: 1 })),
      questline: catList(QL_GROUPS, 'questline'),
      tradein: [...new Set(INDEX.tradein.map(r => r.cat))].map((key, i) => ({ key, title: tiTown(turnins().find(x => slug(tiTown(x)) === key)), parent: '', order: i, count: INDEX.tradein.filter(r => r.cat === key).length })),
    };
    const n = Object.values(INDEX).reduce((a, l) => a + l.length, 0);
    const T = (t, d) => Object.assign({ filters: [], count: INDEX[t].length, pages: INDEX[t].length }, d);
    META = {
      name: 'Mabinogi Wiki', badge: 'G13', searchHint: 'Search items, enchants, skills, monsters, NPCs, quests, dungeons…',
      tagline: "G13 Mabinogi's items, enchants, upgrades, crafting, skills, monsters, pets, NPCs, titles, quests, dungeons and fishing.",
      built: D ? D.built : '',
      // Made by this page (boot.js's setup, build-info.js builder 'page'), or before it.
      builtNote: HOSTED ? 'Made from the game\'s own files.' : window.MABIWIKI_BUILD && window.MABIWIKI_BUILD.builder === 'page'
        ? 'Made from your game\'s files on this computer. After a game patch, choose Update to make it again.'
        : 'Made from your game\'s own files.',
      sections: [
        // Five groups (the sidebar's headings and the home page's tile groups, homeMeta).
        ...NAV_GROUPS.map(([id, title, icon, types], i) => ({ id, title, order: 10 + i * 10, icon, types,
          count: types.reduce((a, t) => a + INDEX[t].length, 0) })),
      ],
      types: {
        item: T('item', { title: 'Items', route: 'items', view: Object.keys(ICONS).length ? 'grid' : 'table', columns: [{ key: 'price', title: 'Shop price', num: true }, { key: 'size', title: 'Size' }], section: 'items', order: 1 }),
        // Category (Prefix, Suffix...) before the name; no Info column (it'd repeat the rank and category).
        enchant: T('enchant', { title: 'Enchants', route: 'enchants', view: 'table', catFirst: true, noInfo: true,
          columns: [{ key: 'rank', title: 'Rank' }, { key: 'onto', title: 'Enchants onto' }, { key: 'fxv', title: 'Effects', wide: true }, { key: 'from', title: 'Comes from' }], section: 'items', order: 2 }),
        upgrade: T('upgrade', { title: 'Upgrades', route: 'upgrades', icon: 'hammer', view: 'table',
          columns: [{ key: 'ep', title: 'Proficiency', num: true }, { key: 'gold', title: 'Gold', num: true }, { key: 'steps', title: 'Steps' }], section: 'items', order: 3 }),
        set_effect: T('set_effect', { title: 'Set effects', route: 'set-effects', icon: 'gem', view: 'table',
          columns: [{ key: 'at', title: 'Activates at (points)', num: true }, { key: 'items', title: 'Items', num: true }, { key: 'enchants', title: 'Enchants', num: true }], section: 'items', order: 4 }),
        skill: T('skill', { title: 'Skills', route: 'skills', icon: 'zap', view: 'table', columns: [], section: 'skills', order: 1 }),
        // Its rows open the items' own pages (linkAs).
        craft: T('craft', { title: 'Craftables', route: 'crafting', icon: 'hammer', linkAs: 'item', view: Object.keys(ICONS).length ? 'grid' : 'table',
          columns: [{ key: 'skills', title: 'Skills' }, { key: 'recipes', title: 'Recipes', num: true }], section: 'skills', order: 2 }),
        // Difficulty sorts by its rank number; the Equipment filter is the made item's category.
        manual: T('manual', { title: 'Manuals & patterns', route: 'manuals', icon: 'scroll', view: 'table', noInfo: true,
          columns: [{ key: 'makes', title: 'Makes' }, { key: 'diff', title: 'Difficulty', sort: 'diffLv', card: true, short: '' }, { key: 'price', title: 'Price', num: true }, { key: 'sold', title: 'In a shop' }],
          filters: [{ key: 'equip', title: 'Equipment', options: [...new Set(INDEX.manual.map(r => r.equip).filter(Boolean))].sort().map(e => [e, e]) }], section: 'skills', order: 3 }),
        // No Info column: it only repeats the Category column.
        title: T('title', { title: 'Titles', route: 'titles', view: 'table', noInfo: true, columns: [{ key: 'stats', title: 'Effects' }], section: 'characters', order: 4 }),
        monster: T('monster', { title: 'Monsters', route: 'monsters', icon: 'skull', view: 'table',
          columns: [{ key: 'hostile', title: 'Disposition' }], section: 'characters', order: 1,
          filters: [{ key: 'kind', title: 'Kind', options: [['Monster', 'Monster'], ['Boss', 'Boss']] }, { key: 'hostile', title: 'Disposition', options: [['Hostile', 'Hostile'], ['Peaceful', 'Peaceful']] }] }),
        npc: T('npc', { title: 'NPCs', route: 'npcs', icon: 'user', view: 'table',
          columns: [], section: 'characters', order: 3 }),
        quest: T('quest', { title: 'Quests', route: 'quests', icon: 'scroll', view: 'table',
          columns: [{ key: 'given', title: 'Given by' }, { key: 'exp', title: 'Exp', num: true }, { key: 'gold', title: 'Gold', num: true }], section: 'quests', order: 1 }),
        dungeon: T('dungeon', { title: 'Dungeons', route: 'dungeons', icon: 'castle', view: 'table',
          columns: [{ key: 'floors', title: 'Floors', num: true }, { key: 'boss', title: 'Boss' }], section: 'world', order: 2 }),
        // No Info column: it only repeats Where.
        mission: T('mission', { title: 'Missions', route: 'missions', icon: 'moon', view: 'table', noInfo: true,
          columns: [{ key: 'where', title: 'Where' }, { key: 'party', title: 'Party' }], section: 'quests', order: 1.5 }),
        tool: T('tool', { title: 'Calculators', route: 'tools', icon: 'sliders', view: 'table', columns: [], section: 'tools', order: 1 }),
        guide: T('guide', { title: 'Guides', route: 'guides', icon: 'book', view: 'table', columns: [], section: 'misc', order: 1 }),
        // Questlines in play order within their group (the list keeps INDEX's order unless sorted).
        questline: T('questline', { title: 'Questlines', route: 'questlines', icon: 'scroll', view: 'table', noInfo: true, noId: true,
          columns: [{ key: 'quests', title: 'Quests', num: true }], section: 'quests', order: 2 }),
        // What NPCs take in for rewards (restored fossils, artifacts, Cor's villagers...), by town.
        tradein: T('tradein', { title: 'Trade-ins', route: 'trade-ins', icon: 'swap', view: 'table', noInfo: true, noId: true,
          columns: [{ key: 'npc', title: 'NPC' }, { key: 'items', title: 'Items', num: true }], section: 'quests', order: 3 }),
        // No Info column (here or in any region category).
        region: T('region', { title: 'Regions', route: 'regions', icon: 'map', view: 'table', noInfo: true,
          columns: [{ key: 'area', title: 'Area' }, { key: 'npcs', title: 'NPCs', num: true }, { key: 'monsters', title: 'Monsters', num: true }, { key: 'map', title: 'Map' }], section: 'world', order: 1 }),
        fishing: T('fishing', { title: 'Fishing', route: 'fishing', icon: 'map', view: 'table',
          columns: [{ key: 'where', title: 'Where' }, { key: 'catches', title: 'Catches', num: true }], section: 'world', order: 4 }),
        prop: T('prop', { title: 'Props', route: 'props', icon: 'cube', view: 'table',
          columns: [{ key: 'where', title: 'Where' }, { key: 'gives', title: 'Items', num: true }], section: 'world', order: 5 }),
        pet: T('pet', { title: 'Pets', route: 'pets', icon: 'paw', view: 'table',
          columns: [{ key: 'group', title: 'Group' }, { key: 'seats', title: 'Passengers', num: true }],
          section: 'characters', order: 2, filters: [{ key: 'group', title: 'Group', options: PET_GROUPS.map(g => [g[1], g[1]]) }] }),
      },
      categories: cats,
      stats: { pages: n, entries: n },
    };
    // The item lists' stat columns and filters per category.
    const hitsSeen = [...new Set(INDEX.item.map(r => r.hits).filter(h => h !== undefined))].sort((a, b) => a - b);
    // Weapons filter by kind of attack; damage and critical are columns to sort by, not filters.
    const MELEE_F = [{ key: 'speed', title: 'Speed', options: SPEEDS.map(s => [s, s]) },
      { key: 'hits', title: 'Combo', options: hitsSeen.map(h => [String(h), h + '-hit']) }];
    // Bows, crossbows, atlatls, defensive gear, ammunition and instruments have no filters: their stats
    // are columns to sort by. (A ranged weapon's empty list keeps the melee filters off it.)
    META.types.item.catColumns = ITEM_COLS;
    META.types.item.catFilters = { weapon: MELEE_F, 'weapon/bow': [], 'weapon/crossbow': [], 'weapon/atlatl': [],
      food: [MIN('hunger', 'Fills hunger')] };
    // Theatre missions are all in Avon: their list has no Where column.
    META.types.mission.catColumns = { theatre: META.types.mission.columns.filter(c => c.key !== 'where') };
    // Enchants by rank (the home page's rank panel links to these).
    META.types.enchant.filters = [{ key: 'rank', title: 'Rank', options: ENCHANT_RANKS.map(r => [r, r]) },
      { key: 'ontoKinds', title: 'Equipment', any: 'All', options: ONTO_KINDS.map(k => [k[0], k[0]]).concat([[ANY_EQUIP, ANY_EQUIP]]) }];
    META.home = homeMeta(cats, n);
  }

  // ---------- the home page (meta.home) ----------
  const ENCHANT_RANKS = ['F', 'E', 'D', 'C', 'B', 'A', '9', '8', '7', '6', '5', '4', '3', '2', '1'];
  // ---------- tools ----------
  const TOOLS = [{ id: 'spirit-weapon-exp', name: 'Spirit Weapon EXP Calculator', cat: 'spirit', sub: 'How much EXP an item or gem gives a spirit weapon' }];
  // The guides (their own type, under Misc in the sidebar): one page each, a category per page.
  const GUIDES = [{ id: 'elements', name: 'Elements', cat: 'elements', sub: 'What a monster\'s elements mean, and how to give your equipment an element' },
    { id: 'npc-friendship', name: 'NPC Friendship', cat: 'friendship', sub: 'How NPCs come to like you, what it unlocks, and the fastest way there' },
    { id: 'fire-arrows', name: 'Fire Arrows', cat: 'fire-arrows', sub: 'Arrows lit at a campfire: how to light them, what they do, and against which monsters they help' }];
  // Spirit weapon feeding (the server's feed code and db/egoweapon, traced in the G13 server).
  const EGO_STATS = ['Str', 'Int', 'Dex', 'Will', 'Luck', 'Social'];
  const EGO_WEAPONS = ['Blunt Weapon', 'Bow', 'Cylinder', 'Sword', 'Wand'];
  // The spirit's type (egodesc.xml Type): 1-8 sword, blunt, wand, bow (male, female); 15-16 the
  // cylinder (only in this client's egodesc.xml).
  const EGO_TYPE = { Male: { Sword: 1, 'Blunt Weapon': 3, Wand: 5, Bow: 7, Cylinder: 15 }, Female: { Sword: 2, 'Blunt Weapon': 4, Wand: 6, Bow: 8, Cylinder: 16 } };
  // The server's numbers (G13 GameSystemD / StandardD and db/egoweapon): a stat at level L needs
  // 4L³ − 39L² + 177L − 82 EXP for the next (StandardD 0x101d24ee), up to level 50; efficiency grades
  // from egodesc.xml (S 1.6, A 1.333, B 1, C 0.8; Social B everywhere, items give it a fifth); the
  // gem tables from egofeed.xml (the newer rows, 1.2 times the old; Cylinder: not in G13, the
  // community's choice of rows with the same values); growth by mood (desire 0-100, 5 per stage).
  const EGO = {
    maxLevel: 50,
    need: L => 4 * L ** 3 - 39 * L ** 2 + 177 * L - 82,
    mult: {
      Male: { 'Blunt Weapon': [1.333, 1, 0.8, 1.333, 0.8], Bow: [1, 1.333, 1.6, 0.8, 0.8], Cylinder: [1, 1, 1, 0.8, 1.333], Sword: [1, 1.333, 1, 1, 0.8], Wand: [0.8, 1.333, 1.333, 0.8, 1.333] },
      Female: { 'Blunt Weapon': [1.333, 0.8, 0.8, 1.333, 1], Bow: [1, 1, 1.6, 0.8, 1], Cylinder: [1, 1, 1.333, 0.8, 1], Sword: [1, 1, 0.8, 1, 0.8], Wand: [1, 1.333, 1, 1, 1] } },
    moods: [['Fine (stage 1)', 20], ['Fine (stages 2–3)', 70], ['Unhappy (stages 1–4)', 100], ['Unhappy (stages 5–6)', 80], ['Mad (stages 1–8)', 50], ['Mad (stages 9–10)', 0]],
    moodPoints: p => (p >= 4000 ? 100 : p >= 3000 ? 80 : p >= 2000 ? 60 : p >= 1000 ? 40 : 20),
  };
  const EGO_GEMS = {
    Male: {
      Sword: {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Topaz: [96, 0, 96, 0, 0, 38], 'Star Sapphire': [76, 76, 76, 0, 0, 46],
        Emerald: [0, 0, 0, 154, 0, 30], Aquamarine: [0, 0, 0, 96, 96, 38], Garnet: [0, 38, 38, 58, 116, 50],
        Jasper: [0, 96, 0, 0, 96, 38], Ruby: [0, 96, 96, 0, 0, 38], Spinel: [154, 0, 0, 0, 0, 30],
        Diamond: [384, 384, 384, 384, 384, 384], Peridot: [85, 0, 85, 0, 0, 34], 'Fire Opal': [64, 64, 64, 0, 0, 38],
        Turquoise: [0, 0, 0, 128, 0, 25], Amethyst: [0, 0, 0, 85, 85, 34], 'Cat\'s Eye': [350, 350, 350, 350, 350, 350],
        'Lapis-Lazuli': [0, 32, 32, 48, 96, 41], 'Rose Quartz': [0, 85, 0, 0, 85, 34], Alexandrite: [0, 85, 85, 0, 0, 34],
        Spodumene: [0, 85, 85, 0, 0, 34], Andalusite: [128, 0, 0, 0, 0, 25],
      },
      'Blunt Weapon': {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Topaz: [154, 0, 0, 0, 0, 30], 'Star Sapphire': [96, 0, 96, 0, 0, 38],
        Emerald: [76, 76, 76, 0, 0, 46], Aquamarine: [0, 0, 0, 154, 0, 30], Garnet: [0, 0, 0, 96, 96, 38],
        Jasper: [0, 38, 38, 58, 116, 50], Ruby: [0, 96, 0, 0, 96, 38], Spinel: [0, 96, 96, 0, 0, 38],
        Diamond: [384, 384, 384, 384, 384, 384], Peridot: [128, 0, 0, 0, 0, 25], 'Fire Opal': [85, 0, 85, 0, 0, 34],
        Turquoise: [64, 64, 64, 0, 0, 38], Amethyst: [0, 0, 0, 128, 0, 25], 'Cat\'s Eye': [350, 350, 350, 350, 350, 350],
        'Lapis-Lazuli': [0, 0, 0, 85, 85, 34], 'Rose Quartz': [0, 32, 32, 48, 96, 41], Alexandrite: [0, 85, 0, 0, 85, 34],
        Spodumene: [0, 85, 0, 0, 85, 34], Andalusite: [0, 85, 85, 0, 0, 34],
      },
      Wand: {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Topaz: [0, 0, 0, 154, 0, 30], 'Star Sapphire': [0, 0, 0, 96, 96, 38],
        Emerald: [0, 38, 38, 58, 116, 50], Aquamarine: [0, 96, 0, 0, 96, 38], Garnet: [0, 96, 96, 0, 0, 38],
        Jasper: [154, 0, 0, 0, 0, 30], Ruby: [96, 0, 96, 0, 0, 38], Spinel: [76, 76, 76, 0, 0, 46],
        Diamond: [384, 384, 384, 384, 384, 384], Peridot: [0, 0, 0, 128, 0, 25], 'Fire Opal': [0, 0, 0, 85, 85, 34],
        Turquoise: [0, 32, 32, 48, 96, 41], Amethyst: [0, 85, 0, 0, 85, 34], 'Cat\'s Eye': [350, 350, 350, 350, 350, 350],
        'Lapis-Lazuli': [0, 85, 85, 0, 0, 34], 'Rose Quartz': [128, 0, 0, 0, 0, 25], Alexandrite: [85, 0, 85, 0, 0, 34],
        Spodumene: [85, 0, 85, 0, 0, 34], Andalusite: [64, 64, 64, 0, 0, 38],
      },
      Bow: {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Topaz: [76, 76, 76, 0, 0, 46], 'Star Sapphire': [0, 0, 0, 154, 0, 30],
        Emerald: [0, 0, 0, 96, 96, 38], Aquamarine: [0, 38, 38, 58, 116, 50], Garnet: [0, 96, 0, 0, 96, 38],
        Jasper: [0, 96, 96, 0, 0, 38], Ruby: [154, 0, 0, 0, 0, 30], Spinel: [96, 0, 96, 0, 0, 38],
        Diamond: [384, 384, 384, 384, 384, 384], Peridot: [64, 64, 64, 0, 0, 38], 'Fire Opal': [0, 0, 0, 128, 0, 25],
        Turquoise: [0, 0, 0, 85, 85, 34], Amethyst: [0, 32, 32, 48, 96, 41], 'Cat\'s Eye': [350, 350, 350, 350, 350, 350],
        'Lapis-Lazuli': [0, 85, 0, 0, 85, 34], 'Rose Quartz': [0, 85, 85, 0, 0, 34], Alexandrite: [128, 0, 0, 0, 0, 25],
        Spodumene: [128, 0, 0, 0, 0, 25], Andalusite: [85, 0, 85, 0, 0, 34],
      },
      Cylinder: {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Garnet: [154, 0, 0, 0, 0, 30], Jasper: [76, 76, 76, 0, 0, 46],
        Ruby: [0, 0, 0, 96, 96, 38], Spinel: [96, 0, 96, 0, 0, 38], Topaz: [0, 38, 38, 58, 116, 50],
        'Star Sapphire': [0, 96, 0, 0, 96, 38], Emerald: [0, 0, 0, 154, 0, 30], Aquamarine: [0, 96, 96, 0, 0, 38],
        Diamond: [384, 384, 384, 384, 384, 384],
      },
    },
    Female: {
      Sword: {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Topaz: [0, 96, 0, 0, 96, 38], 'Star Sapphire': [0, 96, 96, 0, 0, 38],
        Emerald: [154, 0, 0, 0, 0, 30], Aquamarine: [96, 0, 96, 0, 0, 38], Garnet: [76, 76, 76, 0, 0, 46],
        Jasper: [0, 0, 0, 154, 0, 30], Ruby: [0, 0, 0, 96, 96, 38], Spinel: [0, 38, 38, 58, 116, 50],
        Diamond: [384, 384, 384, 384, 384, 384], Peridot: [0, 85, 0, 0, 85, 34], 'Fire Opal': [0, 85, 85, 0, 0, 34],
        Turquoise: [128, 0, 0, 0, 0, 25], Amethyst: [85, 0, 85, 0, 0, 34], 'Cat\'s Eye': [350, 350, 350, 350, 350, 350],
        'Lapis-Lazuli': [64, 64, 64, 0, 0, 38], 'Rose Quartz': [0, 0, 0, 128, 0, 25], Alexandrite: [0, 0, 0, 85, 85, 34],
        Spodumene: [0, 0, 0, 85, 85, 34], Andalusite: [0, 32, 32, 48, 96, 41],
      },
      'Blunt Weapon': {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Topaz: [0, 38, 38, 58, 116, 50], 'Star Sapphire': [0, 96, 0, 0, 96, 38],
        Emerald: [0, 96, 96, 0, 0, 38], Aquamarine: [154, 0, 0, 0, 0, 30], Garnet: [96, 0, 96, 0, 0, 38],
        Jasper: [76, 76, 76, 0, 0, 46], Ruby: [0, 0, 0, 154, 0, 30], Spinel: [0, 0, 0, 96, 96, 38],
        Diamond: [384, 384, 384, 384, 384, 384], Peridot: [0, 32, 32, 48, 96, 41], 'Fire Opal': [0, 85, 0, 0, 85, 34],
        Turquoise: [0, 85, 85, 0, 0, 34], Amethyst: [128, 0, 0, 0, 0, 25], 'Cat\'s Eye': [350, 350, 350, 350, 350, 350],
        'Lapis-Lazuli': [85, 0, 85, 0, 0, 34], 'Rose Quartz': [64, 64, 64, 0, 0, 38], Alexandrite: [0, 0, 0, 128, 0, 25],
        Spodumene: [0, 0, 0, 128, 0, 25], Andalusite: [0, 0, 0, 85, 85, 34],
      },
      Wand: {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Topaz: [0, 0, 0, 96, 96, 38], 'Star Sapphire': [0, 38, 38, 58, 116, 50],
        Emerald: [0, 96, 0, 0, 96, 38], Aquamarine: [0, 96, 96, 0, 0, 38], Garnet: [154, 0, 0, 0, 0, 30],
        Jasper: [96, 0, 96, 0, 0, 38], Ruby: [76, 76, 76, 0, 0, 46], Spinel: [0, 0, 0, 154, 0, 30],
        Diamond: [384, 384, 384, 384, 384, 384], Peridot: [0, 0, 0, 85, 85, 34], 'Fire Opal': [0, 32, 32, 48, 96, 41],
        Turquoise: [0, 85, 0, 0, 85, 34], Amethyst: [0, 85, 85, 0, 0, 34], 'Cat\'s Eye': [350, 350, 350, 350, 350, 350],
        'Lapis-Lazuli': [128, 0, 0, 0, 0, 25], 'Rose Quartz': [85, 0, 85, 0, 0, 34], Alexandrite: [64, 64, 64, 0, 0, 38],
        Spodumene: [64, 64, 64, 0, 0, 38], Andalusite: [0, 0, 0, 128, 0, 25],
      },
      Bow: {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Topaz: [0, 96, 96, 0, 0, 38], 'Star Sapphire': [154, 0, 0, 0, 0, 30],
        Emerald: [96, 0, 96, 0, 0, 38], Aquamarine: [76, 76, 76, 0, 0, 46], Garnet: [0, 0, 0, 154, 0, 30],
        Jasper: [0, 0, 0, 96, 96, 38], Ruby: [0, 38, 38, 58, 116, 50], Spinel: [0, 96, 0, 0, 96, 38],
        Diamond: [384, 384, 384, 384, 384, 384], Peridot: [0, 85, 85, 0, 0, 34], 'Fire Opal': [128, 0, 0, 0, 0, 25],
        Turquoise: [85, 0, 85, 0, 0, 34], Amethyst: [64, 64, 64, 0, 0, 38], 'Cat\'s Eye': [350, 350, 350, 350, 350, 350],
        'Lapis-Lazuli': [0, 0, 0, 128, 0, 25], 'Rose Quartz': [0, 0, 0, 85, 85, 34], Alexandrite: [0, 32, 32, 48, 96, 41],
        Spodumene: [0, 32, 32, 48, 96, 41], Andalusite: [0, 85, 0, 0, 85, 34],
      },
      Cylinder: {
        'Spirit\'s Blue Liquor': [0, 0, 0, 0, 0, 1200], Garnet: [0, 38, 38, 58, 116, 50], Jasper: [0, 96, 96, 0, 0, 38],
        Ruby: [96, 0, 96, 0, 0, 38], Spinel: [0, 96, 0, 0, 96, 38], Topaz: [76, 76, 76, 0, 0, 46],
        'Star Sapphire': [0, 0, 0, 154, 0, 30], Emerald: [154, 0, 0, 0, 0, 30], Aquamarine: [0, 0, 0, 96, 96, 38],
        Diamond: [384, 384, 384, 384, 384, 384],
      },
    },
  };
  const BLUE_LIQUOR = "Spirit's Blue Liquor";
  // Which stat an item raises (besides Social): a hash of its ID plus the spirit's type (GameSystemD
  // 0x67e24720, Thomas Wang's 64-bit hash), mod 5: Str, Int, Dex, Will, Luck.
  function egoItemStat(itemId, egoType) {
    const M = (1n << 64n) - 1n, sar = (k, n) => (BigInt.asIntN(64, k) >> BigInt(n)) & M;
    let k = BigInt(Number(itemId) + egoType) & M;
    k = (k + (~(k << 15n) & M)) & M; k ^= sar(k, 10); k = (k * 9n) & M; k ^= sar(k, 6);
    k = (k + (~(k << 11n) & M)) & M; k ^= sar(k, 16);
    return Number((k & 0xffffffffn) % 5n);
  }
  // The server's steps, each rounded down: the base, times the grade, times the mood (whole percent),
  // then (gems) times size^1.2; at most a tenth of the next level (not for Spirit's Blue Liquor).
  const f32 = Math.fround;
  const egoItemExp = (p, L, m, mood, social) => {
    if (p <= 1) return 0;
    const base = Math.trunc(f32(f32(Math.log10(p)) ** 4.2) * f32(Math.log10(L + 1)) * (social ? 1 / 5 : 1) * f32(1.2));
    return Math.trunc(Math.trunc(base * m) * mood / 100);
  };
  const egoGemExp = (size, v, m, mood) => Math.trunc(Math.trunc(Math.trunc(v * m) * mood / 100) * size ** 1.2);
  const egoCap = L => Math.floor(EGO.need(L) / 10);
  function toolPage(id) {
    const t = TOOLS.find(x => x.id === id);
    if (!t) return null;
    const moodRows = EGO.moods.map(([m, r]) => [m, r + '%']);
    // noMeta: no kind, id or category tags under the title (a tool page is one of its kind).
    return { title: t.name, subtitle: null, icon: null, category: [t.cat], kind: 'Calculator', noMeta: true, infobox: [], summary: t.sub + '.',
      sections: [
        { heading: 'Calculator', wide: true, blocks: [{ kind: 'embed', name: 'spiritCalc', plain: true }] },
        { heading: 'How it works', wide: true, blocks: [
          { kind: 'list', items: [
            'An item raises Social and one other stat. Which one is fixed for each item and kind of spirit (pick the item to see it). A gem raises every stat it lists.',
            'Item EXP = 1.2 × (log₁₀ price)^4.2 × log₁₀(stat level + 1), rounded down; then × the efficiency grade, rounded down; then × the mood\'s growth %, rounded down. Social gets a fifth of the base. The price is the item\'s shop price.',
            'Gem EXP = the gem\'s value for the stat × the grade, rounded down; × the mood\'s growth %, rounded down; × size^1.2 (size in cm), rounded down.',
            'One item or gem gives a stat at most a tenth of the EXP it needs for its next level (Spirit\'s Blue Liquor has no limit). A stat at level L needs 4L³ − 39L² + 177L − 82; the most is level 50, and a stat goes up at most one level per item.',
            'Efficiency grades: S 1.6, A 1.333, B 1, C 0.8, by weapon, gender and stat.'] },
          { kind: 'table', title: 'Growth by mood', columns: ['Mood', 'Growth'], rows: moodRows },
          { kind: 'note', text: 'A happy spirit won\'t take items. Each item also cheers it up by 20 (under 1,000 G) to 100 (4,000 G or more) points; a mood stage is 100 points. While you carry it, every 5 minutes there\'s a 50% chance its mood drops a stage.' },
          { kind: 'note', text: 'Cylinder spirits\' gem values follow the other spirits\' pattern; the game files don\'t list them.' }] },
        { heading: 'Feeding guide', wide: true, blocks: egoGuide() }] };
  }
  // A guide's page (GUIDES).
  function guidePage(id) {
    const g = GUIDES.find(x => x.id === id);
    return !g ? null : id === 'elements' ? elementsGuide(g) : id === 'npc-friendship' ? friendshipGuide(g) : id === 'fire-arrows' ? fireArrowGuide(g) : null;
  }
  // The elements guide: what a monster's make-up means (GAME_SOURCES.elements), the monsters with the
  // most of each element, and how equipment gets one. Equipment elements come from the elemental slot
  // (Par_BasicOption elemental(...)), one per item, which the elemental crystals fill; sigils, patching
  // kits and stat kits use the same slot.
  function elementsGuide(t) {
    const D = (summary, ...blocks) => ({ kind: 'details', summary, blocks });
    const L = (...items) => ({ kind: 'list', items });
    const all = Object.keys((window.GAME_SOURCES && window.GAME_SOURCES.elements) || {})
      .map(id => [monsterById(id), monsterElements(id)]).filter(([m, e]) => m && e);
    // Per element, the monsters that have it, most first.
    const byElement = i => all.map(([m, e]) => [m, (e.find(x => x[0] === ELEMENT_NAMES[i]) || [])[1] || 0]).filter(x => x[1] > 0)
      .sort((a, b) => b[1] - a[1] || a[0].name.localeCompare(b[0].name));
    const monsterRows = list => list.map(([m, p]) => [{ t: 'monster', id: m.id, label: m.name }, p + '%', elementsText(monsterElements(m.id))]);
    // A worked example: a monster with 3 ice (the Ice Sprite).
    const sprite = all.find(([m]) => m.name === 'Ice Sprite');
    const ex = sprite && elementOf(sprite[0].id);
    const lists = [1, 2, 3].map(i => [ELEMENT_NAMES[i], byElement(i)]).filter(([, l]) => l.length);
    const ench = enchantById(7066);
    return { title: t.name, subtitle: null, icon: null, category: [t.cat], kind: 'Guide', noMeta: true, infobox: [], summary: t.sub + '.',
      sections: [
        { heading: 'A monster\'s elements', wide: true, blocks: [
          L('Everything that fights, monsters and characters alike, has an element counted in ninths: Physical, Fire, Ice and Lightning. A monster\'s page shows how much Fire, Ice or Lightning it has out of 9 (Fire 33% is 3 of 9), or "Physical only".',
            'Most monsters are physical only. Sprites, elementals and many bosses carry some Fire, Ice or Lightning.',
            'Fire and Ice cancel each other in a monster: one listed with both keeps only the difference.'),
          { kind: 'table', title: 'Examples', columns: ['Monster', 'Elements'], rows: ['Gray Wolf', 'Fire Sprite', 'Ice Sprite', 'Lightning Sprite', 'Black Wizard']
            .map(n => all.find(([m]) => m.name === n)).filter(Boolean).map(([m, e]) => [{ t: 'monster', id: m.id, label: m.name }, elementsText(e)]) }] },
        { heading: 'How elements change damage', wide: true, blocks: [
          L('Every hit is multiplied by a rate that compares the attacker\'s element with the target\'s. An attack with no element (all physical) always does normal damage.',
            'The same elements cancel: Fire against a Fire monster loses as much Fire as the monster has, and the other way round.',
            'Fire is strong against Ice: each point of Fire counts double, up to the target\'s Ice.',
            'Ice is strong against Fire: each point of Ice counts double, up to the target\'s Fire. Ice also gains a third of the target\'s Lightning.',
            'Lightning gains a third of the target\'s Ice.',
            'Melee and normal ranged attacks use your own element, from your equipment\'s crystals. Spells use their own: Firebolt, Fireball and the other fire spells are all Fire, the ice spells all Ice, the lightning spells all Lightning, whatever you wear. An arrow you light at a campfire is mostly Fire and also does 1.5× damage.',
            'Your element is also your defence: the same rate decides how much a monster\'s elemental attacks hurt you, so crystals of its own element soften its hits.',
            'Each monster with an element has an Elements section on its page with the rate for each kind of attack and the crystals that hurt it most.'),
          ...(ex ? [{ kind: 'table', title: `Example: ${sprite[0].name} (${elementsText(sprite[1])})`, columns: ['Attack', 'Damage'], rows: [
            ['Melee, no crystals', pctOf(elementRate(gearElement(0, 0, 0), ex))],
            ['Melee, 3 Fire crystals', pctOf(elementRate(gearElement(3, 0, 0), ex))],
            ['Melee, 3 Fire + 1 Lightning', pctOf(elementRate(gearElement(3, 0, 1), ex))],
            ['Melee, 3 Ice crystals', pctOf(elementRate(gearElement(0, 3, 0), ex))],
            ...SPELL_ELEMENTS.slice(0, 3).map(([n, a]) => [n, pctOf(elementRate(a, ex))])] }] : [])] },
        { heading: 'Giving your equipment an element', wide: true, blocks: [
          L({ lines: [['Each piece of equipment has one elemental slot. Use a ', itemRef(62006), ', ', itemRef(62007), ' or ', itemRef(62008), ' on it to give it +1 of that element.']], sep: '' },
            { lines: [['A ', itemRef(62009), ', ', itemRef(62010), ' or ', itemRef(62011), ' gives +1 to every piece you\'re wearing at once.']], sep: '' },
            { lines: [[itemRef(62012), ' clears the slot.']], sep: '' },
            'An item holds one element at a time, so a full set of equipment can reach 9 of one element (8 with a two-handed weapon).',
            'Sigils, patching kits and stat kits use the same slot: an item can carry an element or one of those, not both, and putting an element on removes them.',
            'Some items can\'t take an element at all.'),
          // Aurora (suffix 7066): ice +2 and lightning -1, only while Lightning Shield is up.
          ...(ench ? [{ kind: 'links', title: 'The only enchant with an element (Ice +2, Lightning −1 while Lightning Shield is up)', items: [enchantLink(ench)] }] : []),
          { kind: 'note', text: 'Each crystal\'s page lists the monsters that drop it and where else it comes from.' }] },
        { heading: 'Monsters by element', wide: true, blocks: lists.map(([n, l]) => D(`${n}: ${l.length.toLocaleString('en-US')} monsters`,
          { kind: 'table', columns: ['Monster', n, 'All its elements'], rows: monsterRows(l) })) }] };
  }
  // The NPC friendship guide. How NPCs keep liking, memory and stress, every way they change, what they
  // unlock and the fastest way up: traced in the G13 server's scripts and code, the secret shops too.
  function friendshipGuide(t) {
    const L = (...items) => ({ kind: 'list', items });
    const shops = Object.entries((window.GAME_SOURCES && window.GAME_SOURCES.secretShops) || {}).filter(([, s]) => s.u[0] === 'affection')
      .map(([k]) => npcById(k)).filter(Boolean).map(n => ({ t: 'npc', id: n.id, label: n.name, icon: npcFace(n.id) }));
    return { title: t.name, subtitle: null, icon: null, category: [t.cat], kind: 'Guide', noMeta: true, infobox: [], summary: t.sub + '.',
      sections: [
        // Written from what a player sees: the numbers are hidden, but the face and the mood line shown when
        // a conversation starts give them away (GetFaceType, GetStateMessage: the tiredness lines first).
        { heading: 'How to read an NPC', wide: true, blocks: [
          L('Every NPC keeps three hidden numbers about you: how much they like you, how well they know you, and how tired they are of talking to you right now. The game never shows them, but their face and the line under their greeting do.'),
          { kind: 'table', columns: ['When a conversation starts, they…', 'Which means'], rows: [
            ['"want to stop talking" or "seem to have other business"', 'They\'re tired of talking. Leave them a minute or two (this line hides the others).'],
            ['"look at you favorably"', 'They like you a little: from here, talking raises their liking'],
            ['"have a good impression of you"', 'They like you well'],
            ['"like you a lot", and beam', 'They like you very much (the secret shop is a bit past this)'],
            ['give you "a friendly expression"', 'They know you well (shown only before they start liking you)'],
            ['greet you with "we meet often"', 'They know you fairly well'],
            ['look uncomfortable, sour or avoid your eyes', 'They dislike you']] },
          L('Your character remembers at most 15 NPCs. Meeting new ones makes the others forget you bit by bit, so stick to the NPCs you\'re working on.',
            'Tiredness wears off by itself after a minute or two. Liking and how well they know you each fade a little for every hour you\'re online.',
            'The first time you ever choose Talk with someone only introduces you: nothing counts until your second conversation with them (a gift before then is wasted too).') ] },
        { heading: 'What raises them', wide: true, blocks: [
          { kind: 'table', columns: ['What you do', 'Knows you', 'Liking', 'Tires them'], rows: [
            ['Greet them (Talk)', 'a little', '—', 'yes, a lot (only when they\'re fully rested)'],
            ['Private Story', 'a little, every time', 'a little, but only once they look at you favorably and aren\'t tired (and not after they like you well)', 'slightly'],
            ['Nearby Rumors', 'a little, every time', 'the same as Private Story', 'more than Private Story'],
            ['Any other keyword', '—', '—', 'yes'],
            ['A gift they love or like', '—', 'about +2 to +5 points', 'no'],
            ['A gift they dislike', '—', 'about −2 to −5 points', 'no'],
            ['Likeability Potion', 'yes', '+10 points', 'no'],
            ['Their part-time job (perfect / good / poor / fail)', '—', 'up a little / up a little / none / down a little (some NPCs are harsher)', 'no']] },
          L('Each NPC\'s page has a Gifts section with what they love, and a Friendship section with anything special about them.',
            'Some NPCs never warm up from talking; their page says so. Gifts and their part-time job still work.',
            'One gift per conversation (giving it ends the talk), no daily limit. A part-time job once a day.',
            'The old "give them 1 gold a few times" trick does nothing: they take the gold, but it adds no liking and doesn\'t make them less tired. It only seemed to work because those few gifts take about a minute, and a minute is what lets them rest.') ] },
        { heading: 'What it unlocks', wide: true, blocks: [
          { kind: 'table', columns: ['When', 'What happens'], rows: [
            ['They look at you favorably', 'Private Story and Nearby Rumors start raising liking'],
            ['They know you well and have a good impression of you', 'Friendlier lines; talking stops raising liking from here (use gifts)'],
            ['A while after they like you a lot (and they know you well)', 'Their secret shop opens, if they have one (the NPCs below)'],
            ['Around the same point', 'Their Valentine\'s or White Day gift'],
            ['Special NPCs', 'Story scenes and events; see each NPC\'s Friendship section']] },
          ...(shops.length ? [{ kind: 'links', title: 'NPCs with a secret shop for friends', items: shops }] : [])] },
        // Tiredness can only be read when a talk opens (the state line shows stress >8 / >12 before
        // anything else). It falls a step every 10 s: 9-12 is back under 9 within ~40 s, 13-15 within
        // ~70 s. Opening a talk only tires them when they're fully rested (the greeting's +5/+10).
        { heading: 'Checking how tired they are', wide: true, blocks: [
          L('You can only see it at one moment: when you choose Talk, the line under their greeting says how they feel. Nothing tells you again until the next conversation.'),
          { kind: 'table', columns: ['When you choose Talk, they…', 'What to do'], rows: [
            ['"seem to have other business"', 'Very tired. End the talk and come back in a bit over a minute.'],
            ['"want to stop talking"', 'Tired. End the talk and come back in about 40 seconds.'],
            ['say anything else', 'Fine. Go ahead.']] },
          L('Checking costs nothing while they\'re still a little tired. It only tires them when they\'re completely rested (after two minutes or more away): the greeting itself wears them out a bit, so take the first few picks of that talk slowly.',
            'Giving a gift ends the conversation, so the gift-then-Talk rhythm below checks on them every round for free.') ] },
        // The numbers, from the traced rules: Private Story gives +1 memory half the time and (in the
        // slightly-favorable tier) +1 liking half the time, and tires them a step half the time; they
        // recover a step every 10 s, so a pick every ~5 s holds steady. A loved gift is point 10 -> +5 or
        // +3 (about +4), no tiredness, one per conversation, no daily limit.
        { heading: 'Gifts or talking?', wide: true, blocks: [
          { kind: 'table', columns: ['', 'Liking', 'How well they know you', 'Tires them', 'Cost'], rows: [
            ['A gift they love', 'about +4 each (+3 or +5)', '—', 'no', 'the item; one per conversation'],
            ['A gift they like', 'about +2 each', '—', 'no', 'the item; one per conversation'],
            ['Likeability Potion', '+10', 'a lot (+4)', 'no', 'the potion'],
            ['Private Story', 'half the time +1 (about +1 per 2 picks), only once they look at you favorably', 'half the time +1', 'half the time a little', 'free, but needs pacing'],
            ['Their part-time job', 'up to +3', '—', 'no', 'once a day']] },
          L('Gifts raise liking fastest: about four points per loved gift, and you can give one, start a new talk and give another straight away. They never tire an NPC.',
            'Talking is free but slower: about one point of liking every two picks, and only between "look at you favorably" and "have a good impression of you". It\'s also the only free way to make them know you better, which the secret shop needs.',
            'So the best mix: gifts to get them started and to finish, talking in between where it counts twice (each pick can raise both liking and how well they know you).') ] },
        { heading: 'The fastest way', wide: true, blocks: [
          { kind: 'list', ordered: true, items: [
            'Get them to remember you: open their menu and choose Talk, end the conversation, then do it once more. The very first Talk only introduces you (the game makes its note of you but counts nothing yet); from the second one on, everything counts. Meanwhile, don\'t meet lots of new NPCs: you\'re only remembered by 15 at a time.',
            'Give gifts they love until they "look at you favorably" (two or three usually do it, or one Likeability Potion). Before that, talking raises nothing but how well they know you, so it\'s better spent later.',
            'Now choose Talk and pick Private Story at an easy pace, about one every 5 seconds. Each pick tires them a little half the time, and they rest a step every 10 seconds, so that pace never wears them out.',
            'Every few minutes (or after each gift), end the talk and choose Talk again to check their line: if they "want to stop talking", wait about 40 seconds and slow your pace; if they "seem to have other business", wait a bit over a minute.',
            'Keep going for about 30 picks: each one has an even chance to make them know you better, and about 15 of those (a friendly greeting, "we meet often") is what the secret shop wants. The same picks add roughly 15 liking.',
            'Once they "have a good impression of you", talking stops helping much; finish with gifts they love (five or so take them past "like you a lot" to where a secret shop opens).',
            'Don\'t leave them alone for two minutes or more between talks: once they\'re completely rested, the next greeting tires them again.',
            'For a secret shop: they must not be tired at all, and the shop doesn\'t show their line. After your last talk, wait about a minute (more if they "seemed to have other business"), then press Shop straight away instead of Talk, so no greeting tires them first.',
            'Do it in one sitting: every hour online costs a little liking.'] }] }] };
  }
  // The feeding guide under the calculator, each part folded: the spirit's moods and how to read
  // them, how the mood drops and comes back, what it will eat, how much weaker the weapon gets
  // (db/egoweapon/egodesire.xml), what the stats and Social do, and common mistakes. Traced in the
  // G13 server (the timer, feed list, prices, penalties and Social gates).
  function egoGuide() {
    const D = (summary, ...blocks) => ({ kind: 'details', summary, blocks });
    const L = (...items) => ({ kind: 'list', items });
    const sk = id => skillLink(id, skillName(id).replace(/\s+/g, ' '));
    const awaken = [22005, 22006, 30401, 21009].map(sk);
    const egoTitles = [10037, 10038, 10039, 10040, 10041, 10042, 10043, 10044].map(id => titleById(id)).filter(Boolean)
      .map(t => ({ t: 'title', id: t.id, label: t.name }));
    const moodRows = [
      ['Smiling: "seems to be in a good mood"', 'Happy', 'Won\'t take items'],
      ['Calm: "doesn\'t seem to have any complaints"', 'Fine 1–3', '20% (stage 1), 70% (stages 2–3)'],
      ['Frowning: "seems to be in a bad mood"', 'Unhappy 1–6', '100% (stages 1–4), 80% (stages 5–6)'],
      ['Upset: "a very bad mood", the weapon gets weaker', 'Mad 1–8', '50%'],
      ['Angry: "very angry", the weapon gets weaker still', 'Mad 9–10', '0%']];
    const pen = egoPenaltyBlocks();
    return [
      D('How to tell its mood',
        { kind: 'table', columns: ['What it shows', 'Mood', 'Growth'], rows: moodRows },
        L('Talk to it: its face and a line about its mood show each time its menu comes back, so open Give Item or Repair and cancel to see them.',
          'Fine 1 and Fine 2–3 look the same, and so do Unhappy 1–4 and 5–6. To know the exact stage, count its messages: feed it until it\'s happy, then each "wants to talk" message is one stage down. 1 is Fine 1, 2–3 Fine 2–3, 4–7 Unhappy 1–4, 8–9 Unhappy 5–6, 10–17 Mad 1–8, and 18 or more Mad 9–10.')),
      D('How its mood drops',
        L('Every 5 minutes there\'s a 50% chance it drops one stage. That happens whenever you carry it, worn or not, but not during a fashion show.',
          'Each drop posts a system message: "<name> wants to talk", or "<name> seems to want an item" if you\'re talking to it at the time.',
          'The 5 minutes start when you log in or change channel.',
          'Feeding it 2½ minutes or more after the last check skips the next check.')),
      D('Cheering it up',
        L('Each item cheers it up by its price, whether or not it gives EXP: under 1,000 G a fifth of a stage, under 2,000 G two fifths, under 3,000 G three fifths, under 4,000 G four fifths, and 4,000 G or more a full stage.',
          'From Unhappy 1, it takes 16 to 20 items under 1,000 G, or 4 items of 4,000 G or more, to make it happy.',
          'For the most EXP from each stage of mood, give items just under 1,000 G. They cost only a fifth of a stage each.',
          'The best time to feed is Unhappy 1–2: full growth, and the weapon isn\'t weaker yet.',
          'At Mad 9–10 it gains nothing. Give it the cheapest items you have; 11 of them bring even the angriest spirit back to Mad 8 (50%).')),
      D('What it will eat',
        L('It won\'t take stackable items, enchant scrolls or other scrolls, music scores, keys, cooked food, items you can\'t give to people, items with no shop price, expired items, or anything you\'re wearing.',
          'It will take accessories.',
          'It asks before eating an item worth 10,000 G or more, or one that\'s upgraded or enchanted.',
          'It counts the item\'s own price. An item bought from a shop that sells it for a different price keeps that price. Discounts (the weekly sale, other races\' shops) don\'t count, and enchants don\'t change it.',
          'A stat at level 50 can\'t go higher. An item that would level it up is eaten for its mood only.')),
      ...(pen.length ? [D('How much weaker the weapon gets', ...pen)] : []),
      D('What its stats and Social do',
        L('Str raises damage. Int raises balance, Dex raises injury, Will raises durability and Luck raises critical; each of them also adds a little damage.',
          'Social doesn\'t change the weapon\'s stats, but:',
          { lines: [['At Social 21 the spirit can use ', ...awaken.flatMap((l, i) => (i ? [' / ', l] : [l])), ' (rank F), and it goes up a rank every 2 Social levels, to rank 1 at Social 50. It holds more uses as Social rises, up to 3 at Social 50.']], sep: '' },
          { lines: [['Spirit fire, ice and lightning wands get chain casting for ', sk(30201), ', ', sk(30301), ' and ', sk(30101), ': 1 at Social 6, 2 at 11, 3 at 16 and 4 at 21.']], sep: '' },
          'A spirit healing wand heals more with Str (+1 HP, up to +9 at Str 40) and costs less Mana with Int (5% less, up to 25% at Int 40).',
          { lines: [['At Social 10 it tells you about its title, and at Social 20 it gives it (+5 to every stat, +1 Defense, +1 Protection): ', ...egoTitles.flatMap((l, i) => (i ? [' · ', l] : [l]))]], sep: '' },
          'Once a game day, when you talk to it while it\'s happy or fine (happy or Fine 1 for a female sword or male wand spirit), there\'s a 1 in 6 chance it helps: full Life and wounds healed, wounds healed, full Stamina, full Mana or a full stomach. At Social 6 it can also bless an item you\'re wearing, and at Social 7 everything you\'re wearing.',
          'Its glow color depends on Social. At Mad 9–10 it doesn\'t glow.')),
      D('Common mistakes',
        L('"It only gets unhappy while it\'s equipped." It does whenever you carry it.',
          '"Items bought during a sale give less." The spirit counts the item\'s price, not what you paid.',
          '"Enchants that change the repair cost change what it\'s worth." They don\'t.',
          '"Accessories can\'t be given." They can.',
          '"Stackable items just give little." It won\'t take them at all.',
          '"Unhappy 1 to happy takes 15 cheap items." It takes 16 to 20.',
          '"Social does nothing in a fight." It unlocks the spirit\'s Awakening skill and a spirit wand\'s chain casting.'))];
  }
  // The weaker-weapon table: weapons with the same egodesire rows share a row; a column per row
  // after the first (Desire_Max 30: no change). The first row with desire <= Desire_Max applies;
  // "*x" multiplies, "-x" takes away, "=x" sets.
  function egoPenaltyBlocks() {
    const text = stripped('db/egoweapon/egodesire.xml'), groups = new Map();
    const attrs = s => Object.fromEntries([...s.matchAll(/(\w+)\s*=\s*"([^"]*)"/g)].map(a => [a[1], a[2]]));
    for (const m of text.matchAll(/<EgoItem\s+ID\s*=\s*"(\d+)"\s*>([\s\S]*?)<\/EgoItem>/g)) {
      const pen = [...m[2].matchAll(/<Penalty\s((?:[^>"]|"[^"]*")*?)\/?>/g)].map(p => attrs(p[1]));
      if (!items().get(m[1])) continue;
      const key = JSON.stringify(pen);
      if (!groups.has(key)) groups.set(key, { pen, ids: [] });
      groups.get(key).ids.push(m[1]);
    }
    if (!groups.size) return [];
    // A desire (0-100, 5 a stage) as the mood stage it's in.
    const stage = d => (d < 5 ? 'Happy' : d < 20 ? 'Fine ' + Math.floor(d / 5) : d < 50 ? 'Unhappy ' + (Math.floor(d / 5) - 3) : 'Mad ' + Math.min(10, Math.floor(d / 5) - 9));
    const maxes = [...new Set([...groups.values()].flatMap(g => g.pen.map(r => +r.Desire_Max)))].sort((a, b) => a - b);
    const cols = maxes.slice(1).map((hi, i) => ({ lo: maxes[i] + 1, hi }));
    const STATS = [['attack', 'Damage'], ['wound', 'Injury'], ['critical', 'Critical'], ['balance', 'Balance'], ['attack_range', 'Range']];
    const pct = v => Math.round(parseFloat(v.slice(1)) * 100);
    const cell = r => {
      if (!r) return '—';
      const out = [];
      for (const [k, label] of STATS) {
        const vals = k === 'attack' || k === 'wound' ? [r[k + '_min'], r[k + '_max']] : [r[k]];
        const parts = vals.filter(v => v && v !== '*1');
        if (!parts.length) continue;
        if (parts.every(v => v[0] === '*')) {
          const ps = vals.map(v => (v && v[0] === '*' ? pct(v) : 100)), lo = Math.min(...ps), hi = Math.max(...ps);
          out.push(`${label} ${lo === hi ? lo : lo + '–' + hi}%`);
        } else out.push(`${label} ${parts.map(v => (v[0] === '-' ? '−' + v.slice(1) : v[0] === '=' ? v.slice(1) : '×' + v.slice(1))).join(' / ')}`);
      }
      return out.length ? { lines: out.map(x => [x]) } : 'No change';
    };
    const named = [...groups.values()].map(g => ({ g, links: g.ids.map(id => itemLink(id, items().get(id).name)).sort((a, b) => a.label.localeCompare(b.label)) }));
    const none = named.filter(x => x.g.pen.every(r => cell(r) === 'No change'));
    const rowsOut = named.filter(x => !none.includes(x)).sort((a, b) => b.links.length - a.links.length)
      .map(x => [x.links, ...cols.map(c => cell(x.g.pen.find(r => +r.Desire_Max === c.hi)))]);
    const blocks = [
      { kind: 'text', text: 'Up to about Unhappy 3 the weapon works normally. Below that it loses damage first, then injury, critical and balance; bows and crossbows also lose range. The numbers are % of normal.' },
      { kind: 'table', columns: ['Weapons', ...cols.map(c => stage(c.lo) + ' – ' + stage(c.hi))], rows: rowsOut },
      { kind: 'note', text: 'Each column can start or end a stage early, depending on where in the stage its mood is.' }];
    if (none.length) blocks.push({ kind: 'text', text: 'These don\'t get weaker: ' + none.flatMap(x => x.links.map(l => l.label)).join(', ') + '.' });
    return blocks;
  }
  // The calculator: weapon, gender, item or gem, the stats' levels and the mood
  // in; per stat, the EXP it gets, its cap, and the price or gem size that reaches the cap.
  function spiritCalc(el) {
    const h = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
    const opt = (list, cur) => list.map(x => `<option${x === cur ? ' selected' : ''}>${h(x)}</option>`).join('');
    const st = { weapon: 'Sword', gender: 'Male', mode: 'Item', item: '', gem: 'Diamond', size: 1, mood: 2, lv: [1, 1, 1, 1, 1, 1] };
    try { Object.assign(st, JSON.parse(localStorage.getItem('mabiwiki-spiritcalc') || '{}')); } catch (e) { /* no storage */ }
    delete st.price;
    // Items with a shop price, for the item box's suggestions. A spirit takes one item at a time and
    // never a stackable one, so those aren't offered (typed in, they say so).
    const stacks = it => num(it.r.Bundle_Max) > 1;
    const pool = [...items().values()].filter(it => num(it.r.Price_Buy) > 0 && !testItem(it) && !stacks(it));
    const byName = new Map(pool.map(it => [it.name.toLowerCase(), it]));
    const typed = s => { s = String(s || '').trim(); const id = items().has(s) ? s : itemByName(s); return id ? items().get(String(id)) : null; };
    const findItem = s => { const it = typed(s); if (it) return stacks(it) ? null : it; return byName.get(String(s || '').trim().toLowerCase()) || null; };
    const gemNames = () => Object.keys(EGO_GEMS[st.gender][st.weapon]);
    el.innerHTML = `<div class="calc">
      <label><span>Weapon</span><select data-k="weapon">${opt(EGO_WEAPONS, st.weapon)}</select></label>
      <label><span>Spirit's gender</span><select data-k="gender">${opt(['Male', 'Female'], st.gender)}</select></label>
      <label><span>Mood</span><select data-k="mood">${EGO.moods.map((m, i) => `<option value="${i}"${i === +st.mood ? ' selected' : ''}>${h(m[0].replace(/ \(stages? (.+)\)/, ' $1'))} · ${m[1]}%</option>`).join('')}</select></label>
      <label><span>Give</span><select data-k="mode">${opt(['Item', 'Gem'], st.mode)}</select></label>
      <label class="im"><span>Item (name or ID)</span><input data-k="item" list="egoitems" autocomplete="off" placeholder="e.g. Rusted Longsword" value="${h(st.item)}"><datalist id="egoitems"></datalist></label>
      <label class="gm"><span>Gem</span><select data-k="gem"></select></label>
      <label class="gm"><span>Gem size (cm)</span><input data-k="size" type="number" min="1" max="10" step="0.01" inputmode="decimal" value="${h(st.size)}"></label>
      <fieldset><legend>Stat levels</legend>${EGO_STATS.map((s, i) => `<label><span>${s}</span><input data-lv="${i}" type="number" min="1" max="${EGO.maxLevel}" step="1" inputmode="numeric" value="${h(st.lv[i])}"></label>`).join('')}</fieldset>
    </div><div class="calcout"></div>`;
    const out = el.querySelector('.calcout'), gemSel = el.querySelector('[data-k="gem"]'), list = el.querySelector('#egoitems');
    const fillGems = () => { const g = gemNames(); if (!g.includes(st.gem)) st.gem = 'Diamond'; gemSel.innerHTML = opt(g, st.gem); };
    const draw = () => {
      for (const x of el.querySelectorAll('.im')) x.hidden = st.mode !== 'Item';
      for (const x of el.querySelectorAll('.gm')) x.hidden = st.mode !== 'Gem';
      const mood = EGO.moods[+st.mood] ? EGO.moods[+st.mood][1] : 100, mult = EGO.mult[st.gender][st.weapon];
      const it = st.mode === 'Item' ? findItem(st.item) : null, price = it ? num(it.r.Price_Buy) : 0;
      const raises = it ? egoItemStat(it.r.ID, EGO_TYPE[st.gender][st.weapon]) : -1;
      const gem = EGO_GEMS[st.gender][st.weapon][st.gem] || [0, 0, 0, 0, 0, 0], liquor = st.mode === 'Gem' && st.gem === BLUE_LIQUOR;
      const size = liquor ? 1 : Math.max(1, Math.min(10, +st.size || 1));
      const rows = EGO_STATS.map((s, i) => {
        const L = Math.max(1, Math.min(EGO.maxLevel, Math.floor(+st.lv[i] || 1))), need = EGO.need(L), cap = egoCap(L);
        const m = i === 5 ? 1 : mult[i], skip = st.mode === 'Gem' ? !gem[i] : raises >= 0 && i !== 5 && i !== raises;
        const cells = (got, extra, best) => `<td class="num">${got}${extra}</td><td class="num">${liquor ? 'No limit' : cap.toLocaleString('en-US')}</td><td class="num">${best}</td>`;
        if (skip) return `<tr class="muted"><td>${s}</td><td class="num">${L}</td><td class="num">${need.toLocaleString('en-US')}</td>${cells('—', '', '—')}</tr>`;
        // No item picked: no EXP to show, only the price that reaches the limit.
        const none = st.mode === 'Item' && !it;
        const raw = st.mode === 'Gem' ? egoGemExp(size, gem[i], m, mood) : egoItemExp(price, L, m, mood, i === 5);
        const got = liquor ? raw : Math.min(raw, cap), capped = !liquor && raw > cap;
        // What reaches the cap: the price (items) or the gem size (gems); a dash if nothing can at this mood.
        let best = '—';
        if (mood > 0 && !liquor) {
          if (st.mode === 'Gem') {
            let sz = 1;
            while (sz < 10 && egoGemExp(sz, gem[i], m, mood) < cap) sz = Math.round((sz + 0.01) * 100) / 100;
            best = egoGemExp(sz, gem[i], m, mood) >= cap ? sz.toFixed(2) + ' cm' : 'Over 10 cm';
          } else if (egoItemExp(1e9, L, m, mood, i === 5) < cap) best = 'Out of reach';
          else {
            let lo = 2, hi = 1e9;
            while (lo < hi) { const md = Math.floor((lo + hi) / 2); if (egoItemExp(md, L, m, mood, i === 5) >= cap) hi = md; else lo = md + 1; }
            best = lo.toLocaleString('en-US') + ' G';
          }
        }
        const pct = need ? ` <small class="muted">${(got * 100 / need).toFixed(1)}%</small>` : '';
        const mine = raises >= 0 && i === raises;
        if (none) return `<tr><td>${s}</td><td class="num">${L}</td><td class="num">${need.toLocaleString('en-US')}</td>${cells('—', '', best)}</tr>`;
        return `<tr${mine ? ' class="on"' : ''}><td>${s}${mine ? ' <small class="muted">this item</small>' : ''}</td><td class="num">${L}</td><td class="num">${need.toLocaleString('en-US')}</td>${cells(got.toLocaleString('en-US'), (capped ? ` <small class="muted">(${raw.toLocaleString('en-US')} before the limit)</small>` : '') + pct, best)}</tr>`;
      }).join('');
      const pts = it ? EGO.moodPoints(price) : null;
      const head = it ? `<p class="calcnote"><b>${h(it.name)}</b>: ${price.toLocaleString('en-US')} G, raises ${EGO_STATS[raises]} and Social for this spirit.</p>`
        : st.mode === 'Item' ? `<p class="calcnote muted">${typed(st.item) && stacks(typed(st.item)) ? 'Spirits don\'t take stackable items.' : st.item ? 'No item by that name or ID.' : 'Pick an item to see what it gives.'}</p>` : '';
      out.innerHTML = head + `<div class="tablebox"><div class="scroll"><table class="t"><thead><tr><th>Stat</th><th class="num">Level</th><th class="num">EXP to next level</th><th class="num">${st.mode === 'Gem' ? 'This gem gives' : 'This item gives'}</th><th class="num">Most per item</th><th class="num">${st.mode === 'Gem' ? 'Gem size for the most' : 'Price for the most'}</th></tr></thead><tbody>${rows}</tbody></table></div></div>` +
        (pts ? `<p class="muted calcnote">It also cheers the spirit up by ${pts} points (a mood stage is 100).</p>` : '');
      try { localStorage.setItem('mabiwiki-spiritcalc', JSON.stringify(st)); } catch (e) { /* no storage */ }
    };
    el.addEventListener('input', e => {
      const x = e.target;
      if (x.dataset.lv != null) st.lv[+x.dataset.lv] = x.value; else if (x.dataset.k) st[x.dataset.k] = x.value;
      if (x.dataset.k === 'weapon' || x.dataset.k === 'gender') fillGems();
      if (x.dataset.k === 'item') {
        // Suggestions: the first names containing what's typed.
        const q = x.value.trim().toLowerCase();
        list.innerHTML = q.length < 2 ? '' : pool.filter(it => it.name.toLowerCase().includes(q)).slice(0, 20).map(it => `<option value="${h(it.name)}">${h(num(it.r.Price_Buy).toLocaleString('en-US'))} G</option>`).join('');
      }
      draw();
    });
    fillGems();
    draw();
  }
  // The wiki's groups, [id, title, icon, types]: META.sections (the sidebar) and the home page's
  // tile groups. Each type's `section` in META.types names one of these ids.
  const NAV_GROUPS = [
    ['items', 'Items & gear', 'bag', ['item', 'enchant', 'upgrade', 'set_effect']],
    ['skills', 'Skills & crafting', 'zap', ['skill', 'craft', 'manual']],
    ['characters', 'Characters', 'users', ['monster', 'pet', 'npc', 'title']],
    ['world', 'World', 'globe', ['region', 'dungeon', 'fishing', 'prop']],
    ['quests', 'Quests', 'scroll', ['quest', 'mission', 'questline', 'tradein']],
    ['tools', 'Tools', 'sliders', ['tool']],
    ['misc', 'Misc', 'book', ['guide']],
  ];
  // Featured items and skills, by id; each shows only when the data has it (with its own name and
  // icon) and it isn't a test item.
  const HOME_TRY = [['skill', '20002'], ['skill', '30201'], ['item', '46001'], ['item', '63016']];
  const HOME_SKILLS = ['20002', '20001', '20003', '22001', '21001', '21002', '30201', '30301', '30101', '30006'];
  function homeMeta(cats, pages) {
    const cc = (t, k) => ((cats[t] || []).find(c => c.key === k) || {}).count || 0;
    const okItem = id => { const it = items().get(id); return it && !testItem(it) ? it : null; };
    // The first of the ids with an icon, else the category's first item with one.
    const art = (ids, cat) => {
      for (const id of ids) if (okItem(id) && itemIcon(id)) return itemIcon(id);
      const r = INDEX.item.find(x => x.icon && (x.cat === cat || x.cat.indexOf(cat + '/') === 0) && !testItem(items().get(x.id)));
      return r ? r.icon : null;
    };
    const tile = (label, go, look, count) => (count ? Object.assign({ label, count }, go, look.icon ? { icon: look.icon } : { glyph: look.glyph }) : null);
    // The tiles in the sidebar's five groups. One look per group: Items & gear in item art (a
    // glyph only when the data has no icon), every other group in line glyphs.
    const tiles = {
      items: [
        tile('Weapons', { t: 'item', cat: 'weapon' }, { icon: art(['40078', '40012'], 'weapon'), glyph: 'sword' }, cc('item', 'weapon')),
        tile('Clothing & Armor', { t: 'item', cat: 'armor' }, { icon: art(['14004', '13164'], 'armor'), glyph: 'shirt' }, cc('item', 'armor')),
        tile('Headgear', { t: 'item', cat: 'headgear' }, { icon: art(['18660'], 'headgear'), glyph: 'crown' }, cc('item', 'headgear')),
        tile('Shields', { t: 'item', cat: 'shield' }, { icon: art(['46001'], 'shield'), glyph: 'bag' }, cc('item', 'shield')),
        tile('Potions', { t: 'item', cat: 'potion' }, { icon: art(['51002'], 'potion'), glyph: 'flask' }, cc('item', 'potion')),
        tile('Food', { t: 'item', cat: 'food' }, { icon: art(['50004'], 'food'), glyph: 'pot' }, cc('item', 'food')),
        tile('Enchants', { t: 'enchant' }, { icon: art(['62005'], 'enchant'), glyph: 'scroll' }, INDEX.enchant.length),
        tile('Collection books', { href: '#/items?k=collectionbook' }, { icon: art(['1500'], 'book'), glyph: 'book' }, Object.keys(collectionData()).length),
      ],
      skills: [
        tile('Skills', { t: 'skill' }, { glyph: 'zap' }, INDEX.skill.length),
        tile('Crafting', { t: 'craft' }, { glyph: 'hammer' }, INDEX.craft.length),
      ],
      characters: [
        tile('Monsters', { t: 'monster' }, { glyph: 'skull' }, INDEX.monster.length),
        tile('Pets', { t: 'pet' }, { glyph: 'paw' }, INDEX.pet.length),
        tile('NPCs', { t: 'npc' }, { glyph: 'user' }, INDEX.npc.length),
        tile('Titles', { t: 'title' }, { glyph: 'crown' }, INDEX.title.length),
      ],
      world: [
        tile('Regions', { t: 'region' }, { glyph: 'map' }, INDEX.region.length),
        tile('Dungeons', { t: 'dungeon' }, { glyph: 'castle' }, INDEX.dungeon.length),
        tile('Shadow Missions', { t: 'mission' }, { glyph: 'moon' }, INDEX.mission.length),
        tile('Fishing', { t: 'fishing' }, { glyph: 'fish' }, INDEX.fishing.length),
        tile('Props', { t: 'prop' }, { glyph: 'box' }, INDEX.prop.length),
      ],
      quests: [
        tile('Quests', { t: 'quest' }, { glyph: 'scroll' }, INDEX.quest.length),
      ],
      misc: [
        tile('Guides', { t: 'guide' }, { glyph: 'book' }, INDEX.guide.length),
      ],
    };
    const featured = NAV_GROUPS.map(([id, title, icon]) => ({ id, title, icon, tiles: (tiles[id] || []).filter(Boolean) }))
      .filter(g => g.tiles.length);
    const tries = HOME_TRY.map(([t, id]) => {
      if (t === 'skill') { const s = skillById(id); return s ? skillLink(id, s.name) : null; }
      const it = okItem(id);
      return it ? itemLink(id, it.name) : null;
    }).filter(Boolean);
    const sk = HOME_SKILLS.map(skillById).filter(Boolean).map(s => skillLink(s.id, s.name));
    const byRank = {};
    for (const e of INDEX.enchant) if (e.rank) byRank[e.rank] = (byRank[e.rank] || 0) + 1;
    const ranked = ENCHANT_RANKS.filter(r => byRank[r]);
    const nRanked = ranked.reduce((a, r) => a + byRank[r], 0);
    const upg = [
      { t: 'upgrade', cat: 'artisan', label: 'Artisan upgrades', glyph: 'anvil', count: cc('upgrade', 'artisan') },
      { t: 'enchant', cat: 'holyfire', label: 'Holy Fire', glyph: 'zap', count: cc('enchant', 'holyfire') },
      { t: 'enchant', cat: 'alchemy', label: 'Alchemy bonuses', glyph: 'cube', count: cc('enchant', 'alchemy') },
      { t: 'upgrade', list: 1, label: 'Gear upgrades', glyph: 'sliders', count: INDEX.upgrade.length },
    ].filter(x => x.count);
    const panels = [];
    if (sk.length) panels.push({ title: 'Rank up a skill', text: 'Training methods, costs and bonuses for every rank.', blocks: [{ kind: 'links', style: 'list', items: sk }] });
    if (ranked.length) {
      panels.push({ title: 'Enchants by rank', text: `${nRanked.toLocaleString('en-US')} enchants, from rank ${ranked[0]} to ${ranked[ranked.length - 1]}.`,
        blocks: [{ kind: 'links', style: 'ranks', items: ranked.map(r => ({ t: 'enchant', f: { rank: r }, label: 'Rank ' + r, mark: r, count: byRank[r] })) }] });
    }
    if (upg.length) panels.push({ title: 'Upgrades', text: "What can be added to gear after it's made.", blocks: [{ kind: 'links', style: 'rows', items: upg }] });
    return { try: tries, featured, panels, searchHint: `Search ${pages.toLocaleString('en-US')} pages: items, skills, monsters, quests…`,
      footnote: D && D.built ? `Built ${D.built} from your game's files` : '' };
  }

  // An artisan upgrade's page is its enchant page, under upgrade (where the list has it).
  const isArtisan = id => { const e = enchantById(id); return !!e && e.cat === 'artisan'; };
  const PAGES = { region: regionPage, mission: missionPage, prop: propPage, fishing: fishingPage, dungeon: dungeonPage, quest: questPage, craft: craftPage, manual: manualPage, item: itemPage,
    enchant: id => (isArtisan(id) ? null : enchantPage(id)), upgrade: id => upgradePage(id) || (isArtisan(id) ? enchantPage(id) : null), tool: toolPage, guide: guidePage, questline: questlinePage, tradein: tradeinPage, set_effect: setEffectPage, title: titlePage, skill: skillPage, monster: monsterPage, npc: npcPage, pet: petPage };
  const later = fn => new Promise((ok, no) => setTimeout(() => { try { ok(fn()); } catch (e) { no(e); } }, 0));

  // ---------- the 3D viewer ----------
  // Items with a model: the dropped one (File_FieldMesh) or a worn one. viewer3d.js reads the model
  // from the packs in the browser when the reader asks; it and three.js load only then. The copy
  // for hosting has it in lib/; in the source it sits next to index.html. The single-file page also carries its own copies (<script type="text/plain"
  // id="lib-viewer3d">, lib-region3d, lib-three), which boot.js's MabiWikiLib turns into blob: URLs:
  // those come first, so the page works from any folder.
  const MESH_FIELDS = ['File_FieldMesh', 'File_MaleMesh', 'File_FemaleMesh', 'File_GiantMesh', 'File_FemaleGiantMesh', 'File_MaleGiantMesh'];
  const lib = (name, file) => (window.MabiWikiLib ? window.MabiWikiLib(name, file) : file);
  const loadScript = src => new Promise((ok, no) => {
    const s = document.createElement('script');
    s.src = src;
    s.onload = () => ok();
    s.onerror = () => no(new Error('no ' + src));
    document.head.appendChild(s);
  });
  let VIEWER = null;
  function loadViewer() {
    if (window.MabiWikiViewer) return Promise.resolve();
    if (!VIEWER) {
      // three.js first when the page has it inline (viewer3d.js would look for lib/ otherwise).
      const three = lib('three', null);
      VIEWER = (three && !window.THREE ? loadScript(three) : Promise.resolve())
        .then(() => loadScript(lib('viewer3d', BASE ? BASE + 'lib/viewer3d.js' : 'viewer3d.js')))
        .then(() => {
          if (!window.MabiWikiViewer) throw new Error('no viewer');
          // The packs picked while setting up the wiki in this session: the 3D views use them.
          const boot = window.MabiWikiBoot;
          if (boot && boot.packFiles && window.MabiWikiViewer.usePackFiles) {
            window.MabiWikiViewer.usePackFiles(boot.packFiles).catch(() => {});
            boot.packFiles = null;
          }
        }, e => { VIEWER = null; throw e; });
    }
    return VIEWER;
  }
  function model3d(el, args) {
    el.innerHTML = '<button type="button" class="btn">View in 3D</button><div class="model3d"></div>';
    const btn = el.querySelector('button'), box = el.querySelector('.model3d');
    let view = null;
    btn.onclick = () => {
      if (view) {
        view.close();
        view = null;
        box.innerHTML = '';
        btn.textContent = 'View in 3D';
        return;
      }
      btn.disabled = true;
      loadViewer().then(() => {
        const it = items().get(String(args.id));
        view = window.MabiWikiViewer.open(box, args.id, it ? { row: it.r } : {});
        btn.textContent = 'Close the 3D view';
      }, () => {
        box.textContent = "The 3D viewer couldn't be loaded.";
      }).then(() => { btn.disabled = false; });
    };
  }
  // A monster's model: viewer3d.js's openRace(container, raceId, {row}) with the race's db/race.xml
  // row (DefaultMesh, Framework, RaceDesc, ExtraData, FirstCreateStript...).
  function race3d(el, args) {
    el.innerHTML = '<button type="button" class="btn">View in 3D</button><div class="model3d"></div>';
    const btn = el.querySelector('button'), box = el.querySelector('.model3d');
    let view = null;
    btn.onclick = () => {
      if (view) {
        if (view.close) view.close();
        view = null;
        box.innerHTML = '';
        btn.textContent = 'View in 3D';
        return;
      }
      btn.disabled = true;
      loadViewer().then(() => {
        if (!(window.MabiWikiViewer && window.MabiWikiViewer.openRace)) {
          box.textContent = "The 3D viewer can't show monsters yet.";
          return;
        }
        const r = raceRows().get(String(args.id));
        view = window.MabiWikiViewer.openRace(box, String(args.id), r ? { row: r } : {});
        btn.textContent = 'Close the 3D view';
      }, () => {
        box.textContent = "The 3D viewer couldn't be loaded.";
      }).then(() => { btn.disabled = false; });
    };
  }

  // A prop's model (tree, rock, herb patch, building): viewer3d.js's openProp(container, class ids),
  // which shows the first of the prop page's class ids with a model.
  function prop3d(el, args) {
    el.innerHTML = '<button type="button" class="btn">View in 3D</button><div class="model3d"></div>';
    const btn = el.querySelector('button'), box = el.querySelector('.model3d');
    let view = null;
    btn.onclick = () => {
      if (view) {
        if (view.close) view.close();
        view = null;
        box.innerHTML = '';
        btn.textContent = 'View in 3D';
        return;
      }
      btn.disabled = true;
      loadViewer().then(() => {
        if (!(window.MabiWikiViewer && window.MabiWikiViewer.openProp)) {
          box.textContent = "The 3D viewer can't show props yet.";
          return;
        }
        view = window.MabiWikiViewer.openProp(box, String(args.ids || args.id).split(','));
        btn.textContent = 'Close the 3D view';
      }, () => {
        box.textContent = "The 3D viewer couldn't be loaded.";
      }).then(() => { btn.disabled = false; });
    };
  }

  // A region to walk around: region3d.js (after viewer3d.js, whose pack reader and folder it uses),
  // full screen over the page.
  let REGION3D = null;
  function loadRegion3d() {
    if (window.MabiWikiRegion && window.MabiWikiRegion.open) return Promise.resolve();
    if (!REGION3D) {
      REGION3D = loadViewer().then(() => new Promise((ok, no) => {
        const s = document.createElement('script');
        s.src = lib('region3d', BASE ? BASE + 'lib/region3d.js' : 'region3d.js');
        s.onload = () => (window.MabiWikiRegion ? ok() : no(new Error('no region viewer')));
        s.onerror = () => { REGION3D = null; no(new Error('no region viewer')); };
        document.head.appendChild(s);
      }));
    }
    return REGION3D;
  }
  // The walker that's open: one at a time (the page under it can still be reached with Tab, and a
  // second walker on top would draw the region twice).
  let walker = null;
  function region3d(el, args) {
    el.innerHTML = '<button type="button" class="btn">Walk around in 3D</button>';
    const btn = el.querySelector('button');
    btn.onclick = () => {
      if (walker) return;
      btn.disabled = true;
      loadRegion3d().then(() => {
        walker = window.MabiWikiRegion.open(Number(args.id), { title: args.title, onClose: () => { walker = null; btn.focus(); } });
      }, () => {
        el.append(document.createTextNode(" The 3D view couldn't be loaded."));
      }).then(() => { btn.disabled = false; });
    };
  }

  // ---------- the 3D models the pages show ----------
  // For a copy for hosting with the 3D models (boot.js makeHostable): every model the pages show, read
  // from the packs by viewer3d.js's exportModels, which knows which files a model needs.

  // The models the pages show: [{ kind: 'item' | 'race' | 'prop', id, row, ids (a prop's class ids) }].
  function modelList() {
    build();
    const out = [], seen = new Set();
    const walk = o => {
      if (Array.isArray(o)) { o.forEach(walk); return; }
      if (!o || typeof o !== 'object') return;
      if (o.kind === 'embed') {
        const kind = o.name === 'model3d' ? 'item' : o.name === 'race3d' ? 'race' : o.name === 'prop3d' ? 'prop' : null, id = String(o.args && o.args.id);
        if (kind === 'prop' && !seen.has(kind + id)) {
          seen.add(kind + id);
          out.push({ kind, id, ids: String(o.args.ids || id).split(',') });
        } else if (kind && !seen.has(kind + id)) {
          seen.add(kind + id);
          const row = kind === 'item' ? (items().get(id) || {}).r : raceRows().get(id);
          out.push(row ? { kind, id, row } : { kind, id });
        }
        return;
      }
      walk(o.sections); walk(o.blocks); walk(o.tabs);
    };
    for (const t of ['item', 'monster', 'npc', 'pet', 'prop']) {
      for (const r of INDEX[t] || []) {
        try { walk(PAGES[t](r.id)); } catch (e) { /* a page that can't be made shows no model either */ }
      }
    }
    return out;
  }

  window.WIKI_SOURCE = {
    embed: (name, el, args) => { if (name === 'model3d') model3d(el, args); else if (name === 'race3d') race3d(el, args); else if (name === 'prop3d') prop3d(el, args); else if (name === 'spiritCalc') spiritCalc(el, args); else if (name === 'region3d') region3d(el, args); },
    models: () => later(modelList),
    meta: () => later(() => {
      if (!D) {
        return { name: 'Mabinogi Wiki', badge: 'G13', tagline: "G13 Mabinogi's items, enchants, skills, monsters and titles.",
          // The page with boot.js sets itself up from the game's files.
          note: window.MabiWikiBoot ? "The game's data isn't here yet. Set the wiki up from your game files."
            : "The game's data isn't here yet.",
          noteLink: window.MabiWikiBoot ? { href: '#/setup', label: 'Set up the wiki' } : null,
          sections: [], types: {}, categories: {}, stats: { pages: 0, entries: 0 } };
      }
      build();
      return META;
    }),
    index: t => later(() => { build(); return INDEX[t] || []; }),
    page: (t, id) => later(() => {
      const p = PAGES[t] && PAGES[t](id);
      if (!p) throw new Error('no ' + t + ' ' + id);
      return Object.assign(p, { type: t, id: String(id) });
    }),
    search: () => later(() => {
      build();
      const out = [];
      // [type, id, name, alt, icon, sub, tags, effects]: effects (enchants') are shown and searched.
      // sub tells same-named entries apart (searchSubs).
      // Every type with pages of its own (Craftables is a list of items, already searched as items).
      for (const t of ['item', 'enchant', 'upgrade', 'set_effect', 'skill', 'monster', 'pet', 'npc', 'title', 'quest', 'questline', 'tradein', 'manual', 'region', 'dungeon', 'mission', 'fishing', 'prop', 'guide', 'tool']) {
        const subs = searchSubs(t, INDEX[t]);
        for (const r of INDEX[t]) out.push([t, r.id, r.name, r.alt || '', r.icon || null, subs.has(r) ? subs.get(r) : r.sub || '', r.tags || '', r.fx || '']);
      }
      return out;
    }),
  };
})();
