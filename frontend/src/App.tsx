import { useState } from 'react'
import { Circle, CheckCircle, Loader2, XCircle } from 'lucide-react'
import type { WarehouseCase, Solution } from './types'

export default function App() {
  const [warehouseCase, setWarehouseCase] = useState<WarehouseCase | null>(null)
  const [solution, setSolution] = useState<Solution | null>(null)
  const [isRunning, setIsRunning] = useState(false)
  const [showCeiling, setShowCeiling] = useState(false)
  const [showLabels, setShowLabels] = useState(true)

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
          {/* FileLoader will go here — T4 */}
          <div className="flex-1 flex items-center justify-center p-4">
            <span style={{ color: 'var(--color-muted)', fontSize: 13 }}>Load CSVs (T4)</span>
          </div>

          {/* Controls — T13 */}
          <div style={{ padding: 16, flexShrink: 0, borderTop: '1px solid var(--color-border)' }}>
            <button
              disabled={!warehouseCase || isRunning}
              style={{
                width: '100%',
                padding: '8px 0',
                borderRadius: 6,
                border: 'none',
                fontFamily: 'var(--font-ui)',
                fontSize: 14,
                fontWeight: 500,
                transition: 'opacity 0.15s, background 0.15s',
                background: warehouseCase && !isRunning ? 'var(--color-accent)' : 'var(--color-border)',
                color: warehouseCase && !isRunning ? '#000' : 'var(--color-muted)',
                cursor: warehouseCase && !isRunning ? 'pointer' : 'not-allowed',
                opacity: !warehouseCase && !isRunning ? 0.5 : 1,
              }}
            >
              {isRunning ? 'Running…' : 'Run Solver'}
            </button>
          </div>
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
          <div style={{ padding: '16px', borderBottom: '1px solid var(--color-border)' }}>
            <span style={{ color: 'var(--color-muted)', fontSize: 13 }}>Metrics (T11)</span>
          </div>
          <div style={{ padding: '16px', flex: 1 }}>
            <span style={{ color: 'var(--color-muted)', fontSize: 13 }}>Legend (T12)</span>
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
