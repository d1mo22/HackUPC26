# Solver optimization plan — next session

## Current state

**Production version: Step10 totals (Q sum across 15 cases = 32023.61, all cases ≤ 22s, total 330s).**

| Case | Step10 Q | vs Step5 |
|------|----------|----------|
| Case0 | 1101.26 | −16% (target: 900–1000) |
| Case1 | 1317.44 | −15% |
| Case2 | 2323.61 | −5% |
| Case3 | 2723.03 | +15% (regression — see below) |
| CaseWeird | 2136.43 | −8% |
| Case40 | 2005.43 | −10% |
| CaseAngledA | 2074.97 | −9% |
| CaseAngledB | 2314.92 | −7% |
| CaseAngledC | 2016.63 | −8% |
| CaseAngledD | 2609.88 | −6% |
| CaseDiagArmA | 2123.28 | −7% |
| CaseDiagArmB | 2141.06 | −9% |
| CaseDiagArmC | 2009.20 | −4% |
| CaseDiagArmD | 1723.79 | −22% |
| CaseForcedAngle | 3402.68 | −3% |

Step5 baseline was 35644.61 → Step10 is **−3621 net (−10.2%)** vs Step5.
Original baseline (pre-step1) was 38983.45 → Step10 is **−6960 net (−17.9%)** vs original.

## Current state of solver.cpp

Active changes vs original baseline (everything kept):

1. ✅ Active-ops fix: ops 0/1 re-enabled in `hill()`; ops 6/7 weight-reduced on axis-aligned cases (`build_active_ops()`).
2. ✅ Cached polys + bounds in `PlacedBay`; bb-rejection in `valid_candidate`; `SpatialIndex` in `add_bay`.
3. ✅ Deadline-driven hill loop (22s/case via `CASE_BUDGET_SECONDS`).
4. ✅ Best-of-4 shelf-pack init (H/V × offset/no-offset, picked by lowest initial Q).
5. ✅ Robust CSV parser (`try_stoi/try_stod`).
6. ✅ Tangent edge midpoints in `candidate_points`.
7. ✅ Solution accumulator (`QSums`, `quality_with_added`) used in `upgrade_bay` and `add_shared_gap_bay`.
8. ✅ Mixed-orientation shelf-pack (now confirmed neutral-to-positive after `MAX_POINTS_ADD=80` revert).
9. ✅ **Step 7:** `position_perturb_and_add` operator (op 8). Picks a bay, tries ±100/±300mm shifts on x/y, follows up with `add_shared_gap_bay`. Weight 2 axis-aligned, 1 angled.
10. ✅ **Step 8:** Adaptive operator weights in `hill()`. Per-restart rolling tried/improved counters, weight = `prior * (improved+1)/(tried+4)`. Warmup 150 iters, decay every 300 iters.
11. ✅ **Step 9:** `SpatialIndex` in `upgrade_bay` and `add_shared_gap_bay`. Throughput-neutral on N≤300 cases but cleaner code.
12. ✅ **Step 10:** Simulated annealing variant `hill_sa()`. Linear cooling T0=5%·Q → ~0 over deadline. **Used for 8 of 10 restarts** (2 strict-hill kept as safety net). **Single biggest win this session: −3268 net.**

## What was tried and learned

| Step | Change | Result | Verdict |
|------|--------|--------|---------|
| 1 | Op 0/1 re-enabled, ops 6/7 case-gated | Partial | ✅ Kept |
| 2 | + Cached polys + SpatialIndex in add_bay | 2.07× speedup | ✅ Kept |
| 3 | + Deadline 22s + shelf-pack init | net −3034 vs step1 | ✅ Kept |
| 4 | + multi-strategy + Q-delta in add_bay | mixed | ❌ Reverted |
| 5 | + best-of-4 shelf-pack per restart | net −307 vs step3 | ✅ Kept |
| 6 | + mixed-orient shelf, +tangent points, +MAX_POINTS=120, +accumulator | partial regression | ⚠ Partial revert: kept accumulator + tangent points; MAX_POINTS reverted. Mixed-orient confirmed neutral. |
| 7 | + position_perturb_and_add (op 8). First version (4 attempts × 16 shifts × full add_bay) was net +677. **Lean version (2×8 shifts, only `add_shared_gap_bay`) is net −123.** | ✅ Kept lean variant |
| 8 | + Adaptive op weights with smoothing prior | net −85 vs step 7 | ✅ Kept. Stronger smoothing (prior=30) made it worse — original (prior=4) is right. |
| 9 | + SpatialIndex in upgrade_bay, add_shared_gap_bay | throughput-neutral on Case0 (10k iters) and CaseAngledD (large). Q noise-level. | ✅ Kept (no harm, code is cleaner) |
| 10 | + SA variant `hill_sa()`, 5/5 hill/SA split | net −2599 vs step 9 (huge) | ✅ Kept |
| 10b | Tuning split: 5/5 → 3/7 → 2/8 → 1/9 → 0/10 | 32542 → 32229 → 31895 → 31877 → 31880 | Plateau ~31880 from 1/9. **Chose 2/8 for safety margin.** |

