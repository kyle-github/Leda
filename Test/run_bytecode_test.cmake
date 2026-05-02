if(NOT DEFINED TEST_NAME)
    message(FATAL_ERROR "TEST_NAME is required")
endif()

if(NOT DEFINED BUILD_DIR)
    message(FATAL_ERROR "BUILD_DIR is required")
endif()

if(NOT DEFINED LEDAC_PATH)
    message(FATAL_ERROR "LEDAC_PATH is required")
endif()

if(NOT DEFINED LBCDUMP_PATH)
    message(FATAL_ERROR "LBCDUMP_PATH is required")
endif()

if(NOT DEFINED FIXTURE_PATH)
    message(FATAL_ERROR "FIXTURE_PATH is required")
endif()

if(NOT DEFINED EXPECTED_PATH)
    message(FATAL_ERROR "EXPECTED_PATH is required")
endif()

if(NOT DEFINED RUN_VM)
    set(RUN_VM OFF)
endif()

if(RUN_VM AND NOT DEFINED LEDAVM_PATH)
    message(FATAL_ERROR "LEDAVM_PATH is required when RUN_VM is ON")
endif()

set(test_dir "${BUILD_DIR}/Testing/${TEST_NAME}")
set(lbc_path "${test_dir}/${TEST_NAME}.lbc")
set(dump_path "${test_dir}/${TEST_NAME}.dump")

file(REMOVE_RECURSE "${test_dir}")
file(MAKE_DIRECTORY "${test_dir}")

execute_process(
    COMMAND "${LEDAC_PATH}" "${FIXTURE_PATH}" -o "${lbc_path}"
    RESULT_VARIABLE ledac_result
    OUTPUT_VARIABLE ledac_stdout
    ERROR_VARIABLE ledac_stderr
)

if(NOT ledac_result EQUAL 0)
    message(FATAL_ERROR "ledac failed for ${TEST_NAME}\n${ledac_stdout}${ledac_stderr}")
endif()

execute_process(
    COMMAND "${LBCDUMP_PATH}" "${lbc_path}"
    RESULT_VARIABLE lbcdump_result
    OUTPUT_FILE "${dump_path}"
    ERROR_VARIABLE lbcdump_stderr
)

if(NOT lbcdump_result EQUAL 0)
    message(FATAL_ERROR "lbcdump failed for ${TEST_NAME}\n${lbcdump_stderr}")
endif()

if(RUN_VM)
    execute_process(
        COMMAND "${LEDAVM_PATH}" "${lbc_path}"
        RESULT_VARIABLE ledavm_result
        OUTPUT_VARIABLE ledavm_stdout
        ERROR_VARIABLE ledavm_stderr
    )

    if(NOT ledavm_result EQUAL 0)
        message(FATAL_ERROR "ledavm failed for ${TEST_NAME}\n${ledavm_stdout}${ledavm_stderr}")
    endif()
endif()

file(READ "${dump_path}" dump_contents)
file(STRINGS "${EXPECTED_PATH}" expected_lines)

foreach(expected_line IN LISTS expected_lines)
    if(expected_line STREQUAL "")
        continue()
    endif()

    string(FIND "${dump_contents}" "${expected_line}" expected_pos)
    if(expected_pos EQUAL -1)
        message(FATAL_ERROR "missing expected text in ${TEST_NAME}: ${expected_line}\n\nDump contents:\n${dump_contents}")
    endif()
endforeach()