import { Layers, Tag } from 'lucide-react'

interface Props {
  isReady: boolean
  isRunning: boolean
  showCeiling: boolean
  showLabels: boolean
  onRun: () => void
  onToggleCeiling: () => void
  onToggleLabels: () => void
}

export default function Controls({
  isReady,
  isRunning,
  showCeiling,
  showLabels,
  onRun,
  onToggleCeiling,
  onToggleLabels,
}: Props) {
  return (
    <div style={{ padding: 16, flexShrink: 0, borderTop: '1px solid var(--color-border)', display: 'flex', flexDirection: 'column', gap: 10 }}>

      <Toggle
        icon={<Layers size={13} aria-hidden />}
        label="Ceiling overlay"
        enabled={showCeiling}
        onToggle={onToggleCeiling}
      />
      <Toggle
        icon={<Tag size={13} aria-hidden />}
        label="Bay labels"
        enabled={showLabels}
        onToggle={onToggleLabels}
      />

      <button
        onClick={onRun}
        disabled={!isReady || isRunning}
        style={{
          marginTop: 4,
          width: '100%',
          padding: '8px 0',
          borderRadius: 6,
          border: 'none',
          fontFamily: 'var(--font-ui)',
          fontSize: 14,
          fontWeight: 500,
          transition: 'opacity 0.15s, background 0.15s',
          background: isReady && !isRunning ? 'var(--color-accent)' : 'var(--color-border)',
          color: isReady && !isRunning ? '#000' : 'var(--color-muted)',
          cursor: isReady && !isRunning ? 'pointer' : 'not-allowed',
          opacity: !isReady && !isRunning ? 0.5 : 1,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          gap: 8,
        }}
      >
        {isRunning && (
          <svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" style={{ animation: 'spin 1s linear infinite' }}>
            <path d="M21 12a9 9 0 1 1-6.219-8.56" />
          </svg>
        )}
        {isRunning ? 'Running…' : 'Run Solver'}
      </button>

      <style>{`@keyframes spin { from { transform: rotate(0deg) } to { transform: rotate(360deg) } }`}</style>
    </div>
  )
}

function Toggle({
  icon,
  label,
  enabled,
  onToggle,
}: {
  icon: React.ReactNode
  label: string
  enabled: boolean
  onToggle: () => void
}) {
  return (
    <button
      role="switch"
      aria-checked={enabled}
      onClick={onToggle}
      style={{
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'space-between',
        width: '100%',
        background: 'none',
        border: 'none',
        cursor: 'pointer',
        padding: '2px 0',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, color: 'var(--color-muted)' }}>
        {icon}
        <span style={{ fontSize: 13, fontFamily: 'var(--font-ui)', color: 'var(--color-muted)' }}>{label}</span>
      </div>

      {/* Pill toggle */}
      <div style={{
        width: 32,
        height: 18,
        borderRadius: 9999,
        background: enabled ? 'var(--color-accent)' : 'var(--color-border)',
        transition: 'background 0.2s',
        position: 'relative',
        flexShrink: 0,
      }}>
        <div style={{
          width: 12,
          height: 12,
          borderRadius: 9999,
          background: '#fff',
          position: 'absolute',
          top: 3,
          left: enabled ? 17 : 3,
          transition: 'left 0.2s',
        }} />
      </div>
    </button>
  )
}
