import os

collect_compiletimes = False
tests_to_record = []
# tests_to_record = ["matmul.cpp", "prime_sieve.c", "fib.c", "mandelbrot.cpp", "lu_decomp.cpp"]

if __name__ == "__main__":
    out_dir = "../build"
    res_dir = "../bench/Output"

    benches = [benchy for benchy in os.listdir("../bench/") if(benchy.endswith(".c") or benchy.endswith(".cpp"))]
    if(len(tests_to_record) != 0):
        benches = [bench for bench in benches if bench in tests_to_record];

    clang_options = "-fno-exceptions -fno-stack-protector -I../test/llvm_benchmark_adobe_cpp/"
    
   
    if collect_compiletimes:
        compile_command = f"hyperfine -N --export-csv={res_dir}/compile.csv"
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

    hyperfine_run_command = f"hyperfine -i -N --export-csv={res_dir}/perf.csv"
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

    # for benchy in os.listdir("../bench/"):
    #     if(benchy.endswith(".c") or benchy.endswith(".cpp")):
    #         os.system(f"nasm {out_dir}/{benchy}.tmp.ss -felf64 -g -F dwarf && ld {out_dir}/{benchy}.tmp.o -o {out_dir}/{benchy}.tmp.out")

    #         name = ".".join(benchy.split(".")[:-1])
    #         hyperfine_run_command += f" -n {name}_run"
    #         hyperfine_run_command += f" \"{out_dir}/{benchy}.tmp.out\""

    # os.system(hyperfine_run_command)  



