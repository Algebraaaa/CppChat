include_guard(GLOBAL)
find_package(Threads REQUIRED)
add_library(cppchat_server_logging STATIC
  "${CPPCHAT_SOURCE_DIR}/shared/logging/ServerLogger.cpp"
  "${CPPCHAT_SOURCE_DIR}/shared/logging/InstanceLease.cpp")
target_include_directories(cppchat_server_logging PUBLIC "${CPPCHAT_SOURCE_DIR}/shared"
  PRIVATE "${CPPCHAT_SOURCE_DIR}/CppChat/third_party/spdlog/include")
target_compile_features(cppchat_server_logging PUBLIC cxx_std_17)
target_link_libraries(cppchat_server_logging PRIVATE Threads::Threads)
if(MSVC)
  target_compile_options(cppchat_server_logging PRIVATE /utf-8)
endif()
set_target_properties(cppchat_server_logging PROPERTIES FOLDER "服务端/共享")
