# Conventions

Confirmed:
- Namespaces follow directory ownership, e.g. `timetable::domain::assignment`, `timetable::domain::assignment::detail`, `timetable::domain::assignment::runtime`, `timetable::app`, `timetable::infra`, `timetable::io`.
- Error handling uses `mathfp::Expected<T>`/`mathfp::Unit` instead of exceptions on public computational paths. `src/main.cpp::run_app` composes operations with `mathfp::fp::pipe::map` and `and_then`.
- Public domain facades are in `include/timetable/domain/assignment/*.hpp`; implementation-only orchestration lives under `src/domain/assignment/detail/**`. `run_timetable_assignment_pipeline_with_context` is documented as a private bridge used only by `pipeline.cpp`; application code should depend on public `run_timetable_assignment_pipeline` instead.
- Domain result projections are layered: `AssignmentOutput` is canonical/lossless domain result; UI, text and file projections derive from it.
- C++ types are mostly small final structs/classes with explicit domain names and public value fields for data aggregates (`AssignmentInput`, `AssignmentOutput`, `Connection`, `SearchBranch`, `PreprocessedNetwork`, `SearchBatchContext`).
- Mathematical modules should be named after the mathematical object/relation/transformation they own, not only after file size or implementation convenience. See `architecture/domain-module-decomposition.md`.
- Architecture-rule tests are part of the normal GTest target; docs refer to `ArchitectureRules.MathematicalModulesDoNotIncludeEffectfulDependencies` as the registry for current math modules.
- Build spelling is consistently `timetable-transit-assigment` (misspelled “assigment”) in project/target/package names; preserve existing spelling in commands/paths/targets.

Inferences / assumptions:
- Keep new APIs close to existing facade/detail separation: expose only stable domain contracts in `include`, keep orchestration helpers in `src/.../detail` unless already public.
- Prefer returning structured diagnostics/results over adding logging/progress effects to pure domain code.
- When touching search, inspect symbol references with Serena first because `SearchBranch`/`PreprocessedNetwork` cross many modules and tests.