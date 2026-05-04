cmake_minimum_required(VERSION 3.10)
project(Leda C)

# Original configuration used targeted build modes: opt (optimized) and debug
# Set NDEBUG for release builds (like opt) and DEBUG for debug builds.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG)
else()
    add_compile_definitions(NDEBUG)
endif()

# We need Flex and Bison for lexer.l and gram.y
find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

# We put the generated parser files in a specific directory to match the #include "gram.y.h" from lexer.l
set(BISON_GEN_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/Src/gram.y.c")
set(BISON_GEN_HEADER "${CMAKE_CURRENT_BINARY_DIR}/Src/gram.y.h")
set(FLEX_GEN_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/Src/lexer.l.c")

set(BYTECODE_BISON_GEN_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/src_bytecode/gram.y.c")
set(BYTECODE_BISON_GEN_HEADER "${CMAKE_CURRENT_BINARY_DIR}/src_bytecode/gram.y.h")
set(BYTECODE_FLEX_GEN_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/src_bytecode/lexer.l.c")

bison_target(Parser Src/gram.y ${BISON_GEN_SOURCE} DEFINES_FILE ${BISON_GEN_HEADER})
flex_target(Scanner Src/lexer.l ${FLEX_GEN_SOURCE})
add_flex_bison_dependency(Scanner Parser)

bison_target(BytecodeParser src_bytecode/gram.y ${BYTECODE_BISON_GEN_SOURCE} DEFINES_FILE ${BYTECODE_BISON_GEN_HEADER})
flex_target(BytecodeScanner src_bytecode/lexer.l ${BYTECODE_FLEX_GEN_SOURCE})
add_flex_bison_dependency(BytecodeScanner BytecodeParser)

# Add the main compiler executable `lc`
add_executable(lc
    Src/lc.c
    Src/gen.c
    Src/interp.c
    Src/types.c
    Src/memory.c
    ${BISON_Parser_OUTPUTS}
    ${FLEX_Scanner_OUTPUTS}
)

# Ensure the executable can find headers in Src and the generated header in the build dir
target_include_directories(lc PRIVATE
    Src
    ${CMAKE_CURRENT_BINARY_DIR}/Src
)

add_library(leda_bytecode_core
    src_bytecode/bytecode.c
    src_bytecode/bc_emit.c
    src_bytecode/vm.c
)

add_library(leda_bytecode_frontend
    src_bytecode/lc_frontend.c
    src_bytecode/types_frontend.c
    src_bytecode/gen_frontend.c
    src_bytecode/frontend_runtime_support.c
    ${BISON_BytecodeParser_OUTPUTS}
    ${FLEX_BytecodeScanner_OUTPUTS}
)

target_include_directories(leda_bytecode_core PRIVATE
    src_bytecode
)

target_include_directories(leda_bytecode_frontend PRIVATE
    src_bytecode
    ${CMAKE_CURRENT_BINARY_DIR}/src_bytecode
)

add_executable(ledac
    src_bytecode/ledac.c
)

target_link_libraries(ledac PRIVATE leda_bytecode_core leda_bytecode_frontend)
target_include_directories(ledac PRIVATE src_bytecode)

add_executable(ledavm
    src_bytecode/ledavm.c
)

target_link_libraries(ledavm PRIVATE leda_bytecode_core)
target_include_directories(ledavm PRIVATE src_bytecode)

add_executable(lbcdump
    src_bytecode/lbcdump.c
)

target_link_libraries(lbcdump PRIVATE leda_bytecode_core)
target_include_directories(lbcdump PRIVATE src_bytecode)

# Documentation target
find_program(EMACS_EXECUTABLE emacs)
if(EMACS_EXECUTABLE)
    add_custom_target(doc
        COMMAND sed 's%file:%http://henry.github.com/Leda/%' ${CMAKE_SOURCE_DIR}/index.org > ${CMAKE_BINARY_DIR}/README.org
        # The remaining doc/emacs tasks can go here
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Building documentation"
    )
endif()

# Testing
enable_testing()
add_custom_target(regression
    COMMAND bash ${CMAKE_SOURCE_DIR}/Test/regression.sh $<TARGET_FILE:lc>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/Test
    DEPENDS lc
)
add_test(NAME regression_test COMMAND ${CMAKE_COMMAND} --build . --target regression)

set(LEGACY_REGRESSION_CASES
    chap2a chap2b chap2c chap2d chap2e chap2f chap3 chap4a chap4b chap4c
    chap5 chap6a chap6b chap6d chap6e chap6f chap7a chap7b chap7c chap7d
    chap8a chap8c chap9 chap11 chap14 chap15a chap15b chap15c chap15d chap16
    chap17 chap19a chap19b chap19c chap20a chap20b chap20c chap20d chap21)
set(LEGACY_REGRESSION_STARTS
    1 4 8 12 16 20 25 127 132 136
    162 195 208 212 254 278 291 300 309 318
    327 334 344 348 430 450 454 460 464 470
    500 509 523 557 571 589 598 602 623)
set(LEGACY_REGRESSION_ENDS
    3 7 11 15 19 24 126 131 135 161
    194 207 211 253 277 290 299 308 317 326
    333 343 347 429 449 453 459 463 469 499
    508 522 556 570 588 597 601 622 631)

list(LENGTH LEGACY_REGRESSION_CASES LEGACY_REGRESSION_COUNT)
math(EXPR LEGACY_REGRESSION_LAST "${LEGACY_REGRESSION_COUNT} - 1")

foreach(LEGACY_REGRESSION_INDEX RANGE ${LEGACY_REGRESSION_LAST})
    list(GET LEGACY_REGRESSION_CASES ${LEGACY_REGRESSION_INDEX} LEGACY_REGRESSION_CASE)
    list(GET LEGACY_REGRESSION_STARTS ${LEGACY_REGRESSION_INDEX} LEGACY_REGRESSION_START)
    list(GET LEGACY_REGRESSION_ENDS ${LEGACY_REGRESSION_INDEX} LEGACY_REGRESSION_END)

    set(LEGACY_REGRESSION_INPUT_FILE "")
    set(LEGACY_REGRESSION_MEMORY_LIMIT "")
    if(LEGACY_REGRESSION_CASE STREQUAL "chap8c")
        set(LEGACY_REGRESSION_INPUT_FILE "${CMAKE_SOURCE_DIR}/Test/concordanceInput")
    elseif(LEGACY_REGRESSION_CASE STREQUAL "chap17")
        set(LEGACY_REGRESSION_INPUT_FILE "${CMAKE_SOURCE_DIR}/Test/chap17input")
    elseif(LEGACY_REGRESSION_CASE STREQUAL "chap20d")
        set(LEGACY_REGRESSION_MEMORY_LIMIT "500000")
    endif()

    add_test(NAME legacy_${LEGACY_REGRESSION_CASE}_test
        COMMAND ${CMAKE_COMMAND}
            -DTEST_NAME=legacy_${LEGACY_REGRESSION_CASE}_test
            -DTEST_DIR=${CMAKE_SOURCE_DIR}/Test
            -DLC_PATH=$<TARGET_FILE:lc>
            -DCASE_NAME=${LEGACY_REGRESSION_CASE}
            -DREF_PATH=${CMAKE_SOURCE_DIR}/Test/ref
            -DREF_START=${LEGACY_REGRESSION_START}
            -DREF_END=${LEGACY_REGRESSION_END}
            -DINPUT_FILE=${LEGACY_REGRESSION_INPUT_FILE}
            -DMEMORY_LIMIT=${LEGACY_REGRESSION_MEMORY_LIMIT}
            -P ${CMAKE_SOURCE_DIR}/Test/run_lc_regression_case.cmake)
    set_tests_properties(legacy_${LEGACY_REGRESSION_CASE}_test PROPERTIES LABELS "legacy-regression")
endforeach()

add_test(NAME lbcdump_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=lbcdump_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/lbcdump_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/lbcdump_test.golden.txt
        -DRUN_VM=OFF
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME direct_call_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=direct_call_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/direct_call_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/direct_call_test.golden.txt
        -DRUN_VM=OFF
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME constant_alias_call_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=constant_alias_call_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/constant_alias_call_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/constant_alias_call_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME local_alias_call_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=local_alias_call_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/local_alias_call_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/local_alias_call_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME comma_byref_temp_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=comma_byref_temp_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/comma_byref_temp_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/comma_byref_temp_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME eval_reference_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=eval_reference_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/eval_reference_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/eval_reference_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME build_instance_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=build_instance_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/build_instance_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/build_instance_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME object_slot_access_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=object_slot_access_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/object_slot_access_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/object_slot_access_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME method_context_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=method_context_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/method_context_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/method_context_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME nested_direct_call_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=nested_direct_call_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/nested_direct_call_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/nested_direct_call_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME tier1_primitives_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=tier1_primitives_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/tier1_primitives_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/tier1_primitives_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME real_primitives_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=real_primitives_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/real_primitives_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/real_primitives_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME pattern_match_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=pattern_match_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/pattern_match_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/pattern_match_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME object_array_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=object_array_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/object_array_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/object_array_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
add_test(NAME indirect_call_test
    COMMAND ${CMAKE_COMMAND}
        -DTEST_NAME=indirect_call_test
        -DBUILD_DIR=${CMAKE_BINARY_DIR}
        -DLEDAC_PATH=$<TARGET_FILE:ledac>
        -DLBCDUMP_PATH=$<TARGET_FILE:lbcdump>
        -DLEDAVM_PATH=$<TARGET_FILE:ledavm>
        -DFIXTURE_PATH=${CMAKE_SOURCE_DIR}/Test/indirect_call_fixture.led
        -DEXPECTED_PATH=${CMAKE_SOURCE_DIR}/Test/indirect_call_test.golden.txt
        -DRUN_VM=ON
        -P ${CMAKE_SOURCE_DIR}/Test/run_bytecode_test.cmake)
