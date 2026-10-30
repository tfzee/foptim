#!/usr/bin/env python3
# Drop in replacement for clang / clang++ that runs foptim instead of the llvm
# optimizer. No llvm optimizations are ever run, only the clang frontend.
#
# Without -flto every translation unit goes through foptim_main on its own at
# -c time and the link is a plain system link. With -flto (the last of
# -flto / -flto=* / -fno-lto wins) -c only runs the frontend and leaves bitcode
# in the object file, linking merges all bitcode into one module, runs
# foptim_main on it once and links the result with the system linker.
#
# Use it as CC / CXX (the program name decides C or C++: foptim-cc / foptim-c++,
# see make_links below), e.g. CC=foptim-cc ./configure && make.
#
# Environment:
#   FOPTIM_MAIN      path of foptim_main (default: ../build/foptim_main)
#   FOPTIM_CLANG     clang used as frontend and linker driver (default: clang)
#   FOPTIM_CLANGXX   clang++ (default: clang++)
#   FOPTIM_LLVM_LINK llvm-link (default: llvm-link)
#   FOPTIM_AR        ar used to read archives (default: ar)
#   FOPTIM_FLAGS     extra flags for foptim_main, split like a shell would
#   FOPTIM_CC_LOG    file that gets one line per foptim run (appended)
#   FOPTIM_CC_KEEP   directory that gets a copy of every bitcode file foptim_main
#                    is run on (named after the source or the linked binary)
#
# There is no fallback: input foptim cannot compile (inline asm, shared
# libraries, a crash in foptim_main, ...) fails the command.
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.realpath(__file__))
MAIN = os.environ.get("FOPTIM_MAIN", os.path.join(HERE, "..", "build", "foptim_main"))
CLANG = os.environ.get("FOPTIM_CLANG", "clang")
CLANGXX = os.environ.get("FOPTIM_CLANGXX", "clang++")
LLVM_LINK = os.environ.get("FOPTIM_LLVM_LINK", "llvm-link")
AR = os.environ.get("FOPTIM_AR", "ar")

C_EXT = {".c"}
CXX_EXT = {".cc", ".cpp", ".cxx", ".c++", ".C", ".cp"}
ASM_EXT = {".s", ".S", ".sx"}
OBJ_EXT = {".o", ".obj", ".lo", ".bc"}

# options whose value is the next argument
TAKES_VALUE = {
    "-o", "-x", "-I", "-D", "-U", "-include", "-imacros", "-isystem", "-idirafter",
    "-iquote", "-isysroot", "-iprefix", "-iwithprefix", "-iwithprefixbefore",
    "-MF", "-MT", "-MQ", "-MJ", "-Xclang", "-Xpreprocessor", "-Xlinker",
    "-Xassembler", "-L", "-l", "-T", "-u", "-z", "-target", "--target", "-arch",
    "-mllvm", "--sysroot", "-working-directory", "-fuse-ld", "-B",
    "-rpath", "-soname", "-e", "-Tbss", "-Tdata", "-Ttext",
}
# these go to the linker only
LINK_ONLY_PREFIX = ("-l", "-L", "-Wl,", "-fuse-ld=", "-rdynamic", "-static", "-s",
                    "-nostdlib", "-nodefaultlibs", "-nostartfiles", "-pie", "-no-pie",
                    "-z", "-T", "-u", "-Xlinker", "-static-libstdc++", "-static-libgcc",
                    "-shared-libgcc", "-rpath", "-e", "-B")
DROP_PREFIX = ("-O", "-g", "-fprofile", "-fcoverage",
               "-fstack-protector", "-fno-stack-protector", "-fstack-clash",
               "-D_FORTIFY_SOURCE", "-ffat-lto-objects", "-fsave-optimization-record",
               "-Rpass", "-fdebug-prefix-map", "-fmacro-prefix-map")
