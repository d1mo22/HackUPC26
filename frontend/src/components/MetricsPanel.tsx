import { useEffect, useRef, useState } from 'react'
import { TrendingUp, Box, DollarSign, Package, LayoutGrid, Info } from 'lucide-react'
import { computeMetrics } from '../lib/scoring'
import type { Solution, WarehouseCase } from '../types'

interface Props {
  solution: Solution | null
  warehouseCase: WarehouseCase | null
}

export default function MetricsPanel({ solution, warehouseCase }: Props) {
  const metrics = solution && warehouseCase
    ? computeMetrics(solution.placements, warehouseCase.bayTypes, warehouseCase.polygon)
    : null

  return (
    <div style={{ padding: 16 }}>
      <p style={{ color: 'var(--color-muted)', fontFamily: 'var(--font-mono)', fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.08em', marginBottom: 16 }}>
        Metrics
      </p>

      {/* Q Score — hero metric */}
      <div style={{ marginBottom: 20, padding: 16, borderRadius: 8, background: 'rgba(34,197,94,0.06)', border: '1px solid rgba(34,197,94,0.2)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 6 }}>
          <TrendingUp size={13} style={{ color: 'var(--color-accent)' }} aria-hidden />
          <span style={{ color: 'var(--color-muted)', fontSize: 11, fontFamily: 'var(--font-mono)', textTransform: 'uppercase', letterSpacing: '0.08em' }}>
            Q Score
          </span>
          <QFormulaTooltip />
        </div>
        <AnimatedNumber
          value={metrics?.q ?? null}
          format={(v) => v.toLocaleString('de-DE', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}
          style={{ fontFamily: 'var(--font-mono)', fontSize: 28, fontWeight: 700, color: 'var(--color-accent)', lineHeight: 1 }}
        />
      </div>

      {/* Secondary metrics */}
      <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
        <MetricRow
          icon={<Box size={13} aria-hidden />}
          label="Coverage"
          value={metrics ? `${metrics.coveragePct.toFixed(1)}%` : null}
        />
        <MetricRow
          icon={<DollarSign size={13} aria-hidden />}
          label="Total price"
          value={metrics ? metrics.totalPrice.toLocaleString() : null}
        />
        <MetricRow
          icon={<Package size={13} aria-hidden />}
          label="Total loads"
          value={metrics ? metrics.totalLoads.toLocaleString() : null}
        />
        <MetricRow
          icon={<LayoutGrid size={13} aria-hidden />}
          label="Bay count"
          value={metrics ? String(metrics.bayCount) : null}
        />
      </div>
    </div>
  )
}

function MetricRow({ icon, label, value }: { icon: React.ReactNode; label: string; value: string | null }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', padding: '8px 0', borderBottom: '1px solid var(--color-border)' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, color: 'var(--color-muted)' }}>
        {icon}
        <span style={{ fontSize: 13 }}>{label}</span>
      </div>
      <span style={{ fontFamily: 'var(--font-mono)', fontSize: 13, color: value ? 'var(--color-fg)' : 'var(--color-muted)' }}>
        {value ?? '—'}
      </span>
    </div>
  )
}

function QFormulaTooltip() {
  const [visible, setVisible] = useState(false)
  const [pos, setPos] = useState({ top: 0, left: 0 })
  const btnRef = useRef<HTMLButtonElement>(null)

  function showTooltip() {
    if (btnRef.current) {
      const r = btnRef.current.getBoundingClientRect()
      setPos({ top: r.bottom + 6, left: r.right })
    }
    setVisible(true)
  }

  return (
    <div style={{ marginLeft: 'auto', lineHeight: 0 }}>
      <button
        ref={btnRef}
        onMouseEnter={showTooltip}
        onMouseLeave={() => setVisible(false)}
        style={{ background: 'none', border: 'none', cursor: 'pointer', padding: 0, color: 'var(--color-muted)' }}
        aria-label="Q score formula"
      >
        <Info size={11} />
      </button>
      {visible && (
        <div style={{
          position: 'fixed', top: pos.top, left: pos.left, zIndex: 1000,
          transform: 'translateX(-100%)',
          background: '#0f172a', border: '1px solid #334155',
          borderRadius: 6, padding: '12px 14px',
          fontFamily: 'var(--font-mono)', fontSize: 11,
          color: 'var(--color-fg)', whiteSpace: 'nowrap',
          boxShadow: '0 4px 20px rgba(0,0,0,0.7)',
          pointerEvents: 'none', lineHeight: 1.6,
        }}>
          <div style={{ marginBottom: 8, color: 'var(--color-muted)', fontSize: 10, textTransform: 'uppercase', letterSpacing: '0.08em' }}>Formula</div>
          <div style={{ fontSize: 13, fontWeight: 600, marginBottom: 8 }}>Q = (Σ price/loads)<sup style={{ fontSize: 9 }}>exp</sup></div>
          <div style={{ color: 'var(--color-muted)', fontSize: 10, marginBottom: 4 }}>exp = 2 − area_bays / area_warehouse</div>
          <div style={{ color: '#22c55e', fontSize: 10 }}>Lower Q is better</div>
        </div>
      )}
    </div>
  )
}

function AnimatedNumber({
  value,
  format,
  style,
}: {
  value: number | null
  format: (v: number) => string
  style?: React.CSSProperties
}) {
  const [display, setDisplay] = useState('—')
  const rafRef = useRef<number | null>(null)

  useEffect(() => {
    if (value === null) { setDisplay('—'); return }

    const duration = 600
    const start = performance.now()
    const from = 0
    const target = value

    function tick(now: number) {
      const elapsed = now - start
      const progress = Math.min(elapsed / duration, 1)
      const eased = 1 - Math.pow(1 - progress, 3)
      setDisplay(format(from + (target - from) * eased))
      if (progress < 1) rafRef.current = requestAnimationFrame(tick)
    }

    rafRef.current = requestAnimationFrame(tick)
    return () => { if (rafRef.current) cancelAnimationFrame(rafRef.current) }
  }, [value])

  return <span style={style}>{display}</span>
}
