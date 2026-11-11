# bettercadesimu

A clean-room, cross-platform replacement for the proprietary **CADe SIMU** relay-logic
schematic tool, built with C++20 and Qt 6.

![Viewer screenshot](docs/screenshot.png)

The goal is to re-create the *functionality* — drawing IEC motor-control schematics,
simulating contactor/relay logic, and testing control circuits — without copying any
proprietary code, symbols, or assets. The original application is closed-source freeware;
this project only reads its plain-text `.cad` document format for interoperability and
draws its own symbols from scratch.

> Not affiliated with, endorsed by, or derived from CADe SIMU.

## Status

Early work in progress. The viewer is usable and the editor core has landed:

- `.cad` parser that replays the append-only record log by object ID.
- Native `.bcs` JSON project format with save / save-as / load.
- Pan / zoom / fit-to-window canvas with a snap-style grid.
- Rubber-band selection in every direction, plus hover highlight.
- Drag objects to move them (snapped to the grid) and <kbd>Del</kbd> to remove them.
- Undo / redo for placement, movement and deletion (Ctrl+Z / Ctrl+Shift+Z).
- Copy / paste / duplicate selected objects (Ctrl+C, Ctrl+V, Ctrl+D).
- Edit an object's reference label or text with <kbd>F2</kbd> (or double-click it),
  with undo support.
- Wire and junction tools: press <kbd>W</kbd> to draw a wire (click the start,
  then the end; <kbd>Esc</kbd> cancels), <kbd>J</kbd> to drop a junction dot,
  and <kbd>T</kbd> to place a text label. Press <kbd>S</kbd> to return to
  selection.
- Symbol palette dock generated from the surveyed catalogue (18 sections,
  243 entries), with painted previews. Click an entry and then click the canvas
  to place it; double-click the entry to place it in the centre.
- Inspector dock showing the raw type field and every numeric field.
- IEC symbol painters for power feeds, fuses, breakers, contactors, motors,
  drives, contacts, push buttons, coils and timers.
- CLI modes for dumping a parsed document, rendering it to PNG, printing the
  palette, rendering per-section artwork proof sheets, and converting `.cad`
  files to the native project format.
- Drag-and-drop and recent-files support.

Rotation, property editing, simulation and the rest of the catalogue come next.

## Requirements

- CMake 3.21+
- Ninja (or another generator)
- Qt 6.5+ with the Widgets module
- A C++20 compiler

On Arch/Omarchy:

```sh
sudo pacman -S --needed cmake ninja qt6-base
```

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build
```

## Run

Open the viewer:

```sh
./build/bettercadesimu
./build/bettercadesimu "path/to/schematic"
```

Print the parsed records:

```sh
./build/bettercadesimu --dump "path/to/schematic"
```

Render a PNG (works headlessly; forces the offscreen Qt platform):

```sh
./build/bettercadesimu --render out.png "path/to/schematic"
```

Print the symbol palette catalogue:

```sh
./build/bettercadesimu --palette
```

Render one artwork proof sheet per palette section, for reviewing the symbols
against the reference screenshots:

```sh
./build/bettercadesimu --palette-sheet build/palette-sheets
```

Convert a `.cad` document to the native JSON project format (also accepts a
`.bcs` file, so it can be used to validate a project):

```sh
./build/bettercadesimu --save-project out.bcs "path/to/schematic"
```

## Native project format

Projects are UTF-8 JSON with the `.bcs` extension. Each object keeps the raw
CADe SIMU field layout, so imported documents survive a round trip:

```json
{
  "format": "bettercadesimu-project",
  "version": 1,
  "objects": [
    {
      "id": 7,
      "type": 5000,
      "typeField": "5000#-F1##1#2",
      "pinFlagCount": 8,
      "fields": ["15", "6", "0", "0", "-12", "-9", "6", "15", "0", "0", "0", "1", "0", "0", "0"]
    }
  ]
}
```

`fields` holds `value` or `value#text` strings in file order, exactly as the
`.cad` parser exposes them.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

Optionally point the suite at a real schematic:

```sh
BETTERCADESIMU_FIXTURE="path/to/schematic" ./build/cad_parser_test
```

## Reading the `.cad` format

A `.cad` file is Latin-1-ish plain text:

```
CADe_SIMU*<record>*<record>*...$$$<metadata>
```

Each record is `id`, then a type field, then `max(8, pinCount)` pin-flag fields,
then a fixed 15-field tail (23 fields in total for most symbols):

| Tokens | Meaning |
| --- | --- |
| 0 | object ID |
| 1 | type field: `type#label#sublabel#pin#pin#...` |
| 2 … 2+max(8, pins)-1 | pin flags |
| … | 15-field tail: geometry, box, inline text |

The pin-flag count is `type field parts - 3`, so nine-pin symbols carry one extra
field and shift the tail by one. Records form an append-only change log: a later
record with the same ID overrides the earlier one, so the last definition wins.
Geometry lives in the first four tail fields:

- Lines and wires use them as `x1, y1, x2, y2`.
- Components use the first two as the insertion point and fields 4–7 of the tail as
  a local bounding box.
- Text uses them as a text box.

Coordinates are rendered at 4 px per file unit, which matches the dimensions of PNGs
exported by the original application. The insertion point of a component is its first
pin; additional poles sit 6 units to the right.

## Layout

```
src/cad/        format parser, project model and palette
src/ui/         Qt canvas, symbol painting, main window
tests/          parser, project and canvas-editing tests
```

## Licence

TBD. The source code in this repository is original work.