# preprocess / query modes that never produce code, forwarded untouched
QUERY_FLAGS = {"-E", "-M", "-MM", "--version", "-dumpmachine", "-dumpversion", "-###",
               "-v", "--help", "-help", "-print-search-dirs", "-print-resource-dir",
               "-print-targets", "-print-supported-cpus", "-dumpspecs", "-cc1", "-S",
               "-fsyntax-only"}
QUERY_PREFIX = ("-print-prog-name", "-print-file-name", "-print-libgcc-file-name",
                "-print-multi", "-print-runtime-dir", "-print-target-triple",
                "-print-effective-triple", "-dump", "-verify-", "-analyze")


def die(msg, code=1):
    sys.stderr.write("foptim-cc: " + msg + "\n")
    sys.exit(code)


def is_cxx_invocation():
    name = os.path.basename(sys.argv[0])
    return name.endswith("++") or name.endswith("xx") or name.endswith("cpp")


def expand_response_files(args):
    out = []
    for a in args:
        if a.startswith("@") and os.path.isfile(a[1:]):
            with open(a[1:]) as f:
                out.extend(expand_response_files(shlex.split(f.read())))
        else:
            out.append(a)
    return out


class Cmd:
    def __init__(self, args):
        self.cxx = is_cxx_invocation()
        self.out = None
        self.compile_only = False
        self.shared = False
        self.lto = False
        self.frontend = []  # flags for the clang frontend
        self.link = []  # flags for the final link only
        self.inputs = []  # (path, lang or None)
        self.lang = None
        self.parse(args)

    def parse(self, args):
        i = 0
        n = len(args)

        def value(opt):
            nonlocal i
            # -Ifoo or -I foo
            if len(args[i]) > len(opt):
                return args[i][len(opt):]
            i += 1
            if i >= n:
                die("missing argument to " + opt)
            return args[i]

        while i < n:
            a = args[i]
            if a == "-o":
                self.out = value("-o")
            elif a.startswith("-o") and len(a) > 2 and not a.startswith("-obj"):
                self.out = a[2:]
            elif a == "-c":
                self.compile_only = True
            elif a == "-shared" or a == "-dynamiclib" or a == "-r" or a == "-Wl,-r":
                self.shared = True
            elif a == "-x":
                self.lang = value("-x")
                if self.lang == "none":
                    self.lang = None
            elif a.startswith("-x") and len(a) > 2:
                self.lang = a[2:]
            elif a == "-flto" or a.startswith("-flto="):
                self.lto = True
            elif a == "-fno-lto":
                self.lto = False
            elif a.startswith("-fsanitize") or a == "-pg" or a == "-fopenmp":
                die("unsupported option " + a)
            elif a.startswith(DROP_PREFIX):
                # -O* and -g* are replaced by the fixed frontend flags, a value
                # of a few of the others is a separate argument
                if a in ("-fdebug-prefix-map", "-fmacro-prefix-map"):
                    i += 1
            elif a in ("-l", "-L", "-T", "-u", "-z", "-rpath", "-e", "-B", "-Xlinker"):
                i += 1
                self.link += [a, args[i]] if i < n else [a]
            elif a.startswith(LINK_ONLY_PREFIX) and a not in ("-static-pie",):
                self.link.append(a)
            elif a in ("-pthread", "-static-pie", "-pipe", "-w", "-nostdinc",
                       "-nostdinc++", "-nostdlib++"):
                self.frontend.append(a)
                if a in ("-pthread", "-static-pie"):
                    self.link.append(a)
            elif a in TAKES_VALUE:
                i += 1
                if i >= n:
                    die("missing argument to " + a)
                self.frontend += [a, args[i]]
                if a in ("-target", "--target", "-arch", "--sysroot", "-isysroot"):
                    self.link += [a, args[i]]
            elif a.startswith("--sysroot=") or a.startswith("--target=") or a.startswith("-stdlib="):
                self.frontend.append(a)
                self.link.append(a)
            elif a.startswith("-") and a != "-":
                self.frontend.append(a)
            else:
                self.inputs.append((a, self.lang))
            i += 1

    def is_query(self, args):
        for a in args:
            if a in QUERY_FLAGS or a.startswith(QUERY_PREFIX):
                return True
        return False


