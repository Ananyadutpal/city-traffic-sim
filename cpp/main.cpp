// main.cpp
// Command line program. The Python server runs it and reads what it prints.
// Everything is printed as JSON, one line at a time.
//
//   ./traffic run    --strategy dynamic --seed 0 --vehicles 1000   run a simulation, print the results
//   ./traffic stream --strategy dynamic --seed 0 --vehicles 1000   same, but print the map and a frame every 4 seconds
//   ./traffic route  --src 0 --dst 143 --algo astar [--free-flow]  fastest route between two intersections
//   ./traffic city   --seed 0                                      list all roads (used by the tests)
//   ./traffic algos  --rows 30 --cols 30 --queries 300             Dijkstra vs A* benchmark
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include "network.h"
#include "routing.h"
#include "simulation.h"

const int TICKS_PER_FRAME = 4;   // simulation seconds between two frames

map<string, string> args;

bool has(const string& key) { return args.count(key) > 0; }
int getInt(const string& key, int def) { return has(key) ? atoi(args[key].c_str()) : def; }
string getStr(const string& key, const string& def) { return has(key) ? args[key] : def; }

Config readConfig() {
    Config cfg;
    cfg.rows = getInt("rows", cfg.rows);
    cfg.cols = getInt("cols", cfg.cols);
    cfg.seed = getInt("seed", cfg.seed);
    cfg.vehicles = getInt("vehicles", cfg.vehicles);
    cfg.spawnWindow = getInt("window", cfg.spawnWindow);
    cfg.incidentDuration = getInt("incident-duration", cfg.incidentDuration);
    return cfg;
}

int badStrategy(const string& name) {
    cout << "{\"error\":\"Unknown strategy '" << name << "'.\"}" << endl;
    return 1;
}

int cmdRun() {
    Strategy s;
    if (!parseStrategy(getStr("strategy", "dynamic"), s)) return badStrategy(getStr("strategy", ""));
    Simulation sim(readConfig(), s);
    sim.run();
    cout << sim.results() << endl;
    return 0;
}

int cmdStream() {
    Strategy s;
    if (!parseStrategy(getStr("strategy", "dynamic"), s)) return badStrategy(getStr("strategy", ""));
    Simulation sim(readConfig(), s);
    cout << sim.layout() << "\n";
    while (!sim.done() && sim.tick < sim.cfg.maxTicks) {
        for (int i = 0; i < TICKS_PER_FRAME; i++) sim.step();
        cout << sim.frame() << "\n";
    }
    cout << "{\"type\":\"done\",\"results\":" << sim.results() << "}" << endl;
    return 0;
}

int cmdRoute() {
    Config cfg = readConfig();
    Network net = makeCity(cfg.rows, cfg.cols, cfg.seed);
    if (has("load-seed")) addRandomLoad(net, getInt("load-seed", 0));

    int src = getInt("src", 0), dst = getInt("dst", 0);
    int n = net.numNodes();
    if (src < 0 || src >= n || dst < 0 || dst >= n) {
        cout << "{\"error\":\"Intersection ids run from 0 to " << n - 1 << ".\"}" << endl;
        return 2;
    }

    string algo = getStr("algo", "astar");
    bool freeFlow = has("free-flow");
    RouteResult r = (algo == "dijkstra") ? dijkstra(net, src, dst, freeFlow)
                                         : astar(net, src, dst, freeFlow);

    cout << "{\"algorithm\":\"" << algo << "\",\"path\":";
    if (!r.found) {
        cout << "null,\"travel_time_s\":null";
    } else {
        cout << "[";
        for (size_t i = 0; i < r.path.size(); i++) cout << (i ? "," : "") << r.path[i];
        cout << "],\"travel_time_s\":" << fmt(r.cost, 6);
    }
    cout << ",\"nodes_expanded\":" << r.expanded << "}" << endl;
    return 0;
}

// every directed road with its empty-road time and its current time
int cmdCity() {
    Config cfg = readConfig();
    Network net = makeCity(cfg.rows, cfg.cols, cfg.seed);
    if (has("load-seed")) addRandomLoad(net, getInt("load-seed", 0));
    cout << "{\"nodes\":" << net.numNodes() << ",\"roads\":[";
    for (size_t i = 0; i < net.roads.size(); i++) {
        const Road& r = net.roads[i];
        cout << (i ? "," : "") << "[" << r.from << "," << r.to << ","
             << fmt(r.freeFlowTime(), 9) << "," << fmt(r.travelTime(), 9) << "]";
    }
    cout << "]}" << endl;
    return 0;
}

// Dijkstra vs A* on random pairs of intersections (empty roads)
int cmdAlgos() {
    Config cfg = readConfig();
    if (!has("rows")) cfg.rows = 30;
    if (!has("cols")) cfg.cols = 30;
    if (!has("seed")) cfg.seed = 1;
    int queries = getInt("queries", 300);
    Network net = makeCity(cfg.rows, cfg.cols, cfg.seed);
    int n = net.numNodes();

    Random rng(42);
    vector<int> from, to;
    for (int i = 0; i < queries; i++) {
        int s = rng.integer(0, n - 1), t = rng.integer(0, n - 1);
        while (t == s) t = rng.integer(0, n - 1);
        from.push_back(s);
        to.push_back(t);
    }

    double avgNodes[2], avgMs[2];
    for (int algo = 0; algo < 2; algo++) {       // 0 = Dijkstra, 1 = A*
        long long totalNodes = 0;
        auto start = chrono::steady_clock::now();
        for (int i = 0; i < queries; i++) {
            RouteResult r = (algo == 0) ? dijkstra(net, from[i], to[i], true)
                                        : astar(net, from[i], to[i], true);
            totalNodes += r.expanded;
        }
        chrono::duration<double, milli> elapsed = chrono::steady_clock::now() - start;
        avgNodes[algo] = (double)totalNodes / queries;
        avgMs[algo] = elapsed.count() / queries;
    }

    cout << "{\"nodes\":" << n << ",\"queries\":" << queries
         << ",\"dijkstra\":{\"avg_nodes_expanded\":" << fmt(avgNodes[0], 1)
         << ",\"avg_ms_per_query\":" << fmt(avgMs[0], 3) << "}"
         << ",\"astar\":{\"avg_nodes_expanded\":" << fmt(avgNodes[1], 1)
         << ",\"avg_ms_per_query\":" << fmt(avgMs[1], 3) << "}"
         << ",\"astar_expands_fewer_pct\":" << fmt(100.0 * (avgNodes[0] - avgNodes[1]) / avgNodes[0], 1)
         << "}" << endl;
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        cerr << "usage: traffic run|stream|route|city|algos [--option value ...]" << endl;
        return 1;
    }
    string command = argv[1];

    // read the --key value pairs (a --key with no value is a flag)
    for (int i = 2; i < argc; i++) {
        string a = argv[i];
        if (a.rfind("--", 0) != 0) continue;
        string key = a.substr(2);
        if (i + 1 < argc && string(argv[i + 1]).rfind("--", 0) != 0) args[key] = argv[++i];
        else args[key] = "1";
    }

    if (command == "run") return cmdRun();
    if (command == "stream") return cmdStream();
    if (command == "route") return cmdRoute();
    if (command == "city") return cmdCity();
    if (command == "algos") return cmdAlgos();
    cerr << "unknown command: " << command << endl;
    return 1;
}
