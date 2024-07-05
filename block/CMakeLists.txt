if(NOT SILKWORM_CORE_ONLY)

  find_package(absl REQUIRED)
  find_package(gRPC REQUIRED)
  find_package(Protobuf REQUIRED)
  find_package(CLI11 REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(brotli REQUIRED IMPORTED_TARGET libbrotlienc libbrotlidec)

  add_executable(copy_block copy_block.cpp)
  target_include_directories(copy_block PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(
    copy_block absl::flags_parse gRPC::grpc++_unsecure protobuf::libprotobuf silkworm_rpcdaemon CLI11::CLI11 PkgConfig::brotli
  )

endif()