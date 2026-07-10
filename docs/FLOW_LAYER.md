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

**Honest performance framing.** This is Dijkstra-fast, not CH-fast — the CH's build-once advantage
doesn't survive per-iteration reweighting. But Gravel's edge stands: most traffic-assignment tools are
slow precisely *because* their shortest-path core is slow, and Gravel's structure-of-arrays Dijkstra is
already best-in-class. Cost per MSA iteration ≈ `(|origins| + |destinations|)` one-to-many SSSP + an
O(E) rebuild; a few dozen iterations to convergence. County-scale is comfortable; CONUS would want the
zone aggregation standard in the field (assign at the traffic-analysis-zone level, not per node).

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

Before any scenario number is trusted, reproduce a **known** equilibrium: the layer must recover the
published User-Equilibrium solution on a standard benchmark (Sioux Falls, or another TNTP network with a
distributed reference solution) to within the usual gap tolerance. Only once the solver is shown correct
on a network with a known answer do ΔTSTT scenario results mean anything — the same discipline the 2.9
study applied to the cascade. Correctness is a solved-benchmark match, not a plausible-looking map.

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
The MSA inner loop rebuilds the graph from COO with BPR-updated weights and runs one-to-many Dijkstra,
because the CH cannot be re-queried under changed weights without a rebuild (`PRD.md` static-topology
CH Non-Goal). The one additive primitive required is a Python binding for the existing C++ one-to-many
`dijkstra()`. CCH would change this calculus, but it is unbuilt and out of scope here.

### DD-F3: Consumer layer, not core (outside DD-6)
Lives in `gravel.flow` behind a `[sue]` extra, importing Gravel one-directionally. It does not modify
the core, the DAG, or DD-6; the core stays a demand-agnostic topological engine. This is the whole point
— flow modeling is defensible *as a superstructure* precisely because the core boundary is kept.

### DD-F4: Gravity/synthetic demand first, LODES later
Ship with a gravity-model demand adapter (off `node_weights`) and a synthetic generator for tests and
tutorials; treat LODES/LEHD commute flows as the real-data upgrade via a future `gravel.datasets`
loader. Demand quality bounds result quality, so make the demand source explicit and swappable rather
than hard-coding one assumption.

---

## Roadmap

### Phase F1 — Road SUE (core solver)
`flow.assign(graph, capacity, demand, config)`; BPR + Dial logit loading + MSA; the one-to-many
`dijkstra()` binding; gravity/synthetic demand adapters. **Exit criterion:** reproduces the Sioux Falls
UE benchmark within tolerance.

### Phase F2 — Scenario fragility (the payoff)
`flow.flow_fragility(..., scenario_edges)` → ΔTSTT and ΔTSTT-fraction under edge failure with demand
re-equilibration; compose with the existing scenario / hazard-footprint machinery so a flood or a
failure set maps to a region-wide delay cost.

### Phase F3 — Transit (multi-modal, genuinely novel)
The same logit machinery on the GTFS transit graph, with **GTFS-Realtime** closures reweighting live: a
segment drops, riders redistribute probabilistically to alternate transit paths. Same solver, different
substrate. Wiring GTFS-RT into an equilibrium model is something almost nobody does; it is the natural
third act, once road SUE is trustworthy.

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
- **θ calibration** — literature ranges exist, but the right dispersion for a *fragility* readout (vs. a
  planning forecast) may differ; sensitivity-sweep it, as with cascade α.
- **Zone aggregation for CONUS** — node-level assignment is county-scale; national runs likely need
  traffic-analysis-zone aggregation. Decide the zone scheme before promising national ΔTSTT.
- **Demand realism vs. availability** — LODES is commute-only and residence-to-work; non-commute trips
  are unmodeled. Document what the demand does and does not represent, the way capacity provenance is
  already tracked.
