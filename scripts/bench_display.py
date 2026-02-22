import streamlit as st
import pandas as pd
import glob
import json
import os
import plotly.express as px
import plotly.graph_objects as go
import re
from datetime import datetime

DATA_DIR = "../bench/Output"

st.set_page_config(page_title=PAGE_TITLE, layout=LAYOUT)

@st.cache_data
def load_data(data_dir):
    all_files = glob.glob(os.path.join(data_dir, "*.json"))
    
    if not all_files:
        return pd.DataFrame()

    records = []

    for filename in all_files:
        try:
            with open(filename, 'r') as f:
                data = json.load(f)
                
            timestamp_raw = data.get("timestamp") or data.get("date")
            if not timestamp_raw:
                match = re.search(r'(\d{4}_\d{2}_\d{2}_\d{2}_\d{2})', filename)
                if match:
                    timestamp_raw = match.group(1)
                else:
                    timestamp_raw = os.path.getmtime(filename)

            try:
                if isinstance(timestamp_raw, (int, float)):
                    timestamp = datetime.fromtimestamp(timestamp_raw)
                else:
                    try:
                        timestamp = datetime.strptime(timestamp_raw, "%Y_%m_%d_%H_%M")
                    except:
                        timestamp = pd.to_datetime(timestamp_raw)
            except:
                timestamp = datetime.fromtimestamp(os.path.getmtime(filename))

            processor = data.get("processor", "Unknown")

            for res in data.get("results", []):
                command = res.get("command", "")               
                
                compiler = "My Compiler"
                clean_name = command
                
                if "_clang_O1" in command:
                    compiler = "Clang O1"
                    clean_name = command.replace("_clang_O1_run_baseline", "").replace("_clang_O1", "")
                elif "_clang_O3" in command:
                    compiler = "Clang O3"
                    clean_name = command.replace("_clang_O3_run_baseline", "").replace("_clang_O3", "")
                elif "_gcc_O3" in command:
                    compiler = "GCC O3"
                    clean_name = command.replace("_gcc_O3_run_baseline", "").replace("_gcc_O3", "")
                else:
                    clean_name = command.replace("_run", "")

                clean_name = clean_name.replace("./", "").split(".tmp")[0].strip()

                records.append({
                    "Timestamp": timestamp,
                    "Benchmark": clean_name,
                    "Compiler": compiler,
                    "Mean Time (s)": res["mean"],
                    "StdDev": res.get("stddev", 0),
                    "Min": res.get("min", res["mean"]),
                    "Max": res.get("max", res["mean"]),
                    "Processor": processor,
                    "Filename": os.path.basename(filename)
                })
        except Exception as e:
            st.warning(f"Could not parse file {filename}: {e}")

    df = pd.DataFrame(records)
    if not df.empty:
        df = df.sort_values(by="Timestamp")
    return df

st.title(f"Compiler Performance Tracker")

LAYOUT = "wide"
df = load_data(DATA_DIR)

if df.empty:
    st.error(f"No JSON data found in '{DATA_DIR}'. Please ensure your hyperfine JSON files are there.")
    st.stop()

st.sidebar.header("Global Filters")
selected_benchmarks = st.sidebar.multiselect(
    "Select Benchmarks",
    options=sorted(df["Benchmark"].unique()),
    default=sorted(df["Benchmark"].unique())
)

filtered_df = df[df["Benchmark"].isin(selected_benchmarks)]

# --- Tab View ---
tab1, tab2, tab3 = st.tabs(["Performance Over Time", "Snapshot View", "Raw Data"])

