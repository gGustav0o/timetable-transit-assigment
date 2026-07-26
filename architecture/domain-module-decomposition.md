# Domain Module Decomposition

This document fixes the decomposition boundary for oversized mathematical
domain modules. The goal is not smaller files by itself. The goal is that each
module owns one mathematical object, one relation, or one pure transformation
with explicit invariants.

## Classification

The first audit uses line count only as a signal. A large file is a problem
only when it mixes different architectural roles.

| Module | Size signal | Current roles | Classification | First action |
| --- | ---: | --- | --- | --- |
| `src/domain/assignment/split/split.cpp` | ~2200+ lines | split alternatives, perceived journey time, temporal utility, split impedance, Box-Cox transform, choice weights, probability normalization, independence/commonality, capacity adjustment, OD lookup, conservation certificates, load facade, progress logging | mixed mathematical kernel + orchestration + diagnostics + runtime effects | decompose first |
| `src/domain/assignment/output_assembly.cpp` | ~1300 lines | output materialization, projections, summary assembly | materialization/orchestration, not mathematical kernel | keep out of first math refactor |
| `src/domain/assignment/capacity.cpp` | ~980 lines | capacity exposure, overload/load state, penalties, validation | mathematical kernel + validation + materialization | decompose after split |
| `src/domain/assignment/search/residual_reachability.cpp` | ~840 lines | relaxed-state feasibility, lower-bound/reachability queries | graph algebra + search pruning support | continue residual decomposition |
| `src/domain/assignment/skim.cpp` | ~860 lines | skim matrix aggregation and validation | aggregation algebra + output metric policy | later, after core split/search pieces |
| `src/domain/assignment/output_validation.cpp` | ~830 lines | cross-output invariants and consistency checks | validation suite | keep as validation boundary unless invariants need domain value types |
| `src/domain/preprocessing/route_segments.cpp` | ~780 lines | line segment derivation, walk graph shortest paths, preprocessing logging | preprocessing algorithms + graph algebra + diagnostics | split only after progress dependency is removed |
| `src/domain/assignment/day_path.cpp` | ~780 lines | day-path identity, support retention, projection logic | domain algebra + result construction | second wave |
| `src/domain/assignment/capacity_aware_assignment.cpp` | ~760 lines | fixed-point policies, MSA update, convergence, diagnostics | iterative numerical method + orchestration | follow `capacity.cpp` |
| `src/domain/assignment/search_cost.cpp` | ~520 lines after first extraction | generalized search impedance, capacity-aware search exposure, context factories, validation | cost algebra + validation | continue search-cost decomposition |
| `src/domain/preprocessing/connection_segments.cpp` | ~740 lines | timed/walk connection segment construction, fares, route grouping | preprocessing construction + validation | later |
| `src/domain/assignment/validation/split.cpp` | ~710 lines | split result invariants | validation suite | may shrink after split value types exist |
| `src/domain/assignment/output_loads.cpp` | ~660 lines | passenger/load accumulation projections | load aggregation + output materialization | extract pure load algebra later |
| `src/domain/assignment/search/projection/complete_connection.cpp` | ~650 lines | complete connection materialization and retention | search projection algebra + retention | second wave |
| `src/domain/assignment/search/preprocessed_network.cpp` | ~645 lines | indexes, canonicalization, validation, diagnostics | domain index construction + diagnostics | later, depends on diagnostic boundary |

Classification tags:

- **mathematical kernel**: pure formulas/relations over domain values.
- **validation**: invariant checking and smart-constructor support.
- **orchestration**: ordering of already-defined mathematical steps.
- **diagnostics**: structured runtime facts about a computation.
- **runtime effects**: progress logging, wall clock, files, UI, threads.

## Mathematical Module Rule

A mathematical module must not include effectful or adapter-level dependencies.
This is an architectural rule, not a style preference.

Forbidden from mathematical modules:

- `timetable/infra/*`
- `progress_bus`
- `spdlog`
- `<filesystem>`
- UI libraries or project UI headers
- threading/concurrency headers such as `<thread>`, `<future>`, `<mutex>`,
  `<shared_mutex>`, `<condition_variable>` and `<atomic>`

Mathematical modules may depend on:

- C++ value/container/numeric headers;
- `mathfp` value, tolerance, summation and `Expected` support;
- domain value types needed to state the formula.

The enforced registry of current mathematical modules lives in
`ArchitectureRules.MathematicalModulesDoNotIncludeEffectfulDependencies`. New
split math modules must be added to that registry in the same change that
creates them.

The first decomposition target is `split.cpp`, because it mixes all five tags in
one implementation unit.

## Split Boundary

The split layer implements the connection-tree-level demand distribution:

```text
selected alternatives C(a)
  -> perceived journey time
  -> interval-specific split impedance IMP_a(c)
  -> independence IND(c)
  -> log choice weight log w_a(c)
  -> normalized probabilities
  -> passenger masses
  -> emitted shares / unassigned demand / certificates
```

