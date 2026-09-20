# Benchmark results

Average over 10 random seeds. 144 intersections, 3 accidents per run. Trip time is seconds from start to arrival.

## Routing strategies

| Cars | Static | Adaptive | Dynamic | Dynamic vs static | Dynamic vs adaptive |
|---|---|---|---|---|---|
| light (300) | 170.3 s | 156.3 s | 150.0 s | 11.9% faster | 4.0% faster |
| medium (600) | 234.9 s | 158.7 s | 152.0 s | 35.3% faster | 4.2% faster |
| heavy (1000) | 587.7 s | 179.4 s | 157.6 s | 73.2% faster | 12.2% faster |
| rush (1400) | 1202.6 s | 399.0 s | 174.9 s | 85.5% faster | 56.2% faster |

Note: static in 'rush (1400)' left 345 of 14000 cars stuck when the time limit (20000 s) was reached, so its average only counts the cars that arrived and is lower than the real average.

## Dijkstra vs A*

300 random trips on a 900-intersection city.

| Algorithm | Avg nodes expanded | Avg ms per query |
|---|---|---|
| Dijkstra | 440.7 | 0.04 |
| A* | 154.2 | 0.024 |

A* expands 65.0% fewer nodes and finds the same best route.
