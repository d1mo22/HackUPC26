import { useRef, useState } from 'react'
import { Check, AlertCircle, Loader2, Plus } from 'lucide-react'
import {
  parseWarehouse,
  parseObstacles,
  parseCeiling,
  parseBayTypes,
  parseSolution,
} from '../lib/csvParser'
import type { WarehouseCase, Solution } from '../types'

interface FileSlotState {
  file: File | null
  error: string | null
  loading: boolean
}

interface Props {
  onCaseLoaded: (wc: WarehouseCase, files: RawFiles) => void
  onSolutionLoaded: (s: Solution) => void
}

export interface RawFiles {
  warehouse: File
  obstacles: File
  ceiling: File
  types: File
}

const SLOTS = [
  { key: 'warehouse', label: 'warehouse' },
  { key: 'obstacles', label: 'obstacles' },
  { key: 'ceiling',   label: 'ceiling'   },
  { key: 'types',     label: 'types_of_bays' },
] as const

type SlotKey = typeof SLOTS[number]['key']

const PARSERS = {
  warehouse: parseWarehouse,
  obstacles: parseObstacles,
  ceiling:   parseCeiling,
  types:     parseBayTypes,
}

const ERROR_HINTS: Record<SlotKey, string> = {
  warehouse: 'Expected: x, y',
  obstacles: 'Expected: x, y, w, d',
  ceiling:   'Expected: x, h',
  types:     'Expected: id, w, d, h, gap, loads, price',
}

export default function FileLoader({ onCaseLoaded, onSolutionLoaded }: Props) {
  const [slots, setSlots] = useState<Record<SlotKey, FileSlotState>>({
    warehouse: { file: null, error: null, loading: false },
    obstacles: { file: null, error: null, loading: false },
    ceiling:   { file: null, error: null, loading: false },
    types:     { file: null, error: null, loading: false },
  })
  const [solutionSlot, setSolutionSlot] = useState<FileSlotState>({ file: null, error: null, loading: false })

  const allLoaded = SLOTS.every(({ key }) => slots[key].file !== null)

  async function handleFile(key: SlotKey, file: File) {
    setSlots(prev => ({ ...prev, [key]: { file: null, error: null, loading: true } }))
    try {
      await PARSERS[key](file)
      const next = { ...slots, [key]: { file, error: null, loading: false } }
      setSlots(next)

      const allDone = SLOTS.every(({ key: k }) => (k === key ? true : next[k].file !== null))
      if (allDone) {
        const rawFiles = {
          warehouse: (next.warehouse.file ?? file) as File,
          obstacles: (next.obstacles.file ?? file) as File,
          ceiling:   (next.ceiling.file ?? file)   as File,
          types:     (next.types.file ?? file)     as File,
        }
        const [polygon, obstacleList, ceilingList, bayTypes] = await Promise.all([
          parseWarehouse(rawFiles.warehouse),
          parseObstacles(rawFiles.obstacles),
          parseCeiling(rawFiles.ceiling),
          parseBayTypes(rawFiles.types),
        ])
        onCaseLoaded({ polygon, obstacles: obstacleList, ceiling: ceilingList, bayTypes }, rawFiles)
      }
    } catch (e) {
      setSlots(prev => ({
        ...prev,
        [key]: { file: null, error: e instanceof Error ? e.message : ERROR_HINTS[key], loading: false },
      }))
    }
  }

  async function handleSolutionFile(file: File) {
    setSolutionSlot({ file: null, error: null, loading: true })
    try {
      const sol = await parseSolution(file)
      setSolutionSlot({ file, error: null, loading: false })
      onSolutionLoaded({ placements: sol })
    } catch (e) {
      setSolutionSlot({ file: null, error: e instanceof Error ? e.message : 'Invalid format', loading: false })
    }
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 12, padding: '20px 16px 16px' }}>

      <p style={{ color: 'var(--color-muted)', fontFamily: 'var(--font-mono)', fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.08em' }}>
        Input files
      </p>

      {/* 2×2 grid */}
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 8 }}>
        {SLOTS.map(({ key, label }) => (
          <GridTile
            key={key}
            label={label}
            state={slots[key]}
            onFile={(f) => handleFile(key, f)}
          />
        ))}
      </div>

      {/* Error messages */}
      {SLOTS.map(({ key }) =>
        slots[key].error ? (
          <p key={key} role="alert" style={{ fontSize: 11, color: 'var(--color-destructive)', fontFamily: 'var(--font-mono)', marginTop: -4 }}>
            {slots[key].error}
          </p>
        ) : null
      )}

      {allLoaded && (
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'center', gap: 6, color: 'var(--color-accent)', fontSize: 12 }}>
          <Check size={12} aria-hidden />
          <span style={{ fontFamily: 'var(--font-mono)' }}>All files loaded</span>
        </div>
      )}

      <p style={{ color: 'var(--color-muted)', fontFamily: 'var(--font-mono)', fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.08em', marginTop: 4 }}>
        Solution (optional)
      </p>

      <SolutionSlot state={solutionSlot} onFile={handleSolutionFile} />

      {solutionSlot.error && (
        <p role="alert" style={{ fontSize: 11, color: 'var(--color-destructive)', fontFamily: 'var(--font-mono)', marginTop: -4 }}>
          {solutionSlot.error}
        </p>
      )}
    </div>
  )
}

