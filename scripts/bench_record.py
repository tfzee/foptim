import os


tests_to_record = ["matmul.cpp", "prime_sieve.c"]

if __name__ == "__main__":
    out_dir = "../test/Output"
    hyperfine_compile_command = f"hyperfine -N --export-csv={out_dir}/compile.csv"

    benches = [benchy for benchy in os.listdir("../test/") if(benchy.endswith(".c") or benchy.endswith(".cpp"))]
    benches = [bench for bench in benches if bench in tests_to_record];

    clang_options = "-ffreestanding -fno-exceptions"
    
    
    for benchy in benches:
        clang_command = f"clang{'++' if benchy.endswith(".cpp") else ''} -O0 -Xclang -disable-O0-optnone {clang_options} ../test/{benchy} -o {out_dir}/{benchy}.tmp.ll -S -emit-llvm"
        os.system(clang_command)
        
        name = ".".join(benchy.split(".")[:-1])
        hyperfine_compile_command += f" -n {name}_compile"
        hyperfine_compile_command += f" \"../build/foptim_main {out_dir}/{benchy}.tmp.ll {out_dir}/{benchy}.tmp.ss\""

        hyperfine_compile_command += f" -n {name}_clang_O1_compile_baseline"
        hyperfine_compile_command += f" \"clang++ {out_dir}/{benchy}.tmp.ll -O1 {clang_options} -o {out_dir}/{benchy}_clang_O1.tmp.out\""

        hyperfine_compile_command += f" -n {name}_clang_O3_compile_baseline"
        hyperfine_compile_command += f" \"clang++ {out_dir}/{benchy}.tmp.ll -O3 {clang_options} -o {out_dir}/{benchy}_clang_O3.tmp.out\""

    # print(hyperfine_compile_command)
    os.system(hyperfine_compile_command)  

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



