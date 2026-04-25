# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HackUPC 2026 — Mecalux Warehouse Optimizer. Given a warehouse polygon, obstacles, a stepped ceiling, and bay type catalog, produce a 2D bay placement that maximises the metric:

```
Q = (Σ price/loads)^(2 - area_bays/area_warehouse)
```

## Repo layout

```
solver/
  solver.cpp         # C++ solver (primary) — hill climbing, 8 parallel restarts via std::async
  solver_ALNS.cpp    # C++ ALNS solver (optimized) — deadline-driven, OpenMP, all optimizations
  solver.py          # Python solver (legacy)
  visualize.py       # 2D matplotlib visualisation — run with python3, blocks on plt.show()
  visualize_3d.py
  Case0–Case3/       # input CSVs + solution.csv per case
frontend/            # React+Vite UI (in development — owner: David)
plan.md              # full architecture doc — read this for design decisions
```

## Solver CLI

### C++ solver (primary)

```bash
# Compile (macOS, no OpenMP needed)
g++ -O3 -std=c++17 -o /tmp/solver solver/solver.cpp

# Compile solver_ALNS (macOS, homebrew libomp)
g++ -O3 -std=c++17 -I/opt/homebrew/opt/libomp/include -Xpreprocessor -fopenmp -lomp \
    -L/opt/homebrew/opt/libomp/lib -o /tmp/optimizer solver/solver_ALNS.cpp

# Run (from solver/ directory so Case0–Case3 are relative)
cd solver && /tmp/solver
```

Current wall time: ~40s per case (8 restarts × `ITERATIONS=450`). **Over the 30s judge limit** — deadline-driven loop not yet implemented in `solver.cpp`.

### Python solver (legacy)

```bash
cd solver && python3 solver.py
```

Output is written to `Case{N}/solution.csv` (columns: `Id,X,Y,Rotation`).

## CSV formats

All coordinates are in **millimetres**, origin bottom-left.

| File | Columns |
|------|---------|
| `warehouse.csv` | `x,y` — polygon vertices in order |
| `obstacles.csv` | `x,y,w,d` — axis-aligned rectangles |
| `ceiling.csv` | `x,h` — stepped ceiling; segment `[x, next_x)` has height `h` |
| `types_of_bays.csv` | `id,w,d,h,gap,loads,price` |
| `solution.csv` | `Id,X,Y,Rotation` — `Rotation` is `0` or `1` (not degrees) |

`Rotation=0` → w×d as-is. `Rotation=1` → w and d are swapped (90°).
`gap` is a clearance zone added **after** the bay: rot=0 adds `(x, y+d, w, gap)`; rot=1 adds `(x+w, y, gap, d)`.

## Frontend stack

React + TypeScript + Vite + Tailwind CSS + Canvas 2D API (native). PapaParse for CSV parsing. Small Node/Express bridge to spawn `solver.py` and return `solution.csv`.

See `plan.md` §6 for the full component breakdown and integration approach.

## Key geometry invariants

- `rect_inside_polygon` uses vertical ray-casting at mid-points of sub-intervals — do not replace with a naive AABB check.
- `min_ceiling_between(x1, x2, ceiling)` is a linear scan; the ceiling is a step function defined by `(x_start, height)` pairs with the last segment extending to infinity.
- Two rectangles overlap iff `not (ax+aw≤bx or bx+bw≤ax or ay+ad≤by or by+bd≤ay)`.

## solver.cpp — current state

### Constants (top of file)
```cpp
const int ITERATIONS    = 450;   // fixed per restart — replace with deadline
const int INITIAL_ADDS  = 80;
const int RESTARTS      = 8;
const int MAX_POINTS_ADD = 80;
const int ANGLE_SAMPLE  = 14;
```

### Parallelism
Uses `std::async(launch::async, worker, r)` — 8 futures, one per restart. Each restart is independent with a seeded `thread_local mt19937 rng`.

### Operators (in `hill`)
- `add_bay` — greedy placement at candidate points
- `replace_bay` — remove one bay, add new one
- `fill_aggressive` — add up to 5 bays
- `remove_k_and_refill` — remove k bays, add k+3
- `shared_gap_refill` — remove k bays, refill with gap-sharing angle pairs
- `upgrade_bay` — swap type/angle of one bay in-place

### Solution accumulator (`struct Solution`)
Wraps `vector<PlacedBay>` with O(1) quality computation:
- `sum_price`, `sum_loads`, `bay_area` maintained incrementally
- `push()`, `pop()`, `erase(idx)` update accumulators
- `quality(wh_area)` is O(1)
- `recompute()` rebuilds from scratch after bulk ops (O(n), called once per hill iteration)
- Used in: `upgrade_bay`, `remove_k_and_refill`, `replace_bay`, `shared_gap_refill`, `hill`

### Known gaps vs solver_ALNS.cpp
| Feature | solver_ALNS.cpp | solver.cpp |
|---|---|---|
| Deadline-driven loop | ✓ | ✗ (fixed ITERATIONS) |
| SpatialIndex in valid_candidate | ✓ | ✗ |
| Interior grid candidate points | ✓ | ✗ |
| Face-to-face gap rule | ✓ | ✗ |
| Type-swap operator | ✓ | ✗ |
| Rotation-perturbation operator | ✓ | ✗ |
| Solution accumulator (O(1) Q) | ✓ | ✓ |
| OpenMP parallelism | ✓ | ✗ (std::async) |

## solver_ALNS.cpp — optimizations implemented

1. `Deadline` struct + wall-budget constants (25s ALNS + 3.5s intensification)
2. `Solution` accumulator for O(1) Q eval
3. Deadline-driven ALNS loop
4. Per-restart time slicing with OpenMP
5. 360° angle search (36 angles, 0–350°), `ANGLE_SAMPLE=12`, `INTENSIFY_ANGLE_SAMPLE=18`
6. Face-to-face gap rule: co-linear gaps allowed if bay separation ≥ `max(gap_A, gap_B)`
7. Interior grid candidate points (`build_interior_grid`), cap 400
8. Type-swap operator (`type_swap_pass`)
9. Rotation-perturbation operator (`rotate_perturb_pass`, deltas ±10/±20/±90/180°)
10. `SpatialIndex` grid-cell hash for O(1) collision narrowing

## Best known Q scores

| Case | solver.cpp | solver_ALNS.cpp |
|------|-----------|-----------------|
| Case0 | 1533 | 1665 |
| Case1 | 2250 | 3705 |
| Case2 | 5394 | 6288 |
| Case3 | 3074 | 7258 |

(Lower Q is better)

## Pending optimizations for solver.cpp

Priority order:
1. **Deadline-driven loop** — most critical, currently exceeds 30s judge limit
2. **SpatialIndex** — O(n)→O(1) collision check, hottest code path
3. **Interior grid candidate points** — better coverage of open space
4. **Type-swap operator** — focused type search at fixed positions
5. **Rotation-perturbation operator** — small angle deltas as diversification
6. **Thread `Solution` through operators** — avoid `recompute()` O(n) per iteration
