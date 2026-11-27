#!/usr/bin/env python3
"""Builds a cost model csv for one microarchitecture from the uops.info export.

Usage:
    scripts/import_uops.py <instructions.xml> <ARCH> <op_sizes.csv> <out.csv> [-v]

instructions.xml is https://uops.info/instructions.xml (about 140 MB), ARCH the
name used there (ZEN4, SKL, ...). Every X86OpClass is measured by the median
over a few instruction forms listed in CLASSES. uops.info has no encoded size,
that column comes from op_sizes.csv. -v prints the forms behind every row.
"""

import csv
import functools
import operator
import re
import statistics
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# class -> regexes on the uops.info instruction string, all matching forms count
CLASSES = {
    "ALU_RR": [r"^(ADD|SUB|AND|OR|XOR|CMP)(_\w+)? \(R64, R64\)$"],
    "ALU_RI": [r"^(ADD|SUB|AND|OR|XOR|CMP)(_\w+)? \(R64, I8\)$"],
    "ALU_RM": [r"^(ADD|SUB|AND|OR|XOR|CMP)(_\w+)? \(R64, M64\)$"],
    "LEA": [r"^LEA_B_I(S)? \(R64\)$", r"^LEA_B_D8 \(R64\)$"],
    "IMUL_RR": [r"^IMUL \(R64, R64\)$"],
    "IMUL_RM": [r"^IMUL \(R64, M64\)$"],
    "IDIV32": [r"^IDIV \(R32\)$"],
    "IDIV64": [r"^IDIV \(R64\)$"],
    "SHIFT_RI": [r"^(SHL|SHR|SAR) \(R64, I8\)$"],
    "SHIFT_RR": [r"^(SHL|SHR|SAR) \(R64, CL\)$"],
    "CMOV": [r"^CMOV(Z|NZ|L|G|B|A) \(R64, R64\)$"],
    "SETCC": [r"^SET(Z|NZ|L|G|B|A) \(R8l\)$"],
    "JCC": [r"^J(Z|NZ|L|G|B|A) \(Rel8\)$"],
    "JMP": [r"^JMP \(Rel8\)$"],
    "MOV_RR": [r"^MOV(_\w+)? \(R64, R64\)$"],
    "LOAD": [r"^MOV(_\w+)? \(R64, M64\)$"],
    "STORE": [r"^MOV(_\w+)? \(M64, R64\)$"],
    "POPCNT": [r"^POPCNT \(R64, R64\)$"],
    "LZCNT": [r"^LZCNT \(R64, R64\)$"],
    "TZCNT": [r"^TZCNT \(R64, R64\)$"],
    "BSWAP": [r"^BSWAP \(R64\)$"],
    "CALL": [r"^CALL_NEAR \(Rel32\)$"],
    "FP_ALU": [r"^V(ADD|SUB|MUL)SD \(XMM, XMM, XMM\)$"],
    "FP_DIV": [r"^V(DIV|SQRT)SD \(XMM, XMM(, XMM)?\)$"],
    "FP_CMP": [r"^VUCOMISD \(XMM, XMM\)$"],
    "VEC_ALU_128": [r"^VP(ADD|SUB)D \(XMM, XMM, XMM\)$", r"^VP(AND|OR|XOR) \(XMM, XMM, XMM\)$"],
    "VEC_ALU_256": [r"^VP(ADD|SUB)D \(YMM, YMM, YMM\)$", r"^VP(AND|OR|XOR) \(YMM, YMM, YMM\)$"],
    "VEC_SHUFFLE": [r"^VPSHUFD \(XMM, XMM, I8\)$", r"^VPSHUFB \(XMM, XMM, XMM\)$"],
    "VEC_IMUL": [r"^VPMULLD \(XMM, XMM, XMM\)$"],
    "VEC_FP_ALU": [r"^V(ADD|SUB|MUL)PS \(XMM, XMM, XMM\)$"],
    "VEC_FP_DIV": [r"^V(DIV|SQRT)PS \(XMM, XMM(, XMM)?\)$"],
    "VEC_CROSS_SHUFFLE": [r"^VPERMD \(YMM, YMM, YMM\)$", r"^VPERMQ \(YMM, YMM, I8\)$"],
    "VEC_BLEND": [r"^VPBLENDVB \(XMM, XMM, XMM, XMM\)$", r"^VBLENDVPS \(XMM, XMM, XMM, XMM\)$"],
    "VEC_CMP": [r"^VPCMP(EQ|GT)D \(XMM, XMM, XMM\)$"],
    "VEC_SHIFT": [r"^VPS(LL|RL|RA)D \(XMM, XMM, I8\)$", r"^VPS(LL|RL|RA)VD \(XMM, XMM, XMM\)$"],
    "VEC_MINMAX": [r"^VPM(IN|AX)SD \(XMM, XMM, XMM\)$", r"^VM(IN|AX)PS \(XMM, XMM, XMM\)$"],
    "VEC_GATHER": [r"^VPGATHERDD \(YMM, VSIB_YMM, YMM\)$"],
    "VEC_MASK_LOAD": [r"^VPMASKMOVD \(YMM, YMM, M256\)$"],
    "VEC_MASK_STORE": [r"^VPMASKMOVD \(M256, YMM, YMM\)$"],
    "VEC_LOAD": [r"^VMOVDQU \(YMM, M256\)$"],
    "VEC_STORE": [r"^VMOVDQU \(M256, YMM\)$"],
    "VEC_INSERT": [r"^VPINSRD \(XMM, XMM, R32, I8\)$"],
    "VEC_EXTRACT": [r"^VPEXTRD \(R32, XMM, I8\)$"],
    "VEC_BROADCAST": [r"^VPBROADCASTD \(YMM, XMM\)$"],
    "VEC_CVT": [r"^VCVTDQ2PS \(XMM, XMM\)$"],
}

