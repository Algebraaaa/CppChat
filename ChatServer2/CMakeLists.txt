cppchat_add_server(ChatServer2 ChatServerShared ChatServerShared
  "${CMAKE_CURRENT_SOURCE_DIR}/ChatServer.cpp")
target_link_libraries(ChatServer2 PRIVATE unofficial::mysql-connector-cpp::connector-jdbc)
target_compile_definitions(ChatServer2 PRIVATE CPPCHAT_NODE_NAME="ChatServer2")
