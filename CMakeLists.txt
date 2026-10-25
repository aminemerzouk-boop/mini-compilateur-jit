cmake_minimum_required(VERSION 3.16)
project(mlang C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# ------------------------------------------------------------------
#  Raccourci : ajoute des cibles de compilation pour chaque fichier .mlang
#  Ex : mlang_run(tests/fib.mlang)
# ------------------------------------------------------------------
function(mlang_test name)
    add_test(NAME ${name}
             COMMAND ${CMAKE_CURRENT_BINARY_DIR}/mlang
                     ${CMAKE_CURRENT_SOURCE_DIR}/tests/${name}.mlang)
endfunction()

# ------------------------------------------------------------------
#  libgccjit
# ------------------------------------------------------------------
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=libgccjit.so
    OUTPUT_VARIABLE _gccjit_hint
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(_gccjit_hint MATCHES "libgccjit" AND EXISTS "${_gccjit_hint}")
    get_filename_component(_gccjit_dir "${_gccjit_hint}" DIRECTORY)
    find_library(GCCJIT_LIBRARY NAMES gccjit HINTS ${_gccjit_dir})
endif()
if(NOT GCCJIT_LIBRARY)
    find_library(GCCJIT_LIBRARY NAMES gccjit)
endif()
if(NOT GCCJIT_LIBRARY)
    message(FATAL_ERROR
        "\n*** libgccjit introuvable ***\n"
        "  Ubuntu/WSL : sudo apt install libgccjit-$(gcc -dumpversion | cut -d. -f1)-dev\n"
        "  MSYS2      : pacman -S mingw-w64-ucrt-x86_64-libgccjit\n")
endif()

execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
    OUTPUT_VARIABLE GCC_INTERNAL_INCLUDE
    OUTPUT_STRIP_TRAILING_WHITESPACE)

message(STATUS "libgccjit   : ${GCCJIT_LIBRARY}")
message(STATUS "include GCC : ${GCC_INTERNAL_INCLUDE}")

# ------------------------------------------------------------------
#  Le compilateur
# ------------------------------------------------------------------
add_executable(mlang
    src/main.c
    src/lexer.c
    src/parser.c
    src/bytecode.c
    src/compiler.c
    src/vm.c
    src/jit.c
)

target_include_directories(mlang PRIVATE ${GCC_INTERNAL_INCLUDE})
target_link_libraries(mlang PRIVATE ${GCCJIT_LIBRARY})

if(NOT WIN32)
    target_link_libraries(mlang PRIVATE m)
endif()

# utile pour d'éventuels symboles partagés avec le code JIT
if(UNIX AND NOT APPLE)
    target_link_options(mlang PRIVATE -Wl,--export-dynamic)
endif()

# ------------------------------------------------------------------
#  Tests (facultatif : ctest)
# ------------------------------------------------------------------
enable_testing()
mlang_test(arith)
mlang_test(boucle)
mlang_test(conditions)
mlang_test(fib)
mlang_test(variables)
