# Solver optimization plan — next session

## Current state

**Production version: Q sum across 15 cases = 30683–30816 (3-run range, mean ~30740). Per-case 25s deadline, total ~375s wall.**

| Case | Latest Q |
|------|----------|
| CaseWeird | 2163.16 |
| Case1 | 1303.05 |
| Case2 | 2392.41 |
| Case3 | 2154.59 |
| Case0 | 1093.35 |
| Case40 | 2041.53 |
| CaseAngledA | 1958.45 |
| CaseAngledB | 2111.10 |
| CaseAngledC | 1929.66 |
| CaseAngledD | 2684.17 |
| CaseDiagArmA | 2045.56 |
| CaseDiagArmB | 1988.65 |
| CaseDiagArmC | 2037.38 |
| CaseDiagArmD | 1841.12 |
| CaseForcedAngle | 3071.50 |

**Cumulative improvement vs Step10 baseline (32023.61): −1208 (−3.8%).**

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

### Step 11 — Segment-aware shelf packing (Tier B #8) — DONE

**Implemented as ceiling pre-check hoist in `valid_candidate`.** Moved the `min_ceiling_between` check from the end of `valid_candidate` to immediately after the warehouse polygon check, before the obstacle-loop and SpatialIndex collision scans. This rejects tall-bay-in-low-ceiling candidates in O(segments) instead of O(obstacles + sol). Logically equivalent (same predicate, same return value) but faster on negative cases.

Result: subset Q sum 11021.47 → **10997.63** (−23.84, all from CaseWeird). Full 15-case Q sum **31843.27** (vs prior baselines 31977.55 / 32128.21 / 32041.67 — net ≈ −180 vs median).

### Step 12 — Multi-bay swap pass (Tier D #13) — NOT ATTEMPTED

Periodically: for each pair `(bay_A, bay_B)` placed adjacent, try replacing with one larger type that covers both footprints. Catches cases where 2× small + gap = 1× large with better Q. Run every 50 hill iterations.

Skipped this session: structural overlap with `remove_k_and_refill` (op 4) and `replace_bay` (op 1) — risk of redundant compute outweighed expected gain given current Q margins. Worth attempting only if a focused per-case study identifies adjacent-bay patterns left on the table.

### Step 13 — SA-aware adaptive weights — TRIED, REVERTED

Tried 3 variants (CREDIT_WINDOW=20 linear-decay; W=10 same; hybrid 1.0·improved + 0.25·credit). All net-negative on subset (+92, +89, +95 vs 11021.47 baseline). The binary improvement counter is the right signal even in SA: new-best discoveries come from successful improvement chains where the *most recent* op deserves the credit, not the surrounding context. SA's stochastic acceptance already handles "useful exploration" implicitly via the temperature schedule. **Don't retry without a fundamentally different signal.**

### Step 14 — Per-case T0 calibration — DONE

Added `t0_frac` parameter to `hill_sa`. Restart distribution: 2× cold (3%), 4× medium (5%), 2× hot (8%). Marginal angled-case win (CaseAngledA −59, CaseAngledB −17). Kept; absorbed into `hill_sa` signature.

### Step 15 — Restart re-seeding from best on plateau — TRIED, REVERTED

Tried PLATEAU=1000/2500 with BOOST=0.5/0.4/0.2 multipliers on T. All noise-level (±20 on subset). Reverted. Lesson: SA naturally drifts back toward `best`-quality regions through accumulated improvements; explicit re-seeding adds randomness that net-cancels.

## Session N+1 — Driven by op-stats diagnostic (full 15-case run)

Used `Operator stats for X` diagnostic output to identify high-ROI changes. Net result: **31843.27 → ~30740 (−1100, −3.5%).**

### Step A — Kill dead operators — REVERTED

Diagnostic showed `fill_aggressive` (op 2) had 0 tries / 0 improved on every case (it was already absent from `build_active_ops`). Tried gating ops 6,7 (`rotate_compact_and_add`, `add_45_degree_bay`) off when `axis_aligned=true` since aggregate stats showed 0 improvements on axis-aligned cases.

