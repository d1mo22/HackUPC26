import { useEffect, useRef } from 'react'
import { Canvas, useThree } from '@react-three/fiber'
import { OrbitControls, Line, Text } from '@react-three/drei'
import * as THREE from 'three'
import { getBayColor } from './BayLegend'
import { bayDimensions, polygonBounds } from '../lib/geometry'
import type { Point, Obstacle, CeilingSegment, BayType, PlacedBay } from '../types'

const S = 0.001  // mm → three.js units

function hexToThree(hex: string): THREE.Color {
  return new THREE.Color(
    parseInt(hex.slice(1, 3), 16) / 255,
    parseInt(hex.slice(3, 5), 16) / 255,
    parseInt(hex.slice(5, 7), 16) / 255,
  )
}

// Warehouse floor as a filled polygon
function Floor({ polygon }: { polygon: Point[] }) {
  const shape = new THREE.Shape()
  shape.moveTo(polygon[0].x * S, -polygon[0].y * S)
  for (let i = 1; i < polygon.length; i++) {
    shape.lineTo(polygon[i].x * S, -polygon[i].y * S)
  }
  shape.closePath()
  const pts = [...polygon, polygon[0]].map(p => new THREE.Vector3(p.x * S, 0.001, -p.y * S))
  return (
    <group>
      <mesh rotation={[-Math.PI / 2, 0, 0]}>
        <shapeGeometry args={[shape]} />
        <meshStandardMaterial color="#08111f" side={THREE.DoubleSide} />
      </mesh>
      <Line points={pts} color="#334155" lineWidth={1.5} />
    </group>
  )
}

// Obstacle boxes — solid red with edge outlines
function ObstacleBox({ obs, maxCeilH }: { obs: Obstacle; maxCeilH: number }) {
  const bh = maxCeilH * 0.5 * S
  const cx = (obs.x + obs.w / 2) * S
  const cy = bh / 2
  const cz = -(obs.y + obs.d / 2) * S
  const bw = obs.w * S
  const bd = obs.d * S
  const geo = new THREE.BoxGeometry(bw, bh, bd)
  return (
    <group position={[cx, cy, cz]}>
      <mesh>
        <boxGeometry args={[bw, bh, bd]} />
        <meshStandardMaterial color="#dc2626" />
      </mesh>
      <lineSegments>
        <edgesGeometry args={[geo]} />
        <lineBasicMaterial color="#000000" />
      </lineSegments>
      <Text
        position={[0, bh / 2 + 0.02, 0]}
        fontSize={0.06}
        color="white"
        anchorX="center"
        anchorY="bottom"
        font={undefined}
      >
        OBS
      </Text>
    </group>
  )
}

// A single bay box with edge outline and optional label
function BayBox({ p, type, showLabels, showGaps }: { p: PlacedBay; type: BayType; showLabels: boolean; showGaps: boolean }) {
  const dims = bayDimensions(p, type)
  const color = hexToThree(getBayColor(p.id))
  const cx = (p.x + dims.w / 2) * S
  const cy = (type.h / 2) * S
  const cz = -(p.y + dims.d / 2) * S
  const bw = dims.w * S
  const bh = type.h * S
  const bd = dims.d * S
  const geo = new THREE.BoxGeometry(bw, bh, bd)

  return (
    <group>
      <group position={[cx, cy, cz]}>
        <mesh castShadow>
          <boxGeometry args={[bw, bh, bd]} />
          <meshStandardMaterial color={color} />
        </mesh>
        <lineSegments>
          <edgesGeometry args={[geo]} />
          <lineBasicMaterial color="#000000" transparent opacity={0.45} />
        </lineSegments>
        {showLabels && (
          <Text
            position={[0, bh / 2 + 0.01, 0]}
            fontSize={0.05}
            color="rgba(255,255,255,0.92)"
            anchorX="center"
            anchorY="bottom"
            font={undefined}
          >
            {String(p.id)}
          </Text>
        )}
      </group>

      {/* Gap zone — semi-transparent plane on the floor */}
      {showGaps && type.gap > 0 && (() => {
        const gapW = p.rotation === 0 ? dims.w * S : type.gap * S
        const gapD = p.rotation === 0 ? type.gap * S : dims.d * S
        const gapX = p.rotation === 0 ? cx : (p.x + dims.w + type.gap / 2) * S
        const gapZ = p.rotation === 0 ? -(p.y + dims.d + type.gap / 2) * S : cz
        return (
          <mesh position={[gapX, 0.002, gapZ]} rotation={[-Math.PI / 2, 0, 0]}>
            <planeGeometry args={[gapW, gapD]} />
            <meshStandardMaterial color={getBayColor(p.id)} opacity={0.12} transparent side={THREE.DoubleSide} />
          </mesh>
        )
      })()}
    </group>
  )
}