def lang_of(path, lang):
    if lang:
        return lang
    ext = os.path.splitext(path)[1]
    if ext in C_EXT:
        return "c"
    if ext in CXX_EXT:
        return "c++"
    if ext in ASM_EXT or ext == ".S":
        return "asm"
    if ext in (".i",):
        return "cpp-output"
    if ext in (".ii",):
        return "c++-cpp-output"
    return None


def is_bitcode(path):
    try:
        with open(path, "rb") as f:
            magic = f.read(4)
    except OSError:
        return False
    return magic == b"BC\xc0\xde" or magic == b"\xde\xc0\x17\x0b"


def is_archive(path):
    try:
        with open(path, "rb") as f:
            return f.read(8) == b"!<arch>\n"
    except OSError:
        return False


def run(cmd, **kw):
    if os.environ.get("FOPTIM_CC_VERBOSE"):
        sys.stderr.write("+ " + " ".join(shlex.quote(c) for c in cmd) + "\n")
    return subprocess.run(cmd, **kw)


def frontend_cmd(cmd, src, lang, out):
    clang = CLANGXX if (lang in ("c++", "c++-cpp-output") or (cmd.cxx and lang != "c")) else CLANG
    args = [clang, "-c", "-emit-llvm", "-O3", "-mllvm", "-disable-llvm-optzns",
            "-fno-stack-protector", "-fno-exceptions"]
    # the frontend must not choose its own cpu features for the module
    args += cmd.frontend
    if lang:
        args += ["-x", lang]
    args += [src, "-o", out]
    return args


def log(line):
    path = os.environ.get("FOPTIM_CC_LOG")
    if path:
        with open(path, "a") as f:
            f.write(line + "\n")


def extract_archive(path, tmp, counter):
    # archive members, bitcode ones are merged, others are linked as objects
    d = os.path.join(tmp, "ar%d" % counter)
    os.makedirs(d)
    r = run([AR, "t", os.path.abspath(path)], capture_output=True, text=True)
    if r.returncode != 0:
        die("cannot read archive " + path)
    members = [m for m in r.stdout.split("\n") if m]
    if run([AR, "x", os.path.abspath(path)], cwd=d).returncode != 0:
        die("cannot extract archive " + path)
    bc, obj = [], []
    for m in dict.fromkeys(members):
        p = os.path.join(d, m)
        if not os.path.isfile(p):
            continue
        (bc if is_bitcode(p) else obj).append(p)
    return bc, obj


def main():
    args = expand_response_files(sys.argv[1:])
    cmd = Cmd(args)
    clang = CLANGXX if cmd.cxx else CLANG

    # queries (configure probes of the preprocessor, version, search paths)
    if cmd.is_query(args) or not args:
        sys.exit(run([clang] + args).returncode)
    if cmd.shared:
        die("shared libraries and relocatable links are not supported")
    if not cmd.inputs:
        die("no input files")

    tmp = tempfile.mkdtemp(prefix="foptim-cc-")
    try:
        sys.exit(build(cmd, tmp))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def assemble(cmd, path, out):
    # assembly never goes through foptim, clang writes the object
    r = run([CLANG, "-c"] + cmd.frontend + [path, "-o", out])
    if r.returncode != 0:
        sys.exit(r.returncode)


def compile_source(cmd, path, lang, out):
    lang = lang_of(path, lang)
    if lang == "asm":
        return assemble(cmd, path, out)
    if lang is None:
        die("unknown input file type: " + path)
    r = run(frontend_cmd(cmd, path, lang, out))
    if r.returncode != 0:
        sys.exit(r.returncode)


