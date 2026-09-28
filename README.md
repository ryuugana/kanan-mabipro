# Kanan for MabiPro
A fork of [Kanan's New Mabinogi Mod](https://github.com/cursey/kanan-new) with over 100 mods for G13 Mabinogi on MabiPro. It includes every AstralWorld patch, and it loads without replacing any of the game's files.

## Preview
<img width="526" height="692" alt="image" src="https://github.com/user-attachments/assets/d6c858e3-58fd-4f6c-bdbf-7baf25a985b2" />

## Download
[Download the latest KananMabiPro.zip here](https://github.com/ryuugana/kanan-mabipro/releases/latest/download/KananMabiPro.zip).

## Install
1. Close the game.
2. Extract KananMabiPro.zip into your MabiPro folder (for example `C:\Nexon\MabiPro`), keeping its folders. That puts:
   * `Kanan.asi` in `system\mss`
   * `Patches.json` in the MabiPro folder, next to `Client.exe`
3. Start the game from the MabiPro launcher. The launcher lists `Kanan.asi` as not part of MabiPro; choose to keep it.
4. In game, press **Insert** to open Kanan.

Kanan is loaded by the game's own sound library, which loads every plugin in `system\mss`. No game file is replaced, so the launcher's file check stays happy.

## Using Kanan
* **Insert** opens and closes the menu. You can change the key in Kanan Settings.
* Closing the menu saves your settings to `config.txt` in the MabiPro folder.
* The search box finds mods by name, description or section.
* On first start, Kanan offers a recommended set of mods. You can apply it again later from Kanan Settings.
* Kanan writes what it does to `kananLog.txt` in the MabiPro folder. Include that file when reporting a problem.

## Features
Every mod is off until you turn it on, except those in the recommended set if you choose to apply it.

**Kanan itself**
* Mod menu sorted by topic, with a search box
* Recommended settings for new players
* Rebindable menu key, and a key to open the housing board
* Adjustable font size for Kanan's windows
* Mod windows you can move without opening the menu
* Log window, and a frame time graph
* Notices when a new version is out (can be turned off)

**Interface**
* Assistant Character Location: shows where your assistant characters were last
* Clear Dungeon Fog: removes the fog of war from dungeon minimaps
* Display Names From Far: shows character, guild and item names from much farther away, without fading
* Dungeon Map Resize: lets you resize the dungeon map
* Enable Minimap Zoom: lets you zoom the minimap on every map
* Enable NPC Equip View: lets you view NPCs' equipment
* Enable Self Right-Click: opens the right-click menu on your own character
* Entity HP: shows HP numbers over monsters and characters
* Faster Interface Windows: removes the fade and open/close animations of windows like the character and skill windows
* Font Size: changes the size of TrueType interface text
* Font Style: interface text in TrueType or bitmap fonts
* Gold Format: shows gold with commas (46,500), short (46.5k) or money letters (46k500)
* Keep Pet Window Open: keeps the pet window open after summoning a pet
* Large Clock Text: shows the in-game clock in large text
* Meditation Tint: tints characters that are meditating, and can show a Meditation condition icon
* Name Coloring: colors character names by type (player, NPC, pet, enemy)
* No Window Close On Talk: open windows, like your inventory, stay open when you talk to an NPC
* Party Board To Housing: the party board and party buttons open the housing board instead
* Remove Blacklist Button: removes the Blacklist button from other players' right-click menu
* Show Clock Minutes: the in-game clock shows exact minutes instead of rounding down to 10
* Show Combat Power: shows the combat power and max HP numbers next to character names
* Show Detailed FPS: shows the detailed frame rate and rendering statistics
* Show Exploration Percent: shows your exploration level and percent in the character window
* Show Item ID: shows each item's ID in its description
* Show Item Price: shows the shop buying and selling price in item descriptions
* Show Negative HP: shows HP below zero instead of stopping at 0
* Show Negative Stats: shows Strength, Dexterity, Intelligence, Will and Luck below zero
* Show Poison Durability: shows how much poison durability is left on poisoned items
* Show Simple FPS: shows a small frames-per-second counter
* Show True Durability: shows exact item durability in descriptions, with item colors
* Show True Food Quality: shows the exact quality number next to the stars on food
* Show True HP: shows your real maximum HP instead of the capped value
* Show Unknown Quest Objectives: shows quest objectives that haven't been revealed yet
* Show Unknown Skill Requirements: shows the hidden requirements for training skills
* Show Unknown Titles: shows every title in the title list, including ones you don't know yet
* Show Unknown Upgrades: shows the requirements of skill ranks you haven't unlocked yet
* UI Scale: makes the game's interface bigger, with sharp or smooth text

**Graphics & Camera**
* Block Critical Hit Effects: hides Critical Hit effects
* Borderless Window: runs the game in a borderless window or borderless fullscreen
* Disable Auto Camera: turns off the automatic camera, handy in Tower Cylinder
* Disable Character Alpha Transparency: characters don't turn see-through when you zoom in close
* Disable Cloud Render: stops clouds from being drawn
* Disable Flashy Dyes: shows flashy dyes on worn equipment as their plain color
* Disable Gray Fog: removes the gray distance fog
* Disable Inventory Flashy: shows flashy items in your inventory and on the ground as their plain color
* Disable Lights: turns off most light props, for more FPS in places like Emain Macha
* Disable Nighttime: keeps the sky looking like daytime between 18:00 and 4:00
* Disable Player Effects: turns off most player effects, for more FPS in places like Tara's castle
* Disable Screen Shake: stops the camera from shaking
* Disable Sunlight Glare: removes the bright sunlight glare and glow
* Display Scaling: makes the game sharp instead of blurry on high resolution screens (1440p, 4K, laptops) with Windows display scaling above 100%
* Field Of View: changes the camera's field of view
* Fix Giant Camera: uses the regular camera for giant characters
* Free Indoor Camera: lets you rotate the camera freely indoors
* Freeze Camera Angle: locks the camera's angle and position, handy for screenshots
* No Render Sky: doesn't draw the sky
* Render Distance: how far away the world is drawn
* Texture Stop: uses the lowest texture detail
* Zoom Limit: lets the camera zoom out farther

**Combat & Skills**
* Always Enable Attack with Pet: always lets you attack with your pet
* Combat Mastery Swap: attacking with no skill loaded loads a skill of your choice instead, like Smash
* Default Ranged Swap: uses another ranged skill in place of Ranged Attack (Magnum Shot, Arrow Revolver, Support Shot, Mirage Missile, Crash Shot)
* Delag Skill: reduces skill lag
* Disable Skill Locks: lets you use other skills right after Flame Burst
* DPS Meter: shows your damage per second
* Elf Lag Fix: stops ranged skills misbehaving on high ping by turning off aiming while moving
* Show Objects In Hide: shows characters and things that are hidden, like elves using Hide
* Target Props: Ctrl-targeting can also pick props while in combat mode
* Target Resting Enemies: lets you target mimics, watermelons, sulfur golems and flying books while they're still resting
* Tick Timer: shows the timing of the game's regeneration ticks

**Chat & Messages**
* Block Critical Text: hides "Critical Hit!!!" from chat
* Block Party Ads: hides party advertisements
* Block Pet Pickup Messages: hides your pet's item pickup messages
* Block Pet Status Messages: hides your pet's status messages
* Block Spam: hides spam messages from chat
* Block System Spam: hides many system and some combat chat lines, including EXP gained
* Chat Commands: adds chat commands such as .help and .ping
* Chat Log: saves chat to a log file and shows it in a window
* Disable Pet Summon Messages: hides the messages when a pet is summoned
* Disable Skill Rank Up Message: hides the skill rank up message
* No Channel Penalty Message: hides the warning when you change channels during or right after combat
* No SM Clear/Fail Message: hides the Mission Complete and Mission Failed messages
* Remove Chat Restrictions: allows chat spam and repeated messages
* Scrolling Messages To Chat: copies the messages that scroll across the screen into chat (field bosses, auctions)

**Convenience**
* Auto Login Channel: logs in to the channel you choose automatically
* Auto Mute: mutes the game while it's in the background
* Block Ending Ads: stops the ad popup when you close the game
* Enable Cutscene Skip: lets you skip cutscenes
* Enter Remote Shop: opens player shops from any distance
* Fast Flight: faster turning and climbing while flying
* Fast Nao: Nao appears right away when you revive
* Instant Conversation: NPC conversations show their text at once instead of letter by letter
* Item Split Quantity: the amount the item split window starts at
* Move While Talking: lets you walk around while talking to an NPC
* Nao Counter: counts Nao Soul Stone revives
* No Channel Move Denial: removes the check that stops you from changing channels in some situations
* No Mount Timeout: your mount isn't sent away after a while
* No Pet Idle: while you play as your pet, it doesn't wander when you're away from the keyboard
* Perfect Cooking Bar: the cooking bar doesn't shake when adding ingredients
* Remove Login Delay: removes the 30-second wait to log in again after a disconnect
* Stay As Alchemy Golem: keeps you in control of your golem when it goes out of range
* Talk To Unequipped Ego: lets you talk to your spirit weapon without equipping it
* Time Alarm: alarms at in-game times, like transformation time
* Uncap Alchemy Auto Production: removes the limit on alchemy auto production
* Uncap Auto Production: removes the limit on how many items the production window makes in one go
* Uncap Flying Height: mounts can fly as high as you like
* Warn Drop On All Items: with the game's drop warning on, asks before dropping any item, not only expensive ones

**Screenshots**
* Lossless Screenshots: also saves each screenshot as a PNG
* Screenshot Fix: fixes screenshots not being saved with Windows display scaling above 100%
* Screenshot Quality: the JPEG quality screenshots are saved at

**Performance & System**
* Auto Set MTU: sets your network MTU when you log in or change channels
* CPU Scheduling: fixes freezes on Intel CPUs with performance and efficiency cores
* Disable Nagle: sends network messages right away, for less lag
* Max Frame Rate: limits the frame rate, with a separate limit while the game is in the background
* Overlay Detection: warns about overlays like MSI Afterburner/RTSS that can hide Kanan's menu

**Fun**
* Basic Ghost Lock: aim and use non-target skills on other players
* Derandomize Login Screen: always shows the Morrighan statue on the login screen
* Enable Naked Mode: draws characters without their clothes and heads
* Far Dice Throw: throw dice much farther away

**Debug**
* Debug Wireframe Mode: draws the world as a wireframe

## Requirements
* Windows, and MabiPro installed with its launcher.
* Nothing else to install: Kanan includes the Visual C++ runtime it needs.

## Building
* Visual Studio 2017 or later, with the Visual Studio 2017 (v141) C++ build tools.
* Open `Kanan.sln` and build **Release | x86**. The build makes `Release\Kanan.asi` and copies `Patches.json` next to it.

## Credits
Many original patches and ideas came from these projects:
* AstralWorld (Fantasia)
* MAMP
* JAP
* Gerent/GerentxNogi
* MNG
* Noginogi-Party
* DataCami
