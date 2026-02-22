import os
import subprocess
import difflib
import tempfile
import lit.formats
import lit.Test

class ReferenceOutputTest(lit.formats.TestFormat):
    def execute(self, test, lit_config):
        source_path = test.getSourcePath()
        # Get the path in the build directory
        exec_path = test.getExecPath() 
        exec_dir = os.path.dirname(exec_path)
        
        # 1. Use exist_ok=True to prevent the race condition
        tmp_dir = os.path.join(exec_dir, "Output")
        os.makedirs(tmp_dir, exist_ok=True)

        # 2. Use a unique base name for artifacts to avoid parallel collisions
        test_name = os.path.basename(exec_path)
        t_ll = os.path.join(tmp_dir, f"{test_name}.ll")
        t_o = os.path.join(tmp_dir, f"{test_name}.o")
        t_out = os.path.join(tmp_dir, f"{test_name}.out")

        base, _ = os.path.splitext(source_path)
        ref_file = base + ".reference_output"

        if not os.path.exists(ref_file):
            return lit.Test.UNSUPPORTED, "missing .reference_output file"

        foptim = os.path.join(test.config.foptim_bin_dir, "foptim_main")

        # Step 1: clang -> LLVM IR
        cmd1 = ["clang++", "-std=c++26", "-fno-exceptions", "-O0", source_path, "-o", t_ll, "-S", "-emit-llvm"]
        r1 = subprocess.run(cmd1, capture_output=True, text=True)
        if r1.returncode != 0:
            return lit.Test.FAIL, f"clang failed:\n{r1.stdout}\n{r1.stderr}"

        # Step 2: run optimizer
        cmd2 = [foptim, t_ll, t_o]
        r2 = subprocess.run(cmd2, capture_output=True, text=True)
        if r2.returncode != 0:
            return lit.Test.FAIL, f"foptim failed:\n{r2.stdout}\n{r2.stderr}"

        # Step 3: link
        cmd3 = ["clang++", "-static-libstdc++", t_o, "-o", t_out]
        r3 = subprocess.run(cmd3, capture_output=True, text=True)
        if r3.returncode != 0:
            return lit.Test.FAIL, f"clang link failed:\n{r3.stdout}\n{r3.stderr}"

        # Step 4: run binary
        try:
            r4 = subprocess.run(
                [t_out],
                capture_output=True,
                text=True,
                timeout=20,
            )
            actual_output = (r4.stdout + r4.stderr).strip()
            actual_output += f"\nexit {r4.returncode}"
        except subprocess.TimeoutExpired:
            return lit.Test.FAIL, "Test timed out after 20 seconds"
        except Exception as e:
            return lit.Test.FAIL, f"program crashed: {e}"

        # Step 5: compare with reference
        with open(ref_file) as f:
            expected = f.read().strip()

        if actual_output == expected:
            return lit.Test.PASS, ""
        else:
            # Fixed the difflib call (no more placeholders!)
            diff = "\n".join(
                difflib.unified_diff(
                    expected.splitlines(),
                    actual_output.splitlines(),
                    fromfile="expected",
                    tofile="actual",
                    lineterm=""
                )
            )
            return lit.Test.FAIL, "output mismatch:\n" + diff

class MultiFormat(lit.formats.TestFormat):
    def __init__(self):
        self.sh = lit.formats.ShTest(execute_external=True)
        self.ref = ReferenceOutputTest()

    def getTestsInDirectory(self, testSuite, path_in_suite, litConfig, localConfig):
        yield from self.sh.getTestsInDirectory(testSuite, path_in_suite, litConfig, localConfig)
        # yield from self.ref.getTestsInDirectory(testSuite, path_in_suite, litConfig, localConfig)

    def execute(self, test, lit_config):
        path = test.getSourcePath()
        if os.path.exists(os.path.splitext(path)[0] + ".reference_output"):
            return self.ref.execute(test, lit_config)
        elif path.endswith(".c") or path.endswith(".cpp"):
            return self.sh.execute(test, lit_config)
        else:
            return lit.Test.UNSUPPORTED, "no handler for this file type"
