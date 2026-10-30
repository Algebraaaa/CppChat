include(FetchContent)
# A release commit, not a moving branch. Downloaded only for test builds.
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
  GIT_SHALLOW FALSE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
# CMAKE_FOLDER is inherited by dependency subdirectories and scoped to tests/cpp.
set(CMAKE_FOLDER "测试/依赖")
FetchContent_MakeAvailable(googletest)
set(CMAKE_FOLDER "测试")
add_executable(core_tests core_tests.cpp "${CPPCHAT_SOURCE_DIR}/ChatServerShared/BusinessExecutor.cpp")
target_include_directories(core_tests PRIVATE "${CPPCHAT_SOURCE_DIR}/ChatServerShared"
  "${CPPCHAT_SOURCE_DIR}/ResourceServer")
target_link_libraries(core_tests PRIVATE GTest::gtest_main GTest::gmock JsonCpp::JsonCpp)
if(NOT WIN32)
  target_link_libraries(core_tests PRIVATE Threads::Threads)
endif()
add_test(NAME core.regression COMMAND core_tests
  "--gtest_output=xml:${CPPCHAT_BINARY_DIR}/core-junit.xml")
set_tests_properties(core.regression PROPERTIES LABELS "unit;smoke" TIMEOUT 20)
add_subdirectory("${CPPCHAT_SOURCE_DIR}/tests/logging/cases" logging)

# Preserve the existing real-connection recovery test and both DAO configurations.
foreach(variant shared gate)
  set(name mysql_pool_${variant})
  set(sources "${CPPCHAT_SOURCE_DIR}/tests/integration/mysql_pool_recovery.cpp"
    "${CPPCHAT_SOURCE_DIR}/ChatServerShared/MySqlPool.cpp")
  if(variant STREQUAL gate)
    list(APPEND sources "${CPPCHAT_SOURCE_DIR}/GateServer/MysqlDao.cpp"
      "${CPPCHAT_SOURCE_DIR}/GateServer/ConfigMgr.cpp"
      "${CPPCHAT_SOURCE_DIR}/GateServer/Logger.cpp")
  else()
    file(GLOB dao_sources CONFIGURE_DEPENDS "${CPPCHAT_SOURCE_DIR}/ChatServerShared/MysqlDao*.cpp")
    list(APPEND sources ${dao_sources} "${CPPCHAT_SOURCE_DIR}/ChatServerShared/ConfigMgr.cpp"
      "${CPPCHAT_SOURCE_DIR}/ChatServerShared/Logger.cpp")
  endif()
  add_executable(${name} ${sources})
  if(variant STREQUAL gate)
    target_include_directories(${name} PRIVATE "${CPPCHAT_SOURCE_DIR}/GateServer" "${CPPCHAT_SOURCE_DIR}/ChatServerShared")
  else()
    target_include_directories(${name} PRIVATE "${CPPCHAT_SOURCE_DIR}/ChatServerShared")
    target_compile_definitions(${name} PRIVATE TEST_CHAT_DAO)
  endif()
  if(MSVC)
    target_include_directories(${name} PRIVATE "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/jdbc")
  endif()
  target_link_libraries(${name} PRIVATE unofficial::mysql-connector-cpp::connector-jdbc
    OpenSSL::Crypto Boost::filesystem JsonCpp::JsonCpp cppchat_server_logging)
  if(variant STREQUAL gate)
    target_compile_definitions(${name} PRIVATE TEST_GATE_POOL)
  endif()
endforeach()
