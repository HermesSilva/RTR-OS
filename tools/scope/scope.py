#!/usr/bin/env python3
"""RTR-OS - virtual oscilloscope for the emulator.

Reads the capture log written by the rtr-scope device of qemu-pi4 (one
"<ns> <pin> <level>" line per transition, virtual time) and produces an
HTML page with the waveforms, pan and zoom, cursors and measurements:
frequency, period, duty cycle and period jitter per pin.

Usage: scope.py <capture log> <output html> [pin ...]
"""
import json
import statistics
import sys


def load(path):
    pins = {}
    with open(path, encoding="ascii", errors="replace") as f:
        for line in f:
            parts = line.split()
            if len(parts) != 3:
                continue
            try:
                t, pin, level = int(parts[0]), int(parts[1]), int(parts[2])
            except ValueError:
                continue
            pins.setdefault(pin, []).append((t, level))
    return pins


def measure(edges):
    rises = [t for t, level in edges if level]
    falls = [t for t, level in edges if not level]
    periods = [b - a for a, b in zip(rises, rises[1:])]
    highs = []
    for r in rises:
        nxt = next((f for f in falls if f > r), None)
        if nxt is not None:
            highs.append(nxt - r)
    m = {"transitions": len(edges), "rising": len(rises)}
    if periods:
        avg = statistics.fmean(periods)
        m.update({
            "period_ns": avg,
            "frequency_hz": 1e9 / avg if avg else 0,
            "period_min_ns": min(periods),
            "period_max_ns": max(periods),
            "period_jitter_ns": max(periods) - min(periods),
            "period_stdev_ns": statistics.pstdev(periods) if len(periods) > 1 else 0,
        })
    if highs and periods:
        m["high_ns"] = statistics.fmean(highs)
        m["duty_percent"] = 100 * statistics.fmean(highs) / statistics.fmean(periods)
        m["high_min_ns"] = min(highs)
        m["high_max_ns"] = max(highs)
    return m


