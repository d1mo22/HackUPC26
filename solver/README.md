# solver/

The C++ optimisation engine for the Mecalux Warehouse Optimizer. One source file, two run modes, 29 bundled test cases.

## Build

```bash
# macOS / Linux — no OpenMP needed (uses std::async for parallelism)
g++ -O3 -std=c++17 -o /tmp/solver solver.cpp
```

The frontend bridge compiles the solver automatically on startup, so you only need the manual build for CLI use.

## Run modes

### CLI mode — solve one case directory

```bash
/tmp/solver path/to/case_dir
```

`case_dir` must contain the four input CSVs **flat** (no nested `CaseX/`):

```
case_dir/
  warehouse.csv
  obstacles.csv
  ceiling.csv
  types_of_bays.csv
```

The solver writes `case_dir/solution.csv`. This is the mode the frontend uses.

### Discovery mode — solve all `CaseX/` subfolders

```bash
cd solver && /tmp/solver
```

Scans the current directory for subfolders that contain all four CSVs, then solves each one. `solution.csv` is written into each subfolder. Folders are processed in a preferred order (see `PREFERRED_CASE_ORDER` in `solver.cpp`).

## Output

One line per case to stdout:

```
=== Solving <case_dir> ===
BEST bays=18 area=3.728e+07 loads=147 price=44100 Q=1180.87 elapsed=2.3s -> <case_dir>/solution.csv
```

`solution.csv` columns: `Id,X,Y,Rotation` (rotation in degrees, multiples of 90 in I/O).

## Algorithm

Hill climbing + simulated annealing with **10 parallel restarts** via `std::async(launch::async, ...)`. Each restart has its own seeded `thread_local mt19937` and runs until the wall-time deadline or iteration cap is reached.

### Restart strategy

| Restart `r` | Mode |
|---|---|
| 0–1 | Strict-improvement hill climbing (safety net) |
| 2–3 | SA with `T0 = 3%` of starting Q (cold) |
| 4–7 | SA with `T0 = 5%` (medium) |
| 8–9 | SA with `T0 = 8%` (hot) |

Cold SA wins on tight axis-aligned cases; hot SA helps angled / forced-angle cases escape larger local minima. Two pure hill restarts guarantee we never regress from a deterministic baseline.

### Operators

The hill loop picks one operator per iteration via an adaptive UCB-style selector. It warms up uniformly for 150 iterations and then biases toward operators with a high recent improvement rate, with exponential weight decay every 300 iterations.

Each operator has a prior weight for axis-aligned (`weight_axis`) and angled (`weight_angled`) warehouses:

| # | Operator | axis w | angled w | What it does |
|---|---|---|---|---|
| 0 | `add_bay` | 4 | 4 | Tries up to 500 candidate points (polygon vertices, obstacle corners, existing bay corners/midpoints). For each point tests all types × up to 14 angles. Places the single best-scoring valid bay. |
| 1 | `replace_bay` | 2 | 2 | Removes a random bay, calls `add_bay` in its place. Reverts if Q worsens. |
| 2 | `fill_aggressive` | 0 | 0 | Calls `add_bay` up to 5 times in one step. Weight 0 means it is skipped during warmup and only activated by the adaptive selector post-warmup. |
| 3 | `remove_k_and_refill` | 6 | 6 | Removes k = min(4, \|sol\|/5) random bays, then calls `add_bay` k+3 times. Reverts if Q worsens. Main escape-from-local-optima operator. |
| 4 | `shared_gap_refill` | 8 | 8 | Removes k = min(6, max(2, \|sol\|/4)) bays — randomly or in a spatial cluster (25% chance). Refills using `add_shared_gap_bay` (back-to-back gap sharing) with `add_bay` fallback. Highest prior weight. |
| 5 | `upgrade_bay` | 4 | 4 | Removes a random bay and tries every type × angle at the **same (x,y) position**. Reinstalls the one with the lowest `quality_with_added`, or the original if nothing is better. Pure in-place type/angle swap. |
| 6 | `rotate_compact_and_add` | 1 | 4 | Picks up to 4 random bays, tries rotating each to all alternative angles. For each valid rotation attempts `add_shared_gap_bay` (or `add_bay`). Keeps the globally best result. |
| 7 | `add_45_degree_bay` | 1 | 2 | Compacts existing diagonal bays toward the nearest wall/obstacle, then calls `add_bay` restricted to angles {45°, 135°, 225°, 315°}. Compacts again. Reverts if Q worsens. |
| 8 | `position_perturb_and_add` | 2 | 1 | Shifts up to 2 random bays by ±100 mm or ±300 mm in X or Y. For each valid shifted position attempts `add_shared_gap_bay`. Keeps the best result. |
| 9 | `split_bay` | 2 | 2 | Finds diagonal bays whose bounding box is ≥1.15× their actual area. Removes the victim and attempts 12 random placements of smaller types within its bbox at axis-aligned angles. Accepts only if ≥2 placed **and** Q improved. |

