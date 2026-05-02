if(NOT DEFINED TEST_NAME)
    message(FATAL_ERROR "TEST_NAME is required")
endif()

if(NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "TEST_DIR is required")
endif()

if(NOT DEFINED LC_PATH)
    message(FATAL_ERROR "LC_PATH is required")
endif()

if(NOT DEFINED CASE_NAME)
    message(FATAL_ERROR "CASE_NAME is required")
endif()

if(NOT DEFINED REF_PATH)
    message(FATAL_ERROR "REF_PATH is required")
endif()

if(NOT DEFINED REF_START OR NOT DEFINED REF_END)
    message(FATAL_ERROR "REF_START and REF_END are required")
endif()

set(case_path "${TEST_DIR}/${CASE_NAME}.led")
set(command_args)
if(DEFINED MEMORY_LIMIT AND NOT MEMORY_LIMIT STREQUAL "")
    list(APPEND command_args -m ${MEMORY_LIMIT})
endif()
list(APPEND command_args "${case_path}")

set(input_args)
if(DEFINED INPUT_FILE AND NOT INPUT_FILE STREQUAL "")
    list(APPEND input_args INPUT_FILE "${INPUT_FILE}")
endif()

execute_process(
    COMMAND "${LC_PATH}" ${command_args}
    WORKING_DIRECTORY "${TEST_DIR}"
    ${input_args}
    RESULT_VARIABLE lc_result
    OUTPUT_VARIABLE lc_stdout
    ERROR_VARIABLE lc_stderr
)

if(NOT lc_result EQUAL 0)
    message(FATAL_ERROR "lc failed for ${CASE_NAME}\n${lc_stdout}${lc_stderr}")
endif()

file(READ "${REF_PATH}" ref_contents)
string(REPLACE "\r\n" "\n" ref_contents "${ref_contents}")
string(REPLACE "\r" "\n" ref_contents "${ref_contents}")
string(REPLACE "\n" ";" ref_lines "${ref_contents}")

math(EXPR ref_start_index "${REF_START} - 1")
math(EXPR ref_end_index "${REF_END} - 1")
set(expected_output "")
foreach(ref_index RANGE ${ref_start_index} ${ref_end_index})
    list(GET ref_lines ${ref_index} ref_line)
    string(APPEND expected_output "${ref_line}\n")
endforeach()

string(REPLACE "\r\n" "\n" lc_stdout "${lc_stdout}")
string(REPLACE "\r" "\n" lc_stdout "${lc_stdout}")

if(NOT lc_stdout STREQUAL expected_output)
    message(FATAL_ERROR
        "stdout mismatch for ${CASE_NAME}\n"
        "--- expected ---\n${expected_output}"
        "--- actual ---\n${lc_stdout}"
        "--- stderr ---\n${lc_stderr}")
endif()