**REVERTED.** CaseForcedAngle has axis-aligned warehouse but obstacle-forced angled bays — op 7 contributed 10 improvements there (small count, +388 Q regression when removed). Aggregate diagnostics ≠ per-case importance. Even geometric gating is too aggressive.

**Lesson:** when interpreting op-stats, treat "0% on aggregate" as "0% on most cases" — always look for outliers before disabling. The user's overfitting concern (idea B in chat) generalizes to geometry-driven gating too.

### Step D — Cluster removal in `shared_gap_refill` — KEPT

Replaced random k-bay removal with a 25%-probability cluster-removal: pick a seed bay, compute squared distances from its center to all others, partial-sort to find k nearest, erase those. Creates a contiguous void where larger high-value bays can fit during the refill phase.

Tuning:
- 50% cluster prob: subset +250 (CaseAngledD/DiagArmD/AngledD all regressed).
- 25% cluster prob: subset −80 to −100. Full **−836 vs prior best (31843 → 31007)**.

Big winners: Case3 (−512), CaseForcedAngle (−305). Cluster mode lets the refill place larger types in contiguous voids; scattered removal at the same rate just reshuffles the existing tight pack.

### Step E1 — Bump deadline 22s → 25s — KEPT

Trivial change to `CASE_BUDGET_SECONDS`. Tested 22 (baseline) / 24 / 25 / 26. SA cooling is `t / total_seconds` so longer deadlines stretch the T schedule, which can over-explore on sensitive cases (CaseForcedAngle regressed at 26s).

Sweet spot at 25s:
- 22s: baseline 31006.80
- 24s: 30782.28 (run 1) / 30917.18 (run 2) → mean ~30850
- **25s: 30683.31 (run 1) / 30713.74 (run 2) / 30683.31 (run 3) → mean 30693**
- 26s: 30815.95 (run 1) → CaseForcedAngle +160

Per-case 25s, total 375s, well under 30s judge limit.

### Step F — Cluster removal in `remove_k_and_refill` — REVERTED

Tried the same 25% cluster pattern in op 4. **Subset +340, CaseForcedAngle +330 reproducibly.**

**Lesson:** cluster-removal works because of **interaction** with `add_shared_gap_bay`'s gap-aware placement strategy. Op 4 refills with vanilla `add_bay` which places greedily anywhere — cluster void doesn't compose with that, the refill just puts bays back in the same spots. Don't generalize successful changes to similar-looking operators without checking the refill side.

### Step C — Smarter initial seed for angled cases — REVERTED

For non-axis-aligned warehouses, `build_initial` skips shelf-pack and only does `INITIAL_ADDS=80` of plain `add_bay`. Tried bootstrapping with one `add_shared_gap_bay` mid-seed (i==5 and i==10) so subsequent `shared_gap_refill` operators have a pattern to extend.

Results were noise-level: trades CaseAngledD (−56) for CaseDiagArmD (+52), or vice versa depending on i. No net win across angled cases.

**Lesson:** angled-case seed quality is dominated by SA exploration, not by the initial layout. The 25s SA budget is enough that any reasonable seed converges to similar Q. To actually help, the seed would need to be structurally different (e.g. shelf-pack along warehouse's primary axis after rotation), which is significant code complexity for unclear gain.

## Final state after this session

Production:
- `CASE_BUDGET_SECONDS = 25.0` (was 22)
- `shared_gap_refill` has 25% cluster-mode branch
- All other operators unchanged from previous session

Full 15-case Q ≈ 30683–30816 (3-run band, mean ~30740). Per-case wall ≈ 25s, total ~375s.

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

- `solver/solver.cpp` — production solver (now contains `hill_sa`, op 8, per-case T0, ceiling pre-check hoist)
- `solver/solver_ALNS.cpp` — separate ALNS solver (stale, not synced with this session)
- `/tmp/step{N}_out.log` — most recent benchmark logs from this session
- `/tmp/step10_final.log` — final 15-case run (Q sum 32023.61)
- `/tmp/step11_full.log` — current 15-case run (Q sum 31843.27, this session)

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
