// network.h
// The city is a graph. Intersections are nodes, roads are directed edges.
// Every road remembers how many cars are on it, and more cars = slower road.
// That is what makes the routing "dynamic".
#pragma once
#include <vector>
#include <queue>
#include <cmath>
#include <random>
using namespace std;

const double ALPHA = 1.5;              // how quickly congestion grows
const double BLOCKED_SLOWDOWN = 25.0;  // a road with an accident is 25x slower
const double INF = 1e18;

// ---------- random numbers ----------
// mt19937 gives the same numbers on every compiler, but the std::distribution
// classes do not, so I wrote the few helpers I need by hand.
struct Random {
    mt19937 gen;
    Random(unsigned int seed) : gen(seed) {}

    double real() { return gen() / 4294967296.0; }               // 0 <= x < 1
    double uniform(double lo, double hi) { return lo + (hi - lo) * real(); }
    int integer(int lo, int hi) {                                // lo..hi, both included
        return lo + (int)(gen() % (unsigned)(hi - lo + 1));
    }
    void shuffle(vector<int>& v) {                               // Fisher-Yates
        for (int i = (int)v.size() - 1; i > 0; i--) swap(v[i], v[integer(0, i)]);
    }
    // triangular distribution: most values are near "mode"
    double triangular(double lo, double hi, double mode) {
        double u = real();
        double c = (mode - lo) / (hi - lo);
        if (u > c) { u = 1 - u; c = 1 - c; swap(lo, hi); }
        return lo + (hi - lo) * sqrt(u * c);
    }
};

// ---------- roads ----------
struct Road {
    int from, to;
    double length;      // metres
    double speed;       // metres per second when the road is empty
    int capacity;       // cars it can hold before it gets congested
    int load = 0;       // cars on it right now
    double slowdown = 1.0;   // > 1 while there is an accident

    double freeFlowTime() const { return length / speed; }

    // travel time = free flow time * (1 + 1.5 * (load / capacity)^3) * slowdown
    // (this curve is called the BPR function, it is used in transport engineering)
    double travelTime() const {
        double r = (double)load / capacity;
        return freeFlowTime() * (1.0 + ALPHA * r * r * r) * slowdown;
    }

    bool isArterial() const { return speed > 12; }   // the fast main roads
};

struct Network {
    vector<double> x, y;          // position of each intersection
    vector<Road> roads;           // every directed road
    vector<vector<int>> adj;      // adj[u] = ids of the roads leaving u
    double maxSpeed = 1.0;        // needed for the A* heuristic

    int numNodes() const { return x.size(); }
    int numRoads() const { return roads.size() / 2; }   // two-way roads

    int addNode(double px, double py) {
        x.push_back(px);
        y.push_back(py);
        adj.push_back(vector<int>());
        return x.size() - 1;
    }

    void addRoad(int u, int v, double length, double speed, int capacity) {
        Road r;
        r.from = u; r.to = v; r.length = length; r.speed = speed; r.capacity = capacity;
        roads.push_back(r);
        adj[u].push_back(roads.size() - 1);
        if (speed > maxSpeed) maxSpeed = speed;
    }

    void addTwoWay(int u, int v, double length, double speed, int capacity) {
        addRoad(u, v, length, speed, capacity);
        addRoad(v, u, length, speed, capacity);
    }

    // id of the road going from u to v, or -1
    int findRoad(int u, int v) const {
        for (int id : adj[u])
            if (roads[id].to == v) return id;
        return -1;
    }

    double distance(int a, int b) const {
        double dx = x[a] - x[b], dy = y[a] - y[b];
        return sqrt(dx * dx + dy * dy);
    }

    // BFS from node 0, can we reach every intersection?
    bool isConnected() const {
        vector<bool> seen(numNodes(), false);
        queue<int> q;
        q.push(0);
        seen[0] = true;
        int count = 1;
        while (!q.empty()) {
            int u = q.front(); q.pop();
            for (int id : adj[u]) {
                int v = roads[id].to;
                if (!seen[v]) { seen[v] = true; count++; q.push(v); }
            }
        }
        return count == numNodes();
    }
};

// ---------- building the city ----------
// A street grid with a little random jitter. Every 4th row and column is a fast
// arterial road. About 5% of the small streets are removed so it is not a
// perfect grid (only if the city stays connected).
// 12 x 12 gives 144 intersections.

struct Candidate { int u, v; bool arterial; };

Network buildCity(const vector<double>& xs, const vector<double>& ys,
                  const vector<Candidate>& cands, const vector<bool>& skip, Random& rng) {
    Network net;
    for (size_t i = 0; i < xs.size(); i++) net.addNode(xs[i], ys[i]);
    for (size_t i = 0; i < cands.size(); i++) {
        if (skip[i]) continue;
        const Candidate& e = cands[i];
        double length = net.distance(e.u, e.v) * rng.uniform(1.0, 1.1);   // roads are a bit curvy
        if (e.arterial) net.addTwoWay(e.u, e.v, length, 16.0, 20);
        else            net.addTwoWay(e.u, e.v, length, 9.0, 8);
    }
    return net;
}

Network makeCity(int rows = 12, int cols = 12, int seed = 0) {
    Random rng(seed);
    double block = 250.0;   // metres between intersections

    vector<double> xs, ys;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            double jitterX = rng.uniform(-0.15, 0.15) * block;
            double jitterY = rng.uniform(-0.15, 0.15) * block;
            xs.push_back(c * block + jitterX);
            ys.push_back(r * block + jitterY);
        }
    }

    vector<Candidate> cands;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int id = r * cols + c;
            if (c + 1 < cols) cands.push_back({id, id + 1, r % 4 == 0});
            if (r + 1 < rows) cands.push_back({id, id + cols, c % 4 == 0});
        }
    }

    // pick 5% of the small streets to remove
    vector<int> local;
    for (size_t i = 0; i < cands.size(); i++)
        if (!cands[i].arterial) local.push_back(i);
    rng.shuffle(local);
    vector<bool> skip(cands.size(), false);
    int drop = (int)(local.size() * 0.05);
    for (int i = 0; i < drop; i++) skip[local[i]] = true;

    Network net = buildCity(xs, ys, cands, skip, rng);
    if (!net.isConnected()) {   // removed too much, keep every street
        vector<bool> none(cands.size(), false);
        net = buildCity(xs, ys, cands, none, rng);
    }
    return net;
}

// Put a random number of cars on every road (used by the tests)
void addRandomLoad(Network& net, int seed) {
    Random rng(seed);
    for (Road& r : net.roads) r.load = rng.integer(0, r.capacity);
}
