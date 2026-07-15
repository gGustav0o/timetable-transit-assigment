# Search Runtime Decomposition

This document fixes the architectural boundary used while decomposing the
branch-and-bound search implementation.

## Boundary

The paper-level search core is a domain module. It may depend on:

- preprocessed connection-segment supply
- branch transition and successor generation
- `C_y` retention and pruning policies
- projection-facing value types
- `mathfp::Expected` for explicit error flow

It must not depend on:

- progress logging or formatted runtime status
- wall-clock heartbeats
- thread, future, or parallel batch orchestration
- UI or application lifecycle state
- cancellation tokens
- batch index/count reporting

Runtime modules own effects. They assemble batches, run pure tree steps, handle
parallelism/cancellation, and translate diagnostics into logs.

## Target Shape

The decomposition should move code in this direction:

```text
search/runtime/*
  -> search/tree/*
  -> search/frontier/*
  -> search/projection/*
  -> search/generation/*
  -> search/model/*

search/tree/*
  -> search/frontier/paper_connection_retention
  -> search/generation/*
  -> search/generation/branch_transition
  -> search/projection value types
```

No `search/tree/*` or `search/frontier/*` module may call progress logging or
runtime orchestration APIs.

## First Extraction Units

1. `search/frontier/paper_connection_retention`
   Owns the algebra of retaining a candidate prefix in paper `C_y`.

2. `search/tree/paper_successor_step`
   Owns the pure successor acceptance decision before projection/frontier
   insertion.

3. `search/tree/level_expansion`
   Owns the two-layer transfer-depth frontier, branch phase counts, and the
   mathematical rule that decides whether an accepted successor stays in the
   current level or advances to the next one.

4. `search/projection/sink`
   Owns the value-level projection boundary for a batch: slot family kind,
   destination indexing, and initial projection retention allocation. It does
   not project branches and does not log.

5. `search/tree/tree_runner`
   Owns the generic tree traversal loop. It has callback hooks for runtime
   effects, but no direct cancellation, logging, threading, or batch reporting.

6. `search/runtime/cancellation` and `search/runtime/parallel`
   Own effectful runtime orchestration values. `parallel` owns worker count,
   batch claiming, async worker execution, fast-fail cancellation, and
   completed/cancelled accounting. These modules remain outside the paper-level
   tree core.

7. `search/runtime/batch_context`
   Owns the explicit non-owning boundary between a batch runner, immutable
   search inputs, mutable batch state, and runtime-only effects. It is a
   context value, not a replacement algorithm.

8. `search/runtime/batch_state`
   Owns batch-local mutable storage and safe construction of
   `SearchBatchContext`: projection sinks, retentions, branch arena, frontier
   state, reachability caches, OD-day destination membership, and per-slot
   diagnostics. It is the lifetime owner behind the context view.

9. `search/runtime/batch_diagnostics`
   Owns runtime-only diagnostics effects for one batch: progress status,
   heartbeat logs, storage diagnostics, OD-day memory-limit reporting,
   projection detail logs, cancellation logs, and batch summary logs.

10. `search/runtime/reachability_masks`
   Owns cached residual reachability masks and rejection accounting values used
   at the projection boundary.

11. `search/runtime/accepted_successor_application`
   Owns application of a successor already accepted by the paper tree step:
   OD-day carrier projection, phase validation, transfer-limit rejection, and
   composition of the projection/filter/enqueue substeps. It does not generate
   successors and does not decide paper `C_y` acceptance.

12. `search/runtime/projection_application`
    Owns applying a candidate branch to projection sinks: complete target
    detection, complete connection retention, and zone-sink rejection. It does
    not enqueue branches or compute continuation masks.

13. `search/runtime/continuation_filter`
    Owns the continuation feasibility filter after projection application:
    residual reachability masks, suffix lower-bound pruning, tree-global and
    projection-slot-local partial retention, and active-mask propagation.

14. `search/runtime/branch_enqueue`
    Owns branch arena insertion, projection-state storage, accepted-branch
    statistics, and level-frontier placement. It does not decide whether a
    branch is mathematically admissible.

15. `search/runtime/od_day_frontier_synchronization`
    Owns synchronization of OD-day frontier queues with paper `C_y` label
    liveness: stale branch release and frontier compaction. Runtime logging
    remains in the batch runner.

16. `search/runtime/root_initialization`
    Owns the root branch value, initial root projection masks, root
    reachability rejection accounting, and frontier seeding.

17. `search/runtime/result_finalization`
    Owns the post-tree projection from retained alternatives to
    `SearchSlotResult` values, tolerance finalization, result validation, and
    finalization-time diagnostics accounting. It does not log and does not run
    the tree.

18. `search/runtime/batch_tree_execution`
    Owns the batch-specific callback layer around `search/tree/tree_runner`:
    cancellation checkpoints, runtime heartbeats, OD-day frontier synchronization,
    successor generation dispatch, paper successor acceptance, and accepted
    successor application. It is runtime glue, not a second tree traversal loop.

19. `search/runtime/batch_planning`
    Owns public-runner search setup lowered to runtime values: origin-period
    tree jobs, interval-local batches, projection-contract validation, expected
    tree counts, batch execution diagnostics, and optional time-domain summary.
    It does not run batches and does not log.

20. `search/runtime/batch_runner`
    Owns the shared internal batch execution scenario:
    batch validation, state preparation, root initialization, tree execution,
    post-tree C_y cleanup, result finalization, and batch-level invariant
    checks. It is not a public runner.

21. `search/runtime/result_materialization`
    Owns public-runner support values after batches have been executed:
    common formatting helpers, result connection counting, demand-task
    materialization, OD-day origin materialization, and count-only all-zone
    sink state.

22. `search/runtime/demand_runner.cpp`,
    `search/runtime/all_zone_runner.cpp`, and `search/runtime/od_day_runner.cpp`
    own the three public runtime entry scenarios. They prepare public execution
    contracts, batching, parallel orchestration, and result materialization, but
    reuse the same internal batch runner.

`search/runtime/search_runtime.hpp` remains only the API-facing declaration
for public search entry points. There is no corresponding monolithic
`search_runtime.cpp` implementation.

`SearchBatchContext` is a narrowed runtime view over already prepared state.
It must not carry setup-only inputs such as the residual graph or day-level
supply switch once `search/runtime/batch_state` has derived reachability,
projection indices, and OD-day supply mode.

The legacy OD-day label-state retention path has been removed from the
production model. OD-day production retains the paper C_y carrier through
`ConnectionSetCy` and frontier labels; stale label storage is not representable
in `TreePartialRetention`.

New modules must move existing behavior rather than create a second
branch-and-bound implementation.
