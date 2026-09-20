# test_server.py
# Python tests. The C++ tests (run_tests) check the simulation itself, these check
#  1. that the C++ routes agree with NetworkX (a well known graph library)
#  2. that the web API works
# Build the C++ program first with:  make
import json
import random
import subprocess

import networkx as nx
import pytest
from fastapi.testclient import TestClient

import server

client = TestClient(server.app)


def engine(*args):
    done = subprocess.run([str(server.ENGINE), *map(str, args)], capture_output=True, text=True)
    return json.loads(done.stdout)


# ---------- C++ routing vs NetworkX ----------

@pytest.mark.parametrize("free_flow", [False, True])
def test_cpp_routes_match_networkx(free_flow):
    city_args = ["--rows", 10, "--cols", 10, "--seed", 3, "--load-seed", 1]
    city = engine("city", *city_args)

    g = nx.DiGraph()
    for u, v, empty_time, current_time in city["roads"]:
        g.add_edge(u, v, weight=empty_time if free_flow else current_time)

    rng = random.Random(7)
    for _ in range(25):
        s, t = rng.sample(range(city["nodes"]), 2)
        expected = nx.shortest_path_length(g, s, t, weight="weight")
        for algo in ("dijkstra", "astar"):
            extra = ["--free-flow"] if free_flow else []
            got = engine("route", *city_args, "--src", s, "--dst", t, "--algo", algo, *extra)
            assert got["travel_time_s"] == pytest.approx(expected, abs=1e-3)


# ---------- REST API ----------

def test_health():
    assert client.get("/api/health").json() == {"status": "ok"}


def test_route_returns_valid_path():
    res = client.get("/api/route", params={"src": 0, "dst": 143})
    assert res.status_code == 200
    body = res.json()
    assert body["path"][0] == 0 and body["path"][-1] == 143
    assert body["travel_time_s"] > 0


def test_astar_and_dijkstra_agree():
    a = client.get("/api/route", params={"src": 5, "dst": 120, "algo": "astar"}).json()
    d = client.get("/api/route", params={"src": 5, "dst": 120, "algo": "dijkstra"}).json()
    assert a["travel_time_s"] == d["travel_time_s"]
    assert a["nodes_expanded"] <= d["nodes_expanded"]


def test_route_rejects_bad_input():
    assert client.get("/api/route", params={"src": 0, "dst": 9999}).status_code == 404
    assert client.get("/api/route", params={"src": 3, "dst": 3}).status_code == 422
    assert client.get("/api/route", params={"src": 0, "dst": 5, "algo": "bfs"}).status_code == 422


# ---------- WebSocket ----------

def test_websocket_sends_layout_frames_and_done(monkeypatch):
    monkeypatch.setattr(server, "FRAME_DELAY", 0)
    with client.websocket_connect("/ws/simulate?strategy=dynamic&seed=1&vehicles=50") as ws:
        layout = ws.receive_json()
        assert layout["type"] == "layout"
        assert len(layout["nodes"]) == 144

        kinds = set()
        while True:
            msg = ws.receive_json()
            kinds.add(msg["type"])
            if msg["type"] == "frame":
                assert len(msg["congestion"]) == len(layout["edges"])
                assert len(msg["cars"]) == msg["active"]
            if msg["type"] == "done":
                break
        assert kinds == {"frame", "done"}
        assert msg["results"]["completed"] == 50


def test_websocket_rejects_unknown_strategy():
    with client.websocket_connect("/ws/simulate?strategy=teleport") as ws:
        assert ws.receive_json()["type"] == "error"
