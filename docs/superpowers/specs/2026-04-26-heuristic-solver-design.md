# Heuristic Solver Design (`solver_heuristic.cpp`)

**Date:** 2026-04-26
**Goal:** Build a fully deterministic, purely heuristic constructive solver for the Mecalux warehouse bay placement problem. No metaheuristics (no ALNS, no hill climbing, no simulated annealing, no random restarts). Decisions driven by hand-designed criteria.

## 1. Problem recap

- Place axis-aligned bays inside a polygonal warehouse with obstacles and a stepped ceiling.
- Each bay type has `(w, d, h, gap, loads, price)`.
- Rotation ∈ {0, 1} (0° or 90°). Coordinates in mm.
- Gap rule: clearance zone after the bay. When two bays face each other along the gap side, effective separation = `max(gap_a, gap_b)` (face-to-face sharing) instead of `gap_a + gap_b`.
- **Objective:** **MINIMIZE** `Q = (Σprice / Σloads)^(2 - area_bays / area_warehouse)`.

The base `Σprice/Σloads` is typically > 1, and the exponent `(2 - area_bays/area_wh)` decreases as we fill the warehouse, so filling more space *reduces* Q. Both numerator improvement (low price/loads) and area filling pull Q down.

## 2. Architecture

```
read_inputs()
   │
   ▼
build_variants()  ──►  N=12 deterministic variants
   │
   ▼  (parallel via OpenMP)
for each variant v:
    sol_v = construct(v)        // pure greedy with criteria fixed by v
   │
   ▼
return argmin Q(sol_v)
   │
   ▼
write solution.csv
```

- New file: `solver/solver_heuristic.cpp` (~600–800 lines).
- Reuses geometry primitives from `solver_ALNS.cpp` (copied, not refactored to header — YAGNI):
  `rect_inside_polygon`, `min_ceiling_between`, gap rule, `SpatialIndex`, `Solution` accumulator.
- **Does not** reuse: ALNS operators, hill-climb, random restarts, type-swap pass, rotate-perturb pass.

## 3. Data structures

```cpp
struct Candidate {
    double x, y;          // bay anchor (lower-left of placed rect)
    int    type_id;
    int    rotation;      // 0 or 1
    double score;
};

struct CornerPoint {
    double x, y;
    enum Origin { WALL_BL, WALL_BR, WALL_TL, WALL_TR,
                  CONCAVE_VERTEX,
                  BAY_TL, BAY_TR, BAY_BL, BAY_BR } origin;
};

enum SweepOrder { LOWEST_LEFT, LEFTMOST_LOW, LONGEST_EDGE_FIRST };
enum TypePrio   { BY_PRICE_PER_LOAD, BY_AREA_DESC, BY_AREA_ASC, BY_DENSITY };

struct Variant {
    SweepOrder sweep;
    TypePrio   type_prio;
    int        seed_corner;     // 0=BL, 1=BR, 2=TL, 3=TR (used for tie-breaking in sweep)
    double w_ratio, w_fill, w_gap_pair, w_corner, w_ceiling;
};
```

## 4. Candidate point generation (skyline / corner points)

**Initial candidates (before any bay):**
- 4 corners of the warehouse polygon's bounding box.
- Every concave vertex of the polygon (interior angle > 180°).

**After placing bay with rect `(x, y, w_eff, d_eff)` and gap zone `G`:**
- Add 4 outer corners of the bay rectangle (TL, TR, BL, BR).
- Add 4 outer corners of the `bay ∪ gap` rectangle (so the next bay can sit flush against the gap edge).
- Lazy-invalidate any existing `CornerPoint` strictly inside `bay ∪ gap`.

**Validation of `(cp, type_id, rotation)`:**
1. The placed rectangle `(cp.x, cp.y, w_eff, d_eff)` lies inside the warehouse polygon (`rect_inside_polygon`).
2. Does not overlap any obstacle.
3. Does not overlap any placed bay, applying face-to-face gap rule:
   - If two bays are co-linear on the gap side, separation = `max(gap_a, gap_b)`.
   - Otherwise, separation = `gap_a + gap_b` (additive, default).
4. `bay.h ≤ min_ceiling_between(cp.x, cp.x + w_eff)`.

Use `SpatialIndex` (grid-cell hash) for O(1) amortized overlap checks.

## 5. Multi-criteria score

For each valid `(cp, type_id, rotation)`:

```
score = w_ratio    * S_ratio
      + w_fill     * S_fill
      + w_gap_pair * S_gap_pair
      + w_corner   * S_corner
      + w_ceiling  * S_ceiling
```

All components normalized to [0, 1]:

| Component | Definition |
|---|---|
| `S_ratio` | `(price/loads) / max_price_per_load_global` |
| `S_fill` | `area_bay / max_bay_area_global` |
| `S_gap_pair` | `1` if placement shares a gap face-to-face with an existing bay or wall (effective separation = `max(gap_a, gap_b)` < `gap_a + gap_b`); else `0` |
| `S_corner` | `1` if bay touches two warehouse/obstacle/bay edges (corner contact), `0.5` if one, `0` if floating |
| `S_ceiling` | `1 - (local_ceiling - bay_height) / max_ceiling`, clamped to [0, 1]. Penalizes short bays under tall ceiling. |

**Hard filter (applied before scoring):**
- Reject any candidate whose placement does not strictly improve Q vs current solution.
  This implements the "stop when no improvement" termination.

