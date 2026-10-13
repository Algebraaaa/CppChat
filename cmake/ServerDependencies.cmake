include_guard(GLOBAL)

if(MSVC)
  add_compile_options(/utf-8 /bigobj /MP2)
  add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX BOOST_ALL_NO_LIB)
  # Debug builds use /Od and symbols with the ABI of the installed static /MD libraries.
  add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)

  find_package(Boost REQUIRED COMPONENTS asio beast filesystem uuid property_tree)
  find_package(Protobuf CONFIG REQUIRED)
  find_package(gRPC CONFIG REQUIRED)
  find_package(jsoncpp CONFIG REQUIRED)
  find_package(hiredis CONFIG REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(unofficial-mysql-connector-cpp CONFIG REQUIRED)
else()
  find_package(Threads REQUIRED)
  find_package(Boost 1.74 REQUIRED COMPONENTS system filesystem thread)
  # Asio/Beast/UUID/PropertyTree are header-only in distribution Boost packages.
  foreach(component asio beast uuid property_tree)
    if(NOT TARGET Boost::${component})
      add_library(Boost::${component} INTERFACE IMPORTED)
      set_target_properties(Boost::${component} PROPERTIES
        INTERFACE_LINK_LIBRARIES "Boost::system;Threads::Threads"
        INTERFACE_INCLUDE_DIRECTORIES "${Boost_INCLUDE_DIRS}")
    endif()
  endforeach()
  find_package(Protobuf REQUIRED)
  find_package(gRPC CONFIG REQUIRED)
  find_package(jsoncpp CONFIG REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(HIREDIS REQUIRED IMPORTED_TARGET hiredis)
  add_library(hiredis::hiredis ALIAS PkgConfig::HIREDIS)
  find_path(MYSQLCPPCONN_INCLUDE_DIR mysql_driver.h PATH_SUFFIXES jdbc REQUIRED)
  find_library(MYSQLCPPCONN_LIBRARY mysqlcppconn REQUIRED)
  add_library(unofficial::mysql-connector-cpp::connector-jdbc UNKNOWN IMPORTED)
  set_target_properties(unofficial::mysql-connector-cpp::connector-jdbc PROPERTIES
    IMPORTED_LOCATION "${MYSQLCPPCONN_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MYSQLCPPCONN_INCLUDE_DIR}")
endif()
