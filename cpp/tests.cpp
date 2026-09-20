// tests.cpp
// Run with:  make run_tests && ./run_tests
// Each test calls check(). It prints PASS or FAIL and I count them at the end.
#include <iostream>
#include "network.h"
#include "routing.h"
#include "simulation.h"

int passed = 0, failed = 0;

void check(bool ok, const string& name) {
    if (ok) { passed++; cout << "PASS  " << name << endl; }
    else    { failed++; cout << "FAIL  " << name << endl; }
}

bool closeEnough(double a, double b) { return fabs(a - b) < 1e-6 * max(1.0, fabs(b)); }

// A slow but simple shortest path (Bellman-Ford) to compare Dijkstra and A* against.
// It works in a completely different way, so it can catch mistakes in both.
vector<double> bellmanFord(const Network& net, int src, bool freeFlow) {
    vector<double> dist(net.numNodes(), INF);
    dist[src] = 0;
    for (int round = 0; round < net.numNodes(); round++) {
        bool changed = false;
        for (const Road& r : net.roads) {
            if (dist[r.from] + roadWeight(r, freeFlow) < dist[r.to] - 1e-12) {
                dist[r.to] = dist[r.from] + roadWeight(r, freeFlow);
                changed = true;
            }
        }
        if (!changed) break;
    }
    return dist;
}

Config smallConfig() {
    Config c;
    c.rows = 8; c.cols = 8; c.seed = 5;
    c.vehicles = 200; c.spawnWindow = 150; c.incidentDuration = 100;
    return c;
}

// ---------- city ----------

void testCity() {
    Network net = makeCity(12, 12, 0);
    check(net.isConnected(), "city is connected");
    check(net.numNodes() == 144 && net.numRoads() >= 200, "12x12 city has 144 intersections and 200+ roads");
    check(makeCity(12, 12, 0).numRoads() == net.numRoads(), "same seed gives the same city");
}

void testRandomHelpers() {
    Random rng(1);
    bool ok = true;
    for (int i = 0; i < 10000; i++) {
        int k = rng.integer(3, 7);
        double t = rng.triangular(0, 600, 240);
        if (k < 3 || k > 7 || t < 0 || t > 600) ok = false;
    }
    check(ok, "random numbers stay inside their range");
}

// ---------- routing ----------

void testRouting() {
    Network net = makeCity(10, 10, 3);
    addRandomLoad(net, 1);      // different load on every road
    Random rng(7);

    bool dijkstraOk = true, dijkstraFreeOk = true, astarOk = true;
    long long expandedD = 0, expandedA = 0;
    for (int i = 0; i < 40; i++) {
        int s = rng.integer(0, 99), t = rng.integer(0, 99);
        if (s == t) continue;
        RouteResult d = dijkstra(net, s, t), a = astar(net, s, t);
        RouteResult dFree = dijkstra(net, s, t, true);
        if (!closeEnough(d.cost, bellmanFord(net, s, false)[t])) dijkstraOk = false;
        if (!closeEnough(dFree.cost, bellmanFord(net, s, true)[t])) dijkstraFreeOk = false;
        if (!closeEnough(a.cost, d.cost)) astarOk = false;
        expandedD += d.expanded;
        expandedA += a.expanded;
    }
    check(dijkstraOk, "Dijkstra matches Bellman-Ford (with traffic)");
    check(dijkstraFreeOk, "Dijkstra matches Bellman-Ford (empty roads)");
    check(astarOk, "A* finds the same cost as Dijkstra");
    check(expandedA <= expandedD, "A* expands no more nodes than Dijkstra");

    RouteResult r = dijkstra(net, 0, 99);
    bool pathOk = r.path.front() == 0 && r.path.back() == 99 && closeEnough(pathCost(net, r.path), r.cost);
    for (size_t i = 0; i + 1 < r.path.size(); i++)
        if (net.findRoad(r.path[i], r.path[i + 1]) < 0) pathOk = false;
    check(pathOk, "path uses real roads and its cost matches");

    Network small = makeCity(3, 3, 0);
    int lonely = small.addNode(0, 0);       // an intersection with no roads
    RouteResult none = dijkstra(small, 0, lonely);
    check(!none.found && none.cost == INF, "unreachable intersection gives no path");
}

// ---------- simulation ----------

void testEveryStrategy(Strategy s) {
    Simulation sim(smallConfig(), s);
    sim.run();
    int loadLeft = 0;
    for (const Road& r : sim.net.roads) loadLeft += r.load;
    check(sim.finishedCount == 200 && sim.active.empty(),
          "every car finishes (" + strategyName(s) + ")");
    check(loadLeft == 0, "every car that entered a road also left it (" + strategyName(s) + ")");
}

void testSimulation() {
    testEveryStrategy(STATIC);
    testEveryStrategy(ADAPTIVE);
    testEveryStrategy(DYNAMIC);

    Simulation a(smallConfig(), DYNAMIC), b(smallConfig(), DYNAMIC);
    a.run(); b.run();
    check(a.results() == b.results(), "same seed gives the same result");

    Simulation s1(smallConfig(), STATIC), s2(smallConfig(), ADAPTIVE), s3(smallConfig(), DYNAMIC);
    bool same = true;
    for (size_t i = 0; i < s1.vehicles.size(); i++) {
        const Vehicle &x = s1.vehicles[i], &y = s2.vehicles[i], &z = s3.vehicles[i];
        if (x.origin != y.origin || x.origin != z.origin || x.dest != y.dest ||
            x.dest != z.dest || x.spawnTick != y.spawnTick || x.spawnTick != z.spawnTick) same = false;
    }
    check(same, "all strategies get the same trips");

    bool tripsOk = true;
    for (const Vehicle& v : s1.vehicles)
        if (v.origin == v.dest || v.spawnTick < 0 || v.spawnTick > 150) tripsOk = false;
    check(tripsOk, "trips have different start and end, and spawn inside the window");

    Config heavy;
    heavy.rows = 10; heavy.cols = 10; heavy.seed = 2; heavy.vehicles = 700; heavy.spawnWindow = 300;
    Simulation st(heavy, STATIC), dy(heavy, DYNAMIC);
    st.run(); dy.run();
    int stReroutes = 0, dyReroutes = 0;
    for (const Vehicle& v : st.vehicles) stReroutes += v.reroutes;
    for (const Vehicle& v : dy.vehicles) dyReroutes += v.reroutes;
    check(stReroutes == 0, "static never changes route");
    check(dyReroutes > 0, "dynamic changes route when traffic is heavy");

    // an accident makes a road 25x slower and then goes away again
    Simulation inc(smallConfig(), STATIC);
    const Incident first = inc.incidents[0];
    while (inc.tick <= first.start) inc.step();
    bool slow = inc.net.roads[first.roadA].slowdown == BLOCKED_SLOWDOWN &&
                inc.net.roads[first.roadB].slowdown == BLOCKED_SLOWDOWN;
    while (inc.tick <= first.end) inc.step();
    bool cleared = inc.net.roads[first.roadA].slowdown == 1.0;
    check(slow && cleared, "accident slows the road down and then clears");
}

int main() {
    testCity();
    testRandomHelpers();
    testRouting();
    testSimulation();
    cout << "\n" << passed << " passed, " << failed << " failed" << endl;
    return failed == 0 ? 0 : 1;
}
