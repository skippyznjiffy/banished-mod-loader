<p align="center">
  <img src="docs/img/banner.png" alt="BML, the Banished Mod Loader">
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
3. Start the game. The BML button in the main menu turns plugins on or off.

To remove it, delete `dinput8.dll`. No game files are changed.

## Plugins

| Plugin | What it does |
|---|---|
| Grid Build | Shift-drag any building to fill an area with rows of them and roads |
| Area Fill | Shift-drag a field or pasture over rough ground, it fits in what it can |
| Road Fill | Shift-drag a road to pave a whole area |
| Replant | Shift-drag the Forester Lodge to plant a forest |
| Speed | Speeds of 1, 10, 50 and 250 instead of 1, 2, 5 and 10 |
| Auto Rebuild | Burnt buildings rebuild themselves |
| Idle Work | Citizens with nothing to do help build |
| Farm Limits | Wheat fields and pastures keep working past the food limit |
| Storage Sync | Storage mods also grow the barns you already built |
| Growth Sync | Plant mods also apply to the trees and herbs already on the map |

More on each one in [docs/plugins.md](docs/plugins.md).

<table>
  <tr>
    <td><img src="docs/img/main-menu.png" alt="The BML button in the main menu"></td>
    <td><img src="docs/img/bml-screen.png" alt="The BML screen listing the active mods"></td>
  </tr>
  <tr>
    <td align="center"><sub>A BML button in the main menu and the pause menu</sub></td>
    <td align="center"><sub>It lists your plugins, with a checkbox to turn each on or off</sub></td>
  </tr>
</table>

<p align="center">
  <img src="docs/img/area-fill-problem.webp" width="600" alt="A crop field refused over rough ground">
  <br><sub>Without Area Fill, a few bad tiles and the whole field is refused</sub>
</p>

## Good to know

- Only works on the 64-bit Steam version, 1.0.7. On anything else it turns itself off.
- BML checks GitHub for a new version when the game starts. When there is one, the BML
  screen has an update button, and the new version loads the next time you start the game.
  Set `check = 0` under `[update]` in `NativeMods\bml.ini` to turn the check off.
- Some antivirus programs flag it, since loading through `dinput8.dll` is also what cheats
  do. The source is all here if you'd rather build it yourself with `build.bat`.

Want to write your own plugin? See [docs/api.md](docs/api.md).

MIT licensed. Banished belongs to Shining Rock Software, none of its code is in here.
