# frontend/

React + TypeScript + Vite UI for the Mecalux Warehouse Optimizer. Loads the four input CSVs, calls the C++ solver via a small Express bridge, and renders the result interactively in 2D and 3D.

## Stack

- **React 19** + **TypeScript** + **Vite** for the UI.
- **Native Canvas 2D API** for the warehouse renderer (zoom-to-cursor, pan, hover tooltip, fit-to-screen, type filtering). No charting library — everything is drawn by hand for full control over performance.
- **Three.js** + `@react-three/fiber` + `@react-three/drei` for the 3D view (orthographic camera, ceiling planes, axis helpers).
- **PapaParse** for CSV parsing.
- **Tailwind CSS** for structural utilities only (`flex`, `flex-1`, `overflow-hidden`). Layout/spacing/colour use inline `style={{}}` — Tailwind utility classes for those are unreliable in the current setup.
- **Express** (`server/index.ts`) bridges the React app to the C++ solver binary.

## Run

```bash
cd frontend
npm install

# Terminal 1 — Vite dev server (UI on http://localhost:5173)
npm run dev

# Terminal 2 — solver bridge (compiles solver.cpp, listens on :3001)
npm run dev:server
```

Vite proxies `POST /solve` to `http://localhost:3001` (see `vite.config.ts`), so the UI talks to the bridge through the same origin.

### Other scripts

| Script | What it does |
|---|---|
| `npm run dev` | Vite dev server with HMR |
| `npm run dev:server` | `tsx watch server/index.ts` — recompiles solver on startup, restarts on edits |
| `npm run build` | Type-check + production build |
| `npm run preview` | Preview the production build |
| `npm run lint` | ESLint |

## Layout

```
frontend/
├── src/
│   ├── App.tsx                 # 3-column root layout, solver fetch, run history
│   ├── main.tsx                # React entry
│   ├── index.css               # Tailwind + CSS variables (theme tokens)
│   ├── types.ts                # Domain types (Point, Obstacle, BayType, PlacedBay, …)
│   ├── components/
│   │   ├── FileLoader.tsx      # 2x2 CSV drop zone + solution slot, header validation
│   │   ├── Controls.tsx        # Layer toggles + Run Solver / Export buttons
│   │   ├── Canvas.tsx          # 2D canvas (also routes to Canvas3D for 3D mode)
│   │   ├── Canvas3D.tsx        # Three.js scene
│   │   ├── BayLegend.tsx       # Per-type colour legend; click to filter the canvas
│   │   ├── MetricsPanel.tsx    # Q score, coverage %, totals
│   │   ├── BreakdownTab.tsx    # price/loads bar chart per bay type
│   │   ├── HistoryTab.tsx      # Past solver runs, click to restore
│   │   └── RightPanel.tsx      # 3-tab right column (Metrics · Breakdown · History)
│   ├── hooks/
│   │   └── useCanvas.ts        # 2D zoom + pan transform state
│   └── lib/
│       ├── csvParser.ts        # PapaParse + header validation, friendly errors
│       ├── geometry.ts         # bayDimensions(), polygonBounds()
│       └── scoring.ts          # computeMetrics — mirrors solver/solver.cpp:quality()
├── server/
│   └── index.ts                # Express bridge: compiles solver, exposes POST /solve
├── public/
├── vite.config.ts              # /solve proxy → :3001, Tailwind plugin
└── package.json
```

## Data flow

```
┌─────────────┐  drag/drop CSVs  ┌─────────────────┐  csvParser.ts   ┌──────────────┐
│ FileLoader  │ ───────────────► │ App.tsx (state) │ ──────────────► │ Canvas       │
└─────────────┘                  │  warehouseCase  │                 │ (2D/3D)      │
                                 │  rawFiles       │                 └──────────────┘
                                 └────────┬────────┘                        ▲
                                          │ click "Run Solver"              │
                                          ▼                                 │
                          POST /solve  (multipart: 4 CSVs)                  │
                                          │                                 │
                          ┌───────────────┴────────────────┐                │
                          │ frontend/server/index.ts       │                │
                          │  • mkdtempSync() → tmpDir      │                │
                          │  • write 4 CSVs flat           │                │
                          │  • execFile(solver, [tmpDir])  │                │
                          │  • read tmpDir/solution.csv    │                │
                          └───────────────┬────────────────┘                │
                                          │ text/csv body                   │
                                          ▼                                 │
                                  parseSolution() ──── placements ──────────┘
                                          │
                                          ▼
                                  computeMetrics() → Q, coverage, totals
                                          │
                                          ▼
                                  RunRecord pushed to runHistory
```

