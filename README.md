# Timetable Transit Assignment

Timetable-based public transit assignment. The core algorithm follows the branch-and-bound timetable assignment approach
outlined in `Timetable-Based_Transit_Assignment_Using_Branch_an.pdf`.

## Build

This project uses CMake and vcpkg.

### Windows

Set `VCPKG_ROOT` to a user-writable vcpkg checkout used by the project presets.
Manifest mode writes under `VCPKG_ROOT/buildtrees`, so a read-only or
partially-writable vcpkg installation is not a reproducible test environment.
The repository provides a Windows test wrapper that imports the Visual Studio
C++ environment and then uses the CMake/CTest presets.

```powershell
$env:VCPKG_ROOT = "D:\vcpkg"
.\tools\test.ps1
```

By default, the wrapper performs a fresh CMake configure so stale compiler
paths in an existing build directory cannot affect the test result. For a fast
incremental local run after the environment is already known-good:

```powershell
.\tools\test.ps1 -ReuseConfigure
```

The equivalent explicit preset workflow is:

```powershell
cmake --fresh --preset vcpkg
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

To run a single test or test suite:

```powershell
.\tools\test.ps1 -TestRegex FriedrichHofsaessWekeckRegression
```

## Current Scope

This repository is currently developed as a minimal working timetable-assignment project.

The explicitly supported runtime contract at this stage is only the `pair-file` input path:

- `--pair-data-dir <path>`
- default auto-discovery of `data/test`

The following constraints are intentional project scope decisions for the current stage:

- only the `pair-file` data source is supported
- deprecated compatibility paths (`--data-dir`, `--data-file`) are not part of the maintained scope
- `params.txt` is used for the supported `SearchParams` subset when present
- built-in runtime rollout configuration is still used
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

Optional:

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
- `params.txt` parsing is connected for parameters already represented by `SearchParams`.
- The deprecated file data source remains outside the current maintained scope.
- Error handling uses `mathfp::Expected` end-to-end.
