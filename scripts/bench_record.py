import os

collect_compiletimes = False
tests_to_record = ["matmul.cpp", "prime_sieve.c", "fib.c"]

if __name__ == "__main__":
    out_dir = "../build/test/Output"

    if collect_compiletimes:
        compile_command = f"hyperfine -N --export-csv={out_dir}/compile.csv"
    else:
        compile_command = f"clang --version"

    benches = [benchy for benchy in os.listdir("../test/") if(benchy.endswith(".c") or benchy.endswith(".cpp"))]
    benches = [bench for bench in benches if bench in tests_to_record];

    clang_options = "-ffreestanding -fno-exceptions -fno-stack-protector"
    
    
    for benchy in benches:
        if collect_compiletimes:
            clang_command = f"clang{'++' if benchy.endswith(".cpp") else ''} -O0 -Xclang -disable-O0-optnone {clang_options} ../test/{benchy} -o {out_dir}/{benchy}.tmp.ll -S -emit-llvm"
            os.system(clang_command)
        
            name = ".".join(benchy.split(".")[:-1])
            compile_command += f" -n {name}_compile"
            compile_command += f" \"../build/foptim_main {out_dir}/{benchy}.tmp.ll {out_dir}/{benchy}.tmp.ss\""

            compile_command += f" -n {name}_clang_O1_compile_baseline"
            compile_command += f" \"clang++ {out_dir}/{benchy}.tmp.ll -march=native -O1 {clang_options} -o {out_dir}/{benchy}_clang_O1.tmp.out\""

            compile_command += f" -n {name}_clang_O3_compile_baseline"
            compile_command += f" \"clang++ {out_dir}/{benchy}.tmp.ll -march=native -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out\""
        else:
            compile_command += f" && "
            compile_command += f" ../build/foptim_main {out_dir}/{benchy}.tmp.ll {out_dir}/{benchy}.tmp.ss"
            compile_command += f" && "
            compile_command += f" clang++ {out_dir}/{benchy}.tmp.ll -march=native -O1 {clang_options} -o {out_dir}/{benchy}_clang_O1.tmp.out"
            compile_command += f" && "
            compile_command += f" clang++ {out_dir}/{benchy}.tmp.ll -march=native -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out"
            

    # print(hyperfine_compile_command)
    os.system(compile_command)  

    hyperfine_run_command = f"hyperfine -i -N --export-csv={out_dir}/perf.csv"
    for benchy in benches:
        nasm_command = f"nasm {out_dir}/{benchy}.tmp.ss -felf64 -g -F dwarf && ld {out_dir}/{benchy}.tmp.o -o {out_dir}/{benchy}.tmp.out"
        print(nasm_command)
        os.system(nasm_command)

        name = ".".join(benchy.split(".")[:-1])
        hyperfine_run_command += f" -n {name}_run"
        hyperfine_run_command += f" \"{out_dir}/{benchy}.tmp.out\""

        hyperfine_run_command += f" -n {name}_clang_O1_run_baseline"
        hyperfine_run_command += f" \"./{out_dir}/{benchy}_clang_O1.tmp.out\""

        hyperfine_run_command += f" -n {name}_clang_O3_run_baseline"
        hyperfine_run_command += f" \"./{out_dir}/{benchy}_clang_O3.tmp.out\""

    os.system(hyperfine_run_command)  

    # for benchy in os.listdir("../test/"):
    #     if(benchy.endswith(".c") or benchy.endswith(".cpp")):
    #         os.system(f"nasm {out_dir}/{benchy}.tmp.ss -felf64 -g -F dwarf && ld {out_dir}/{benchy}.tmp.o -o {out_dir}/{benchy}.tmp.out")

    #         name = ".".join(benchy.split(".")[:-1])
    #         hyperfine_run_command += f" -n {name}_run"
    #         hyperfine_run_command += f" \"{out_dir}/{benchy}.tmp.out\""

    # os.system(hyperfine_run_command)  