// Ceiling planes with dashed-style edges using Line segments
function CeilingPlanes({ ceiling, polygon }: { ceiling: CeilingSegment[]; polygon: Point[] }) {
  const bounds = polygonBounds(polygon)
  const sorted = [...ceiling].sort((a, b) => a.x - b.x)
  const maxH = Math.max(...sorted.map(s => s.h))
  const minH = Math.min(...sorted.map(s => s.h))
  const minY = bounds.minY
  const maxY = bounds.maxY

  return (
    <>
      {sorted.map((seg, i) => {
        const nextX = i + 1 < sorted.length ? sorted[i + 1].x : bounds.maxX
        const w = (nextX - seg.x) * S
        const cx = (seg.x + (nextX - seg.x) / 2) * S
        const cy = seg.h * S
        const cz = -(minY + (maxY - minY) / 2) * S
        const d = (maxY - minY) * S
        const color = seg.h >= maxH ? '#22c55e' : seg.h <= minH ? '#ef4444' : '#eab308'

        // Four corners of the ceiling plane for dashed outline
        const x0 = seg.x * S, x1 = nextX * S
        const z0 = -minY * S, z1 = -maxY * S
        const corners = [
          new THREE.Vector3(x0, cy, z0),
          new THREE.Vector3(x1, cy, z0),
          new THREE.Vector3(x1, cy, z1),
          new THREE.Vector3(x0, cy, z1),
          new THREE.Vector3(x0, cy, z0),
        ]

        return (
          <group key={i}>
            <mesh position={[cx, cy, cz]} rotation={[-Math.PI / 2, 0, 0]}>
              <planeGeometry args={[w, d]} />
              <meshStandardMaterial color={color} opacity={0.07} transparent side={THREE.DoubleSide} />
            </mesh>
            <Line points={corners} color={color} lineWidth={1} dashed dashSize={0.05} gapSize={0.03} />
          </group>
        )
      })}
    </>
  )
}

// Auto-position camera to fit warehouse
function AutoCamera({ polygon }: { polygon: Point[] }) {
  const { camera } = useThree()
  const fitted = useRef(false)

  useEffect(() => { fitted.current = false }, [polygon])

  useEffect(() => {
    if (fitted.current) return
    const bounds = polygonBounds(polygon)
    const cx = ((bounds.minX + bounds.maxX) / 2) * S
    const cz = -((bounds.minY + bounds.maxY) / 2) * S
    const span = Math.max(bounds.maxX - bounds.minX, bounds.maxY - bounds.minY) * S
    camera.position.set(cx + span * 0.8, span * 0.9, cz + span * 0.8)
    camera.lookAt(cx, 0, cz)
    camera.updateProjectionMatrix()
    fitted.current = true
  }, [polygon, camera])

  return null
}

export interface Canvas3DProps {
  polygon: Point[]
  obstacles: Obstacle[]
  ceiling: CeilingSegment[]
  bayTypes: BayType[]
  placements: PlacedBay[]
  showLabels: boolean
  showCeiling: boolean
  showGaps: boolean
  selectedTypeIds: Set<number>
}

export default function Canvas3D({
  polygon, obstacles, ceiling, bayTypes, placements,
  showLabels, showCeiling, showGaps, selectedTypeIds,
}: Canvas3DProps) {
  const typeMap = new Map(bayTypes.map(bt => [bt.id, bt]))
  const visiblePlacements = selectedTypeIds.size > 0
    ? placements.filter(p => selectedTypeIds.has(p.id))
    : placements
  const maxCeilH = ceiling.length > 0
    ? Math.max(...ceiling.map(s => s.h))
    : 3000

  return (
    <Canvas
      style={{ background: '#020617', width: '100%', height: '100%' }}
      camera={{ fov: 50, near: 0.01, far: 1000 }}
      gl={{ antialias: true }}
    >
      <ambientLight intensity={1.4} />
      <directionalLight position={[5, 10, 5]} intensity={0.6} castShadow />

      <AutoCamera polygon={polygon} />
      <OrbitControls makeDefault enableDamping dampingFactor={0.08} />

      <Floor polygon={polygon} />

      {obstacles.map((obs, i) => (
        <ObstacleBox key={i} obs={obs} maxCeilH={maxCeilH} />
      ))}

      {visiblePlacements.map((p, i) => {
        const type = typeMap.get(p.id)
        if (!type) return null
        return <BayBox key={i} p={p} type={type} showLabels={showLabels} showGaps={showGaps} />
      })}

      {showCeiling && ceiling.length > 0 && (
        <CeilingPlanes ceiling={ceiling} polygon={polygon} />
      )}
    </Canvas>
  )
}
