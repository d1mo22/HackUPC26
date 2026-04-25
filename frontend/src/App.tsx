import { useState } from 'react'
import { Circle, CheckCircle, Loader2, XCircle } from 'lucide-react'
import FileLoader, { type RawFiles } from './components/FileLoader'
import MetricsPanel from './components/MetricsPanel'
import BayLegend from './components/BayLegend'
import Controls from './components/Controls'
import type { WarehouseCase, Solution } from './types'

const MOCK_CASE: WarehouseCase = {
  polygon: [{ x: 0, y: 0 }, { x: 10000, y: 0 }, { x: 10000, y: 10000 }, { x: 0, y: 10000 }],
  obstacles: [],
  ceiling: [{ x: 0, h: 3000 }],
  bayTypes: [
    { id: 0, w: 800,  d: 1200, h: 2800, gap: 200, loads: 4,  price: 2000 },
    { id: 1, w: 1600, d: 1200, h: 2800, gap: 200, loads: 8,  price: 2500 },
    { id: 5, w: 2400, d: 1000, h: 1800, gap: 150, loads: 9,  price: 2600 },
    { id: 3, w: 800,  d: 1000, h: 1800, gap: 150, loads: 3,  price: 1800 },
  ],
}

const MOCK_SOLUTION: Solution = {
  placements: [
    { id: 5, x: 1700, y: 4200, rotation: 1 },
    { id: 5, x: 1700, y: 6600, rotation: 1 },
    { id: 3, x: 1700, y: 9000, rotation: 1 },
    { id: 1, x: 1500, y: 750,  rotation: 1 },
    { id: 0, x: 1500, y: 3150, rotation: 1 },
    { id: 5, x: 2900, y: 750,  rotation: 0 },
    { id: 3, x: 2900, y: 1900, rotation: 1 },
    { id: 3, x: 4050, y: 1900, rotation: 1 },
    { id: 5, x: 5300, y: 750,  rotation: 0 },
    { id: 3, x: 5300, y: 1900, rotation: 1 },
    { id: 3, x: 6450, y: 1900, rotation: 1 },
    { id: 1, x: 7700, y: 750,  rotation: 0 },
  ],
}

export default function App() {
  const [warehouseCase, setWarehouseCase] = useState<WarehouseCase | null>(MOCK_CASE)
  const [rawFiles, setRawFiles] = useState<RawFiles | null>(null)
  const [solution, setSolution] = useState<Solution | null>(MOCK_SOLUTION)
  const [isRunning, setIsRunning] = useState(false)
  const [showCeiling, setShowCeiling] = useState(false)
  const [showLabels, setShowLabels] = useState(true)

  function handleCaseLoaded(wc: WarehouseCase, files: RawFiles) {
    setWarehouseCase(wc)
    setRawFiles(files)
    setSolution(null)
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
            showCeiling={showCeiling}
            showLabels={showLabels}
            onRun={() => {}}
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
          className="flex flex-col shrink-0 overflow-y-auto"
          style={{ width: 280, background: 'var(--color-card)', borderLeft: '1px solid var(--color-border)' }}
        >
          <div style={{ borderBottom: '1px solid var(--color-border)' }}>
            <MetricsPanel solution={solution} warehouseCase={warehouseCase} />
          </div>
          <div style={{ flex: 1, overflowY: 'auto' }}>
            <BayLegend solution={solution} warehouseCase={warehouseCase} />
          </div>
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
