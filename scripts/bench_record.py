import os

if __name__ == "__main__":
    out_dir = "../test/Output"
    hyperfine_compile_command = f"hyperfine -N --export-csv={out_dir}/compile.csv"
    
    for benchy in os.listdir("../test/"):
        if(benchy.endswith(".c") or benchy.endswith(".cpp")):
            os.system(f"clang -O0 -Xclang -disable-O0-optnone  ../test/{benchy} -o {out_dir}/{benchy}.tmp.ll -S -emit-llvm")
            
            name = ".".join(benchy.split(".")[:-1])
            hyperfine_compile_command += f" -n {name}_compile"
            hyperfine_compile_command += f" \"../build/foptim_main {out_dir}/{benchy}.tmp.ll {out_dir}/{benchy}.tmp.ss\""

            hyperfine_compile_command += f" -n {name}_clang_O1_compile_baseline"
            hyperfine_compile_command += f" \"clang {out_dir}/{benchy}.tmp.ll -O1 -o {out_dir}/{benchy}_clang_O1.tmp.out\""

            hyperfine_compile_command += f" -n {name}_clang_O3_compile_baseline"
            hyperfine_compile_command += f" \"clang {out_dir}/{benchy}.tmp.ll -O3 -o {out_dir}/{benchy}_clang_O3.tmp.out\""

    os.system(hyperfine_compile_command)  

    hyperfine_run_command = f"hyperfine -i -N --export-csv={out_dir}/perf.csv"
    for benchy in os.listdir("../test/"):
        if(benchy.endswith(".c") or benchy.endswith(".cpp")):
            os.system(f"nasm {out_dir}/{benchy}.tmp.ss -felf64 -g -F dwarf && ld {out_dir}/{benchy}.tmp.o -o {out_dir}/{benchy}.tmp.out")

            name = ".".join(benchy.split(".")[:-1])
            hyperfine_run_command += f" -n {name}_run"
            hyperfine_run_command += f" \"{out_dir}/{benchy}.tmp.out\""

            hyperfine_run_command += f" -n {name}_clang_O1_run_baseline"
            hyperfine_run_command += f" \"./{out_dir}/{benchy}_clang_O1.tmp.out\""

            hyperfine_run_command += f" -n {name}_clang_O3_run_baseline"
            hyperfine_run_command += f" \"./{out_dir}/{benchy}_clang_O3.tmp.out\""

    # os.system(hyperfine_run_command)  

    # for benchy in os.listdir("../test/"):
    #     if(benchy.endswith(".c") or benchy.endswith(".cpp")):
    #         os.system(f"nasm {out_dir}/{benchy}.tmp.ss -felf64 -g -F dwarf && ld {out_dir}/{benchy}.tmp.o -o {out_dir}/{benchy}.tmp.out")

    #         name = ".".join(benchy.split(".")[:-1])
    #         hyperfine_run_command += f" -n {name}_run"
    #         hyperfine_run_command += f" \"{out_dir}/{benchy}.tmp.out\""

    # os.system(hyperfine_run_command)  



