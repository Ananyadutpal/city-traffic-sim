# server.py
# The web server. It does not simulate anything itself, it runs the C++ program
# (./traffic) and passes the results on to the browser.
#
#   GET /api/route      fastest route between two intersections
#   GET /api/benchmark  saved benchmark results
#   WS  /ws/simulate    live simulation stream
#   GET /               demo page
#
# Run with:  uvicorn server:app --reload
import asyncio
import json
import os
import subprocess
from pathlib import Path

from fastapi import FastAPI, HTTPException, Query, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

HERE = Path(__file__).parent
ENGINE = HERE / ("traffic.exe" if os.name == "nt" else "traffic")
FRAME_DELAY = 0.04   # real seconds between two frames sent to the browser

app = FastAPI(title="City Traffic Network Simulator")
app.mount("/static", StaticFiles(directory=HERE / "static"), name="static")


def run_engine(*args):
    """Run the C++ program once and return its JSON output."""
    if not ENGINE.exists():
        raise HTTPException(status_code=500, detail="C++ program not built yet. Run: make")
    done = subprocess.run([str(ENGINE), *map(str, args)], capture_output=True, text=True, timeout=30)
    return json.loads(done.stdout)


@app.get("/")
def index():
    return FileResponse(HERE / "static" / "index.html")


@app.get("/api/health")
def health():
    return {"status": "ok"}


@app.get("/api/route")
def route(
    src: int = Query(..., ge=0, description="Start intersection id"),
    dst: int = Query(..., ge=0, description="End intersection id"),
    algo: str = Query("astar", pattern="^(astar|dijkstra)$"),
    seed: int = 0,
):
    """Fastest route on an empty city."""
    if src == dst:
        raise HTTPException(status_code=422, detail="src and dst must be different intersections.")
    result = run_engine("route", "--src", src, "--dst", dst, "--algo", algo, "--seed", seed, "--free-flow")
    if "error" in result:
        raise HTTPException(status_code=404, detail=result["error"])
    return {
        "algorithm": result["algorithm"],
        "path": result["path"],
        "travel_time_s": round(result["travel_time_s"], 1),
        "nodes_expanded": result["nodes_expanded"],
    }


@app.get("/api/benchmark")
def benchmark():
    file = HERE / "results" / "benchmark.json"
    if not file.exists():
        raise HTTPException(status_code=404, detail="Run `python benchmark.py` first.")
    return json.loads(file.read_text())


@app.websocket("/ws/simulate")
async def simulate(ws: WebSocket, strategy: str = "dynamic", seed: int = 0, vehicles: int = 1000):
    """Each browser connection starts its own C++ simulation and gets its output line by line."""
    await ws.accept()
    if strategy not in ("static", "adaptive", "dynamic"):
        await ws.send_json({"type": "error", "detail": f"Unknown strategy '{strategy}'."})
        await ws.close()
        return
    if not ENGINE.exists():
        await ws.send_json({"type": "error", "detail": "C++ program not built yet. Run: make"})
        await ws.close()
        return

    vehicles = max(50, min(vehicles, 2000))
    engine = subprocess.Popen(
        [str(ENGINE), "stream", "--strategy", strategy, "--seed", str(seed), "--vehicles", str(vehicles)],
        stdout=subprocess.PIPE,
        text=True,
    )
    try:
        while True:
            line = await asyncio.to_thread(engine.stdout.readline)
            if not line:
                break
            await ws.send_text(line)
            await asyncio.sleep(FRAME_DELAY)
        await ws.close()
    except WebSocketDisconnect:
        pass   # the browser tab was closed
    finally:
        engine.kill()
        engine.wait()
