import os
import json
import re
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np
from matplotlib.patches import Rectangle
from datetime import datetime
import numpy as np

def extract_benchmark_name(command):
    if command.endswith("_run"):
        return command.replace("_run", ""), "Mine"
    if "_clang_O1_run_baseline" in command:
        return command.replace("_clang_O1_run_baseline", ""), "Clang O1"
    if "_clang_O3_run_baseline" in command:
        return command.replace("_clang_O3_run_baseline", ""), "Clang O3"
    if "_gcc_O3_run_baseline" in command:
        return command.replace("_gcc_O3_run_baseline", ""), "GCC O3"
    return None, None

def parse_timestamp_from_filename(filename):
    match = re.match(r"(\d{4}_\d{2}_\d{2}_\d{2}_\d{2})_perf", filename)
    if match:
        return datetime.strptime(match.group(1), "%Y_%m_%d_%H_%M")
    return None

def load_all_results(folder):
    performance_data = {}

    for filename in os.listdir(folder):
        if filename.endswith(".json") and "_perf" in filename:
            filepath = os.path.join(folder, filename)
            timestamp = parse_timestamp_from_filename(filename)
            if not timestamp:
                continue

            with open(filepath, 'r') as f:
                data = json.load(f)

            for entry in data.get("results", []):
                benchmark, compiler = extract_benchmark_name(entry["command"])
                if benchmark is None:
                    continue

                if benchmark not in performance_data:
                    performance_data[benchmark] = {}
                if compiler not in performance_data[benchmark]:
                    performance_data[benchmark][compiler] = []

                performance_data[benchmark][compiler].append({
                    "time": timestamp,
                    "mean": entry["mean"],
                    "stddev": entry["stddev"]
                })

    return performance_data

def plot_benchmark_performance_overlay(performance_data):
    compiler_colors = ['red', 'blue', 'cyan', 'green']

    for benchmark, compilers in performance_data.items():
        for compiler, entries in compilers.items():
            entries.sort(key=lambda x: x["time"])

        fig, ax = plt.subplots(figsize=(10, 6))

        # === BAR AXIS IN BACKGROUND ===
        ax_bar = fig.add_axes(ax.get_position(), frameon=False, zorder=0)
        ax_bar.set_facecolor('none')
        ax_bar.set_yticks([])
        ax_bar.set_xticks(range(len(compilers)))
        ax_bar.set_xticklabels(list(compilers.keys()))
        ax_bar.tick_params(axis='x', rotation=45)

        # Draw bars first
        latest_means = []
        latest_stddevs = []
        for compiler, entries in compilers.items():
            if entries:
                latest_entry = entries[-1]
                latest_means.append(latest_entry["mean"])
                latest_stddevs.append(latest_entry["stddev"])
            else:
                latest_means.append(0)
                latest_stddevs.append(0)

        ax_bar.set_ylim(0, max(latest_means) * 2.0)
        ax_bar.set_xlim(-1, 4 * 2)
        ax_bar.bar(
            range(len(compilers)),
            latest_means,
            yerr=latest_stddevs,
            alpha=0.9,
            color=compiler_colors[:len(compilers)],
            label='Latest Run (Bar)',
            zorder=0
        )

        # Ensure main axis is drawn over the bar axis
        ax.set_zorder(ax_bar.get_zorder() + 1)
        ax.patch.set_visible(False)  # Make ax background transparent

        # === LINE PLOT ON TOP ===
        for i, (compiler, entries) in enumerate(compilers.items()):
            times = [e["time"] for e in entries]
            means = [e["mean"] for e in entries]
            stddevs = [e["stddev"] for e in entries]
            lower = [m - s for m, s in zip(means, stddevs)]
            upper = [m + s for m, s in zip(means, stddevs)]
            ax.plot(times, means, label=compiler, color=compiler_colors[i], zorder=2)
            ax.fill_between(times, lower, upper, alpha=0.3, color=compiler_colors[i], zorder=1)

        # Formatting
        ax.set_title(f"Performance Over Time: {benchmark}")
        ax.set_xlabel("Timestamp")
        ax.set_ylabel("Mean Time (s)")
        ax.grid(True)
        ax.legend(loc='upper left')
        ax.tick_params(axis='x', rotation=45)
        plt.show()


