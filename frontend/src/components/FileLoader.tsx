import { useRef, useState } from 'react'
import { Check, AlertCircle, Upload, Loader2 } from 'lucide-react'
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
  { key: 'warehouse', label: 'warehouse.csv' },
  { key: 'obstacles', label: 'obstacles.csv' },
  { key: 'ceiling',   label: 'ceiling.csv'   },
  { key: 'types',     label: 'types_of_bays.csv' },
] as const

type SlotKey = typeof SLOTS[number]['key']

const PARSERS = {
  warehouse: parseWarehouse,
  obstacles: parseObstacles,
  ceiling:   parseCeiling,
  types:     parseBayTypes,
}

const ERROR_HINTS: Record<SlotKey, string> = {
  warehouse: 'Expected columns: x, y',
  obstacles: 'Expected columns: x, y, w, d',
  ceiling:   'Expected columns: x, h',
  types:     'Expected columns: id, w, d, h, gap, loads, price',
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
    } catch {
      setSlots(prev => ({
        ...prev,
        [key]: { file: null, error: `Invalid CSV — ${ERROR_HINTS[key]}`, loading: false },
      }))
    }
  }

  async function handleSolutionFile(file: File) {
    setSolutionSlot({ file: null, error: null, loading: true })
    try {
      const sol = await parseSolution(file)
      setSolutionSlot({ file, error: null, loading: false })
      onSolutionLoaded({ placements: sol })
    } catch {
      setSolutionSlot({ file: null, error: 'Invalid CSV — Expected: Id, X, Y, Rotation', loading: false })
    }
  }

  return (
    <div className="flex flex-col gap-2 p-4">
      <p
        className="text-xs font-medium mb-1"
        style={{ color: 'var(--color-muted)', fontFamily: 'var(--font-mono)', textTransform: 'uppercase', letterSpacing: '0.08em' }}
      >
        Input files
      </p>

      {SLOTS.map(({ key, label }) => (
        <DropSlot
          key={key}
          label={label}
          state={slots[key]}
          onFile={(f) => handleFile(key, f)}
        />
      ))}

      <p
        className="text-xs font-medium mt-3 mb-1"
        style={{ color: 'var(--color-muted)', fontFamily: 'var(--font-mono)', textTransform: 'uppercase', letterSpacing: '0.08em' }}
      >
        Solution (optional)
      </p>
      <DropSlot
        label="solution.csv"
        state={solutionSlot}
        onFile={handleSolutionFile}
      />

      {allLoaded && (
        <p className="flex items-center gap-1 text-xs mt-2 justify-center" style={{ color: 'var(--color-accent)' }}>
          <Check size={12} aria-hidden /> All files loaded
        </p>
      )}
    </div>
  )
}

function DropSlot({
  label,
  state,
  onFile,
}: {
  label: string
  state: FileSlotState
  onFile: (f: File) => void
}) {
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
    ? 'rgba(34,197,94,0.06)'
    : hasError
    ? 'rgba(239,68,68,0.06)'
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
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault()
      inputRef.current?.click()
    }
  }

  const inputId = `file-slot-${label.replace(/\W/g, '-')}`

  return (
    <div>
      <div
        role="button"
        tabIndex={0}
        aria-label={`Upload ${label}`}
        onClick={() => inputRef.current?.click()}
        onKeyDown={onKeyDown}
        onDragOver={(e) => { e.preventDefault(); setDragging(true) }}
        onDragLeave={() => setDragging(false)}
        onDrop={onDrop}
        className="rounded-md px-3 py-2 cursor-pointer transition-colors active:opacity-70"
        style={{
          border: `1px dashed ${borderColor}`,
          background: bg,
          outline: 'none',
        }}
      >
        <input
          ref={inputRef}
          id={inputId}
          type="file"
          accept=".csv"
          aria-label={`Select ${label}`}
          className="sr-only"
          onChange={(e) => { const f = e.target.files?.[0]; if (f) onFile(f) }}
        />
        <div className="flex items-center justify-between gap-2">
          <span
            className="text-xs truncate"
            style={{ fontFamily: 'var(--font-mono)', color: loaded ? 'var(--color-fg)' : 'var(--color-muted)' }}
          >
            {state.loading ? 'Parsing…' : state.file ? state.file.name : label}
          </span>
          {state.loading && <Loader2 size={12} className="animate-spin shrink-0" style={{ color: 'var(--color-muted)' }} aria-hidden />}
          {loaded && !state.loading && <Check size={12} className="shrink-0" style={{ color: 'var(--color-accent)' }} aria-hidden />}
          {hasError && <AlertCircle size={12} className="shrink-0" style={{ color: 'var(--color-destructive)' }} aria-hidden />}
        </div>
      </div>
      {hasError && (
        <p className="text-xs mt-1 px-1" style={{ color: 'var(--color-destructive)' }} role="alert">
          {state.error}
        </p>
      )}
    </div>
  )
}
