Majere's TES3MP client
======================

This is a personal fork of the TES3MP 0.8.1 client with a set of HUD additions for alchemists and gatherers,
built on top of the unmodified upstream source below. Everything added is marked with a `majere addition` or
`majere change` comment in the code, and every new option lives in `files/settings-default.cfg` under its own
section (`[Hotbar]`, `[EffectDials]`, `[StatBars]`, `[Ingredients]`, `[SessionLog]`).

**Visual overlay only.** Everything here reads what the game already knows and draws it on your screen. Nothing
acts in the game world, presses a key for you, or sends anything to a server on your behalf: no packet is
added or changed.

**Still, ask before you use this on someone else's server.** Some servers forbid modified clients outright, and
what counts as fair is the owner's call, not yours or mine. The author only uses this client where it has been
allowed, and takes no responsibility for anyone else's use of it.

Showcase
--------

Eighty seconds of the overlays in play, captions included:

https://github.com/user-attachments/assets/349edf18-71a2-4c88-9fa9-12c5228ffe12

What is added
-------------

* **Hotbar** with drag-and-drop item and spell slots, item and spell tooltips, a page label fed by a server's
  "Quick Key Page: N" message, and a gold flash on use.
* **Effect strip**: the active effects as small dials with a soft clock sweep, a Sun Damage column that only
  shows in daylight, and the constant-effect (Azura's Star) indicator with a two-column popup.
* **Resistances**: a collapsible 3x2 grid of fire, frost, shock, magicka, poison and paralysis.
* **Stat bars** for health, magicka and fatigue beside the hotbar, with +/- signs for anything draining or
  restoring them per second and tooltips naming the sources.
* **Ingredient finder**: a mortar-and-pestle icon opens a picker that searches every ingredient record by name
  or effect, tiled as icons with effect tooltips and a colour code by effect category; up to three are tracked.
  A coin icon lists the shops in town that restock them and any seller standing nearby; an alchemy icon unfolds
  into a 3x3 map of the surrounding cells with the count of tracked plants per cell, shop markers and a facing
  arrow.
* **Session log**: a per-session plain-text log of chat, server messages and the player's vitals (read-only).

Download
--------

You do not need to build anything. The [Releases page](https://github.com/gillyguthrie/TES3MP/releases) has
a zip with the client, the runtime libraries it was built against and the resources it needs. Have the
official TES3MP 0.8.1 for Windows installed and run once, copy that folder to a new one, unzip the release
into the copy, and start `tes3mp.exe` from there. Your existing server list, name and settings carry over.
The full steps are in `INSTALL.txt` inside the zip.

Building
--------

Build it like upstream TES3MP 0.8.1 (see below); nothing new is required. Windows / MSVC 2019 is what the
author builds with. The new textures under `files/vfs/textures` are installed with the rest of the resources.

Upstream README follows.

---

TES3MP
======

Copyright (c) 2008-2015, OpenMW Team  
Copyright (c) 2016-2022, David Cernat & Stanislav Zhukov

TES3MP is a project adding multiplayer functionality to [OpenMW](https://github.com/OpenMW/openmw), an open-source game engine that supports playing "The Elder Scrolls III: Morrowind" by Bethesda Softworks.

* TES3MP version: 0.8.1
* OpenMW version: 0.47.0
* License: GPLv3 with additional allowed terms (see [LICENSE](https://github.com/TES3MP/TES3MP/blob/master/LICENSE) for more information)

Font Licenses:
* DejaVuLGCSansMono.ttf: custom (see [files/mygui/DejaVuFontLicense.txt](https://github.com/TES3MP/TES3MP/blob/master/files/mygui/DejaVuFontLicense.txt) for more information)

Project status
--------------

[Version changelog](https://github.com/TES3MP/TES3MP/blob/master/tes3mp-changelog.md)

As of version 0.8.1, TES3MP is fully playable, providing very extensive player, NPC, world and quest synchronization, as well as state saving and loading, all of which are highly customizable via [serverside Lua scripts](https://github.com/TES3MP/CoreScripts).

Remaining gameplay problems mostly relate to AI and the fact that clientside script variables need to be placed on a synchronization whitelist to avoid packet spam.

TES3MP now also has a [VR branch](https://github.com/TES3MP/TES3MP/tree/0.8.1-vr) that combines its code with that of Mads Buvik Sandvei's [OpenMW VR](https://gitlab.com/madsbuvi/openmw).

Donations
---------------

You can benefit the project by donating on Patreon to our two developers, [David Cernat](https://www.patreon.com/davidcernat) and [Koncord](https://www.patreon.com/Koncord), as well as by supporting [OpenMW](https://openmw.org).

Contributing
---------------

Helping us with documentation, bug hunting and video showcases is always greatly appreciated.

For code contributions, it's best to start out with modestly sized fixes and features and work your way up. There are so many different possible implementations of more major features – many of which would cause undesirable code or vision conflicts with OpenMW – that those should be talked over in advance with the existing developers before effort is spent on them.

Feel free to contact the [team members](https://github.com/TES3MP/TES3MP/blob/master/tes3mp-credits.md) for any questions you might have.

Getting started
---------------

* [Quickstart guide](https://github.com/TES3MP/TES3MP/wiki/Quickstart-guide)
* [Steam group](https://steamcommunity.com/groups/mwmulti) and its [detailed FAQ](https://steamcommunity.com/groups/mwmulti/discussions/1/353916184342480541/)
* [TES3MP section on OpenMW forums](https://forum.openmw.org/viewforum.php?f=45)
* [Discord server](https://discord.gg/ECJk293)
* [Subreddit](https://www.reddit.com/r/tes3mp)
* [Known issues and bug reports](https://github.com/TES3MP/TES3MP/issues)
