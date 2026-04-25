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

  // Sort best Q first (lowest Q = best when minimising)
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
