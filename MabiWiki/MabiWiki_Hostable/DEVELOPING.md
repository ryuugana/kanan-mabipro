# Developing this wiki

This folder is a complete, self-contained website: the Mabinogi Wiki. Everything here is plain
HTML, CSS and JavaScript with no build step, no package manager and no server-side code. Change a
file, save it, refresh the page. Everything below describes only the files in this folder, and it
is meant to be enough on its own: a developer (or a coding assistant) with nothing but this folder
can keep the wiki going from here.

The folder (`MabiWiki_Hostable`) ships as code only. Its game data, `data/`, is made into it from
the game's own files the first time `index.html` is opened from disk (see [Updating the game
data](#updating-the-game-data)); after that the folder can be uploaded to any static web host as it
is. `README.txt` says the same for people who only want to host it.

| File | What it is |
|---|---|
| `index.html` | the page: loads everything below with plain `<script>` and `<link>` tags |
| `app.css` | the look: colours (light and dark), layout, fonts |
| `app.js` | the app: header, sidebar, search, lists, and how a page is drawn |
| `datasource.js` | makes every list and page from the game's files and `sources.js` |
| `sources.js` | what the game's files don't say: where things come from, shops, quests... |
| `README.txt` | how to set the folder up and host it |
| `DEVELOPING.md` | this file |
| `.nojekyll` | an empty file that keeps GitHub Pages from leaving out folders whose names start with "_" |
| `lib/` | the 3D viewer (`viewer3d.js`, `region3d.js`, `packs.js`, `three-bundle.min.js`, `three-LICENSE.txt`) and the setup (`boot.js`, `images.js`, `sinks.js`, `prepare.js`) |
| `data/` | only once set up: the game's files, picture lists, pictures, `site.js` and, with 3D, `models/` |

## Contents

1. [How it works](#how-it-works): the files and the order they load in
2. [Routes](#routes)
3. [The data source interface](#the-data-source-interface-windowwiki_source)
4. [The page model](#the-page-model): pages, blocks and values
5. [Worked example: a section on item pages](#worked-example-a-section-on-item-pages)
6. [Conventions](#conventions) and [House style](#house-style): how pages read, look and are coded
7. [Adding a new page type](#adding-a-new-page-type)
8. [sources.js reference](#sourcesjs-reference): every table in it, and how to edit it by hand
9. [How to check your change](#how-to-check-your-change)
10. [Updating the game data](#updating-the-game-data)

## How it works

The wiki is built in the visitor's browser every time the page opens:

1. `index.html` is static (nothing rewrites it). It loads, in order, with plain `<script src>`
   tags (no `fetch`, so it works opened from disk too):
   - a small inline script setting `window.MABIWIKI_BASE = "./"` (where `data/` and `lib/` are)
     and `window.MABIWIKI_HOSTED = true` (this is a copy for hosting);
   - `data/site.js`: the 3D setting (`window.MABIWIKI_NO3D = true`, or
     `window.MABIWIKI_MODELS = "data/models/"` when the site has the 3D models);
   - `data/game-data.js`: the game's own data files as text, in `window.GAME_DATA = {version,
     built, source, files: {name: text}}`. It holds only the head (`files: {}`) and then loads its
     parts, `data/game-data-1.js`, `-2.js`... (each adds `window.GAME_DATA.files[name] = text;`
     lines, at most 16 MB a part), with `document.write` while the page is still loading, so
     `index.html` names only `game-data.js` and the number of parts can change. The names are paths
     like `db/itemdb.xml` or `local/xml/itemdb.japan.txt` (XML tables and their text tables; the
     text tables are named after the client's locale, which `datasource.js` finds by itself);
   - `data/item-icons.js`, `data/skill-icons.js`, `data/npc-images.js`: which picture in
     `data/icons/` and `data/portraits/` belongs to which item, skill or NPC (`window.GAME_ICONS =
     {item id: file}`, `GAME_SKILL_ICONS = {skill id: file}`, `GAME_NPC_IMAGES = {npc id: {icon,
     portrait}}`); `data/maps.js`: the list of minimap pictures in `data/maps/` (`GAME_MAPS =
     [name]`, each `data/maps/<name>.jpg`; which region uses which map comes from the game's
     `db/minimapinfo.xml`);
   - `sources.js`: data the game's files don't have (`window.GAME_SOURCES`): where items and
     enchant scrolls come from, shops, quests, dungeons, missions, regions and more (see
     [sources.js reference](#sourcesjs-reference));
   - `datasource.js` and `app.js`, written as `<script type="text/plain" data-run
     src="...">`: the browser doesn't run these by itself (see below). `datasource.js` reads all
     of the above and makes the wiki's content; it sets `window.WIKI_SOURCE`. `app.js` draws the
     frame (header, sidebar, search), routes by the URL hash and renders whatever `WIKI_SOURCE`
     gives it; it knows nothing about Mabinogi;
   - `lib/boot.js`, last. Its `boot()` decides what happens: when `window.GAME_DATA` is set (the
     data is there), it runs the `data-run` scripts in the order they appear (`runApp()`) and the
     wiki starts. When it isn't: opened from disk, it shows the setup (loading `lib/packs.js`,
     `images.js`, `sinks.js` and `prepare.js` only then, `ensureTools()`); on a web host, it shows
     "This wiki isn't ready yet". (`HOSTABLE` in `boot.js` is true for this folder; `canUpdate` is
     true only when the page is opened from disk.)

   Both `datasource.js` and `app.js` read the data globals once, when they start, which is why
   they wait for `boot.js`. A new script that reads the data must be a `data-run` script too,
   placed where it should run among them (before `app.js` if `app.js` needs it); a script that
   only defines helpers can be a plain `<script src>`. A new data file the setup doesn't make can't
   go in `data/` (the setup rewrites it): put it next to `index.html`, like `sources.js`.
2. `app.css` styles everything. Colors are tokens on `:root` (light) with a dark set under
   `prefers-color-scheme: dark` and `[data-theme="dark"]`; components use only the tokens.
3. `lib/`: the 3D viewer, `viewer3d.js` (items and monsters in 3D), `region3d.js` (walking around a
   region), `packs.js` (reads the game's file formats) and `three-bundle.min.js` (three.js), which
   load only when a reader opens a 3D view; and the setup, `boot.js` (above), `prepare.js` (makes
   `data/` from the game's files), `images.js` (the pictures) and `sinks.js` (where the files are
   written: the folder, or a zip). With the 3D models, they are in `data/models/`, with
   `data/models/index.json` listing them, and are read with `fetch`: they show once the site is
   hosted (or served by a local web server); opened from disk, a 3D view may ask for the visitor's
   own Mabinogi folder instead.

Every code file starts with comments on how it works, and the comments through the files explain
the game's data formats where they're read. Read the top of `app.js` and `datasource.js` first.

## Routes

The URL hash is the route: `#/` (home), `#/items` (a list; also `?cat=`, `?q=` and filters),
`#/item/40001` (a page: type, then id), `#/search?q=wolf`. `app.js` `route()` dispatches them.
A type's list route is its `route` in `META.types` (`items`, `quests`...); its pages are always
`#/<type>/<id>`. `#/setup` is the setup screen (the sidebar's "Update from your game files"; only from disk). Adding
`?theme=dark` (or `light`) to the page's address forces a theme.

## The data source interface (`window.WIKI_SOURCE`)

`app.js` asks `datasource.js` for everything through five functions (the first four return a
promise):

| Function | Gives |
|---|---|
| `meta()` | the wiki's name and tagline, the sidebar sections, every page type (`types`) with its list columns and filters, the categories of each type, and the home page (`home`) |
| `index(type)` | the list of a type: rows `{id, name, alt, icon, cat, sub, tags, ...columns}` |
| `page(type, id)` | one page (below) |
| `search()` | the search index: rows `[type, id, name, alt, icon, sub, tags, effects]` |
| `embed(name, element, args)` | draws an `embed` block itself (the 3D viewers, a calculator) |

`page()` rejects with `Error('no <type> <id>')` when the type's builder returns `null` (no such
page). `WIKI_SOURCE` also carries a few helpers the setup uses (`models()`: the 3D models the pages
show); the pages don't use them.

In `datasource.js`, `build()` makes `INDEX` (the list rows of every type) and `META` (types,
columns, filters, categories, sidebar sections) once, on first use. `PAGES` maps each type to the
function that builds its pages: `item: itemPage`, `enchant`, `upgrade`, `skill: skillPage`,
`monster: monsterPage`, `npc: npcPage`, `pet`, `title`, `set_effect`, `craft`, `manual`, `quest`,
`questline`, `dungeon`, `mission`, `region`, `fishing`, `prop`, `tool`, `guide`. Search for
`const PAGES` and for the function names. The sidebar's groups are `NAV_GROUPS`; the home page is
`homeMeta()`.

The game's files are read with small helpers near the top of `datasource.js`: `rows(file, tag)`
gives every `<tag .../>` row of an XML file as `{attribute: value}`, `tr()` resolves the text
references (`_LT[xml.<table>.<line>]`) through the text tables, and `items()`, `skills()`,
`monsters()`, `npcs()`... give each kind of thing, parsed once and cached.

## The page model

`page(type, id)` returns a plain object; `app.js` `renderPage()` draws it:

```js
{
  title: 'Wooden Stick', subtitle: 'local name or null', icon: 'data/icons/items/x.png',
  category: ['weapon/blunt'],            // category keys of this type (breadcrumbs, pills)
  kind: 'Blunt weapon',                  // the small label over the title
  summary: 'The description, under the title.',
  stats: [['Damage', '2~8'], ['Speed', 'Normal', '3-hit']],   // the bar under the title
  infobox: [['Type', 'Blunt weapon'], ['Size', '1×3']],      // the box at the side
  tags: [{ href: '#/items?k=wood', label: 'wood' }],
  sections: [ { heading: 'Combat', blocks: [ /* blocks */ ], wide: false, empty: null } ],
}
```

A section with `wide: true` spans the whole page below the article; `empty: 'text'` (or
`{title, text}`) keeps the section with an empty state when it has no blocks.

### Blocks (`app.js` `block()`)

Every block is `{kind, title?, ...}`:

| kind | Fields | Draws |
|---|---|---|
| `text` | `text` (blank lines split paragraphs), `tones` | paragraphs; with `tones`, "+5"/"-3%" colored |
| `kv` | `rows: [[label, value]]` | a label/value list |
| `table` | `columns: ['Name', ...]`, `rows: [[value, ...]]`, `sort` (column index) | a sortable table (long ones show 60 rows and a "show all") |
| `list` | `items: [value]`, `ordered`, `icon: 'check' \| 'x'` | a list (short items as pills) |
| `links` | `items: [link]`, `style: 'chips' \| 'list' \| 'ranks' \| 'rows'` | links to other pages |
| `grid` | `items: [link]` | link cards with big icons |
| `details` | `summary`, `blocks`, `open` | a fold that opens to more blocks |
| `tabs` | `tabs: [{label, blocks, count}]`, `start`, `remember`, `style: 'line'` | tabbed sets of blocks |
| `ranks` | `rows`, `blocks` (one per rank), `start` | a rank picker (skills) |
| `note` | `text`, `tone: 'warn'` | an info or warning note |
| `formula` | `text` | a formula, as written |
| `image` | `src`, `alt`, `caption`, `markers` | a picture (a region's minimap, with pins) |
| `embed` | `name`, `args`, `caption`, `plain` | drawn by `WIKI_SOURCE.embed` |
| `builder` | `groups` | the upgrade planner |

The top of `app.js` lists more optional fields.

### Values

A value (a table cell, a kv value, a list item) can be:

- a string or number (numbers are formatted), `true`/`false` (Yes/No), `null` or `''` (a dash);
- a link to a page: `{t: 'item', id: '40001', label: 'Wooden Stick', icon, sub}` (`itemLink(id,
  name)` and `skillLink(id, name)` in `datasource.js` make them);
- a link to a list: `{t: 'item', cat: 'weapon', label}` or `{href: '#/...', label}`;
- colored text: `{text: '+5', tone: 'pos' | 'neg' | 'gold'}`, or tagged: `{text, tag: 'Hidden'}`;
- several lines: `{lines: [a, [b, c]], sep: ', '}` (an inner array is one line, joined by `sep`;
  `sep: ''` builds one line out of text and links); `rule: true` draws a rule between the lines
  and sets each inner array's pieces side by side (wrapping), or one under another with
  `stack: true` (the "Versions with other stats" cells use both);
- a fold: `{fold: 'summary', body: value}`;
- an array: its values joined with commas.

## Worked example: a section on item pages

`itemPage(id)` in `datasource.js` collects `sections` and returns the page. To add a section with
the item's raw XML row (handy while developing), add before `const page = {` near its end:

```js
    sections.push({ heading: 'Raw data', blocks: [
      { kind: 'details', summary: 'The item\'s row in db/itemdb.xml', blocks: [
        { kind: 'kv', rows: Object.entries(r).map(([k, v]) => [k, v]) },
      ] },
    ] });
```

(`r` is the item's row there: `{attribute: value}` from `db/itemdb.xml`.) Save and refresh an item
page, such as `#/item/40001`. A new list column works the same way: add the field to the item rows
in `build()` (`INDEX.item`) and a `{key, title}` to `META.types.item.columns`.

## Conventions

- Plain modern JavaScript (ES2020) in an IIFE per file with `'use strict'`; no modules, no
  dependencies (except three.js for 3D), no build step. Scripts load with `<script src>` so the
  site works from `file://`.
- Comments say what a thing is and why, in plain sentences, above the code.
- What a page shows comes from the game's files (or `sources.js`); a page leaves out what the data
  doesn't have rather than guessing.
- `app.js` stays generic: Mabinogi-specific logic belongs in `datasource.js`.
- Styles use the tokens in `app.css` so light and dark both work.

## House style

The wiki reads and looks the same everywhere because every page follows these rules. Keep to them
when you add or change anything.

### Writing (the text on the pages)

- Plain English a player can follow. No internal ids, codes, attribute names, file names or
  function names in page text (an id may appear in an infobox row that is plainly an id, like a
  prop's "Prop ids", or in the lists' ID column, which `app.js` draws).
- Be explicit about cause: "an arrow lit at a campfire", not "a lit arrow"; "if it leaves undefeated
  (its time runs out)", not "if it escapes".
- Ranks as players say them: "rank F", "rank 9", "Novice", "Dan 1". `rankLabel(level)` and
  `skillRank(level)` in `datasource.js` turn the game's rank numbers (0 Novice, 1–6 F–A, 7–15
  rank 9–1, 16+ Dan) into those words; use them rather than writing your own.
- NPCs, items, monsters, skills and quests by the names players see, and linked: `itemRef(id)`,
  `itemLink(id, name)`, `skillLink(id, name)`, `skillRef(id)`, `npcRef(id)`, `raceLink(id)` and
  `questLink(id)` make the links. Free text from `sources.js` goes through `noteLinks(text)`,
  which links the item, NPC, monster and enchant names it finds.
- When something isn't certain, say "likely" (or "isn't known") instead of guessing. Leave out what
  the data doesn't have.
- Never mention where the data came from, or any tools.

### Colours

- Stat gains are blue and losses red, and the whole line is coloured, not just the number: use the
  tone `'pos'` or `'neg'` (`{text, tone}`). In `datasource.js`, `toneOf(label, value)` picks the
  tone from the sign, `toned(label, value)` colours a value cell, `tonedLine(label, value)` colours
  the whole "Label +5" line, and `tonedAs(cell, tone)` colours any text cell (links keep their own
  colour). In `app.js`, `tonesHtml(text)` colours the signed amounts in a `text` block with
  `tones: true` (a short part with one amount is coloured whole).
- For stats where lower is better (mana or stamina used, costs, cooldowns, casting time, Mana Burn)
  the colours flip: "Mana Usage -10%" is blue. A "... Reduction +N" (or "decrease") is still good,
  so it stays blue. The words are the regular expressions `DOWN_GOOD` in `datasource.js` and
  `TONE_DOWN_GOOD` in `app.js`; they must stay alike, so change both together.
- An item's own stats changing show before → after: "Balance 50% → 55%", coloured by whether it went
  up or down. See the `arrow` helper in `variantsSection` in `datasource.js` (the "Versions with
  other stats" section); other stats show the change ("STR +15").
- `tone: 'gold'` (the colour prices get in an infobox) is never used for gains or losses.

### Layout

- Long or secondary tables go in a fold: a `details` block whose `summary` says what it is and how
  many, such as `Shops (12)` or `Quests given (31)`. `sourcesSection` and `skillWays` do this.
- Long lists of link chips show one line and a "Show all": a `links` block with `oneRow: true`.
- Stat lists are one stat per line, no commas (`{lines: [[a], [b]], sep: ''}`).
- A section with many columns gets `wide: true`: it is drawn below the article and the infobox,
  across the whole page ("Where to get it", a questline's "Steps").
- NPC pages set `capTables: true` on the page, so each table scrolls in a short box (shops and
  quest lists run long).
- Prefer one combined row over several rows that say the same thing (the same quest under two ids,
  the same shop line twice: `skillWays` drops repeated rows, for example).

### Code

- Plain JavaScript, no build step, no libraries.
- Every reader of a data table has a comment above it giving the data's shape and the rule it
  implements, in plain sentences (see the comment above `sourcesOf`, `npcShopBlocks` or
  `questlinePage`). When you change a shape, change the comment.
- Keep functions small and named for what they build (`fishingSection`, `npcGiftBlocks`,
  `missionPage`). Sections return `null` (or `[]` for block lists) when they have nothing to show,
  and the page builder skips them.
- Match the surrounding style: compact lines, `const` arrow helpers, comments in sentences.
- Anything heavy that several pages need (a lookup by item or race) is built once, on first use,
  and cached in a module variable (`let FISH_BY_ITEM = null` and the function that fills it).

## Adding a new page type

A page type is the set of things that has a list (`#/<route>`) and pages (`#/<type>/<id>`).
Six places in `datasource.js` make one; all of them are needed:

| Where | What to add |
|---|---|
| `build()`, `INDEX` | the list rows: `INDEX.<type> = [{id, name, icon, cat, sub, tags, ...columns}]` |
| `build()`, `cats` | the type's categories: `[{key, title, parent, order, count}]` (counts from `catRows(INDEX.<type>)`) |
| `build()`, `META.types` | `T('<type>', {title, route, icon, view: 'table' \| 'grid', columns: [{key, title, num}], section, order})` |
| `NAV_GROUPS` | the type's id in one sidebar group (the group's id is the `section` above) |
| `PAGES` | `<type>: <builder>`, a function from id to a page object (or `null`) |
| `WIKI_SOURCE.search` | the type in the list of types the search index covers (leave it out to keep it out of search) |

`T(type, fields)` adds `filters: []` and the counts. `order` is the type's place inside its group;
`icon` is one of the icon names in `app.js`'s `P` table. Optional fields on a type: `filters`,
`catColumns`, `catFilters`, `catFirst`, `noInfo`, `noId` (see the top of `app.js`). A row's `cat`
is a category key or a list of them ("a/b" is a child of "a"); `sub` is the grey line under the
name in lists and search.

Worked example: a "Shops" type with one page per NPC who sells something, reusing the NPC page's
shop blocks. Add the builder before the line `// ---------- the source app.js reads ----------`:

```js
  // ---------- shops ----------
  // One page per NPC who sells something (GAME_SOURCES.shops, keyed by NPC id): their shop tabs, as
  // the NPC's own page shows them. One category, so the list has a single heading.
  const SHOP_CATS = [['shop', 'NPC shops', '']];
  const shopRows = () => Object.keys((window.GAME_SOURCES || {}).shops || {}).map(npcById).filter(Boolean)
    .map(n => ({ id: n.id, name: `${n.name}'s shop`, icon: npcFace(n.id), cat: 'shop', sub: n.town || 'Shop',
      tabs: window.GAME_SOURCES.shops[n.id].t.length, tags: n.name }));
  function shopPage(id) {
    const n = npcById(id), blocks = n ? npcShopBlocks(n.id) : [];
    if (!blocks.length) return null;
    return { title: `${n.name}'s shop`, subtitle: null, icon: npcFace(n.id), category: ['shop'], kind: 'Shop',
      infobox: [['Sold by', { t: 'npc', id: n.id, label: n.name, icon: npcFace(n.id) }]], summary: '',
      sections: [{ heading: 'For sale', wide: true, blocks }] };
  }
```

Then, in `build()`, the rows (before the line `const cc = catRows(INDEX.craft), ...`):

```js
    INDEX.shop = shopRows();
```

its categories (first entry of `const cats = {`):

```js
      shop: (sc2 => SHOP_CATS.filter(c => sc2[c[0]]).map((c, i) => ({ key: c[0], title: c[1], parent: '', order: i, count: sc2[c[0]] })))(catRows(INDEX.shop)),
```

and its type (first entry of `types: {` in `META`):

```js
        shop: T('shop', { title: 'Shops', route: 'shops', icon: 'shop', view: 'table', columns: [{ key: 'tabs', title: 'Tabs', num: true }], section: 'world', order: 6 }),
```

Outside `build()`: add `'shop'` to the World group in `NAV_GROUPS` (`['region', 'dungeon',
'mission', 'fishing', 'prop', 'shop']`), `shop: shopPage` to `const PAGES = {`, and `'shop'` to the
type list in `search: () => ...`. Refresh: "Shops" appears under World in the sidebar, `#/shops`
lists the shops (158: the NPCs in `shops` that the wiki has pages for) with a Tabs column,
`#/shop/nora` is "Nora's shop" with her tabs and an infobox linking to her, and searching "shop"
finds them.
A home page tile is optional (`homeMeta`, `tiles`).

## sources.js reference

`sources.js` holds what the game's own files don't: where things come from, shops, quests and so
on. **It is edited by hand now**: the tool that produced it isn't part of this folder. It must stay
valid JavaScript of exactly this form (a comment line, then one assignment):

```js
// <a comment>
window.GAME_SOURCES = {"version":2,"strings":[...],"sets":{...}, ...};
```

The object is plain JSON: double-quoted keys and strings, no comments, no trailing commas, no
`undefined`. Ids are strings when they are object keys (`"40001"`) and are written as numbers or
strings inside rows exactly as the examples below show (the code compares them with `String()`,
so either works for item, race and quest ids; NPC ids are always strings such as `"nora"`).

### Editing a 7.7 MB one-line file

Don't open it in an ordinary text editor and search by eye: the whole object is one line. Either
use an editor that can format and fold JSON (format a copy, edit, then collapse it back to the
form above), or, better, let a small Node.js script load it, change it, and write it back. Save
this as `edit-sources.js` next to `sources.js` and run `node edit-sources.js`:

```js
// Loads sources.js, runs the edits below, and writes it back. Run it next to sources.js:
//   node edit-sources.js
const fs = require('fs'), vm = require('vm');
const FILE = process.argv[2] || 'sources.js';
const text = fs.readFileSync(FILE, 'utf8');
const box = { window: {} };
vm.runInNewContext(text, box, { filename: FILE });
const S = box.window.GAME_SOURCES;

// A text's index in S.strings (added at the end when it isn't there yet).
const str = s => { let i = S.strings.indexOf(s); if (i < 0) i = S.strings.push(s) - 1; return i; };

// ---- your edits go here ----

// ---- write it back: the comment line(s) above the object, then the object on one line ----
const head = text.slice(0, text.indexOf('window.GAME_SOURCES'));
fs.writeFileSync(FILE, head + 'window.GAME_SOURCES = ' + JSON.stringify(S) + ';\n');
console.log('Wrote', FILE);
```

Run with no edits, it rewrites the file with the same content (the bytes differ slightly the first
time: `25.0` becomes `25`, for example). Keep a copy of `sources.js` before a big change, and check
the result in the browser (see [How to check your change](#how-to-check-your-change)). A mistake
that breaks the file shows as a syntax error in the console, and the wiki then opens without
anything from `sources.js` (no quests, regions or shops; every item "N/A: no known source").

### The string table

`strings` is a list of texts that many rows share (place names, notes, how chances work). A row
refers to one by its position: `"w":358` means `strings[358]` ("Devil Cat shop"). Which fields use
it:

- `items` and `sets` source rows: `n`, `w` and `cn`. These may also be written as plain text
  (`"w":"Nora shop, Cooking Tools tab"`): `sourcesOf()` accepts either.
- `variants` source rows (`s`): `n`, `w` and `cn`; `skillSources` rows: text, condition and rank.
  These must be indexes, with `-1` for none: plain text there is shown as nothing.

Add a text with `str('...')` from the script above (it reuses an existing entry). Never remove or
reorder entries: that shifts the later positions, and the rows using them would show the wrong
text. To change a shared text everywhere, change it in place; to change it for one row, give that
row a new entry.

### Every table

The examples are real rows from the file, shortened with `...`.

#### `version`

`2`. The format's version; nothing reads it.

#### `strings`

`["Reading the book successfully", "10 of 253 per successful read", ...]` (7,701 texts). See
[The string table](#the-string-table).

#### `items` and `sets`: where things come from

`items` is `{item id: [source]}`; `sets` is the same for enchant scrolls, keyed by enchant id.

```json
"1000": [{"k":"shop","t":"npc","id":"devilcat","w":358},{"k":"shop","t":"npc","id":"devilcat","w":359,"p":100}]
"1567": [{"k":"monster","t":"monster","id":"130023","c":0.4}]
"2000": [{"k":"boss","t":"monster","id":"100023","w":563,"c":100,"a":"1000, 300"}]
```

A source: `k` kind (`monster`, `boss`, `shadow_mission`, `dungeon`, `chest`, `puzzle`, `box`,
`gachapon`, `event`, `treasure`, `pet_gift`, `collection`, `shop`, `quest`, `npc_gift`, `other`;
the names are `SOURCE_KINDS`), `t` what `id` is (`monster` = a race id, `item`, `npc`, `quest`; a
quest id that is a mission's links to the mission; leave both out for a place), `n` a name to show
instead of the thing's own, `w` where (one or more notes, joined with `"; "`), `c` chance in percent
(leave out for a sure way), `cn` how the chance works, `p` price in gold, `a` amount (text, shown as
"×1000, 300"). For a shop line, write the tab in `w` as "<NPC> shop, <Tab> tab": the tab goes in the
Tab column and the NPC's name isn't repeated.

Read by `sourcesOf(table, id)` (which resolves the string indexes) and `sourcesSection(table, id)`:
the "Where to get it" section of item and enchant pages, one fold per kind of source (Shops,
Monster drops, Dungeons and shadow missions, Quests and gifts, Boxes and gachapons, Other).
`dropsOfRace()` turns the `t: "monster"` rows around for the monster pages' "Drops" section; the
enchant list's "Comes from" column and the manual pages' "Where to get it" use them too. An item
with no row says "N/A: no known source".

#### `artisan`: what artisan upgrades roll

`{set: {n: [[how many results, weight]], o: [[enchant id, weight]]}}`:

```json
"1": {"n":[[2,40],[3,40],[4,20]],"o":[[80001,20],[80003,20],[80011,50],[80024,50]]}
```

How many results is rolled first, then each result is picked by weight among those not picked yet.
Read by `artisanSet(n)` for the upgrade pages that do `luckyupgrade(<set>)` (the Upgrades list
and its "Artisan upgrades" category).

#### `enhance`: special (S and R) upgrades

`{levels: [[level, gold, success %, break %, level-lost %]], convert: [[level, gold]], bonus: {set:
[[bonus, [value at level 1..10]]]}}`:

```json
"levels": [[1,30000,100,0,0],[2,30000,50,0,100], ...], "convert": [[1,20000],[2,40000], ...], "bonus": {"101": [], ...}
```

Read by `enhance()` and `specialUpgradeSection()`: the "Special upgrade" section of item pages.
Only levels up to 3 (`ENHANCE_MAX`) are shown, as the game stops there.

#### `repair`

`{npcs: {npc: {r: success %, f: [item filters], p: price factor (0 free), m: 1 only inside a
mission}}, tables: {table: [[b, f] for rates 90..100]}, kinds: [[category tags, table]]}`:

```json
"npcs": {"ailbhis":{"r":90,"f":["*"]}, "jenna_renewal":{"r":80,"f":["*/smith_repairable/*"],"p":0,"m":1}, ...}
"tables": {"weapon":[[100,0.005],[150,0.01], ...]}, "kinds": [["/weapon/edged/","weapon"], ...]
```

The comment above `repairCostPerPoint()` gives the price formula. Read by `repairSection()` (item
pages, "Repair") and `npcRepairRows()` (the NPC infobox's "Repairs" and "Repair success").

#### `synthesis` and `cooking`: recipes the game's files don't have

`synthesis`: `[[result item, how many, [[material, count]], [success % at Novice, F ... rank 1]]]`
(16 rates). `cooking`: `[[result item, method, [[ingredient, share %, low, high]], [[optional
extra, share %, low, high]]]]` (an extra with a positive high makes the dish better, a negative low
worse):

```json
["65034",1,[["65007",1],["65005",5],["64041",5]],[58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88]]
["50116","mix",[["50009",75,0,60],["50145",20,0,20]],[["50132",10,0,20],["50131",5,-10,0],["50156",5,-5,0]]]
```

Read by `recipes()`: the Craftables pages (`craft`) and the item page's crafting section.

#### `npcRegions`

`{npc: region id}`, for NPCs with no town of their own: `"arneng": 3400`. Read by `regionTown()`,
which names the town in the NPC list and categories.

#### `skillVars`: what a rank's Var values mean

`{skill: [[Var, label, unit]]}` (unit `''`, `'%'`, `'ms'`, `'s'` or `'deg'`; a label with
"(random)" shows "value–twice the value"):

```json
"40001": [["Var1","Max HP",""],["Var2","Max MP",""], ... ,["Var6","Paladin pet gets","%"]]
```

Read by `SKILL_VARS()` and `varValue()`: extra columns in the skill page's rank table.

#### `ai`: monster behaviour

`{t: {AI file: {sense, tt, ch, att, opt, st}}, r: {race ClassName (lowercase): [AI file, {variable:
value}]}}`. `sense` is `[sight, angle, hearing, leash radius]` (numbers or `%variables` filled from
`r`), `tt` the target type (who it will gang up on), `ch` the AI file's character settings, `att` `[[race tags or ClassName, attitude, 'stringid' | 'name']]`, `opt` `[[option, attrs]]`,
`st` `[[state, type, attrs, actions, events, target weights]]`; the comment above
`monsterBehaviour()` has the full shape and what each part means in the game.

```json
"r": {"bigearaardvark": ["aardvark", {"visualfield_radius":"500","audio_range":"300"}], ...}
```

Read by `monsterBehaviour()`: the monster pages' "Behavior" section.

#### `elements`

`{race id: [physical, fire, ice, lightning]}` out of 9: `"1": [9,0,0,0]`. Read by `elementOf()`:
the monster pages' "Elements" section, the Elements guide and the fire-arrow figures.

#### `gifts`

`{npc: {k: kind, w: [11 trait weights], b: best [[item, point]], x: worst [[item, point]], s:
[special gift notes]}}`. Kinds: `taste` (weights 2 loves, 1 likes, -1 dislikes, 0 nothing, in the
order of `GIFT_TRAITS`), `flat` (takes anything, no change), `refuses`, `ignores`, `special`
(only the notes in `s` are shown):

```json
"aer": {"k":"taste","w":[1,2,-1,2,2,0,-1,2,-1,2,0],"b":[[730009,6],[542083,5], ...],"x":[[542212,-7], ...],"s":["Give her a Suspicious Fomor Pass and she gives you a Rundal Siren Dungeon Pass.", ...]}
```

Read by `npcGiftBlocks()`: the NPC pages' "Gifts" section.

#### `affection`

`{npc: {t: 1 talking raises liking | 0 it never does, j: 'usual' | 'harsh' part-time job, g:
[special scene notes]}}`: `"deian": {"t":1,"j":"usual","g":["Female characters who know him well ... get a special talk."]}`.
Read by `npcFriendBlocks()`: the NPC pages' "Friendship" section.

#### `secretShops`

`{npc: {u: [how it opens, detail], tabs: [tab names], i: [[item, price, count]], x: [more ways in,
same shape]}}`. How it opens: `affection`, `item` (detail: the item that opens it), `keyword`
(detail 1: Mondays and Tuesdays only), `quest` (detail: the quest), `membership`, `skill` (detail:
the skill):

```json
"stewart": {"u":["affection"],"tabs":["Magic Books"],"i":[[1007,9800,1], ...],"x":[{"u":["skill",41001],"tabs":["Talismans"],"i":[[71072,999,1]]}]}
```

Read by `npcShopBlocks()` (those tabs get a lock, items sold only there a "Secret" tag),
`secretShopHow()` (the note saying how to open it), the NPC infobox's "Secret shop" row, and the
NPC Friendship guide.

#### `shops` and `shopConds`

`shops` is `{npc: {t: [[tab, [[item, price, count, when, extra]], random stock]], d: discount %}}`;
`shopConds` is `{code: when}`:

```text
a shop:             "bryce": {"t":[["Licenses",[[60103,4000,1],[81010,2800,1],[81011,2800,1],[81012,2800,1]]]]}
a timed row:        [63076,250,5,"c1"]   with shopConds "c1": "during the Summer fireworks event"
random daily stock: [[40006,40005,40010, ...],200,400,"1-3"]
```

A row: item, price (for the whole `count`, so a stack's price is the stack's), count, `when` (a
`shopConds` code, or `0`/left out for always), and an optional `extra` saying what a generic item
really is: `mf` the manual form it is (its page), `m` the item a manual makes, `mp` its name, `q` a
quest scroll's quest (`qn` its name), `sn` a score's tune, `pf`/`sf` prefix/suffix enchant ids,
`ql` a quality, `jp` a price in jousting points (the price is then `null`):
`[64581,13200,1,0,{"mf":20161,"m":81120,"mp":"BlackSmith Manual - Furnace of Gold Ingot"}]`.
Random stock: `[[items], lowest %, highest % of the price, how many]`, rolled each Erinn day with a
random enchant. `d`: the discount for Humans who support the NPC's race.

Read by `npcShopBlocks()` and `shopItemBits()`: the NPC pages' "Shop" tabs; `formSellers()` reads
the `mf` extras for the Manuals & patterns list ("In a shop") and the manual pages. Note: the item
page's "Where to get it → Shops" comes from `items`, not from here; a new shop line needs a row in
both (example (b) below).

#### `crafting`: making things by hand

`{table: {Tailoring | Blacksmith: 31 rows [worst, bad, good, best] %}, forms: {manual form:
[difficulty, max progress, lowest rank allowed, [attempts to finish at each rank, Novice ... Dan
3]]}, quality: {keys, rules, forms, play}}`. Rank numbers index `CRAFT_RANKS` (0 Novice, 1 F ...).

```json
"forms": {"1": [3,0.5,0,[7.5,5.7,4.5,3.7, ...]], ...}
"quality": {"keys": ["attack_min","attack_max","wound_min","wound_max","critical","balance","defense","protect","durability_max","durability"],
  "rules": [[[6,7,8,9], [[-100,-81,[0,0,-2,-5]], ..., [95,100,[1,3,5,5]]], [[15,[0,0,1,1]]]], ...],
  "forms": {"1": [0,[2,0,15,15]], ...},
  "play": {"Blacksmithing": [[57,100,67.51],[66,100,74.21],[78,100,81.86]], "Tailoring": [...]}}
```

`quality.rules`: `[[stat indexes into keys], [[quality from, to, [change per stat]]], [[skill level,
[bonus per stat]]]]`; `quality.forms`: `{form: [rule index, [base value per stat]]}`; `play`: per
skill, the finishing minigame's `[lowest, highest, average]` quality for three rank groups (Novice
to D, C to 9, 8 and up). The comments above `manualMakingBlocks()` and `qualityRule()` explain the
rules. Read by `manualMakingBlocks()` (manual pages, "Making it"), `qualityRule()` /
`qualityStats()` (the item page's "Made by hand: worst to best") and `qualityStatsOf()` (variants
made at a quality).

#### `variants`: versions of an item with other stats

`{item: [{e: [prefix, suffix] (0 none), q: quality or [low, high], d: [base, max durability], c:
[durability, of max], x: [[stat, change]], xc: [[stat, change, condition]], s: [[kind, t, id, n, w,
chance (-1 unknown), cn]]}]}`:

```json
"12000": [{"e":[20737,0],"x":[["Str",15],["Luck",10]],"s":[["gachapon","item","91259",5650,-1,0.9693,5651]]}]
"13043": [{"d":[15,17],"s":[["gachapon","item","91023",10,-1,0.0637,5730], ...]}, ...]
```

Read by `variantsSection()`: the item pages' "Versions with other stats".

#### `skillSources`: ways to learn a skill

`{skill: [[kind, quest, npc, item, text, condition, rank, generation, temporary]]}`. Kind: `quest`,
`book`, `npc`, `item`, `equip`, `auto`, `event`, `other` (a `quest` with a generation is shown as
that generation's story). `quest`, `npc` and `item` are the source (0 when not used; `npc` is the
NPC id), text/condition/rank are string indexes (-1 none), generation is e.g. `"G1"` or 0,
temporary 1 or 0. A rank text containing "training" puts the row in the "Training from people or
books" fold instead of "Ways to learn it".

```json
"10004": [["quest",20021,0,0,6925,-1,6926,0,0], ["book",0,0,1082,7200,7201,7202,0,0], ["npc",0,"nora",0,7213,7041,6929,0,0], ...]
```

Read by `skillWays()`: the skill pages' "How to get".

#### `questChains`: questlines

`[{id, t: title, g: group, n: generation (0 none), s: summary, st: [[quest, name, step, how,
condition, branch, note, alternatives]]}]`, in play order. Steps sharing a number are different
paths; `branch` names the path (it fills the "Path" column). `note` is a warning, shown in red
under the step's "How to get it" text. `alternatives` (optional) is `[[quest, name, when]]`: other versions of the step ("or X (when)").
`g` is one of the group titles in `QL_GROUPS` ("Main stream", "Side stories"...).

```json
{"id":"aeira-lunchbox","t":"Aeira's Lunchbox","g":"Side stories","n":0,"s":"A mini-story with Aeira in Dunbarton: ...",
 "st":[[60011,"I Want to Cook",1,"Talk to Aeira in Dunbarton while wearing the title \"the Lucky\". ...","While wearing the title \"the Lucky\"","","",[[60012,"I Want to Cook","One of eight requests, picked at random"], ...]], ...]}
```

Read by `questlinePage()` (the Questlines list and pages) and `questlineBlocks()` (the "Questline"
section on each quest's page: step N of M, before and next).

#### `quests`

`{id: {n: name, nj: name in its original text, c: category, g: story ("G1"... or "JG"), s: steps,
r: rewards, gv: who gives it, rq: requirements, rp: repeatable, sc: scroll item, pr: scroll price,
nq: can't be given up, pj: part-time job, ch: part-time job reward choices}}`. A few rows also
carry `ex`, `lc` and `et`, which nothing reads.

```json
"20001": {"n":"Deliver Ranald's Armor","c":"skill","s":[["give",{"item":70002,"npc":"ferghus","count":1,"m":1}],["talk",{"npc":"ranald","m":1}]],
          "r":[{"kind":"exp","value":5},{"kind":"item","item":1004,"count":1}],"sc":70026}
```

A step is `[kind, {npc, item, race, skill, count, m: region, x: the step in words, ...}]`; kinds
drawn specially are in `questStep()` (talk, kill, collect, gather, fish, give, receive, craft,
learn, exchange); any other kind shows its `x` text. Rewards are `{kind: exp | gold | ap | item |
skill | quest | ..., value | item + count (+ prefix, suffix) | skill | quest}` (`questReward()`);
givers `{npc, owl, shop, tab, ptj, board, cost, quest, fishing, item, when}` (`questGiver()`);
requirements `{level, totalLevel, age, race, gender, afterQuest, quests, skillRank, hasItem}`
(`questReq()`). Categories are `QUEST_CATS`. Read by `quests()` and `questPage()` (the Quests list
and pages) and `questRefs()` (the quest tables on NPC, monster and item pages).

#### `collections`: collection books

`{book item id: {e: slots [[item | 0, condition, {prefix, suffix} | {race}]], r: rewards, b:
treasure box {n, i: [{item, count, chance, ...}]}}}`:

```json
"1212": {"e":[[58506,""],[58507,""],[58508,""],[58509,""]],"r":[{"kind":"item","item":53024,"count":1}]}
```

Read by `collectionSection()`: the "Collection" section on the books' and the collected items'
pages; the home page's "Collection books" tile counts them.

#### `dungeons`

`{p: room types [[kind, what the room does]], d: {dungeon key: {n: name, r: [region id, name], pv:
private, en: ways in, v: other dungeons its altar sends to, f: floors, b: bosses, w: rewards}}}`.
A floor is `{n, s: [w, h], c: critical path, x: sections [{c: [min, max] rooms, p: room pool
[[weight, room type (index into p), monsters [[race, count, chance % (-1 depends)]]]]}]}`; a boss
`{f: floor, m: [[race, count]], c: chests}`; a reward `[item, count, chance % (-1 unknown), where,
note, prefix, suffix]`.

```json
"tircho_alby_dungeon": {"n":"Alby Dungeon","r":[13,"Alby Dungeon Lobby (Before)"],"en":[{"a":"tircho_alby_dungeon","t":"any item that picks no other dungeon"}],
  "b":[{"f":1,"m":[[30004,1],[30003,6]],"c":"one per party member (up to 8)"}],"w":[[2000,1,100,"boss room chest","gold; amount worked out at run time",0,0], ...], ...}
```

Read by `dungeons()` and `dungeonPage()`: the Dungeons list and pages; region pages list the
altars.

#### `fishing`

`{f: fish sizes {item: [min, max] cm}, g: grounds {key: {n: name, k: spot | event | conditional, w:
[[region, name, spots]], t: when it applies, b: [bait items], rd: [rod items], c: catches [[item,
chance %, category, quest]]}}}`:

```json
"JP_Nekojima_islet": {"n":"Nekojima Island","k":"spot","w":[[600,"Nekojima Island",6]],"c":[[50254,15.3652,"fish"],[65028,10.0572,"item"], ...]}
```

Read by `fishingGrounds()`, `fishingPage()` (the Fishing list and pages) and `fishingSection()`
(the item pages' "Fishing").

#### `props`

`{p: {key: {n: name, k: hit | gather | hit+gather, ids: prop class ids, w: [[region, how many]], r:
results [[item, chance %, min, max, how (hit | gather), tool, success % (-1 none), condition,
region (0 anywhere)]], nf: 1 when the name is made from its internal name}}, g: {region id: name}}`:

```json
"1": {"n":"Tree 01","k":"hit+gather","ids":[1],"w":[],"r":[[52001,10,1,1,"hit","",-1,"",0], ..., [63002,100,1,1,"gather","an axe",50,"",0]],"nf":1}
```

Read by `propPages()`, `propPage()` (the Props list and pages) and `propSection()` (the item pages'
"From props").

#### `missions`: shadow and theatre missions

`{id: {n, t: shadow | theatre, d: description, o: objective, r: [region, name], gv: givers [{type,
npc, item, level, prop_name}], p: party [min, max], tm: time limit (s), lv: total level, rq: items
needed [[item, difficulty]], w: spawns [[stage or wave, times, [[chance, [[race, count, boss]]]]]],
df: difficulties [{n, lv, x: exp, g: gold, s: [monster life ×, attack ×], i: rewards [[item, chance,
count, prefix, suffix, chest, when]]}]}}`:

```json
"201129": {"n":"(Event) Protect Tara in the Shadow Realm","t":"shadow","d":"Protect Tara in the Shadow Realm and you might receive an unexpected reward.",
  "o":"Monsters appear when all 8 orbs go out.","r":[401,"Tara"],"gv":[{"type":"board","prop_name":"Shadow Mission Bulletin_Tara"}],"p":[8,8], ...}
```

Read by `missionPage()` (the Missions list and pages) and `missionRefs()` (missions listed on
monster and item pages).

#### `regions`

`{id: {n: name, a: area, k: kind, tw: 1 a town, tn: the town's name, e3: 1 nothing to draw in 3D, l:
[connected regions], s: field spawns [[race, count, respawn min s, max s, place, when, x, y]], np:
NPCs [[npc, x, y, place, event 0/1]], b: field bosses [{r: [races], w: when (in words), s: its
schedule (`bossFormula()` reads it), ev: event code, c: chance %, lt: stays (s), z: [[x, y,
place]]}]}}`. Kinds are the keys of `REGION_KIND` (`field`,
`interior / sub-area`, `dungeon_lobby`...).

```json
"2": {"n":"Erskin Bank","a":"Uladh","k":"interior / sub-area","l":[1],"np":[["bebhinn",1364,1785,"Erskin Bank",0], ...]}
```

Read by `regionData()`, `regionById()` and `regionPage()`: the Regions list and pages (with the
minimap's pins), the NPC infobox's "Region", field-boss lines on monster pages, and links from
missions, props and fishing.

### Worked examples: hand-editing content

Each of these goes where the script says `// ---- your edits go here ----`. They were checked on a
copy of the site: after the edit and a refresh, each shows where described. (They are examples,
not real game facts: don't keep them.)

**(a) A drop source.** Red Fox (race 50002) drops a Wooden Stick (item 40001), 1.5% per kill:

```js
S.items['40001'].push({ k: 'monster', t: 'monster', id: '50002', c: 1.5 });
```

`#/item/40001`: "Where to get it" → "Monster drops" lists Red Fox at 1.5%. `#/monster/50002`: its
"Drops" section lists the Wooden Stick. Add `w: str('Only at night')` for a note.

**(b) A shop item.** Nora sells Bread (50004) in her Cooking Tools tab, one for 60 G. The NPC's
shop comes from `shops`; the item page's shop line from `items`, so add both:

```js
S.shops.nora.t.find(t => t[0] === 'Cooking Tools')[1].push([50004, 60, 1]);
(S.items['50004'] = S.items['50004'] || []).push({ k: 'shop', t: 'npc', id: 'nora', w: str('Nora shop, Cooking Tools tab'), p: 60 });
```

`#/npc/nora`: the Cooking Tools tab has Bread, 60 G. `#/item/50004`: "Where to get it" → "Shops"
has a row for Nora: Tab "Cooking Tools tab", Price 60 G.

**(c) A step in a questline.** A third step at the end of Aeira's Lunchbox (`st` rows are
`[quest, name, step, how, condition, branch, note]`):

```js
S.questChains.find(c => c.id === 'aeira-lunchbox').st.push(
  [20001, "Deliver Ranald's Armor", 3, 'Ferghus gives it once the lunchbox is delivered.', 'Level 5 or higher', '', '']);
```

`#/questline/aeira-lunchbox`: step 3 appears in the Steps table. `#/quest/20001`: a "Questline"
section says Aeira's Lunchbox, step 3 of 3, with the step before it. To put a step in the middle,
`splice` it in and renumber the steps after it; to add another path for an existing step, give it
the same step number and a `branch` text.

**(d) A way to learn a skill.** Duncan teaches Rest (skill 10004) at Novice:

```js
S.skillSources['10004'].push(['npc', 0, 'duncan', 0, str('Talk to Duncan with the "Skills" keyword; he teaches Rest at Novice.'), str('Humans only.'), str('Novice'), 0, 0]);
```

`#/skill/10004`: "How to get" → "Ways to learn it" has a row from Duncan (NPC) with that text, rank
Novice and the condition.

## How to check your change

1. Set the data up first if `data/` isn't there yet (see [Updating the game
   data](#updating-the-game-data)). Then open `index.html` straight from disk, or serve the folder
   with any static server (for example `python -m http.server 8000` in this folder, then
   http://localhost:8000/), which is how visitors of a host see it. The 3D models are read with
   `fetch`, so they need the server; opened from disk, a 3D view may ask for your Mabinogi folder
   instead. Served, the sidebar has no "Update from your game files" (that is only shown when opened from disk).
2. Open the browser's developer console (F12) and browse the pages you changed. A page that fails
   to build shows an error and logs `Building <type> <id> failed:` with the error.
   `WIKI_SOURCE.page('item', '40001')` in the console returns a page object to inspect.
3. Build every page. Paste this into the console on any wiki page:

   ```js
   (async () => {
     const S = window.WIKI_SOURCE, types = Object.keys((await S.meta()).types);
     let built = 0, failed = 0;
     for (const type of types) {
       for (const row of await S.index(type)) {
         try { await S.page(type, row.id); built++; }
         catch (e) { failed++; console.error('Failed:', type, row.id, e); }
       }
     }
     console.log(`${built} pages built, ${failed} failed`);
     return { built, failed };
   })();
   ```

   It builds every page of every type (about 29,000) and ends with "N pages built, M failed",
   logging each failure's type, id and error. It takes about five minutes; keep the tab in front
   (browsers slow down timers in background tabs). To check one type only, replace `types` with a
   list, such as `['quest', 'questline']`. Expect 0 failed.
4. Check both themes: the sun/moon button at the top right (or `index.html?theme=dark`). Colours
   come from the tokens in `app.css`, so a new style that uses a fixed colour will look wrong in
   one of them.
5. Check a phone width: the browser's device toolbar (Ctrl+Shift+M in Chrome or Edge), about 390
   pixels wide. Nothing should scroll sideways except wide tables inside their own box;
   `document.documentElement.scrollWidth <= innerWidth` in the console should be `true`.

## Updating the game data

The game data in `data/` is made from the game's own files, in the browser, by the setup in
`lib/` (nothing is uploaded anywhere):

1. Open `index.html` from this folder, from disk, in Chrome or Edge. With no `data/` yet it shows
   the setup (the console's "file not found" errors for the `data/` scripts are expected then).
   With data already there, click "Update from your game files" at the bottom of the sidebar (it is only shown when the
   page is opened from disk).
2. Choose your Mabinogi folder (or the `package` folder inside it).
3. Include 3D models: Yes or No. Yes adds every model the pages show to `data/models/` (about
   520 MB in about 24,000 files, and processing takes longer); No leaves the pages without 3D views.
4. Where: "Into this folder" writes `data/` next to `index.html` (the browser asks you to choose
   the folder, to allow it), or "Download the data" gives a zip of `data/` to extract into the
   folder. Firefox can't write into a folder, so it gets only the zip.
5. Click Process. The setup screens give no time estimates (it varies between computers).

It writes only `data/`: `game-data.js` and its parts (old parts are removed first), the picture
lists and pictures, `site.js` (the 3D setting) and, with 3D, `models/`. Your changes to the code
files (`index.html`, `app.js`, `app.css`, `datasource.js`, `sources.js`, `lib/`) stay. Then upload
the whole folder. On a web host, visitors never see the setup; a copy uploaded without `data/`
shows "This wiki isn't ready yet".

After an update (a game patch, say), run the "build every page" check above: a patch can change
the files the wiki reads.
