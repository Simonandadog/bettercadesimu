# BetterCADe SIMU — Technical Documentation

## 1. Source Code Layout

The project repository is structured into the following directories:

* `src/cad/`: Format parser, project data model, and symbol palette.
* `src/sim/`: Relay-logic connectivity engine and simulation solver.
* `src/ui/`: Qt-based canvas, custom symbol painting, and main window logic.
* `examples/`: Sample `.bcs` schematics (e.g., `latching-start.bcs`, `dol-starter.bcs`).
* `tests/`: Automated unit and integration tests for parsers, projects, canvas editing, and simulation.

---

## 2. Requirements & Build Instructions

### Prerequisites
* **CMake**: Version 3.21 or higher.
* **Ninja**: (Or another compatible generator).
* **Qt 6**: Version 6.5+ with the Widgets module.
* **Compiler**: A modern C++20 compatible compiler.

### Installing Dependencies (Arch / Omarchy Linux)
```bash
sudo pacman -S --needed cmake ninja qt6-base