add_executable(server_logging_probe "${CPPCHAT_SOURCE_DIR}/tests/logging/logging_probe.cpp")
target_include_directories(server_logging_probe PRIVATE "${CPPCHAT_SOURCE_DIR}/ChatServerShared")
target_link_libraries(server_logging_probe PRIVATE cppchat_server_logging)
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(server_logging_probe PRIVATE -municode)
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME server.logging COMMAND "${Python3_EXECUTABLE}" -B
  "${CPPCHAT_SOURCE_DIR}/tests/logging/verify_server_logging.py"
  --probe "$<TARGET_FILE:server_logging_probe>" --artifacts "${CMAKE_CURRENT_BINARY_DIR}/artifacts")
set_tests_properties(server.logging PROPERTIES LABELS "unit;smoke" TIMEOUT 60)
if(WIN32)
  add_test(NAME server.log_viewer COMMAND powershell -NoProfile -ExecutionPolicy Bypass
    -File "${CPPCHAT_SOURCE_DIR}/tests/logging/test_log_viewer.ps1"
    -Artifacts "${CMAKE_CURRENT_BINARY_DIR}/viewer-artifacts")
  set_tests_properties(server.log_viewer PROPERTIES LABELS "unit;smoke" TIMEOUT 30)
endif()