def keep_bitcode(inp, what):
    d = os.environ.get("FOPTIM_CC_KEEP")
    if not d:
        return
    os.makedirs(d, exist_ok=True)
    base = os.path.basename(what.split(" ")[0]) or "module"
    dst = os.path.join(d, base + ".bc")
    n = 0
    while os.path.exists(dst):
        n += 1
        dst = os.path.join(d, "%s.%d.bc" % (base, n))
    shutil.copyfile(inp, dst)


def run_foptim(inp, obj, what):
    keep_bitcode(inp, what)
    flags = shlex.split(os.environ.get("FOPTIM_FLAGS", ""))
    t0 = time.time()
    r = run([MAIN, inp, obj] + flags, stdout=subprocess.DEVNULL)
    log("%s %.2fs rc=%d" % (what, time.time() - t0, r.returncode))
    if r.returncode != 0:
        die("foptim_main failed on " + what + " (not supported, no fallback)")


def compile_object(cmd, path, lang, out, tmp):
    # frontend to bitcode, then foptim_main writes the real object
    if lang_of(path, lang) == "asm":
        return assemble(cmd, path, out)
    bc = os.path.join(tmp, "tu%d.bc" % len(os.listdir(tmp)))
    compile_source(cmd, path, lang, bc)
    run_foptim(bc, out, path)


def build(cmd, tmp):
    if cmd.compile_only:
        # with lto the object file holds bitcode and foptim runs at link time
        srcs = [(p, l) for p, l in cmd.inputs if not os.path.splitext(p)[1] in OBJ_EXT]
        if cmd.out and len(srcs) > 1:
            die("cannot specify -o with -c and multiple input files")
        for p, l in srcs:
            out = cmd.out or os.path.splitext(os.path.basename(p))[0] + ".o"
            if cmd.lto:
                compile_source(cmd, p, l, out)
            else:
                compile_object(cmd, p, l, out, tmp)
        return 0

    bitcode, objects = [], []
    counter = 0
    for p, l in cmd.inputs:
        ext = os.path.splitext(p)[1]
        if ext in OBJ_EXT or ext == ".a" or is_archive(p) or is_bitcode(p):
            if is_archive(p):
                counter += 1
                bc, obj = extract_archive(p, tmp, counter)
                bitcode += bc
                objects += obj
            elif is_bitcode(p):
                bitcode.append(p)
            else:
                objects.append(p)
        elif ext == ".so" or ".so." in p:
            objects.append(p)
        else:
            counter += 1
            if cmd.lto and lang_of(p, l) != "asm":
                out = os.path.join(tmp, "src%d.bc" % counter)
                compile_source(cmd, p, l, out)
                bitcode.append(out)
            else:
                out = os.path.join(tmp, "src%d.o" % counter)
                compile_object(cmd, p, l, out, tmp)
                objects.append(out)

    out = cmd.out or "a.out"
    link_args = []
    if bitcode:
        merged = os.path.join(tmp, "merged.bc")
        if len(bitcode) == 1:
            merged = bitcode[0]
        else:
            r = run([LLVM_LINK] + bitcode + ["-o", merged])
            if r.returncode != 0:
                die("llvm-link failed")
        obj = os.path.join(tmp, "foptim.o")
        run_foptim(merged, obj, "%s (%d modules)" % (out, len(bitcode)))
        link_args.append(obj)
    link_args += objects + cmd.link
    clang = CLANGXX if cmd.cxx else CLANG
    if not link_args:
        die("nothing to link")
    return run([clang] + link_args + ["-o", out]).returncode


def make_links():
    # foptim-cc and foptim-c++ next to this file, what CC / CXX point at
    for name in ("foptim-cc", "foptim-c++"):
        link = os.path.join(HERE, name)
        if not os.path.lexists(link):
            os.symlink(os.path.basename(__file__), link)


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--make-links":
        make_links()
    else:
        main()