with tab1:
    st.subheader("Runtime Evolution")
    
    if filtered_df.empty:
        st.info("No benchmarks selected.")
    else:
        st.info("Gaps in lines with a red 'x' at the bottom indicate missing/failed samples.")
        normalize = st.toggle("Normalize to Clang O3 (Lower is better, 1.0 = Clang O3)", value=False)
        
        # We need a complete grid to ensure Plotly shows gaps correctly
        all_timestamps = sorted(filtered_df["Timestamp"].unique())
        all_compilers = sorted(filtered_df["Compiler"].unique())
        all_benchmarks = sorted(filtered_df["Benchmark"].unique())
        
        # Create a full index of all possible combinations
        full_index = pd.MultiIndex.from_product(
            [all_timestamps, all_benchmarks, all_compilers], 
            names=["Timestamp", "Benchmark", "Compiler"]
        )
        
        # Reindex to ensure missing rows exist as NaNs
        chart_data = filtered_df.set_index(["Timestamp", "Benchmark", "Compiler"]).reindex(full_index).reset_index()
        
        y_col = "Mean Time (s)"
        
        if normalize:
            pivoted = chart_data.pivot_table(
                index=["Timestamp", "Benchmark"], 
                columns="Compiler", 
                values="Mean Time (s)"
            ).reset_index()
            
            if "Clang O3" in pivoted.columns:
                for col in pivoted.columns:
                    if col not in ["Timestamp", "Benchmark", "Clang O3"]:
                        pivoted[col] = pivoted[col] / pivoted["Clang O3"]
                
                pivoted["Clang O3"] = 1.0
                chart_data = pivoted.melt(
                    id_vars=["Timestamp", "Benchmark"],
                    var_name="Compiler",
                    value_name="Normalized Time"
                )
                y_col = "Normalized Time"
            else:
                st.warning("Cannot normalize: 'Clang O3' data missing from selection.")

        num_benchmarks = len(all_benchmarks)
        cols = 2
        rows = (num_benchmarks + cols - 1) // cols
        row_spacing = min(0.08, 0.5 / max(1, rows - 1))
        dynamic_height = max(600, rows * 350)

        # Base Chart
        fig_time = px.line(
            chart_data, 
            x="Timestamp", 
            y=y_col, 
            color="Compiler",
            facet_col="Benchmark", 
            facet_col_wrap=cols,
            facet_row_spacing=row_spacing,
            markers=True,
            title=f"Performance History ({'Normalized' if normalize else 'Absolute'})",
            height=dynamic_height
        )

        missing_data = chart_data[chart_data[y_col].isna()].copy()

        # Add markers for missing data points
        if not missing_data.empty:
            for bench in all_benchmarks:
                bench_missing = missing_data[missing_data["Benchmark"] == bench]
                if not bench_missing.empty:
                    bench_idx = all_benchmarks.index(bench)
                    row_idx = rows - (bench_idx // cols)
                    col_idx = (bench_idx % cols) + 1    
                    
                    fig_time.add_trace(
                        go.Scatter(
                            x=bench_missing["Timestamp"],
                            y=[0] * len(bench_missing),
                            mode="markers",
                            marker=dict(symbol="x", color="red", size=8),
                            name=f"Missing: {bench}",
                            showlegend=False,
                            hoverinfo="text",
                            text=[f"Missing ({bench}): {c}" for c in bench_missing["Compiler"]],
                        ),
                        row=row_idx,
                        col=col_idx
                    )
        
        fig_time.update_traces(connectgaps=False)
        fig_time.update_yaxes(matches=None, rangemode="tozero")
        fig_time.for_each_annotation(lambda a: a.update(text=a.text.split("=")[-1]))
        
        st.plotly_chart(fig_time, use_container_width=True)

with tab2:
    if not filtered_df.empty:
        all_timestamps = sorted(filtered_df["Timestamp"].unique(), reverse=True)
        timestamp_options = {ts.strftime('%Y-%m-%d %H:%M:%S'): ts for ts in all_timestamps}
        
        col_run, col_comp = st.columns(2)
        with col_run:
            selected_ts_str = st.selectbox("Select a specific run to inspect:", options=list(timestamp_options.keys()))
            selected_ts = timestamp_options[selected_ts_str]
        
        snap_df = filtered_df[filtered_df["Timestamp"] == selected_ts]
        
        available_compilers = sorted([c for c in snap_df["Compiler"].unique() if c != "My Compiler"])
        with col_comp:
            baseline_compiler = st.selectbox(
                "Select Baseline for Comparison:", 
                options=available_compilers,
                index=available_compilers.index("GCC O3") if "GCC O3" in available_compilers else 0
            )
        
        st.subheader(f"Snapshot: Run at {selected_ts_str}")
        log_scale = st.checkbox("Use Logarithmic Scale (recommended for mixed benchmark sizes)", value=True)
        
        fig_bar = px.bar(
            snap_df,
            x="Benchmark",
            y="Mean Time (s)",
            color="Compiler",
            barmode="group",
            error_y="StdDev",
            log_y=log_scale,
            title=f"Results for {selected_ts_str} (Lower is better)",
            text_auto='.3f'
        )
        st.plotly_chart(fig_bar, use_container_width=True)
        
        st.markdown(f"### ⚔️ Head-to-Head: My Compiler vs {baseline_compiler}")
        active_benchmarks = sorted(snap_df["Benchmark"].unique())
        m_cols = st.columns(4)
        
        for i, bench in enumerate(active_benchmarks):
            bench_data = snap_df[snap_df["Benchmark"] == bench]
            my_time = bench_data[bench_data["Compiler"] == "My Compiler"]["Mean Time (s)"].values
            base_time = bench_data[bench_data["Compiler"] == baseline_compiler]["Mean Time (s)"].values
            
            with m_cols[i % 4]: 
                if len(my_time) > 0 and len(base_time) > 0:
                    diff = my_time[0] - base_time[0]
                    percent = (diff / base_time[0]) * 100
                    st.metric(
                        label=bench,
                        value=f"{my_time[0]:.4f}s",
                        delta=f"{percent:.1f}% vs {baseline_compiler}",
                        delta_color="inverse"
                    )
                else:
                    st.metric(label=bench, value="N/A", delta="Missing Data")
    else:
        st.info("No data available for snapshot selection.")

with tab3:
    st.subheader("Raw Data")
    st.dataframe(
        filtered_df.sort_values(by="Timestamp", ascending=False),
        use_container_width=True
    )
    csv = filtered_df.to_csv(index=False).encode('utf-8')
    st.download_button("Download CSV", csv, "benchmark_data.csv", "text/csv")
