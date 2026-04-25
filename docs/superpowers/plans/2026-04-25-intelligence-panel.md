# Intelligence Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single Metrics view in the right panel with a 3-tab panel (Metrics · Breakdown · History) and add an Export CSV button to Controls.

**Architecture:** `RightPanel` owns `activeTab` state and composes the three tab components. `App.tsx` owns `runHistory: RunRecord[]` and `activeRunId`, appending a new record each time the solver returns a solution. Restore writes back to `solution` state and switches to the Metrics tab. All styling via inline styles — no Tailwind utility classes for layout/spacing/color.

**Tech Stack:** React 18, TypeScript, Vite, inline styles, CSS variables (`--color-*`, `--font-*`), lucide-react icons, `getBayColor` from `BayLegend.tsx`.

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/types.ts` | Modify | Add `RunRecord` interface |
| `src/components/RightPanel.tsx` | Create | Tab header bar + renders active tab |
| `src/components/BreakdownTab.tsx` | Create | price/loads bar chart per bay type + tip card |
| `src/components/HistoryTab.tsx` | Create | List of past runs, restore interaction |
| `src/components/Controls.tsx` | Modify | Add `onExport` prop + Export CSV button |
| `src/App.tsx` | Modify | `runHistory`, `activeRunId`, solver timing, wire restore + export |

---

## Task 1: Add RunRecord to types.ts

**Files:**
- Modify: `src/types.ts`

- [ ] **Step 1: Add the RunRecord interface**

In `src/types.ts`, append after the `Solution` interface:

```ts
export interface RunRecord {
  id: number
  solution: Solution
  metrics: {
    q: number
    coveragePct: number
    bayCount: number
  }
  elapsedMs: number
  timestamp: Date
}
```

- [ ] **Step 2: Verify TypeScript compiles**

```bash
cd /Users/david.morais/HackUPC26/frontend && npx tsc --noEmit
```
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/types.ts
git commit -m "feat: add RunRecord type for solver history"
```

---

## Task 2: Create BreakdownTab

**Files:**
- Create: `src/components/BreakdownTab.tsx`

- [ ] **Step 1: Create the file**

```tsx
import { getBayColor } from './BayLegend'
import type { Solution, WarehouseCase } from '../types'

interface Props {
  solution: Solution
  warehouseCase: WarehouseCase
}

export default function BreakdownTab({ solution, warehouseCase }: Props) {
  const typeMap = new Map(warehouseCase.bayTypes.map(t => [t.id, t]))

  // Count placements per type
  const counts = new Map<number, number>()
  for (const p of solution.placements) {
    counts.set(p.id, (counts.get(p.id) ?? 0) + 1)
  }

  // Build rows: only types present in solution, sorted by price/loads desc
  const rows = [...counts.keys()]
    .map(id => {
      const type = typeMap.get(id)!
      return { id, type, count: counts.get(id)!, ratio: type.price / type.loads }
    })
    .sort((a, b) => b.ratio - a.ratio)

  if (rows.length === 0) return (
    <div style={{ padding: 16, color: 'var(--color-muted)', fontSize: 13 }}>No solution loaded.</div>
  )

  const maxRatio = rows[0].ratio
  const bestId = rows[0].id
  const worstId = rows[rows.length - 1].id

  // Best available type across all bay types (not just used ones)
  const bestAvailable = warehouseCase.bayTypes
    .map(t => ({ id: t.id, ratio: t.price / t.loads }))
    .sort((a, b) => b.ratio - a.ratio)[0]

  const showTip = rows.length > 1 && bestAvailable.id !== worstId

  return (
    <div style={{ padding: 16 }}>
      <p style={{
        color: 'var(--color-muted)',
        fontFamily: 'var(--font-mono)',
        fontSize: 11,
        textTransform: 'uppercase',
        letterSpacing: '0.08em',
        marginBottom: 14,
      }}>
        Price / Loads
      </p>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
        {rows.map(({ id, type, count, ratio }) => {
          const color = id === bestId
            ? 'var(--color-accent)'
            : id === worstId
            ? 'var(--color-destructive)'
            : '#3B82F6'
          const barWidth = `${Math.round((ratio / maxRatio) * 100)}%`
          const label = id === bestId ? '★ best' : id === worstId ? '↓ worst' : null

          return (
            <div key={id}>
              <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 4 }}>
                <div style={{ display: 'flex', alignItems: 'center', gap: 6 }}>
                  <div style={{
                    width: 8, height: 8, borderRadius: 2,
                    background: getBayColor(id), flexShrink: 0,
                  }} />
                  <span style={{ fontFamily: 'var(--font-mono)', fontSize: 12, color: 'var(--color-fg)' }}>
                    Type {id}
                  </span>
                  <span style={{ fontFamily: 'var(--font-mono)', fontSize: 11, color: 'var(--color-muted)' }}>
                    ×{count}
                  </span>
                  {label && (
                    <span style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color }}>
                      {label}
                    </span>
                  )}
                </div>
                <span style={{ fontFamily: 'var(--font-mono)', fontSize: 12, color: 'var(--color-fg)' }}>
                  {ratio.toFixed(2)}
                </span>
              </div>
              <div style={{ background: 'var(--color-border)', borderRadius: 2, height: 6 }}>
                <div style={{
                  width: barWidth,
                  height: 6,
                  borderRadius: 2,
                  background: color,
                  transition: 'width 0.4s ease',
                }} />
              </div>
            </div>
          )
        })}
      </div>

      {showTip && (
        <div style={{
          marginTop: 14,
          padding: '8px 10px',
          background: 'rgba(34,197,94,0.06)',
          border: '1px solid rgba(34,197,94,0.2)',
          borderRadius: 6,
          borderLeft: '3px solid var(--color-accent)',
        }}>
          <div style={{ color: 'var(--color-muted)', fontSize: 10, fontFamily: 'var(--font-mono)', marginBottom: 3 }}>
            TIP
          </div>
          <div style={{ color: 'var(--color-fg)', fontSize: 12, fontFamily: 'var(--font-ui)' }}>
            Replace Type {worstId} with Type {bestAvailable.id} to improve Q
          </div>
        </div>
      )}
    </div>
  )
}
```

