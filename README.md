# Mecalux Warehouse Optimizer — HackUPC 2026

A 2D warehouse bay-placement solver built for the **Mecalux** challenge at HackUPC 2026. Given a warehouse polygon, axis-aligned obstacles, a stepped ceiling, and a catalogue of bay types, it produces a placement that minimises a price/loads/area tradeoff metric.

The repo contains:

- **`solver/`** — the C++ solver (hill climbing + simulated annealing, 10 parallel restarts) plus all test cases and Python visualisers.
- **`frontend/`** — a React + Vite UI that loads the four input CSVs, calls the solver via a small Express bridge, and renders the result in interactive 2D and 3D.

```
HackUPC26/
├── solver/
│   ├── solver.cpp          # the only solver
│   ├── visualize.py        # 2D matplotlib visualiser
│   ├── visualize_3d.py     # 3D matplotlib visualiser
│   └── CaseN/, EdgeCaseN/, HardCaseN/   # 29 input case folders, each with 4 CSVs
├── frontend/
│   ├── src/                # React UI (App, components, hooks, lib)
│   ├── server/index.ts     # Express bridge that compiles + runs the solver
│   ├── vite.config.ts      # proxies /solve to localhost:3001
│   └── package.json
├── docs/plan.md            # original architecture/design doc
└── CLAUDE.md               # working notes for AI coding agents
```

## The metric

```
Q = (Σprice / Σloads) ^ (2 - bay_area / available_warehouse_area)
```

where `available_warehouse_area = polygon_area − Σ obstacle_areas`.

**Lower Q is better.** Empty solutions are infeasible — every solution must place at least one bay.

## CSV formats

All coordinates are in **millimetres**, origin bottom-left.

| File | Columns |
|------|---------|
| `warehouse.csv` | `x,y` — polygon vertices, in order |
| `obstacles.csv` | `x,y,w,d` — axis-aligned rectangles (bottom-left + size) |
| `ceiling.csv` | `x,h` — stepped ceiling; segment `[x, next_x)` has height `h`; last row extends to infinity |
| `types_of_bays.csv` | `id,w,d,h,gap,loads,price` — bay catalogue |
| `solution.csv` | `Id,X,Y,Rotation` — `Rotation` is the angle in degrees (multiples of 90 in I/O; intermediate angles allowed in solver internals) |

`gap` is a clearance zone added on the bay's footprint side; direction follows rotation.

## Quick start

### Run the solver from the CLI

```bash
g++ -O3 -std=c++17 -o /tmp/solver solver/solver.cpp

# Solve a single case (writes solution.csv into the case directory)
/tmp/solver solver/Case0

# Solve every CaseN/ folder under CWD
cd solver && /tmp/solver
```

See [`solver/README.md`](solver/README.md) for output format, restart strategy, and tuning constants.

### Run the full UI

```bash
# Terminal 1 — frontend dev server (Vite)
cd frontend && npm install && npm run dev

# Terminal 2 — solver bridge (recompiles solver.cpp on startup)
cd frontend && npm run dev:server
```

Open the printed Vite URL, drop the four CSVs onto the loader, and hit **Run Solver**. See [`frontend/README.md`](frontend/README.md) for component overview and the bridge protocol.

## Scoring consistency

The frontend's `scoring.ts` mirrors the solver's `quality()` function bit-for-bit, including the **available** warehouse area (polygon minus obstacles). The Q displayed in the UI matches the Q the solver prints to stdout to numerical precision.

## License

Hackathon project, no formal licence. Built by team for HackUPC 2026.
