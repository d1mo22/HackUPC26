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

  // Build rows: only types present in solution, sorted by price/loads asc
  const rows = [...counts.keys()]
    .map(id => {
      const type = typeMap.get(id)!
      return { id, type, count: counts.get(id)!, ratio: type.price / type.loads }
    })
    .sort((a, b) => a.ratio - b.ratio)

  if (rows.length === 0) return (
    <div style={{ padding: 16, color: 'var(--color-muted)', fontSize: 13, fontFamily: 'var(--font-mono)' }}>No solution loaded.</div>
  )

  const maxRatio = rows[rows.length - 1].ratio
  const bestId = rows[0].id
  const worstId = rows[rows.length - 1].id

  // Best available type across all bay types (not just used ones)
  const bestAvailable = warehouseCase.bayTypes
    .map(t => ({ id: t.id, ratio: t.price / t.loads }))
    .sort((a, b) => a.ratio - b.ratio)[0]

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
        {rows.map(({ id, count, ratio }) => {
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
