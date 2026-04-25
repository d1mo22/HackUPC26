import { useEffect, useRef, useState } from 'react'
import { Circle, CheckCircle, Loader2, XCircle, Sun, Moon } from 'lucide-react'
import FileLoader, { type RawFiles } from './components/FileLoader'
import RightPanel from './components/RightPanel'
import Controls from './components/Controls'
import Canvas from './components/Canvas'
import { computeMetrics } from './lib/scoring'
import { parseSolution } from './lib/csvParser'
import type { BayType, CeilingSegment, Obstacle, Point, RunRecord, WarehouseCase, Solution } from './types'

let nextRunId = 1

export default function App() {
  // Incremental per-layer state — each updates as soon as its file is loaded
  const [polygon,   setPolygon]   = useState<Point[]          | null>(null)
  const [obstacles, setObstacles] = useState<Obstacle[]       | null>(null)
  const [ceiling,   setCeiling]   = useState<CeilingSegment[] | null>(null)
  const [bayTypes,  setBayTypes]  = useState<BayType[]        | null>(null)

  // Derived full case (only set when all 4 files are loaded — needed for solver)
  const [warehouseCase, setWarehouseCase] = useState<WarehouseCase | null>(null)
  const [rawFiles, setRawFiles] = useState<RawFiles | null>(null)
  const [solution, setSolution] = useState<Solution | null>(null)
  const [isRunning, setIsRunning] = useState(false)
  const [showCeiling, setShowCeiling] = useState(false)
  const [showLabels, setShowLabels] = useState(true)
  const [showGaps, setShowGaps] = useState(false)
  const [runHistory, setRunHistory] = useState<RunRecord[]>([])
  const [activeRunId, setActiveRunId] = useState<number | null>(null)
  const [selectedTypeIds, setSelectedTypeIds] = useState<Set<number>>(new Set())
  const [canvasViewMode, setCanvasViewMode] = useState<'2d' | '3d' | undefined>(undefined)
  const [revealCount, setRevealCount] = useState<number | null>(null)
  const [theme, setTheme] = useState<'dark' | 'light'>('dark')
  const revealTimerRef = useRef<ReturnType<typeof setInterval> | null>(null)
  const solverStartRef = useRef<number>(0)

  // Apply theme to <html>
  useEffect(() => {
    document.documentElement.setAttribute('data-theme', theme)
  }, [theme])

  // Keyboard shortcuts
  useEffect(() => {
    function onKey(e: KeyboardEvent) {
      // Don't fire when typing in inputs
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement) return
      switch (e.key.toLowerCase()) {
        case 'r': fitCanvasRef.current?.(); break
        case 'l': setShowLabels(v => !v); break
        case 'g': setShowGaps(v => !v); break
        case 'c': setShowCeiling(v => !v); break
        case '2': setCanvasViewMode('2d'); break
        case '3': setCanvasViewMode('3d'); break
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])

  const fitCanvasRef = useRef<(() => void) | null>(null)
  const canvasElRef = useRef<HTMLCanvasElement | null>(null)

  function startRevealAnimation(total: number) {
    if (revealTimerRef.current) clearInterval(revealTimerRef.current)
    setRevealCount(0)
    let count = 0
    const batchSize = Math.max(1, Math.floor(total / 60)) // ~60 steps
    const intervalMs = Math.max(16, Math.floor(1200 / (total / batchSize)))
    revealTimerRef.current = setInterval(() => {
      count += batchSize
      if (count >= total) {
        setRevealCount(null) // null = show all
        clearInterval(revealTimerRef.current!)
        revealTimerRef.current = null
      } else {
        setRevealCount(count)
      }
    }, intervalMs)
  }

  function handlePartialLoad(data: { polygon?: Point[]; obstacles?: Obstacle[]; ceiling?: CeilingSegment[]; bayTypes?: BayType[] }) {
    if (data.polygon)   setPolygon(data.polygon)
    if (data.obstacles) setObstacles(data.obstacles)
    if (data.ceiling)   setCeiling(data.ceiling)
    if (data.bayTypes)  setBayTypes(data.bayTypes)
  }

  function handleCaseLoaded(wc: WarehouseCase, files: RawFiles) {
    setPolygon(wc.polygon)
    setObstacles(wc.obstacles)
    setCeiling(wc.ceiling)
    setBayTypes(wc.bayTypes)
    setWarehouseCase(wc)
    setRawFiles(files)
    setSolution(null)
    setSelectedTypeIds(new Set())
  }

  function handleToggleType(id: number) {
    setSelectedTypeIds(prev => {
      const next = new Set(prev)
      if (next.has(id)) next.delete(id); else next.add(id)
      return next
    })
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
      const placements = await parseSolution(file, warehouseCase.bayTypes)
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
      startRevealAnimation(placements.length)
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
    startRevealAnimation(sol.placements.length)
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

  function handleExportPng() {
    const canvas = canvasElRef.current
    if (!canvas) return
    const url = canvas.toDataURL('image/png')
    const a = document.createElement('a')
    a.href = url
    a.download = 'warehouse_layout.png'
    a.click()
  }

  const status: 'idle' | 'ready' | 'running' | 'error' =
    isRunning ? 'running' : warehouseCase ? 'ready' : polygon ? 'ready' : 'idle'

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
          <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
            <button
              onClick={() => setTheme(t => t === 'dark' ? 'light' : 'dark')}
              title={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}
              style={{
                background: 'none',
                border: '1px solid var(--color-border)',
                borderRadius: 6,
                color: 'var(--color-muted)',
                cursor: 'pointer',
                display: 'flex',
                alignItems: 'center',
                justifyContent: 'center',
                width: 30,
                height: 30,
              }}
            >
              {theme === 'dark' ? <Sun size={14} /> : <Moon size={14} />}
            </button>
            <StatusBadge status={status} />
          </div>
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
              onSolutionLoaded={(s) => { setSolution(s); startRevealAnimation(s.placements.length) }}
              onPartialLoad={handlePartialLoad}
              onClear={() => {
                setPolygon(null)
                setObstacles(null)
                setCeiling(null)
                setBayTypes(null)
                setWarehouseCase(null)
                setRawFiles(null)
                setSolution(null)
                setRunHistory([])
                setActiveRunId(null)
                setSelectedTypeIds(new Set())
                setCanvasViewMode('2d')
              }}
              bayTypes={bayTypes ?? undefined}
            />
          </div>

          <Controls
            isReady={!!warehouseCase}
            isRunning={isRunning}
            hasSolution={!!solution}
            showCeiling={showCeiling}
            showLabels={showLabels}
            showGaps={showGaps}
            onRun={handleRun}
            onExport={handleExport}
            onExportPng={handleExportPng}
            onToggleCeiling={() => setShowCeiling(v => !v)}
            onToggleLabels={() => setShowLabels(v => !v)}
            onToggleGaps={() => setShowGaps(v => !v)}
          />
        </aside>

        {/* Canvas center */}
        <main
          className="flex-1 overflow-hidden"
          style={{ background: 'var(--color-bg)' }}
        >
          <Canvas
            polygon={polygon}
            obstacles={obstacles}
            ceiling={ceiling}
            bayTypes={bayTypes}
            placements={revealCount !== null && solution ? solution.placements.slice(0, revealCount) : (solution?.placements ?? null)}
            showGaps={showGaps}
            showCeiling={showCeiling}
            showLabels={showLabels}
            selectedTypeIds={selectedTypeIds}
            viewModeOverride={canvasViewMode}
            onViewModeChange={setCanvasViewMode}
            onFitRef={(fn) => { fitCanvasRef.current = fn }}
            onCanvasRef={(el) => { canvasElRef.current = el }}
          />
        </main>

        {/* Right panel */}
        <aside
          className="flex flex-col shrink-0 overflow-hidden"
          style={{ width: 280, background: 'var(--color-card)', borderLeft: '1px solid var(--color-border)' }}
        >
          <RightPanel
            solution={solution}
            warehouseCase={warehouseCase}
            bayTypes={bayTypes}
            runHistory={runHistory}
            activeRunId={activeRunId}
            onRestore={handleRestore}
            selectedTypeIds={selectedTypeIds}
            onToggleType={handleToggleType}
            onClearFilter={() => setSelectedTypeIds(new Set())}
          />
        </aside>

      </div>
    </div>
  )
}

function StatusBadge({ status }: { status: 'idle' | 'ready' | 'running' | 'error' }) {
  const config = {
    idle: { label: 'No data', color: 'var(--color-muted)', bg: 'rgba(148,163,184,0.1)', Icon: Circle },
    ready: { label: 'Ready', color: 'var(--color-accent)', bg: 'rgba(34,197,94,0.1)', Icon: CheckCircle },
    running: { label: 'Running…', color: '#FBBF24', bg: 'rgba(251,191,36,0.1)', Icon: Loader2 },
    error: { label: 'Error', color: 'var(--color-destructive)', bg: 'rgba(239,68,68,0.1)', Icon: XCircle },
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
