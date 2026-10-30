cmake_minimum_required(VERSION 3.24)
project(MobileUpdateTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Network Concurrent Test)
get_filename_component(root "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(mobile_update_tests mobile_update_tests.cpp
  "${root}/CppChatAndroid/update/mobileupdateservice.cpp"
  "${root}/CppChatAndroid/update/downloadbackend.cpp"
  "${root}/CppChat/services/clientupdateservice.cpp")
target_include_directories(mobile_update_tests PRIVATE "${root}/CppChat" "${root}/CppChatAndroid")
target_link_libraries(mobile_update_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Test)
add_test(NAME mobile.updates COMMAND mobile_update_tests -o "${CMAKE_BINARY_DIR}/updates.xml,junitxml" -o "${CMAKE_BINARY_DIR}/updates.txt,txt")
set_tests_properties(mobile.updates PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)