function GridTile({ label, state, onFile }: { label: string; state: FileSlotState; onFile: (f: File) => void }) {
  const inputRef = useRef<HTMLInputElement>(null)
  const [dragging, setDragging] = useState(false)

  const loaded = state.file !== null
  const hasError = state.error !== null

  const borderColor = hasError
    ? 'var(--color-destructive)'
    : loaded
    ? 'var(--color-accent)'
    : dragging
    ? 'var(--color-accent)'
    : 'var(--color-border)'

  const bg = loaded
    ? 'rgba(34,197,94,0.08)'
    : hasError
    ? 'rgba(239,68,68,0.08)'
    : dragging
    ? 'rgba(34,197,94,0.04)'
    : 'transparent'

  function onDrop(e: React.DragEvent) {
    e.preventDefault()
    setDragging(false)
    const file = e.dataTransfer.files[0]
    if (file) onFile(file)
  }

  function onKeyDown(e: React.KeyboardEvent) {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); inputRef.current?.click() }
  }

  return (
    <div
      role="button"
      tabIndex={0}
      aria-label={`Upload ${label}.csv`}
      onClick={() => inputRef.current?.click()}
      onKeyDown={onKeyDown}
      onDragOver={(e) => { e.preventDefault(); setDragging(true) }}
      onDragLeave={() => setDragging(false)}
      onDrop={onDrop}
      style={{
        border: `1px dashed ${borderColor}`,
        borderRadius: 8,
        background: bg,
        padding: '14px 8px',
        textAlign: 'center',
        cursor: 'pointer',
        transition: 'border-color 0.15s, background 0.15s',
        display: 'flex',
        flexDirection: 'column',
        alignItems: 'center',
        gap: 6,
        outline: 'none',
      }}
    >
      <input
        ref={inputRef}
        type="file"
        accept=".csv"
        aria-label={`Select ${label}.csv`}
        className="sr-only"
        onChange={(e) => { const f = e.target.files?.[0]; if (f) onFile(f) }}
      />

      <div style={{ color: loaded ? 'var(--color-accent)' : hasError ? 'var(--color-destructive)' : 'var(--color-border)' }}>
        {state.loading
          ? <Loader2 size={18} className="animate-spin" aria-hidden />
          : loaded
          ? <Check size={18} aria-hidden />
          : hasError
          ? <AlertCircle size={18} aria-hidden />
          : <Plus size={18} aria-hidden />
        }
      </div>

      <span style={{
        fontFamily: 'var(--font-mono)',
        fontSize: 10,
        color: loaded ? 'var(--color-fg)' : 'var(--color-muted)',
        wordBreak: 'break-all',
        lineHeight: 1.3,
      }}>
        {label}
      </span>
    </div>
  )
}

function SolutionSlot({ state, onFile }: { state: FileSlotState; onFile: (f: File) => void }) {
  const inputRef = useRef<HTMLInputElement>(null)
  const [dragging, setDragging] = useState(false)

  const loaded = state.file !== null
  const borderColor = loaded ? 'var(--color-accent)' : dragging ? 'var(--color-accent)' : 'var(--color-border)'
  const bg = loaded ? 'rgba(34,197,94,0.06)' : dragging ? 'rgba(34,197,94,0.04)' : 'transparent'

  function onDrop(e: React.DragEvent) {
    e.preventDefault()
    setDragging(false)
    const file = e.dataTransfer.files[0]
    if (file) onFile(file)
  }

  function onKeyDown(e: React.KeyboardEvent) {
    if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); inputRef.current?.click() }
  }

  return (
    <div
      role="button"
      tabIndex={0}
      aria-label="Upload solution.csv"
      onClick={() => inputRef.current?.click()}
      onKeyDown={onKeyDown}
      onDragOver={(e) => { e.preventDefault(); setDragging(true) }}
      onDragLeave={() => setDragging(false)}
      onDrop={onDrop}
      style={{
        border: `1px dashed ${borderColor}`,
        borderRadius: 8,
        background: bg,
        padding: '10px 12px',
        cursor: 'pointer',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        gap: 8,
        outline: 'none',
        transition: 'border-color 0.15s, background 0.15s',
      }}
    >
      <input
        ref={inputRef}
        type="file"
        accept=".csv"
        aria-label="Select solution.csv"
        className="sr-only"
        onChange={(e) => { const f = e.target.files?.[0]; if (f) onFile(f) }}
      />
      <span style={{ fontFamily: 'var(--font-mono)', fontSize: 11, color: loaded ? 'var(--color-fg)' : 'var(--color-muted)' }}>
        {state.file ? state.file.name : 'solution.csv'}
      </span>
      <div style={{ color: loaded ? 'var(--color-accent)' : 'var(--color-muted)', flexShrink: 0 }}>
        {state.loading
          ? <Loader2 size={13} className="animate-spin" aria-hidden />
          : loaded
          ? <Check size={13} aria-hidden />
          : <Plus size={13} aria-hidden />
        }
      </div>
    </div>
  )
}
