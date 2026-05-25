if(NOT DEFINED TEST_NAME)
    message(FATAL_ERROR "TEST_NAME is required")
endif()

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED LEDAC_PATH)
    message(FATAL_ERROR "LEDAC_PATH is required")
endif()

if(NOT DEFINED LEDAVM_PATH)
    message(FATAL_ERROR "LEDAVM_PATH is required")
endif()

if(NOT DEFINED FIXTURE_PATH)
    message(FATAL_ERROR "FIXTURE_PATH is required")
endif()

if(NOT DEFINED EXPECTED_OUTPUT_PATH)
    message(FATAL_ERROR "EXPECTED_OUTPUT_PATH is required")
endif()

set(test_dir "${BUILD_DIR}/Testing/${TEST_NAME}")
set(lbc_path "${test_dir}/${TEST_NAME}.lbc")

file(REMOVE_RECURSE "${test_dir}")
file(MAKE_DIRECTORY "${test_dir}")

execute_process(
    COMMAND "${LEDAC_PATH}" "${FIXTURE_PATH}" -o "${lbc_path}"
    WORKING_DIRECTORY "${BUILD_DIR}"
    RESULT_VARIABLE ledac_result
    OUTPUT_VARIABLE ledac_stdout
    ERROR_VARIABLE ledac_stderr
)

if(NOT ledac_result EQUAL 0)
    message(FATAL_ERROR "ledac failed for ${TEST_NAME}\n${ledac_stdout}${ledac_stderr}")
endif()

set(vm_input_args)
if(DEFINED INPUT_FILE AND NOT INPUT_FILE STREQUAL "")
    list(APPEND vm_input_args INPUT_FILE "${INPUT_FILE}")
endif()

set(vm_timeout_args)
if(DEFINED TIMEOUT AND NOT TIMEOUT STREQUAL "")
    list(APPEND vm_timeout_args TIMEOUT ${TIMEOUT})
endif()

execute_process(
    COMMAND "${LEDAVM_PATH}" "${lbc_path}"
    WORKING_DIRECTORY "${BUILD_DIR}"
    ${vm_input_args}
    ${vm_timeout_args}
    RESULT_VARIABLE ledavm_result
    OUTPUT_VARIABLE ledavm_stdout
    ERROR_VARIABLE ledavm_stderr
)

if(NOT ledavm_result EQUAL 0)
    message(FATAL_ERROR "ledavm failed for ${TEST_NAME}\n${ledavm_stdout}${ledavm_stderr}")
endif()

file(READ "${EXPECTED_OUTPUT_PATH}" expected_output)

string(REPLACE "\r\n" "\n" ledavm_stdout "${ledavm_stdout}")
string(REPLACE "\r"   "\n" ledavm_stdout "${ledavm_stdout}")
string(REPLACE "\r\n" "\n" expected_output "${expected_output}")
string(REPLACE "\r"   "\n" expected_output "${expected_output}")

string(REGEX REPLACE "[ \t\n]+$" "" ledavm_stdout "${ledavm_stdout}")
string(REGEX REPLACE "[ \t\n]+$" "" expected_output "${expected_output}")

if(NOT ledavm_stdout STREQUAL expected_output)
    message(FATAL_ERROR
        "output mismatch for ${TEST_NAME}\n"
        "--- expected ---\n${expected_output}"
        "--- actual ---\n${ledavm_stdout}")
endif()
