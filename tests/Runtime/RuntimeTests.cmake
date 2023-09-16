#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

# Test definitions
set(SRC ${CMAKE_SOURCE_DIR}/tests/Runtime)

set(TEST_CFLAGS "-std=c99 -static -fno-pic -fno-pie -g")
set(TESTS "calc" "function_call" "floating_point" "syscall" "global")

## calc
set(TEST_SOURCES_calc "${SRC}/calc.c")

set(TEST_RUNS_calc "literal" "sum" "multiplication")
set(TEST_ARGS_calc_literal "12")
set(TEST_ARGS_calc_sum "'(+ 4 5)'")
set(TEST_ARGS_calc_multiplication "'(* 5 6)'")

## function_call
set(TEST_SOURCES_function_call "${SRC}/function-call.c")

set(TEST_RUNS_function_call "default")
set(TEST_ARGS_function_call_default "nope")

## floating_point
set(TEST_SOURCES_floating_point "${SRC}/floating-point.c")

set(TEST_RUNS_floating_point "default")
set(TEST_ARGS_floating_point_default "nope")

## syscall
set(TEST_SOURCES_syscall "${SRC}/syscall.c")

set(TEST_RUNS_syscall "default")
set(TEST_ARGS_syscall_default "nope")

## global
set(TEST_SOURCES_global "${SRC}/global.c")

set(TEST_RUNS_global "default")
set(TEST_ARGS_global_default "nope")

# Create native executable and tests
foreach(TEST_NAME ${TESTS})
  add_executable(test-native-${TEST_NAME} ${TEST_SOURCES_${TEST_NAME}})
  set_target_properties(test-native-${TEST_NAME} PROPERTIES COMPILE_FLAGS "${TEST_CFLAGS}")
  set_target_properties(test-native-${TEST_NAME} PROPERTIES LINK_FLAGS "${TEST_CFLAGS}")

  foreach(RUN_NAME ${TEST_RUNS_${TEST_NAME}})
    add_test(NAME run-test-native-${TEST_NAME}-${RUN_NAME}
      COMMAND sh -c "$<TARGET_FILE:test-native-${TEST_NAME}> ${TEST_ARGS_${TEST_NAME}_${RUN_NAME}} > ${CMAKE_CURRENT_BINARY_DIR}/tests/run-test-native-${TEST_NAME}-${RUN_NAME}.log")
    set_tests_properties(run-test-native-${TEST_NAME}-${RUN_NAME}
        PROPERTIES LABELS "runtime;run-test-native;${TEST_NAME};${RUN_NAME}")
  endforeach()
endforeach()

# Helper macro to "evaluate" CMake variables such as CMAKE_C_LINK_EXECUTABLE,
# which looks like this:
# <CMAKE_C_COMPILER> <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS>
#                    -o <TARGET> <LINK_LIBRARIES>
macro(parse STRING OUTPUT)
  # set(COMMAND ${CMAKE_C_LINK_EXECUTABLE})
  set(COMMAND "${STRING}")
  string(REGEX MATCH "<([^>]*)>" VARNAME "${COMMAND}")
  while(VARNAME)
    string(REPLACE "<" "" VARNAME "${VARNAME}")
    string(REPLACE ">" "" VARNAME "${VARNAME}")
    string(REPLACE "<${VARNAME}>" "${${VARNAME}}" COMMAND "${COMMAND}")
    string(REGEX MATCH "<([^>]*)>" VARNAME "${COMMAND}")
  endwhile()
  set(${OUTPUT} ${COMMAND} PARENT_SCOPE)
endmacro()

# Wrapper function for parse so we don't mess around with scopes
function(compile_executable OBJECTS TARGET OUTPUT)
  parse("${CMAKE_C_LINK_EXECUTABLE}" "${OUTPUT}")
endfunction()