## Solver bridge protocol (`server/index.ts`)

1. **On startup** it compiles `../solver/solver.cpp` to `$TMPDIR/warehouse-solver` with `g++ -O3 -std=c++17`. If compilation fails, the server exits — the UI's Run Solver button will then 502/network-error until you fix the code.
2. **`POST /solve`** accepts a `multipart/form-data` body with four file fields: `warehouse`, `obstacles`, `ceiling`, `types`. Missing fields → `400`.
3. The handler creates a fresh `mkdtempSync()` directory, writes the four CSVs into it flat (no nested `CaseX/` folder).
4. It spawns `solver <tmpDir>` (CLI mode — see [`solver/README.md`](../solver/README.md)). Timeout is 120 s.
5. If `tmpDir/solution.csv` exists when the child exits, the server returns it as `text/csv`. Otherwise it returns `500` with the solver's stderr.
6. The temp dir is always cleaned up.

## Canvas implementation notes

These details live in `Canvas.tsx`. They're easy to break and worth knowing about:

- **HiDPI**: `canvas.width = rect.width * dpr` and `ctx.setTransform(dpr, 0, 0, dpr, 0, 0)`. All draw coordinates stay in logical pixels.
- **2D transform convention**: `screen_x = offsetX + wx * scale`, `screen_y = offsetY − wy * scale` (Y is flipped because canvas Y grows downward, world Y grows upward).
- **Fit-to-screen** centers the polygon by computing the leftover slack on the non-binding axis (`drawnW = worldW * scale`) and distributing it equally.
- **Native wheel listener** with `{ passive: false }`. React's `onWheel` is passive and silently fails to call `preventDefault()`, which would let the browser zoom the page when scrolling on the canvas.
- **Hover highlight** is drawn in a second pass *after* all bay fills, so an adjacent bay can't paint over the highlight border.
- **3D projection** in `Canvas3D.tsx`: `screenX = ox + (wx·cosAz − wy·sinAz)·s`, `screenY = oy − (ry·sinEl + wz·cosEl)·s`. Painter sort is **descending** by depth (larger depth = farther = drawn first).

## Q calculation must match the solver

`scoring.ts:computeMetrics` mirrors `solver/solver.cpp:quality()` exactly:

```
Q = (Σprice / max(1, Σloads)) ^ (2 - bay_area / available_warehouse_area)
```

The denominator is the **available** warehouse area: polygon area (shoelace) **minus** the sum of obstacle areas. Both `App.tsx` and `MetricsPanel.tsx` call `computeMetrics` with `warehouseCase.obstacles` to keep this in sync. If you change one side of the formula, change the other.

## Theming

CSS variables in `src/index.css` define the colour tokens (`--color-bg`, `--color-fg`, `--color-accent`, etc.). The header has a sun/moon button that toggles between `data-theme="dark"` and `data-theme="light"` on `<html>`; the CSS variables update accordingly. Inline styles read the variables, so the whole app re-themes without React re-renders.

## Keyboard shortcuts

| Key | Action |
|---|---|
| `R` | Fit canvas to polygon |
| `2` / `3` | Switch to 2D / 3D view |
| `L` | Toggle bay labels |
| `G` | Toggle gap zones |
| `C` | Toggle ceiling overlay |

## Common pitfalls

- **Solver returns 500 instantly** — the bridge couldn't compile or run the solver. Check the `dev:server` terminal for the error. If you edited `solver.cpp` and broke compilation, the server fails to start.
- **Q in the UI doesn't match the solver's stdout** — most likely you broke the obstacle-subtraction in `scoring.ts` or stopped passing `warehouseCase.obstacles`. Diff against `solver/solver.cpp:quality()`.
- **Canvas looks blurry** — the HiDPI setup in the resize observer must run before the first draw; resizing during a frame can desync. Force a re-render via the `canvasSize` state.
- **Tailwind classes silently no-op** — use inline `style={{ … }}` for layout/spacing/colour. Only the structural classes work reliably here.
