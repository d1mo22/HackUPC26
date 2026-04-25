import type { Solution, WarehouseCase } from '../types'

interface Props {
  solution: Solution | null
  warehouseCase: WarehouseCase | null
}

const BAY_COLORS = [
  '#3B82F6', '#F59E0B', '#EC4899', '#8B5CF6',
  '#06B6D4', '#F97316', '#84CC16', '#14B8A6',
]

export function getBayColor(typeId: number): string {
  return BAY_COLORS[typeId % BAY_COLORS.length]
}

export default function BayLegend({ solution, warehouseCase }: Props) {
  if (!solution || !warehouseCase) return null

  const typeMap = new Map(warehouseCase.bayTypes.map(t => [t.id, t]))

  const usedTypeIds = [...new Set(solution.placements.map(p => p.id))]
    .sort((a, b) => a - b)

  const counts = new Map<number, number>()
  for (const p of solution.placements) {
    counts.set(p.id, (counts.get(p.id) ?? 0) + 1)
  }

  return (
    <div style={{ padding: 16 }}>
      <p style={{
        color: 'var(--color-muted)',
        fontFamily: 'var(--font-mono)',
        fontSize: 11,
        textTransform: 'uppercase',
        letterSpacing: '0.08em',
        marginBottom: 12,
      }}>
        Bay types
      </p>

      <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
        {usedTypeIds.map(id => {
          const type = typeMap.get(id)
          if (!type) return null
          const color = getBayColor(id)
          const count = counts.get(id) ?? 0

          return (
            <div
              key={id}
              style={{
                display: 'flex',
                alignItems: 'center',
                gap: 10,
                padding: '8px 10px',
                borderRadius: 6,
                background: `${color}0f`,
                border: `1px solid ${color}33`,
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
                    ×{count}
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
            </div>
          )
        })}
      </div>
    </div>
  )
}
