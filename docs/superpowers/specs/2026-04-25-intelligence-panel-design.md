# Intelligence Panel — Design Spec

**Date:** 2026-04-25  
**Owner:** David (frontend)  
**Status:** Approved

---

## Overview

The right panel gains a 3-tab structure replacing the single Metrics view. Tabs: **Metrics** (existing), **Breakdown** (new), **History** (new). The Controls panel gains an **Export CSV** button. No canvas changes required — History uses restore-on-canvas (Option A).

---

## Architecture

The right panel component (`RightPanel` or lifted into `App.tsx`) holds a `activeTab: 'metrics' | 'breakdown' | 'history'` state. Tab headers are rendered at the top; the active tab's content fills the body below.

Run history is stored in `App.tsx` as `runHistory: RunRecord[]`, appended every time `useSolver` returns a solution. A `RunRecord` holds the full `Solution` plus the computed metrics snapshot (Q, coverage, bay count, timestamp). Clicking "restore" in History calls `setSolution(record.solution)` — no canvas plumbing needed beyond what already exists.

---

## Data Shapes

```ts
interface RunRecord {
  id: number              // auto-increment
  solution: Solution
  metrics: {
    q: number
    coveragePct: number
    bayCount: number
  }
  timestamp: Date
}
```

`runHistory` lives in `App.tsx` state. It is never persisted (in-memory only — page refresh clears it, which is fine for a hackathon demo).

---

## Tab 1 — Metrics (no changes)

Existing `MetricsPanel` component, unchanged.

---

## Tab 2 — Breakdown

Rendered only when `warehouseCase` and `solution` are available. Shows a list of bay types present in the current solution, sorted by `price/loads` ratio descending.

**Per row:**
- Color swatch (matching `BayLegend` colors)
- Type ID + count in solution
- `price/loads` ratio value (2 decimal places, JetBrains Mono)
- Horizontal bar: width proportional to ratio relative to max ratio in the set; best type = `--color-accent` green, worst = `--color-destructive` red, others = `#3B82F6`

**Tip card:** Below the bars, a single sentence: "Replace Type X with Type Y to improve Q" — where X is the worst ratio type present and Y is the best ratio type in `warehouseCase.bayTypes` (whether or not it's currently used).

If only one bay type is present, omit the tip card.

---

## Tab 3 — History

List of `RunRecord[]`, newest first (or sorted best-Q first — best-Q first is preferred for demo clarity). Each row:
- Q score (bold, `--color-accent` if active, `--color-fg` otherwise)
- Bay count · coverage% · elapsed time (formatted as `mm:ss`)
- "active" label on the current solution; "restore" label (muted) on all others

**Restore interaction:** clicking any non-active row calls `onRestore(record.solution)` which calls `setSolution` in App.tsx and switches to the Metrics tab. The restored record becomes "active".

Empty state: "Run the solver to see history here."

---

## Export CSV

A secondary button below **Run Solver** in the Controls panel. Label: "↓ Export CSV". Enabled only when a solution is loaded.

Behavior: generates a CSV string from `solution.placements` (`Id,X,Y,Rotation` columns), creates a Blob, triggers a download as `solution.csv`. Pure client-side — no server call needed.

Disabled state: same visual treatment as other disabled controls (muted color, `cursor: not-allowed`).

---

## Component Breakdown

| Component | File | Change |
|-----------|------|--------|
| `RightPanel` | `src/components/RightPanel.tsx` | New — wraps tab headers + active tab content |
| `BreakdownTab` | `src/components/BreakdownTab.tsx` | New |
| `HistoryTab` | `src/components/HistoryTab.tsx` | New |
| `MetricsPanel` | existing | No changes |
| `Controls` | existing | Add Export CSV button + `onExport` prop |
| `App.tsx` | existing | Add `runHistory`, `activeTab`, wire restore + export |

`RightPanel` receives: `warehouseCase`, `solution`, `metrics`, `runHistory`, `activeTab`, `onTabChange`, `onRestore`.

---

## Out of Scope

- Split-canvas comparison view (deferred — needs Ferran's canvas to be prop-driven)
- Persisting history across page refreshes
- Export PNG (can add later, blocked on canvas stability)
- Animated Q score timeline chart in History tab
