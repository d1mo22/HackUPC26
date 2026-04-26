# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

HackUPC 2026 — Mecalux Warehouse Optimizer. Given a warehouse polygon, obstacles, a stepped ceiling, and bay type catalog, produce a 2D bay placement that minimises the metric:

```
Q = (Σprice / Σloads) ^ (2 - bay_area / available_warehouse_area)
```

where `available_warehouse_area = polygon_area − Σobstacle_areas`.

## Repo layout

```
solver/
  solver.cpp         # C++ solver — original single-TU reference (kept for diff/parity)
  main.cpp           # entry point for multi-TU build
  Makefile           # builds solver/solver binary from src/*.cpp + main.cpp
  src/               # split TUs: types.h, geometry, csv_io, case_io, spatial, scoring, operators, search, solver_core
  visualize.py       # 2D matplotlib visualisation
  visualize_3d.py    # 3D matplotlib visualisation
  CaseX/             # input CSVs + solution.csv per case
frontend/
  src/               # React+Vite UI (App.tsx, components/, lib/, hooks/)
  server/index.ts    # Express bridge: runs `make -C solver/` on startup, exposes POST /solve on :3001
docs/
  plan.md            # original architecture doc
```

## Solver CLI

```bash
# Build (macOS, no OpenMP needed)
cd solver && make

# Discovery mode: scans CWD for CaseX/ subfolders, solves each.
cd solver && ./solver

# CLI mode: solve a single case directory containing flat CSVs (no CaseX/ subfolder).
# Writes solution.csv into <case_dir>/. This is what the frontend bridge uses.
./solver /path/to/case_dir
```

Wall budget: `CASE_BUDGET_SECONDS = 25` per case. Hill loops also cap at `ITERATIONS = 1000`, whichever fires first. Bump `ITERATIONS` if you want pure deadline-bound behaviour.

Output (per case, single line):
```
=== Solving <case_dir> ===
BEST bays=N area=... loads=... price=... Q=... elapsed=Ts -> <case_dir>/solution.csv
```

## CSV formats

All coordinates are in **millimetres**, origin bottom-left.

| File | Columns |
|------|---------|
| `warehouse.csv` | `x,y` — polygon vertices in order |
| `obstacles.csv` | `x,y,w,d` — axis-aligned rectangles |
| `ceiling.csv` | `x,h` — stepped ceiling; segment `[x, next_x)` has height `h` |
| `types_of_bays.csv` | `id,w,d,h,gap,loads,price` |
| `solution.csv` | `Id,X,Y,Rotation` — `Rotation` is the angle in degrees (0/90/180/270; intermediate angles permitted in solver internals) |

`gap` is a clearance zone added on the bay's footprint side; direction follows rotation.

## Frontend stack

React + TypeScript + Vite + Tailwind CSS + Canvas 2D API (native). PapaParse for CSV parsing. Node/Express bridge (`frontend/server/index.ts`) runs `make -C solver/` on startup, exposes `POST /solve`, and forwards CSVs to the binary via argv.

**Important:** Tailwind utility classes for layout/spacing/color are unreliable in this setup — use inline `style={{}}` props for everything. Tailwind structural classes (`flex`, `flex-1`, `overflow-hidden`) still work.

### To start the app

```bash
# Terminal 1 — frontend dev server
cd frontend && npm run dev

# Terminal 2 — solver bridge (needed for Run Solver button)
cd frontend && npm run dev:server
```

Vite proxies `/solve` to `http://localhost:3001` (see `frontend/vite.config.ts`).

### Solver bridge protocol (`frontend/server/index.ts`)

1. Runs `make -C solver/` to build `solver/solver` on startup.
2. `POST /solve` accepts multipart form fields: `warehouse`, `obstacles`, `ceiling`, `types`.
3. Writes the four CSVs flat into a fresh `mkdtempSync` directory.
4. Spawns `solver <tmpDir>` (CLI mode). Solver writes `<tmpDir>/solution.csv`.
5. Returns the CSV body, deletes the tmp dir.

## Frontend components

| File | What it does |
|------|--------------|
| `src/App.tsx` | Root: 3-column layout, StatusBadge, solver fetch, run history, bay-type filter state |
| `src/components/FileLoader.tsx` | 2×2 CSV drop zone + solution slot; validates required columns |
| `src/components/Controls.tsx` | Ceiling / Labels / Gap-zones toggles; Run Solver + Export CSV buttons |
| `src/components/Canvas.tsx` | 2D/3D canvas (zoom-to-cursor, pan, hover tooltip, fit-to-screen, orthographic 3D) |
| `src/components/BayLegend.tsx` | Per-type rows; click to filter canvas to that type |
| `src/components/MetricsPanel.tsx` | Q score, coverage %, bay count, total price/loads |
| `src/components/RightPanel.tsx` | 3-tab panel: Metrics+Legend · Breakdown · History |
| `src/components/BreakdownTab.tsx` | price/loads bar chart per type |
| `src/components/HistoryTab.tsx` | Past solver runs; click to restore on canvas |
| `src/hooks/useCanvas.ts` | Zoom + pan transform state |
| `src/lib/geometry.ts` | `bayDimensions`, `polygonBounds` |
| `src/lib/csvParser.ts` | CSV parsing with header validation |
| `src/lib/scoring.ts` | `computeMetrics` — mirrors solver's `quality()` exactly (subtracts obstacle area) |
| `src/types.ts` | `Point`, `Obstacle`, `CeilingSegment`, `BayType`, `PlacedBay`, `WarehouseCase`, `Solution`, `RunRecord` |

### Key Canvas notes

