// simulation.h
// One tick = one second. Cars appear over a "rush hour" window, most of them
// driving to a downtown area, and a few main roads get accidents.
//
// Three ways of choosing a route, all on the exact same city, trips and accidents:
//   STATIC   route is chosen once at the start and ignores traffic
//   ADAPTIVE route is chosen once at the start using the traffic at that moment
//   DYNAMIC  like ADAPTIVE, but the car checks again at every intersection.
//            It only changes route if the new one is more than 5% faster,
//            otherwise everybody keeps swapping between the same two routes.
#pragma once
#include <algorithm>
#include <cstdio>
#include <numeric>
#include <sstream>
#include <string>
#include "network.h"
#include "routing.h"

enum Strategy { STATIC, ADAPTIVE, DYNAMIC };

string strategyName(Strategy s) {
    if (s == STATIC) return "static";
    if (s == ADAPTIVE) return "adaptive";
    return "dynamic";
}

bool parseStrategy(const string& name, Strategy& out) {
    if (name == "static") out = STATIC;
    else if (name == "adaptive") out = ADAPTIVE;
    else if (name == "dynamic") out = DYNAMIC;
    else return false;
    return true;
}

// number -> text with a fixed number of decimals (for the JSON output)
string fmt(double v, int decimals) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

struct Config {
    int rows = 12, cols = 12;
    int seed = 0;
    int vehicles = 1000;
    int spawnWindow = 600;          // seconds over which the cars appear
    double hotspotFrac = 0.6;       // share of trips that end downtown
    int incidents = 3;
    int incidentDuration = 300;
    int maxTicks = 20000;
    double rerouteThreshold = 0.05;
};

struct Vehicle {
    int id = 0, origin = 0, dest = 0, spawnTick = 0;
    vector<int> path;        // intersections to drive through
    int idx = 0;             // position in path of the intersection it last passed
    double progress = 0;     // 0..1 along the current road
    int arriveTick = -1;     // -1 = still driving
    int reroutes = 0;
};

struct Incident {
    int roadA, roadB;        // both directions of the blocked road
    int start, end;
};

class Simulation {
public:
    Config cfg;
    Strategy strategy;
    Network net;
    vector<Vehicle> vehicles;    // sorted by spawn time
    vector<Incident> incidents;
    vector<int> active;          // indexes (into vehicles) of cars on the road
    int nextSpawn = 0;
    int finishedCount = 0;
    long long tripSum = 0;       // total trip time of the finished cars
    int tick = 0;

    // the two-way roads (u < v), used to draw the map in the browser
    vector<int> edgeU, edgeV, edgeFwd, edgeBack;

    Simulation(Config c, Strategy s) : cfg(c), strategy(s) {
        net = makeCity(cfg.rows, cfg.cols, cfg.seed);
        makeDemand();
        makeIncidents();
        listEdges();
    }

    bool done() const { return nextSpawn >= (int)vehicles.size() && active.empty(); }

    void run() {
        while (!done() && tick < cfg.maxTicks) step();
    }

    void step() {
        updateIncidents();
        spawnCars();

        vector<int> stillActive;
        for (int i : active) {
            Vehicle& v = vehicles[i];
            Road& road = net.roads[net.findRoad(v.path[v.idx], v.path[v.idx + 1])];
            v.progress += 1.0 / road.travelTime();   // a slow road means slow progress
            if (v.progress < 1.0) {
                stillActive.push_back(i);
                continue;
            }

            // reached the next intersection
            road.load--;
            v.idx++;
            v.progress = 0;
            if (v.idx == (int)v.path.size() - 1) {      // arrived
                v.arriveTick = tick + 1;
                finishedCount++;
                tripSum += v.arriveTick - v.spawnTick;
                continue;
            }
            if (strategy == DYNAMIC) maybeReroute(v);
            enterNextRoad(v);
            stillActive.push_back(i);
        }
        active = stillActive;
        tick++;
    }

    // ---------- output for the browser and the benchmark (JSON, one line) ----------