# a macro fused cmp+jcc is one jcc uop, the fusion is not an instruction
DERIVED = {"CMP_JCC_FUSED": "JCC"}
# a store, a branch and a call produce no register value (uops.info has no
# latency of a call on every microarchitecture)
LATENCY_OVERRIDE = {"STORE": 1.0, "VEC_STORE": 1.0, "VEC_MASK_STORE": 1.0, "JCC": 1.0, "JMP": 1.0, "CALL": 1.0}

# abstract ports of Machine::kNumPorts, the named port groups of uops.info are
# mapped onto them, everything else from the throughput
PORT_GROUPS = {
    "ALU": 0xF, "ALU0/3": 0x9, "ALU1/2": 0x6, "ALU1": 0x2,
    "FP0/1/2/3": 0xF, "FP2/3": 0xC, "FP1/2": 0x6, "FP0/1": 0x3,
    "FP3": 0x8, "FP1": 0x2, "FP2": 0x4, "FP0": 0x1,
}
PORTS_FROM_THROUGHPUT = {"LOAD", "STORE", "ALU_RM", "VEC_LOAD", "VEC_STORE", "VEC_MASK_LOAD", "VEC_MASK_STORE", "VEC_GATHER"}
NUM_PORTS = 4


def first_number(text):
    m = re.search(r"\d+(\.\d+)?", text or "")
    return float(m.group(0)) if m else None


def latency_of(cls, arch_el):
    m = arch_el.find("measurement")
    doc = arch_el.find("doc")
    lats = m.findall("latency")
    addr = [float(l.get("cycles_addr")) for l in lats if l.get("cycles_addr")]
    # a memory source reads through the address, a load is that latency alone
    if cls in ("LOAD", "VEC_LOAD", "VEC_MASK_LOAD"):
        return min(addr) if addr else None
    if cls.endswith("_RM") and addr:
        return max(addr)
    if doc is not None and first_number(doc.get("latency")) is not None:
        return first_number(doc.get("latency"))
    dest = [l for l in lats if l.get("target_op") == "1"] or lats
    vals = [float(l.get("cycles") or l.get("min_cycles")) for l in dest if l.get("cycles") or l.get("min_cycles")]
    vals += addr
    return max(vals) if vals else None


def ports_of(cls, arch_el, tp, uops):
    doc = arch_el.find("doc")
    group = (doc.get("ports") or "").split(",")[0].strip() if doc is not None else ""
    if cls not in PORTS_FROM_THROUGHPUT and group in PORT_GROUPS:
        return PORT_GROUPS[group]
    n = max(1, min(NUM_PORTS, round(uops / tp))) if tp > 0 else 1
    return (1 << n) - 1


def collect(xml_path, arch):
    compiled = {c: [re.compile(p) for p in pats] for c, pats in CLASSES.items()}
    forms = {c: [] for c in CLASSES}
    for _, el in ET.iterparse(xml_path):
        if el.tag != "instruction":
            continue
        s = el.get("string")
        for cls, pats in compiled.items():
            if any(p.match(s) for p in pats):
                for a in el.findall("architecture"):
                    if a.get("name") == arch and a.find("measurement") is not None:
                        forms[cls].append((s, a))
        el.clear()
    return forms


def main():
    args = [a for a in sys.argv[1:] if a != "-v"]
    verbose = "-v" in sys.argv
    if len(args) != 4:
        raise SystemExit(__doc__)
    xml_path, arch, sizes_path, out_path = args[0], args[1], Path(args[2]), Path(args[3])
    sizes = {
        r["op_class"]: r["size_bytes"]
        for r in csv.DictReader(l for l in sizes_path.read_text().splitlines() if not l.startswith("#"))
    }
    forms = collect(xml_path, arch)
    rows = {}
    for cls, fs in forms.items():
        if not fs:
            raise SystemExit(f"no {arch} measurement for class {cls}")
        lat, tp, uops, ports = [], [], [], []
        for s, a in fs:
            m = a.find("measurement")
            t = float(m.get("TP_unrolled") or m.get("TP_loop"))
            u = float(m.get("uops"))
            l = latency_of(cls, a)
            if l is not None:
                lat.append(l)
            tp.append(t)
            uops.append(u)
            ports.append(ports_of(cls, a, t, u))
            if verbose:
                print(f"{cls:14} {s:30} tp={t} uops={u} lat={l} ports={ports[-1]:#x}")
        if not lat and cls not in LATENCY_OVERRIDE:
            raise SystemExit(f"no latency for class {cls}")
        rows[cls] = dict(
            latency=LATENCY_OVERRIDE.get(cls) or statistics.median(lat),
            recip_throughput=statistics.median(tp),
            ports_mask=functools.reduce(operator.or_, ports),
            n_uops=round(statistics.median(uops)),
        )
    for cls, src in DERIVED.items():
        rows[cls] = dict(rows[src], latency=1.0, n_uops=1)

    missing = [c for c in sizes if c not in rows]
    if missing:
        raise SystemExit(f"classes without data: {', '.join(missing)}")
    with out_path.open("w") as f:
        f.write(f"# {arch} cost model data from uops.info, generated by scripts/import_uops.py\n")
        f.write("# (median over the forms of each class, sizes from op_sizes.csv).\n")
        f.write("# ports_mask is over the abstract ports of Machine::kNumPorts.\n")
        f.write("op_class,latency,recip_throughput,ports_mask,n_uops,size_bytes\n")
        for cls in sizes:
            r = rows[cls]
            f.write(
                f"{cls},{r['latency']:g},{r['recip_throughput']:g},"
                f"{r['ports_mask']:#x},{r['n_uops']},{sizes[cls]}\n"
            )
    print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
