cmake_minimum_required(VERSION 3.16.3)
project(foptim
        LANGUAGES CXX C)
set(CMAKE_EXPERIMENTAL_CXX_MODULE_DYNDEP 1)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_COMPILER "clang++")
set(CMAKE_C_COMPILER "clang")

if(CMAKE_EXPORT_COMPILE_COMMANDS)
  set(CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES 
      ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
endif()

add_compile_options(-fvisibility=hidden -ggdb
  -Wall -Wextra -Werror -Wno-c23-extensions -Wno-unused-const-variable
  -Wno-unused-function -Wno-unused-private-field -Wno-implicit-fallthrough)

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
                ./src/optim/fir_pipeline.cpp
                ./src/mir/func_order.cpp
                ./src/optim/analysis/live_variables.cpp
                ./src/optim/analysis/loop_analysis.cpp
                ./src/optim/analysis/cfg.cpp
                ./src/optim/analysis/analysis_manager.cpp
                ./src/optim/analysis/basic_alias_test.cpp
                ./src/optim/analysis/constraint_analysis.cpp
                ./src/optim/analysis/attributer/KnownBits.cpp
                ./src/optim/func_passes/mem2reg.cpp
                ./src/optim/func_passes/loop_unroll.cpp
                ./src/optim/func_passes/loop_unswitch.cpp
                ./src/optim/func_passes/loop_rotate.cpp
                ./src/optim/func_passes/slp_vectorizer.cpp
                ./src/optim/func_passes/simplify_cfg.cpp
                ./src/optim/func_passes/double_load.cpp
                ./src/optim/func_passes/intrin_simplify.cpp
                ./src/optim/func_passes/intrin_expand.cpp
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
                ./src/mir/optim/mir_pipeline.cpp
                ./src/mir/optim/reg_alloc.cpp
                ./src/mir/optim/lvn.cpp
                ./src/mir/optim/dce.cpp
                ./src/mir/optim/calling_conv.cpp
                ./src/mir/optim/bb_reordering.cpp
                ./src/mir/optim/inst_simplify.cpp
                ./src/mir/optim/legalization.cpp
                ./src/mir/optim/register_joining.cpp

                #codegen
                ./src/x86_codegen/backend3.cpp
                ./src/x86_codegen/backend3_instr_gen.cpp
                ./src/x86_codegen/backend3_print.cpp

                #utils
                ./src/utils/job_system.cpp
                ./src/utils/arena.cpp

               )

target_compile_options(foptim PRIVATE -fno-exceptions)
target_include_directories(foptim PUBLIC ./src/)
target_include_directories(foptim SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_libraries(foptim PUBLIC ${llvm_libs} Threads::Threads unordered_dense::unordered_dense)


###############################################
#zydis assembling libary
add_library(zydis STATIC ./src/third_party/Zydis.c)
target_include_directories(zydis PUBLIC ./src/)
target_compile_options(zydis PRIVATE -w)
target_link_libraries(foptim PUBLIC zydis)




set(FOPTIM_PCH
  <fmt/base.h> <fmt/core.h> <fmt/format.h> <fmt/color.h>
  <ankerl/unordered_dense.h>
  <algorithm> <vector> <string> <string_view> <optional> <span> <ranges>
  <memory> <functional> <unordered_map> <deque> <atomic> <mutex>
  <type_traits> <limits> <bit> <cstring> <cstdint>)
target_precompile_headers(foptim PRIVATE ${FOPTIM_PCH})

###############################################
#foptim unit test
add_executable (foptim_unit_test
                ./src/test/test_main.cpp
               )
target_compile_options(foptim_unit_test PRIVATE -fno-exceptions)
target_link_libraries(foptim_unit_test PRIVATE foptim gtest)
target_include_directories(foptim_unit_test PRIVATE ./src/)


################################################
#foptim main
add_executable (foptim_main
                ./src/main.cpp
                #arg parsing
                ./src/arg_parsing/parser.cpp
                #config parsing
                ./src/config/compiler_config.cpp
               )

target_link_libraries(foptim_main PRIVATE foptim)
target_include_directories(foptim_main PRIVATE ./src/)



target_precompile_headers(foptim_unit_test REUSE_FROM foptim)
target_precompile_headers(foptim_main PRIVATE ${FOPTIM_PCH})

###############################################
#foptim lit test
set_property(GLOBAL PROPERTY GLOBAL_LIT_TARGETS)
function(add_my_lit_suite target_name test_dir dependency_target)
    add_custom_target(${target_name}
        COMMAND lit "${test_dir}" -v
        DEPENDS ${dependency_target}
        COMMENT "Running lit test suite: ${target_name}"
        USES_TERMINAL
    )   
    set_property(GLOBAL APPEND PROPERTY GLOBAL_LIT_TARGETS ${target_name})
endfunction()
add_subdirectory(test)
get_property(ALL_LIT_TESTS GLOBAL PROPERTY GLOBAL_LIT_TARGETS)
add_custom_target(check-unit
    COMMAND foptim_unit_test
    DEPENDS foptim_unit_test
    COMMENT "Running unit tests"
    USES_TERMINAL
)
add_custom_target(check-all
    COMMENT "Running all registered test suites..."
)
add_dependencies(check-all check-unit ${ALL_LIT_TESTS})
