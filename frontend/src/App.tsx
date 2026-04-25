import { useRef, useState } from 'react'
import { Circle, CheckCircle, Loader2, XCircle } from 'lucide-react'
import FileLoader, { type RawFiles } from './components/FileLoader'
import RightPanel from './components/RightPanel'
import Controls from './components/Controls'
import { computeMetrics } from './lib/scoring'
import { parseSolution } from './lib/csvParser'
import type { RunRecord, WarehouseCase, Solution } from './types'
import WarehouseViewer from './components/WarehouseViewer'

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
        {/* <main
          className="flex-1 overflow-hidden flex flex-col items-center justify-center gap-2"
          style={{ background: 'var(--color-bg)' }}
        >
          <span style={{ color: 'var(--color-muted)', fontSize: 14 }}>Canvas (T7)</span>
          {!warehouseCase && (
            <span style={{ color: 'var(--color-muted)', fontSize: 12 }}>
              Load the 4 CSV files in the left panel to begin
            </span>
          )}
        </main> */}

        <main className="flex-1 relative bg-slate-50">
        {rawFiles ? (
          <WarehouseViewer 
            
          />
        ) : (
          <div className="h-full flex flex-col items-center justify-center text-muted-foreground p-10 text-center">
            <div className="mb-4 opacity-20">
              {/* Una icona gran de magatzem o fitxer */}
            </div>
            <p className="text-lg font-medium">No hi ha dades carregades</p>
            <p className="text-sm">Puja els fitxers CSV al menú de l'esquerra per visualitzar el magatzem.</p>
          </div>
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
