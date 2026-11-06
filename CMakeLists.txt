cmake_minimum_required(VERSION 3.16)
project(z80emu VERSION 0.8.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_library(z80core src/cpu.cpp src/disasm.cpp src/loader.cpp src/serial.cpp)
target_include_directories(z80core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(z80core PRIVATE -Wall -Wextra -Wpedantic)

# The same tested z80core is linked by the native debugger and by the
# Emscripten browser module. The WASM build never compiles POSIX terminal code.
if(EMSCRIPTEN)
  target_compile_options(z80core PRIVATE -fexceptions)
  add_executable(z80wasm src/web_api.cpp)
  target_link_libraries(z80wasm PRIVATE z80core)
  target_compile_options(z80wasm PRIVATE -fexceptions)
  target_link_options(z80wasm PRIVATE
    -O2 -fexceptions
    "--no-entry"
    "-sINITIAL_MEMORY=33554432"
    "-sMAXIMUM_MEMORY=134217728"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sENVIRONMENT=worker"
    "-sMIN_SAFARI_VERSION=150000"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORTED_FUNCTIONS=['_z80_init','_z80_load','_z80_reset','_z80_step','_z80_run_slice','_z80_set_breakpoint','_z80_set_pc','_z80_clear_breakpoints','_z80_queue_input','_z80_read_byte','_z80_write_byte','_z80_memory_ptr','_z80_snapshot_size','_z80_snapshot_save','_z80_snapshot_restore','_z80_state_json','_z80_disassembly','_z80_disassembly_length','_z80_output_ptr','_z80_output_length','_z80_last_error','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','HEAPU8']"
  )
  # A single, deployable static-site directory; Python's http.server can
  # serve it locally. No backend or host machine access is needed.
  set_target_properties(z80wasm PROPERTIES
    SUFFIX ".mjs" OUTPUT_NAME "z80wasm"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/site")
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/web/index.html"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/app.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/worker.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/style.css"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/assembler-source.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/assembler-core.mjs"
            "${CMAKE_CURRENT_SOURCE_DIR}/web/session-format.mjs"
            "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_LICENSES.txt"
       DESTINATION "${CMAKE_BINARY_DIR}/site")
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/site/examples")
  file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.hex"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/monitor.hex"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/console_hello.hex"
            "${CMAKE_CURRENT_SOURCE_DIR}/examples/console_echo.hex"
       DESTINATION "${CMAKE_BINARY_DIR}/site/examples")
else()
add_executable(z80emu src/main.cpp src/clock.cpp)
target_link_libraries(z80emu PRIVATE z80core)
target_compile_options(z80emu PRIVATE -Wall -Wextra -Wpedantic)
include(CTest)
if(BUILD_TESTING)
 add_executable(z80tests tests/tests.cpp)
 target_link_libraries(z80tests PRIVATE z80core)
 target_compile_definitions(z80tests PRIVATE EXAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
 add_test(NAME core_and_loader COMMAND z80tests)
endif()
install(TARGETS z80emu RUNTIME DESTINATION bin)

# Optional external conformance runner. CP/M tests are fetched by the user,
# not bundled, keeping the distribution separate from GPL-licensed exercisers.
add_executable(z80zex tests/zex_runner.cpp)
target_link_libraries(z80zex PRIVATE z80core)
target_compile_options(z80zex PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
 add_executable(z80alu_exhaustive tests/alu_exhaustive.cpp)
 target_link_libraries(z80alu_exhaustive PRIVATE z80core)
 target_compile_options(z80alu_exhaustive PRIVATE -Wall -Wextra -Wpedantic)
 add_test(NAME exhaustive_8bit_alu COMMAND z80alu_exhaustive)
endif()
if(BUILD_TESTING)
 add_test(NAME zex_rejects_incomplete_output COMMAND z80zex ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/exit.com)
 set_tests_properties(zex_rejects_incomplete_output PROPERTIES WILL_FAIL TRUE)
endif()

if(BUILD_TESTING)
 add_executable(z80flags_exhaustive tests/flag_conformance.cpp)
 target_link_libraries(z80flags_exhaustive PRIVATE z80core)
 target_compile_options(z80flags_exhaustive PRIVATE -Wall -Wextra -Wpedantic)
 add_test(NAME exhaustive_daa_and_bit_flags COMMAND z80flags_exhaustive)
endif()
if(BUILD_TESTING)
 add_test(NAME zex_counts_short_padded_names
          COMMAND z80zex ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/short_pad_67.com)
endif()

if(BUILD_TESTING)
 add_executable(z80clocktests tests/clock_tests.cpp src/clock.cpp)
 target_include_directories(z80clocktests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
 target_compile_options(z80clocktests PRIVATE -Wall -Wextra -Wpedantic)
 add_test(NAME clock_rate_and_parsing COMMAND z80clocktests)
 add_test(NAME cli_rejects_invalid_clock COMMAND z80emu ${CMAKE_CURRENT_SOURCE_DIR}/examples/hello.hex --steps 1 --clock 0)
 set_tests_properties(cli_rejects_invalid_clock PROPERTIES WILL_FAIL TRUE)
endif()

if(BUILD_TESTING)
 add_test(NAME clock_demo_unlimited COMMAND z80emu ${CMAKE_CURRENT_SOURCE_DIR}/examples/clock_demo.hex --steps 300000)
 set_tests_properties(clock_demo_unlimited PROPERTIES PASS_REGULAR_EXPRESSION "T-states=1703919")
 add_test(NAME clock_demo_4mhz COMMAND z80emu ${CMAKE_CURRENT_SOURCE_DIR}/examples/clock_demo.hex --steps 300000 --clock 4MHz)
 set_tests_properties(clock_demo_4mhz PROPERTIES PASS_REGULAR_EXPRESSION "T-states=1703919" TIMEOUT 10)
endif()

# Native ABI smoke test for the browser wrapper; runs without Emscripten.
add_library(z80webapi STATIC src/web_api.cpp)
target_link_libraries(z80webapi PUBLIC z80core)
if(BUILD_TESTING)
  add_executable(z80webapitests tests/web_api_tests.cpp)
  target_link_libraries(z80webapitests PRIVATE z80webapi)
  target_compile_definitions(z80webapitests PRIVATE EXAMPLE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")
  add_test(NAME browser_abi_and_loading COMMAND z80webapitests)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME browser_source_integrity
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_web.py)
  endif()
  # Optional Node.js behavioural smoke test of the actual browser worker.
  # The CPU mock tests scheduling and breakpoint continuation, not WASM fidelity.
  find_program(NODE_EXECUTABLE NAMES node)
  if(NODE_EXECUTABLE)
    add_test(NAME browser_assembler_hex_conversion COMMAND ${NODE_EXECUTABLE} --test ${CMAKE_CURRENT_SOURCE_DIR}/tests/assembler_core.test.mjs)
    add_test(NAME browser_session_file_format COMMAND ${NODE_EXECUTABLE} --test ${CMAKE_CURRENT_SOURCE_DIR}/tests/session_format.test.mjs)
    add_test(NAME browser_worker_breakpoint_continuation
      COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/worker_breakpoint_smoke.cjs)
  endif()
endif()
endif() # EMSCRIPTEN
