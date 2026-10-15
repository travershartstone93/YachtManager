# Yacht Manager

Desktop app for running the books of a yacht charter fleet. Built in C++ with
Qt6 on top of a SQLite database that an ingest script fills from the fleet's
statement-of-account workbooks (one Excel book per boat, one sheet per month)
and the live fleet list on the company website.

## What it does

- Fleet page: photo card grid of the active fleet, the high-level snapshot.
- Finance page: per-boat balances, income and expenses across the fleet.
- Boat detail: photo, balance history chart, itemised expense and income lines.
- Maintenance view and a manual entry page; manual entries survive re-ingest.
- Optional AI dock: pipes a per-yacht digest to a local LLM CLI for a plain
  text summary on demand. Nothing runs unless the button is pressed.
- Detects changed source books on launch and re-ingests automatically.

## Ingest

`ingest.py` parses three generations of workbook layout by anchoring on their
own labels (balance brought forward, expenses, income, totals due) rather than
fixed cell positions, reconciles labour hours against known rates, and treats
the source files as strictly read-only. Parsing uses python-calamine; the
workbooks carry thousands of formatted columns and openpyxl takes minutes
where calamine takes seconds.

## Build

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j8
    build/yachtmanager

Charts are drawn with plain QPainter, so the only dependency is Qt6 Widgets.

## Data

No fleet data ships with this repository. The database, spec sheet, photos
and overrides are produced locally from the operator's own files and are
excluded by .gitignore.
