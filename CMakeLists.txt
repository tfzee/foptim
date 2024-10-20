cmake_minimum_required(VERSION 3.16.3)
project(foptim
        LANGUAGES CXX C)
set(CMAKE_CXX_STANDARD 23)

# set(CMAKE_CXX_CLANG_TIDY
#   clang-tidy;
#   # -format-style='file';
#   -checks=;
#   -header-filter=${CMAKE_CURRENT_SOURCE_DIR};
# )

################################################
#llvm
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})
llvm_map_components_to_libnames(llvm_libs support core irreader)

################################################
#FOPTIM lib

option(generation_check "check generations in references" ON)
option(tracy_perf "tracy" ON)
option(optim_stats "optimization stats" ON)
if(generation_check)
  add_definitions(-DSLOT_CHECK_GENERATION)
endif()
if(optim_stats)
  add_definitions(-DOPTIM_STATS)
endif()
if(tracy_perf)
  add_definitions(-DTRACY_ENABLE)
  link_libraries(tracy)
endif()

# add_compile_options(-fsanitize=undefined,address -fno-omit-frame-pointer)
# add_link_options(-fsanitize=undefined,address -fPIE -pie)

add_library (foptim
                #loading
                ./src/llvm/llir_loader.cpp

                #ir
                ./src/ir/function.cpp
                ./src/ir/basic_block.cpp
                ./src/ir/basic_block_ref.cpp
                ./src/ir/instruction_data.cpp
                ./src/ir/instruction.cpp
                ./src/ir/types.cpp
                ./src/ir/value.cpp
                ./src/ir/use.cpp
                ./src/ir/attributable.cpp
                ./src/ir/constant_value.cpp

                #optim
                ./src/optim/analysis/live_variables.cpp
                ./src/optim/analysis/loop_analysis.cpp
                ./src/optim/func_passes/mem2reg.cpp

                #mir
                ./src/mir/reg_alloc.cpp
                ./src/mir/matcher.cpp
                ./src/mir/optim/reg_alloc.cpp
                ./src/mir/optim/invoke_lower.cpp
                ./src/mir/optim/bb_reordering.cpp
                ./src/mir/optim/inst_simplify.cpp

                #codegen
                ./src/x86_codegen/backend.cpp

                #utils
                ./src/utils/logging.cpp
                ./src/utils/parameters.cpp
                ./src/utils/todo.cpp
                ./src/utils/arena.cpp

               )

target_compile_options(foptim PRIVATE -Wall -Wextra -Werror -ggdb -Wno-unused-private-field)
target_include_directories(foptim PUBLIC ./src/ ${LLVM_INCLUDE_DIRS})
target_link_libraries(foptim PRIVATE ${llvm_libs} asmjit)


################################################
#foptim main test
add_executable (foptim_main
                ./src/main.cpp

                #arg parsing
                ./src/arg_parsing/parser.cpp
               )

target_compile_options(foptim_main PRIVATE -Wall -Wextra -Werror -ggdb -Wno-unused-private-field)
target_link_libraries(foptim_main PRIVATE foptim)
target_include_directories(foptim_main PRIVATE ./src/)




