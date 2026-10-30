cmake_minimum_required(VERSION 3.24)
project(CppChatAndroid VERSION 0.1.24 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Point at the tested Windows client's CppChat directory; never copy a second
# implementation of its wire protocol into this frontend.
set(CPPCHAT_BACKEND_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../CppChat" CACHE PATH "Shared desktop client's CppChat source directory")
if(NOT EXISTS "${CPPCHAT_BACKEND_ROOT}/services/chatsyncservice.cpp")
  message(FATAL_ERROR "Set CPPCHAT_BACKEND_ROOT to the working repository's CppChat directory")
endif()
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Widgets Network Sql Svg Quick QuickControls2 Multimedia Concurrent)
qt_standard_project_setup(REQUIRES 6.9)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/qtkeychain EXCLUDE_FROM_ALL)
include("${CPPCHAT_BACKEND_ROOT}/cmake/VoiceAudio.cmake")
set(backend_sources
  common/attachmentpreparer.cpp common/fileattachment.cpp common/chatstore.cpp
  common/chatstorefavorites.cpp common/chatstoreorganizer.cpp common/contactpreferences.cpp
  common/conversationpreferences.cpp
  common/emojicatalog.cpp common/imageloader.cpp common/inputvalidator.cpp
  common/localdataprotection.cpp common/localstoresecurity.cpp common/logger.cpp
  common/runtimepaths.cpp common/securitypolicy.cpp common/userdata.cpp
  network/httpmgr.cpp network/resourcemgr.cpp network/tcpmgr.cpp network/usermgr.cpp
  services/chatsyncservice.cpp services/messagesender.cpp services/clientupdateservice.cpp
  services/voicecallservice.cpp services/voiceaudio.cpp services/videomedia.cpp services/voicemessageaudio.cpp)
list(TRANSFORM backend_sources PREPEND "${CPPCHAT_BACKEND_ROOT}/")
add_library(CppChatMobileCore OBJECT qml/appbridge.h qml/appbridge.cpp
  qml/appbridge_chat.cpp qml/appbridge_features.cpp qml/appbridge_session.cpp qml/appbridge_media.cpp
  qml/mobilemedia.cpp
  update/downloadbackend.cpp update/downloadbackend.h
  update/mobileupdateservice.cpp update/mobileupdateservice.h
  qml/mobileaccount.cpp qml/mobilesession.cpp ${backend_sources})

# Keep deployment endpoints outside version control. A fresh public checkout
# builds with the documented example until a local deployment config is supplied.
set(CPPCHAT_ANDROID_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/android/config.ini" CACHE FILEPATH "Private Android deployment configuration")
if(NOT EXISTS "${CPPCHAT_ANDROID_CONFIG_FILE}")
  set(CPPCHAT_ANDROID_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/android/config.example.ini")
endif()
set_source_files_properties("${CPPCHAT_ANDROID_CONFIG_FILE}" PROPERTIES QT_RESOURCE_ALIAS "android/config.ini")
qt_add_resources(CppChatMobileCore mobile_config PREFIX "/" FILES "${CPPCHAT_ANDROID_CONFIG_FILE}")
target_include_directories(CppChatMobileCore PUBLIC "${CPPCHAT_BACKEND_ROOT}"
  "${CPPCHAT_BACKEND_ROOT}/../shared"
  "${CPPCHAT_BACKEND_ROOT}/third_party/spdlog/include" "${CMAKE_CURRENT_SOURCE_DIR}/qml")
target_compile_definitions(CppChatMobileCore PUBLIC CPPCHAT_VERSION="${PROJECT_VERSION}")
target_link_libraries(CppChatMobileCore PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
  Qt6::Sql Qt6::Svg Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Concurrent Opus::opus qt6keychain)
if(WIN32)
  target_link_libraries(CppChatMobileCore PUBLIC crypt32 advapi32 bcrypt)
endif()
add_subdirectory(app)
option(CPPCHAT_MOBILE_TESTS "Build mobile facade and QML smoke probes" OFF)
if(CPPCHAT_MOBILE_TESTS AND NOT ANDROID)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  enable_testing()
  qt_add_executable(mobile_features tests/mobile_features.cpp)
  target_link_libraries(mobile_features PRIVATE CppChatMobileCore Qt6::Test)
  add_test(NAME mobile.profile COMMAND mobile_features profileResponseUpdatesCacheBeforeNotification)
  add_test(NAME mobile.features COMMAND mobile_features)
  qt_add_executable(mobile_media tests/mobile_media.cpp)
  target_link_libraries(mobile_media PRIVATE CppChatMobileCore Qt6::Test)
  add_test(NAME mobile.media COMMAND mobile_media)
  qt_add_executable(mobile_file_probe tests/mobile_file_probe.cpp)
  set(file_schema "${CPPCHAT_BACKEND_ROOT}/../database/sqlite/schema.sql")
  set_source_files_properties("${file_schema}" PROPERTIES QT_RESOURCE_ALIAS "chatstore-schema.sql")
  qt_add_resources(mobile_file_probe "file_storage" PREFIX "/storage" FILES "${file_schema}")
  target_link_libraries(mobile_file_probe PRIVATE CppChatMobileCore)
  qt_add_executable(mobile_session tests/mobile_session.cpp)
  set(session_schema "${CPPCHAT_BACKEND_ROOT}/../database/sqlite/schema.sql")
  set_source_files_properties("${session_schema}" PROPERTIES QT_RESOURCE_ALIAS "chatstore-schema.sql")
  qt_add_resources(mobile_session "session_storage" PREFIX "/storage" FILES "${session_schema}")
  target_link_libraries(mobile_session PRIVATE CppChatMobileCore Qt6::Test)
  add_test(NAME mobile.session.policy COMMAND mobile_session boundedRecoveryAndCancellation)
  qt_add_executable(mobile_interop tests/mobile_interop.cpp qml.qrc "${CPPCHAT_BACKEND_ROOT}/rc.qrc")
  target_link_libraries(mobile_interop PRIVATE CppChatMobileCore)
  qt_add_executable(mobile_ui_smoke tests/mobile_ui_smoke.cpp qml.qrc "${CPPCHAT_BACKEND_ROOT}/rc.qrc")
  target_link_libraries(mobile_ui_smoke PRIVATE CppChatMobileCore)
endif()