### Key lessons from this session

- **SA was the biggest single win**, by a wide margin. The plan's hypothesis ("operator improvement rates 5–15% → many local minima, SA likely escapes them") is correct. Almost every case is now SA-dominated.
- **Don't over-smooth adaptive sampling.** Smoothing prior=4 keeps responsiveness; prior=30 made adaptive weights too uniform (effectively reverted to static), erasing the gain. Critical: high-leverage low-rate operators (e.g. `remove_k_and_refill`) survive prior=4 because their per-improvement Q delta dominates per-call cost in the hill regression check, not in adaptive weighting itself — the adaptive weights mostly redirect compute away from cheap dead operators (`add_45_degree_bay` on axis-aligned).
- **Operator cost matters more than improvement rate.** Step 7's first iteration of `position_perturb_and_add` was *net negative* despite a non-trivial improvement rate (3–5% on some cases) because each call cost two `add_bay` invocations. The lean version (skip when shared-gap fails) is net positive because it stops wasting iterations on the costly fallback.
- **2-bay/8-SA restart split is a Pareto choice.** Pure 0/10-SA scores ~3 better on average than 2/8 but variance is higher; the two strict-hill restarts cost almost nothing (best-of-10 keeps the winner) and give a safety net for cases where SA's worsening-acceptance pathologically diverges. Worth keeping.

## Remaining items

These were planned but skipped this session because step 10 alone exceeded the cumulative target of all prior steps. They remain reasonable next-session candidates if more Q is wanted.

### Step 11 — Segment-aware shelf packing (Tier B #8)

Detect ceiling height segments (`min_ceiling_between` per x-band) and pre-filter bay types per segment before the shelf walk. Avoids wasted `valid_candidate` calls on tall bays in short segments. Affects shelf-pack speed primarily, slight Q win.

### Step 12 — Multi-bay swap pass (Tier D #13)

Periodically: for each pair `(bay_A, bay_B)` placed adjacent, try replacing with one larger type that covers both footprints. Catches cases where 2× small + gap = 1× large with better Q. Run every 50 hill iterations.

### Step 13 — SA-aware adaptive weights

The current adaptive sampler increments `roll_improved[op]` only when `q < best_q`, but in `hill_sa` worsening-accepted moves are not counted. Consider a separate counter for "useful exploration" — e.g. moves that became part of a chain leading to a new best within K iterations. Lower priority; current weights still work in SA.

### Step 14 — Per-case T0 calibration

T0 is fixed at 5% of starting Q. Some cases may benefit from hotter (cases with deep local minima — CaseForcedAngle still highest at 3403) or colder schedules. Quick win: try 3% / 5% / 8% T0 across restarts as a third restart-level dimension.

### Step 15 — Restart re-seeding from best on plateau

If SA chain has not improved best for ~1000 iters, re-seed `current` from `best` and bump T to ~0.5·T0. Standard SA escape mechanism. Likely helps the few cases where SA wandered into a poor region late.

## Hard cases worth deep dives

- **CaseForcedAngle (3402.68 → still highest Q)**: only −3% from step5. The angled-bay search is still under-exploited. Op 7 (`add_45_degree_bay`) hit rate is healthy under SA but the case shape forces a non-standard layout pattern. Worth a focused angled-shelf init variant.
- **Case0 (1101.26 vs 900–1000 target)**: still ~10% above the reported greedy baseline. Likely needs tighter row packing.
- **Case3 (2723.03)**: only case where best-of-restart scoring lands above step5's value (2371.35). Investigate restart selection or per-case mode/strategy.

## Hard limits / constraints to remember

- Per-case budget: **30s judge limit**. Currently 22s deadline + ~3s overhead. Don't exceed 27s without testing.
- All 10 restarts run via `std::async(launch::async)` — wall time = max(restart times).
- `thread_local rng` is reseeded per worker; results deterministic per (case, r) modulo OS scheduling jitter affecting deadline-bounded loops.
- `is_valid_solution` is the final gate — any new operator that mutates `PlacedBay` must call `refresh_cache` to keep cached polys consistent.
- SA never replaces `best` with a worsening move — `best` is only updated when `q < best_q`. Worsening is only applied to the SA chain (`current`).

## Files

- `solver/solver.cpp` — production solver (now contains `hill_sa` and op 8)
- `solver/solver_ALNS.cpp` — separate ALNS solver (stale, not synced with this session)
- `/tmp/step{N}_out.log` — most recent benchmark logs from this session
- `/tmp/step10_final.log` — final 15-case run (Q sum 32023.61)

## Quick start commands

```bash
# Build
g++ -O3 -std=c++17 -o /tmp/solver solver/solver.cpp

# Run all cases (current case list = 15)
cd solver && /tmp/solver > /tmp/run.log 2>&1

# Compare Q to step10 production (32023.61)
grep -A1 "BEST Case" /tmp/run.log | grep "Q=" | awk -F'Q=' '{s+=$2} END {printf "Q sum = %.2f\n", s}'
grep "total elapsed" /tmp/run.log
```