Only the first six steps are mathematical kernel. Emitting shares, looking up
OD tasks, validating certificates, logging, and capacity-aware fixed-point
iteration are separate responsibilities.

## Target Modules

The target split shape is:

```text
include/timetable/domain/assignment/split/
  alternative.hpp
  impedance.hpp
  independence.hpp
  choice_weight.hpp
  probability.hpp
  demand_projection.hpp
  support_selection.hpp
  conservation.hpp
  capacity_adjustment.hpp
  kernel.hpp

src/domain/assignment/split/
  alternative.cpp
  impedance.cpp
  independence.cpp
  choice_weight.cpp
  probability.cpp
  demand_projection.cpp
  support_selection.cpp
  conservation.cpp
  capacity_adjustment.cpp
  kernel.cpp
  split.cpp
```

`split.cpp` remains the compatibility facade for the current public API in
`include/timetable/domain/assignment/split/split.hpp`. It should eventually
contain validation, lookup, orchestration, and materialization only.

## Mathematical Extraction Units

### `split/impedance`

Owns:

- `perceived_journey_time`
- interval reference time by `DemandSegmentBasis`
- early/late temporal deviation
- temporal utility
- interval-specific split impedance
- Box-Cox/raw impedance transform

Mathematical contract:

```text
IMP_a(c) = q_1 PJT(c) + q_2 U_a(c) + q_3 FARE(c)
```

No result materialization, no demand lookup, no logging.

### `split/choice_weight`

Owns:

- supported choice-model validation
- Kirchhoff/logit/transformed-impedance log weights
- the MNL log-weight expression

Mathematical contract:

```text
log w_a(c) = log IND(c) - beta b^(t)(IMP_a(c))
```

This module should receive already transformed impedance for models that need
it. It must not normalize probabilities.

### `split/independence`

Owns:

- temporal similarity
- perceived-journey-time quality advantage
- fare quality advantage
- asymmetric quality scales
- capped proximity
- influence `f_c(c')`
- independence `IND(c)`
- assignment of independence over an alternative set

Mathematical contract:

```text
IND(c) = 1 / (1 + sum_{c' in C, c' != c} f_c(c'))
```

This module must operate on a light mathematical view of an alternative:
departure, arrival, perceived journey time, and fare. It should not know whether
the source was a timed connection or an OD-day support.

### `split/probability`

Owns:

- max-log-weight stabilization
- weight normalization
- residual probability assignment to the max-weight alternative
- numerical support compaction

Mathematical contract:

```text
p_i = exp(log_w_i - max(log_w)) / sum_j exp(log_w_j - max(log_w))
sum_i p_i = 1
passengers_i = DEM * p_i
sum_i passengers_i = DEM
```

This should be the first extraction because the same algorithm is duplicated in
both timed split and OD-day split paths.

### `split/demand_projection`

Owns:

- pairing probabilities with demand mass
- converting normalized alternatives into `ConnectionDemandShare`
- unassigned-demand creation

It may know public result types. It should not compute log weights or
independence.

### `split/support_selection`

Owns:

- interval-admissible support selection for OD-day alternatives
- candidate/rejected support counts
- timed-support alternative views

It should not compute probabilities.

### `split/conservation`

Owns:

- connection split certificates
- assigned/unassigned conservation checks
- interval probability-sum checks

It is validation-oriented and should remain separate from the pure probability
kernel.

### `split/capacity_adjustment`

Owns:

- capacity context enablement
- capacity exposure selection for timed connection vs day-path support
- capacity-adjusted perceived journey time
- recomputation of independence after adjustment

The endogenous fixed-point loop stays outside this module. The module represents
one exogenous load-state split evaluation.

### `split/kernel`

Owns only the pure composition:

```text
alternatives + demand interval + policies
  -> scored alternatives
  -> normalized split allocation
```

It should return a value object, not append directly to `DemandSplitResult`.

## First PR Slice

The first code slice should extract `split/probability` and introduce focused
tests.

Reason:

- the normalization algorithm is duplicated in `append_split_shares` and
  `split_demand_over_connections_impl`;
- it is mathematically critical;
- it has a narrow input/output contract;
- it does not require changing public API or result types.

Proposed public/internal contract:

```cpp
struct SplitAllocation final {
    std::vector<double> probabilities;
    std::vector<double> passengers;
    std::size_t residual_index{};
    std::size_t suppressed_numerical_shares{};
};

mathfp::Expected<SplitAllocation> normalize_split_log_weights(
    std::span<const double> log_weights,
    double demand_passengers
);
```

Invariants:

- `log_weights` is non-empty;
- each log weight is finite;
- `demand_passengers` is finite and non-negative;
- returned vectors have the same size as `log_weights`;
- probabilities sum to one within tolerance when input is non-empty;
- passengers sum to demand within tolerance;
- suppressed numerical support is conserved by the residual alternative.

After this extraction, both existing call sites should:

1. compute `independences`, `split_impedances`, and `log_weights`;
2. call `normalize_split_log_weights`;
3. materialize shares from the returned allocation.

