import os

collect_compiletimes = False
tests_to_record = ["mandelbrot.cpp", "matmul.cpp", "prime_sieve.c", "fib.c"]

if __name__ == "__main__":
    out_dir = "../build/test/Output"

    benches = [benchy for benchy in os.listdir("../test/") if(benchy.endswith(".c") or benchy.endswith(".cpp"))]
    benches = [bench for bench in benches if bench in tests_to_record];

    clang_options = "-fno-exceptions -fno-stack-protector"
    
   
    if collect_compiletimes:
        compile_command = f"hyperfine -N --export-csv={out_dir}/compile.csv"
        for benchy in benches:
            clang_command = f"clang{'++' if benchy.endswith(".cpp") else ''} -O0 -Xclang -disable-O0-optnone {clang_options} ../test/{benchy} -o {out_dir}/{benchy}.tmp.ll -S -emit-llvm"
            os.system(clang_command)
    
            name = ".".join(benchy.split(".")[:-1])
            compile_command += f" -n {name}_compile"
            compile_command += f" \"../build/foptim_main {out_dir}/{benchy}.tmp.ll {out_dir}/{benchy}.tmp.ss\""

            compile_command += f" -n {name}_clang_O1_compile_baseline"
            compile_command += f" \"clang++ {out_dir}/{benchy}.tmp.ll -march=native -O1 {clang_options} -o {out_dir}/{benchy}_clang_O1.tmp.out\""

            compile_command += f" -n {name}_clang_O3_compile_baseline"
            compile_command += f" \"clang++ {out_dir}/{benchy}.tmp.ll -march=native -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out\""
            compile_command += f" -n {name}_gcc_O3_compile_baseline"
            compile_command += f" \"g++ ../test/{benchy} -march=native -O3 {clang_options} -o {out_dir}/{benchy}_gcc_O3.tmp.out\""
        os.system(compile_command)  
    else:
        for benchy in benches:
            clang_command = f"clang{'++' if benchy.endswith(".cpp") else ''} -O0 -Xclang -disable-O0-optnone {clang_options} ../test/{benchy} -o {out_dir}/{benchy}.tmp.ll -S -emit-llvm"
            os.system(clang_command)
            print(clang_command)
            os.system(f"../build/foptim_main {out_dir}/{benchy}.tmp.ll {out_dir}/{benchy}.tmp.ss")
            os.system(f"clang++ {out_dir}/{benchy}.tmp.ll -march=native -O1 {clang_options} -o {out_dir}/{benchy}_clang_O1.tmp.out")
            os.system(f"clang++ {out_dir}/{benchy}.tmp.ll -march=native -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out")
            os.system(f"g++ ../test/{benchy} -march=native -O3 {clang_options} -o {out_dir}/{benchy}_gcc_O3.tmp.out")
            print(f"clang++ {out_dir}/{benchy}.tmp.ll -march=native -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out")
            

    # print(hyperfine_compile_command)

    hyperfine_run_command = f"hyperfine -i -N --export-csv={out_dir}/perf.csv"
    for benchy in benches:
        nasm_command = f"nasm {out_dir}/{benchy}.tmp.ss -felf64 -g -F dwarf && gcc {out_dir}/{benchy}.tmp.o -o {out_dir}/{benchy}.tmp.out"
        print(nasm_command)
        os.system(nasm_command)

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

    # for benchy in os.listdir("../test/"):
    #     if(benchy.endswith(".c") or benchy.endswith(".cpp")):
    #         os.system(f"nasm {out_dir}/{benchy}.tmp.ss -felf64 -g -F dwarf && ld {out_dir}/{benchy}.tmp.o -o {out_dir}/{benchy}.tmp.out")

    #         name = ".".join(benchy.split(".")[:-1])
    #         hyperfine_run_command += f" -n {name}_run"
    #         hyperfine_run_command += f" \"{out_dir}/{benchy}.tmp.out\""

    # os.system(hyperfine_run_command)  