- [ ] **Step 2: Verify TypeScript compiles**

```bash
cd /Users/david.morais/HackUPC26/frontend && npx tsc --noEmit
```
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/BreakdownTab.tsx
git commit -m "feat: add BreakdownTab with price/loads bar chart"
```

---

## Task 3: Create HistoryTab

**Files:**
- Create: `src/components/HistoryTab.tsx`

- [ ] **Step 1: Create the file**

```tsx
import type { RunRecord, Solution } from '../types'

interface Props {
  history: RunRecord[]
  activeRunId: number | null
  onRestore: (solution: Solution, id: number) => void
}

function formatElapsed(ms: number): string {
  const totalSec = Math.floor(ms / 1000)
  const m = Math.floor(totalSec / 60)
  const s = totalSec % 60
  return `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`
}

export default function HistoryTab({ history, activeRunId, onRestore }: Props) {
  if (history.length === 0) {
    return (
      <div style={{ padding: 16 }}>
        <p style={{
          color: 'var(--color-muted)',
          fontFamily: 'var(--font-mono)',
          fontSize: 11,
          textTransform: 'uppercase',
          letterSpacing: '0.08em',
          marginBottom: 14,
        }}>
          History
        </p>
        <p style={{ color: 'var(--color-muted)', fontSize: 13, fontFamily: 'var(--font-ui)' }}>
          Run the solver to see history here.
        </p>
      </div>
    )
  }

  // Sort best Q first
  const sorted = [...history].sort((a, b) => a.metrics.q - b.metrics.q)

  return (
    <div style={{ padding: 16 }}>
      <p style={{
        color: 'var(--color-muted)',
        fontFamily: 'var(--font-mono)',
        fontSize: 11,
        textTransform: 'uppercase',
        letterSpacing: '0.08em',
        marginBottom: 14,
      }}>
        History
      </p>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        {sorted.map(record => {
          const isActive = record.id === activeRunId
          return (
            <div
              key={record.id}
              role={isActive ? undefined : 'button'}
              tabIndex={isActive ? undefined : 0}
              onClick={isActive ? undefined : () => onRestore(record.solution, record.id)}
              onKeyDown={isActive ? undefined : (e) => {
                if (e.key === 'Enter' || e.key === ' ') {
                  e.preventDefault()
                  onRestore(record.solution, record.id)
                }
              }}
              style={{
                background: isActive ? 'rgba(34,197,94,0.08)' : 'var(--color-bg)',
                border: `1px solid ${isActive ? 'rgba(34,197,94,0.4)' : 'var(--color-border)'}`,
                borderRadius: 6,
                padding: '8px 10px',
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                cursor: isActive ? 'default' : 'pointer',
                outline: 'none',
                transition: 'border-color 0.15s',
              }}
            >
              <div>
                <div style={{
                  fontFamily: 'var(--font-mono)',
                  fontSize: 12,
                  fontWeight: 600,
                  color: isActive ? 'var(--color-accent)' : 'var(--color-fg)',
                }}>
                  Q {record.metrics.q.toLocaleString('de-DE', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
                </div>
                <div style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--color-muted)', marginTop: 2 }}>
                  {record.metrics.bayCount} bays · {record.metrics.coveragePct.toFixed(1)}% · {formatElapsed(record.elapsedMs)}
                </div>
              </div>
              <span style={{
                fontFamily: 'var(--font-mono)',
                fontSize: 10,
                color: isActive ? 'var(--color-accent)' : 'var(--color-muted)',
              }}>
                {isActive ? 'active' : 'restore'}
              </span>
            </div>
          )
        })}
      </div>
    </div>
  )
}
```

- [ ] **Step 2: Verify TypeScript compiles**

```bash
cd /Users/david.morais/HackUPC26/frontend && npx tsc --noEmit
```
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/HistoryTab.tsx
git commit -m "feat: add HistoryTab with restore interaction"
```

---

## Task 4: Create RightPanel

**Files:**
- Create: `src/components/RightPanel.tsx`

- [ ] **Step 1: Create the file**

```tsx
import { useState } from 'react'
import MetricsPanel from './MetricsPanel'
import BreakdownTab from './BreakdownTab'
import HistoryTab from './HistoryTab'
import BayLegend from './BayLegend'
import type { RunRecord, Solution, WarehouseCase } from '../types'

type Tab = 'metrics' | 'breakdown' | 'history'

interface Props {
  solution: Solution | null
  warehouseCase: WarehouseCase | null
  runHistory: RunRecord[]
  activeRunId: number | null
  onRestore: (solution: Solution, id: number) => void
}

const TABS: { id: Tab; label: string }[] = [
  { id: 'metrics',   label: 'Metrics'   },
  { id: 'breakdown', label: 'Breakdown' },
  { id: 'history',   label: 'History'   },
]

export default function RightPanel({ solution, warehouseCase, runHistory, activeRunId, onRestore }: Props) {
  const [activeTab, setActiveTab] = useState<Tab>('metrics')

  function handleRestore(sol: Solution, id: number) {
    onRestore(sol, id)
    setActiveTab('metrics')
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>

      {/* Tab header */}
      <div style={{
        display: 'flex',
        borderBottom: '1px solid var(--color-border)',
        flexShrink: 0,
      }}>
        {TABS.map(tab => (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id)}
            style={{
              flex: 1,
              padding: '10px 0',
              background: 'none',
              border: 'none',
              borderBottom: activeTab === tab.id ? '2px solid var(--color-accent)' : '2px solid transparent',
              color: activeTab === tab.id ? 'var(--color-fg)' : 'var(--color-muted)',
              fontFamily: 'var(--font-mono)',
              fontSize: 11,
              cursor: 'pointer',
              transition: 'color 0.15s, border-color 0.15s',
              marginBottom: -1,
            }}
          >
            {tab.label}
          </button>
        ))}
      </div>

      {/* Tab content */}
      <div style={{ flex: 1, overflowY: 'auto' }}>
        {activeTab === 'metrics' && (
          <>
            <MetricsPanel solution={solution} warehouseCase={warehouseCase} />
            <div style={{ borderTop: '1px solid var(--color-border)' }}>
              <BayLegend solution={solution} warehouseCase={warehouseCase} />
            </div>
          </>
        )}
        {activeTab === 'breakdown' && solution && warehouseCase && (
          <BreakdownTab solution={solution} warehouseCase={warehouseCase} />
        )}
        {activeTab === 'breakdown' && !(solution && warehouseCase) && (
          <div style={{ padding: 16, color: 'var(--color-muted)', fontSize: 13, fontFamily: 'var(--font-ui)' }}>
            Load files and run the solver first.
          </div>
        )}
        {activeTab === 'history' && (
          <HistoryTab
            history={runHistory}
            activeRunId={activeRunId}
            onRestore={handleRestore}
          />
        )}
      </div>
    </div>
  )
}
```

- [ ] **Step 2: Verify TypeScript compiles**

```bash
cd /Users/david.morais/HackUPC26/frontend && npx tsc --noEmit
```
Expected: no errors.

- [ ] **Step 3: Commit**

```bash
git add src/components/RightPanel.tsx
git commit -m "feat: add RightPanel with 3-tab structure"
```

---

## Task 5: Add Export CSV to Controls

**Files:**
- Modify: `src/components/Controls.tsx`

- [ ] **Step 1: Add `onExport` prop and Export CSV button**

Replace the `Props` interface and add the button. Full updated file:

```tsx
import { Layers, Tag } from 'lucide-react'

interface Props {
  isReady: boolean
  isRunning: boolean
  hasSolution: boolean
  showCeiling: boolean
  showLabels: boolean
  onRun: () => void
  onExport: () => void
  onToggleCeiling: () => void
  onToggleLabels: () => void
}

export default function Controls({
  isReady,
  isRunning,
  hasSolution,
  showCeiling,
  showLabels,
  onRun,
  onExport,
  onToggleCeiling,
  onToggleLabels,
}: Props) {
  return (
    <div style={{ padding: 16, flexShrink: 0, borderTop: '1px solid var(--color-border)', display: 'flex', flexDirection: 'column', gap: 10 }}>

      <Toggle
        icon={<Layers size={13} aria-hidden />}
        label="Ceiling overlay"
        enabled={showCeiling}
        onToggle={onToggleCeiling}
      />
      <Toggle
        icon={<Tag size={13} aria-hidden />}
        label="Bay labels"
        enabled={showLabels}
        onToggle={onToggleLabels}
      />

      <button
        onClick={onRun}
        disabled={!isReady || isRunning}
        style={{
          marginTop: 4,
          width: '100%',
          padding: '8px 0',
          borderRadius: 6,
          border: 'none',
          fontFamily: 'var(--font-ui)',
          fontSize: 14,
          fontWeight: 500,
          transition: 'opacity 0.15s, background 0.15s',
          background: isReady && !isRunning ? 'var(--color-accent)' : 'var(--color-border)',
          color: isReady && !isRunning ? '#000' : 'var(--color-muted)',
          cursor: isReady && !isRunning ? 'pointer' : 'not-allowed',
          opacity: !isReady && !isRunning ? 0.5 : 1,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          gap: 8,
        }}
      >
        {isRunning && (
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ animation: 'spin 1s linear infinite' }}>
            <path d="M21 12a9 9 0 1 1-6.219-8.56" />
          </svg>
        )}
        {isRunning ? 'Running…' : 'Run Solver'}
      </button>

      <button
        onClick={onExport}
        disabled={!hasSolution}
        style={{
          width: '100%',
          padding: '8px 0',
          borderRadius: 6,
          border: '1px solid var(--color-border)',
          background: 'transparent',
          fontFamily: 'var(--font-ui)',
          fontSize: 13,
          color: hasSolution ? 'var(--color-muted)' : 'var(--color-border)',
          cursor: hasSolution ? 'pointer' : 'not-allowed',
          transition: 'color 0.15s, border-color 0.15s',
        }}
      >
        ↓ Export CSV
      </button>

      <style>{`@keyframes spin { from { transform: rotate(0deg) } to { transform: rotate(360deg) } }`}</style>
    </div>
  )
}

function Toggle({
  icon,
  label,
  enabled,
  onToggle,
}: {
  icon: React.ReactNode
  label: string
  enabled: boolean
  onToggle: () => void
}) {
  return (
    <button
      role="switch"
      aria-checked={enabled}
      onClick={onToggle}
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        width: '100%',
        background: 'none',
        border: 'none',
        cursor: 'pointer',
        padding: '2px 0',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, color: 'var(--color-muted)' }}>
        {icon}
        <span style={{ fontSize: 13, fontFamily: 'var(--font-ui)', color: 'var(--color-muted)' }}>{label}</span>
      </div>

      <div style={{
        width: 32,
        height: 18,
        borderRadius: 9999,
        background: enabled ? 'var(--color-accent)' : 'var(--color-border)',
        transition: 'background 0.2s',
        position: 'relative',
        flexShrink: 0,
      }}>
        <div style={{
          width: 12,
          height: 12,
          borderRadius: 9999,
          background: '#fff',
          position: 'absolute',
          top: 3,
          left: enabled ? 17 : 3,
          transition: 'left 0.2s',
        }} />
      </div>
    </button>
  )
}
```

- [ ] **Step 2: Verify TypeScript compiles**

```bash
cd /Users/david.morais/HackUPC26/frontend && npx tsc --noEmit
```
Expected: error on App.tsx because `hasSolution` and `onExport` props are now required — that's expected and fixed in Task 6.

- [ ] **Step 3: Commit**

```bash
git add src/components/Controls.tsx
git commit -m "feat: add Export CSV button to Controls"
```

---

## Task 6: Wire everything in App.tsx

**Files:**
- Modify: `src/App.tsx`

- [ ] **Step 1: Replace App.tsx with the wired version**

```tsx
import { useRef, useState } from 'react'
import { Circle, CheckCircle, Loader2, XCircle } from 'lucide-react'
import FileLoader, { type RawFiles } from './components/FileLoader'
import RightPanel from './components/RightPanel'
import Controls from './components/Controls'
import { computeMetrics } from './lib/scoring'
import { parseSolution } from './lib/csvParser'
import type { RunRecord, WarehouseCase, Solution } from './types'

let nextRunId = 1

export default function App() {
  const [warehouseCase, setWarehouseCase] = useState<WarehouseCase | null>(null)
  const [rawFiles, setRawFiles] = useState<RawFiles | null>(null)
  const [solution, setSolution] = useState<Solution | null>(null)
  const [isRunning, setIsRunning] = useState(false)
  const [showCeiling, setShowCeiling] = useState(false)
  const [showLabels, setShowLabels] = useState(true)
  const [runHistory, setRunHistory] = useState<RunRecord[]>([])
  const [activeRunId, setActiveRunId] = useState<number | null>(null)
  const solverStartRef = useRef<number>(0)

  function handleCaseLoaded(wc: WarehouseCase, files: RawFiles) {
    setWarehouseCase(wc)
    setRawFiles(files)
    setSolution(null)
  }

  async function handleRun() {
    if (!rawFiles || !warehouseCase) return
    setIsRunning(true)
    solverStartRef.current = Date.now()
    try {
      const formData = new FormData()
      formData.append('warehouse', rawFiles.warehouse)
      formData.append('obstacles', rawFiles.obstacles)
      formData.append('ceiling', rawFiles.ceiling)
      formData.append('types', rawFiles.types)
      const res = await fetch('/solve', { method: 'POST', body: formData })
      if (!res.ok) throw new Error(`Server error: ${res.status}`)
      const text = await res.text()
      const blob = new Blob([text], { type: 'text/csv' })
      const file = new File([blob], 'solution.csv')
      const placements = await parseSolution(file)
      const sol: Solution = { placements }
      const elapsedMs = Date.now() - solverStartRef.current
      const metrics = computeMetrics(placements, warehouseCase.bayTypes, warehouseCase.polygon)
      const record: RunRecord = {
        id: nextRunId++,
        solution: sol,
        metrics: { q: metrics.q, coveragePct: metrics.coveragePct, bayCount: metrics.bayCount },
        elapsedMs,
        timestamp: new Date(),
      }
      setSolution(sol)
      setRunHistory(prev => [...prev, record])
      setActiveRunId(record.id)
    } catch (err) {
      console.error('Solver failed:', err)
    } finally {
      setIsRunning(false)
    }
  }

  function handleRestore(sol: Solution, id: number) {
    setSolution(sol)
    setActiveRunId(id)
  }

  function handleExport() {
    if (!solution) return
    const rows = ['Id,X,Y,Rotation', ...solution.placements.map(p => `${p.id},${p.x},${p.y},${p.rotation}`)]
    const blob = new Blob([rows.join('\n')], { type: 'text/csv' })
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = 'solution.csv'
    a.click()
    URL.revokeObjectURL(url)
  }

  const status: 'idle' | 'ready' | 'running' | 'error' =
    isRunning ? 'running' : warehouseCase ? 'ready' : 'idle'

  return (
    <div className="flex flex-col w-screen h-screen overflow-hidden" style={{ background: 'var(--color-bg)', color: 'var(--color-fg)' }}>

      {/* Topbar */}
      <header
        className="flex items-center justify-between shrink-0"
        style={{ height: 48, padding: '0 20px', background: 'var(--color-card)', borderBottom: '1px solid var(--color-border)' }}
      >
        <span style={{ fontFamily: 'var(--font-mono)', fontWeight: 700, fontSize: 15, letterSpacing: '0.05em' }}>
          Warehouse Optimizer
        </span>
        <StatusBadge status={status} />
      </header>

      {/* Main 3-column layout */}
      <div className="flex flex-1 overflow-hidden">

        {/* Left panel */}
        <aside
          className="flex flex-col shrink-0 overflow-y-auto"
          style={{ width: 260, background: 'var(--color-card)', borderRight: '1px solid var(--color-border)' }}
        >
          <div style={{ flex: 1, overflowY: 'auto' }}>
            <FileLoader
              onCaseLoaded={handleCaseLoaded}
              onSolutionLoaded={(s) => setSolution(s)}
            />
          </div>

          <Controls
            isReady={!!warehouseCase}
            isRunning={isRunning}
            hasSolution={!!solution}
            showCeiling={showCeiling}
            showLabels={showLabels}
            onRun={handleRun}
            onExport={handleExport}
            onToggleCeiling={() => setShowCeiling(v => !v)}
            onToggleLabels={() => setShowLabels(v => !v)}
          />
        </aside>

        {/* Canvas center */}
        <main
          className="flex-1 overflow-hidden flex flex-col items-center justify-center gap-2"
          style={{ background: 'var(--color-bg)' }}
        >
          <span style={{ color: 'var(--color-muted)', fontSize: 14 }}>Canvas (T7)</span>
          {!warehouseCase && (
            <span style={{ color: 'var(--color-muted)', fontSize: 12 }}>
              Load the 4 CSV files in the left panel to begin
            </span>
          )}
        </main>

        {/* Right panel */}
        <aside
          className="flex flex-col shrink-0 overflow-hidden"
          style={{ width: 280, background: 'var(--color-card)', borderLeft: '1px solid var(--color-border)' }}
        >
          <RightPanel
            solution={solution}
            warehouseCase={warehouseCase}
            runHistory={runHistory}
            activeRunId={activeRunId}
            onRestore={handleRestore}
          />
        </aside>

      </div>
    </div>
  )
}

function StatusBadge({ status }: { status: 'idle' | 'ready' | 'running' | 'error' }) {
  const config = {
    idle:    { label: 'No data',  color: 'var(--color-muted)',       bg: 'rgba(148,163,184,0.1)', Icon: Circle },
    ready:   { label: 'Ready',    color: 'var(--color-accent)',      bg: 'rgba(34,197,94,0.1)',   Icon: CheckCircle },
    running: { label: 'Running…', color: '#FBBF24',                  bg: 'rgba(251,191,36,0.1)',  Icon: Loader2 },
    error:   { label: 'Error',    color: 'var(--color-destructive)', bg: 'rgba(239,68,68,0.1)',   Icon: XCircle },
  }[status]

  const { Icon } = config

  return (
    <span
      className="flex items-center"
      style={{
        fontFamily: 'var(--font-mono)',
        fontSize: 12,
        color: config.color,
        background: config.bg,
        border: `1px solid ${config.color}`,
        borderRadius: 9999,
        padding: '3px 10px',
        gap: 6,
      }}
      role="status"
      aria-label={`Status: ${config.label}`}
    >
      <Icon size={11} className={status === 'running' ? 'animate-spin' : ''} aria-hidden />
      {config.label}
    </span>
  )
}
```

- [ ] **Step 2: Verify TypeScript compiles cleanly**

```bash
cd /Users/david.morais/HackUPC26/frontend && npx tsc --noEmit
```
Expected: no errors.

- [ ] **Step 3: Start dev server and verify in browser**

```bash
cd /Users/david.morais/HackUPC26/frontend && npm run dev
```

Check:
- Right panel shows 3 tabs: Metrics · Breakdown · History
- Metrics tab renders MetricsPanel + BayLegend (same as before)
- Breakdown tab shows "Load files and run the solver first." before data is loaded
- History tab shows "Run the solver to see history here."
- Controls shows Export CSV button (disabled/muted when no solution)
- Run Solver button is wired (clicking it attempts the fetch — server may not be running, that's OK)

- [ ] **Step 4: Commit**

```bash
git add src/App.tsx
git commit -m "feat: wire Intelligence Panel — history, restore, export CSV"
```
