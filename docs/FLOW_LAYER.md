# Gravel Flow Layer — Design Spec (Stochastic User Equilibrium)

*Design document. Status: **proposed, not implemented.** Companion to [`PRD.md`](PRD.md); adopts its
house style (DD-N decision records + phased roadmap). Revision: 2026-07-10, drafted alongside the 2.10
cascade cleanup.*

---

## Purpose, and where this sits

The topological cascade (`cascade_fragility`, Phase 2A) answers *structural* what-ifs: remove an edge,
recompute betweenness, see what overloads. The 2.9 validation showed why it can't be more than that —
betweenness is not a physical flow, and a threshold-trip rule is the wrong ontology for roads, which
**slow down** under load rather than binary-trip.

This layer answers the question the cascade cannot: **what actually happens to travel when part of the
network fails and demand re-routes around the damage?** Concretely — congestion as delay, not blockage;
some travelers choosing a slightly longer path to avoid a jam; the true region-wide cost of losing a
bridge. The established model for exactly this is **Stochastic User Equilibrium (SUE)** with BPR link
costs (Sheffi, *Urban Transportation Networks*, 1985).

**This layer is a consumer, never part of the core.** It imports Gravel as a routing / capacity /
sampling engine and adds demand + equilibrium on top. It changes nothing in the seven-library DAG and
does not relax [`PRD.md`](PRD.md)'s DD-6 ("topology, not flow") or its "not a traffic-assignment engine"
Non-Goal — those govern *the core*. A superstructure that imports Gravel is free to model flow precisely
*because* it keeps that boundary intact: the defensible topological core stays defensible.

---

## What it computes

Given a network, per-edge capacity, and an origin-destination (O-D) demand matrix, solve for the flow
pattern travelers settle into when each chooses a route probabilistically by cost, and cost rises with
congestion.

**BPR volume-delay (congestion is slow-down, not blockage).** Each edge's travel time rises smoothly
with its volume/capacity ratio:

```
t_a(x_a) = t0_a · ( 1 + α · (x_a / c_a)^β )          α = 0.15, β = 4  (BPR defaults)
```

`t0_a` is free-flow time (the edge weight — already travel time for OSM builds), `c_a` its capacity
(the 2.7 overlay or `estimate_capacity`), `x_a` the assigned flow. A congested edge never leaves the
graph; its weight just climbs. This is Robert's "it moves slowly," made formula.

**Logit route choice (some take a longer path to dodge congestion).** Between an O-D pair, flow spreads
over reasonable routes by a logit rule rather than dumping entirely on the single cheapest one:

```
P(route k) ∝ exp( −θ · cost_k )
```

θ is the dispersion: θ → ∞ recovers deterministic all-or-nothing loading (the "pure next-route cascade"
we explicitly rejected — everyone piles onto the current shortest path); finite θ is the realistic
spread. Loading is done with **Dial's STOCH** algorithm over efficient links, which needs only
shortest-path *distances* from each origin and to each destination, not path enumeration.

**Method of Successive Averages (the solver).** Iterate to the flow fixed point:

```
1. compute link costs  t_a(x_a^n)  from current flows           (BPR)
2. stochastic network loading → auxiliary flows  y^n            (Dial, at costs t^n)
3. average:  x^(n+1) = x^n + (1/n)·(y^n − x^n)                  (MSA step)
4. stop when the relative gap < gap_tol, or max_iterations
```

MSA is provably convergent for SUE and needs no line search (unlike Frank–Wolfe for deterministic UE,
which is the θ→∞ cousin and remains available as an option).

---

## Why SUE, and not the alternatives

- **Not the topological cascade** — it models the wrong physics for roads (threshold-trip, no demand).
- **Not agent-based modeling** — ABM's realism is bought with parameters you can't observe (per-agent
  departure times, value-of-time heterogeneity, learning rates, information penetration). SUE has ~3
  parameters (α, β, θ), all with literature-standard values, and converges to a fixed point you can
  characterize, bound, and validate. ABM earns its keep only when *behavioral heterogeneity itself* is
  the research question; for "how much worse does scenario X make this network," SUE is the more
  falsifiable, and therefore more honest, model. See DD-F1 for the dynamics it deliberately omits.

