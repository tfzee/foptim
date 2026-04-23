cmake_minimum_required(VERSION 3.16.3)
project(foptim
        LANGUAGES CXX C)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)
set(CMAKE_CXX_STANDARD 23)

set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_C_COMPILER "clang")

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES 
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()


include(FetchContent)

################################################
#threads
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

################################################
#llvm
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
option(LINK_STATIC_LLVM "Link against individual LLVM static libraries" OFF)
#cause different linux distros handle it differently
if(LINK_STATIC_LLVM)
    link_directories(${LLVM_LIBRARY_DIRS})
    llvm_map_components_to_libnames(llvm_libs support core irreader)
else()
    set(LLVM_LINK_LLVM_DYLIB ON)
    set(llvm_libs LLVM)
endif()

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

###############################################
find_package(unordered_dense CONFIG REQUIRED)


################################################
#FOPTIM lib

option(generation_check "check generations in references" ON)
option(tracy_perf "tracy" OFF)
option(optim_stats "optimization stats" OFF)
option(assert_check "Assertion Checks" ON)
option(fail_on_impl "Abort when impl gets hit" ON)
if(fail_on_impl)
  add_definitions(-DIMPL_ABORT)
else()
  remove_definitions(-DIMPL_ABORT)
endif()
if(generation_check)
  add_definitions(-DSLOT_CHECK_GENERATION)
else()
  remove_definitions(-DSLOT_CHECK_GENERATION)
endif()
if(optim_stats)
  add_definitions(-DOPTIM_STATS)
else()
  remove_definitions(-DOPTIM_STATS)
endif()
if(tracy_perf)
  link_libraries(TracyClient)
  add_definitions(-DTRACY_ENABLE)
else()
  remove_definitions(-DTRACY_ENABLE)
endif()
if(assert_check)
  add_definitions(-DASSERT_ENABLED)
else()
  remove_definitions(-DASSERT_ENABLED)
endif()


link_libraries(fmt)
# add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
# add_link_options(-fsanitize=thread -fPIE -pie)

add_library (foptim OBJECT
                #loading
                ./src/llvm/llir_loader.cpp

                #ir
                ./src/ir/context.cpp
                ./src/ir/function.cpp
                ./src/ir/basic_block.cpp
                ./src/ir/basic_block_ref.cpp
                ./src/ir/basic_block_arg.cpp
                ./src/ir/instruction_data.cpp
                ./src/ir/instruction.cpp
                ./src/ir/global.cpp
                ./src/ir/types.cpp
                ./src/ir/value.cpp
                ./src/ir/use.cpp
                ./src/ir/attributable.cpp
                ./src/ir/constant_value.cpp
                ./src/ir/helpers.cpp
                ./src/ir/builder.cpp
                ./src/ir/interpreter/interpreter.cpp

                #optim
                ./src/optim/analysis/live_variables.cpp
                ./src/optim/analysis/loop_analysis.cpp
                ./src/optim/analysis/cfg.cpp
                ./src/optim/analysis/basic_alias_test.cpp
                ./src/optim/func_passes/mem2reg.cpp
                ./src/optim/func_passes/loop_unroll.cpp
                ./src/optim/func_passes/loop_unswitch.cpp
                ./src/optim/func_passes/loop_rotate.cpp
                ./src/optim/func_passes/slp_vectorizer.cpp
                ./src/optim/func_passes/simplify_cfg.cpp
                ./src/optim/func_passes/double_load.cpp
                ./src/optim/func_passes/intrin_simplify.cpp
                ./src/optim/func_passes/inst_simplify.cpp
                ./src/optim/func_passes/lvn.cpp
                ./src/optim/func_passes/sccp.cpp
                ./src/optim/func_passes/stack_known_bits.cpp
                ./src/optim/func_passes/simple_vectorize.cpp
                ./src/optim/func_passes/inst_simplify/simplify_binary.cpp
                ./src/optim/module_passes/function_dedup.cpp
                ./src/optim/module_passes/arg_promotion.cpp
                ./src/optim/module_passes/IPCP.cpp
                ./src/optim/helper/inline.cpp
                ./src/optim/helper/WFVector.cpp
                ./src/optim/helper/helper.cpp

                #mir
                ./src/mir/instr.cpp
                ./src/mir/reg_alloc.cpp
                ./src/mir/matcher.cpp
                ./src/mir/matcher_patterns.cpp
                ./src/mir/matcher_helpers.cpp
                ./src/mir/analysis/live_variables.cpp
                ./src/mir/optim/reg_alloc.cpp
                ./src/mir/optim/reg_alloc_wp.cpp
                ./src/mir/optim/lvn.cpp
                ./src/mir/optim/dce.cpp
                ./src/mir/optim/calling_conv.cpp
                ./src/mir/optim/bb_reordering.cpp
                ./src/mir/optim/inst_simplify.cpp
                ./src/mir/optim/legalization.cpp
                ./src/mir/optim/register_joining.cpp

                #codegen
                ./src/x86_codegen/backend3.cpp
                ./src/x86_codegen/backend3_print.cpp
                ./src/third_party/Zydis.c

                #utils
                ./src/utils/parameters.cpp
                ./src/utils/job_system.cpp
                ./src/utils/arena.cpp

               )

target_compile_options(foptim PRIVATE -fno-exceptions -Wall -Wextra -Werror -ggdb -Wno-unused-const-variable -Wno-unused-function -Wno-unused-private-field -Wno-implicit-fallthrough)
target_include_directories(foptim PUBLIC ./src/)
target_include_directories(foptim SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_libraries(foptim PUBLIC ${llvm_libs} Threads::Threads unordered_dense::unordered_dense)
set_target_properties(foptim PROPERTIES UNITY_BUILD ON UNITY_BUILD_MODE BATCH UNITY_BUILD_BATCH_SIZE 4)


###############################################
#foptim unit test
add_executable (foptim_unit_test
                ./src/test/test_main.cpp
               )
target_compile_options(foptim_unit_test PRIVATE -fvisibility=hidden -Wall -Wextra -Werror -ggdb -Wno-unused-private-field -Wno-implicit-fallthrough)
target_link_libraries(foptim_unit_test PRIVATE foptim gtest)
target_include_directories(foptim_unit_test PRIVATE ./src/)


################################################
#foptim main
add_executable (foptim_main
                ./src/main.cpp

                #arg parsing
                ./src/arg_parsing/parser.cpp
               )

target_compile_options(foptim_main PRIVATE -fvisibility=hidden -Wall -Wextra -Werror -ggdb -Wno-unused-private-field -Wno-implicit-fallthrough)
target_link_libraries(foptim_main PRIVATE foptim)
target_include_directories(foptim_main PRIVATE ./src/)

# add_subdirectory(test)

