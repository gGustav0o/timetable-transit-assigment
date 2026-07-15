# Architecture notes

This project is developed as a math-first, functional-core timetable assignment
system. The preferred direction is:

- domain code owns pure value types, algorithms, invariants and mathematical
  policies;
- infra code owns concrete file formats, logging sinks and other adapters;
- app code wires effects into the domain pipeline;
- CMake targets should eventually mirror those layers.

The current codebase intentionally accepts a few architecture compromises while
the assignment model is still being stabilized. They are documented here so the
compromises stay explicit, reviewable and removable.

## Intentional architecture compromises

### Domain progress diagnostics currently use infra

Status: accepted temporary compromise.

Domain and preprocessing algorithms currently report progress through
`timetable::infra::progress`. This violates the ideal dependency direction:
domain should not depend on infra.

The compromise is currently accepted because removing it cleanly would require a
broader redesign of diagnostic effects across search, choice, split,
preprocessing and application orchestration. A small local patch would likely
replace the explicit dependency with either hidden global state or duplicated
wiring, which would be less honest and less functional.

The mathematically clean target design is one of:

- pure functions returning structured diagnostics together with the result;
- a writer-style diagnostic monoid accumulated by domain computations;
- an explicit domain-owned diagnostic callback/context passed through the
  execution request.

In all target designs, app/infra interprets diagnostics as logs, status updates
or UI messages. Domain algorithms should not know about concrete logging,
terminal UI, files or other adapters.

Exit criteria:

- domain and preprocessing modules no longer include `timetable/infra/*`;
- diagnostic effects are represented by domain-owned values or interfaces;
- app/infra owns interpretation of those diagnostics.

### `timetable_core` is intentionally still broad

Status: accepted consequence of the current boundary compromise.

The project currently builds most implementation files into one
`timetable_core` target. This is broader than the desired final target graph,
but splitting it now would mostly encode the current dependency violation into
CMake instead of removing it.

A clean split should happen after the domain/infra diagnostic dependency is
removed. Until then, premature target decomposition would likely introduce
artificial seams, cyclic dependencies or adapter targets with unclear ownership.

Target direction:

- `timetable_domain`: pure types, algorithms and mathematical policies;
- `timetable_io`: abstract input/output contracts when they are not pure domain;
- `timetable_infra`: concrete CSV/XLSX/logging/file adapters;
- `timetable_ui`: FTXUI rendering and UI state;
- `timetable_app`: orchestration and effect wiring.

Exit criteria:

- target boundaries match dependency boundaries;
- external adapter dependencies are private to infra/UI/app targets;
- tests can link mostly against the smallest mathematical target they need.

### Deprecated CLI inputs are retained but not maintained

Status: retained compatibility surface, not the active runtime contract.

The maintained runtime input contract is the pair-file path:
`--pair-data-dir <path>`.

The deprecated `--data-dir` and `--data-file` paths remain in the codebase
because they may still be useful for development or future migration work.
However, they are not kept mathematically or operationally equivalent to the
active pair-file path, and they are not the reproducible test workflow.

This is an explicit scope decision rather than an accidental omission.

Exit criteria for promotion:

- a deprecated input path has a clear product/runtime purpose;
- it is covered by deterministic tests;
- its parameter mapping and assignment semantics are specified against the same
  domain model as pair-file input.

Exit criteria for removal:

- no expected development or migration use remains;
- the CLI/help text and infra factories can be simplified without losing a
  known workflow.

## Related design notes

- [Search runtime decomposition](architecture/search-runtime-decomposition.md)
