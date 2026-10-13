cppchat_add_server(GateServer GateServer GateServer
  "${CPPCHAT_SOURCE_DIR}/ChatServerShared/BusinessExecutor.cpp"
  "${CPPCHAT_SOURCE_DIR}/ChatServerShared/MySqlPool.cpp"
  "${CPPCHAT_SOURCE_DIR}/ChatServerShared/MySqlPool.h")
target_link_libraries(GateServer PRIVATE unofficial::mysql-connector-cpp::connector-jdbc)
