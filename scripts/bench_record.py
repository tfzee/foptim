import os, platform, subprocess, re
import json
from datetime import datetime

collect_compiletimes = False
tests_to_record = []
# tests_to_record = ["fib.c", "matmul.cpp"]

def line_prepender(filename, line):
    with open(filename, 'r+') as f:
        content = f.read()
        f.seek(0, 0)
        f.write(line.rstrip('\r\n') + '\n' + content)

def get_processor_name():
    if platform.system() == "Windows":
        return platform.processor()
    elif platform.system() == "Darwin":
        os.environ['PATH'] = os.environ['PATH'] + os.pathsep + '/usr/sbin'
        command ="sysctl -n machdep.cpu.brand_string"
        return subprocess.check_output(command).strip()
    elif platform.system() == "Linux":
        command = "cat /proc/cpuinfo"
        all_info = subprocess.check_output(command, shell=True).decode().strip()
        for line in all_info.split("\n"):
            if "model name" in line:
                return re.sub( ".*model name.*:", "", line,1)
    return ""



res_dir = "../bench/Output/"
perf_name = res_dir + datetime.today().strftime('%Y_%m_%d_%H_%M') + "_perf.json"
comp_name = res_dir + datetime.today().strftime('%Y_%m_%d_%H_%M') + "_comp.json"

if __name__ == "__main__":
    out_dir = "../build"

    benches = [benchy for benchy in os.listdir("../bench/") if(benchy.endswith(".c") or benchy.endswith(".cpp"))]
    if(len(tests_to_record) != 0):
        benches = [bench for bench in benches if bench in tests_to_record];

    clang_options = "-fno-exceptions -fno-stack-protector -I../test/llvm_benchmark_adobe_cpp/"
    
   
    if collect_compiletimes:
        compile_command = f"hyperfine -N --export-json={comp_name}"
        for benchy in benches:
            clang_name = f"clang{'++' if benchy.endswith(".cpp") else ''}"
            clang_command = f"{clang_name} -mllvm -disable-llvm-optzns -03 {clang_options} ../bench/{benchy} -o {out_dir}/{benchy}.tmp.ll -S -emit-llvm"
            os.system(clang_command)
    
            name = ".".join(benchy.split(".")[:-1])
            compile_command += f" -n {name}_compile"
            compile_command += f" \"../build/foptim_main {out_dir}/{benchy}.tmp.ll {out_dir}/{benchy}.tmp.o\""

            compile_command += f" -n {name}_clang_O1_compile_baseline"
            compile_command += f" \"{clang_name} ../bench/{benchy} -march=native -O1 {clang_options} -o {out_dir}/{benchy}_clang_O1.tmp.out\""

            compile_command += f" -n {name}_clang_O3_compile_baseline"
            compile_command += f" \"{clang_name} ../bench/{benchy} -march=native -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out\""
            compile_command += f" -n {name}_gcc_O3_compile_baseline"
            compile_command += f" \"g++ ../bench/{benchy} -march=native -O3 {clang_options} -o {out_dir}/{benchy}_gcc_O3.tmp.out\""
        os.system(compile_command)  
    else:
        for benchy in benches:
            clang_name = f"clang{'++' if benchy.endswith(".cpp") else ''}"
            clang_command = f"{clang_name} -mllvm -disable-llvm-optzns -O3 {clang_options} ../bench/{benchy} -o {out_dir}/{benchy}.tmp.ll -S -emit-llvm"
            os.system(clang_command)
            print(clang_command)
            os.system(f"../build/foptim_main {out_dir}/{benchy}.tmp.ll {out_dir}/{benchy}.tmp.o")
            os.system(f"{clang_name} ../bench/{benchy} -march=native -O1 {clang_options} -o {out_dir}/{benchy}_clang_O1.tmp.out")
            os.system(f"{clang_name} ../bench/{benchy} -march=native -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out")
            os.system(f"g++ ../bench/{benchy} -march=native -O3 {clang_options} -o {out_dir}/{benchy}_gcc_O3.tmp.out")
            print(f"clang++ {out_dir}/{benchy}.tmp.ll -march=native -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out")
            

    # print(hyperfine_compile_command)

    hyperfine_run_command = f"hyperfine -i -N --export-json={perf_name}"
    for benchy in benches:
        link_command = f"clang{'++' if benchy.endswith(".cpp") else ''} {out_dir}/{benchy}.tmp.o -o {out_dir}/{benchy}.tmp.out"
        os.system(link_command)

        name = ".".join(benchy.split(".")[:-1])
        hyperfine_run_command += f" -n {name}_run"
        hyperfine_run_command += f" \"{out_dir}/{benchy}.tmp.out\""

        hyperfine_run_command += f" -n {name}_clang_O1_run_baseline"
        hyperfine_run_command += f" \"./{out_dir}/{benchy}_clang_O1.tmp.out\""

        hyperfine_run_command += f" -n {name}_clang_O3_run_baseline"
        hyperfine_run_command += f" \"./{out_dir}/{benchy}_clang_O3.tmp.out\""

        hyperfine_run_command += f" -n {name}_gcc_O3_run_baseline"
        hyperfine_run_command += f" \"./{out_dir}/{benchy}_gcc_O3.tmp.out\""

    os.system(hyperfine_run_command)  

    with open(perf_name, "r") as read_data:
        d = json.load(read_data)
    d["processor"] = get_processor_name()
    with open(perf_name, "w") as write_data:
        json.dump(d, write_data, indent=4)
