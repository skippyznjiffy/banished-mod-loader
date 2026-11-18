# Plugins

## Town Stats

Adds a Town Stats button to the gear menu that opens an overlay over the game, with five pages
on tabs along its top and its own close button.

- Overview: how many people there are and who they are, how long the food lasts, what is
  harvested and eaten a season, health, happiness, education and clothing, and a row for every
  kind of goods with its stock, a small line of the stock, what is made and used a season, the
  net of the two and when the stock runs out at that pace. Click a row for its charts.
- Food: the stock, what is harvested and eaten a season, what the town needs a season at its
  size, the net and how many months the stock lasts if nothing more is harvested. Under that a
  chart of harvested and eaten, what each season of the year brings and takes, and the stock.
- Goods: the same for one kind of goods, picked from a list on the left.
- People: population, adults, students and children with their change in a year, a chart of who
  lives in the town, and health, happiness, education and clothing now and over the years.
- Charts: a chart for every kind of goods you tick, side by side, of what is made and used, of
  the stock, or of the net.

Every page has the time span on its top right, from one year to the whole save, and Smooth
seasons. With that on, what is made and used is the average of the year before each point, so a
harvest that lands in one season does not make a spike. Off shows the season before each point,
as the game counted it. Hover a chart for the numbers at that date. The overlay keeps the mouse
while it is under it, so clicks on it never reach the town. What you pick is saved in
`NativeMods\townstats.ini`, the window's place and size in `townstats-layout.ini`.

It needs the DirectX 11 renderer (Video Options in the launcher). The overlay is drawn with
[Dear ImGui](https://github.com/ocornut/imgui) and [ImPlot](https://github.com/epezent/implot),
both MIT licensed, in `third_party`. The goods icons are the game's own, read from its sprite
sheet while it runs, the others are [Lucide](https://lucide.dev) (ISC).

`tools\townstats-preview\build.bat` builds the overlay as a program of its own, to work on it
without the game: `out\preview\townstats-preview.exe --records <file>` opens it in a window over
a grey backdrop, and `--shot <png>` writes one frame as a picture instead. `--page`, `--range`,
`--raw`, `--goods`, `--window W H` and `--mouse X Y` set what that frame shows.

## Grid Build

Pick a house (or any building), hold Shift and drag. The area fills with rows of buildings
side by side. Two rows face each other across one road and share it as their door row, and
the next pair starts right behind them, so the area is rows of houses with only roads in
between. Ghosts of the buildings follow the drag and only show where one fits. Let go to
place them, right-click or Escape cancels.

| Keys | Roads |
|---|---|
| Shift + drag | dirt |
| Shift + Ctrl + drag | stone |
| Shift + Alt + drag | none |

The rotate keys turn the rows. Spots that are blocked are skipped. Every building is an
ordinary build order, so it needs builders and materials and saves like any other.

## Area Fill

Pick a crop field, orchard, pasture, stockpile or cemetery, hold Shift and drag over
ground that is only partly usable. The usable part is split into the largest areas that
fit and each one is placed as its own field. While dragging, the areas show in two greens
so the splits are visible and red marks what is left out. Right-click or Escape cancels.

## Road Fill

Pick a dirt or stone road, hold Shift and drag. Instead of one line, the whole rectangle
is paved. While dragging, green shows where road goes and red the tiles the road tool
would refuse there (water, steep slopes, buildings, road that is already down). Right-click
or Escape cancels. Each tile is an ordinary road order, so builders still lay it and stone
roads still cost stone.

## Replant

Pick the Forester Lodge, hold Shift and drag over open ground to plant a forest there:
about 6 trees in 10 free tiles, every free tile with Ctrl as well. Trees are mixed kinds
and ages, mostly close to grown, and only go where a forester could plant one.

## Farm Limits

Crop fields stop harvesting once the town is at its food limit (planting and tending go
on), and pastures stop culling. With this plugin a field growing wheat keeps harvesting
past the limit and pastures keep culling, fields of every other crop still stop. Eggs and
wool keep their own limit. With the Wheat for Ale data mod, wheat no longer counts as food
either, so the wheat fields keep farming for the tavern however full the barns are.

## Storage Sync

A barn, stockpile, market or tavern keeps the capacity it was built with in the save, so a
data mod that makes storage bigger only reaches buildings put up after it. With this
plugin every storage building is grown to what its template gives when a game loads. It
never makes one smaller.

## Growth Sync

A tree, herb or other plant draws its lifespan from its template once, when it appears, and
keeps it in the save, so a data mod that shortens a lifespan only reaches new plants. With
this plugin every plant is cut back to what its template allows when a game loads. It never
makes one live longer.

## Auto Rebuild

A building that a fire or a tornado damaged waits for you to press Rebuild or Destroy in
its damage window. With this plugin it is set to rebuild on its own, the same as pressing
the button, including buildings that were already damaged in a save when it loads.
Builders still tear the ruin down and build it again, and the materials still have to be
there.

## Idle Work

A citizen looking for something to do tries their own job first, then the general jobs, then
the laborers' jobs. With this plugin a citizen who found nothing there helps build, so
builders are no longer the only ones putting up buildings. Builders still go first on their
own jobs, and children and students never build.

## Speed

Replaces the speed steps 1, 2, 5, 10 with 1 and the three numbers in
`NativeMods\speed.ini` (`steps = 10, 50, 250` by default).

The engine updates on a fixed time step, and at speed N the whole sim runs N times per
update. While those N steps take less than one time step, the game really runs N times
faster. Past that the engine drops the time it can't catch up on, so a higher setting
only lowers the frame rate and the real speed stops climbing. The ceiling depends on the
PC and the size of the town. The plugin logs what the game actually achieved every 10
seconds:

```
[speed] set to 250x: ran <real>x real time, <n> updates a second, <ms> ms each
```