def plot_benchmark_performance_single(performance_data):
    compiler_colors = ['red', 'blue', 'cyan', 'green']

    for benchmark, compilers in performance_data.items():
        for compiler, entries in compilers.items():
            entries.sort(key=lambda x: x["time"])

        # === MAIN PLOT (Bar Chart) ===
        fig, ax = plt.subplots(figsize=(10, 6))

        compiler_names = list(compilers.keys())
        latest_means = []
        latest_stddevs = []

        for compiler in compiler_names:
            entries = compilers[compiler]
            if entries:
                latest_entry = entries[-1]
                latest_means.append(latest_entry["mean"])
                latest_stddevs.append(latest_entry["stddev"])
            else:
                latest_means.append(0)
                latest_stddevs.append(0)

        ax.bar(range(len(compiler_names)), latest_means, yerr=latest_stddevs,
               color=compiler_colors[:len(compiler_names)], alpha=0.8)

        ax.set_xticks(range(len(compiler_names)))
        ax.set_xticklabels(compiler_names, rotation=45)
        ax.set_ylabel("Latest Mean Time (s)")
        ax.set_title(f"Latest Runtime: {benchmark}")
        ax.grid(True)

        # === INSET PLOT (Time Series) ===
        inset_ax = ax.inset_axes([0.1, 0.1, 0.4, 0.35])  # [x, y, width, height] in figure coords
        inset_ax.tick_params(axis='x', labelrotation=45)
        inset_ax.tick_params(axis='y', labelrotation=45)

        for i, (compiler, entries) in enumerate(compilers.items()):
            times = [e["time"] for e in entries]
            means = [e["mean"] for e in entries]
            stddevs = [e["stddev"] for e in entries]
            lower = [m - s for m, s in zip(means, stddevs)]
            upper = [m + s for m, s in zip(means, stddevs)]

            inset_ax.plot(times, means, label=compiler, color=compiler_colors[i])
            inset_ax.fill_between(times, lower, upper, alpha=0.2, color=compiler_colors[i])

        inset_ax.xaxis.set_major_formatter(mdates.DateFormatter('%m-%d'))
        inset_ax.grid(True)

        plt.tight_layout()
        plt.show()

def plot_benchmark_performance(performance_data):
    compiler_colors = ['red', 'blue', 'cyan', 'green']
    mine_color = 'magenta'  # color for Mine compiler line

    benchmarks = list(performance_data.keys())
    compiler_names = list(next(iter(performance_data.values())).keys())
    n_benchmarks = len(benchmarks)
    n_compilers = len(compiler_names)

    bar_width = 0.15
    group_width = bar_width * n_compilers + 0.1

    fig, ax_bar = plt.subplots(figsize=(max(10, n_benchmarks * 3), 6))

    x_base = np.arange(n_benchmarks) * group_width

    # Plot bars normalized to Mine per benchmark
    for ci, compiler in enumerate(compiler_names):
        bar_positions = x_base + ci * bar_width

        means = []
        stddevs = []

        for benchmark in benchmarks:
            entries = performance_data[benchmark][compiler]
            entries.sort(key=lambda x: x["time"])

            # Normalize using Mine latest mean
            mine_entries = performance_data[benchmark].get('Mine', [])
            mine_entries.sort(key=lambda x: x["time"])
            if mine_entries:
                mine_latest_mean = mine_entries[-1]["mean"]
            else:
                mine_latest_mean = None

            if entries:
                latest_entry = entries[-1]
                if mine_latest_mean and mine_latest_mean != 0:
                    norm_mean = latest_entry["mean"] / mine_latest_mean
                    norm_std = latest_entry["stddev"] / mine_latest_mean
                else:
                    norm_mean = latest_entry["mean"]
                    norm_std = latest_entry["stddev"]

                means.append(norm_mean)
                stddevs.append(norm_std)
            else:
                means.append(0)
                stddevs.append(0)

        ax_bar.bar(bar_positions, means, width=bar_width, yerr=stddevs,
                   label=compiler, color=compiler_colors[ci], alpha=0.8)

    group_centers = x_base + (n_compilers - 1) * bar_width / 2
    ax_bar.set_xticks(group_centers)
    ax_bar.set_xticklabels(benchmarks, rotation=45, ha='right', rotation_mode='anchor')
    ax_bar.set_ylabel("Normalized Time (relative to Mine = 1)")
    ax_bar.set_title("Normalized Latest Runtime by Benchmark and Compiler")
    ax_bar.grid(True)

    # Overlay Mine time series on same plot (normalized)
    for i, benchmark in enumerate(benchmarks):
        entries = performance_data[benchmark].get('Mine', [])
        if not entries:
            continue
        entries.sort(key=lambda x: x["time"])
        means = [e["mean"] for e in entries]

        # Normalize means by latest Mine mean to align scale
        latest_mine_mean = means[-1]
        norm_means = [m / latest_mine_mean for m in means]

        n_points = len(entries)
        # Distribute the line points horizontally within the benchmark group width
        x_start = group_centers[i] - bar_width
        x_end = group_centers[i] + bar_width
        x_line = np.linspace(x_start, x_end, n_points)

        ax_bar.plot(x_line, norm_means, color=mine_color, marker='o', linestyle='-', label='Time Series' if i == 0 else "")
        ax_bar.fill_between(x_line,
                            [m * 0.9 for m in norm_means],  # dummy ±10% fill for visual
                            [m * 1.1 for m in norm_means],
                            color=mine_color, alpha=0.15)

    # Adjust legend: remove duplicate 'Mine Time Series' label by only adding it once
    handles, labels = ax_bar.get_legend_handles_labels()
    by_label = dict(zip(labels, handles))
    ax_bar.legend(by_label.values(), by_label.keys())

    plt.tight_layout()
    plt.show()


folder_path = "../bench/Output"  # <- change this to your actual folder
data = load_all_results(folder_path)
plot_benchmark_performance(data)