## Test Plan

Add focused tests before broader refactoring:

- equal log weights produce equal probabilities;
- probabilities sum to one;
- passenger masses sum to demand;
- very small alternatives are suppressed and conserved in residual mass;
- non-finite log weights are rejected;
- zero demand returns zero passenger masses but valid probabilities;
- empty log-weight input is rejected.

The existing Friedrich-Hofsaess-Wekeck regression remains the end-to-end guard
for behavioral equivalence.

## Residual Reachability Boundary

The residual reachability layer implements a timetable-independent suffix
relaxation:

```text
route segments + connection segments
  -> residual reverse graph
  -> relaxed predecessor transitions
  -> destination reachability states
  -> suffix lower bounds
  -> feasibility decision
```

Current extracted modules:

- `search/residual/types`: phase, relaxed state, reachability key/result value
  objects;
- `search/residual/graph`: pure construction of the physical reverse graph
  with minimum run time per physical edge.
- `search/residual/transition`: reverse relaxed predecessor transitions and
  transition costs.
- `search/residual/lower_bound`: Dijkstra over residual transitions for
  journey-time, transfer-count and impedance lower bounds.
- `search/residual/closure`: destination reachability closure over relaxed
  predecessor transitions.
- `search/residual/query`: feasibility decision helpers over a built
  `ResidualReachability` value. It should own transfer-budget, phase and
  unreachable-destination classification without constructing reachability.
- `search/residual/validation`: invariant checks for built residual
  reachability values: destination completion seeds, phase/endpoint consistency,
  transfer-budget monotonicity and finite non-negative suffix lower bounds.

`residual_reachability.cpp` is now the compatibility build facade. It composes
graph-derived closure and suffix lower bounds for each destination, while query
and validation are independently testable mathematical modules.

Next residual extraction:

- none for size pressure. Continue only if a new invariant or formula emerges
  with a sharper mathematical name than the facade composition itself.

## Search Cost Boundary

The search-cost layer implements the generalized-cost scalar used consistently
by branch-and-bound, complete-connection dominance and choice filtering:

```text
connection metric components
  -> base generalized search impedance
  -> optional capacity penalty prefix lookup
  -> capacity exposure
  -> capacity-adjusted search impedance
```

Current extracted modules:

- `search/cost/types`: search-cost value objects and immutable capacity-cost
  index shapes.
- `search/cost/capacity_index`: construction, validation and half-open range
  summation over capacity penalty prefix indexes.
- `search/cost/exposure`: ride-leg and connection capacity exposure over an
  immutable capacity penalty prefix index.
- `search/cost/impedance`: base generalized-cost formula, search impedance
  weight/component validation and capacity-adjusted scalar composition.
- `search/cost/validation`: mode, context, capacity-cost config and component
  invariants that bind the search-cost value objects together.

`search_cost.hpp` remains the compatibility facade for existing call sites.
`search_cost.cpp` now owns only context/component factories and delegates all
invariants and formulas to narrow `search/cost/*` modules. New search-cost math
should include the narrow header that owns the needed object.

Next search-cost extraction:

- no immediate search-cost extraction for size pressure. Continue only if the
  factory facade accumulates a new policy or materialization concern.

## Day Path Boundary

The OD-day path layer keeps structural path identity separate from timed
support:

```text
timed connection trace
  -> production day-path leg projection
  -> structural DayPathSignature
  -> retained timed support descriptors
  -> representative metrics and bounded retention
```

Current extracted modules:

- `day_path/types`: DayPath retention/config value objects and hashable
  structural containers.
- `day_path/signature`: clock-free production path identity, prefix extension
  and `SearchConnection -> DayPathSignature` projection.
- `day_path/support`: timed ride-support descriptor construction, support
  descriptor accessors and support-set dominance over complete-connection
  metric sets.
- `day_path/retention`: representative ordering, timed-support saturation,
  support-dominated path removal and bounded retention policy.
- `day_path/finalization`: deterministic materialization of retained
  alternatives/representatives, representative metric summaries and final
  choice-tolerance filtering.
- `day_path/alternative`: validation of `DayPathAlternative` structural shape
  and pure construction from a complete connection plus already computed
  representative metrics.

`day_path.hpp` remains the compatibility facade for existing call sites. New
day-path math should include the narrow `day_path/*` header that owns the
needed object.

Next day-path extraction:

- none for formula pressure. `day_path.cpp` is now a facade/orchestrator for
  search-cost evaluation, retention insertion and public compatibility wrappers.
  Continue only if retention insertion grows enough to deserve a named
  `day_path/facade` or `day_path/insertion` adapter.

## Exit Criteria

The split decomposition is complete when:

- `split.cpp` is a facade/orchestrator and contains no core formulas;
- formulas live in modules named after their mathematical object;
- pure modules do not include `timetable/infra/*`, `fmt`, UI, filesystem,
  threads, or progress logging;
- duplicated normalization code is removed;
- each extracted module has focused tests;
- existing article regression output is unchanged.
