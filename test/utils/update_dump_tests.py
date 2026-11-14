#!/usr/bin/env python3
# ai slop
"""A test case update script for the debug dumps of analysis / print passes.

update_fir_tests.py regenerates CHECKs for the FIR printer. This is its
sibling for the per function debug dumps the printing passes write, which the
FIR script cannot read because they are not FIR:

    LoopNestOpt{print=true}       -> "loop nests of <f>"
    PrintCostModel{show=[...]}    -> "cost model instrs|blocks|loops of <f>"
    PrintAnalysis{show=[...]}     -> "accesses|provenance|alias|loop alias of <f>"
                                     "memory ssa of <f>"

Every one of those headers is `<kind> of <function name>`, which is what makes
one script enough. A dump block runs from its header to the next header, to
the start of the FIR dump, or to the end of the output; blank lines inside it
are dropped, because a blank line cannot be matched with CHECK-NEXT.

Several dumps of one function (`show=["accesses","loop_alias"]`) are emitted
in the order the pass wrote them, all under the same `define`.

Usage:
    test/utils/update_dump_tests.py --tool-binary build/foptim_main \\
        test/opt/loopnest/deps.ll

Check the diff before committing it: the point of these tests is that someone
decided the numbers are right, and a regenerated CHECK only says the compiler
is consistent with itself.
"""

from __future__ import print_function

import argparse
import os
import re
import sys
from traceback import print_exc

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from UpdateTestChecks import common

# `<kind> of <name>` at the start of a line, then everything up to the next
# such header, the start of the FIR dump, or the end. The kind is lower case
# words, which is what keeps this from matching a line of dump body that
# happens to say "of".
#
# When the RUN line also passes --print-fir, the FIR printer precedes every
# function with a blank line and one comment line holding the function
# attributes (ir/function.cpp writes "\n; " and then the attribute list). That
# is `; ` for a function with no attributes and `; NO_RECURSE, MEM(READ), `
# for one with some, so the terminator is a comment line, not a fixed string.
# The globals the FIR printer writes before the functions end a block too.
DUMP_BLOCK_RE = re.compile(
    r"^(?P<header>(?P<kind>[a-z][a-z0-9 ]*?) of (?P<func>[A-Za-z0-9_.$-]+))[ \t]*\n"
    r"(?P<body>.*?)"
    r"(?=^[a-z][a-z0-9 ]*? of [A-Za-z0-9_.$-]+[ \t]*\n|^;|^func\s|^GLOBAL\s|\Z)",
    flags=(re.M | re.S),
)


def escape_filecheck(line):
    """Makes sure FileCheck does not read printed output as regex or variable."""
    return line.replace("{{", "{{[{][{]}}").replace("[[", "{{[[]}}[")


def parse_blocks(output):
    """[(header, func, [body line, ...]), ...] in the order the tool wrote them."""
    blocks = []
    for m in DUMP_BLOCK_RE.finditer(output):
        # a blank line cannot be matched with CHECK-NEXT, and the ones at the
        # end are the separator the pass prints between functions anyway
        lines = [l.rstrip() for l in m.group("body").splitlines() if l.strip()]
        blocks.append((m.group("header"), m.group("func"), lines))
    return blocks


def add_dump_checks(output_lines, run_list, blocks_by_prefix, func_name):
    """CHECK lines for every dump block of one function, for every prefix."""
    printed_prefixes = set()
    for prefixes, _, _, _ in run_list:
        for prefix in prefixes:
            if prefix in printed_prefixes:
                continue
            blocks = [b for b in blocks_by_prefix.get(prefix, []) if b[1] == func_name]
            if not blocks:
                continue
            printed_prefixes.add(prefix)
            for header, _, body in blocks:
                output_lines.append(
                    "; {}-LABEL: {}".format(prefix, escape_filecheck(header))
                )
                for line in body:
                    output_lines.append(
                        "; {}-NEXT:{}".format(prefix, escape_filecheck(line))
                    )


def update_test(ti: common.TestInfo):
    run_list = []
    for line in ti.run_lines:
        if "|" not in line:
            common.warn("Skipping unparsable RUN line: " + line)
            continue
        tool_cmd, filecheck_cmd, preprocess_cmd = common.split_run_line(line)
        tool_bin = tool_cmd.split(" ")[0]
        if not filecheck_cmd.startswith("FileCheck "):
            common.warn("Skipping non-FileChecked RUN line: " + line)
            continue
        tool_cmd_args = tool_cmd[len(tool_bin):].strip()
        tool_cmd_args = tool_cmd_args.replace("< %s", "").replace("%s", "").strip()
        run_list.append(
            (common.get_check_prefixes(filecheck_cmd), tool_bin, tool_cmd_args,
             preprocess_cmd)
        )

    blocks_by_prefix = {}
    for prefixes, tool_bin, tool_args, preprocess_cmd in run_list:
        common.debug("Extracted tool cmd:", tool_bin, tool_args)
        raw = common.invoke_tool(
            ti.args.tool_binary or tool_bin,
            tool_args,
            ti.path,
            preprocess_cmd,
            verbose=ti.args.verbose,
        )
        blocks = parse_blocks(raw)
        if not blocks:
            common.warn(
                "No `<kind> of <function>` dump block in the output of: "
                + tool_args
                + " -- does the pass option that prints it have to be on?"
            )
        for prefix in prefixes:
            blocks_by_prefix[prefix] = blocks

    prefix_set = set(prefix for p in run_list for prefix in p[0])
    output_lines = []
    is_in_function = False
    is_in_function_start = False
    func_name = None

    for input_info in ti.iterlines(output_lines):
        input_line = input_info.line

        if is_in_function_start:
            if input_line == "":
                continue
            if input_line.lstrip().startswith(";"):
                m = common.CHECK_RE.match(input_line)
                if not m or m.group(1) not in prefix_set:
                    output_lines.append(input_line)
                    continue
            add_dump_checks(output_lines, run_list, blocks_by_prefix, func_name)
            is_in_function_start = False

        if is_in_function:
            if common.should_add_line_to_output(input_line, prefix_set):
                output_lines.append(input_line)
            if input_line.strip() == "}":
                is_in_function = False
            continue

        output_lines.append(input_line)

        m = common.IR_FUNCTION_RE.match(input_line)
        if not m:
            continue
        func_name = m.group(1)
        if ti.args.function is not None and func_name != ti.args.function:
            continue
        is_in_function = is_in_function_start = True

    common.debug("Writing %d lines to %s..." % (len(output_lines), ti.path))
    with open(ti.path, "wb") as f:
        f.writelines(["{}\n".format(l).encode("utf-8") for l in output_lines])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--tool-binary",
        default=None,
        help="The binary to use to generate the test case (e.g. foffcc)",
    )
    parser.add_argument("--function", help="The function in the test file to update")
    parser.add_argument("tests", nargs="+")

    initial_args = common.parse_commandline_args(parser)
    script_name = os.path.basename(__file__)

    returncode = 0
    for ti in common.itertests(
        initial_args.tests, parser, script_name="utils/" + script_name
    ):
        try:
            update_test(ti)
        except Exception:
            sys.stderr.write(f"Error: Failed to update test {ti.path}\n")
            print_exc()
            returncode = 1
    return returncode


if __name__ == "__main__":
    sys.exit(main())
