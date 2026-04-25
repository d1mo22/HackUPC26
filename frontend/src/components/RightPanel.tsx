import { useState } from 'react'
import MetricsPanel from './MetricsPanel'
import BreakdownTab from './BreakdownTab'
import HistoryTab from './HistoryTab'
import BayLegend from './BayLegend'
import type { BayType, RunRecord, Solution, WarehouseCase } from '../types'

type Tab = 'metrics' | 'breakdown' | 'history'

interface Props {
  solution: Solution | null
  warehouseCase: WarehouseCase | null
  bayTypes: BayType[] | null
  runHistory: RunRecord[]
  activeRunId: number | null
  onRestore: (solution: Solution, id: number) => void
  selectedTypeIds: Set<number>
  onToggleType: (id: number) => void
  onClearFilter: () => void
}

const TABS: { id: Tab; label: string }[] = [
  { id: 'metrics',   label: 'Metrics'   },
  { id: 'breakdown', label: 'Breakdown' },
  { id: 'history',   label: 'History'   },
]

export default function RightPanel({ solution, warehouseCase, bayTypes, runHistory, activeRunId, onRestore, selectedTypeIds, onToggleType, onClearFilter }: Props) {
  const [activeTab, setActiveTab] = useState<Tab>('metrics')

  function handleRestore(sol: Solution, id: number) {
    onRestore(sol, id)
  }

  return (
    <div style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>

      {/* Tab header */}
      <div style={{
        display: 'flex',
        borderBottom: '1px solid var(--color-border)',
        flexShrink: 0,
      }}>
        {TABS.map(tab => (
          <button
            key={tab.id}
            onClick={() => setActiveTab(tab.id)}
            style={{
              flex: 1,
              padding: '10px 0',
              background: 'none',
              border: 'none',
              borderBottom: activeTab === tab.id ? '2px solid var(--color-accent)' : '2px solid transparent',
              color: activeTab === tab.id ? 'var(--color-fg)' : 'var(--color-muted)',
              fontFamily: 'var(--font-mono)',
              fontSize: 11,
              cursor: 'pointer',
              transition: 'color 0.15s, border-color 0.15s',
              marginBottom: -1,
            }}
          >
            {tab.label}
          </button>
        ))}
      </div>

      {/* Tab content */}
      <div style={{ flex: 1, overflowY: 'auto' }}>
        {activeTab === 'metrics' && (
          <>
            <MetricsPanel solution={solution} warehouseCase={warehouseCase} />
            <div style={{ borderTop: '1px solid var(--color-border)' }}>
              <BayLegend
                solution={solution}
                warehouseCase={warehouseCase}
                bayTypes={bayTypes}
                selectedTypeIds={selectedTypeIds}
                onToggleType={onToggleType}
                onClearFilter={onClearFilter}
              />
            </div>
          </>
        )}
        {activeTab === 'breakdown' && solution && warehouseCase && (
          <BreakdownTab solution={solution} warehouseCase={warehouseCase} />
        )}
        {activeTab === 'breakdown' && !solution && bayTypes && (
          <BayTypesTable bayTypes={bayTypes} />
        )}
        {activeTab === 'breakdown' && !solution && !bayTypes && (
          <div style={{ padding: 16, color: 'var(--color-muted)', fontSize: 13, fontFamily: 'var(--font-mono)' }}>
            Load types_of_bays.csv to see bay types.
          </div>
        )}
        {activeTab === 'history' && (
          <HistoryTab
            history={runHistory}
            activeRunId={activeRunId}
            onRestore={handleRestore}
          />
        )}
      </div>
    </div>
  )
}

function BayTypesTable({ bayTypes }: { bayTypes: BayType[] }) {
  return (
    <div style={{ padding: 16 }}>
      <p style={{ color: 'var(--color-muted)', fontFamily: 'var(--font-mono)', fontSize: 11, textTransform: 'uppercase', letterSpacing: '0.08em', marginBottom: 12 }}>
        Bay types catalog
      </p>
      <div style={{ display: 'flex', flexDirection: 'column', gap: 6 }}>
        {bayTypes.map(t => (
          <div key={t.id} style={{
            padding: '8px 10px', borderRadius: 6,
            background: 'rgba(255,255,255,0.03)', border: '1px solid var(--color-border)',
          }}>
            <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
              <span style={{ fontFamily: 'var(--font-mono)', fontSize: 12, fontWeight: 600, color: 'var(--color-fg)' }}>
                Type {t.id}
              </span>
              <span style={{ fontFamily: 'var(--font-mono)', fontSize: 11, color: 'var(--color-muted)' }}>
                {t.w}×{t.d}×{t.h} mm
              </span>
            </div>
            <div style={{ display: 'flex', gap: 12 }}>
              {[['loads', t.loads], ['price', t.price], ['gap', t.gap]].map(([k, v]) => (
                <span key={k} style={{ fontFamily: 'var(--font-mono)', fontSize: 10, color: 'var(--color-muted)' }}>
                  {k}: <span style={{ color: 'var(--color-fg)' }}>{v}</span>
                </span>
              ))}
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}