---

## Architecture

A pure-Python **`gravel.flow`** submodule, shipped in the gravel wheel but gated behind a `[sue]` pip
extra, mirroring the existing `viz` / `interop` / `hazards` consumer-layer precedent: eager
`from . import _gravel`, `numpy` at top, any heavier dependency lazy-imported through a local
`_require(module, extra="sue")`. Gravel never depends on `gravel.flow`; the dependency points only one
way. (If a cleaner separation is ever wanted, the same module lifts out to a standalone `gravel-flow`
package unchanged — nothing here couples it to the core beyond the public Python API.)

### Computational core — the load-bearing reality

**Gravel's CH is baked at build time and cannot be re-queried under changed weights** (`PRD.md`
"static-topology CH" Non-Goal; CCH is an unbuilt research-track item). Since BPR changes every edge's
weight on every MSA iteration, the inner loop is **not** CH re-query. It is:

```
each MSA iteration:
  rebuild an ArrayGraph from COO with BPR-updated weights   (Graph.from_coo, O(E) counting sort)
  one-to-many Dijkstra from each origin        → distances over all nodes
  one-to-many Dijkstra to each destination     → distances on the reverse graph
  Dial STOCH loading using those distances      → auxiliary flows y^n
```

The primitive is **one-to-many single-source shortest paths returning `{distances, predecessors}`**. It
already exists in C++ (`include/gravel/core/dijkstra.h:15`, `dijkstra(graph, source)`) but is **not
bound to Python** — only single-pair `dijkstra_pair` is. So the one piece of real C++ this otherwise
pure-Python layer needs is a **binding for the existing one-to-many `dijkstra()`** (distances required
for Dial; predecessors enable all-or-nothing UE and path recovery). That is a purely additive binding
in `gravel-core`; it violates nothing and touches no DAG boundary.

**Honest performance framing — Dijkstra-fast is the right target, not a shortfall.** It is tempting to
frame this as "we'd use CH if only the weights held still." That is wrong twice over, and worth being
precise about:

1. **CH's speedup is a point-to-point pruning trick.** A CH query is fast because bidirectional search
   up the hierarchy visits a tiny corridor of the graph and prunes everything else. SUE loading is not
   point-to-point — it assigns flow *across the whole network* (one-to-many / one-to-all from each
   origin). When you need distances to many or all nodes, there is nothing to prune: you visit
   essentially every node regardless, and Dijkstra visits each exactly once, which is optimal. So even
   with weights frozen, CH's query advantage would largely evaporate here. Dijkstra is the *right* tool
   for the many-targets regime, not a fallback from CH.
2. **Changing weights would need CCH anyway.** Re-querying CH under BPR-updated weights requires either a
   full rebuild (defeats the point) or Customizable CH — which stores extra metric-independent structure
   (the "redundant CH data") and re-customizes each iteration. Stacked on point (1), that is a lot of
   machinery to accelerate a regime where the query wasn't the bottleneck.

The one thing that genuinely beats plain Dijkstra for one-to-all is **PHAST** (Delling et al.): sweep
the CH DAG in rank order with good memory locality and SIMD/parallelism — a **constant-factor** win
(~an order of magnitude), not asymptotic, and it needs the CH/CCH structure built and re-customized per
reweight. Noted as a possible future constant-factor optimization; almost certainly not worth the
redundant-structure cost for a first cut.

So the realistic ceiling is **well-implemented Dijkstra, parallel over origins** — which is exactly
Gravel's strength: most traffic-assignment tools are slow because their shortest-path core is slow, and
Gravel's structure-of-arrays Dijkstra is already best-in-class. Cost per MSA iteration ≈
`(|origins| + |destinations|)` one-to-many SSSP + an O(E) rebuild; a few dozen iterations to
convergence. County-scale is comfortable; CONUS wants the zone aggregation standard in the field
(assign at the traffic-analysis-zone level, not per node) — which also shrinks `|origins|`.

---

## Inputs