PAGE = r"""<!DOCTYPE html>
<html lang="en"><head><meta charset="utf-8"><title>RTR-OS — virtual oscilloscope</title>
<style>
:root { --bg:#0e1116; --card:#171b22; --ink:#e6e9ee; --dim:#9aa4b2; --line:#2b323c; --trace:#6cb6ff; --cursor:#ffb62b; --grid:#1f252e; }
body { margin:0; background:var(--bg); color:var(--ink); font:14px/1.4 system-ui, sans-serif; }
header { padding:12px 20px; border-bottom:1px solid var(--line); background:var(--card); display:flex; gap:20px; align-items:center; flex-wrap:wrap; }
h1 { font-size:18px; margin:0; } h1 small { color:var(--dim); font-weight:400; font-size:12px; margin-left:8px; }
main { padding:16px 20px; }
canvas { width:100%; height:360px; background:var(--card); border:1px solid var(--line); border-radius:8px; display:block; cursor:crosshair; }
.hint { color:var(--dim); font-size:12px; margin:6px 0 14px; }
table { border-collapse:collapse; font-size:13px; } th,td { padding:5px 12px; border-bottom:1px solid var(--line); text-align:right; font-variant-numeric:tabular-nums; }
th:first-child, td:first-child { text-align:left; } th { color:var(--dim); font-weight:600; }
#readout { font-family:ui-monospace, Consolas, monospace; color:var(--cursor); margin-left:auto; }
button { background:var(--card); color:var(--ink); border:1px solid var(--line); border-radius:6px; padding:5px 12px; font:inherit; cursor:pointer; }
</style></head><body>
<header><h1>RTR-OS <small>virtual oscilloscope · virtual time of the emulator</small></h1>
<button id="fit">Fit</button><button id="zin">Zoom +</button><button id="zout">Zoom −</button>
<span id="readout"></span></header>
<main>
<canvas id="scope"></canvas>
<p class="hint">Drag to pan, wheel to zoom, click to place cursor A, shift-click for cursor B. Levels come from the GPIO controller model; time is the emulator's virtual clock, so widths are logically exact but say nothing about the real board.</p>
<table><thead><tr><th>Pin</th><th>Transitions</th><th>Frequency</th><th>Period</th><th>Duty</th><th>High time</th><th>Period min / max</th><th>Period jitter (p-p)</th><th>Period σ</th></tr></thead>
<tbody id="rows"></tbody></table>
</main>
<script>
const DATA = __DATA__;
const pins = Object.keys(DATA.pins).map(Number).sort((a,b)=>a-b);
const t0 = DATA.t0, t1 = DATA.t1;
let view = [t0, Math.min(t1, t0 + 10e6)], cursors = [null, null], drag = null;   // start on the first 10 ms
const canvas = document.getElementById("scope"), ctx = canvas.getContext("2d");
const fmt = (ns) => { const a = Math.abs(ns); if (a < 1e3) return ns.toFixed(0)+" ns"; if (a < 1e6) return (ns/1e3).toFixed(2)+" µs"; if (a < 1e9) return (ns/1e6).toFixed(3)+" ms"; return (ns/1e9).toFixed(4)+" s"; };
const hz = (f) => f >= 1e6 ? (f/1e6).toFixed(3)+" MHz" : f >= 1e3 ? (f/1e3).toFixed(3)+" kHz" : f.toFixed(2)+" Hz";
function levelAt(edges, t) { let lo = 0, hi = edges.length; while (lo < hi) { const m = (lo+hi)>>1; if (edges[m][0] <= t) lo = m+1; else hi = m; } return lo ? edges[lo-1][1] : 0; }
function draw() {
  const W = canvas.width = canvas.clientWidth * devicePixelRatio, H = canvas.height = canvas.clientHeight * devicePixelRatio;
  ctx.clearRect(0,0,W,H);
  const [a,b] = view, span = b - a, x = (t) => (t - a) / span * W;
  ctx.strokeStyle = getComputedStyle(document.body).getPropertyValue("--grid"); ctx.lineWidth = 1;
  let step = Math.pow(10, Math.floor(Math.log10(span/10)));
  for (const k of [1, 2, 5, 10, 20, 50]) { if (step * k / span * canvas.clientWidth >= 90) { step *= k; break; } }
  for (let t = Math.ceil(a/step)*step; t <= b; t += step) { ctx.beginPath(); ctx.moveTo(x(t),0); ctx.lineTo(x(t),H); ctx.stroke(); }
  ctx.fillStyle = "#9aa4b2"; ctx.font = (11*devicePixelRatio)+"px system-ui";
  for (let t = Math.ceil(a/step)*step; t <= b; t += step) ctx.fillText(fmt(t - t0), x(t)+3*devicePixelRatio, H-4*devicePixelRatio);
  const lane = H / Math.max(1, pins.length);
  pins.forEach((pin, i) => {
    const edges = DATA.pins[pin], top = i*lane + lane*0.2, bottom = i*lane + lane*0.8;
    ctx.strokeStyle = "#6cb6ff"; ctx.lineWidth = 2*devicePixelRatio; ctx.beginPath();
    let lvl = levelAt(edges, a), px = 0, py = lvl ? top : bottom; ctx.moveTo(px, py);
    for (const [t, l] of edges) { if (t < a) continue; if (t > b) break; const nx = x(t); ctx.lineTo(nx, py); py = l ? top : bottom; ctx.lineTo(nx, py); }
    ctx.lineTo(W, py); ctx.stroke();
    ctx.fillStyle = "#e6e9ee"; ctx.fillText("GPIO " + pin, 8*devicePixelRatio, i*lane + 14*devicePixelRatio);
  });
  cursors.forEach((c, i) => { if (c === null) return; ctx.strokeStyle = "#ffb62b"; ctx.lineWidth = 1*devicePixelRatio; ctx.setLineDash([6,4]); ctx.beginPath(); ctx.moveTo(x(c),0); ctx.lineTo(x(c),H); ctx.stroke(); ctx.setLineDash([]); ctx.fillStyle = "#ffb62b"; ctx.fillText(i ? "B" : "A", x(c)+4*devicePixelRatio, 14*devicePixelRatio); });
  const r = document.getElementById("readout");
  if (cursors[0] !== null && cursors[1] !== null) { const d = Math.abs(cursors[1]-cursors[0]); r.textContent = "A→B " + fmt(d) + (d ? " · " + hz(1e9/d) : ""); }
  else if (cursors[0] !== null) r.textContent = "A at " + fmt(cursors[0] - t0); else r.textContent = "window " + fmt(span);
}
const tAt = (e) => { const r = canvas.getBoundingClientRect(); return view[0] + (e.clientX - r.left) / r.width * (view[1]-view[0]); };
canvas.addEventListener("wheel", (e) => { e.preventDefault(); const t = tAt(e), f = e.deltaY > 0 ? 1.25 : 0.8; view = [t - (t-view[0])*f, t + (view[1]-t)*f]; draw(); });
canvas.addEventListener("mousedown", (e) => { drag = { x: e.clientX, view: [...view], moved: false }; });
window.addEventListener("mousemove", (e) => { if (!drag) return; const r = canvas.getBoundingClientRect(); const dt = (e.clientX - drag.x) / r.width * (drag.view[1]-drag.view[0]); if (Math.abs(e.clientX - drag.x) > 3) drag.moved = true; view = [drag.view[0]-dt, drag.view[1]-dt]; draw(); });
window.addEventListener("mouseup", (e) => { if (drag && !drag.moved) { cursors[e.shiftKey ? 1 : 0] = tAt(e); draw(); } drag = null; });
document.getElementById("fit").onclick = () => { view = [t0, t1]; draw(); };
document.getElementById("zin").onclick = () => { const m = (view[0]+view[1])/2, s = (view[1]-view[0])*0.4; view = [m-s, m+s]; draw(); };
document.getElementById("zout").onclick = () => { const m = (view[0]+view[1])/2, s = (view[1]-view[0])*0.625; view = [m-s, m+s]; draw(); };
window.addEventListener("resize", draw);
document.getElementById("rows").innerHTML = pins.map((p) => { const m = DATA.measure[p]; const g = (k, f) => (k in m ? f(m[k]) : "—");
  return "<tr><td>GPIO " + p + "</td><td>" + m.transitions + "</td><td>" + g("frequency_hz", hz) + "</td><td>" + g("period_ns", fmt) + "</td><td>" + g("duty_percent", (v) => v.toFixed(1) + " %") + "</td><td>" + g("high_ns", fmt) + "</td><td>" + (("period_min_ns" in m) ? fmt(m.period_min_ns) + " / " + fmt(m.period_max_ns) : "—") + "</td><td>" + g("period_jitter_ns", fmt) + "</td><td>" + g("period_stdev_ns", fmt) + "</td></tr>"; }).join("");
draw();
</script></body></html>
"""


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    pins = load(sys.argv[1])
    wanted = [int(p) for p in sys.argv[3:]] or sorted(pins)
    pins = {p: pins[p] for p in wanted if p in pins}
    if not pins:
        print("no transitions captured")
        return 1
    times = [t for edges in pins.values() for t, _ in edges]
    data = {
        "t0": min(times), "t1": max(times),
        "pins": {str(p): e for p, e in pins.items()},
        "measure": {str(p): measure(e) for p, e in pins.items()},
    }
    with open(sys.argv[2], "w", encoding="utf-8") as f:
        f.write(PAGE.replace("__DATA__", json.dumps(data)))
    for p, m in data["measure"].items():
        if "frequency_hz" in m:
            print(f"GPIO {p}: {m['transitions']} transitions, {m['frequency_hz']:.2f} Hz, "
                  f"duty {m.get('duty_percent', 0):.1f} %, period jitter {m['period_jitter_ns']} ns")
        else:
            print(f"GPIO {p}: {m['transitions']} transitions")
    print(f"page: {sys.argv[2]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
