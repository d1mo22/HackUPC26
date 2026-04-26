import { useEffect, useRef, useState, useCallback } from "react";

// ─── Types ───────────────────────────────────────────────────────────────────

interface Point2D { x: number; y: number; }
interface Obstacle { x: number; y: number; w: number; h: number; label: string; }
interface BayType {
  id: number; width: number; depth: number; height: number;
  thickness: number; levels: number; capacity: number; name: string;
}
interface PlacedBay { id: number; x: number; y: number; rot: 0 | 1; }
interface CeilingZone { x: number; height: number; }
interface HoverTarget { type: "bay" | "obs"; i: number; }

// ─── Default data (replace with CSV/JSON props as needed) ────────────────────

const DEFAULT_WAREHOUSE_POINTS: [number, number][] = [
  [0, 0], [10000, 0], [10000, 3000], [3000, 3000], [3000, 10000], [0, 10000],
];

const DEFAULT_OBSTACLES: Obstacle[] = [
  { x: 750,  y: 750,  w: 750,  h: 750,  label: "Obstacle A" },
  { x: 8000, y: 2500, w: 1500, h: 300,  label: "Obstacle B" },
  { x: 1500, y: 4200, w: 200,  h: 4600, label: "Obstacle C" },
];

const DEFAULT_BAY_TYPES: BayType[] = [
  { id: 0, width: 800,  depth: 1200, height: 2800, thickness: 200, levels: 4,  capacity: 2000, name: "Tipus 0" },
  { id: 1, width: 1600, depth: 1200, height: 2800, thickness: 200, levels: 8,  capacity: 2500, name: "Tipus 1" },
  { id: 2, width: 2400, depth: 1200, height: 2800, thickness: 200, levels: 12, capacity: 2800, name: "Tipus 2" },
  { id: 3, width: 800,  depth: 1000, height: 1800, thickness: 150, levels: 3,  capacity: 1800, name: "Tipus 3" },
  { id: 4, width: 1600, depth: 1000, height: 1800, thickness: 150, levels: 6,  capacity: 2300, name: "Tipus 4" },
  { id: 5, width: 2400, depth: 1000, height: 1800, thickness: 150, levels: 9,  capacity: 2600, name: "Tipus 5" },
];

const DEFAULT_SOLUTION: PlacedBay[] = [
  { id: 5, x: 1700, y: 4200, rot: 1 },
  { id: 5, x: 1700, y: 6600, rot: 1 },
  { id: 3, x: 1700, y: 9000, rot: 1 },
  { id: 2, x: 1500, y: 750,  rot: 1 },
  { id: 0, x: 1500, y: 3150, rot: 1 },
  { id: 5, x: 2900, y: 750,  rot: 0 },
  { id: 3, x: 2900, y: 1900, rot: 1 },
  { id: 3, x: 4050, y: 1900, rot: 1 },
  { id: 5, x: 5300, y: 750,  rot: 0 },
  { id: 3, x: 5300, y: 1900, rot: 1 },
  { id: 3, x: 6450, y: 1900, rot: 1 },
  { id: 1, x: 7700, y: 750,  rot: 0 },
];

const DEFAULT_CEILING: CeilingZone[] = [
  { x: 0,    height: 3000 },
  { x: 3000, height: 2000 },
  { x: 6000, height: 3000 },
];

// ─── Constants ────────────────────────────────────────────────────────────────

const WW = 10000;
const WH = 10000;
const OBS_COLOR = "#E24B4A";
const BAY_COLORS_LIGHT = ["#3B8BD4","#1D9E75","#EF9F27","#D85A30","#7F77DD","#D4537E"];
const BAY_COLORS_DARK  = ["#B5D4F4","#9FE1CB","#FAC775","#F5C4B3","#CECBF6","#F4C0D1"];

// ─── Props ────────────────────────────────────────────────────────────────────

export interface WarehouseVisualizerProps {
  warehousePoints?: [number, number][];
  obstacles?: Obstacle[];
  bayTypes?: BayType[];
  solution?: PlacedBay[];
  ceiling?: CeilingZone[];
  darkMode?: boolean;
}