| Input | Source in Gravel | Notes |
|---|---|---|
| Graph | any `Graph` | roads (OSM) or a 2.7 `NetworkGraph` substrate |
| Free-flow time `t0` | the edge weight | already travel time for OSM `SpeedProfile` builds; else length/speed |
| Capacity `c` | `NetworkGraph.capacity` (2.7) or `estimate_capacity` (roads, HCM PCE) | CSR-aligned to `to_coo()` |
| **O-D demand matrix** | **not in Gravel** | the one input the core lacks — see DD-F4 |

The O-D demand matrix (trips per origin-destination pair) is the genuine external dependency. Gravel
provides the *structure* to build one — `stratified_sample` yields distance-stratified O-D pairs, and
`SamplingConfig.node_weights` carries per-node population/importance mass — but not trip volumes. The
layer supplies its own demand adapters (DD-F4): a gravity model off node weights and a synthetic/uniform
generator first, with LODES/LEHD commute flows as the real-data upgrade (a future `gravel.datasets`
loader).

---

## API sketch (illustrative, not final)

```python
from gravel import flow            # requires: pip install gravel-fragility[sue]

cfg = flow.FlowConfig(alpha=0.15, beta=4.0, theta=1.0,
                      max_iterations=60, gap_tol=1e-4)

# solve the equilibrium
result = flow.assign(graph, capacity, demand, cfg)
result.edge_flows        # per-edge x_a   (CSR order, aligned to to_coo())
result.edge_times        # per-edge t_a(x_a)
result.total_travel_time # Σ x_a · t_a  (TSTT)
result.gap, result.iterations

# the fragility payoff: what a scenario failure costs, after demand re-equilibrates
impact = flow.flow_fragility(graph, capacity, demand,
                             scenario_edges=[(u, v), ...], config=cfg)
impact.delta_tstt        # increase in total system travel time vs. the intact equilibrium
impact.delta_tstt_frac   # as a fraction of intact TSTT — "the region got X% slower"
```

**The payoff metric is ΔTSTT** — the increase in total system travel time when the scenario edges are
removed and the whole demand re-equilibrates around the damage. That is the "actual impact for various
scenarios" this layer exists to produce: not "is it disconnected" (topology) but "how much more travel
time does losing this cost everyone." It composes with Gravel's existing scenario and hazard machinery
(a flood footprint → failed edges → ΔTSTT).

---

## Validation plan

Two tiers, in order. Tier 2 is what makes 3.0.0 a *validated* release rather than a correct-solver one,
and it mirrors the 2.9 cascade discipline exactly: don't assume the model, measure it against ground
truth.

**Tier 1 — solver correctness (does the math converge to the right answer?).** Reproduce a *known*
equilibrium: recover the published User-Equilibrium solution on a standard benchmark (Sioux Falls, or
another TNTP network with a distributed reference solution) to within the usual gap tolerance. Only once
the solver is correct on a network with a known answer do any downstream numbers mean anything.
Correctness is a solved-benchmark match, not a plausible-looking map.

**Tier 2 — behavioral validation from realtime diversion (does reality reroute the way the model
says?).** The solver can be perfectly correct and still mispredict real behavior if θ — how sharply
travelers prefer the shortest path — is wrong. So *measure* it. When a real road closes, traffic
redistributes to alternates at some observed **diversion rate**; the model predicts a diversion rate for
that same closure. Fit θ to reproduce observed diversions on a training set of closures, hold out others
to test it, and report the calibrated θ with an **out-of-sample error band**. This is a
revealed-preference / discrete-choice estimation (Ben-Akiva & Lerman) — a well-established method —
applied to real closure events.

The tractable first cut uses **planned closures as natural experiments** (a construction closure with a
known start/end is far cleaner than a random incident) on **instrumented corridors**:

- **Closure event** (the perturbation): 511 / state-DOT event feeds (open), or Waze for Cities /
  Connected Citizens (agency access, anonymized) — when and where a road went down.
- **Observed diversion** (the response): **Caltrans PeMS** loop-detector *volumes* (open; CA) are the
  gold standard — you can literally measure how much traffic left the closed link and where it went.
  **NPMRDS** probe *speeds* (FHWA, agency access; national) are the speed-based fallback where volumes
  aren't instrumented.
