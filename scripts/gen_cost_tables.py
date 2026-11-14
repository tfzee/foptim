#!/usr/bin/env python3
# AI SLOP
"""Generates a cost-model TableRow array from a CSV.

Usage:
    scripts/gen_cost_tables.py <output.inc> <ArchName>=<csv-path>...

One table per microarchitecture plus table_of(MicroArch), which needs a
MicroArch enumerator named ArchName. CMake runs it into the build directory
whenever a csv, this script or the X86OpClass enum changes. The op-class order
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


def main():
    if len(sys.argv) < 3 or any("=" not in a for a in sys.argv[2:]):
        raise SystemExit(__doc__)
    out_path = Path(sys.argv[1])
    archs = [(a.split("=", 1)[0], Path(a.split("=", 1)[1])) for a in sys.argv[2:]]
    order = read_enum_order()

    lines = ["// GENERATED FILE -- do not edit by hand."]
    for name, csv_path in archs:
        lines += table_lines(name, csv_path, order)
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
    out_path.write_text("\n".join(lines) + "\n")
    print(f"wrote {out_path} ({len(archs)} microarchitectures)")


if __name__ == "__main__":
    main()
