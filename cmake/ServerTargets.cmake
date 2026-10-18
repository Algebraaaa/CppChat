include_guard(GLOBAL)

function(cppchat_add_protocol target directory)
  set(proto_dir "${CPPCHAT_SOURCE_DIR}/${directory}")
  set(generated "${CPPCHAT_BINARY_DIR}/generated/${target}")
  file(MAKE_DIRECTORY "${generated}")
  add_custom_command(OUTPUT "${generated}/message.pb.cc" "${generated}/message.pb.h" "${generated}/message.grpc.pb.cc" "${generated}/message.grpc.pb.h"
    COMMAND protobuf::protoc --proto_path=${proto_dir}
      --cpp_out=${generated} --grpc_out=${generated}
      --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
      "${proto_dir}/message.proto"
    DEPENDS "${proto_dir}/message.proto" protobuf::protoc gRPC::grpc_cpp_plugin VERBATIM)
  set(protocol_sources "${generated}/message.pb.cc" "${generated}/message.pb.h"
    "${generated}/message.grpc.pb.cc" "${generated}/message.grpc.pb.h")
  target_sources(${target} PRIVATE ${protocol_sources} "${proto_dir}/message.proto")
  source_group("协议文件" FILES "${proto_dir}/message.proto")
  source_group("构建辅助/协议生成" FILES ${protocol_sources})
  target_include_directories(${target} BEFORE PRIVATE "${generated}")
endfunction()

function(cppchat_map_debug_sources target output)
  # Compile shadow copies so quoted includes use regenerated protocol headers.
  # #line keeps debugger locations mapped to the editable source files.
  set(local_sources)
  set(original_sources)
  string(ASCII 239 187 191 utf8_bom)
  foreach(input IN LISTS ARGN)
    get_filename_component(source "${input}" ABSOLUTE)
    get_filename_component(name "${source}" NAME)
    if(name MATCHES "\\.pb\\.h$")
      continue()
    endif()
    configure_file("${source}" "${CPPCHAT_BINARY_DIR}/sources/${target}/.original/${name}" COPYONLY)
    file(READ "${CPPCHAT_BINARY_DIR}/sources/${target}/.original/${name}" contents)
    string(REGEX REPLACE "^${utf8_bom}" "" contents "${contents}")
    set(mapped "#line 1 \"${source}\"\n${contents}")
    set(shadow "${CPPCHAT_BINARY_DIR}/sources/${target}/${name}")
    set(previous "")
    if(EXISTS "${shadow}")
      file(READ "${shadow}" previous)
    endif()
    if(NOT previous STREQUAL mapped)
      file(WRITE "${shadow}" "${mapped}")
    endif()
    list(APPEND local_sources "${shadow}")
    list(APPEND original_sources "${source}")
    if(name MATCHES "\\.(h|hpp)$")
      set(editable_group "头文件")
    else()
      set(editable_group "源文件")
    endif()
    if(source MATCHES "/ChatServerShared/")
      string(APPEND editable_group "/共享")
    endif()
    source_group("${editable_group}" FILES "${source}")
  endforeach()
  set_source_files_properties(${original_sources} PROPERTIES HEADER_FILE_ONLY TRUE)
  source_group("构建辅助/编译副本" FILES ${local_sources})
  set(${output} ${local_sources} ${original_sources} PARENT_SCOPE)
endfunction()

function(cppchat_configure_server target)
  source_group("构建辅助/CMake" FILES "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
    REGULAR_EXPRESSION "\\.rule$")
  source_group("构建辅助/调试可视化" REGULAR_EXPRESSION "\\.natvis$")
  if(WIN32)
    set(runtime_directory "${CPPCHAT_BINARY_DIR}/bin/${target}")
  else()
    # Keep the executable paths used by the Linux systemd service units.
    set(runtime_directory "${CPPCHAT_BINARY_DIR}/bin")
  endif()
  set_target_properties(${target} PROPERTIES
    FOLDER "服务端"
    RUNTIME_OUTPUT_DIRECTORY "${runtime_directory}"
    VS_DEBUGGER_WORKING_DIRECTORY "${CPPCHAT_SOURCE_DIR}/.runtime/${target}"
    VS_DEBUGGER_COMMAND "$<TARGET_FILE:${target}>"
    VS_DEBUGGER_COMMAND_ARGUMENTS "")
endfunction()

function(cppchat_add_server target source_directory proto_directory)
  set(source_dir "${CPPCHAT_SOURCE_DIR}/${source_directory}")
  file(GLOB sources CONFIGURE_DEPENDS "${source_dir}/*.cpp" "${source_dir}/*.h")
  cppchat_map_debug_sources(${target} mapped_sources ${sources} ${ARGN})
  add_executable(${target} ${mapped_sources})
  target_include_directories(${target} PRIVATE "${CPPCHAT_BINARY_DIR}/sources/${target}"
    "${source_dir}" "${CPPCHAT_SOURCE_DIR}/ChatServerShared" "${CPPCHAT_SOURCE_DIR}/shared"
    "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/jdbc")
  target_link_libraries(${target} PRIVATE Boost::asio Boost::beast Boost::filesystem Boost::uuid
    Boost::property_tree gRPC::grpc++ protobuf::libprotobuf JsonCpp::JsonCpp
    hiredis::hiredis OpenSSL::SSL OpenSSL::Crypto)
  if(WIN32)
    target_link_libraries(${target} PRIVATE ws2_32 crypt32)
  endif()
  cppchat_add_protocol(${target} "${proto_directory}")
  cppchat_configure_server(${target})
endfunction()