- **Transit** (harder): GTFS-Realtime gives the closure event, but measuring rider redistribution needs
  automated-passenger-count data, rarely public — so transit diversion is a stretch goal, not the
  Tier-2 gate.

Data-source stance (per project values): use **open / public-agency** sources — PeMS, NPMRDS, 511, Waze
for Cities — and avoid commercial extractive traffic APIs (Google / TomTom / HERE) whose licenses forbid
the derived-data publication research needs. The measurement is only as reproducible as its inputs are
open.

**Graduation gate (the 2.9 pattern).** 3.0.0 graduates the flow layer to *supported* only if Tier 1
passes and Tier 2 validates within a disclosed error band. If diversion does not validate, 3.0.0 still
ships — the solver, the harness, and an honest verdict naming the gap — exactly as 2.9 shipped the
cascade study and a non-graduation. A validated model and a well-characterized failure are both real
results; a plausible-looking map is neither.

---

## Key Design Decisions

### DD-F1: Static SUE, not dynamic
Model the equilibrium flow pattern, not its time evolution. This deliberately omits queue **spillback**
and **capacity drop** (throughput falling ~5–10% once a jam forms) — genuinely dynamic effects that
need a Cell Transmission Model (Daganzo, 1994), a much heavier commitment. For scenario-level fragility
("this bridge is out, how much slower is the region"), the equilibrium answer is the right altitude;
dynamics are a possible later tier, not the foundation. Stated so nobody mistakes static SUE for a
shockwave simulator.

### DD-F2: Rebuild + Dijkstra, not CH re-query
The MSA inner loop rebuilds the graph from COO with BPR-updated weights and runs one-to-many Dijkstra.
Two reasons, not one: the CH can't be re-queried under changed weights without a rebuild (`PRD.md`
static-topology CH Non-Goal), *and* — more fundamentally — CH's speedup is point-to-point pruning that
does not help the one-to-many/one-to-all loading SUE actually does, where Dijkstra (each node visited
once) is already optimal. So Dijkstra-fast is the target, not a compromise. The one additive primitive
required is a Python binding for the existing C++ one-to-many `dijkstra()`. CCH + PHAST could add a
constant-factor one-to-all speedup at the cost of redundant structure re-customized per reweight; out of
scope for a first cut (see the performance discussion above).

### DD-F3: Consumer layer, not core (outside DD-6)
Lives in `gravel.flow` behind a `[sue]` extra, importing Gravel one-directionally. It does not modify
the core, the DAG, or DD-6; the core stays a demand-agnostic topological engine. This is the whole point
— flow modeling is defensible *as a superstructure* precisely because the core boundary is kept.

### DD-F4: Gravity/synthetic demand first, LODES later
Ship with a gravity-model demand adapter (off `node_weights`) and a synthetic generator for tests and
tutorials; treat LODES/LEHD commute flows as the real-data upgrade via a future `gravel.datasets`
loader. Demand quality bounds result quality, so make the demand source explicit and swappable rather
than hard-coding one assumption.

### DD-F5: θ is measured from realtime diversion, not assumed
θ (logit dispersion) is the one parameter that encodes *behavior* — how much traffic spreads vs.
concentrates on the shortest path — and literature values are a starting point, not an answer for a
fragility readout. 3.0.0 calibrates θ against observed diversion rates from real road closures (Tier 2
of the validation plan) rather than hard-coding it: ship a literature default, expose θ, and report a
data-calibrated θ with its error band. This is what earns the flow layer the right to be called
validated, and it is the part of 3.0.0 that must be built as experiment-and-measurement, not just code.

---

## Roadmap

