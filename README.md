# Timetable Transit Assignment

Timetable-based public transit assignment. The core algorithm follows the branch-and-bound timetable assignment approach
outlined in `Timetable-Based_Transit_Assignment_Using_Branch_an.pdf`.

## Build

This project uses CMake and vcpkg.

```
cmake --preset default
cmake --build --preset default
```

## Current Scope

This repository is currently developed as a minimal working timetable-assignment project.

The explicitly supported runtime contract at this stage is only the `pair-file` input path:

- `--pair-data-dir <path>`
- default auto-discovery of `data/test`

The following constraints are intentional project scope decisions for the current stage:

- only the `pair-file` data source is supported
- deprecated compatibility paths (`--data-dir`, `--data-file`) are not part of the maintained scope
- `params.txt` is intentionally ignored at runtime for now
- built-in default assignment parameters are used
- the current target is a minimal working project, not the full final product surface

## Run

In Debug builds, if no argument is provided, the app looks for `data/test`
starting from the current working directory and walking up parent directories.

```
timetable-transit-assigment.exe --pair-data-dir D:\path\to\project\data\test
```

If no argument is provided in Release builds, the app exits with an error.

## Pair Input Layout

The supported input directory must contain:

- `connection_segments_input.csv`
- `time_intervals.csv` or `generated_demand/time_intervals.csv`
- `od_demand.csv` or `generated_demand/od_demand.csv`

Optional but currently ignored at runtime:

- `params.txt`

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

- The maintained runtime path is `pair-file` input only.
- `params.txt` parsing infrastructure exists, but it is not connected to the active runtime path yet.
- The deprecated file data source remains outside the current maintained scope.
- Error handling uses `mathfp::Expected` end-to-end.
