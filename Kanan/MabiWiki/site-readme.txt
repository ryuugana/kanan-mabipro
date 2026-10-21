Mabinogi Wiki, for hosting
==========================

index.html is the wiki. Nothing runs on the server: every page is made in the visitor's browser,
so any static web host serves this folder, and it also works opened straight from disk.

Setting it up
-------------
The wiki is made from the game's own files, so this folder needs its data folder before it's
hosted. If it has none yet (or after a game update):

1. Open index.html from this folder in Chrome or Edge (Firefox works too: it gives you the data
   as a zip to extract into this folder).
2. Choose your Mabinogi folder (or the package folder inside it), whether to include the 3D
   models, and click Process. With data already there, click "Update from your game files" at
   the bottom of the sidebar first.
3. The data folder is written into this folder: about 175 MB, or about 695 MB with the 3D models
   (which also take longer to process).

Nothing is uploaded while you do this; the folder is then ready to upload as it is.

Hosting
-------
Upload this whole folder as it is:

- Netlify or Cloudflare Pages: drag the folder onto their deploy page.
- GitHub Pages: put the folder's contents in a repository and turn on Pages. The .nojekyll
  file keeps GitHub from leaving out folders whose names start with "_".
- Your own web server (nginx, Apache, IIS...): copy the folder into the site's folder.

Check the folder's size and file count against your host's limits (GitHub Pages: 1 GB;
Cloudflare Pages: 20,000 files and 25 MB a file). Without the 3D models the data is about
175 MB in about 9,000 files; the models add about 520 MB in about 24,000 files.

Visitors only ever see the wiki: a hosted copy never asks them for game files. A copy uploaded
without its data folder tells them it isn't ready yet.

What's in it
------------
index.html         the page: it loads the files below with plain <script> and <link> tags
app.css            the look: colors (light and dark), layout, fonts
app.js             the app: the sidebar, search, lists and how a page is laid out
datasource.js      makes every page from the game's files in data/
sources.js         what the game's files don't say: where items come from, and more
data/              made by the setup: the game's files (game-data.js, which loads its parts
                   game-data-N.js), the picture lists (item-icons.js, skill-icons.js,
                   npc-images.js, maps.js), the pictures (icons/, portraits/, maps/), the 3D
                   models (models/, when included) and site.js (whether it has the 3D models)
lib/               the 3D viewer (viewer3d.js, region3d.js, packs.js, three.js) and the setup
                   (boot.js, which also starts the wiki, and images.js, sinks.js, prepare.js)
DEVELOPING.md      how the code works and how to change it

3D models
---------
data/site.js says whether this copy has them. With them, they're read over the web, so the 3D
views show them once the site is hosted; opened from disk, a browser may not read them and asks
for the visitor's Mabinogi folder instead (to try them on this computer, run a small web server
in this folder, such as "python -m http.server 8000", and open http://localhost:8000/).
Without them, the pages have no 3D views. Set the data up again to change it.

Changing it
-----------
The code is plain JavaScript and CSS with no build step: change app.css, app.js or
datasource.js, save, and refresh the page. Each file starts with notes on how it works, and
DEVELOPING.md explains the rest. Setting the data up again only writes data/, so your changes
stay.
