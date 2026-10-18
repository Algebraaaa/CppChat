file(GLOB resource_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")
cppchat_map_debug_sources(ResourceServer mapped_sources ${resource_sources}
  "${CPPCHAT_SOURCE_DIR}/ChatServerShared/BusinessExecutor.cpp")
add_executable(ResourceServer ${mapped_sources})
target_include_directories(ResourceServer PRIVATE "${CPPCHAT_BINARY_DIR}/sources/ResourceServer"
  "${CMAKE_CURRENT_SOURCE_DIR}" "${CPPCHAT_SOURCE_DIR}/ChatServerShared")
target_link_libraries(ResourceServer PRIVATE Boost::asio Boost::filesystem JsonCpp::JsonCpp
  hiredis::hiredis OpenSSL::SSL OpenSSL::Crypto gRPC::grpc++ protobuf::libprotobuf)
cppchat_add_protocol(ResourceServer ChatServerShared)
if(WIN32)
  target_link_libraries(ResourceServer PRIVATE ws2_32 crypt32)
endif()
cppchat_configure_server(ResourceServer)

if(NOT CPPCHAT_BUILD_TESTS AND EXISTS "${CPPCHAT_SOURCE_DIR}/.runtime/secrets.json")
  file(READ "${CPPCHAT_SOURCE_DIR}/.runtime/secrets.json" local_secrets)
  string(JSON redis_password GET "${local_secrets}" redis)
  set_target_properties(ResourceServer PROPERTIES VS_DEBUGGER_ENVIRONMENT
    "CPPCHAT_REDIS_HOST=127.0.0.1\nCPPCHAT_REDIS_PORT=6380\nCPPCHAT_REDIS_PASSWORD=${redis_password}\nCPPCHAT_RESOURCE_DIR=${CPPCHAT_SOURCE_DIR}/.runtime/ResourceServer/resource_data")
  unset(local_secrets)
  unset(redis_password)
endif()