    string results() const {
        vector<int> trips;
        int reroutes = 0;
        for (const Vehicle& v : vehicles) {
            if (v.arriveTick < 0) continue;
            trips.push_back(v.arriveTick - v.spawnTick);
            reroutes += v.reroutes;
        }
        int loadLeft = 0;
        for (const Road& r : net.roads) loadLeft += r.load;

        ostringstream out;
        out << "{\"strategy\":\"" << strategyName(strategy) << "\",\"completed\":" << trips.size();
        if (!trips.empty()) {
            sort(trips.begin(), trips.end());
            int n = trips.size();
            double avg = (double)tripSum / n;
            double median = (n % 2 == 1) ? trips[n / 2] : (trips[n / 2 - 1] + trips[n / 2]) / 2.0;
            int p95 = trips[(int)(0.95 * (n - 1))];
            int unfinished = (int)active.size() + (int)vehicles.size() - nextSpawn;
            out << ",\"unfinished\":" << unfinished
                << ",\"avg_trip_s\":" << fmt(avg, 2)
                << ",\"median_trip_s\":" << fmt(median, 2)
                << ",\"p95_trip_s\":" << p95
                << ",\"reroutes\":" << reroutes
                << ",\"ticks\":" << tick
                << ",\"load_left\":" << loadLeft;
        }
        out << "}";
        return out.str();
    }

    // the map: intersection positions and roads
    string layout() const {
        ostringstream out;
        out << "{\"type\":\"layout\",\"nodes\":[";
        for (int i = 0; i < net.numNodes(); i++) {
            if (i > 0) out << ",";
            out << "[" << fmt(net.x[i], 1) << "," << fmt(net.y[i], 1) << "]";
        }
        out << "],\"edges\":[";
        for (size_t i = 0; i < edgeU.size(); i++) {
            if (i > 0) out << ",";
            out << "[" << edgeU[i] << "," << edgeV[i] << "]";
        }
        out << "],\"arterial\":[";
        for (size_t i = 0; i < edgeU.size(); i++) {
            if (i > 0) out << ",";
            out << (net.roads[edgeFwd[i]].isArterial() ? "true" : "false");
        }
        out << "]}";
        return out.str();
    }

    // a snapshot of the current state
    string frame() const {
        ostringstream out;
        out << "{\"type\":\"frame\",\"tick\":" << tick
            << ",\"active\":" << active.size()
            << ",\"finished\":" << finishedCount << ",\"avg_trip_s\":";
        if (finishedCount > 0) out << fmt((double)tripSum / finishedCount, 1);
        else out << "null";

        out << ",\"cars\":[";
        for (size_t k = 0; k < active.size(); k++) {
            const Vehicle& v = vehicles[active[k]];
            int a = v.path[v.idx], b = v.path[v.idx + 1];
            double px = net.x[a] + (net.x[b] - net.x[a]) * v.progress;
            double py = net.y[a] + (net.y[b] - net.y[a]) * v.progress;
            if (k > 0) out << ",";
            out << "[" << fmt(px, 1) << "," << fmt(py, 1) << "]";
        }

        out << "],\"congestion\":[";
        string blocked;
        for (size_t i = 0; i < edgeU.size(); i++) {
            const Road& a = net.roads[edgeFwd[i]];
            const Road& b = net.roads[edgeBack[i]];
            double c = max((double)a.load / a.capacity, (double)b.load / b.capacity);
            if (i > 0) out << ",";
            out << fmt(c, 2);
            if (a.slowdown > 1.0) {
                if (!blocked.empty()) blocked += ",";
                blocked += to_string(i);
            }
        }
        out << "],\"incidents\":[" << blocked << "]}";
        return out.str();
    }

private:
    // the middle 4x4 block of intersections is "downtown"
    vector<int> hotspots() const {
        vector<int> hot;
        int r0 = cfg.rows / 2 - 2, c0 = cfg.cols / 2 - 2;
        for (int dr = 0; dr < 4; dr++)
            for (int dc = 0; dc < 4; dc++)
                hot.push_back((r0 + dr) * cfg.cols + (c0 + dc));
        return hot;
    }