// ─── Component ────────────────────────────────────────────────────────────────

export default function WarehouseVisualizer({
  warehousePoints = DEFAULT_WAREHOUSE_POINTS,
  obstacles       = DEFAULT_OBSTACLES,
  bayTypes        = DEFAULT_BAY_TYPES,
  solution        = DEFAULT_SOLUTION,
  ceiling         = DEFAULT_CEILING,
  darkMode,
}: WarehouseVisualizerProps) {
  const canvasRef   = useRef<HTMLCanvasElement>(null);
  const wrapRef     = useRef<HTMLDivElement>(null);
  const [view, setView]           = useState<"2d" | "3d">("2d");
  const [hovered, setHovered]     = useState<HoverTarget | null>(null);
  const [tooltip, setTooltip]     = useState<{ html: string; x: number; y: number } | null>(null);

  // 3D drag state (refs to avoid re-renders)
  const rotX      = useRef(30);
  const rotY      = useRef(-40);
  const dragging  = useRef(false);
  const lastPos   = useRef({ x: 0, y: 0 });
  const hoveredRef = useRef<HoverTarget | null>(null);

  const isDark = useCallback(() =>
    darkMode ?? matchMedia("(prefers-color-scheme: dark)").matches, [darkMode]);

  const bayColor = useCallback((id: number) =>
    (isDark() ? BAY_COLORS_DARK : BAY_COLORS_LIGHT)[id % 6], [isDark]);

  // ── Polygon area ────────────────────────────────────────────────────────────
  const polyArea = useCallback(() => {
    let a = 0;
    for (let i = 0; i < warehousePoints.length; i++) {
      const j = (i + 1) % warehousePoints.length;
      a += warehousePoints[i][0] * warehousePoints[j][1] - warehousePoints[j][0] * warehousePoints[i][1];
    }
    return Math.abs(a / 2);
  }, [warehousePoints]);

  // ── 2D helpers ──────────────────────────────────────────────────────────────
  const getScale = useCallback((cw: number, ch: number) => {
    const margin = 40;
    return Math.min((cw - margin * 2) / WW, (ch - margin * 2) / WH);
  }, []);

  const t2d = useCallback((x: number, y: number, W: number, H: number, scale: number) => {
    const margin = 40;
    return [margin + x * scale, H - margin - y * scale] as [number, number];
  }, []);

  // ── 3D helpers ──────────────────────────────────────────────────────────────
  const project3d = useCallback((x: number, y: number, z: number, W: number, H: number) => {
    const cx = W / 2, cy = H / 2;
    const rx = rotX.current * Math.PI / 180;
    const ry = rotY.current * Math.PI / 180;
    const scale3d = Math.min(W, H) / 16000;
    const xc = x - WW / 2, yc = y - WH / 2;
    const x1 = xc * Math.cos(ry) - yc * Math.sin(ry);
    const y1 = xc * Math.sin(ry) + yc * Math.cos(ry);
    const y2 = y1 * Math.cos(rx) - z * Math.sin(rx);
    const z2 = y1 * Math.sin(rx) + z * Math.cos(rx);
    return [cx + x1 * scale3d, cy - z2 * scale3d] as [number, number];
  }, []);

  // ── Draw 2D ─────────────────────────────────────────────────────────────────
  const draw2d = useCallback((ctx: CanvasRenderingContext2D, W: number, H: number) => {
    const scale = getScale(W, H);
    const t = (x: number, y: number) => t2d(x, y, W, H, scale);
    const dark = isDark();

    ctx.clearRect(0, 0, W, H);

    // Warehouse fill
    ctx.beginPath();
    warehousePoints.forEach(([px, py], i) => {
      const [cx, cy] = t(px, py);
      i === 0 ? ctx.moveTo(cx, cy) : ctx.lineTo(cx, cy);
    });
    ctx.closePath();
    ctx.fillStyle = dark ? "rgba(44,44,42,0.3)" : "rgba(241,239,232,0.4)";
    ctx.fill();

    // Ceiling zones
    const ceilColors = ["rgba(211,173,80,0.08)", "rgba(59,139,212,0.08)", "rgba(211,173,80,0.08)"];
    const ceilZones: [number, number][] = [[0, 3000], [3000, 6000], [6000, 10000]];
    ceilZones.forEach(([x1, x2], i) => {
      const [tx1] = t(x1, 0);
      const [tx2] = t(x2, 0);
      const [, tyBot] = t(x1, WH);
      const [, tyTop] = t(x1, 0);
      ctx.fillStyle = ceilColors[i];
      ctx.fillRect(tx1, tyBot, tx2 - tx1, tyTop - tyBot);
      const [lx, ly] = t((x1 + x2) / 2, WH - 200);
      ctx.fillStyle = dark ? "rgba(200,200,180,0.4)" : "rgba(100,100,80,0.4)";
      ctx.font = "10px sans-serif";
      ctx.textAlign = "center";
      ctx.fillText(`H:${ceiling[i]?.height ?? "?"}mm`, lx, ly);
    });

    // Warehouse outline
    ctx.strokeStyle = dark ? "#888780" : "#444441";
    ctx.lineWidth = 2;
    ctx.beginPath();
    warehousePoints.forEach(([px, py], i) => {
      const [cx, cy] = t(px, py);
      i === 0 ? ctx.moveTo(cx, cy) : ctx.lineTo(cx, cy);
    });
    ctx.closePath();
    ctx.stroke();

    // Grid
    ctx.strokeStyle = dark ? "rgba(255,255,255,0.04)" : "rgba(0,0,0,0.05)";
    ctx.lineWidth = 0.5;
    for (let gx = 0; gx <= WW; gx += 1000) {
      const [ax, ay] = t(gx, 0); const [, by] = t(gx, WH);
      ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(ax, by); ctx.stroke();
    }
    for (let gy = 0; gy <= WH; gy += 1000) {
      const [ax, ay] = t(0, gy); const [bx] = t(WW, gy);
      ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, ay); ctx.stroke();
    }

    // Obstacles
    obstacles.forEach((obs, i) => {
      const [bx, by] = t(obs.x, obs.y + obs.h);
      const sw = obs.w * scale, sh = obs.h * scale;
      const isHov = hoveredRef.current?.type === "obs" && hoveredRef.current.i === i;
      ctx.fillStyle = dark
        ? `rgba(226,75,74,${isHov ? 0.55 : 0.35})`
        : `rgba(226,75,74,${isHov ? 0.42 : 0.22})`;
      ctx.strokeStyle = OBS_COLOR;
      ctx.lineWidth = isHov ? 2 : 1;
      ctx.beginPath(); ctx.rect(bx, by, sw, sh); ctx.fill(); ctx.stroke();
      ctx.save(); ctx.clip();
      ctx.strokeStyle = dark ? "rgba(226,75,74,0.3)" : "rgba(226,75,74,0.2)";
      ctx.lineWidth = 0.5;
      for (let k = -sh; k < sw + sh; k += 8) {
        ctx.beginPath(); ctx.moveTo(bx + k, by); ctx.lineTo(bx + k + sh, by + sh); ctx.stroke();
      }
      ctx.restore();
      ctx.fillStyle = OBS_COLOR;
      ctx.font = "10px sans-serif"; ctx.textAlign = "center";
      ctx.fillText(obs.label, bx + sw / 2, by + sh / 2 + 4);
    });

    // Bays
    solution.forEach((sol, i) => {
      const bt = bayTypes.find(b => b.id === sol.id);
      if (!bt) return;
      let bw = bt.width, bd = bt.depth;
      if (sol.rot === 0) { [bw, bd] = [bd, bw]; }
      const [bx, by] = t(sol.x, sol.y + bd);
      const sw = bw * scale, sh = bd * scale;
      const isHov = hoveredRef.current?.type === "bay" && hoveredRef.current.i === i;
      const col = bayColor(sol.id);
      ctx.globalAlpha = isHov ? 1 : 0.85;
      ctx.fillStyle = dark ? col + "55" : col + "33";
      ctx.strokeStyle = col;
      ctx.lineWidth = isHov ? 2 : 1;
      ctx.beginPath();
      ctx.roundRect(bx, by, sw, sh, 2);
      ctx.fill(); ctx.stroke();
      ctx.strokeStyle = col + "aa"; ctx.lineWidth = 0.5;
      const nLevels = Math.min(bt.levels, 4);
      for (let l = 1; l < nLevels; l++) {
        const ly = by + (l * sh) / nLevels;
        ctx.beginPath(); ctx.moveTo(bx + 2, ly); ctx.lineTo(bx + sw - 2, ly); ctx.stroke();
      }
      ctx.globalAlpha = 1;
      if (sw > 18 && sh > 12) {
        ctx.fillStyle = col;
        ctx.font = `${Math.max(8, Math.min(11, sw / 5))}px sans-serif`;
        ctx.textAlign = "center";
        ctx.fillText(`T${sol.id}`, bx + sw / 2, by + sh / 2 + 3);
      }
    });

    // Scale bar
    const scaleLen = 2000 * scale;
    const sx = 40, sy = H - 15;
    ctx.strokeStyle = dark ? "#888" : "#555"; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(sx, sy); ctx.lineTo(sx + scaleLen, sy); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(sx, sy - 3); ctx.lineTo(sx, sy + 3); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(sx + scaleLen, sy - 3); ctx.lineTo(sx + scaleLen, sy + 3); ctx.stroke();
    ctx.fillStyle = dark ? "#aaa" : "#555";
    ctx.font = "10px sans-serif"; ctx.textAlign = "center";
    ctx.fillText("2m", sx + scaleLen / 2, sy - 4);
  }, [warehousePoints, obstacles, solution, bayTypes, ceiling, getScale, t2d, isDark, bayColor]);

  // ── Draw 3D ─────────────────────────────────────────────────────────────────
  const draw3d = useCallback((ctx: CanvasRenderingContext2D, W: number, H: number) => {
    const dark = isDark();
    ctx.clearRect(0, 0, W, H);
    const p = (x: number, y: number, z: number) => project3d(x, y, z, W, H);

    // Floor
    ctx.beginPath();
    [[0,0],[WW,0],[WW,WH],[0,WH]].forEach(([fx,fy], i) => {
      const [px,py] = p(fx, fy, 0);
      i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
    });
    ctx.closePath();
    ctx.fillStyle = dark ? "rgba(60,60,55,0.6)" : "rgba(230,228,220,0.6)";
    ctx.fill();
    ctx.strokeStyle = dark ? "#666" : "#999"; ctx.lineWidth = 1; ctx.stroke();

    // Grid
    ctx.strokeStyle = dark ? "rgba(255,255,255,0.04)" : "rgba(0,0,0,0.05)";
    ctx.lineWidth = 0.5;
    for (let gx = 0; gx <= WW; gx += 2000) {
      const [ax,ay] = p(gx, 0, 0), [bx,by] = p(gx, WH, 0);
      ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.stroke();
    }
    for (let gy = 0; gy <= WH; gy += 2000) {
      const [ax,ay] = p(0, gy, 0), [bx,by] = p(WW, gy, 0);
      ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.stroke();
    }

    const drawBox = (x: number, y: number, w: number, d: number, h: number, fill: string, stroke: string) => {
      const corners = [
        [x,   y,   0], [x+w, y,   0], [x+w, y+d, 0], [x,   y+d, 0],
        [x,   y,   h], [x+w, y,   h], [x+w, y+d, h], [x,   y+d, h],
      ].map(([cx,cy,cz]) => p(cx as number, cy as number, cz as number));
      const faces = [[0,1,2,3],[4,5,6,7],[0,1,5,4],[1,2,6,5],[2,3,7,6],[0,3,7,4]];
      const shades = [0.6, 1.0, 0.85, 0.7, 0.75, 0.8];
      faces.forEach((face, fi) => {
        ctx.beginPath();
        face.forEach((ci, i) => {
          const [px,py] = corners[ci];
          i === 0 ? ctx.moveTo(px, py) : ctx.lineTo(px, py);
        });
        ctx.closePath();
        ctx.fillStyle = fill;
        ctx.globalAlpha = shades[fi] * 0.85;
        ctx.fill();
        ctx.strokeStyle = stroke; ctx.lineWidth = 0.5;
        ctx.globalAlpha = 1; ctx.stroke();
      });
    };

    obstacles.forEach(obs => drawBox(obs.x, obs.y, obs.w, obs.h, 2000,
      dark ? "rgba(226,75,74,0.4)" : "rgba(226,75,74,0.35)", OBS_COLOR));

    solution.forEach(sol => {
      const bt = bayTypes.find(b => b.id === sol.id);
      if (!bt) return;
      let bw = bt.width, bd = bt.depth;
      if (sol.rot === 0) { [bw, bd] = [bd, bw]; }
      const col = bayColor(sol.id);
      drawBox(sol.x, sol.y, bw, bd, bt.height, col + "66", col);
      for (let lv = 1; lv < bt.levels; lv++) {
        const lh = (bt.height / bt.levels) * lv;
        ctx.strokeStyle = col; ctx.lineWidth = 0.5; ctx.globalAlpha = 0.4;
        ctx.beginPath();
        const [ax,ay] = p(sol.x, sol.y, lh), [bx,by] = p(sol.x + bw, sol.y, lh);
        ctx.moveTo(ax, ay); ctx.lineTo(bx, by); ctx.stroke();
        ctx.globalAlpha = 1;
      }
    });

    // Walls
    ctx.strokeStyle = dark ? "#888" : "#555"; ctx.lineWidth = 1.5;
    const wallH = 3000;
    for (let i = 0; i < warehousePoints.length; i++) {
      const [x1,y1] = warehousePoints[i];
      const [x2,y2] = warehousePoints[(i+1) % warehousePoints.length];
      const [ax,ay] = p(x1, y1, 0), [bx,by] = p(x2, y2, 0);
      const [cx,cy] = p(x1, y1, wallH), [dx,dy] = p(x2, y2, wallH);
      ctx.beginPath(); ctx.moveTo(ax, ay); ctx.lineTo(cx, cy); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(bx, by); ctx.lineTo(dx, dy); ctx.stroke();
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(dx, dy); ctx.stroke();
    }

    ctx.fillStyle = dark ? "rgba(180,180,160,0.5)" : "rgba(80,80,60,0.4)";
    ctx.font = "11px sans-serif"; ctx.textAlign = "center";
    ctx.fillText("Arrossega per rotar", W / 2, H - 8);
  }, [warehousePoints, obstacles, solution, bayTypes, project3d, isDark, bayColor]);

  // ── Main draw ───────────────────────────────────────────────────────────────
  const draw = useCallback(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    if (view === "2d") draw2d(ctx, canvas.width, canvas.height);
    else draw3d(ctx, canvas.width, canvas.height);
  }, [view, draw2d, draw3d]);

  // ── Resize ──────────────────────────────────────────────────────────────────
  const resize = useCallback(() => {
    const canvas = canvasRef.current;
    const wrap = wrapRef.current;
    if (!canvas || !wrap) return;
    canvas.width = Math.max(wrap.clientWidth, 400);
    canvas.height = view === "2d"
      ? Math.min(canvas.width * 1.05, 600)
      : Math.min(canvas.width * 0.75, 520);
    draw();
  }, [view, draw]);

  useEffect(() => {
    resize();
    window.addEventListener("resize", resize);
    return () => window.removeEventListener("resize", resize);
  }, [resize]);

  useEffect(() => { draw(); }, [hovered, draw]);

  // ── Mouse events ────────────────────────────────────────────────────────────
  const hitTest = useCallback((mx: number, my: number, W: number, H: number): HoverTarget | null => {
    const scale = getScale(W, H);
    const margin = 40;
    for (let i = 0; i < solution.length; i++) {
      const sol = solution[i];
      const bt = bayTypes.find(b => b.id === sol.id);
      if (!bt) continue;
      let bw = bt.width, bd = bt.depth;
      if (sol.rot === 0) { [bw, bd] = [bd, bw]; }
      const bx = margin + sol.x * scale;
      const by = H - margin - (sol.y + bd) * scale;
      if (mx >= bx && mx <= bx + bw * scale && my >= by && my <= by + bd * scale)
        return { type: "bay", i };
    }
    for (let i = 0; i < obstacles.length; i++) {
      const obs = obstacles[i];
      const bx = margin + obs.x * scale;
      const by = H - margin - (obs.y + obs.h) * scale;
      if (mx >= bx && mx <= bx + obs.w * scale && my >= by && my <= by + obs.h * scale)
        return { type: "obs", i };
    }
    return null;
  }, [solution, bayTypes, obstacles, getScale]);

  const onMouseMove = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    if (view === "3d") {
      if (dragging.current) {
        const dx = e.clientX - lastPos.current.x;
        const dy = e.clientY - lastPos.current.y;
        rotY.current += dx * 0.4;
        rotX.current = Math.max(-10, Math.min(70, rotX.current + dy * 0.4));
        lastPos.current = { x: e.clientX, y: e.clientY };
        draw();
      }
      return;
    }

    const rect = canvas.getBoundingClientRect();
    const mx = e.clientX - rect.left, my = e.clientY - rect.top;
    const hit = hitTest(mx, my, canvas.width, canvas.height);

    if (hit) {
      hoveredRef.current = hit;
      setHovered(hit);
      let html = "";
      if (hit.type === "bay") {
        const sol = solution[hit.i];
        const bt = bayTypes.find(b => b.id === sol.id)!;
        const rot = sol.rot === 1 ? "Vertical" : "Horitzontal";
        html = `<strong>${bt.name}</strong>X: ${sol.x}mm, Y: ${sol.y}mm<br>Rotació: ${rot}<br>Mides: ${bt.width}×${bt.depth}mm<br>Alçada: ${bt.height}mm | Nivells: ${bt.levels}<br>Capacitat: ${bt.capacity}kg`;
      } else {
        const obs = obstacles[hit.i];
        html = `<strong>${obs.label}</strong>X: ${obs.x}mm, Y: ${obs.y}mm<br>Mides: ${obs.w}×${obs.h}mm`;
      }
      setTooltip({ html, x: e.clientX + 14, y: e.clientY - 20 });
    } else {
      hoveredRef.current = null;
      setHovered(null);
      setTooltip(null);
    }
  }, [view, hitTest, solution, bayTypes, obstacles, draw]);

  const onMouseDown = useCallback((e: React.MouseEvent<HTMLCanvasElement>) => {
    if (view === "3d") { dragging.current = true; lastPos.current = { x: e.clientX, y: e.clientY }; }
  }, [view]);

  const onMouseUp = useCallback(() => { dragging.current = false; }, []);
  const onMouseLeave = useCallback(() => {
    dragging.current = false;
    hoveredRef.current = null;
    setHovered(null);
    setTooltip(null);
  }, []);

  // ── Stats ───────────────────────────────────────────────────────────────────
  const stats = [
    { label: "Estanteries",     value: solution.length },
    { label: "Capacitat total", value: solution.reduce((s, sol) => s + (bayTypes.find(b => b.id === sol.id)?.capacity ?? 0), 0).toLocaleString() + " kg" },
    { label: "Àrea magatzem",   value: (polyArea() / 1e6).toFixed(1) + " m²" },
    { label: "Àrea estanteries",value: (solution.reduce((s, sol) => {
        const bt = bayTypes.find(b => b.id === sol.id);
        return s + (bt ? bt.width * bt.depth : 0);
      }, 0) / 1e6).toFixed(1) + " m²" },
    { label: "Ocupació",        value: (() => {
        const ba = solution.reduce((s, sol) => {
          const bt = bayTypes.find(b => b.id === sol.id);
          return s + (bt ? bt.width * bt.depth : 0);
        }, 0);
        return (ba / polyArea() * 100).toFixed(1) + "%";
      })() },
    { label: "Tipus usats",     value: [...new Set(solution.map(s => s.id))].length },
  ];

  const usedTypes = [...new Set(solution.map(s => s.id))].sort();

  return (
    <div style={{ display: "flex", flexDirection: "column", fontFamily: "sans-serif", fontSize: 14 }}>
      {/* Toolbar */}
      <div style={{ display: "flex", alignItems: "center", gap: 8, padding: "10px 12px", borderBottom: "0.5px solid #ccc", flexWrap: "wrap" }}>
        <span style={{ fontWeight: 500, fontSize: 15, marginRight: 8 }}>Visualitzador de Magatzem</span>
        {(["2d", "3d"] as const).map(v => (
          <button
            key={v}
            onClick={() => setView(v)}
            style={{
              padding: "4px 12px", borderRadius: 6,
              border: "0.5px solid #999",
              background: view === v ? "#eee" : "transparent",
              cursor: "pointer", fontSize: 13,
              color: view === v ? "#222" : "#666",
            }}
          >
            Vista {v.toUpperCase()}
          </button>
        ))}
      </div>

      {/* Legend */}
      <div style={{ display: "flex", gap: 12, flexWrap: "wrap", padding: "8px 12px", borderBottom: "0.5px solid #ddd", fontSize: 12, color: "#666" }}>
        {usedTypes.map(id => (
          <span key={id} style={{ display: "flex", alignItems: "center", gap: 5 }}>
            <span style={{ width: 12, height: 12, borderRadius: 2, background: bayColor(id), border: "0.5px solid rgba(0,0,0,.15)", display: "inline-block" }} />
            {bayTypes.find(b => b.id === id)?.name} ({bayTypes.find(b => b.id === id)?.width}×{bayTypes.find(b => b.id === id)?.depth}mm)
          </span>
        ))}
        <span style={{ display: "flex", alignItems: "center", gap: 5 }}>
          <span style={{ width: 12, height: 12, borderRadius: 2, background: OBS_COLOR, display: "inline-block" }} />
          Obstacles
        </span>
      </div>

      {/* Canvas */}
      <div ref={wrapRef} style={{ overflow: "auto", background: "#f5f4ef" }}>
        <canvas
          ref={canvasRef}
          style={{ display: "block", cursor: view === "3d" ? "grab" : "default" }}
          onMouseMove={onMouseMove}
          onMouseDown={onMouseDown}
          onMouseUp={onMouseUp}
          onMouseLeave={onMouseLeave}
        />
      </div>

      {/* Stats */}
      <div style={{ padding: 12, borderTop: "0.5px solid #ddd" }}>
        <p style={{ fontSize: 13, fontWeight: 500, color: "#666", marginBottom: 8 }}>Resum</p>
        <div style={{ display: "grid", gridTemplateColumns: "repeat(auto-fit, minmax(120px, 1fr))", gap: 8 }}>
          {stats.map(s => (
            <div key={s.label} style={{ background: "#f0efe9", borderRadius: 6, padding: "8px 10px" }}>
              <div style={{ fontSize: 11, color: "#888" }}>{s.label}</div>
              <div style={{ fontSize: 18, fontWeight: 500 }}>{s.value}</div>
            </div>
          ))}
        </div>
      </div>

      {/* Tooltip */}
      {tooltip && (
        <div
          style={{
            position: "fixed", pointerEvents: "none",
            background: "#fff", border: "0.5px solid #ccc",
            borderRadius: 6, padding: "6px 10px", fontSize: 12,
            zIndex: 9999, boxShadow: "0 2px 8px rgba(0,0,0,.08)",
            maxWidth: 200, left: tooltip.x, top: tooltip.y,
          }}
          dangerouslySetInnerHTML={{ __html: tooltip.html }}
        />
      )}
    </div>
  );
}