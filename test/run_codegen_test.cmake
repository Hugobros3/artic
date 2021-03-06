execute_process(COMMAND ${TEST_EXECUTABLE} ${TEST_ARGS} OUTPUT_FILE ${TEST_NAME}.out RESULT_VARIABLE status)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Error running \"${TEST_EXECUTABLE} ${TEST_ARGS}\": ${status}")
endif ()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files --ignore-eol ${TEST_NAME}.out ${TEST_REFERENCE} RESULT_VARIABLE status)
if (NOT status STREQUAL "0")
    message(FATAL_ERROR "Reference does not match test output")
endif ()