    // The trips only depend on the seed, never on the strategy,
    // so all three strategies get exactly the same cars.
    void makeDemand() {
        Random rng(cfg.seed * 7919 + 1);
        int n = net.numNodes();
        vector<int> hot = hotspots();
        for (int id = 0; id < cfg.vehicles; id++) {
            Vehicle v;
            v.id = id;
            v.spawnTick = (int)rng.triangular(0, cfg.spawnWindow, cfg.spawnWindow * 0.4);
            v.origin = rng.integer(0, n - 1);
            if (rng.real() < cfg.hotspotFrac) v.dest = hot[rng.integer(0, (int)hot.size() - 1)];
            else v.dest = rng.integer(0, n - 1);
            while (v.dest == v.origin) v.dest = rng.integer(0, n - 1);
            vehicles.push_back(v);
        }
        sort(vehicles.begin(), vehicles.end(), [](const Vehicle& a, const Vehicle& b) {
            if (a.spawnTick != b.spawnTick) return a.spawnTick < b.spawnTick;
            return a.id < b.id;
        });
    }

    // pick some arterial roads and block them in the middle of the rush hour
    void makeIncidents() {
        Random rng(cfg.seed * 104729 + 3);
        vector<int> arterials;   // ids of arterial roads, one direction each
        for (size_t i = 0; i < net.roads.size(); i++)
            if (net.roads[i].isArterial() && net.roads[i].from < net.roads[i].to)
                arterials.push_back(i);
        rng.shuffle(arterials);

        int count = min(cfg.incidents, (int)arterials.size());
        for (int k = 0; k < count; k++) {
            const Road& r = net.roads[arterials[k]];
            int start = rng.integer((int)(cfg.spawnWindow * 0.15), (int)(cfg.spawnWindow * 0.5));
            Incident inc;
            inc.roadA = arterials[k];
            inc.roadB = net.findRoad(r.to, r.from);
            inc.start = start;
            inc.end = start + cfg.incidentDuration;
            incidents.push_back(inc);
        }
    }

    void listEdges() {
        vector<pair<int, int>> pairs;
        for (const Road& r : net.roads)
            if (r.from < r.to) pairs.push_back({r.from, r.to});
        sort(pairs.begin(), pairs.end());
        for (size_t i = 0; i < pairs.size(); i++) {
            edgeU.push_back(pairs[i].first);
            edgeV.push_back(pairs[i].second);
            edgeFwd.push_back(net.findRoad(pairs[i].first, pairs[i].second));
            edgeBack.push_back(net.findRoad(pairs[i].second, pairs[i].first));
        }
    }

    void updateIncidents() {
        for (const Incident& inc : incidents) {
            if (tick == inc.start) {
                net.roads[inc.roadA].slowdown = BLOCKED_SLOWDOWN;
                net.roads[inc.roadB].slowdown = BLOCKED_SLOWDOWN;
            } else if (tick == inc.end) {
                net.roads[inc.roadA].slowdown = 1.0;
                net.roads[inc.roadB].slowdown = 1.0;
            }
        }
    }

    void spawnCars() {
        while (nextSpawn < (int)vehicles.size() && vehicles[nextSpawn].spawnTick <= tick) {
            Vehicle& v = vehicles[nextSpawn];
            RouteResult r;
            if (strategy == STATIC) r = dijkstra(net, v.origin, v.dest, true);   // ignores traffic
            else r = astar(net, v.origin, v.dest);
            v.path = r.path;     // the city is connected so there is always a path
            enterNextRoad(v);
            active.push_back(nextSpawn);
            nextSpawn++;
        }
    }

    void enterNextRoad(const Vehicle& v) {
        net.roads[net.findRoad(v.path[v.idx], v.path[v.idx + 1])].load++;
    }

    // DYNAMIC: look for a better route from the current intersection
    void maybeReroute(Vehicle& v) {
        int here = v.path[v.idx];
        vector<int> rest(v.path.begin() + v.idx, v.path.end());
        double currentCost = pathCost(net, rest);
        RouteResult best = astar(net, here, v.dest);
        if (best.found && best.cost < currentCost * (1.0 - cfg.rerouteThreshold)) {
            vector<int> newPath(v.path.begin(), v.path.begin() + v.idx);
            newPath.insert(newPath.end(), best.path.begin(), best.path.end());
            v.path = newPath;
            v.reroutes++;
        }
    }
};
