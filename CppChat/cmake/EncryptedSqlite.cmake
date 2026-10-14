# Reuse upstream Qt driver and SQLite3MC, with source archives pinned by SHA256.
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${repository_root}/build/dependencies")
FetchContent_Declare(cppchat_mc_driver
  URL "https://github.com/FalsinSoft/QSQLite3MultipleCiphers/archive/65accf531f33952c3431d914fcdf7f6c6cd02c35.zip"
  URL_HASH SHA256=fa93b821dedef3ae763cf2e329adaeae7b95729e7c9affb09e859eca53ba67f4
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(cppchat_mc
  URL "https://github.com/utelle/SQLite3MultipleCiphers/releases/download/v2.6.1/sqlite3mc-2.6.1-sqlite-3.54.0-amalgamation.zip"
  URL_HASH SHA256=3f25af603566a23c821d8e581778b5ece2a541d2435776de57b8d845c176815a
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(cppchat_mc_driver cppchat_mc)
find_package(Qt6 REQUIRED COMPONENTS CorePrivate SqlPrivate)
configure_file("${cppchat_mc_SOURCE_DIR}/sqlite3mc_amalgamation.h"
  "${CMAKE_CURRENT_BINARY_DIR}/encrypted-sqlite/include/sqlite3mc.h" COPYONLY)
add_library(CppChatSqliteMC STATIC "${cppchat_mc_SOURCE_DIR}/sqlite3mc_amalgamation.c")
set_target_properties(CppChatSqliteMC PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF
  POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(CppChatSqliteMC PRIVATE SQLITE_TEMP_STORE=3 SQLITE_THREADSAFE=1
  SQLITE_ENABLE_COLUMN_METADATA SQLITE_OMIT_LOAD_EXTENSION)
if(UNIX)
  target_link_libraries(CppChatSqliteMC PRIVATE ${CMAKE_DL_LIBS} m)
endif()
set(mc_driver "${cppchat_mc_driver_SOURCE_DIR}/QSQLite3MultipleCiphers")
qt_add_plugin(CppChatSqliteCipher SHARED PLUGIN_TYPE sqldrivers
  "${mc_driver}/qsql_sqlite.cpp" "${mc_driver}/qsql_sqlite_p.h"
  "${mc_driver}/qsql_sqlite_vfs.cpp" "${mc_driver}/qsql_sqlite_vfs_p.h"
  "${mc_driver}/qsql_sqlite_multiple_ciphers.cpp"
  "${mc_driver}/qsql_sqlite_multiple_ciphers_p.h" "${mc_driver}/smain.cpp")
target_include_directories(CppChatSqliteCipher PRIVATE "${cppchat_mc_SOURCE_DIR}"
  "${CMAKE_CURRENT_BINARY_DIR}/encrypted-sqlite/include")
target_link_libraries(CppChatSqliteCipher PRIVATE CppChatSqliteMC Qt6::CorePrivate Qt6::SqlPrivate)
target_compile_definitions(CppChatSqliteCipher PRIVATE QT_USE_QSTRINGBUILDER)
set(CPPCHAT_MC_DRIVER_SOURCE "${mc_driver}" CACHE INTERNAL "Pinned Qt encrypted driver sources")
set(CPPCHAT_MC_SOURCE "${cppchat_mc_SOURCE_DIR}" CACHE INTERNAL "Pinned SQLite3MC sources")
set_target_properties(CppChatSqliteCipher PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sqldrivers"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/sqldrivers")
