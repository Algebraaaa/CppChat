# Qt 6.12 also scans a target's SOURCE_DIR, even with QT_QML_ROOT_PATH set.
# Keep the application target here so tests/ is never a deployment scan root.
get_filename_component(mobile_source_root "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
qt_add_executable(CppChat main_android.cpp "${mobile_source_root}/qml.qrc"
  "${CPPCHAT_BACKEND_ROOT}/rc.qrc")
target_link_libraries(CppChat PRIVATE CppChatMobileCore)

if(ANDROID)
  set_target_properties(CppChat PROPERTIES
    QT_QML_ROOT_PATH "${mobile_source_root}/qml"
    QT_ANDROID_PACKAGE_SOURCE_DIR "${mobile_source_root}/android"
    QT_ANDROID_PACKAGE_NAME "org.algebra.cppchat"
    QT_ANDROID_MIN_SDK_VERSION 28
    QT_ANDROID_TARGET_SDK_VERSION 36
    QT_ANDROID_VERSION_CODE 124
    QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}")
  set(ANDROID_OPENSSL_ROOT "" CACHE PATH "Pinned android_openssl checkout with ssl_3 libraries")
  if(NOT ANDROID_OPENSSL_ROOT)
    message(FATAL_ERROR "Set ANDROID_OPENSSL_ROOT to the supported OpenSSL 3 Android build directory")
  endif()
  file(TO_CMAKE_PATH "${ANDROID_OPENSSL_ROOT}" ANDROID_OPENSSL_ROOT)
  set_property(TARGET CppChat APPEND PROPERTY QT_ANDROID_EXTRA_LIBS
    "${ANDROID_OPENSSL_ROOT}/libssl_3.so" "${ANDROID_OPENSSL_ROOT}/libcrypto_3.so")
endif()
