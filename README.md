# Flow-Shop Scheduling with Job Rejection

An academic research project implemented in C++17. It evaluates three heuristics for selecting jobs to reject under a rejection budget in a proportionate flow shop. The reference baseline enumerates feasible rejection sets and uses WSPT-MCI to schedule the remaining jobs.

## Project Files

- `exactRejectionFlowshop.cpp`: H1, H2, H3, the reference baseline, and experiment generation.
- `compare_runs.cpp`: aggregation of saved runs and generation of comparison tables and charts.
- `runs/run_001` through `runs/run_010`: the ten runs used for the aggregate results in the report.
- `runs/comparison`: comparison tables and charts across runs.
- [docs/Final_Project1.pdf](docs/Final_Project1.pdf): the final academic report.
- `docs/report_comparison.csv`: comparison of aggregate metrics from saved results with Tables 1–6 in the report.

## Algorithms

- **H1 — Contribution Ratio:** ranks jobs by their initial weighted completion-time contribution relative to rejection cost.
- **H2 — One-Shot Savings:** ranks jobs by the schedule savings from removing each job individually, relative to rejection cost.
- **H3 — Iterative Greedy with Local Search:** recalculates rejection savings during selection and then checks one-for-one swaps.

On small instances, the heuristics are compared with the reference baseline. On larger instances, they are compared with each other; the best observed result is not assumed to be globally optimal.

## Build

Use a compiler with C++17 support. For example, with GCC:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -pedantic exactRejectionFlowshop.cpp -o exactRejectionFlowshop.exe
g++ -std=c++17 -O2 -Wall -Wextra -pedantic compare_runs.cpp -o compare_runs.exe
```

The following commands use Windows PowerShell syntax. On macOS or Linux, use `./` instead of `.\` to run the executables.

## Inspect Saved Results

The experiment data is already included, so rerunning the benchmark is not required. To regenerate summaries from the saved run directories:

```powershell
.\compare_runs.exe compare runs
```

This overwrites comparison outputs and charts under `runs/comparison`.

## Run a New Experiment

```powershell
.\exactRejectionFlowshop.exe
```

By default, the program runs 5,000 small instances and 150 larger instances, with up to 200 jobs. This may take a substantial amount of time. It creates a new `run_XXX` directory and overwrites the CSV files in the project root. Additional runs change the next summary generated across the run directories.

## Saved Results

The ten saved runs contain **50,000 quality-benchmark instances** and **1,500 scalability instances**. All 27 aggregate metrics in Tables 1–6 match the saved instance-level results after rounding. H3 recorded an **89.19% baseline match rate** and a **2.22% mean relative gap** in the quality benchmark. It achieved the best observed value in **1,385 of 1,500** scalability instances.

The main source was compiled with GCC 13.1.0. Replaying 100 saved small instances from each run, or 1,000 in total, reproduced the stored objective values for the baseline and all three heuristics.

## Reproducibility and Scope

With `randomSeed = 0`, a seed is selected at runtime, so new runs are not expected to generate identical samples. Small-instance inputs are stored in each run's `benchmark_trial_jobs.csv`. Larger-instance results and timings are included, but complete job inputs were not found in the project files and could not be replayed.

Matching the report verifies the recorded results; it is not a mathematical proof of algorithmic correctness or optimality. Saved running times were measured in the original experiment environment.
