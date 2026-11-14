#!/usr/bin/env python3
# AI SLOP
"""Generates a cost-model TableRow array from a CSV.

Usage:
    scripts/gen_cost_tables.py <output.inc> <ArchName>=<csv-path>...
        [cache:<ArchName>=<csv-path>...]

One table per microarchitecture plus table_of(MicroArch), which needs a
MicroArch enumerator named ArchName. CMake runs it into the build directory
whenever a csv, this script or the X86OpClass enum changes. A cache: argument
gives the cache hierarchy of the same architecture (rows L1D, L2, LLC, MEM),
every architecture needs one. The op-class order
of the arrays is read out of that enum, so it lines up with Machine::table().
"""

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENUM_HEADER = ROOT / "src/optim/cost_model/x86_op_class.hpp"


def read_enum_order():
    text = ENUM_HEADER.read_text()
    m = re.search(r"enum class X86OpClass[^{]*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit(f"could not find the X86OpClass enum in {ENUM_HEADER}")
    names = []
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip().rstrip(",")
        if not line or line == "COUNT":
            continue
        names.append(line)
    return names


def read_rows(csv_path: Path):
    lines = [
        line
        for line in csv_path.read_text().splitlines(keepends=True)
        if not line.lstrip().startswith("#")
    ]
    rows = {}
    for row in csv.DictReader(lines):
        rows[row["op_class"]] = row
    return rows


def table_lines(arch_name, csv_path, order):
    rows = read_rows(csv_path)
    missing = [name for name in order if name not in rows]
    if missing:
        raise SystemExit(f"{csv_path} is missing rows for: {', '.join(missing)}")
    lines = [
        f"// {arch_name}: {csv_path} (see its header for provenance and accuracy)",
        f"static constexpr foptim::optim::cost_model::TableRow k{arch_name}Table[] = {{",
    ]
    for name in order:
        r = rows[name]
        lines.append(
            "    {{{latency}, {rtput}, {ports}, {uops}, {size}}},  // {name}".format(
                latency=float(r["latency"]),
                rtput=float(r["recip_throughput"]),
                ports=r["ports_mask"],
                uops=int(r["n_uops"]),
                size=int(r["size_bytes"]),
                name=name,
            )
        )
    lines.append("};")
    lines.append(
        "static_assert(sizeof(k{0}Table) / sizeof(k{0}Table[0]) == "
        "static_cast<size_t>(foptim::optim::cost_model::X86OpClass::COUNT));".format(
            arch_name
        )
    )
    return lines


CACHE_LEVELS = ["L1D", "L2", "LLC"]


def cache_lines(arch_name, csv_path):
    lines = [x for x in csv_path.read_text().splitlines(keepends=True)
             if not x.lstrip().startswith("#")]
    rows = {r["level"]: r for r in csv.DictReader(lines)}
    missing = [n for n in CACHE_LEVELS + ["MEM"] if n not in rows]
    if missing:
        raise SystemExit(f"{csv_path} is missing rows for: {', '.join(missing)}")
    out = [
        f"// {arch_name}: {csv_path}",
        f"static constexpr foptim::optim::cost_model::CacheLevel k{arch_name}Cache[] = {{",
    ]
    for name in CACHE_LEVELS:
        r = rows[name]
        out.append(
            "    {{{size}, {line}, {assoc}, {lat}}},  // {name}".format(
                size=int(r["size_bytes"]),
                line=int(r["line_size"]),
                assoc=int(r["assoc"]),
                lat=float(r["latency"]),
                name=name,
            )
        )
    out.append("};")
    out.append(
        "static_assert(sizeof(k{0}Cache) / sizeof(k{0}Cache[0]) == "
        "foptim::optim::cost_model::kNumCacheLevels);".format(arch_name)
    )
    out.append(
        f"static constexpr double k{arch_name}MemLatency = {float(rows['MEM']['latency'])};"
    )
    return out


def main():
    args = sys.argv[2:]
    if len(sys.argv) < 3 or any("=" not in a for a in args):
        raise SystemExit(__doc__)
    out_path = Path(sys.argv[1])
    archs = []
    caches = {}
    for a in args:
        key, path = a.split("=", 1)
        if key.startswith("cache:"):
            caches[key[len("cache:"):]] = Path(path)
        else:
            archs.append((key, Path(path)))
    for name, _ in archs:
        if name not in caches:
            raise SystemExit(f"no cache:{name}=<csv> given")
    order = read_enum_order()

    lines = ["// GENERATED FILE -- do not edit by hand."]
    for name, csv_path in archs:
        lines += table_lines(name, csv_path, order)
        lines += cache_lines(name, caches[name])
    lines.append("")
    lines.append(
        "inline const foptim::optim::cost_model::TableRow *table_of("
        "foptim::optim::cost_model::MicroArch arch) {"
    )
    lines.append("  switch (arch) {")
    for name, _ in archs:
        lines.append(f"    case foptim::optim::cost_model::MicroArch::{name}:")
        lines.append(f"      return k{name}Table;")
    lines.append("  }")
    lines.append("  return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append(
        "inline const foptim::optim::cost_model::CacheLevel *cache_of("
        "foptim::optim::cost_model::MicroArch arch) {"
    )
    lines.append("  switch (arch) {")
    for name, _ in archs:
        lines.append(f"    case foptim::optim::cost_model::MicroArch::{name}:")
        lines.append(f"      return k{name}Cache;")
    lines.append("  }")
    lines.append("  return nullptr;")
    lines.append("}")
    lines.append("")
    lines.append(
        "inline double mem_latency_of(foptim::optim::cost_model::MicroArch arch) {"
    )
    lines.append("  switch (arch) {")
    for name, _ in archs:
        lines.append(f"    case foptim::optim::cost_model::MicroArch::{name}:")
        lines.append(f"      return k{name}MemLatency;")
    lines.append("  }")
    lines.append("  return 0.0;")
    lines.append("}")
    out_path.write_text("\n".join(lines) + "\n")
    print(f"wrote {out_path} ({len(archs)} microarchitectures)")


if __name__ == "__main__":
    main()