- **HiDPI**: `canvas.width = rect.width * dpr`, then `ctx.setTransform(dpr,0,0,dpr,0,0)`.
- **2D transform**: `screen_x = offsetX + wx * scale`, `screen_y = offsetY - wy * scale` (Y flipped).
- **Fit-to-screen** centers the polygon on both axes by distributing leftover slack on the non-binding axis.
- **Native wheel listener** with `{ passive: false }` — React's `onWheel` is passive.
- **Hover highlight**: drawn in a second pass after all bay fills.
- **3D projection**: `screenX = ox + (wx·cosAz − wy·sinAz)·s`, `screenY = oy − (ry·sinEl + wz·cosEl)·s`. Painter-sort descending by depth.

### Frontend Q must match solver Q

`scoring.ts:computeMetrics` mirrors `solver/src/scoring.cpp:quality()`. The denominator is the **available** warehouse area (polygon area minus obstacle areas), not raw polygon area. Pass `warehouseCase.obstacles` to keep them in sync.

## Key geometry invariants

- `rect_inside_polygon` uses vertical ray-casting at mid-points of sub-intervals — do not replace with a naive AABB check.
- `min_ceiling_between(x1, x2, ceiling)` is a linear scan; ceiling is a step function with the last segment extending to infinity.
- Two rectangles overlap iff `not (ax+aw≤bx or bx+bw≤ax or ay+ad≤by or by+bd≤ay)`.

## Multi-TU solver — `solver/src/`

The solver is split into focused translation units. The Makefile compiles exactly:
```
src/geometry.cpp  src/csv_io.cpp  src/case_io.cpp  src/spatial.cpp
src/scoring.cpp   src/solver_core.cpp  main.cpp
```
(`operators.cpp` and `search.cpp` exist but are **not** compiled by the Makefile — their contents are included in `solver_core.cpp` which is self-contained.)

| TU | Contents |
|----|----------|
| `src/types.h` | All shared types, constants, `extern rng`, `extern ANGLES` |
| `src/geometry.h/.cpp` | Geometry functions + `SpatialIndex`; **defines** `rng` and `ANGLES` |
| `src/csv_io.h/.cpp` | `read_warehouse`, `read_obstacles`, `read_ceiling`, `read_bays` |
| `src/case_io.h/.cpp` | `is_test_case_dir`, `discover_test_cases` |
| `src/spatial.h/.cpp` | Thin wrapper (SpatialIndex lives in geometry) |
| `src/scoring.h/.cpp` | `QSums`, `quality*`, `available_warehouse_area` |
| `src/operators.h/.cpp` | All 10 operators, `OPERATORS[]`, `build_active_ops` (reference only) |
| `src/search.h/.cpp` | `OperatorSelector`, `run_search`, `hill`, `hill_sa` (reference only) |
| `src/solver_core.h/.cpp` | `solve_case` + full self-contained build |
| `main.cpp` | Entry point (`main`) |

`solver/solver.cpp` is kept as a single-TU reference for diffing — it is **not** compiled by the Makefile.

### Constants (top of `src/types.h`)
```cpp
const int ITERATIONS    = 1000;       // legacy ceiling — actual stop is the deadline
const int INITIAL_ADDS  = 80;
const int RESTARTS      = 10;
const int ANGLE_SAMPLE  = 14;
const double CASE_BUDGET_SECONDS = 25.0;
```

Hill loops stop on `Clock::now() < deadline && iter < ITERATIONS`.

### Parallelism
`std::async(launch::async, worker, r)` — 10 futures, one per restart, each with a seeded `thread_local mt19937 rng`.

### Operator table (`OPERATORS[]`)
Each operator is an `OperatorEntry` in a static array indexed 0–9:
`add_bay`, `replace_bay`, `fill_aggressive`, `remove_k_and_refill`, `shared_gap_refill`,
`upgrade_bay`, `rotate_compact_and_add`, `add_45_degree_bay`, `position_perturb_and_add`, `split_bay`.

`OPERATORS[i].weight_axis` / `weight_angled` control the prior-weight in `build_active_ops`.
Adding a new operator = adding one row to `OPERATORS[]` and incrementing `NUM_OPERATORS`.

All operators take `(vector<PlacedBay>&, const OperatorContext&)` — no bare 6-argument tails.

### OperatorContext
Bundles `(types, warehouse, obstacles, ceiling, wh_area, mode)` — the read-only context
every operator needs. Passed by const-ref.

### OperatorSelector
Encapsulates warmup (150 iters of static sampling) + adaptive UCB-style operator selection
+ periodic exponential decay (every 300 iters). Used by `run_search`.

### run_search / hill / hill_sa
`run_search` is the unified search loop (`use_sa=false` → hill-climbing, `use_sa=true` → SA).
`hill` and `hill_sa` are thin wrappers that construct `OperatorContext` and forward to `run_search`.

### Solution accumulator (`struct QSums`)
Maintains `price`, `loads`, `area` incrementally. `quality_with_added()` and `quality_from_sums()` are O(1).

### Restart strategy (`RESTART_TIERS[]`)
Static array of `{use_sa, t0_frac}` — one entry per restart:
- `r ∈ [0..1]`: strict-improvement hill climbing.
- `r ∈ [2..3]`: SA with `T0 = 3%` of starting Q (cold).
- `r ∈ [4..7]`: SA with `T0 = 5%` (medium).
- `r ∈ [8..9]`: SA with `T0 = 8%` (hot).

### Parity harness
`--parity` flag: runs all discovered cases single-threaded with `RESTARTS=1`, seed 42,
prints `case\tQ\tbays` per case. Baseline at `solver/.parity_baseline.txt`.

## Best known Q scores (Case0, deadline-bound run)

| Case | Q |
|------|----|
| Case0 | 1125 |

Lower Q is better.
