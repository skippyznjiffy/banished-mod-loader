<p align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&color=0:3b2a1a,100:6b8e3a&height=190&section=header&text=BML&fontColor=f4e9d0&fontSize=72&desc=Banished%20Mod%20Loader&descAlignY=74&descSize=20" alt="BML">
</p>

<p align="center">
  <img src="https://readme-typing-svg.demolab.com?font=Fira+Code&size=18&duration=3000&pause=1200&color=6B8E3A&center=true&vCenter=true&width=560&lines=Code+mods+for+Banished;Place+a+whole+street+in+one+drag;Run+the+game+at+50x" alt="Code mods for Banished">
</p>

<p align="center">
  <a href="https://github.com/skippyznjiffy/banished-mod-loader/releases/latest"><img src="https://img.shields.io/github/v/release/skippyznjiffy/banished-mod-loader?color=6b8e3a&label=download" alt="Download"></a>
  <img src="https://img.shields.io/badge/Banished-1.0.7%20Steam%20x64-3b2a1a" alt="Banished 1.0.7 Steam x64">
  <img src="https://img.shields.io/github/license/skippyznjiffy/banished-mod-loader?color=8a6d3b" alt="MIT license">
</p>

The Banished mod kit can only change data. BML loads real code mods, so it can add new
tools and change how the game behaves.

## Install

1. Download the zip from [the latest release](https://github.com/skippyznjiffy/banished-mod-loader/releases/latest).
2. Unzip it into your Banished folder (`steamapps\common\Banished`).
3. Start the game. Delete the plugins you don't want from `NativeMods`.

To remove it, delete `dinput8.dll`. No game files are changed.

## Plugins

| Plugin | What it does |
|---|---|
| Grid Build | Shift-drag any building to fill an area with rows of them and roads |
| Area Fill | Shift-drag a field or pasture over rough ground, it fits in what it can |
| Replant | Shift-drag the Forester Lodge to plant a forest |
| Speed | Speeds of 1, 10, 50 and 250 instead of 1, 2, 5 and 10 |
| Auto Rebuild | Burnt buildings rebuild themselves |
| Farm Limits | Wheat fields and pastures keep working past the food limit |
| Storage Sync | Storage mods also grow the barns you already built |
| Growth Sync | Plant mods also apply to the trees and herbs already on the map |

More on each one in [docs/plugins.md](docs/plugins.md).

<p align="center">
  <img src="docs/img/area-fill-problem.webp" width="600" alt="A crop field refused over rough ground">
  <br><sub>Without Area Fill, a few bad tiles and the whole field is refused</sub>
</p>

<p align="center">
  <img src="docs/img/console.png" width="600" alt="The BML console">
  <br><sub>The BML console shows what loaded and how fast the game runs</sub>
</p>

## Good to know

- Only works on the 64-bit Steam version, 1.0.7. On anything else it turns itself off.
- Some antivirus programs flag it, since loading through `dinput8.dll` is also what cheats
  do. The source is all here if you'd rather build it yourself with `build.bat`.
- There's a BML button in the main menu that lists your active mods.

Want to write your own plugin? See [docs/api.md](docs/api.md).

MIT licensed. Banished belongs to Shining Rock Software, none of its code is in here.
