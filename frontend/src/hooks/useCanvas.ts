import { useCallback, useRef, useState } from 'react'

export interface Transform {
  scale: number
  offsetX: number
  offsetY: number
}

const MIN_SCALE = 0.05
const MAX_SCALE = 10

export function useCanvas() {
  const [transform, setTransform] = useState<Transform>({ scale: 1, offsetX: 0, offsetY: 0 })
  const isPanning = useRef(false)
  const lastMouse = useRef({ x: 0, y: 0 })

  const onWheel = useCallback((e: React.WheelEvent<HTMLCanvasElement>) => {
    e.preventDefault()
    const factor = e.deltaY < 0 ? 1.1 : 0.9
    const rect = e.currentTarget.getBoundingClientRect()
    const mx = e.clientX - rect.left
    const my = e.clientY - rect.top
    setTransform((t) => {
      const newScale = Math.min(MAX_SCALE, Math.max(MIN_SCALE, t.scale * factor))
      return {
        scale: newScale,
        offsetX: mx - (mx - t.offsetX) * (newScale / t.scale),
        offsetY: my - (my - t.offsetY) * (newScale / t.scale),
      }
    })
  }, [])

  const onMouseDown = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    isPanning.current = true
    lastMouse.current = { x: e.clientX, y: e.clientY }
  }, [])

  const onMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (!isPanning.current) return
    const dx = e.clientX - lastMouse.current.x
    const dy = e.clientY - lastMouse.current.y
    lastMouse.current = { x: e.clientX, y: e.clientY }
    setTransform((t) => ({ ...t, offsetX: t.offsetX + dx, offsetY: t.offsetY + dy }))
  }, [])

  const onMouseUp = useCallback(() => { isPanning.current = false }, [])

  return { transform, setTransform, onWheel, onMouseDown, onMouseMove, onMouseUp }
}
