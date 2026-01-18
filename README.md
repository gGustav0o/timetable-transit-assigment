# Timetable Transit Assignment

Timetable-based public transit assignment. The core algorithm follows the branch-and-bound timetable assignment approach
outlined in `Timetable-Based_Transit_Assignment_Using_Branch_an.pdf`.

## Build

This project uses CMake and vcpkg.

```
cmake --preset default
cmake --build --preset default
```

## Run

In Debug builds, if no argument is provided, the app will look for `data/default`
starting from the current working directory and walking up parent directories.

```
timetable-transit-assigment.exe D:\path\to\project\data\default
```

If no argument is provided in Release builds, the app exits with an error.

## Input directory layout

The input directory must contain the following files:

- `stops.csv`
- `trips.csv`
- `stop_times.csv`
- `walk_links.csv`
- `od.csv`
- `params.json`

There is a default dataset location at `data/default`.

## Logging

- Logs are written to `logs/` by default.
- The same log stream is mirrored into the UI log panel.

## UI

The UI is intentionally minimal:

- main panel: status text lines
- log panel: latest log messages

Use `q`, `Esc`, or `Ctrl+C` to exit.

## Architecture overview

- `domain/` — pure types and algorithms (math-first, no I/O)
- `io/` — abstract input interfaces
- `infra/` — concrete adapters (file input, logging sinks)
- `ui/` — FTXUI rendering only
- `app/` — orchestration, threads, wiring

## Notes

- The current file data source is a stub (`not implemented`); only input validation is active.
- Error handling uses `mathfp::Expected` end-to-end.