### Initial solutions

Two independent construction paths run before search begins.

#### `build_initial` — one per restart (×10)

Each restart (`r = 0..9`) uses `mode = r % 4` (see scoring modes below).

1. **Shelf-pack phase** (axis-aligned warehouses only): tests 4 variants of `shelf_pack_into`:
   - orientation 0, offset (0, 0)
   - orientation 1, offset (0, 0)
   - orientation 0, offset (min_w/2, 0)
   - orientation 1, offset (0, min_w/2)

   Keeps the variant with the lowest Q.

2. **Greedy fill phase**: calls `add_bay` up to `INITIAL_ADDS = 80` times.

#### `build_optimized_greedy` — one concurrent thread (mode = BALANCED)

More elaborate construction running in parallel with the restarts:

1. **Shelf-pack**: 8 variants (orientation 0/1 × off_x ∈ {0, min_w/2} × off_y ∈ {0, min_w/2}).
2. **Greedy fill**: 200 iterations of `add_shared_gap_bay` → `add_bay` fallback.
3. **Light refinement**: 5 rounds of `upgrade_bay` + `shared_gap_refill`.

#### `shelf_pack_into` (base of both)

Row-by-row greedy fill over the warehouse bounding box. Outer loop advances Y by the tallest bay placed in the current row (or Y_SKIP=200 mm if the row was empty). Inner loop advances X by the bay footprint width, or X_STEP=100 mm on failure. Tries the primary orientation first, then the alternative. Stops at MAX_ROWS=500 or the bounding box top.

### Scoring modes for `bay_score_mode`

Used to rank bay types during construction and by some operators (`add_bay`, `upgrade_bay`, `shelf_pack_into`).

| Value | Name | Score formula | Prioritises |
|---|---|---|---|
| 0 | `CHEAP_LOAD` | `-price/loads` | Bays with best price-per-load ratio |
| 1 | `BIG_AREA` | `area / wh_area` | Bays with the largest footprint |
| 2 | `LOW_GAP` | `-gap_ratio + 0.2·area_ratio` | Bays with minimal gap overhead |
| 3 | `BALANCED` | `2·area_ratio − 1·price_load − 0.5·gap_ratio` | Compromise across all dimensions |

Two small tie-breaking adjustments are applied on top via `candidate_score_mode`:
- **+0.05** if the placement angle is axis-aligned (0°/90°/180°/270°)
- **−0.000001 × (x+y)** to break ties toward the origin

### Solution accumulator (`struct QSums`)

Maintains `Σprice`, `Σloads`, `Σarea` incrementally so that:

```
quality_with_added(cur, p, l, a, wh_area)   // O(1)
quality_from_sums(sums, wh_area, empty)     // O(1)
```

This makes the inner loop hot path collision-bound rather than score-bound.

### Tuning constants (top of `solver.cpp`)

```cpp
const int    ITERATIONS            = 450;     // hill/SA loop cap
const int    INITIAL_ADDS          = 80;      // initial-solution placement budget
const int    RESTARTS              = 10;
const int    ANGLE_SAMPLE          = 14;      // 360° angle search resolution
const double CASE_BUDGET_SECONDS   = 200.0;    // wall-time deadline per case
```

Each hill loop stops on `Clock::now() < deadline && iter < ITERATIONS`. The judge limit is 30s; budget is 25s with margin. Bump `ITERATIONS` to a large value (e.g. 100 000) if you want pure deadline-bound behaviour.

## Geometry invariants

- **`rect_inside_polygon`** uses vertical ray-casting at the mid-points of sub-intervals — do *not* simplify to a naive AABB-vs-bbox check; concave warehouses break it.
- **`min_ceiling_between(x1, x2, ceiling)`** is a linear scan; the ceiling is a step function defined by `(x_start, height)` rows, with the last row extending to infinity.
- Two rectangles overlap iff `not (ax+aw≤bx or bx+bw≤ax or ay+ad≤by or by+bd≤ay)`.

## Test cases

29 bundled cases in this directory:

- `Case0`–`Case3`, `Case40`, `CaseWeird`, `CaseForcedAngle` — the original challenge cases.
- `CaseAngled{A..D}`, `CaseDiagArm{A..D}` — angled-warehouse stress tests.
- `EdgeCase01..04`, `HardCase01..10` — edge cases (tiny warehouse, impossible heights, mazes, narrow corridors, ceiling traps, etc.).

Each is a self-contained directory with the four input CSVs.

## Visualisers

```bash
python3 visualize.py path/to/case_dir       # 2D layout
python3 visualize_3d.py path/to/case_dir    # 3D with stepped ceiling
```

Requires `pandas` and `matplotlib`. The frontend's Canvas component renders the same data interactively.

## Best known Q scores (this solver)

| Case | Q |
|---|---|
| Case0 | 1125 (deadline-bound, 25s) |

Lower is better. Other cases TBD.