### Phase F1 — Road UE (core solver) · ✅ implemented, Tier-1 validated
`gravel.flow.assign(graph, capacity, demand, config)` — **deterministic** User Equilibrium via
Frank-Wolfe + BPR, using the one-to-many `dijkstra()` for all-or-nothing loading (`gravel/flow.py`).
Ships `load_tntp` for standard benchmark networks. **Exit criterion met:** reproduces the published
**Sioux Falls** UE solution (MAPE ≈ 0.04%, correlation ≈ 1.0000). The stochastic (logit) generalization
— `theta` finite, some travelers taking a longer path — is deferred to the F3 calibration phase
(`theta=None` selects the deterministic limit today; a `NotImplementedError` guards the rest).
*Known limit:* the node-pair loading assumes no parallel edges between the same node pair; benchmark and
simplified networks satisfy this, dual-carriageway OSM needs edge-level path recovery (a later refinement).

### Phase F2 — Scenario fragility (the payoff) · ✅ implemented
`flow.flow_fragility(graph, capacity, demand, scenario_edges, config)` → ΔTSTT and ΔTSTT-fraction under
edge failure with demand re-equilibration, plus **stranded demand** (trips whose O-D pair the closure
severs). The two are read together: a severing closure shows up as stranded demand, not only as added
delay — and because unservable trips leave TSTT, ΔTSTT alone would understate it. Next: compose with the
existing scenario / hazard-footprint machinery so a flood footprint maps straight to a region-wide delay
cost.

### Phase F3 — Realtime diversion calibration & validation (the phase that validates 3.0.0)
Build the measurement, not just the model. Ingest closure events (511 / Waze for Cities) and observed
volumes/speeds (PeMS / NPMRDS) via new `gravel.datasets` adapters; for each closure, measure the
observed diversion rate; run `flow_fragility` for the same closure to get the predicted diversion; fit θ
to minimize predicted-vs-observed error on a training set and report out-of-sample error on a held-out
set (a `flow.calibrate_theta(closures, observations)` harness). **Exit criterion / graduation gate:**
diversion validates within a disclosed band (→ 3.0.0 *supported*), or the gap is characterized and
documented (→ 3.0.0 ships *experimental*, 2.9-style). This is the phase that makes 3.0.0 a validated
release rather than a correct-solver one.

### Phase F4 — Transit (multi-modal, genuinely novel)
The same logit machinery on the GTFS transit graph, with **GTFS-Realtime** closures reweighting live: a
segment drops, riders redistribute probabilistically to alternate transit paths. Same solver, different
substrate. Wiring GTFS-RT into an equilibrium model is something almost nobody does; it is the natural
act once road SUE is trustworthy. Note: measuring transit *diversion* for Tier-2-style validation needs
automated-passenger-count ridership, rarely public — so transit calibration is a stretch goal, and the
GTFS-RT closure feed is used for live re-assignment even before diversion can be measured.

---

## Non-goals for this layer
- **Not dynamic** — no shockwaves, spillback, or capacity drop (that is CTM; see DD-F1).
- **Not ABM** — no per-agent simulation; the parameter cost isn't worth it for fragility questions.
- **Not a trip planner** — like the core, this models network *behavior under load*, not turn-by-turn or
  door-to-door routing (that is OSRM / OpenTripPlanner territory; see `PRD.md` Non-Goals).
- **Does not change the core** — if anything here seems to require editing `gravel-core`/`-ch`/etc.
  beyond the single additive `dijkstra()` binding, that is a signal the boundary is being crossed and
  should be reconsidered.

## Open questions
- **θ from realtime, and its confounders** — DD-F5 calibrates θ from observed diversion, but real
  closures carry confounders (time-of-day, weather, day-of-week, total demand also shifting). Planned
  closures as natural experiments and matched before/after windows reduce this; residual confounding
  must be reported, not hidden. A wrong-but-precise θ is worse than an honest error band.
- **PeMS is CA-only** — the cleanest volume data is California's. A national Tier-2 needs NPMRDS
  speed-based inference (weaker signal) or more instrumented corridors. Decide how far to generalize
  before claiming a *national* diversion validation vs. a CA-corridor one.
- **Zone aggregation for CONUS** — node-level assignment is county-scale; national runs likely need
  traffic-analysis-zone aggregation. Decide the zone scheme before promising national ΔTSTT.
- **Demand realism vs. availability** — LODES is commute-only and residence-to-work; non-commute trips
  are unmodeled. Document what the demand does and does not represent, the way capacity provenance is
  already tracked.
