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

bison_target(Parser Src/gram.y ${BISON_GEN_SOURCE} DEFINES_FILE ${BISON_GEN_HEADER})
flex_target(Scanner Src/lexer.l ${FLEX_GEN_SOURCE})
add_flex_bison_dependency(Scanner Parser)

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
