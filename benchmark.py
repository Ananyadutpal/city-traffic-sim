# benchmark.py
# Compares the three routing strategies and Dijkstra vs A*.
# The C++ program does the work, this script only collects the numbers,
# writes them to results/ and draws the chart.
#
# Run:  python benchmark.py      (build the C++ program first with: make)
import json
import statistics
import subprocess
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

HERE = Path(__file__).parent
ENGINE = HERE / "traffic"
RESULTS = HERE / "results"

DEMAND = {"light (300)": 300, "medium (600)": 600, "heavy (1000)": 1000, "rush (1400)": 1400}
SEEDS = list(range(10))
STRATEGIES = ["static", "adaptive", "dynamic"]
MAX_SECONDS = 20000   # the simulation stops after this many seconds


def engine(*args):
    done = subprocess.run([str(ENGINE), *map(str, args)], capture_output=True, text=True, check=True)
    return json.loads(done.stdout)


def percent_better(old, new):
    return round(100 * (old - new) / old, 1)


def compare_strategies():
    # same city, same trips, same accidents. Only the routing strategy changes.
    table = {}
    for label, vehicles in DEMAND.items():
        runs = {s: [] for s in STRATEGIES}
        for seed in SEEDS:
            for s in STRATEGIES:
                runs[s].append(engine("run", "--strategy", s, "--seed", seed, "--vehicles", vehicles))

        row = {}
        for s in STRATEGIES:
            row[s] = {
                "avg_trip_s": round(statistics.fmean(r["avg_trip_s"] for r in runs[s]), 1),
                "p95_trip_s": round(statistics.fmean(r["p95_trip_s"] for r in runs[s]), 1),
                "unfinished": sum(r["unfinished"] for r in runs[s]),
                "reroutes": round(statistics.fmean(r["reroutes"] for r in runs[s])),
            }
        row["dynamic_vs_static_pct"] = percent_better(row["static"]["avg_trip_s"], row["dynamic"]["avg_trip_s"])
        row["dynamic_vs_adaptive_pct"] = percent_better(row["adaptive"]["avg_trip_s"], row["dynamic"]["avg_trip_s"])
        table[label] = row
        print(f"{label:14s} static {row['static']['avg_trip_s']:>7} | adaptive {row['adaptive']['avg_trip_s']:>7} | "
              f"dynamic {row['dynamic']['avg_trip_s']:>7} | dynamic vs static {row['dynamic_vs_static_pct']}%")
    return table


def write_markdown(table, algos):
    lines = [
        "# Benchmark results",
        "",
        f"Average over {len(SEEDS)} random seeds. 144 intersections, 3 accidents per run. "
        "Trip time is seconds from start to arrival.",
        "",
        "## Routing strategies",
        "",
        "| Cars | Static | Adaptive | Dynamic | Dynamic vs static | Dynamic vs adaptive |",
        "|---|---|---|---|---|---|",
    ]
    for label, row in table.items():
        lines.append(
            f"| {label} | {row['static']['avg_trip_s']} s | {row['adaptive']['avg_trip_s']} s | "
            f"{row['dynamic']['avg_trip_s']} s | {row['dynamic_vs_static_pct']}% faster | "
            f"{row['dynamic_vs_adaptive_pct']}% faster |"
        )
    # if some cars never arrived, say so (the averages only count cars that arrived)
    for label, row in table.items():
        for s in STRATEGIES:
            if row[s]["unfinished"] > 0:
                total = DEMAND[label] * len(SEEDS)
                lines.append("")
                lines.append(
                    f"Note: {s} in '{label}' left {row[s]['unfinished']} of {total} cars stuck when the "
                    f"time limit ({MAX_SECONDS} s) was reached, so its average only counts the cars that "
                    "arrived and is lower than the real average."
                )
    lines += [
        "",
        "## Dijkstra vs A*",
        "",
        f"{algos['queries']} random trips on a {algos['nodes']}-intersection city.",
        "",
        "| Algorithm | Avg nodes expanded | Avg ms per query |",
        "|---|---|---|",
        f"| Dijkstra | {algos['dijkstra']['avg_nodes_expanded']} | {algos['dijkstra']['avg_ms_per_query']} |",
        f"| A* | {algos['astar']['avg_nodes_expanded']} | {algos['astar']['avg_ms_per_query']} |",
        "",
        f"A* expands {algos['astar_expands_fewer_pct']}% fewer nodes and finds the same best route.",
    ]
    (RESULTS / "benchmark.md").write_text("\n".join(lines) + "\n")


def draw_chart(table):
    labels = list(table)
    colours = {"static": "#B85C4A", "adaptive": "#D9A441", "dynamic": "#2F7D6D"}
    width = 0.26
    fig, ax = plt.subplots(figsize=(8, 4.2))
    for i, s in enumerate(STRATEGIES):
        xs = [x + (i - 1) * width for x in range(len(labels))]
        ax.bar(xs, [table[l][s]["avg_trip_s"] for l in labels], width, label=s, color=colours[s])
    ax.set_xticks(range(len(labels)))
    ax.set_xticklabels(labels)
    ax.set_ylabel("Average trip time (s)")
    ax.set_title("Routing strategy vs traffic (lower is better)")
    ax.legend(frameon=False)
    fig.tight_layout()
    fig.savefig(RESULTS / "strategy_comparison.png", dpi=150)


if __name__ == "__main__":
    RESULTS.mkdir(exist_ok=True)
    print("== Strategies ==")
    table = compare_strategies()
    print("\n== Dijkstra vs A* ==")
    algos = engine("algos")
    print(algos)

    (RESULTS / "benchmark.json").write_text(json.dumps({"strategies": table, "algorithms": algos}, indent=2))
    write_markdown(table, algos)
    draw_chart(table)
    print("\nSaved results/benchmark.json, benchmark.md and strategy_comparison.png")
