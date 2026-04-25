import { useState } from 'react'
import MetricsPanel from './MetricsPanel'
import BreakdownTab from './BreakdownTab'
import HistoryTab from './HistoryTab'
import BayLegend from './BayLegend'
import type { RunRecord, Solution, WarehouseCase } from '../types'

type Tab = 'metrics' | 'breakdown' | 'history'

interface Props {
  solution: Solution | null
  warehouseCase: WarehouseCase | null
  runHistory: RunRecord[]
  activeRunId: number | null
  onRestore: (solution: Solution, id: number) => void
}

const TABS: { id: Tab; label: string }[] = [
  { id: 'metrics',   label: 'Metrics'   },
  { id: 'breakdown', label: 'Breakdown' },
  { id: 'history',   label: 'History'   },
]

export default function RightPanel({ solution, warehouseCase, runHistory, activeRunId, onRestore }: Props) {
  const [activeTab, setActiveTab] = useState<Tab>('metrics')

  function handleRestore(sol: Solution, id: number) {
    onRestore(sol, id)
    setActiveTab('metrics')
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
              <BayLegend solution={solution} warehouseCase={warehouseCase} />
            </div>
          </>
        )}
        {activeTab === 'breakdown' && solution && warehouseCase && (
          <BreakdownTab solution={solution} warehouseCase={warehouseCase} />
        )}
        {activeTab === 'breakdown' && !(solution && warehouseCase) && (
          <div style={{ padding: 16, color: 'var(--color-muted)', fontSize: 13, fontFamily: 'var(--font-mono)' }}>
            Load files and run the solver first.
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
