import type { BayType, Solution, WarehouseCase } from '../types'

interface Props {
  solution: Solution | null
  warehouseCase: WarehouseCase | null
  bayTypes: BayType[] | null
  selectedTypeIds: Set<number>
  onToggleType: (id: number) => void
  onClearFilter: () => void
}

const BAY_COLORS = [
  '#3B82F6', '#F59E0B', '#EC4899', '#8B5CF6',
  '#06B6D4', '#F97316', '#84CC16', '#14B8A6',
]

export function getBayColor(typeId: number): string {
  return BAY_COLORS[typeId % BAY_COLORS.length]
}

export default function BayLegend({ solution, warehouseCase, bayTypes: bayTypesProp, selectedTypeIds, onToggleType, onClearFilter }: Props) {
  // Use solution-based types if available, otherwise fall back to loaded bay types
  const allBayTypes = warehouseCase?.bayTypes ?? bayTypesProp
  if (!allBayTypes) return null

  const typeMap = new Map(allBayTypes.map(t => [t.id, t]))
  const counts = new Map<number, number>()
  if (solution) {
    for (const p of solution.placements) counts.set(p.id, (counts.get(p.id) ?? 0) + 1)
  }
  // Show types used in solution, or all known types if no solution yet
  const usedTypeIds = solution
    ? [...new Set(solution.placements.map(p => p.id))].sort((a, b) => a - b)
    : allBayTypes.map(t => t.id).sort((a, b) => a - b)

  const hasFilter = selectedTypeIds.size > 0

  return (
    <div style={{ padding: 16 }}>
      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 12 }}>
        <p style={{
          color: 'var(--color-muted)',
          fontFamily: 'var(--font-mono)',
          fontSize: 11,
          textTransform: 'uppercase',
          letterSpacing: '0.08em',
          margin: 0,
        }}>
          Bay types
        </p>
        {hasFilter && (
          <button
            onClick={onClearFilter}
            style={{
              background: 'none',
              border: '1px solid var(--color-border)',
              borderRadius: 4,
              color: 'var(--color-muted)',
              fontFamily: 'var(--font-mono)',
              fontSize: 10,
              padding: '2px 7px',
              cursor: 'pointer',
            }}
          >
            Clear filter
          </button>
        )}
      </div>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        {usedTypeIds.map(id => {
          const type = typeMap.get(id)
          if (!type) return null
          const color = getBayColor(id)
          const active = selectedTypeIds.has(id)
          const dimmed = hasFilter && !active

          return (
            <button
              key={id}
              onClick={() => onToggleType(id)}
              style={{
                display: 'flex',
                alignItems: 'center',
                gap: 10,
                padding: '8px 10px',
                borderRadius: 6,
                background: active ? `${color}22` : `${color}0f`,
                border: active ? `1px solid ${color}` : `1px solid ${color}33`,
                opacity: dimmed ? 0.35 : 1,
                transition: 'opacity 0.15s, border-color 0.15s, background 0.15s',
                cursor: 'pointer',
                textAlign: 'left',
                width: '100%',
              }}
            >
              {/* Color swatch */}
              <div style={{
                width: 10,
                height: 10,
                borderRadius: 2,
                background: color,
                flexShrink: 0,
              }} />

              {/* Info */}
              <div style={{ flex: 1, minWidth: 0 }}>
                <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
                  <span style={{ fontFamily: 'var(--font-mono)', fontSize: 12, color: 'var(--color-fg)', fontWeight: 600 }}>
                    Type {id}
                  </span>
                  <span style={{ fontFamily: 'var(--font-mono)', fontSize: 11, color: color, fontWeight: 600 }}>
                    {solution ? `×${counts.get(id) ?? 0}` : `${type.w}×${type.d}`}
                  </span>
                </div>
                <div style={{ display: 'flex', gap: 8, marginTop: 3 }}>
                  <span style={{ fontSize: 11, color: 'var(--color-muted)', fontFamily: 'var(--font-mono)' }}>
                    {type.w}×{type.d}mm
                  </span>
                  <span style={{ fontSize: 11, color: 'var(--color-muted)', fontFamily: 'var(--font-mono)' }}>·</span>
                  <span style={{ fontSize: 11, color: 'var(--color-muted)', fontFamily: 'var(--font-mono)' }}>
                    {type.loads} loads
                  </span>
                  <span style={{ fontSize: 11, color: 'var(--color-muted)', fontFamily: 'var(--font-mono)' }}>·</span>
                  <span style={{ fontSize: 11, color: 'var(--color-muted)', fontFamily: 'var(--font-mono)' }}>
                    {type.price.toLocaleString()}€
                  </span>
                </div>
              </div>
            </button>
          )
        })}
      </div>
    </div>
  )
}
