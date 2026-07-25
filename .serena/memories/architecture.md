# Architecture

Confirmed:
- `ARCHITECTURE.md` states the desired direction: domain owns pure value types, algorithms, invariants and mathematical policies; infra owns concrete file formats/logging/adapters; app wires effects into the domain pipeline; UI renders/interacts through app/domain-facing state.
- Accepted architecture compromise: domain/preprocessing currently report progress through `timetable::infra::progress`, violating ideal domain -> infra independence. Do not hide this with globals or duplicated wiring. Target repair is domain-owned diagnostic values/interfaces, interpreted by app/infra.
- Accepted build-target compromise: `timetable_core` is intentionally broad while the domain/infra diagnostic dependency remains. Target future split: `timetable_domain`, `timetable_io`, `timetable_infra`, `timetable_ui`, `timetable_app`.
- `architecture/domain-module-decomposition.md` defines the mathematical module rule: mathematical modules must not include effectful/adapter dependencies such as `timetable/infra/*`, `fmt`, filesystem/process/thread/UI libraries. Allowed dependencies include standard pure value containers/algorithms, `mathfp` value/tolerance/summation/Expected support, and domain value types needed to state formulas.
- Current major assignment/search decomposition from docs + Graphify:
  - `src/domain/assignment/split/split.cpp`: first decomposition target; mixed math kernel/orchestration/diagnostics/runtime effects.
  - `src/domain/assignment/output_assembly.cpp`: materialization/orchestration, not first math-refactor target.
  - `src/domain/assignment/capacity.cpp`: capacity exposure/load state/penalties/validation.
  - `src/domain/assignment/search/residual_reachability.cpp`: compatibility facade composing extracted residual modules.
  - `src/domain/assignment/search_cost.cpp`: compatibility facade; formulas/invariants delegated to `search/cost/*`.
  - `src/domain/assignment/day_path.cpp` and extracted `day_path/*`: path identity/support/retention/finalization/alternative boundaries.
- Search runtime symbols confirmed with Serena/clangd:
  - `SearchBranch` (`include/timetable/domain/assignment/search/model/branch.hpp`) is one node in the dynamic multi-path connection tree; its incoming edge is a whole connection segment.
  - `PreprocessedNetwork` (`include/timetable/domain/assignment/search/preprocessed_network.hpp`) aggregates route/connection segments and route/connection indexes.
  - `run_search_batch_tree` (`src/domain/assignment/search/runtime/batch_tree_execution.cpp`) takes `SearchBatchContext`, diagnostics runtime, and OD-day memory limits.
  - `search_od_day_paths_by_origin_branch_and_bound` has public/runtime facades in `include/timetable/domain/assignment/od_day_path_search.hpp`, `include/timetable/domain/assignment/od_day_path_runtime.hpp`, `src/domain/assignment/search/search.cpp`, and `src/domain/assignment/search/runtime/od_day_runner.cpp`.
- Serena references show `SearchBranch` is used across generation, successor, frontier/retention, pruning, projection, runtime enqueue/execution, and search tests. `PreprocessedNetwork` is used by pipeline/output/validation plus search kernel/runtime/generation/projection.
- Graphify report currently finds no import cycles and identifies high-connectivity abstractions including `Unit`, `RouteSegment`, `InputModel`, `SearchBranch`, `ConnectionSegment`, `PreprocessedNetwork`, `ConnectionLeg`, `SearchConnection`, `TaskSearchStats`.

Inferences / assumptions:
- For tasks touching math kernels, first check whether the target is a compatibility facade or an extracted narrow module; prefer extending the narrow module if one exists.
- For app/infra/UI tasks, avoid pulling concrete adapter dependencies deeper into domain; existing progress dependency is known debt, not precedent for new effects.