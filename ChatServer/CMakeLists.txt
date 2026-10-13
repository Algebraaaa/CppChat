cppchat_add_server(ChatServer ChatServerShared ChatServerShared
  "${CMAKE_CURRENT_SOURCE_DIR}/ChatServer.cpp")
target_link_libraries(ChatServer PRIVATE unofficial::mysql-connector-cpp::connector-jdbc)
target_compile_definitions(ChatServer PRIVATE CPPCHAT_NODE_NAME="ChatServer")