**Tie-breaking** (when scores equal within 1e-9): smaller `cp.x + cp.y`, then smaller `type_id`, then smaller `rotation`. Guarantees determinism across runs and across OpenMP thread orderings.

## 6. Construction loop (single variant)

```
solution = empty
candidates = initial_corner_points()

repeat:
    Q_now = Q(solution)
    best  = none

    sort candidates by sweep_order(variant.sweep, variant.seed_corner)
    type_order = sort(types) by variant.type_prio

    for each cp in candidates:
        for each type in type_order:
            for rot in {0, 1}:
                if not valid(cp, type, rot): continue
                if Q(solution + bay) >= Q_now: continue
                s = score(cp, type, rot, variant.weights)
                if s > best.score (or tie-break wins): best = ...

    if best is none: break

    solution.push(best)
    candidates += new_corners_from(best)
    candidates = filter_inside(candidates, best.rect_with_gap)

return solution
```

Per-iteration cost: ~|candidates| × |types| × 2 validations, each O(1) amortized.
Estimated ≤ a few hundred ms per variant.

## 7. Variants (12 total)

Run all 12 in parallel with OpenMP. Return argmin Q.

| # | sweep | type_prio | seed | w_ratio | w_fill | w_gap_pair | w_corner | w_ceiling |
|---|---|---|---|---|---|---|---|---|
| 1 | LOWEST_LEFT | BY_PRICE_PER_LOAD | BL | 1.0 | 0.5 | 0.3 | 0.3 | 0.2 |
| 2 | LOWEST_LEFT | BY_AREA_DESC | BL | 0.5 | 1.0 | 0.3 | 0.3 | 0.2 |
| 3 | LOWEST_LEFT | BY_AREA_ASC | BL | 0.7 | 0.7 | 0.5 | 0.3 | 0.2 |
| 4 | LEFTMOST_LOW | BY_PRICE_PER_LOAD | BL | 1.0 | 0.5 | 0.3 | 0.3 | 0.2 |
| 5 | LEFTMOST_LOW | BY_DENSITY | BL | 0.7 | 0.7 | 0.3 | 0.3 | 0.2 |
| 6 | LONGEST_EDGE_FIRST | BY_PRICE_PER_LOAD | auto | 1.0 | 0.5 | 0.5 | 0.3 | 0.2 |
| 7 | LONGEST_EDGE_FIRST | BY_AREA_DESC | auto | 0.5 | 1.0 | 0.5 | 0.3 | 0.2 |
| 8 | LOWEST_LEFT | BY_PRICE_PER_LOAD | BR | 1.0 | 0.5 | 0.3 | 0.3 | 0.2 |
| 9 | LOWEST_LEFT | BY_PRICE_PER_LOAD | TL | 1.0 | 0.5 | 0.3 | 0.3 | 0.2 |
| 10 | LOWEST_LEFT | BY_PRICE_PER_LOAD | TR | 1.0 | 0.5 | 0.3 | 0.3 | 0.2 |
| 11 | LOWEST_LEFT | BY_AREA_DESC | BL | 1.0 | 1.0 | 0.5 | 0.5 | 0.5 |
| 12 | LONGEST_EDGE_FIRST | BY_AREA_ASC | auto | 0.5 | 0.5 | 1.0 | 0.5 | 0.2 |

Definitions:
- `BY_DENSITY`: sort types by `(price/loads) / area` descending.
- `BY_AREA_DESC` / `BY_AREA_ASC`: sort types by `w*d` desc/asc.
- `LOWEST_LEFT`: sort candidates by `(y, x)` ascending. `seed=BR` flips x to descending; `TL` flips y; `TR` flips both.
- `LONGEST_EDGE_FIRST` (auto seed): identify the longest polygon edge; orient candidate sweep along that edge; the start corner is the edge endpoint with the smallest coordinate sum.

## 8. CLI and outputs

- Compile (macOS, brew libomp):
  ```bash
  g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
      -L/opt/homebrew/opt/libomp/lib -o /tmp/solver_heuristic solver/solver_heuristic.cpp
  ```
- Run from `solver/` so `Case0..Case3` are relative.
- Writes `Case{N}/solution.csv` with columns `Id,X,Y,Rotation`.
- Prints per-case Q and chosen variant index to stdout.

## 9. Testing plan

1. **Validity**: for each emitted bay, verify polygon containment, obstacle non-overlap, ceiling, and gap rule. Add an in-binary `--verify` flag that re-checks the written solution.
2. **Q comparison**: run on Case0..Case3 and angled cases; print Q vs `solver_ALNS.cpp` baseline (table in commit message).
3. **Determinism**: run twice; `diff solution.csv` must be empty (byte-identical).
4. **Wall time**: target < 5s per case on M-series macOS with OpenMP.

## 10. Out of scope

- Angles other than 0°/90°.
- Refactoring shared geometry into a header (will revisit only if a third solver appears).
- Frontend integration changes.
- Replacing `solver.cpp` or `solver_ALNS.cpp`.

## 11. Open risks

- The "improve Q strictly" filter may stop early on warehouses where the optimum requires temporarily worse Q. Mitigation: variants 11/12 use heavier `w_fill`, biasing toward area filling regardless of marginal Q.
- Concave vertex detection requires correct polygon winding. We will normalize the polygon to CCW order at parse time.
- Determinism under OpenMP requires that the per-variant function be pure (no shared mutable state) and that the final reduction (argmin) tie-breaks on variant index. Both will be enforced explicitly.
