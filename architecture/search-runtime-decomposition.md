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
   Own effectful runtime orchestration values. These modules remain outside the
   paper-level tree core.

The existing `search_runtime.cpp` remains the only full batch runner during
this transition. New modules must move existing behavior rather than create a
second branch-and-bound implementation.
