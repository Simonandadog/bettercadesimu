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
- Pan / zoom / fit-to-window canvas with a snap-style grid. Drag with the
  middle mouse button to pan; the wheel zooms around the cursor.
- Rubber-band selection in every direction, plus hover highlight.
- Drag objects to move them (snapped to the grid) and <kbd>Del</kbd> to remove them.
- Rotate selected symbols 90° with <kbd>R</kbd> (anticlockwise) or
  <kbd>Shift</kbd>+<kbd>R</kbd> (clockwise). Labels stay upright.
- Undo / redo for placement, movement, rotation, editing and deletion
  (Ctrl+Z / Ctrl+Shift+Z).
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
  palette, rendering per-section artwork proof sheets, converting `.cad` files
  to the native project format, and exporting `.bcs` projects back to `.cad`.
- File → Export CADe SIMU… writes Latin-1 `.cad` documents with the original
  metadata block, eight pin slots per record, and the rotation field set from
  the editor's rotate command.
- Drag-and-drop and recent-files support.
- Relay-logic simulation: press <kbd>F5</kbd> to run, press and hold push
  buttons to operate them, click switches to latch them, and <kbd>F6</kbd> to
  release everything. Live wires and energised coils, lamps and motors are
  highlighted; label-matched contacts follow their contactor/relay coils,
  including seal-in latches.
- Ready-made circuits in `examples/`: `latching-start.bcs` (control-only
  start/stop station) and `dol-starter.bcs` (three-phase direct-on-line motor
  starter with a seal-in contactor).

Three-phase power flow, timers, PLC logic and the rest of the catalogue come next.

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

Export a project as a CADe SIMU document:

```sh
./build/bettercadesimu --export-cad out.cad examples/latching-start.bcs
```

The same command is available as **File → Export CADe SIMU…**. Imported `.cad`
files keep their original header and metadata block; new `.bcs` projects get a
fresh metadata block using the output file name as the title.

## Simulation

Press <kbd>F5</kbd> (or choose **Simulation → Run**) to build the circuit and
solve it. While running:

- Press and hold a momentary push button (types 8000-8015). Maintained
  switches and selectors (2003-2005, 2007 and 8020-8022) latch on a click;
  thermal-overload contacts (8016-8019) stay in their fixed NC/NO state until
  tripping is modelled. <kbd>F6</kbd> releases every button and switch.
- Wires that carry a live or return rail glow green, and energised loads
  (coils, lamps, motors, timers, resistors) get a green outline.
- Contactors and relays are linked by their reference label: a coil labelled
  `-K1` drives every contact labelled `-K1`. Normally-closed contacts invert
  the action. Coil state is retained between solves, so a seal-in contact keeps
  a contactor latched after the start button is released.
- Combined contactor-relay symbols (1070-1073) carry their A1/A2 coil and
  power contacts in one component.
- Busbars and terminal strips (9040-9043, 10100-10112) only join pins that
  share the same terminal name, so an L1/L2/L3/N block never shorts phases
  together.
- Power comes from the phase/neutral feed symbols, 3014/3018 supplies and
  3019 batteries. The solver reports when it cannot find a live or return rail.

This version models single-phase control circuits and a simplified
phase/neutral reachability check for three-phase motors. Timer delays, PLC
blocks, drives and full three-phase power flow are still to come.

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
      "rotation": 90,
      "fields": ["15", "6", "0", "0", "-12", "-9", "6", "15", "0", "0", "0", "1", "0", "0", "0"]
    }
  ]
}
```

`fields` holds `value` or `value#text` strings in file order, exactly as the
`.cad` parser exposes them. `rotation` is optional and written for rotated
symbols (90, 180 or 270 degrees clockwise). The parser reads CADe's tenth tail
field as a quarter-turn count, so imported `.cad` files retain their rotation.
`cadHeader`, `cadMetadata` and trailing `#` markers are preserved when present,
which keeps an imported `.cad` file byte-identical after a load/export cycle.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

Optionally point the suite at a real schematic, and ask it to verify that a
parse/export cycle is byte-identical:

```sh
BETTERCADESIMU_FIXTURE="path/to/schematic" \
BETTERCADESIMU_ROUNDTRIP=1 ./build/cad_parser_test
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

The tail fields used by the editor are:

| Tail index | Meaning |
| --- | --- |
| 0, 1 | insertion point, or wire start |
| 2, 3 | wire end, or component secondary point |
| 4–7 | local box `left, top, width, height` (for selection and labels) |
| 8 | colour or fill flag |
| 9 | clockwise orientation as `rotation / 90` (components) |
| 10, 11, 12 | per-record flags; field 11 is normally `1` |
| 13 | net number (wires) or first-pin net (components) |
| 14 | inline text, always stored as `value#text` |

CADe writes the type field with eight pin slots even for two-pin symbols, and the
exporter pads short type fields the same way. Orientation values outside `0..3`
(seen on a few catalogue records) are preserved verbatim rather than reinterpreted.

## Layout

```
src/cad/        format parser, project model and palette
src/sim/        relay-logic connectivity and simulation
src/ui/         Qt canvas, symbol painting, main window
examples/       sample .bcs schematics
tests/          parser, project, canvas-editing and simulation tests
```

## Licence

TBD. The source code in this repository is original work.
