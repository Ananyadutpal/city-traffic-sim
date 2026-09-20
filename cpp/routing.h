// routing.h
// Dijkstra and A*, both written with a priority queue (min-heap).
// The weight of a road is its CURRENT travel time, so the "shortest" path is
// the fastest one right now, not the shortest by distance.
#pragma once
#include <algorithm>
#include "network.h"

struct RouteResult {
    bool found;
    vector<int> path;    // intersections from src to dst
    double cost;         // travel time in seconds
    int expanded;        // how many nodes we took out of the heap
};

typedef pair<double, int> State;   // (priority, node)

// freeFlow = true ignores traffic (what a normal map app without live traffic does)
double roadWeight(const Road& r, bool freeFlow) {
    return freeFlow ? r.freeFlowTime() : r.travelTime();
}

vector<int> buildPath(const vector<int>& prev, int src, int dst) {
    vector<int> path;
    for (int v = dst; v != src; v = prev[v]) path.push_back(v);
    path.push_back(src);
    reverse(path.begin(), path.end());
    return path;
}

// Dijkstra: O((V + E) log V)
RouteResult dijkstra(const Network& net, int src, int dst, bool freeFlow = false) {
    int n = net.numNodes();
    vector<double> dist(n, INF);
    vector<int> prev(n, -1);
    vector<bool> done(n, false);
    priority_queue<State, vector<State>, greater<State>> pq;
    int expanded = 0;

    dist[src] = 0;
    pq.push({0, src});
    while (!pq.empty()) {
        State top = pq.top();
        pq.pop();
        double d = top.first;
        int u = top.second;
        if (done[u]) continue;      // old entry, this node is already finished
        done[u] = true;
        expanded++;
        if (u == dst) return {true, buildPath(prev, src, dst), d, expanded};

        for (int id : net.adj[u]) {
            const Road& r = net.roads[id];
            double nd = d + roadWeight(r, freeFlow);
            if (nd < dist[r.to]) {
                dist[r.to] = nd;
                prev[r.to] = u;
                pq.push({nd, r.to});
            }
        }
    }
    return {false, vector<int>(), INF, expanded};
}

// A*: same as Dijkstra but the heap is ordered by  (time so far) + (guess of time left).
// The guess is straight line distance / top speed. It never guesses too high
// (no road is faster than the top speed and traffic only adds time), so A*
// still finds the best path but looks at fewer nodes.
RouteResult astar(const Network& net, int src, int dst, bool freeFlow = false) {
    int n = net.numNodes();
    vector<double> g(n, INF);
    vector<int> prev(n, -1);
    vector<bool> done(n, false);
    priority_queue<State, vector<State>, greater<State>> pq;
    int expanded = 0;

    g[src] = 0;
    pq.push({net.distance(src, dst) / net.maxSpeed, src});
    while (!pq.empty()) {
        int u = pq.top().second;
        pq.pop();
        if (done[u]) continue;
        done[u] = true;
        expanded++;
        if (u == dst) return {true, buildPath(prev, src, dst), g[u], expanded};

        for (int id : net.adj[u]) {
            const Road& r = net.roads[id];
            double ng = g[u] + roadWeight(r, freeFlow);
            if (ng < g[r.to]) {
                g[r.to] = ng;
                prev[r.to] = u;
                double h = net.distance(r.to, dst) / net.maxSpeed;
                pq.push({ng + h, r.to});
            }
        }
    }
    return {false, vector<int>(), INF, expanded};
}

// current travel time along a path
double pathCost(const Network& net, const vector<int>& path) {
    double total = 0;
    for (size_t i = 0; i + 1 < path.size(); i++)
        total += net.roads[net.findRoad(path[i], path[i + 1])].travelTime();
    return total;
}
