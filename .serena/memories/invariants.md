# Invariants

Confirmed:
- `SearchTimeDomain` (`include/timetable/domain/assignment/search_time_domain.hpp`) is a canonically normalized union of departure-time windows. Invariants from clangd docs: windows sorted by begin then end; each begin <= end; windows are pairwise disjoint and non-touching after normalization; membership is the union of contained windows. It is mathematical only: no storage policy, no demand-table knowledge, no search execution strategy.
- `Connection` (`include/timetable/domain/assignment/connection.hpp`) is the canonical connection between one origin and one destination zone. It contains the OD-bound time-realized trace; metrics and impedance-like values are derived projections layered on top.
- `SearchBranch` (`include/timetable/domain/assignment/search/model/branch.hpp`) is one node in the dynamic multi-path connection tree; its incoming edge is a whole connection segment: access walk, timed ride, transfer walk, or egress walk. Fields include trace, metrics, OD-day carrier, and retained connection label.
- `PreprocessedNetwork` (`include/timetable/domain/assignment/search/preprocessed_network.hpp`) contains route segments, connection segments, route index, and connection index; references span pipeline, validation, output assembly, search kernel/runtime, generation, OD-day supply graph, and projection.
- `AssignmentInput` (`include/timetable/domain/assignment.hpp`) is the broad input aggregate containing model/input parameters, choice/search/pruning/time-domain/skim/execution/capacity configs and optional presegmented input.
- `AssignmentOutput` (`include/timetable/domain/assignment.hpp`) is documented as the public canonical result of the full timetable assignment pipeline and the source for UI/text/file projections.
- `run_timetable_assignment_pipeline(AssignmentInput)` documented calculated-mode order: preprocessing -> connection search -> connection choice -> demand split.
- `run_timetable_assignment(AssignmentInput)` runs the full assignment pipeline and maps it to `AssignmentOutput`.
- Mathematical module invariant from architecture docs: pure math modules must not include infra, fmt, filesystem/process/thread/UI dependencies; allowed dependencies are pure standard containers/algorithms, mathfp support, and domain value types needed to state formulas.
- Current accepted non-ideal invariant/debt: domain/preprocessing progress reporting uses infra progress; this is documented debt, not a target-state dependency rule.

Inferences / assumptions:
- Treat `AssignmentOutput` as the semantic source of truth for downstream projections; avoid deriving new output semantics independently in infra/UI.
- Treat compatibility facades (`search_cost.hpp/.cpp`, residual reachability facade, broad `split.hpp`) as stability surfaces while extracting or changing narrower mathematical modules behind them.