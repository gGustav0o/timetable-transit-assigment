# Core

Confirmed:
- Project: timetable-based public transit assignment, C++20, CMake/vcpkg, Windows-first workflow. README describes the core algorithm as branch-and-bound timetable assignment following `Timetable-Based_Transit_Assignment_Using_Branch_an.pdf`.
- Source map:
  - `include/timetable/domain/**`: public domain value types, assignment/search/preprocessing contracts.
  - `src/domain/**`: domain algorithms and pipeline implementations.
  - `include/timetable/io/io.hpp`: abstract input contracts; `timetable::io::DataSource::load()` returns `AssignmentInput`.
  - `include/timetable/infra/**`, `src/infra/**`: concrete file/data-source adapters, CSV/XLSX/params/logging/output-file projections.
  - `include/timetable/app/app.hpp`, `src/app/app.cpp`: application orchestration; `timetable::app::run(const AppConfig&, const io::DataSource&)`.
  - `src/main.cpp`: tiny CLI entrypoint; `run_app` does `app::parse_cli -> infra::make_data_source -> app::run` using `mathfp::Expected` pipes.
  - `src/ui/**`, `include/timetable/ui/**`: FTXUI UI/state/commands.
  - `libs/mathfp/**`: vendored/support library used as `mathfp::all`.
  - `tests/domain/**`: GTest domain/regression/architecture-rule tests.
- Primary public flow: `AssignmentInput` (`include/timetable/domain/assignment.hpp`) -> `assignment::run_timetable_assignment()` (`include/timetable/domain/assignment/run.hpp`) -> `AssignmentOutput` (`include/timetable/domain/assignment.hpp`). `AssignmentOutput` is documented by clangd as the public canonical, lossless domain-level result from which UI/text/file projections are derived.
- Pipeline facade: `assignment::run_timetable_assignment_pipeline(AssignmentInput)` (`include/timetable/domain/assignment/pipeline.hpp`) returns a variant-like `AssignmentPipelineResult`; documented mode order for calculated mode is preprocessing -> connection search -> connection choice -> demand split.
- Key memories: architecture boundaries and compromises in `mem:architecture`; build/test commands in `mem:build-and-test`; code conventions in `mem:conventions`; mathematical/domain invariants in `mem:invariants`.

Inferences / assumptions:
- Treat `include/timetable/domain/assignment/*.hpp` public facades as the stable entry points unless a task is explicitly inside `src/domain/assignment/detail/**`.
- Prefer Graphify as the first navigation layer when `graphify-out/graph.json` is present, then verify important C++ claims with Serena/clangd symbols/references.