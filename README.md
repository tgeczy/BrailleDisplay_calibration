# Braille Display Calibration Tool (Windows)

A small Windows GUI tool for testing and calibrating **8-dot braille displays** by flashing dot patterns across a configurable number of cells.

This tool is designed to avoid a moving text caret during output (to reduce cursor artifacts on braille displays) and to support **multi-line displays** by outputting a **single long line** of braille cells.

---

## Why a “single long line” output?

Many screen readers and braille drivers will only show one “current line” of text if the output is stored as multiple lines in a text field.  
Instead of writing `rows` lines, this tool outputs **one line** of length:

- `cells = columns × rows`

Example:
- 24 columns × 4 rows = **96 cells** (one 96-character line)
- 30 columns × 10 rows = **300 cells** (one 300-character line)

Multi-line braille displays typically wrap that long line across physical rows.

---

## Before you start

1. Set your braille translation/grade to **8-dot Computer Braille**.
2. If you see dots 7–8 “cursor” markings interfering with patterns, turn off braille cursor/selection indicators in your screen reader settings (if available).  
   This tool avoids a standard text caret, but the screen reader may still display its own braille cursor in some situations.

---

## Features

### Modes (pattern choices)
All modes can run in two ways:
- **Walking** (default): flashes one cell at a time across the whole cell range.
- **Blink whole line** (checkbox): fills all cells at once (useful for stress testing, less useful for pinpointing a single cell fault).

Pattern modes include:
- All dots (1–8), row-major walk
- All dots (1–8), column-major walk
- Random dot groupings
- Dashes cycle: 1-4 / 2-5 / 3-6 / 7-8
- Dots 7-8
- Dots 1-2-3-7
- Dots 4-5-6-8
- Alternating 1237 / 4568
- Additional dot group patterns (e.g. 1-3-4-6, 1-2-5-6, etc.)

### Controls
- **Start** begins output.
- **Stop** ends output and clears the display.
- While running:
  - Press **S** to stop (if focus is on the output area)
  - Press **Esc** to stop
- When idle:
  - Press **Esc** to close the app

---

## Build (CMake + MSVC)

### Requirements
- Windows
- Visual Studio Build Tools / MSVC
- CMake 3.20+

### Build steps
From the repo root:

```bat
cmake -S . -B build
cmake --build build --config Release
