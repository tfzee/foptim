#!/usr/bin/env python3

"""A test case update script for FIR (--print-fir) tests.

This script updates LLVM tests where the custom tool (e.g. foffcc) prints the
FIR module (`func name<...>\n{ ... }` blocks) after running the FIR passes
selected on the RUN line (`--print-fir --passes "..."`).
"""

from __future__ import print_function

import argparse
import os
import re
import sys
from traceback import print_exc

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from UpdateTestChecks import common

# Regex to capture "func <name><attrs...>\n{\n<body>\n}"
# Stops when it hits the next "func " or the end of the output.
CUSTOM_FIR_FUNC_RE = re.compile(
    r"^(?P<funcdef_attrs_and_ret>func\s+)(?P<func>[A-Za-z0-9_.$-]+)(?P<args_and_sig><[^\n]*)\n(?P<body>.*?)(?=^func\s|\Z)",
    flags=(re.M | re.S),
)


def scrub_custom_fir(body, *args, **kwargs):
    # Preserve internal indentation. The printer emits a blank line and a lone
    # `; ` comment line before every function, which lands at the end of the
    # previous function's body: drop them (a blank line also breaks -NEXT).
    lines = body.rstrip().splitlines()
    while lines and lines[-1].strip() in ("", ";"):
        lines.pop()
    return "\n".join(lines)


def escape_filecheck(line):
    """Makes sure FileCheck does not interpret printed IR as regex/variables."""
    return line.replace("{{", "{{[{][{]}}").replace("[[", "{{[[]}}[")


def add_custom_fir_checks(output_lines, check_indent, run_list, func_dict, func_name):
    """Generates the CHECK lines for the printed FIR of a function."""
    generated_prefixes = []
    printed_prefixes = set()

    for run in run_list:
        prefixes = run[0]
        for prefix in prefixes:
            if prefix in printed_prefixes:
                continue
            if not func_dict[prefix].get(func_name):
                continue

            printed_prefixes.add(prefix)
            generated_prefixes.append(prefix)

            func_info = func_dict[prefix][func_name]

            # 1. Anchor on the function header: `; CHECK-LABEL: func f<CC: C, ...> Uses: 0`
            sig_line = f"func {func_name}{func_info.args_and_sig.rstrip()}"
            output_lines.append(
                f"{check_indent} {prefix}-LABEL: {escape_filecheck(sig_line)}"
            )

            # 2. The body (`{`, blocks, instructions, `}`)
            for line in func_info.scrub.splitlines():
                if not line.strip():
                    continue
                output_lines.append(
                    f"{check_indent} {prefix}-NEXT:{escape_filecheck(line)}"
                )

    return generated_prefixes


def update_test(ti: common.TestInfo):
    run_list = []
    for l in ti.run_lines:
        if "|" not in l:
            common.warn("Skipping unparsable RUN line: " + l)
            continue

        tool_cmd, filecheck_cmd, preprocess_cmd = common.split_run_line(l)
        tool_bin = tool_cmd.split(" ")[0]

        if not filecheck_cmd.startswith("FileCheck "):
            common.warn("Skipping non-FileChecked RUN line: " + l)
            continue

        tool_cmd_args = tool_cmd[len(tool_bin) :].strip()
        tool_cmd_args = tool_cmd_args.replace("< %s", "").replace("%s", "").strip()
        check_prefixes = common.get_check_prefixes(filecheck_cmd)
        run_list.append(
            (check_prefixes, tool_bin, tool_cmd_args, preprocess_cmd)
        )
    # print(run_list)

    # Use a dummy flags object to enable function_signature capture
    flags = type("", (object,), {
        "verbose": ti.args.verbose,
        "filters": [],
        "function_signature": True, # Ensure (args) are captured in m.groupdict()
        "check_attributes": False,
        "replace_value_regex": [],
    })

    builder = common.FunctionTestBuilder(
        run_list=run_list,
        flags=flags,
        scrubber_args=[],
        path=ti.path,
        ginfo=common.make_asm_generalizer(version=1),
    )

    for prefixes, tool_bin, tool_args, preprocess_cmd in run_list:
        common.debug("Extracted tool cmd:", tool_bin, tool_args)
        
        # Run your tool (e.g., foffcc --print-fir)[cite: 1, 4]
        raw_tool_output = common.invoke_tool(
            ti.args.tool_binary or tool_bin,
            tool_args,
            ti.path,
            preprocess_cmd,
            verbose=ti.args.verbose,
        )
        
        # Process output using our custom FIR regex
        builder.process_run_line(CUSTOM_FIR_FUNC_RE, scrub_custom_fir, raw_tool_output, prefixes)
        builder.processed_prefixes(prefixes)

    func_dict = builder.finish_and_get_func_dict()

    is_in_function = False
    is_in_function_start = False
    func_name = None
    prefix_set = set([prefix for p in run_list for prefix in p[0]])
    output_lines = []

    for input_info in ti.iterlines(output_lines):
        input_line = input_info.line
        
        # Start inserting checks immediately inside the LLVM IR define block
        if is_in_function_start:
            if input_line == "":
                continue
            if input_line.lstrip().startswith(";"):
                m = common.CHECK_RE.match(input_line)
                if not m or m.group(1) not in prefix_set:
                    output_lines.append(input_line)
                    continue

            # Add our custom FIR check lines
            add_custom_fir_checks(
                output_lines,
                ";",
                run_list,
                func_dict,
                func_name
            )
            is_in_function_start = False

        if is_in_function:
            if common.should_add_line_to_output(input_line, prefix_set):
                output_lines.append(input_line)
            if input_line.strip() == "}":
                is_in_function = False
            continue

        output_lines.append(input_line)

        # Detect the start of a standard LLVM IR function[cite: 1]
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