foreach(ARCH ${SUPPORTED_ARCHITECTURES})
  foreach(TEST_NAME ${TESTS})
    # Register the programs for compilation
    register_for_compilation("${ARCH}" "${TEST_NAME}" "${TEST_SOURCES_${TEST_NAME}}" "" BINARY)

    # Translate the compiled binary
    add_test(NAME translate-${TEST_NAME}-${ARCH}
      COMMAND sh -c "$<TARGET_FILE:revamb> --functions-boundaries --use-sections -g ll ${BINARY} ${BINARY}.ll")
    set_tests_properties(translate-${TEST_NAME}-${ARCH}
      PROPERTIES LABELS "runtime;translate;${TEST_NAME};${ARCH}")

    # Compose the command line to link support.c and the translated binaries
    string(REPLACE "-" "_" NORMALIZED_ARCH "${ARCH}")
    compile_executable("$(${CMAKE_BINARY_DIR}/li-csv-to-ld-options ${BINARY}.ll.li.csv) ${BINARY}${CMAKE_C_OUTPUT_EXTENSION} ${CMAKE_BINARY_DIR}/support.c -DTARGET_${NORMALIZED_ARCH} -lz -lm -lrt -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -g -fno-pie"
      "${BINARY}.translated"
      COMPILE_TRANSLATED)

    # Compile the translated LLVM IR with llc and link using the previously composed command line
    add_test(NAME compile-translated-${TEST_NAME}-${ARCH}
      COMMAND sh -c "${LLC} -O0 -filetype=obj ${BINARY}.ll -o ${BINARY}${CMAKE_C_OUTPUT_EXTENSION} && ${COMPILE_TRANSLATED}")
    set_tests_properties(compile-translated-${TEST_NAME}-${ARCH}
      PROPERTIES DEPENDS translate-${TEST_NAME}-${ARCH}
                 LABELS "runtime;compile-translated;${TEST_NAME};${ARCH}")

    # For each set of arguments
    foreach(RUN_NAME ${TEST_RUNS_${TEST_NAME}})
      # Test to run the translated program
      add_test(NAME run-translated-test-${TEST_NAME}-${RUN_NAME}-${ARCH}
        COMMAND sh -c "${BINARY}.translated ${TEST_ARGS_${TEST_NAME}_${RUN_NAME}} > ${BINARY}-run-translated-test-${RUN_NAME}-${ARCH}.log")
      set_tests_properties(run-translated-test-${TEST_NAME}-${RUN_NAME}-${ARCH}
        PROPERTIES DEPENDS compile-translated-${TEST_NAME}-${ARCH}
                   LABELS "runtime;run-translated-test;${TEST_NAME};${RUN_NAME};${ARCH}")

      # Check the output of the translated binary corresponds to the native's one
      add_test(NAME check-with-native-${TEST_NAME}-${RUN_NAME}-${ARCH}
        COMMAND "${DIFF}" "${BINARY}-run-translated-test-${RUN_NAME}-${ARCH}.log" "${CMAKE_CURRENT_BINARY_DIR}/tests/run-test-native-${TEST_NAME}-${RUN_NAME}.log")
      set(DEPS "")
      list(APPEND DEPS "run-translated-test-${TEST_NAME}-${RUN_NAME}-${ARCH}")
      list(APPEND DEPS "run-test-native-${TEST_NAME}-${RUN_NAME}")
      set_tests_properties(check-with-native-${TEST_NAME}-${RUN_NAME}-${ARCH}
        PROPERTIES DEPENDS "${DEPS}"
                   LABELS "runtime;check-with-native;${TEST_NAME};${RUN_NAME};${ARCH}")

      # Test to run the compiled program under qemu-user
      add_test(NAME run-qemu-test-${TEST_NAME}-${RUN_NAME}-${ARCH}
        COMMAND sh -c "${QEMU_${ARCH}} ${BINARY} ${TEST_ARGS_${TEST_NAME}_${RUN_NAME}} > ${BINARY}-run-qemu-test-${RUN_NAME}.log")
      set_tests_properties(run-qemu-test-${TEST_NAME}-${RUN_NAME}-${ARCH}
        PROPERTIES LABELS "runtime;run-qemu-test;${TEST_NAME};${RUN_NAME};${ARCH}")

      # Check the output of the translated binary corresponds to the qemu-user's
      # one
      add_test(NAME check-with-qemu-${TEST_NAME}-${RUN_NAME}-${ARCH}
        COMMAND "${DIFF}" "${BINARY}-run-translated-test-${RUN_NAME}-${ARCH}.log" "${BINARY}-run-qemu-test-${RUN_NAME}.log")
      set(DEPS "")
      list(APPEND DEPS "run-translated-test-${TEST_NAME}-${RUN_NAME}-${ARCH}")
      list(APPEND DEPS "run-qemu-test-${TEST_NAME}-${RUN_NAME}-${ARCH}")
      set_tests_properties(check-with-qemu-${TEST_NAME}-${RUN_NAME}-${ARCH}
        PROPERTIES DEPENDS "${DEPS}"
                   LABELS "runtime;check-with-qemu;${TEST_NAME};${RUN_NAME};${ARCH}")
    endforeach()
  endforeach()

endforeach()
