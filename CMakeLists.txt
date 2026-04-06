# GRPC global cmake file
# This currently builds C and C++ code.
# This file has been automatically generated from a template file.
# Please look at the templates directory instead.
# This file can be regenerated from the template by running
# tools/buildgen/generate_projects.sh
#
# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.




cmake_minimum_required(VERSION 3.16)

set(PACKAGE_NAME          "grpc")
set(PACKAGE_VERSION       "1.81.0-dev")
set(gRPC_CORE_VERSION     "53.0.0")
set(gRPC_CORE_SOVERSION   "53")
set(gRPC_CPP_VERSION      "1.81.0-dev")
set(gRPC_CPP_SOVERSION    "1.81")
set(PACKAGE_STRING        "${PACKAGE_NAME} ${PACKAGE_VERSION}")
set(PACKAGE_TARNAME       "${PACKAGE_NAME}-${PACKAGE_VERSION}")
set(PACKAGE_BUGREPORT     "https://github.com/grpc/grpc/issues/")
project(${PACKAGE_NAME} LANGUAGES C CXX)

if(BUILD_SHARED_LIBS AND MSVC)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set(gRPC_INSTALL_BINDIR "bin" CACHE STRING "Installation directory for executables")
set(gRPC_INSTALL_LIBDIR "lib" CACHE STRING "Installation directory for libraries")
set(gRPC_INSTALL_INCLUDEDIR "include" CACHE STRING "Installation directory for headers")
set(gRPC_INSTALL_CMAKEDIR "lib/cmake/${PACKAGE_NAME}" CACHE STRING "Installation directory for cmake config files")
set(gRPC_INSTALL_SHAREDIR "share/grpc" CACHE STRING "Installation directory for root certificates")
set(gRPC_BUILD_MSVC_MP_COUNT 0 CACHE STRING "The maximum number of processes for MSVC /MP option")

# Options
option(gRPC_BUILD_TESTS "Build tests" OFF)
option(gRPC_BUILD_CODEGEN "Build codegen" ON)
option(gRPC_DOWNLOAD_ARCHIVES "Download archives for empty 3rd party directories" ON)

set(gRPC_INSTALL_default ON)
if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Disable gRPC_INSTALL by default if building as a submodule
  set(gRPC_INSTALL_default OFF)
endif()
set(gRPC_INSTALL ${gRPC_INSTALL_default} CACHE BOOL
    "Generate installation target")

# We can install dependencies from submodules if we're running
# CMake v3.13 or newer.
if(CMAKE_VERSION VERSION_LESS 3.13)
  set(_gRPC_INSTALL_SUPPORTED_FROM_MODULE OFF)
else()
  set(_gRPC_INSTALL_SUPPORTED_FROM_MODULE ON)
endif()

# Providers for third-party dependencies (gRPC_*_PROVIDER properties):
# "module": build the dependency using sources from git submodule (under third_party)
# "package": use cmake's find_package functionality to locate a pre-installed dependency

set(gRPC_ZLIB_PROVIDER "module" CACHE STRING "Provider of zlib library")
set_property(CACHE gRPC_ZLIB_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_CARES_PROVIDER "module" CACHE STRING "Provider of c-ares library")
set_property(CACHE gRPC_CARES_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_RE2_PROVIDER "module" CACHE STRING "Provider of re2 library")
set_property(CACHE gRPC_RE2_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_SSL_PROVIDER "module" CACHE STRING "Provider of ssl library")
set_property(CACHE gRPC_SSL_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_PROTOBUF_PROVIDER "module" CACHE STRING "Provider of protobuf library")
set_property(CACHE gRPC_PROTOBUF_PROVIDER PROPERTY STRINGS "module" "package")

if(gRPC_BUILD_TESTS)
  set(gRPC_BENCHMARK_PROVIDER "module" CACHE STRING "Provider of benchmark library")
  set_property(CACHE gRPC_BENCHMARK_PROVIDER PROPERTY STRINGS "module" "package")
else()
  set(gRPC_BENCHMARK_PROVIDER "none")
endif()

set(gRPC_ABSL_PROVIDER "module" CACHE STRING "Provider of absl library")
set_property(CACHE gRPC_ABSL_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_ABSL_USED_TARGETS
  absl_absl_check
  absl_absl_log
  absl_absl_vlog_is_on
  absl_algorithm
  absl_algorithm_container
  absl_any_invocable
  absl_atomic_hook
  absl_base
  absl_base_internal
  absl_bind_front
  absl_bits
  absl_bounded_utf8_length_sequence
  absl_btree
  absl_charset
  absl_check
  absl_city
  absl_civil_time
  absl_cleanup
  absl_cleanup_internal
  absl_common_policy_traits
  absl_compare
  absl_compressed_tuple
  absl_config
  absl_container_common
  absl_container_memory
  absl_cord
  absl_cord_internal
  absl_cordz_functions
  absl_cordz_handle
  absl_cordz_info
  absl_cordz_statistics
  absl_cordz_update_scope
  absl_cordz_update_tracker
  absl_core_headers
  absl_crc32c
  absl_crc_cord_state
  absl_crc_cpu_detect
  absl_crc_internal
  absl_debugging_internal
  absl_decode_rust_punycode
  absl_demangle_internal
  absl_demangle_rust
  absl_dynamic_annotations
  absl_endian
  absl_errno_saver
  absl_examine_stack
  absl_exponential_biased
  absl_fast_type_id
  absl_fixed_array
  absl_flags
  absl_flags_commandlineflag
  absl_flags_commandlineflag_internal
  absl_flags_config
  absl_flags_internal
  absl_flags_marshalling
  absl_flags_path_util
  absl_flags_private_handle_accessor
  absl_flags_program_name
  absl_flags_reflection
  absl_flat_hash_map
  absl_flat_hash_set
  absl_function_ref
  absl_graphcycles_internal
  absl_has_ostream_operator
  absl_hash
  absl_hash_container_defaults
  absl_hash_function_defaults
  absl_hash_policy_traits
  absl_hashtable_control_bytes
  absl_hashtable_debug_hooks
  absl_hashtablez_sampler
  absl_inlined_vector
  absl_inlined_vector_internal
  absl_int128
  absl_iterator_traits_internal
  absl_kernel_timeout_internal
  absl_layout
  absl_leak_check
  absl_log
  absl_log_entry
  absl_log_globals
  absl_log_internal_append_truncated
  absl_log_internal_check_impl
  absl_log_internal_check_op
  absl_log_internal_conditions
  absl_log_internal_config
  absl_log_internal_fnmatch
  absl_log_internal_format
  absl_log_internal_globals
  absl_log_internal_log_impl
  absl_log_internal_log_sink_set
  absl_log_internal_message
  absl_log_internal_nullguard
  absl_log_internal_nullstream
  absl_log_internal_proto
  absl_log_internal_strip
  absl_log_internal_structured_proto
  absl_log_internal_voidify
  absl_log_severity
  absl_log_sink
  absl_log_sink_registry
  absl_low_level_hash
  absl_malloc_internal
  absl_memory
  absl_no_destructor
  absl_node_hash_map
  absl_node_slot_policy
  absl_non_temporal_arm_intrinsics
  absl_non_temporal_memcpy
  absl_nullability
  absl_numeric_representation
  absl_optional
  absl_prefetch
  absl_random_bit_gen_ref
  absl_random_distributions
  absl_random_internal_distribution_caller
  absl_random_internal_entropy_pool
  absl_random_internal_fast_uniform_bits
  absl_random_internal_fastmath
  absl_random_internal_generate_real
  absl_random_internal_iostream_state_saver
  absl_random_internal_nonsecure_base
  absl_random_internal_pcg_engine
  absl_random_internal_platform
  absl_random_internal_randen
  absl_random_internal_randen_engine
  absl_random_internal_randen_hwaes
  absl_random_internal_randen_hwaes_impl
  absl_random_internal_randen_slow
  absl_random_internal_salted_seed_seq
  absl_random_internal_seed_material
  absl_random_internal_traits
  absl_random_internal_uniform_helper
  absl_random_internal_wide_multiply
  absl_random_random
  absl_random_seed_gen_exception
  absl_random_seed_sequences
  absl_raw_hash_map
  absl_raw_hash_set
  absl_raw_hash_set_resize_impl
  absl_raw_logging_internal
  absl_sample_recorder
  absl_span
  absl_spinlock_wait
  absl_stacktrace
  absl_status
  absl_statusor
  absl_str_format
  absl_str_format_internal
  absl_strerror
  absl_string_view
  absl_strings
  absl_strings_internal
  absl_symbolize
  absl_synchronization
  absl_throw_delegate
  absl_time
  absl_time_zone
  absl_tracing_internal
  absl_type_traits
  absl_utf8_for_code_point
  absl_utility
  absl_variant
  absl_vlog_config_internal
  absl_vlog_is_on
  absl_weakly_mixed_integer
  absl_meta
)

# The OpenTelemetry plugin supports "package" build only at present.
set(gRPC_OPENTELEMETRY_PROVIDER "package")
#  set(gRPC_OPENTELEMETRY_PROVIDER "module" CACHE STRING "Provider of opentelemetry library")
#  set_property(CACHE gRPC_OPENTELEMETRY_PROVIDER PROPERTY STRINGS "module" "package")

set(gRPC_USE_PROTO_LITE OFF CACHE BOOL "Use the protobuf-lite library")

if(UNIX)
  if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(_gRPC_PLATFORM_LINUX ON)
    if(NOT CMAKE_CROSSCOMPILING AND CMAKE_SIZEOF_VOID_P EQUAL 4)
      message("+++ Enabling SSE2 for ${CMAKE_SYSTEM_PROCESSOR}")
      set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} -msse2")
    endif()
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(_gRPC_PLATFORM_MAC ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "iOS")
    set(_gRPC_PLATFORM_IOS ON)
  elseif(${CMAKE_SYSTEM_NAME} MATCHES "Android")
    set(_gRPC_PLATFORM_ANDROID ON)
  else()
    set(_gRPC_PLATFORM_POSIX ON)
  endif()
endif()
if(WIN32)
  set(_gRPC_PLATFORM_WINDOWS ON)
endif()

if (APPLE AND NOT DEFINED CMAKE_CXX_STANDARD)
  # AppleClang defaults to C++98, so we bump it to C++17.
  message("CMAKE_CXX_STANDARD was undefined, defaulting to C++17.")
  set(CMAKE_CXX_STANDARD 17)
endif ()

if (NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
  set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

if(MSVC)
  include(cmake/msvc_static_runtime.cmake)
  add_definitions(-D_WIN32_WINNT=0x600 -D_SCL_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_WARNINGS -D_WINSOCK_DEPRECATED_NO_WARNINGS)
  # Set /MP option
  if (gRPC_BUILD_MSVC_MP_COUNT GREATER 0)
    set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /MP${gRPC_BUILD_MSVC_MP_COUNT}")
  elseif (gRPC_BUILD_MSVC_MP_COUNT LESS 0)
    set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /MP")
  endif()
  # needed to compile protobuf
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4065 /wd4506")
  # TODO(jtattermusch): revisit warnings that were silenced as part of upgrade to protobuf3.6.0
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4200 /wd4291 /wd4244")
  # TODO(jtattermusch): revisit C4267 occurrences throughout the code
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4267")
  # TODO(jtattermusch): needed to build boringssl with VS2017, revisit later
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4987 /wd4774 /wd4819 /wd4996 /wd4619")
  # Silences thousands of trucation warnings
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /wd4503")
  # Tell MSVC to build grpc using utf-8
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /utf-8")
  # Inconsistent object sizes can cause stack corruption and should be treated as an error
  set(_gRPC_C_CXX_FLAGS "${_gRPC_C_CXX_FLAGS} /we4789")
  # To decrease the size of PDB files
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /opt:ref /opt:icf /pdbcompress")
endif()
if (MINGW)
  add_definitions(-D_WIN32_WINNT=0x600)
endif()
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${_gRPC_C_CXX_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${_gRPC_C_CXX_FLAGS}")

if(gRPC_USE_PROTO_LITE)
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf-lite")
  add_definitions("-DGRPC_USE_PROTO_LITE")
else()
  set(_gRPC_PROTOBUF_LIBRARY_NAME "libprotobuf")
endif()

if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_MAC OR _gRPC_PLATFORM_IOS)
  set(_gRPC_CORE_NOSTDCXX_FLAGS -fno-exceptions -fno-rtti)
else()
  set(_gRPC_CORE_NOSTDCXX_FLAGS "")
endif()

if(UNIX AND NOT HAIKU)
  # -pthread does more than -lpthread
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads)
  set(_gRPC_ALLTARGETS_LIBRARIES ${CMAKE_DL_LIBS} m Threads::Threads)
  if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
    find_library(LIBRT rt)
    if(LIBRT)
      set(_gRPC_ALLTARGETS_LIBRARIES ${_gRPC_ALLTARGETS_LIBRARIES} rt)
    endif()
  endif()
endif()

include(CheckCXXSourceCompiles)

if(UNIX OR APPLE)
  # Some systems require the __STDC_FORMAT_MACROS macro to be defined
  # to get the fixed-width integer type formatter macros.
  check_cxx_source_compiles("#include <inttypes.h>
  #include <cstdio>
  int main()
  {
    int64_t i64{};
    std::printf(\"%\" PRId64, i64);
  }"
  HAVE_STDC_FORMAT_MACROS)
  if(NOT HAVE_STDC_FORMAT_MACROS)
    add_definitions(-D__STDC_FORMAT_MACROS)
  endif()
endif()

# configure ccache if requested
include(cmake/ccache.cmake)

include(cmake/abseil-cpp.cmake)
include(cmake/address_sorting.cmake)
include(cmake/benchmark.cmake)
include(cmake/cares.cmake)
include(cmake/protobuf.cmake)
include(cmake/re2.cmake)
include(cmake/ssl.cmake)
include(cmake/upb.cmake)
include(cmake/xxhash.cmake)
include(cmake/zlib.cmake)
include(cmake/download_archive.cmake)

if(_gRPC_PLATFORM_LINUX OR _gRPC_PLATFORM_POSIX)
  include(cmake/systemd.cmake)
  set(_gRPC_ALLTARGETS_LIBRARIES ${_gRPC_ALLTARGETS_LIBRARIES} ${_gRPC_SYSTEMD_LIBRARIES})
endif()

option(gRPC_BUILD_GRPCPP_OTEL_PLUGIN "Build grpcpp_otel_plugin" OFF)
if(gRPC_BUILD_GRPCPP_OTEL_PLUGIN)
  include(cmake/opentelemetry-cpp.cmake)
endif()

# Setup external proto library at third_party/cel-spec if it doesn't exist
if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cel-spec AND gRPC_DOWNLOAD_ARCHIVES)
  # Download the archive via HTTP, validate the checksum, and extract to third_party/cel-spec.
  download_archive(
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cel-spec
    https://github.com/google/cel-spec/archive/refs/tags/v0.25.1.tar.gz
    13583c5a312861648449845b709722676a3c9b43396b6b8e9cbe4538feb74ad2
    cel-spec-0.25.1
  )
endif()
# Setup external proto library at third_party/envoy-api if it doesn't exist
if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/envoy-api AND gRPC_DOWNLOAD_ARCHIVES)
  # Download the archive via HTTP, validate the checksum, and extract to third_party/envoy-api.
  download_archive(
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/envoy-api
    https://github.com/envoyproxy/data-plane-api/archive/6ef568cf4a67362849911d1d2a546fd9f35db2ff.tar.gz
    ed5e6c319f8ebcdf24a9491f866a599bb9a3c193b859a94ad13bd31f85b46855
    data-plane-api-6ef568cf4a67362849911d1d2a546fd9f35db2ff
  )
endif()
# Setup external proto library at third_party/googleapis if it doesn't exist
if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/googleapis AND gRPC_DOWNLOAD_ARCHIVES)
  # Download the archive via HTTP, validate the checksum, and extract to third_party/googleapis.
  download_archive(
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/googleapis
    https://github.com/googleapis/googleapis/archive/2193a2bfcecb92b92aad7a4d81baa428cafd7dfd.zip
    07a6f700cee5ca796f5230876ed8454bf1d8cc409b3b3e634505bc4f32a9f72b
    googleapis-2193a2bfcecb92b92aad7a4d81baa428cafd7dfd
  )
endif()
# Setup external proto library at third_party/protoc-gen-validate if it doesn't exist
if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/protoc-gen-validate AND gRPC_DOWNLOAD_ARCHIVES)
  # Download the archive via HTTP, validate the checksum, and extract to third_party/protoc-gen-validate.
  download_archive(
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/protoc-gen-validate
    https://github.com/bufbuild/protoc-gen-validate/archive/refs/tags/v1.2.1.tar.gz
    e4718352754df1393b8792b631338aa8562f390e8160783e365454bc11d96328
    protoc-gen-validate-1.2.1
  )
endif()
# Setup external proto library at third_party/xds if it doesn't exist
if (NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xds AND gRPC_DOWNLOAD_ARCHIVES)
  # Download the archive via HTTP, validate the checksum, and extract to third_party/xds.
  download_archive(
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/xds
    https://github.com/cncf/xds/archive/ee656c7534f5d7dc23d44dd611689568f72017a6.tar.gz
    49535f3c3370004309da50194c09bbfc528d4702424dd46e7d56a278a3dfc15d
    xds-ee656c7534f5d7dc23d44dd611689568f72017a6
  )
endif()

if(WIN32)
  set(_gRPC_ALLTARGETS_LIBRARIES ${_gRPC_ALLTARGETS_LIBRARIES} ws2_32 crypt32)
  set(_gRPC_STATIC_WIN32 STATIC)
endif()
  
if(BUILD_SHARED_LIBS AND WIN32)

# Currently for shared lib on Windows (i.e. a DLL) certain bits of source code
# are generated from protobuf definitions by upbc. This source code does not include
# annotations needed to export these functions from grpc.lib so we have to
# re-include a small subset of these.
#
# This is not an ideal situation because these functions will be unavailable
# to clients of grpc and the libraries that need this (e.g. grpc++) will
# include redundant duplicate code. Hence, the duplication is only activated
# for DLL builds - and should be completely removed when source files are
# generated with the necessary __declspec annotations.
set(gRPC_UPB_GEN_DUPL_SRC
  src/core/ext/upb-gen/src/proto/grpc/gcp/altscontext.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/health/v1/health.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/transport_security_common.upb_minitable.c
)

set(gRPC_ADDITIONAL_DLL_SRC
  src/core/credentials/transport/tls/grpc_tls_certificate_distributor.cc
  src/core/credentials/transport/tls/grpc_tls_certificate_provider.cc
  src/core/credentials/transport/tls/grpc_tls_certificate_verifier.cc
  src/core/credentials/transport/tls/grpc_tls_credentials_options.cc
)

set(gRPC_ADDITIONAL_DLL_CXX_SRC
  src/cpp/common/tls_certificate_provider.cc
  src/cpp/common/tls_certificate_verifier.cc
  src/cpp/common/tls_credentials_options.cc
)

endif() # BUILD_SHARED_LIBS AND WIN32

# Create directory for proto source files
set(_gRPC_PROTO_SRCS_DIR ${CMAKE_BINARY_DIR}/protos)
file(MAKE_DIRECTORY ${_gRPC_PROTO_SRCS_DIR})
# Create directory for generated .proto files
set(_gRPC_PROTO_GENS_DIR ${CMAKE_BINARY_DIR}/gens)
file(MAKE_DIRECTORY ${_gRPC_PROTO_GENS_DIR})

#  protobuf_generate_grpc_cpp
#  --------------------------
#
#   This method is no longer used by gRPC's CMake build process. However, it
#   is used by many open source dependencies, that we might want to keep
#   backward compatibility here.
#
#   Add custom commands to process ``.proto`` files to C++ using protoc and
#   GRPC plugin::
#
#     protobuf_generate_grpc_cpp [<ARGN>...]
#
#   ``ARGN``
#     ``.proto`` files
#
function(protobuf_generate_grpc_cpp)
  if(NOT ARGN)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_GRPC_CPP() called without any proto files")
    return()
  endif()

  foreach(FIL ${ARGN})
    protobuf_generate_grpc_cpp_with_import_path_correction(${FIL} ${FIL})
  endforeach()
endfunction()

#  protobuf_generate_grpc_cpp_with_import_path_correction
#  --------------------------
#
#   Add custom commands to process ``.proto`` files to C++ using protoc and
#   GRPC plugin::
#
#     protobuf_generate_grpc_cpp_with_import_path_correction <FILE_LOCATION> <IMPORT_PATH>
#
#   ``FILE_LOCATION``
#     The relative path of the ``.proto`` file to the project root
#   ``IMPORT_PATH``
#     The proto import path that itself expected to be placed in. For
#     example, a "bar.proto" file wants to be imported as
#     `import "foo/bar.proto"`. Then we should place it under
#     "<ProtoBuf_Include_Path>/foo/bar.proto" instead of
#     "<ProtoBuf_Include_Path>/third_party/foo/bar.proto". This ensures
#     correct symbol being generated and C++ include path being correct.
#     More info can be found at https://github.com/grpc/grpc/pull/25272.
#
function(protobuf_generate_grpc_cpp_with_import_path_correction FILE_LOCATION IMPORT_PATH)
  if(NOT FILE_LOCATION)
    message(SEND_ERROR "Error: PROTOBUF_GENERATE_GRPC_CPP() called without any proto files")
    return()
  endif()

  # Sets the include path for ProtoBuf files
  set(_protobuf_include_path -I . -I ${_gRPC_PROTOBUF_WELLKNOWN_INCLUDE_DIR})
  # The absolute path of the expected place for the input proto file
  # For example, health proto has package name grpc.health.v1, it's expected to be:
  #   ${_gRPC_PROTO_SRCS_DIR}/grpc/health/v1/health.proto
  get_filename_component(ABS_FIL ${_gRPC_PROTO_SRCS_DIR}/${IMPORT_PATH} ABSOLUTE)
  # Get the name of the file, which used to generate output file names for
  # this command.
  # Example: "health" for "health.proto"
  get_filename_component(FIL_WE ${_gRPC_PROTO_SRCS_DIR}/${IMPORT_PATH} NAME_WE)
  # Get the relative path between the expected place for the proto and the
  # working directory. In normal cases, it would be equal IMPORT_PATH, but
  # it's better to be agnostic to all the global folder locations (like the
  # centralized location ${_gRPC_PROTO_SRCS_DIR}).
  # Example: grpc/health/v1/health.proto
  file(RELATIVE_PATH REL_FIL ${_gRPC_PROTO_SRCS_DIR} ${ABS_FIL})
  # Get the directory of the relative path.
  # Example: grpc/health/v1
  get_filename_component(REL_DIR ${REL_FIL} DIRECTORY)
  # Get the directory and name for output filenames generation.
  # Example: "grpc/health/v1/health", the file name extension is omitted.
  set(RELFIL_WE "${REL_DIR}/${FIL_WE}")
  # Copy the proto file to a centralized location, with the correct import
  # path. For example, health proto has package name grpc.health.v1, the bash
  # equivalent would be:
  #   cp src/proto/grpc/health/v1/health.proto ${_gRPC_PROTO_SRCS_DIR}/grpc/health/v1
  file(MAKE_DIRECTORY ${_gRPC_PROTO_SRCS_DIR}/${REL_DIR})
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/${FILE_LOCATION} DESTINATION ${_gRPC_PROTO_SRCS_DIR}/${REL_DIR})

  #if cross-compiling, find host plugin
  if(CMAKE_CROSSCOMPILING)
    find_program(_gRPC_CPP_PLUGIN grpc_cpp_plugin)
  else()
    set(_gRPC_CPP_PLUGIN $<TARGET_FILE:grpc_cpp_plugin>)
  endif()

  add_custom_command(
    OUTPUT "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.cc"
           "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.grpc.pb.h"
           "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}_mock.grpc.pb.h"
           "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.cc"
           "${_gRPC_PROTO_GENS_DIR}/${RELFIL_WE}.pb.h"
    COMMAND ${_gRPC_PROTOBUF_PROTOC_EXECUTABLE}
    ARGS --grpc_out=generate_mock_code=true:${_gRPC_PROTO_GENS_DIR}
         --cpp_out=${_gRPC_PROTO_GENS_DIR}
         --plugin=protoc-gen-grpc=${_gRPC_CPP_PLUGIN}
         ${_protobuf_include_path}
         ${REL_FIL}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${FILE_LOCATION} ${ABS_FIL} ${_gRPC_PROTOBUF_PROTOC} ${_gRPC_CPP_PLUGIN}
    WORKING_DIRECTORY ${_gRPC_PROTO_SRCS_DIR}
    COMMENT "Running gRPC C++ protocol buffer compiler for ${IMPORT_PATH}"
    VERBATIM)
endfunction()

# These options allow users to enable or disable the building of the various
# protoc plugins. For example, running CMake with
# -DgRPC_BUILD_GRPC_CSHARP_PLUGIN=OFF will disable building the C# plugin.
set(_gRPC_PLUGIN_LIST)
option(gRPC_BUILD_GRPC_CPP_PLUGIN "Build grpc_cpp_plugin" ON)
if (gRPC_BUILD_GRPC_CPP_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_cpp_plugin)
endif ()
option(gRPC_BUILD_GRPC_CSHARP_PLUGIN "Build grpc_csharp_plugin" ON)
if (gRPC_BUILD_GRPC_CSHARP_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_csharp_plugin)
endif ()
option(gRPC_BUILD_GRPC_NODE_PLUGIN "Build grpc_node_plugin" ON)
if (gRPC_BUILD_GRPC_NODE_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_node_plugin)
endif ()
option(gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN "Build grpc_objective_c_plugin" ON)
if (gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_objective_c_plugin)
endif ()
option(gRPC_BUILD_GRPC_PHP_PLUGIN "Build grpc_php_plugin" ON)
if (gRPC_BUILD_GRPC_PHP_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_php_plugin)
endif ()
option(gRPC_BUILD_GRPC_PYTHON_PLUGIN "Build grpc_python_plugin" ON)
if (gRPC_BUILD_GRPC_PYTHON_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_python_plugin)
endif ()
option(gRPC_BUILD_GRPC_RUBY_PLUGIN "Build grpc_ruby_plugin" ON)
if (gRPC_BUILD_GRPC_RUBY_PLUGIN)
  list(APPEND _gRPC_PLUGIN_LIST grpc_ruby_plugin)
endif ()

add_custom_target(plugins
  DEPENDS ${_gRPC_PLUGIN_LIST}
)

add_custom_target(tools_c
  DEPENDS
)

add_custom_target(tools_cxx
  DEPENDS
)

add_custom_target(tools
  DEPENDS tools_c tools_cxx)

protobuf_generate_grpc_cpp_with_import_path_correction(
  src/core/ext/transport/chaotic_good/chaotic_good_frame.proto src/core/ext/transport/chaotic_good/chaotic_good_frame.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/channelz/channelz.proto src/proto/grpc/channelz/channelz.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/channelz/v2/channelz.proto src/proto/grpc/channelz/v2/channelz.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/channelz/v2/latent_see.proto src/proto/grpc/channelz/v2/latent_see.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/channelz/v2/property_list.proto src/proto/grpc/channelz/v2/property_list.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/channelz/v2/service.proto src/proto/grpc/channelz/v2/service.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/reflection/v1/reflection.proto src/proto/grpc/reflection/v1/reflection.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/reflection/v1alpha/reflection.proto src/proto/grpc/reflection/v1alpha/reflection.proto
)

# This enables CMake to build the project without requiring submodules
# in the third_party directory as long as test builds are disabled.
if (gRPC_BUILD_TESTS)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/benchmark_service.proto src/proto/grpc/testing/benchmark_service.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/control.proto src/proto/grpc/testing/control.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/echo.proto src/proto/grpc/testing/echo.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/echo_messages.proto src/proto/grpc/testing/echo_messages.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/empty.proto src/proto/grpc/testing/empty.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/messages.proto src/proto/grpc/testing/messages.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/payloads.proto src/proto/grpc/testing/payloads.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/report_qps_scenario_service.proto src/proto/grpc/testing/report_qps_scenario_service.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/simple_messages.proto src/proto/grpc/testing/simple_messages.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/stats.proto src/proto/grpc/testing/stats.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/test.proto src/proto/grpc/testing/test.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  src/proto/grpc/testing/worker_service.proto src/proto/grpc/testing/worker_service.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  third_party/googleapis/google/api/http.proto google/api/http.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  third_party/googleapis/google/rpc/status.proto google/rpc/status.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  third_party/protoc-gen-validate/validate/validate.proto validate/validate.proto
)
protobuf_generate_grpc_cpp_with_import_path_correction(
  third_party/xds/xds/data/orca/v3/orca_load_report.proto xds/data/orca/v3/orca_load_report.proto
)
endif()

if(gRPC_BUILD_TESTS)
  add_custom_target(buildtests_c)

  add_custom_target(buildtests_cxx)
  add_dependencies(buildtests_cxx grpc_cli)
  add_dependencies(buildtests_cxx http2_client)
  add_dependencies(buildtests_cxx interop_client)
  add_dependencies(buildtests_cxx interop_server)
  add_dependencies(buildtests_cxx qps_json_driver)
  add_dependencies(buildtests_cxx qps_worker)

  add_custom_target(buildtests
    DEPENDS buildtests_c buildtests_cxx)
endif()



add_library(address_sorting
  third_party/address_sorting/address_sorting.c
  third_party/address_sorting/address_sorting_posix.c
  third_party/address_sorting/address_sorting_windows.c
)

target_compile_features(address_sorting PUBLIC cxx_std_17)

set_target_properties(address_sorting PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(address_sorting PROPERTIES COMPILE_PDB_NAME "address_sorting"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/address_sorting.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(address_sorting
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(address_sorting
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



if(gRPC_INSTALL)
  install(TARGETS address_sorting EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(gpr
  src/core/config/config_vars.cc
  src/core/config/config_vars_non_generated.cc
  src/core/config/load_config.cc
  src/core/lib/event_engine/thread_local.cc
  src/core/util/alloc.cc
  src/core/util/crash.cc
  src/core/util/examine_stack.cc
  src/core/util/fork.cc
  src/core/util/gpr_time.cc
  src/core/util/host_port.cc
  src/core/util/iphone/cpu.cc
  src/core/util/linux/cpu.cc
  src/core/util/linux/env.cc
  src/core/util/log.cc
  src/core/util/mpscq.cc
  src/core/util/msys/tmpfile.cc
  src/core/util/posix/cpu.cc
  src/core/util/posix/env.cc
  src/core/util/posix/stat.cc
  src/core/util/posix/string.cc
  src/core/util/posix/sync.cc
  src/core/util/posix/thd.cc
  src/core/util/posix/time.cc
  src/core/util/posix/tmpfile.cc
  src/core/util/strerror.cc
  src/core/util/string.cc
  src/core/util/sync.cc
  src/core/util/sync_abseil.cc
  src/core/util/tchar.cc
  src/core/util/time_precise.cc
  src/core/util/time_util.cc
  src/core/util/windows/cpu.cc
  src/core/util/windows/env.cc
  src/core/util/windows/stat.cc
  src/core/util/windows/string.cc
  src/core/util/windows/string_util.cc
  src/core/util/windows/sync.cc
  src/core/util/windows/thd.cc
  src/core/util/windows/time.cc
  src/core/util/windows/tmpfile.cc
)

target_compile_features(gpr PUBLIC cxx_std_17)

set_target_properties(gpr PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(gpr PROPERTIES COMPILE_PDB_NAME "gpr"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(gpr
    PRIVATE
      "GPR_DLL_EXPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gpr.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(gpr
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(gpr
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::base
  absl::config
  absl::core_headers
  absl::log_severity
  absl::cleanup
  absl::flags
  absl::flags_marshalling
  absl::any_invocable
  absl::bind_front
  absl::check
  absl::log_globals
  absl::log
  absl::memory
  absl::bits
  absl::random_random
  absl::status
  absl::cord
  absl::str_format
  absl::strings
  absl::synchronization
  absl::time
  absl::optional
)
if(_gRPC_PLATFORM_ANDROID)
  target_link_libraries(gpr
    android
    log
  )
endif()

foreach(_hdr
  include/grpc/impl/codegen/atm.h
  include/grpc/impl/codegen/atm_gcc_atomic.h
  include/grpc/impl/codegen/atm_gcc_sync.h
  include/grpc/impl/codegen/atm_windows.h
  include/grpc/impl/codegen/gpr_types.h
  include/grpc/impl/codegen/log.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/impl/codegen/sync.h
  include/grpc/impl/codegen/sync_abseil.h
  include/grpc/impl/codegen/sync_custom.h
  include/grpc/impl/codegen/sync_generic.h
  include/grpc/impl/codegen/sync_posix.h
  include/grpc/impl/codegen/sync_windows.h
  include/grpc/support/alloc.h
  include/grpc/support/atm.h
  include/grpc/support/atm_gcc_atomic.h
  include/grpc/support/atm_gcc_sync.h
  include/grpc/support/atm_windows.h
  include/grpc/support/cpu.h
  include/grpc/support/json.h
  include/grpc/support/log.h
  include/grpc/support/log_windows.h
  include/grpc/support/metrics.h
  include/grpc/support/port_platform.h
  include/grpc/support/string_util.h
  include/grpc/support/sync.h
  include/grpc/support/sync_abseil.h
  include/grpc/support/sync_custom.h
  include/grpc/support/sync_generic.h
  include/grpc/support/sync_posix.h
  include/grpc/support/sync_windows.h
  include/grpc/support/thd_id.h
  include/grpc/support/time.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS gpr EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc
  src/core/call/call_arena_allocator.cc
  src/core/call/call_filters.cc
  src/core/call/call_spine.cc
  src/core/call/call_state.cc
  src/core/call/client_call.cc
  src/core/call/interception_chain.cc
  src/core/call/message.cc
  src/core/call/metadata.cc
  src/core/call/metadata_batch.cc
  src/core/call/metadata_info.cc
  src/core/call/parsed_metadata.cc
  src/core/call/request_buffer.cc
  src/core/call/security_context.cc
  src/core/call/server_call.cc
  src/core/call/status_util.cc
  src/core/channelz/channel_trace.cc
  src/core/channelz/channelz.cc
  src/core/channelz/channelz_registry.cc
  src/core/channelz/property_list.cc
  src/core/channelz/text_encode.cc
  src/core/channelz/v2tov1/convert.cc
  src/core/channelz/v2tov1/legacy_api.cc
  src/core/channelz/v2tov1/property_list.cc
  src/core/client_channel/backup_poller.cc
  src/core/client_channel/buffered_call.cc
  src/core/client_channel/client_channel.cc
  src/core/client_channel/client_channel_factory.cc
  src/core/client_channel/client_channel_filter.cc
  src/core/client_channel/client_channel_plugin.cc
  src/core/client_channel/client_channel_service_config.cc
  src/core/client_channel/direct_channel.cc
  src/core/client_channel/dynamic_filters.cc
  src/core/client_channel/global_subchannel_pool.cc
  src/core/client_channel/lb_metadata.cc
  src/core/client_channel/load_balanced_call_destination.cc
  src/core/client_channel/local_subchannel_pool.cc
  src/core/client_channel/retry_filter.cc
  src/core/client_channel/retry_filter_legacy_call_data.cc
  src/core/client_channel/retry_interceptor.cc
  src/core/client_channel/retry_service_config.cc
  src/core/client_channel/retry_throttle.cc
  src/core/client_channel/subchannel.cc
  src/core/client_channel/subchannel_pool_interface.cc
  src/core/client_channel/subchannel_stream_client.cc
  src/core/client_channel/subchannel_stream_limiter.cc
  src/core/config/core_configuration.cc
  src/core/credentials/call/call_creds_registry_init.cc
  src/core/credentials/call/call_creds_util.cc
  src/core/credentials/call/composite/composite_call_credentials.cc
  src/core/credentials/call/external/aws_external_account_credentials.cc
  src/core/credentials/call/external/aws_request_signer.cc
  src/core/credentials/call/external/external_account_credentials.cc
  src/core/credentials/call/external/file_external_account_credentials.cc
  src/core/credentials/call/external/url_external_account_credentials.cc
  src/core/credentials/call/gcp_service_account_identity/gcp_service_account_identity_credentials.cc
  src/core/credentials/call/iam/iam_credentials.cc
  src/core/credentials/call/json_util.cc
  src/core/credentials/call/jwt/json_token.cc
  src/core/credentials/call/jwt/jwt_credentials.cc
  src/core/credentials/call/jwt/jwt_verifier.cc
  src/core/credentials/call/jwt_token_file/jwt_token_file_call_credentials.cc
  src/core/credentials/call/jwt_util.cc
  src/core/credentials/call/oauth2/oauth2_credentials.cc
  src/core/credentials/call/plugin/plugin_credentials.cc
  src/core/credentials/call/token_fetcher/token_fetcher_credentials.cc
  src/core/credentials/transport/alts/alts_credentials.cc
  src/core/credentials/transport/alts/alts_security_connector.cc
  src/core/credentials/transport/alts/check_gcp_environment.cc
  src/core/credentials/transport/alts/check_gcp_environment_linux.cc
  src/core/credentials/transport/alts/check_gcp_environment_no_op.cc
  src/core/credentials/transport/alts/check_gcp_environment_windows.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_client_options.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_options.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_server_options.cc
  src/core/credentials/transport/channel_creds_registry_init.cc
  src/core/credentials/transport/composite/composite_channel_credentials.cc
  src/core/credentials/transport/fake/fake_credentials.cc
  src/core/credentials/transport/fake/fake_security_connector.cc
  src/core/credentials/transport/google_default/credentials_generic.cc
  src/core/credentials/transport/google_default/google_default_credentials.cc
  src/core/credentials/transport/insecure/insecure_credentials.cc
  src/core/credentials/transport/insecure/insecure_security_connector.cc
  src/core/credentials/transport/local/local_credentials.cc
  src/core/credentials/transport/local/local_security_connector.cc
  src/core/credentials/transport/security_connector.cc
  src/core/credentials/transport/ssl/ssl_credentials.cc
  src/core/credentials/transport/ssl/ssl_security_connector.cc
  src/core/credentials/transport/tls/certificate_provider_registry.cc
  src/core/credentials/transport/tls/grpc_tls_certificate_distributor.cc
  src/core/credentials/transport/tls/grpc_tls_certificate_match.cc
  src/core/credentials/transport/tls/grpc_tls_certificate_provider.cc
  src/core/credentials/transport/tls/grpc_tls_certificate_verifier.cc
  src/core/credentials/transport/tls/grpc_tls_credentials_options.cc
  src/core/credentials/transport/tls/grpc_tls_crl_provider.cc
  src/core/credentials/transport/tls/load_system_roots_fallback.cc
  src/core/credentials/transport/tls/load_system_roots_supported.cc
  src/core/credentials/transport/tls/load_system_roots_windows.cc
  src/core/credentials/transport/tls/spiffe_utils.cc
  src/core/credentials/transport/tls/ssl_utils.cc
  src/core/credentials/transport/tls/tls_credentials.cc
  src/core/credentials/transport/tls/tls_security_connector.cc
  src/core/credentials/transport/tls/tls_utils.cc
  src/core/credentials/transport/transport_credentials.cc
  src/core/credentials/transport/xds/xds_credentials.cc
  src/core/ext/filters/backend_metrics/backend_metric_filter.cc
  src/core/ext/filters/census/grpc_context.cc
  src/core/ext/filters/channel_idle/idle_filter_state.cc
  src/core/ext/filters/channel_idle/legacy_channel_idle_filter.cc
  src/core/ext/filters/fault_injection/fault_injection_filter.cc
  src/core/ext/filters/fault_injection/fault_injection_service_config_parser.cc
  src/core/ext/filters/gcp_authentication/gcp_authentication_filter.cc
  src/core/ext/filters/gcp_authentication/gcp_authentication_service_config_parser.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/client_authority_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/compression_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
  src/core/ext/filters/message_size/message_size_filter.cc
  src/core/ext/filters/rbac/rbac_filter.cc
  src/core/ext/filters/rbac/rbac_service_config_parser.cc
  src/core/ext/filters/stateful_session/stateful_session_filter.cc
  src/core/ext/filters/stateful_session/stateful_session_service_config_parser.cc
  src/core/ext/transport/chttp2/alpn/alpn.cc
  src/core/ext/transport/chttp2/chttp2_plugin.cc
  src/core/ext/transport/chttp2/client/chttp2_connector.cc
  src/core/ext/transport/chttp2/server/chttp2_server.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/call_tracer_wrapper.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
  src/core/ext/transport/chttp2/transport/decode_huff.cc
  src/core/ext/transport/chttp2/transport/flow_control.cc
  src/core/ext/transport/chttp2/transport/frame.cc
  src/core/ext/transport/chttp2/transport/frame_data.cc
  src/core/ext/transport/chttp2/transport/frame_goaway.cc
  src/core/ext/transport/chttp2/transport/frame_ping.cc
  src/core/ext/transport/chttp2/transport/frame_rst_stream.cc
  src/core/ext/transport/chttp2/transport/frame_security.cc
  src/core/ext/transport/chttp2/transport/frame_settings.cc
  src/core/ext/transport/chttp2/transport/frame_window_update.cc
  src/core/ext/transport/chttp2/transport/goaway.cc
  src/core/ext/transport/chttp2/transport/hpack_encoder.cc
  src/core/ext/transport/chttp2/transport/hpack_encoder_table.cc
  src/core/ext/transport/chttp2/transport/hpack_parse_result.cc
  src/core/ext/transport/chttp2/transport/hpack_parser.cc
  src/core/ext/transport/chttp2/transport/hpack_parser_table.cc
  src/core/ext/transport/chttp2/transport/http2_client_transport.cc
  src/core/ext/transport/chttp2/transport/http2_settings.cc
  src/core/ext/transport/chttp2/transport/http2_settings_manager.cc
  src/core/ext/transport/chttp2/transport/http2_stats_collector.cc
  src/core/ext/transport/chttp2/transport/http2_transport.cc
  src/core/ext/transport/chttp2/transport/huffsyms.cc
  src/core/ext/transport/chttp2/transport/keepalive.cc
  src/core/ext/transport/chttp2/transport/parsing.cc
  src/core/ext/transport/chttp2/transport/ping_abuse_policy.cc
  src/core/ext/transport/chttp2/transport/ping_callbacks.cc
  src/core/ext/transport/chttp2/transport/ping_promise.cc
  src/core/ext/transport/chttp2/transport/ping_rate_policy.cc
  src/core/ext/transport/chttp2/transport/stream_lists.cc
  src/core/ext/transport/chttp2/transport/transport_common.cc
  src/core/ext/transport/chttp2/transport/varint.cc
  src/core/ext/transport/chttp2/transport/write_cycle.cc
  src/core/ext/transport/chttp2/transport/write_size_policy.cc
  src/core/ext/transport/chttp2/transport/writing.cc
  src/core/ext/transport/inproc/inproc_transport.cc
  src/core/ext/transport/inproc/legacy_inproc_transport.cc
  src/core/ext/upb-gen/cel/expr/checked.upb_minitable.c
  src/core/ext/upb-gen/cel/expr/syntax.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/certs.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/clusters.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/config_dump.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/config_dump_shared.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/init_dump.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/listeners.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/memory.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/metrics.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/mutex_stats.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/server_info.upb_minitable.c
  src/core/ext/upb-gen/envoy/admin/v3/tap.upb_minitable.c
  src/core/ext/upb-gen/envoy/annotations/deprecation.upb_minitable.c
  src/core/ext/upb-gen/envoy/annotations/resource.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/accesslog/v3/accesslog.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/bootstrap/v3/bootstrap.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/cluster/v3/circuit_breaker.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/cluster/v3/cluster.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/cluster/v3/filter.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/cluster/v3/outlier_detection.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/common/matcher/v3/matcher.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/common/mutation_rules/v3/mutation_rules.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/address.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/backoff.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/base.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/cel.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/config_source.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/event_service_config.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/extension.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/grpc_method_list.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/grpc_service.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/health_check.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/http_service.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/http_uri.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/protocol.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/proxy_protocol.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/resolver.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/socket_cmsg_headers.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/socket_option.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/substitution_format_string.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/core/v3/udp_socket_config.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/endpoint/v3/endpoint.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/endpoint/v3/endpoint_components.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/endpoint/v3/load_report.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/listener/v3/api_listener.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/listener/v3/listener.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/listener/v3/listener_components.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/listener/v3/quic_config.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/listener/v3/udp_listener_config.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/metrics/v3/metrics_service.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/metrics/v3/stats.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/overload/v3/overload.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/rbac/v3/rbac.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/route/v3/route.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/route/v3/route_components.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/route/v3/scoped_route.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/tap/v3/common.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/datadog.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/dynamic_ot.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/http_tracer.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/lightstep.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/opentelemetry.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/service.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/skywalking.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/trace.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/xray.upb_minitable.c
  src/core/ext/upb-gen/envoy/config/trace/v3/zipkin.upb_minitable.c
  src/core/ext/upb-gen/envoy/data/accesslog/v3/accesslog.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/clusters/aggregate/v3/cluster.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/common/matching/v3/extension_matcher.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/common/fault/v3/fault.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/common/matcher/action/v3/skip_action.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/http/composite/v3/composite.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/http/fault/v3/fault.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/http/rbac/v3/rbac.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/http/router/v3/router.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/http/stateful_session/v3/stateful_session.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/grpc_service/call_credentials/access_token/v3/access_token_credentials.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/grpc_service/channel_credentials/tls/v3/tls_credentials.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/grpc_service/channel_credentials/xds/v3/xds_credentials.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/http/stateful_session/cookie/v3/cookie.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3/client_side_weighted_round_robin.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/common/v3/common.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/pick_first/v3/pick_first.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/ring_hash/v3/ring_hash.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/load_balancing_policies/wrr_locality/v3/wrr_locality.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/cert.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/common.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/secret.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/tls.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.upb_minitable.c
  src/core/ext/upb-gen/envoy/extensions/upstreams/http/v3/http_protocol_options.upb_minitable.c
  src/core/ext/upb-gen/envoy/service/discovery/v3/ads.upb_minitable.c
  src/core/ext/upb-gen/envoy/service/discovery/v3/discovery.upb_minitable.c
  src/core/ext/upb-gen/envoy/service/load_stats/v3/lrs.upb_minitable.c
  src/core/ext/upb-gen/envoy/service/status/v3/csds.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/http/v3/cookie.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/http/v3/path_transformation.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/address.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/filter_state.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/http_inputs.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/metadata.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/node.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/number.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/path.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/regex.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/status_code_input.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/string.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/struct.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/matcher/v3/value.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/metadata/v3/metadata.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/tracing/v3/custom_tag.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/hash_policy.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/http.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/http_status.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/percent.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/range.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/ratelimit_strategy.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/ratelimit_unit.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/semantic_version.upb_minitable.c
  src/core/ext/upb-gen/envoy/type/v3/token_bucket.upb_minitable.c
  src/core/ext/upb-gen/google/api/annotations.upb_minitable.c
  src/core/ext/upb-gen/google/api/expr/v1alpha1/checked.upb_minitable.c
  src/core/ext/upb-gen/google/api/expr/v1alpha1/syntax.upb_minitable.c
  src/core/ext/upb-gen/google/api/http.upb_minitable.c
  src/core/ext/upb-gen/google/api/httpbody.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/any.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/descriptor.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/duration.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/empty.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/struct.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/timestamp.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/wrappers.upb_minitable.c
  src/core/ext/upb-gen/google/rpc/status.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/channelz.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/channelz.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/promise.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/property_list.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/service.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/altscontext.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/handshaker.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/transport_security_common.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/health/v1/health.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/lb/v1/load_balancer.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/lookup/v1/rls.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/lookup/v1/rls_config.upb_minitable.c
  src/core/ext/upb-gen/udpa/annotations/migrate.upb_minitable.c
  src/core/ext/upb-gen/udpa/annotations/security.upb_minitable.c
  src/core/ext/upb-gen/udpa/annotations/sensitive.upb_minitable.c
  src/core/ext/upb-gen/udpa/annotations/status.upb_minitable.c
  src/core/ext/upb-gen/udpa/annotations/versioning.upb_minitable.c
  src/core/ext/upb-gen/validate/validate.upb_minitable.c
  src/core/ext/upb-gen/xds/annotations/v3/migrate.upb_minitable.c
  src/core/ext/upb-gen/xds/annotations/v3/security.upb_minitable.c
  src/core/ext/upb-gen/xds/annotations/v3/sensitive.upb_minitable.c
  src/core/ext/upb-gen/xds/annotations/v3/status.upb_minitable.c
  src/core/ext/upb-gen/xds/annotations/v3/versioning.upb_minitable.c
  src/core/ext/upb-gen/xds/core/v3/authority.upb_minitable.c
  src/core/ext/upb-gen/xds/core/v3/cidr.upb_minitable.c
  src/core/ext/upb-gen/xds/core/v3/collection_entry.upb_minitable.c
  src/core/ext/upb-gen/xds/core/v3/context_params.upb_minitable.c
  src/core/ext/upb-gen/xds/core/v3/extension.upb_minitable.c
  src/core/ext/upb-gen/xds/core/v3/resource.upb_minitable.c
  src/core/ext/upb-gen/xds/core/v3/resource_locator.upb_minitable.c
  src/core/ext/upb-gen/xds/core/v3/resource_name.upb_minitable.c
  src/core/ext/upb-gen/xds/data/orca/v3/orca_load_report.upb_minitable.c
  src/core/ext/upb-gen/xds/service/orca/v3/orca.upb_minitable.c
  src/core/ext/upb-gen/xds/type/matcher/v3/cel.upb_minitable.c
  src/core/ext/upb-gen/xds/type/matcher/v3/domain.upb_minitable.c
  src/core/ext/upb-gen/xds/type/matcher/v3/http_inputs.upb_minitable.c
  src/core/ext/upb-gen/xds/type/matcher/v3/ip.upb_minitable.c
  src/core/ext/upb-gen/xds/type/matcher/v3/matcher.upb_minitable.c
  src/core/ext/upb-gen/xds/type/matcher/v3/range.upb_minitable.c
  src/core/ext/upb-gen/xds/type/matcher/v3/regex.upb_minitable.c
  src/core/ext/upb-gen/xds/type/matcher/v3/string.upb_minitable.c
  src/core/ext/upb-gen/xds/type/v3/cel.upb_minitable.c
  src/core/ext/upb-gen/xds/type/v3/range.upb_minitable.c
  src/core/ext/upb-gen/xds/type/v3/typed_struct.upb_minitable.c
  src/core/ext/upbdefs-gen/cel/expr/checked.upbdefs.c
  src/core/ext/upbdefs-gen/cel/expr/syntax.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/certs.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/clusters.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/config_dump.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/config_dump_shared.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/init_dump.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/listeners.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/memory.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/metrics.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/mutex_stats.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/server_info.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/admin/v3/tap.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/annotations/deprecation.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/annotations/resource.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/accesslog/v3/accesslog.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/bootstrap/v3/bootstrap.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/cluster/v3/circuit_breaker.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/cluster/v3/cluster.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/cluster/v3/filter.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/cluster/v3/outlier_detection.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/common/matcher/v3/matcher.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/common/mutation_rules/v3/mutation_rules.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/address.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/backoff.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/base.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/cel.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/config_source.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/event_service_config.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/extension.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/grpc_method_list.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/grpc_service.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/health_check.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/http_service.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/http_uri.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/protocol.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/proxy_protocol.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/resolver.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/socket_cmsg_headers.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/socket_option.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/substitution_format_string.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/core/v3/udp_socket_config.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/endpoint/v3/endpoint.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/endpoint/v3/endpoint_components.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/endpoint/v3/load_report.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/listener/v3/api_listener.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/listener/v3/listener.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/listener/v3/listener_components.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/listener/v3/quic_config.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/listener/v3/udp_listener_config.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/metrics/v3/metrics_service.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/metrics/v3/stats.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/overload/v3/overload.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/rbac/v3/rbac.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/route/v3/route.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/route/v3/route_components.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/route/v3/scoped_route.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/tap/v3/common.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/datadog.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/dynamic_ot.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/http_tracer.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/lightstep.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/opentelemetry.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/service.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/skywalking.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/trace.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/xray.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/config/trace/v3/zipkin.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/data/accesslog/v3/accesslog.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/clusters/aggregate/v3/cluster.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/common/matching/v3/extension_matcher.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/common/fault/v3/fault.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/common/matcher/action/v3/skip_action.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/http/composite/v3/composite.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/http/fault/v3/fault.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/http/rbac/v3/rbac.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/http/router/v3/router.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/http/stateful_session/v3/stateful_session.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/http/stateful_session/cookie/v3/cookie.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/http_11_proxy/v3/upstream_http_11_connect.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/cert.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/common.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/secret.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/tls.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/transport_sockets/tls/v3/tls_spiffe_validator_config.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/extensions/upstreams/http/v3/http_protocol_options.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/service/discovery/v3/ads.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/service/discovery/v3/discovery.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/service/load_stats/v3/lrs.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/service/status/v3/csds.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/http/v3/cookie.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/http/v3/path_transformation.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/address.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/filter_state.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/http_inputs.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/metadata.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/node.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/number.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/path.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/regex.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/status_code_input.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/string.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/struct.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/matcher/v3/value.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/metadata/v3/metadata.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/tracing/v3/custom_tag.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/hash_policy.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/http.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/http_status.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/percent.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/range.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/ratelimit_strategy.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/ratelimit_unit.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/semantic_version.upbdefs.c
  src/core/ext/upbdefs-gen/envoy/type/v3/token_bucket.upbdefs.c
  src/core/ext/upbdefs-gen/google/api/annotations.upbdefs.c
  src/core/ext/upbdefs-gen/google/api/expr/v1alpha1/checked.upbdefs.c
  src/core/ext/upbdefs-gen/google/api/expr/v1alpha1/syntax.upbdefs.c
  src/core/ext/upbdefs-gen/google/api/http.upbdefs.c
  src/core/ext/upbdefs-gen/google/api/httpbody.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/any.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/descriptor.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/duration.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/empty.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/struct.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/timestamp.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/wrappers.upbdefs.c
  src/core/ext/upbdefs-gen/google/rpc/status.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/channelz.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/channelz.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/promise.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/property_list.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/service.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/lookup/v1/rls_config.upbdefs.c
  src/core/ext/upbdefs-gen/udpa/annotations/migrate.upbdefs.c
  src/core/ext/upbdefs-gen/udpa/annotations/security.upbdefs.c
  src/core/ext/upbdefs-gen/udpa/annotations/sensitive.upbdefs.c
  src/core/ext/upbdefs-gen/udpa/annotations/status.upbdefs.c
  src/core/ext/upbdefs-gen/udpa/annotations/versioning.upbdefs.c
  src/core/ext/upbdefs-gen/validate/validate.upbdefs.c
  src/core/ext/upbdefs-gen/xds/annotations/v3/migrate.upbdefs.c
  src/core/ext/upbdefs-gen/xds/annotations/v3/security.upbdefs.c
  src/core/ext/upbdefs-gen/xds/annotations/v3/sensitive.upbdefs.c
  src/core/ext/upbdefs-gen/xds/annotations/v3/status.upbdefs.c
  src/core/ext/upbdefs-gen/xds/annotations/v3/versioning.upbdefs.c
  src/core/ext/upbdefs-gen/xds/core/v3/authority.upbdefs.c
  src/core/ext/upbdefs-gen/xds/core/v3/cidr.upbdefs.c
  src/core/ext/upbdefs-gen/xds/core/v3/collection_entry.upbdefs.c
  src/core/ext/upbdefs-gen/xds/core/v3/context_params.upbdefs.c
  src/core/ext/upbdefs-gen/xds/core/v3/extension.upbdefs.c
  src/core/ext/upbdefs-gen/xds/core/v3/resource.upbdefs.c
  src/core/ext/upbdefs-gen/xds/core/v3/resource_locator.upbdefs.c
  src/core/ext/upbdefs-gen/xds/core/v3/resource_name.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/matcher/v3/cel.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/matcher/v3/domain.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/matcher/v3/http_inputs.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/matcher/v3/ip.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/matcher/v3/matcher.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/matcher/v3/range.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/matcher/v3/regex.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/matcher/v3/string.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/v3/cel.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/v3/range.upbdefs.c
  src/core/ext/upbdefs-gen/xds/type/v3/typed_struct.upbdefs.c
  src/core/filter/auth/client_auth_filter.cc
  src/core/filter/auth/server_auth_filter.cc
  src/core/filter/blackboard.cc
  src/core/filter/composite/composite_filter.cc
  src/core/filter/fused_filters.cc
  src/core/handshaker/endpoint_info/endpoint_info_handshaker.cc
  src/core/handshaker/handshaker.cc
  src/core/handshaker/handshaker_registry.cc
  src/core/handshaker/http_connect/http_connect_client_handshaker.cc
  src/core/handshaker/http_connect/http_proxy_mapper.cc
  src/core/handshaker/http_connect/xds_http_proxy_mapper.cc
  src/core/handshaker/proxy_mapper_registry.cc
  src/core/handshaker/security/pipelined_secure_endpoint.cc
  src/core/handshaker/security/secure_endpoint.cc
  src/core/handshaker/security/security_handshaker.cc
  src/core/handshaker/tcp_connect/tcp_connect_handshaker.cc
  src/core/lib/address_utils/parse_address.cc
  src/core/lib/address_utils/sockaddr_utils.cc
  src/core/lib/channel/channel_args.cc
  src/core/lib/channel/channel_args_preconditioning.cc
  src/core/lib/channel/channel_stack.cc
  src/core/lib/channel/channel_stack_builder.cc
  src/core/lib/channel/channel_stack_builder_impl.cc
  src/core/lib/channel/connected_channel.cc
  src/core/lib/channel/promise_based_filter.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/debug/trace.cc
  src/core/lib/debug/trace_flags.cc
  src/core/lib/event_engine/ares_resolver.cc
  src/core/lib/event_engine/cf_engine/cf_engine.cc
  src/core/lib/event_engine/cf_engine/cfsocket_listener.cc
  src/core/lib/event_engine/cf_engine/cfstream_endpoint.cc
  src/core/lib/event_engine/cf_engine/dns_service_resolver.cc
  src/core/lib/event_engine/channel_args_endpoint_config.cc
  src/core/lib/event_engine/default_event_engine.cc
  src/core/lib/event_engine/default_event_engine_factory.cc
  src/core/lib/event_engine/endpoint_channel_arg_wrapper.cc
  src/core/lib/event_engine/event_engine.cc
  src/core/lib/event_engine/posix_engine/ev_epoll1_linux.cc
  src/core/lib/event_engine/posix_engine/ev_poll_posix.cc
  src/core/lib/event_engine/posix_engine/event_poller_posix_default.cc
  src/core/lib/event_engine/posix_engine/file_descriptor_collection.cc
  src/core/lib/event_engine/posix_engine/internal_errqueue.cc
  src/core/lib/event_engine/posix_engine/lockfree_event.cc
  src/core/lib/event_engine/posix_engine/native_posix_dns_resolver.cc
  src/core/lib/event_engine/posix_engine/posix_endpoint.cc
  src/core/lib/event_engine/posix_engine/posix_engine.cc
  src/core/lib/event_engine/posix_engine/posix_engine_listener.cc
  src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.cc
  src/core/lib/event_engine/posix_engine/posix_interface_posix.cc
  src/core/lib/event_engine/posix_engine/posix_interface_windows.cc
  src/core/lib/event_engine/posix_engine/posix_write_event_sink.cc
  src/core/lib/event_engine/posix_engine/set_socket_dualstack.cc
  src/core/lib/event_engine/posix_engine/tcp_socket_utils.cc
  src/core/lib/event_engine/posix_engine/timer.cc
  src/core/lib/event_engine/posix_engine/timer_heap.cc
  src/core/lib/event_engine/posix_engine/timer_manager.cc
  src/core/lib/event_engine/posix_engine/traced_buffer_list.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.cc
  src/core/lib/event_engine/resolved_address.cc
  src/core/lib/event_engine/shim.cc
  src/core/lib/event_engine/slice.cc
  src/core/lib/event_engine/slice_buffer.cc
  src/core/lib/event_engine/tcp_socket_utils.cc
  src/core/lib/event_engine/thread_pool/thread_count.cc
  src/core/lib/event_engine/thread_pool/thread_pool_factory.cc
  src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.cc
  src/core/lib/event_engine/thready_event_engine/thready_event_engine.cc
  src/core/lib/event_engine/time_util.cc
  src/core/lib/event_engine/utils.cc
  src/core/lib/event_engine/windows/grpc_polled_fd_windows.cc
  src/core/lib/event_engine/windows/iocp.cc
  src/core/lib/event_engine/windows/native_windows_dns_resolver.cc
  src/core/lib/event_engine/windows/win_socket.cc
  src/core/lib/event_engine/windows/windows_endpoint.cc
  src/core/lib/event_engine/windows/windows_engine.cc
  src/core/lib/event_engine/windows/windows_listener.cc
  src/core/lib/event_engine/work_queue/basic_work_queue.cc
  src/core/lib/experiments/config.cc
  src/core/lib/experiments/experiments.cc
  src/core/lib/iomgr/buffer_list.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/cfstream_handle.cc
  src/core/lib/iomgr/closure.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/dualstack_socket_posix.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_cfstream.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/error_cfstream.cc
  src/core/lib/iomgr/ev_apple.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/event_engine_shims/closure.cc
  src/core/lib/iomgr/event_engine_shims/endpoint.cc
  src/core/lib/iomgr/event_engine_shims/tcp_client.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/internal_errqueue.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_posix_cfstream.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/polling_entity.cc
  src/core/lib/iomgr/pollset.cc
  src/core/lib/iomgr/pollset_set.cc
  src/core/lib/iomgr/pollset_set_windows.cc
  src/core/lib/iomgr/pollset_windows.cc
  src/core/lib/iomgr/resolve_address.cc
  src/core/lib/iomgr/resolve_address_posix.cc
  src/core/lib/iomgr/resolve_address_windows.cc
  src/core/lib/iomgr/sockaddr_utils_posix.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/systemd_utils.cc
  src/core/lib/iomgr/tcp_client.cc
  src/core/lib/iomgr/tcp_client_cfstream.cc
  src/core/lib/iomgr/tcp_client_posix.cc
  src/core/lib/iomgr/tcp_client_windows.cc
  src/core/lib/iomgr/tcp_posix.cc
  src/core/lib/iomgr/tcp_server.cc
  src/core/lib/iomgr/tcp_server_posix.cc
  src/core/lib/iomgr/tcp_server_utils_posix_common.cc
  src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc
  src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc
  src/core/lib/iomgr/tcp_server_windows.cc
  src/core/lib/iomgr/tcp_windows.cc
  src/core/lib/iomgr/timer.cc
  src/core/lib/iomgr/timer_generic.cc
  src/core/lib/iomgr/timer_heap.cc
  src/core/lib/iomgr/timer_manager.cc
  src/core/lib/iomgr/unix_sockets_posix.cc
  src/core/lib/iomgr/unix_sockets_posix_noop.cc
  src/core/lib/iomgr/vsock.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/promise/activity.cc
  src/core/lib/promise/mpsc.cc
  src/core/lib/promise/party.cc
  src/core/lib/promise/sleep.cc
  src/core/lib/promise/wait_set.cc
  src/core/lib/resource_quota/api.cc
  src/core/lib/resource_quota/arena.cc
  src/core/lib/resource_quota/connection_quota.cc
  src/core/lib/resource_quota/memory_quota.cc
  src/core/lib/resource_quota/periodic_update.cc
  src/core/lib/resource_quota/resource_quota.cc
  src/core/lib/resource_quota/stream_quota.cc
  src/core/lib/resource_quota/thread_quota.cc
  src/core/lib/resource_tracker/resource_tracker.cc
  src/core/lib/security/authorization/audit_logging.cc
  src/core/lib/security/authorization/authorization_policy_provider_vtable.cc
  src/core/lib/security/authorization/evaluate_args.cc
  src/core/lib/security/authorization/grpc_authorization_engine.cc
  src/core/lib/security/authorization/grpc_server_authz_filter.cc
  src/core/lib/security/authorization/matchers.cc
  src/core/lib/security/authorization/rbac_policy.cc
  src/core/lib/security/authorization/stdout_logger.cc
  src/core/lib/slice/percent_encoding.cc
  src/core/lib/slice/slice.cc
  src/core/lib/slice/slice_buffer.cc
  src/core/lib/slice/slice_string_helpers.cc
  src/core/lib/surface/byte_buffer.cc
  src/core/lib/surface/byte_buffer_reader.cc
  src/core/lib/surface/call.cc
  src/core/lib/surface/call_details.cc
  src/core/lib/surface/call_log_batch.cc
  src/core/lib/surface/call_utils.cc
  src/core/lib/surface/channel.cc
  src/core/lib/surface/channel_create.cc
  src/core/lib/surface/channel_init.cc
  src/core/lib/surface/channel_stack_type.cc
  src/core/lib/surface/completion_queue.cc
  src/core/lib/surface/completion_queue_factory.cc
  src/core/lib/surface/connection_context.cc
  src/core/lib/surface/event_string.cc
  src/core/lib/surface/filter_stack_call.cc
  src/core/lib/surface/init.cc
  src/core/lib/surface/init_internally.cc
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/legacy_channel.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/call_final_info.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/promise_endpoint.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/load_balancing/address_filtering.cc
  src/core/load_balancing/backend_metric_parser.cc
  src/core/load_balancing/child_policy_handler.cc
  src/core/load_balancing/endpoint_list.cc
  src/core/load_balancing/grpclb/client_load_reporting_filter.cc
  src/core/load_balancing/grpclb/grpclb.cc
  src/core/load_balancing/grpclb/grpclb_balancer_addresses.cc
  src/core/load_balancing/grpclb/grpclb_client_stats.cc
  src/core/load_balancing/grpclb/load_balancer_api.cc
  src/core/load_balancing/health_check_client.cc
  src/core/load_balancing/lb_policy.cc
  src/core/load_balancing/lb_policy_registry.cc
  src/core/load_balancing/oob_backend_metric.cc
  src/core/load_balancing/outlier_detection/outlier_detection.cc
  src/core/load_balancing/pick_first/pick_first.cc
  src/core/load_balancing/priority/priority.cc
  src/core/load_balancing/ring_hash/ring_hash.cc
  src/core/load_balancing/rls/rls.cc
  src/core/load_balancing/round_robin/round_robin.cc
  src/core/load_balancing/weighted_round_robin/static_stride_scheduler.cc
  src/core/load_balancing/weighted_round_robin/weighted_round_robin.cc
  src/core/load_balancing/weighted_target/weighted_target.cc
  src/core/load_balancing/xds/cds.cc
  src/core/load_balancing/xds/xds_cluster_impl.cc
  src/core/load_balancing/xds/xds_cluster_manager.cc
  src/core/load_balancing/xds/xds_override_host.cc
  src/core/load_balancing/xds/xds_wrr_locality.cc
  src/core/net/socket_mutator.cc
  src/core/plugin_registry/grpc_plugin_registry.cc
  src/core/plugin_registry/grpc_plugin_registry_extra.cc
  src/core/resolver/dns/c_ares/dns_resolver_ares.cc
  src/core/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc
  src/core/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc
  src/core/resolver/dns/c_ares/grpc_ares_wrapper.cc
  src/core/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc
  src/core/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc
  src/core/resolver/dns/dns_resolver_plugin.cc
  src/core/resolver/dns/event_engine/event_engine_client_channel_resolver.cc
  src/core/resolver/dns/event_engine/service_config_helper.cc
  src/core/resolver/dns/native/dns_resolver.cc
  src/core/resolver/endpoint_addresses.cc
  src/core/resolver/fake/fake_resolver.cc
  src/core/resolver/google_c2p/google_c2p_resolver.cc
  src/core/resolver/polling_resolver.cc
  src/core/resolver/resolver.cc
  src/core/resolver/resolver_registry.cc
  src/core/resolver/sockaddr/sockaddr_resolver.cc
  src/core/resolver/xds/xds_config.cc
  src/core/resolver/xds/xds_dependency_manager.cc
  src/core/resolver/xds/xds_resolver.cc
  src/core/server/add_port.cc
  src/core/server/server.cc
  src/core/server/server_call_tracer_filter.cc
  src/core/server/server_config_selector_filter.cc
  src/core/server/xds_channel_stack_modifier.cc
  src/core/server/xds_server_config_fetcher.cc
  src/core/service_config/service_config_channel_arg_filter.cc
  src/core/service_config/service_config_impl.cc
  src/core/service_config/service_config_parser.cc
  src/core/telemetry/call_tracer.cc
  src/core/telemetry/context_list_entry.cc
  src/core/telemetry/default_tcp_tracer.cc
  src/core/telemetry/histogram_view.cc
  src/core/telemetry/instrument.cc
  src/core/telemetry/metrics.cc
  src/core/telemetry/stats.cc
  src/core/telemetry/stats_data.cc
  src/core/telemetry/tcp_tracer.cc
  src/core/transport/auth_context.cc
  src/core/transport/endpoint_transport_client_channel_factory.cc
  src/core/tsi/alts/crypt/aes_gcm.cc
  src/core/tsi/alts/crypt/gsec.cc
  src/core/tsi/alts/frame_protector/alts_counter.cc
  src/core/tsi/alts/frame_protector/alts_crypter.cc
  src/core/tsi/alts/frame_protector/alts_frame_protector.cc
  src/core/tsi/alts/frame_protector/alts_record_protocol_crypter_common.cc
  src/core/tsi/alts/frame_protector/alts_seal_privacy_integrity_crypter.cc
  src/core/tsi/alts/frame_protector/alts_unseal_privacy_integrity_crypter.cc
  src/core/tsi/alts/frame_protector/frame_handler.cc
  src/core/tsi/alts/handshaker/alts_handshaker_client.cc
  src/core/tsi/alts/handshaker/alts_shared_resource.cc
  src/core/tsi/alts/handshaker/alts_tsi_handshaker.cc
  src/core/tsi/alts/handshaker/alts_tsi_utils.cc
  src/core/tsi/alts/handshaker/transport_security_common_api.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_integrity_only_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_privacy_integrity_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_grpc_record_protocol_common.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_iovec_record_protocol.cc
  src/core/tsi/alts/zero_copy_frame_protector/alts_zero_copy_grpc_protector.cc
  src/core/tsi/fake_transport_security.cc
  src/core/tsi/local_transport_security.cc
  src/core/tsi/ssl/key_logging/ssl_key_logging.cc
  src/core/tsi/ssl/session_cache/ssl_session_boringssl.cc
  src/core/tsi/ssl/session_cache/ssl_session_cache.cc
  src/core/tsi/ssl/session_cache/ssl_session_openssl.cc
  src/core/tsi/ssl_transport_security.cc
  src/core/tsi/ssl_transport_security_utils.cc
  src/core/tsi/transport_security.cc
  src/core/tsi/transport_security_grpc.cc
  src/core/util/backoff.cc
  src/core/util/dump_args.cc
  src/core/util/event_log.cc
  src/core/util/gcp_metadata_query.cc
  src/core/util/gethostname_fallback.cc
  src/core/util/gethostname_host_name_max.cc
  src/core/util/gethostname_sysconf.cc
  src/core/util/glob.cc
  src/core/util/grpc_check.cc
  src/core/util/grpc_if_nametoindex_posix.cc
  src/core/util/grpc_if_nametoindex_unsupported.cc
  src/core/util/http_client/format_request.cc
  src/core/util/http_client/httpcli.cc
  src/core/util/http_client/httpcli_security_connector.cc
  src/core/util/http_client/parser.cc
  src/core/util/json/json_object_loader.cc
  src/core/util/json/json_reader.cc
  src/core/util/json/json_util.cc
  src/core/util/json/json_writer.cc
  src/core/util/latent_see.cc
  src/core/util/load_file.cc
  src/core/util/matchers.cc
  src/core/util/per_cpu.cc
  src/core/util/posix/directory_reader.cc
  src/core/util/postmortem_emit.cc
  src/core/util/random_early_detection.cc
  src/core/util/ref_counted_string.cc
  src/core/util/shared_bit_gen.cc
  src/core/util/status_helper.cc
  src/core/util/time.cc
  src/core/util/time_averaged_stats.cc
  src/core/util/uri.cc
  src/core/util/uuid_v4.cc
  src/core/util/validation_errors.cc
  src/core/util/wait_for_single_owner.cc
  src/core/util/windows/directory_reader.cc
  src/core/util/work_serializer.cc
  src/core/xds/grpc/certificate_provider_store.cc
  src/core/xds/grpc/file_watcher_certificate_provider_factory.cc
  src/core/xds/grpc/xds_audit_logger_registry.cc
  src/core/xds/grpc/xds_bootstrap_grpc.cc
  src/core/xds/grpc/xds_certificate_provider.cc
  src/core/xds/grpc/xds_client_grpc.cc
  src/core/xds/grpc/xds_cluster.cc
  src/core/xds/grpc/xds_cluster_parser.cc
  src/core/xds/grpc/xds_cluster_specifier_plugin.cc
  src/core/xds/grpc/xds_common_types.cc
  src/core/xds/grpc/xds_common_types_parser.cc
  src/core/xds/grpc/xds_endpoint.cc
  src/core/xds/grpc/xds_endpoint_parser.cc
  src/core/xds/grpc/xds_health_status.cc
  src/core/xds/grpc/xds_http_composite_filter.cc
  src/core/xds/grpc/xds_http_fault_filter.cc
  src/core/xds/grpc/xds_http_filter.cc
  src/core/xds/grpc/xds_http_filter_registry.cc
  src/core/xds/grpc/xds_http_gcp_authn_filter.cc
  src/core/xds/grpc/xds_http_rbac_filter.cc
  src/core/xds/grpc/xds_http_stateful_session_filter.cc
  src/core/xds/grpc/xds_lb_policy_registry.cc
  src/core/xds/grpc/xds_listener.cc
  src/core/xds/grpc/xds_listener_parser.cc
  src/core/xds/grpc/xds_matcher.cc
  src/core/xds/grpc/xds_matcher_action.cc
  src/core/xds/grpc/xds_matcher_context.cc
  src/core/xds/grpc/xds_matcher_input.cc
  src/core/xds/grpc/xds_matcher_parse.cc
  src/core/xds/grpc/xds_metadata.cc
  src/core/xds/grpc/xds_metadata_parser.cc
  src/core/xds/grpc/xds_route_config.cc
  src/core/xds/grpc/xds_route_config_parser.cc
  src/core/xds/grpc/xds_routing.cc
  src/core/xds/grpc/xds_server_grpc.cc
  src/core/xds/grpc/xds_transport_grpc.cc
  src/core/xds/xds_client/lrs_client.cc
  src/core/xds/xds_client/xds_api.cc
  src/core/xds/xds_client/xds_backend_metric_propagation.cc
  src/core/xds/xds_client/xds_bootstrap.cc
  src/core/xds/xds_client/xds_client.cc
)

target_compile_features(grpc PUBLIC cxx_std_17)

set_target_properties(grpc PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc PROPERTIES COMPILE_PDB_NAME "grpc"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc
    PRIVATE
      "GRPC_DLL_EXPORTS"
      "GPR_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_json_lib
  upb_textformat_lib
  ${_gRPC_RE2_LIBRARIES}
  ${_gRPC_ZLIB_LIBRARIES}
  absl::algorithm_container
  absl::no_destructor
  absl::btree
  absl::flat_hash_map
  absl::flat_hash_set
  absl::inlined_vector
  absl::node_hash_map
  absl::function_ref
  absl::hash
  absl::type_traits
  absl::random_bit_gen_ref
  absl::random_distributions
  absl::statusor
  absl::string_view
  absl::span
  absl::utility
  ${_gRPC_CARES_LIBRARIES}
  gpr
  ${_gRPC_SSL_LIBRARIES}
  ${_gRPC_ADDRESS_SORTING_LIBRARIES}
)
if(_gRPC_PLATFORM_IOS OR _gRPC_PLATFORM_MAC)
  target_link_libraries(grpc "-framework CoreFoundation")
endif()

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/census.h
  include/grpc/compression.h
  include/grpc/create_channel_from_endpoint.h
  include/grpc/credentials.h
  include/grpc/credentials_cpp.h
  include/grpc/event_engine/endpoint_config.h
  include/grpc/event_engine/event_engine.h
  include/grpc/event_engine/extensible.h
  include/grpc/event_engine/internal/memory_allocator_impl.h
  include/grpc/event_engine/internal/slice_cast.h
  include/grpc/event_engine/internal/write_event.h
  include/grpc/event_engine/memory_allocator.h
  include/grpc/event_engine/memory_request.h
  include/grpc/event_engine/port.h
  include/grpc/event_engine/slice.h
  include/grpc/event_engine/slice_buffer.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_audit_logging.h
  include/grpc/grpc_crl_provider.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security.h
  include/grpc/grpc_security_constants.h
  include/grpc/impl/call.h
  include/grpc/impl/channel_arg_names.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/compression_types.h
  include/grpc/impl/connectivity_state.h
  include/grpc/impl/grpc_types.h
  include/grpc/impl/propagation_bits.h
  include/grpc/impl/slice_type.h
  include/grpc/load_reporting.h
  include/grpc/passive_listener.h
  include/grpc/private_key_signer.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if(gRPC_BUILD_TESTS)

add_library(grpc_test_util
  test/core/event_engine/test_init.cc
  test/core/test_util/build.cc
  test/core/test_util/cmdline.cc
  test/core/test_util/grpc_profiler.cc
  test/core/test_util/histogram.cc
  test/core/test_util/mock_endpoint.cc
  test/core/test_util/parse_hexstring.cc
  test/core/test_util/port.cc
  test/core/test_util/port_ci.cc
  test/core/test_util/port_server_client.cc
  test/core/test_util/reconnect_server.cc
  test/core/test_util/resolve_localhost_ip46.cc
  test/core/test_util/slice_splitter.cc
  test/core/test_util/stack_tracer.cc
  test/core/test_util/test_config.cc
  test/core/test_util/test_tcp_server.cc
  test/core/test_util/tls_utils.cc
  test/core/test_util/tracer_util.cc
)

target_compile_features(grpc_test_util PUBLIC cxx_std_17)

set_target_properties(grpc_test_util PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_test_util PROPERTIES COMPILE_PDB_NAME "grpc_test_util"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc_test_util
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_test_util.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_test_util
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc_test_util
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::failure_signal_handler
  absl::stacktrace
  absl::symbolize
  absl::log_initialize
  grpc
)
if(_gRPC_PLATFORM_IOS OR _gRPC_PLATFORM_MAC)
  target_link_libraries(grpc_test_util "-framework CoreFoundation")
endif()


endif()
if(gRPC_BUILD_TESTS)

add_library(grpc_test_util_unsecure
  test/core/event_engine/test_init.cc
  test/core/test_util/build.cc
  test/core/test_util/cmdline.cc
  test/core/test_util/grpc_profiler.cc
  test/core/test_util/histogram.cc
  test/core/test_util/mock_endpoint.cc
  test/core/test_util/parse_hexstring.cc
  test/core/test_util/port.cc
  test/core/test_util/port_ci.cc
  test/core/test_util/port_server_client.cc
  test/core/test_util/reconnect_server.cc
  test/core/test_util/resolve_localhost_ip46.cc
  test/core/test_util/slice_splitter.cc
  test/core/test_util/stack_tracer.cc
  test/core/test_util/test_config.cc
  test/core/test_util/test_tcp_server.cc
  test/core/test_util/tracer_util.cc
)

target_compile_features(grpc_test_util_unsecure PUBLIC cxx_std_17)

set_target_properties(grpc_test_util_unsecure PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_test_util_unsecure PROPERTIES COMPILE_PDB_NAME "grpc_test_util_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc_test_util_unsecure
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_test_util_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_test_util_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc_test_util_unsecure
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::failure_signal_handler
  absl::stacktrace
  absl::symbolize
  absl::log_initialize
  grpc_unsecure
)
if(_gRPC_PLATFORM_IOS OR _gRPC_PLATFORM_MAC)
  target_link_libraries(grpc_test_util_unsecure "-framework CoreFoundation")
endif()


endif()

add_library(grpc_unsecure
  src/core/call/call_arena_allocator.cc
  src/core/call/call_filters.cc
  src/core/call/call_spine.cc
  src/core/call/call_state.cc
  src/core/call/client_call.cc
  src/core/call/interception_chain.cc
  src/core/call/message.cc
  src/core/call/metadata.cc
  src/core/call/metadata_batch.cc
  src/core/call/metadata_info.cc
  src/core/call/parsed_metadata.cc
  src/core/call/request_buffer.cc
  src/core/call/security_context.cc
  src/core/call/server_call.cc
  src/core/call/status_util.cc
  src/core/channelz/channel_trace.cc
  src/core/channelz/channelz.cc
  src/core/channelz/channelz_registry.cc
  src/core/channelz/property_list.cc
  src/core/channelz/text_encode.cc
  src/core/client_channel/backup_poller.cc
  src/core/client_channel/buffered_call.cc
  src/core/client_channel/client_channel.cc
  src/core/client_channel/client_channel_factory.cc
  src/core/client_channel/client_channel_filter.cc
  src/core/client_channel/client_channel_plugin.cc
  src/core/client_channel/client_channel_service_config.cc
  src/core/client_channel/direct_channel.cc
  src/core/client_channel/dynamic_filters.cc
  src/core/client_channel/global_subchannel_pool.cc
  src/core/client_channel/lb_metadata.cc
  src/core/client_channel/load_balanced_call_destination.cc
  src/core/client_channel/local_subchannel_pool.cc
  src/core/client_channel/retry_filter.cc
  src/core/client_channel/retry_filter_legacy_call_data.cc
  src/core/client_channel/retry_interceptor.cc
  src/core/client_channel/retry_service_config.cc
  src/core/client_channel/retry_throttle.cc
  src/core/client_channel/subchannel.cc
  src/core/client_channel/subchannel_pool_interface.cc
  src/core/client_channel/subchannel_stream_client.cc
  src/core/client_channel/subchannel_stream_limiter.cc
  src/core/config/core_configuration.cc
  src/core/credentials/call/call_creds_util.cc
  src/core/credentials/call/composite/composite_call_credentials.cc
  src/core/credentials/call/json_util.cc
  src/core/credentials/call/plugin/plugin_credentials.cc
  src/core/credentials/transport/alts/check_gcp_environment.cc
  src/core/credentials/transport/alts/check_gcp_environment_linux.cc
  src/core/credentials/transport/alts/check_gcp_environment_no_op.cc
  src/core/credentials/transport/alts/check_gcp_environment_windows.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_client_options.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_options.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_server_options.cc
  src/core/credentials/transport/composite/composite_channel_credentials.cc
  src/core/credentials/transport/fake/fake_credentials.cc
  src/core/credentials/transport/fake/fake_security_connector.cc
  src/core/credentials/transport/insecure/insecure_credentials.cc
  src/core/credentials/transport/insecure/insecure_security_connector.cc
  src/core/credentials/transport/security_connector.cc
  src/core/credentials/transport/tls/certificate_provider_registry.cc
  src/core/credentials/transport/tls/load_system_roots_fallback.cc
  src/core/credentials/transport/tls/load_system_roots_supported.cc
  src/core/credentials/transport/tls/load_system_roots_windows.cc
  src/core/credentials/transport/tls/tls_utils.cc
  src/core/credentials/transport/transport_credentials.cc
  src/core/ext/filters/backend_metrics/backend_metric_filter.cc
  src/core/ext/filters/census/grpc_context.cc
  src/core/ext/filters/channel_idle/idle_filter_state.cc
  src/core/ext/filters/channel_idle/legacy_channel_idle_filter.cc
  src/core/ext/filters/fault_injection/fault_injection_filter.cc
  src/core/ext/filters/fault_injection/fault_injection_service_config_parser.cc
  src/core/ext/filters/http/client/http_client_filter.cc
  src/core/ext/filters/http/client_authority_filter.cc
  src/core/ext/filters/http/http_filters_plugin.cc
  src/core/ext/filters/http/message_compress/compression_filter.cc
  src/core/ext/filters/http/server/http_server_filter.cc
  src/core/ext/filters/message_size/message_size_filter.cc
  src/core/ext/transport/chttp2/chttp2_plugin.cc
  src/core/ext/transport/chttp2/client/chttp2_connector.cc
  src/core/ext/transport/chttp2/server/chttp2_server.cc
  src/core/ext/transport/chttp2/transport/bin_decoder.cc
  src/core/ext/transport/chttp2/transport/bin_encoder.cc
  src/core/ext/transport/chttp2/transport/call_tracer_wrapper.cc
  src/core/ext/transport/chttp2/transport/chttp2_transport.cc
  src/core/ext/transport/chttp2/transport/decode_huff.cc
  src/core/ext/transport/chttp2/transport/flow_control.cc
  src/core/ext/transport/chttp2/transport/frame.cc
  src/core/ext/transport/chttp2/transport/frame_data.cc
  src/core/ext/transport/chttp2/transport/frame_goaway.cc
  src/core/ext/transport/chttp2/transport/frame_ping.cc
  src/core/ext/transport/chttp2/transport/frame_rst_stream.cc
  src/core/ext/transport/chttp2/transport/frame_security.cc
  src/core/ext/transport/chttp2/transport/frame_settings.cc
  src/core/ext/transport/chttp2/transport/frame_window_update.cc
  src/core/ext/transport/chttp2/transport/goaway.cc
  src/core/ext/transport/chttp2/transport/hpack_encoder.cc
  src/core/ext/transport/chttp2/transport/hpack_encoder_table.cc
  src/core/ext/transport/chttp2/transport/hpack_parse_result.cc
  src/core/ext/transport/chttp2/transport/hpack_parser.cc
  src/core/ext/transport/chttp2/transport/hpack_parser_table.cc
  src/core/ext/transport/chttp2/transport/http2_client_transport.cc
  src/core/ext/transport/chttp2/transport/http2_settings.cc
  src/core/ext/transport/chttp2/transport/http2_settings_manager.cc
  src/core/ext/transport/chttp2/transport/http2_stats_collector.cc
  src/core/ext/transport/chttp2/transport/http2_transport.cc
  src/core/ext/transport/chttp2/transport/huffsyms.cc
  src/core/ext/transport/chttp2/transport/keepalive.cc
  src/core/ext/transport/chttp2/transport/parsing.cc
  src/core/ext/transport/chttp2/transport/ping_abuse_policy.cc
  src/core/ext/transport/chttp2/transport/ping_callbacks.cc
  src/core/ext/transport/chttp2/transport/ping_promise.cc
  src/core/ext/transport/chttp2/transport/ping_rate_policy.cc
  src/core/ext/transport/chttp2/transport/stream_lists.cc
  src/core/ext/transport/chttp2/transport/transport_common.cc
  src/core/ext/transport/chttp2/transport/varint.cc
  src/core/ext/transport/chttp2/transport/write_cycle.cc
  src/core/ext/transport/chttp2/transport/write_size_policy.cc
  src/core/ext/transport/chttp2/transport/writing.cc
  src/core/ext/transport/inproc/inproc_transport.cc
  src/core/ext/transport/inproc/legacy_inproc_transport.cc
  src/core/ext/upb-gen/google/api/http.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/any.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/descriptor.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/duration.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/empty.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/struct.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/timestamp.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/wrappers.upb_minitable.c
  src/core/ext/upb-gen/google/rpc/status.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/channelz.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/promise.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/property_list.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/service.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/altscontext.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/handshaker.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/transport_security_common.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/health/v1/health.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/lb/v1/load_balancer.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/lookup/v1/rls.upb_minitable.c
  src/core/ext/upb-gen/validate/validate.upb_minitable.c
  src/core/ext/upb-gen/xds/data/orca/v3/orca_load_report.upb_minitable.c
  src/core/ext/upb-gen/xds/service/orca/v3/orca.upb_minitable.c
  src/core/ext/upbdefs-gen/google/protobuf/any.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/duration.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/empty.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/timestamp.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/channelz.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/promise.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/property_list.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/service.upbdefs.c
  src/core/filter/auth/client_auth_filter.cc
  src/core/filter/auth/server_auth_filter.cc
  src/core/filter/blackboard.cc
  src/core/filter/fused_filters.cc
  src/core/handshaker/endpoint_info/endpoint_info_handshaker.cc
  src/core/handshaker/handshaker.cc
  src/core/handshaker/handshaker_registry.cc
  src/core/handshaker/http_connect/http_connect_client_handshaker.cc
  src/core/handshaker/http_connect/http_proxy_mapper.cc
  src/core/handshaker/proxy_mapper_registry.cc
  src/core/handshaker/security/pipelined_secure_endpoint.cc
  src/core/handshaker/security/secure_endpoint.cc
  src/core/handshaker/security/security_handshaker.cc
  src/core/handshaker/tcp_connect/tcp_connect_handshaker.cc
  src/core/lib/address_utils/parse_address.cc
  src/core/lib/address_utils/sockaddr_utils.cc
  src/core/lib/channel/channel_args.cc
  src/core/lib/channel/channel_args_preconditioning.cc
  src/core/lib/channel/channel_stack.cc
  src/core/lib/channel/channel_stack_builder.cc
  src/core/lib/channel/channel_stack_builder_impl.cc
  src/core/lib/channel/connected_channel.cc
  src/core/lib/channel/promise_based_filter.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/debug/trace.cc
  src/core/lib/debug/trace_flags.cc
  src/core/lib/event_engine/ares_resolver.cc
  src/core/lib/event_engine/cf_engine/cf_engine.cc
  src/core/lib/event_engine/cf_engine/cfsocket_listener.cc
  src/core/lib/event_engine/cf_engine/cfstream_endpoint.cc
  src/core/lib/event_engine/cf_engine/dns_service_resolver.cc
  src/core/lib/event_engine/channel_args_endpoint_config.cc
  src/core/lib/event_engine/default_event_engine.cc
  src/core/lib/event_engine/default_event_engine_factory.cc
  src/core/lib/event_engine/endpoint_channel_arg_wrapper.cc
  src/core/lib/event_engine/event_engine.cc
  src/core/lib/event_engine/posix_engine/ev_epoll1_linux.cc
  src/core/lib/event_engine/posix_engine/ev_poll_posix.cc
  src/core/lib/event_engine/posix_engine/event_poller_posix_default.cc
  src/core/lib/event_engine/posix_engine/file_descriptor_collection.cc
  src/core/lib/event_engine/posix_engine/internal_errqueue.cc
  src/core/lib/event_engine/posix_engine/lockfree_event.cc
  src/core/lib/event_engine/posix_engine/native_posix_dns_resolver.cc
  src/core/lib/event_engine/posix_engine/posix_endpoint.cc
  src/core/lib/event_engine/posix_engine/posix_engine.cc
  src/core/lib/event_engine/posix_engine/posix_engine_listener.cc
  src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.cc
  src/core/lib/event_engine/posix_engine/posix_interface_posix.cc
  src/core/lib/event_engine/posix_engine/posix_interface_windows.cc
  src/core/lib/event_engine/posix_engine/posix_write_event_sink.cc
  src/core/lib/event_engine/posix_engine/set_socket_dualstack.cc
  src/core/lib/event_engine/posix_engine/tcp_socket_utils.cc
  src/core/lib/event_engine/posix_engine/timer.cc
  src/core/lib/event_engine/posix_engine/timer_heap.cc
  src/core/lib/event_engine/posix_engine/timer_manager.cc
  src/core/lib/event_engine/posix_engine/traced_buffer_list.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.cc
  src/core/lib/event_engine/resolved_address.cc
  src/core/lib/event_engine/shim.cc
  src/core/lib/event_engine/slice.cc
  src/core/lib/event_engine/slice_buffer.cc
  src/core/lib/event_engine/tcp_socket_utils.cc
  src/core/lib/event_engine/thread_pool/thread_count.cc
  src/core/lib/event_engine/thread_pool/thread_pool_factory.cc
  src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.cc
  src/core/lib/event_engine/thready_event_engine/thready_event_engine.cc
  src/core/lib/event_engine/time_util.cc
  src/core/lib/event_engine/utils.cc
  src/core/lib/event_engine/windows/grpc_polled_fd_windows.cc
  src/core/lib/event_engine/windows/iocp.cc
  src/core/lib/event_engine/windows/native_windows_dns_resolver.cc
  src/core/lib/event_engine/windows/win_socket.cc
  src/core/lib/event_engine/windows/windows_endpoint.cc
  src/core/lib/event_engine/windows/windows_engine.cc
  src/core/lib/event_engine/windows/windows_listener.cc
  src/core/lib/event_engine/work_queue/basic_work_queue.cc
  src/core/lib/experiments/config.cc
  src/core/lib/experiments/experiments.cc
  src/core/lib/iomgr/buffer_list.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/cfstream_handle.cc
  src/core/lib/iomgr/closure.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/dualstack_socket_posix.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_cfstream.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/error_cfstream.cc
  src/core/lib/iomgr/ev_apple.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/event_engine_shims/closure.cc
  src/core/lib/iomgr/event_engine_shims/endpoint.cc
  src/core/lib/iomgr/event_engine_shims/tcp_client.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/internal_errqueue.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_posix_cfstream.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/polling_entity.cc
  src/core/lib/iomgr/pollset.cc
  src/core/lib/iomgr/pollset_set.cc
  src/core/lib/iomgr/pollset_set_windows.cc
  src/core/lib/iomgr/pollset_windows.cc
  src/core/lib/iomgr/resolve_address.cc
  src/core/lib/iomgr/resolve_address_posix.cc
  src/core/lib/iomgr/resolve_address_windows.cc
  src/core/lib/iomgr/sockaddr_utils_posix.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/systemd_utils.cc
  src/core/lib/iomgr/tcp_client.cc
  src/core/lib/iomgr/tcp_client_cfstream.cc
  src/core/lib/iomgr/tcp_client_posix.cc
  src/core/lib/iomgr/tcp_client_windows.cc
  src/core/lib/iomgr/tcp_posix.cc
  src/core/lib/iomgr/tcp_server.cc
  src/core/lib/iomgr/tcp_server_posix.cc
  src/core/lib/iomgr/tcp_server_utils_posix_common.cc
  src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc
  src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc
  src/core/lib/iomgr/tcp_server_windows.cc
  src/core/lib/iomgr/tcp_windows.cc
  src/core/lib/iomgr/timer.cc
  src/core/lib/iomgr/timer_generic.cc
  src/core/lib/iomgr/timer_heap.cc
  src/core/lib/iomgr/timer_manager.cc
  src/core/lib/iomgr/unix_sockets_posix.cc
  src/core/lib/iomgr/unix_sockets_posix_noop.cc
  src/core/lib/iomgr/vsock.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/promise/activity.cc
  src/core/lib/promise/mpsc.cc
  src/core/lib/promise/party.cc
  src/core/lib/promise/sleep.cc
  src/core/lib/promise/wait_set.cc
  src/core/lib/resource_quota/api.cc
  src/core/lib/resource_quota/arena.cc
  src/core/lib/resource_quota/connection_quota.cc
  src/core/lib/resource_quota/memory_quota.cc
  src/core/lib/resource_quota/periodic_update.cc
  src/core/lib/resource_quota/resource_quota.cc
  src/core/lib/resource_quota/stream_quota.cc
  src/core/lib/resource_quota/thread_quota.cc
  src/core/lib/resource_tracker/resource_tracker.cc
  src/core/lib/security/authorization/authorization_policy_provider_vtable.cc
  src/core/lib/security/authorization/evaluate_args.cc
  src/core/lib/security/authorization/grpc_server_authz_filter.cc
  src/core/lib/slice/percent_encoding.cc
  src/core/lib/slice/slice.cc
  src/core/lib/slice/slice_buffer.cc
  src/core/lib/slice/slice_string_helpers.cc
  src/core/lib/surface/byte_buffer.cc
  src/core/lib/surface/byte_buffer_reader.cc
  src/core/lib/surface/call.cc
  src/core/lib/surface/call_details.cc
  src/core/lib/surface/call_log_batch.cc
  src/core/lib/surface/call_utils.cc
  src/core/lib/surface/channel.cc
  src/core/lib/surface/channel_create.cc
  src/core/lib/surface/channel_init.cc
  src/core/lib/surface/channel_stack_type.cc
  src/core/lib/surface/completion_queue.cc
  src/core/lib/surface/completion_queue_factory.cc
  src/core/lib/surface/connection_context.cc
  src/core/lib/surface/event_string.cc
  src/core/lib/surface/filter_stack_call.cc
  src/core/lib/surface/init.cc
  src/core/lib/surface/init_internally.cc
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/legacy_channel.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/bdp_estimator.cc
  src/core/lib/transport/call_final_info.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/promise_endpoint.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/load_balancing/address_filtering.cc
  src/core/load_balancing/backend_metric_parser.cc
  src/core/load_balancing/child_policy_handler.cc
  src/core/load_balancing/endpoint_list.cc
  src/core/load_balancing/grpclb/client_load_reporting_filter.cc
  src/core/load_balancing/grpclb/grpclb.cc
  src/core/load_balancing/grpclb/grpclb_balancer_addresses.cc
  src/core/load_balancing/grpclb/grpclb_client_stats.cc
  src/core/load_balancing/grpclb/load_balancer_api.cc
  src/core/load_balancing/health_check_client.cc
  src/core/load_balancing/lb_policy.cc
  src/core/load_balancing/lb_policy_registry.cc
  src/core/load_balancing/oob_backend_metric.cc
  src/core/load_balancing/outlier_detection/outlier_detection.cc
  src/core/load_balancing/pick_first/pick_first.cc
  src/core/load_balancing/priority/priority.cc
  src/core/load_balancing/ring_hash/ring_hash.cc
  src/core/load_balancing/rls/rls.cc
  src/core/load_balancing/round_robin/round_robin.cc
  src/core/load_balancing/weighted_round_robin/static_stride_scheduler.cc
  src/core/load_balancing/weighted_round_robin/weighted_round_robin.cc
  src/core/load_balancing/weighted_target/weighted_target.cc
  src/core/net/socket_mutator.cc
  src/core/plugin_registry/grpc_plugin_registry.cc
  src/core/plugin_registry/grpc_plugin_registry_noextra.cc
  src/core/resolver/dns/c_ares/dns_resolver_ares.cc
  src/core/resolver/dns/c_ares/grpc_ares_ev_driver_posix.cc
  src/core/resolver/dns/c_ares/grpc_ares_ev_driver_windows.cc
  src/core/resolver/dns/c_ares/grpc_ares_wrapper.cc
  src/core/resolver/dns/c_ares/grpc_ares_wrapper_posix.cc
  src/core/resolver/dns/c_ares/grpc_ares_wrapper_windows.cc
  src/core/resolver/dns/dns_resolver_plugin.cc
  src/core/resolver/dns/event_engine/event_engine_client_channel_resolver.cc
  src/core/resolver/dns/event_engine/service_config_helper.cc
  src/core/resolver/dns/native/dns_resolver.cc
  src/core/resolver/endpoint_addresses.cc
  src/core/resolver/fake/fake_resolver.cc
  src/core/resolver/polling_resolver.cc
  src/core/resolver/resolver.cc
  src/core/resolver/resolver_registry.cc
  src/core/resolver/sockaddr/sockaddr_resolver.cc
  src/core/server/add_port.cc
  src/core/server/server.cc
  src/core/server/server_call_tracer_filter.cc
  src/core/service_config/service_config_channel_arg_filter.cc
  src/core/service_config/service_config_impl.cc
  src/core/service_config/service_config_parser.cc
  src/core/telemetry/call_tracer.cc
  src/core/telemetry/context_list_entry.cc
  src/core/telemetry/default_tcp_tracer.cc
  src/core/telemetry/histogram_view.cc
  src/core/telemetry/instrument.cc
  src/core/telemetry/metrics.cc
  src/core/telemetry/stats.cc
  src/core/telemetry/stats_data.cc
  src/core/telemetry/tcp_tracer.cc
  src/core/transport/auth_context.cc
  src/core/transport/endpoint_transport_client_channel_factory.cc
  src/core/tsi/alts/handshaker/transport_security_common_api.cc
  src/core/tsi/fake_transport_security.cc
  src/core/tsi/local_transport_security.cc
  src/core/tsi/transport_security.cc
  src/core/tsi/transport_security_grpc.cc
  src/core/util/backoff.cc
  src/core/util/dump_args.cc
  src/core/util/event_log.cc
  src/core/util/gethostname_fallback.cc
  src/core/util/gethostname_host_name_max.cc
  src/core/util/gethostname_sysconf.cc
  src/core/util/glob.cc
  src/core/util/grpc_check.cc
  src/core/util/grpc_if_nametoindex_posix.cc
  src/core/util/grpc_if_nametoindex_unsupported.cc
  src/core/util/http_client/format_request.cc
  src/core/util/http_client/httpcli.cc
  src/core/util/http_client/parser.cc
  src/core/util/json/json_object_loader.cc
  src/core/util/json/json_reader.cc
  src/core/util/json/json_writer.cc
  src/core/util/latent_see.cc
  src/core/util/load_file.cc
  src/core/util/per_cpu.cc
  src/core/util/postmortem_emit.cc
  src/core/util/random_early_detection.cc
  src/core/util/ref_counted_string.cc
  src/core/util/shared_bit_gen.cc
  src/core/util/status_helper.cc
  src/core/util/time.cc
  src/core/util/time_averaged_stats.cc
  src/core/util/uri.cc
  src/core/util/uuid_v4.cc
  src/core/util/validation_errors.cc
  src/core/util/wait_for_single_owner.cc
  src/core/util/work_serializer.cc
  ${gRPC_ADDITIONAL_DLL_SRC}
)

target_compile_features(grpc_unsecure PUBLIC cxx_std_17)

set_target_properties(grpc_unsecure PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_unsecure PROPERTIES COMPILE_PDB_NAME "grpc_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc_unsecure
    PRIVATE
      "GRPC_DLL_EXPORTS"
      "GPR_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(grpc_unsecure
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_textformat_lib
  ${_gRPC_ZLIB_LIBRARIES}
  absl::algorithm_container
  absl::no_destructor
  absl::btree
  absl::flat_hash_map
  absl::flat_hash_set
  absl::inlined_vector
  absl::node_hash_map
  absl::function_ref
  absl::hash
  absl::type_traits
  absl::random_bit_gen_ref
  absl::random_distributions
  absl::statusor
  absl::string_view
  absl::span
  absl::utility
  ${_gRPC_CARES_LIBRARIES}
  gpr
  ${_gRPC_ADDRESS_SORTING_LIBRARIES}
)
if(_gRPC_PLATFORM_IOS OR _gRPC_PLATFORM_MAC)
  target_link_libraries(grpc_unsecure "-framework CoreFoundation")
endif()

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/census.h
  include/grpc/compression.h
  include/grpc/create_channel_from_endpoint.h
  include/grpc/credentials.h
  include/grpc/credentials_cpp.h
  include/grpc/event_engine/endpoint_config.h
  include/grpc/event_engine/event_engine.h
  include/grpc/event_engine/extensible.h
  include/grpc/event_engine/internal/memory_allocator_impl.h
  include/grpc/event_engine/internal/slice_cast.h
  include/grpc/event_engine/internal/write_event.h
  include/grpc/event_engine/memory_allocator.h
  include/grpc/event_engine/memory_request.h
  include/grpc/event_engine/port.h
  include/grpc/event_engine/slice.h
  include/grpc/event_engine/slice_buffer.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_audit_logging.h
  include/grpc/grpc_crl_provider.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security.h
  include/grpc/grpc_security_constants.h
  include/grpc/impl/call.h
  include/grpc/impl/channel_arg_names.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/compression_types.h
  include/grpc/impl/connectivity_state.h
  include/grpc/impl/grpc_types.h
  include/grpc/impl/propagation_bits.h
  include/grpc/impl/slice_type.h
  include/grpc/load_reporting.h
  include/grpc/passive_listener.h
  include/grpc/private_key_signer.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if(gRPC_BUILD_TESTS)

add_library(gtest
  third_party/googletest/googlemock/src/gmock-cardinalities.cc
  third_party/googletest/googlemock/src/gmock-internal-utils.cc
  third_party/googletest/googlemock/src/gmock-matchers.cc
  third_party/googletest/googlemock/src/gmock-spec-builders.cc
  third_party/googletest/googlemock/src/gmock.cc
  third_party/googletest/googletest/src/gtest-assertion-result.cc
  third_party/googletest/googletest/src/gtest-death-test.cc
  third_party/googletest/googletest/src/gtest-filepath.cc
  third_party/googletest/googletest/src/gtest-matchers.cc
  third_party/googletest/googletest/src/gtest-port.cc
  third_party/googletest/googletest/src/gtest-printers.cc
  third_party/googletest/googletest/src/gtest-test-part.cc
  third_party/googletest/googletest/src/gtest-typed-test.cc
  third_party/googletest/googletest/src/gtest.cc
)

target_compile_features(gtest PUBLIC cxx_std_17)

set_target_properties(gtest PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(gtest PROPERTIES COMPILE_PDB_NAME "gtest"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/gtest.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(gtest
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
)
target_link_libraries(gtest
  ${_gRPC_ALLTARGETS_LIBRARIES}
  ${_gRPC_RE2_LIBRARIES}
  absl::flat_hash_set
  absl::failure_signal_handler
  absl::stacktrace
  absl::symbolize
  absl::flags
  absl::flags_parse
  absl::flags_reflection
  absl::flags_usage
  absl::strings
  absl::any
  absl::optional
  absl::variant
)


endif()

add_library(upb_base_lib
  third_party/upb/upb/base/status.c
)

target_compile_features(upb_base_lib PUBLIC cxx_std_17)

set_target_properties(upb_base_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_base_lib PROPERTIES COMPILE_PDB_NAME "upb_base_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_base_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_base_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_base_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



if(gRPC_INSTALL)
  install(TARGETS upb_base_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_hash_lib
  third_party/upb/upb/hash/common.c
)

target_compile_features(upb_hash_lib PUBLIC cxx_std_17)

set_target_properties(upb_hash_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_hash_lib PROPERTIES COMPILE_PDB_NAME "upb_hash_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_hash_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_hash_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_hash_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_base_lib
  upb_mem_lib
)



if(gRPC_INSTALL)
  install(TARGETS upb_hash_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_json_lib ${_gRPC_STATIC_WIN32}
  third_party/upb/upb/json/decode.c
  third_party/upb/upb/json/encode.c
)

target_compile_features(upb_json_lib PUBLIC cxx_std_17)

set_target_properties(upb_json_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_json_lib PROPERTIES COMPILE_PDB_NAME "upb_json_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_json_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_json_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_json_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_lex_lib
  upb_reflection_lib
)



if(gRPC_INSTALL)
  install(TARGETS upb_json_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_lex_lib
  third_party/upb/upb/lex/atoi.c
  third_party/upb/upb/lex/round_trip.c
  third_party/upb/upb/lex/strtod.c
  third_party/upb/upb/lex/unicode.c
)

target_compile_features(upb_lex_lib PUBLIC cxx_std_17)

set_target_properties(upb_lex_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_lex_lib PROPERTIES COMPILE_PDB_NAME "upb_lex_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_lex_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_lex_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_lex_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



if(gRPC_INSTALL)
  install(TARGETS upb_lex_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_mem_lib
  third_party/upb/upb/mem/alloc.c
  third_party/upb/upb/mem/arena.c
)

target_compile_features(upb_mem_lib PUBLIC cxx_std_17)

set_target_properties(upb_mem_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_mem_lib PROPERTIES COMPILE_PDB_NAME "upb_mem_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_mem_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_mem_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_mem_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



if(gRPC_INSTALL)
  install(TARGETS upb_mem_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_message_lib
  third_party/upb/upb/message/accessors.c
  third_party/upb/upb/message/array.c
  third_party/upb/upb/message/compat.c
  third_party/upb/upb/message/internal/extension.c
  third_party/upb/upb/message/internal/message.c
  third_party/upb/upb/message/map.c
  third_party/upb/upb/message/map_sorter.c
  third_party/upb/upb/message/message.c
)

target_compile_features(upb_message_lib PUBLIC cxx_std_17)

set_target_properties(upb_message_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_message_lib PROPERTIES COMPILE_PDB_NAME "upb_message_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_message_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_message_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_message_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_mini_table_lib
)



if(gRPC_INSTALL)
  install(TARGETS upb_message_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_mini_descriptor_lib
  third_party/upb/upb/mini_descriptor/build_enum.c
  third_party/upb/upb/mini_descriptor/decode.c
  third_party/upb/upb/mini_descriptor/internal/base92.c
  third_party/upb/upb/mini_descriptor/internal/encode.c
  third_party/upb/upb/mini_descriptor/link.c
)

target_compile_features(upb_mini_descriptor_lib PUBLIC cxx_std_17)

set_target_properties(upb_mini_descriptor_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_mini_descriptor_lib PROPERTIES COMPILE_PDB_NAME "upb_mini_descriptor_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_mini_descriptor_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_mini_descriptor_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_mini_descriptor_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_mini_table_lib
)



if(gRPC_INSTALL)
  install(TARGETS upb_mini_descriptor_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_mini_table_lib
  third_party/upb/upb/mini_table/extension_registry.c
  third_party/upb/upb/mini_table/internal/message.c
  third_party/upb/upb/mini_table/message.c
)

target_compile_features(upb_mini_table_lib PUBLIC cxx_std_17)

set_target_properties(upb_mini_table_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_mini_table_lib PROPERTIES COMPILE_PDB_NAME "upb_mini_table_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_mini_table_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_mini_table_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_mini_table_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_hash_lib
)



if(gRPC_INSTALL)
  install(TARGETS upb_mini_table_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_reflection_lib
  src/core/ext/upb-gen/google/protobuf/descriptor.upb_minitable.c
  third_party/upb/upb/message/copy.c
  third_party/upb/upb/message/merge.c
  third_party/upb/upb/reflection/def_pool.c
  third_party/upb/upb/reflection/def_type.c
  third_party/upb/upb/reflection/desc_state.c
  third_party/upb/upb/reflection/enum_def.c
  third_party/upb/upb/reflection/enum_reserved_range.c
  third_party/upb/upb/reflection/enum_value_def.c
  third_party/upb/upb/reflection/extension_range.c
  third_party/upb/upb/reflection/field_def.c
  third_party/upb/upb/reflection/file_def.c
  third_party/upb/upb/reflection/internal/def_builder.c
  third_party/upb/upb/reflection/internal/strdup2.c
  third_party/upb/upb/reflection/message.c
  third_party/upb/upb/reflection/message_def.c
  third_party/upb/upb/reflection/message_reserved_range.c
  third_party/upb/upb/reflection/method_def.c
  third_party/upb/upb/reflection/oneof_def.c
  third_party/upb/upb/reflection/service_def.c
)

target_compile_features(upb_reflection_lib PUBLIC cxx_std_17)

set_target_properties(upb_reflection_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_reflection_lib PROPERTIES COMPILE_PDB_NAME "upb_reflection_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_reflection_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_reflection_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_reflection_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_mini_descriptor_lib
  upb_wire_lib
)



if(gRPC_INSTALL)
  install(TARGETS upb_reflection_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_textformat_lib ${_gRPC_STATIC_WIN32}
  third_party/upb/upb/text/encode.c
  third_party/upb/upb/text/internal/encode.c
)

target_compile_features(upb_textformat_lib PUBLIC cxx_std_17)

set_target_properties(upb_textformat_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_textformat_lib PROPERTIES COMPILE_PDB_NAME "upb_textformat_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_textformat_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_textformat_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_textformat_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_lex_lib
  upb_reflection_lib
)



if(gRPC_INSTALL)
  install(TARGETS upb_textformat_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(upb_wire_lib
  third_party/upb/upb/message/internal/iterator.c
  third_party/upb/upb/wire/decode.c
  third_party/upb/upb/wire/encode.c
  third_party/upb/upb/wire/eps_copy_input_stream.c
  third_party/upb/upb/wire/internal/decode_fast.c
  third_party/upb/upb/wire/reader.c
)

target_compile_features(upb_wire_lib PUBLIC cxx_std_17)

set_target_properties(upb_wire_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(upb_wire_lib PROPERTIES COMPILE_PDB_NAME "upb_wire_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/upb_wire_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(upb_wire_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(upb_wire_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
  utf8_range_lib
  upb_message_lib
)



if(gRPC_INSTALL)
  install(TARGETS upb_wire_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(utf8_range_lib
  third_party/utf8_range/utf8_range.c
)

target_compile_features(utf8_range_lib PUBLIC cxx_std_17)

set_target_properties(utf8_range_lib PROPERTIES
  VERSION ${gRPC_CORE_VERSION}
  SOVERSION ${gRPC_CORE_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(utf8_range_lib PROPERTIES COMPILE_PDB_NAME "utf8_range_lib"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/utf8_range_lib.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(utf8_range_lib
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
)
target_link_libraries(utf8_range_lib
  ${_gRPC_ALLTARGETS_LIBRARIES}
)



if(gRPC_INSTALL)
  install(TARGETS utf8_range_lib EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

if(gRPC_BUILD_TESTS)

if(gRPC_BUILD_CODEGEN)
add_library(benchmark_helpers ${_gRPC_STATIC_WIN32}
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_mock.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/echo_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/simple_messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/google/api/http.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/google/api/http.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/google/api/http.pb.h
  ${_gRPC_PROTO_GENS_DIR}/google/api/http.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/google/rpc/status.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/google/rpc/status.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/google/rpc/status.pb.h
  ${_gRPC_PROTO_GENS_DIR}/google/rpc/status.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/validate/validate.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/validate/validate.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/validate/validate.pb.h
  ${_gRPC_PROTO_GENS_DIR}/validate/validate.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/xds/data/orca/v3/orca_load_report.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/xds/data/orca/v3/orca_load_report.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/xds/data/orca/v3/orca_load_report.pb.h
  ${_gRPC_PROTO_GENS_DIR}/xds/data/orca/v3/orca_load_report.grpc.pb.h
  test/cpp/microbenchmarks/helpers.cc
)

target_compile_features(benchmark_helpers PUBLIC cxx_std_17)

set_target_properties(benchmark_helpers PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(benchmark_helpers PROPERTIES COMPILE_PDB_NAME "benchmark_helpers"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(benchmark_helpers
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/benchmark_helpers.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(benchmark_helpers
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(benchmark_helpers
  ${_gRPC_ALLTARGETS_LIBRARIES}
  ${_gRPC_BENCHMARK_LIBRARIES}
  grpc++
  grpc_test_util
  grpc++_test_config
)

endif()

endif()

add_library(grpc++
  src/cpp/client/call_credentials.cc
  src/cpp/client/channel_cc.cc
  src/cpp/client/channel_credentials.cc
  src/cpp/client/client_callback.cc
  src/cpp/client/client_context.cc
  src/cpp/client/client_interceptor.cc
  src/cpp/client/client_stats_interceptor.cc
  src/cpp/client/create_channel.cc
  src/cpp/client/create_channel_internal.cc
  src/cpp/client/create_channel_posix.cc
  src/cpp/client/global_callback_hook.cc
  src/cpp/client/insecure_credentials.cc
  src/cpp/client/secure_credentials.cc
  src/cpp/client/xds_credentials.cc
  src/cpp/common/alarm.cc
  src/cpp/common/auth_property_iterator.cc
  src/cpp/common/channel_arguments.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/secure_auth_context.cc
  src/cpp/common/secure_create_auth_context.cc
  src/cpp/common/tls_certificate_provider.cc
  src/cpp/common/tls_certificate_verifier.cc
  src/cpp/common/tls_credentials_options.cc
  src/cpp/common/validate_service_config.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/backend_metric_recorder.cc
  src/cpp/server/channel_argument_option.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/external_connection_acceptor_impl.cc
  src/cpp/server/health/default_health_check_service.cc
  src/cpp/server/health/health_check_service.cc
  src/cpp/server/health/health_check_service_server_builder_option.cc
  src/cpp/server/insecure_server_credentials.cc
  src/cpp/server/secure_server_credentials.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_callback.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/server/xds_server_builder.cc
  src/cpp/server/xds_server_credentials.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  ${gRPC_UPB_GEN_DUPL_SRC}
)

target_compile_features(grpc++ PUBLIC cxx_std_17)

set_target_properties(grpc++ PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++ PROPERTIES COMPILE_PDB_NAME "grpc++"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc++
    PRIVATE
      "GRPCXX_DLL_EXPORTS"
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::dynamic_annotations
  absl::prefetch
  absl::layout
  absl::absl_check
  absl::absl_log
  absl::strings_internal
  grpc
  ${_gRPC_PROTOBUF_LIBRARIES}
)

foreach(_hdr
  include/grpc++/alarm.h
  include/grpc++/channel.h
  include/grpc++/client_context.h
  include/grpc++/completion_queue.h
  include/grpc++/create_channel.h
  include/grpc++/create_channel_posix.h
  include/grpc++/ext/health_check_service_server_builder_option.h
  include/grpc++/generic/async_generic_service.h
  include/grpc++/generic/generic_stub.h
  include/grpc++/grpc++.h
  include/grpc++/health_check_service_interface.h
  include/grpc++/impl/call.h
  include/grpc++/impl/channel_argument_option.h
  include/grpc++/impl/client_unary_call.h
  include/grpc++/impl/codegen/async_stream.h
  include/grpc++/impl/codegen/async_unary_call.h
  include/grpc++/impl/codegen/byte_buffer.h
  include/grpc++/impl/codegen/call.h
  include/grpc++/impl/codegen/channel_interface.h
  include/grpc++/impl/codegen/client_context.h
  include/grpc++/impl/codegen/client_unary_call.h
  include/grpc++/impl/codegen/completion_queue.h
  include/grpc++/impl/codegen/completion_queue_tag.h
  include/grpc++/impl/codegen/config.h
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/rpc_method.h
  include/grpc++/impl/codegen/rpc_service_method.h
  include/grpc++/impl/codegen/security/auth_context.h
  include/grpc++/impl/codegen/serialization_traits.h
  include/grpc++/impl/codegen/server_context.h
  include/grpc++/impl/codegen/server_interface.h
  include/grpc++/impl/codegen/service_type.h
  include/grpc++/impl/codegen/slice.h
  include/grpc++/impl/codegen/status.h
  include/grpc++/impl/codegen/status_code_enum.h
  include/grpc++/impl/codegen/string_ref.h
  include/grpc++/impl/codegen/stub_options.h
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc++/impl/grpc_library.h
  include/grpc++/impl/method_handler_impl.h
  include/grpc++/impl/rpc_method.h
  include/grpc++/impl/rpc_service_method.h
  include/grpc++/impl/serialization_traits.h
  include/grpc++/impl/server_builder_option.h
  include/grpc++/impl/server_builder_plugin.h
  include/grpc++/impl/server_initializer.h
  include/grpc++/impl/service_type.h
  include/grpc++/resource_quota.h
  include/grpc++/security/auth_context.h
  include/grpc++/security/auth_metadata_processor.h
  include/grpc++/security/credentials.h
  include/grpc++/security/server_credentials.h
  include/grpc++/server.h
  include/grpc++/server_builder.h
  include/grpc++/server_context.h
  include/grpc++/server_posix.h
  include/grpc++/support/async_stream.h
  include/grpc++/support/async_unary_call.h
  include/grpc++/support/byte_buffer.h
  include/grpc++/support/channel_arguments.h
  include/grpc++/support/config.h
  include/grpc++/support/slice.h
  include/grpc++/support/status.h
  include/grpc++/support/status_code_enum.h
  include/grpc++/support/string_ref.h
  include/grpc++/support/stub_options.h
  include/grpc++/support/sync_stream.h
  include/grpc++/support/time.h
  include/grpcpp/alarm.h
  include/grpcpp/channel.h
  include/grpcpp/client_context.h
  include/grpcpp/completion_queue.h
  include/grpcpp/create_channel.h
  include/grpcpp/create_channel_posix.h
  include/grpcpp/ext/call_metric_recorder.h
  include/grpcpp/ext/health_check_service_server_builder_option.h
  include/grpcpp/ext/server_metric_recorder.h
  include/grpcpp/generic/async_generic_service.h
  include/grpcpp/generic/callback_generic_service.h
  include/grpcpp/generic/generic_stub.h
  include/grpcpp/generic/generic_stub_callback.h
  include/grpcpp/grpcpp.h
  include/grpcpp/health_check_service_interface.h
  include/grpcpp/impl/call.h
  include/grpcpp/impl/call_op_set.h
  include/grpcpp/impl/call_op_set_interface.h
  include/grpcpp/impl/channel_argument_option.h
  include/grpcpp/impl/channel_interface.h
  include/grpcpp/impl/client_unary_call.h
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_op_set.h
  include/grpcpp/impl/codegen/call_op_set_interface.h
  include/grpcpp/impl/codegen/callback_common.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_callback.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_interceptor.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/config_protobuf.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/delegating_channel.h
  include/grpcpp/impl/codegen/intercepted_channel.h
  include/grpcpp/impl/codegen/interceptor.h
  include/grpcpp/impl/codegen/interceptor_common.h
  include/grpcpp/impl/codegen/message_allocator.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/proto_buffer_reader.h
  include/grpcpp/impl/codegen/proto_buffer_writer.h
  include/grpcpp/impl/codegen/proto_utils.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_callback.h
  include/grpcpp/impl/codegen/server_callback_handlers.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interceptor.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
  include/grpcpp/impl/completion_queue_tag.h
  include/grpcpp/impl/create_auth_context.h
  include/grpcpp/impl/delegating_channel.h
  include/grpcpp/impl/generic_serialize.h
  include/grpcpp/impl/generic_stub_internal.h
  include/grpcpp/impl/grpc_library.h
  include/grpcpp/impl/intercepted_channel.h
  include/grpcpp/impl/interceptor_common.h
  include/grpcpp/impl/metadata_map.h
  include/grpcpp/impl/method_handler_impl.h
  include/grpcpp/impl/proto_utils.h
  include/grpcpp/impl/rpc_method.h
  include/grpcpp/impl/rpc_service_method.h
  include/grpcpp/impl/serialization_traits.h
  include/grpcpp/impl/server_builder_option.h
  include/grpcpp/impl/server_builder_plugin.h
  include/grpcpp/impl/server_callback_handlers.h
  include/grpcpp/impl/server_initializer.h
  include/grpcpp/impl/service_type.h
  include/grpcpp/impl/status.h
  include/grpcpp/impl/sync.h
  include/grpcpp/passive_listener.h
  include/grpcpp/ports_def.inc
  include/grpcpp/ports_undef.inc
  include/grpcpp/resource_quota.h
  include/grpcpp/security/audit_logging.h
  include/grpcpp/security/auth_context.h
  include/grpcpp/security/auth_metadata_processor.h
  include/grpcpp/security/authorization_policy_provider.h
  include/grpcpp/security/credentials.h
  include/grpcpp/security/server_credentials.h
  include/grpcpp/security/tls_certificate_provider.h
  include/grpcpp/security/tls_certificate_verifier.h
  include/grpcpp/security/tls_credentials_options.h
  include/grpcpp/security/tls_crl_provider.h
  include/grpcpp/security/tls_private_key_signer.h
  include/grpcpp/server.h
  include/grpcpp/server_builder.h
  include/grpcpp/server_context.h
  include/grpcpp/server_interface.h
  include/grpcpp/server_posix.h
  include/grpcpp/support/async_stream.h
  include/grpcpp/support/async_unary_call.h
  include/grpcpp/support/byte_buffer.h
  include/grpcpp/support/callback_common.h
  include/grpcpp/support/channel_arguments.h
  include/grpcpp/support/client_callback.h
  include/grpcpp/support/client_interceptor.h
  include/grpcpp/support/config.h
  include/grpcpp/support/global_callback_hook.h
  include/grpcpp/support/interceptor.h
  include/grpcpp/support/message_allocator.h
  include/grpcpp/support/method_handler.h
  include/grpcpp/support/proto_buffer_reader.h
  include/grpcpp/support/proto_buffer_writer.h
  include/grpcpp/support/server_callback.h
  include/grpcpp/support/server_interceptor.h
  include/grpcpp/support/slice.h
  include/grpcpp/support/status.h
  include/grpcpp/support/status_code_enum.h
  include/grpcpp/support/string_ref.h
  include/grpcpp/support/stub_options.h
  include/grpcpp/support/sync_stream.h
  include/grpcpp/support/time.h
  include/grpcpp/support/validate_service_config.h
  include/grpcpp/version_info.h
  include/grpcpp/xds_server_builder.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc++ EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc++_alts
  src/cpp/common/alts_context.cc
  src/cpp/common/alts_util.cc
  ${gRPC_UPB_GEN_DUPL_SRC}
)

target_compile_features(grpc++_alts PUBLIC cxx_std_17)

set_target_properties(grpc++_alts PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_alts PROPERTIES COMPILE_PDB_NAME "grpc++_alts"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc++_alts
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_alts.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_alts
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_alts
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpcpp/security/alts_context.h
  include/grpcpp/security/alts_util.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc++_alts EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc++_error_details
  src/cpp/util/error_details.cc
)

target_compile_features(grpc++_error_details PUBLIC cxx_std_17)

set_target_properties(grpc++_error_details PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_error_details PROPERTIES COMPILE_PDB_NAME "grpc++_error_details"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc++_error_details
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_error_details.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_error_details
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_error_details
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpc++/support/error_details.h
  include/grpcpp/support/error_details.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc++_error_details EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


if(gRPC_BUILD_CODEGEN)
add_library(grpc++_reflection ${_gRPC_STATIC_WIN32}
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1/reflection.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
  src/cpp/ext/proto_server_reflection.cc
  src/cpp/ext/proto_server_reflection_plugin.cc
)

target_compile_features(grpc++_reflection PUBLIC cxx_std_17)

set_target_properties(grpc++_reflection PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_reflection PROPERTIES COMPILE_PDB_NAME "grpc++_reflection"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc++_reflection
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_reflection.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_reflection
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_reflection
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpc++/ext/proto_server_reflection_plugin.h
  include/grpcpp/ext/proto_server_reflection_plugin.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()
endif()

if(gRPC_BUILD_CODEGEN)

if(gRPC_INSTALL)
  install(TARGETS grpc++_reflection EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_TESTS)

add_library(grpc++_test
  src/cpp/client/channel_test_peer.cc
)

target_compile_features(grpc++_test PUBLIC cxx_std_17)

set_target_properties(grpc++_test PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test PROPERTIES COMPILE_PDB_NAME "grpc++_test"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc++_test
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_test
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    third_party/googletest/googletest/include
    third_party/googletest/googletest
    third_party/googletest/googlemock/include
    third_party/googletest/googlemock
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_test
  ${_gRPC_ALLTARGETS_LIBRARIES}
  gtest
  grpc++
)

foreach(_hdr
  include/grpc++/test/mock_stream.h
  include/grpc++/test/server_context_test_spouse.h
  include/grpcpp/test/channel_test_peer.h
  include/grpcpp/test/client_context_test_peer.h
  include/grpcpp/test/default_reactor_test_peer.h
  include/grpcpp/test/mock_stream.h
  include/grpcpp/test/server_context_test_spouse.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()

endif()
if(gRPC_BUILD_TESTS)

add_library(grpc++_test_config
  test/cpp/util/test_config_cc.cc
)

target_compile_features(grpc++_test_config PUBLIC cxx_std_17)

set_target_properties(grpc++_test_config PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test_config PROPERTIES COMPILE_PDB_NAME "grpc++_test_config"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc++_test_config
    PRIVATE
      "GPR_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test_config.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_test_config
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_test_config
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::flags_parse
  gpr
)


endif()
if(gRPC_BUILD_TESTS)

add_library(grpc++_test_util
  src/core/util/subprocess_posix.cc
  src/core/util/subprocess_windows.cc
  test/core/end2end/data/client_certs.cc
  test/core/end2end/data/server1_cert.cc
  test/core/end2end/data/server1_key.cc
  test/core/end2end/data/test_root_cert.cc
  test/cpp/util/byte_buffer_proto_helper.cc
  test/cpp/util/create_test_channel.cc
  test/cpp/util/string_ref_helper.cc
  test/cpp/util/subprocess.cc
  test/cpp/util/test_credentials_provider.cc
)

target_compile_features(grpc++_test_util PUBLIC cxx_std_17)

set_target_properties(grpc++_test_util PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_test_util PROPERTIES COMPILE_PDB_NAME "grpc++_test_util"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc++_test_util
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_test_util.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_test_util
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_test_util
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  grpc_test_util
)


endif()

add_library(grpc++_unsecure
  src/cpp/client/call_credentials.cc
  src/cpp/client/channel_cc.cc
  src/cpp/client/channel_credentials.cc
  src/cpp/client/client_callback.cc
  src/cpp/client/client_context.cc
  src/cpp/client/client_interceptor.cc
  src/cpp/client/client_stats_interceptor.cc
  src/cpp/client/create_channel.cc
  src/cpp/client/create_channel_internal.cc
  src/cpp/client/create_channel_posix.cc
  src/cpp/client/global_callback_hook.cc
  src/cpp/client/insecure_credentials.cc
  src/cpp/common/alarm.cc
  src/cpp/common/channel_arguments.cc
  src/cpp/common/completion_queue_cc.cc
  src/cpp/common/insecure_create_auth_context.cc
  src/cpp/common/resource_quota_cc.cc
  src/cpp/common/rpc_method.cc
  src/cpp/common/validate_service_config.cc
  src/cpp/common/version_cc.cc
  src/cpp/server/async_generic_service.cc
  src/cpp/server/backend_metric_recorder.cc
  src/cpp/server/channel_argument_option.cc
  src/cpp/server/create_default_thread_pool.cc
  src/cpp/server/external_connection_acceptor_impl.cc
  src/cpp/server/health/default_health_check_service.cc
  src/cpp/server/health/health_check_service.cc
  src/cpp/server/health/health_check_service_server_builder_option.cc
  src/cpp/server/insecure_server_credentials.cc
  src/cpp/server/server_builder.cc
  src/cpp/server/server_callback.cc
  src/cpp/server/server_cc.cc
  src/cpp/server/server_context.cc
  src/cpp/server/server_credentials.cc
  src/cpp/server/server_posix.cc
  src/cpp/thread_manager/thread_manager.cc
  src/cpp/util/byte_buffer_cc.cc
  src/cpp/util/status.cc
  src/cpp/util/string_ref.cc
  src/cpp/util/time_cc.cc
  ${gRPC_UPB_GEN_DUPL_SRC}
  ${gRPC_ADDITIONAL_DLL_CXX_SRC}
)

target_compile_features(grpc++_unsecure PUBLIC cxx_std_17)

set_target_properties(grpc++_unsecure PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc++_unsecure PROPERTIES COMPILE_PDB_NAME "grpc++_unsecure"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc++_unsecure
    PRIVATE
      "GRPCXX_DLL_EXPORTS"
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc++_unsecure.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc++_unsecure
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc++_unsecure
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::dynamic_annotations
  absl::prefetch
  absl::layout
  absl::absl_check
  absl::absl_log
  absl::strings_internal
  grpc_unsecure
  ${_gRPC_PROTOBUF_LIBRARIES}
)

foreach(_hdr
  include/grpc++/alarm.h
  include/grpc++/channel.h
  include/grpc++/client_context.h
  include/grpc++/completion_queue.h
  include/grpc++/create_channel.h
  include/grpc++/create_channel_posix.h
  include/grpc++/ext/health_check_service_server_builder_option.h
  include/grpc++/generic/async_generic_service.h
  include/grpc++/generic/generic_stub.h
  include/grpc++/grpc++.h
  include/grpc++/health_check_service_interface.h
  include/grpc++/impl/call.h
  include/grpc++/impl/channel_argument_option.h
  include/grpc++/impl/client_unary_call.h
  include/grpc++/impl/codegen/async_stream.h
  include/grpc++/impl/codegen/async_unary_call.h
  include/grpc++/impl/codegen/byte_buffer.h
  include/grpc++/impl/codegen/call.h
  include/grpc++/impl/codegen/channel_interface.h
  include/grpc++/impl/codegen/client_context.h
  include/grpc++/impl/codegen/client_unary_call.h
  include/grpc++/impl/codegen/completion_queue.h
  include/grpc++/impl/codegen/completion_queue_tag.h
  include/grpc++/impl/codegen/config.h
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpc++/impl/codegen/create_auth_context.h
  include/grpc++/impl/codegen/metadata_map.h
  include/grpc++/impl/codegen/method_handler_impl.h
  include/grpc++/impl/codegen/proto_utils.h
  include/grpc++/impl/codegen/rpc_method.h
  include/grpc++/impl/codegen/rpc_service_method.h
  include/grpc++/impl/codegen/security/auth_context.h
  include/grpc++/impl/codegen/serialization_traits.h
  include/grpc++/impl/codegen/server_context.h
  include/grpc++/impl/codegen/server_interface.h
  include/grpc++/impl/codegen/service_type.h
  include/grpc++/impl/codegen/slice.h
  include/grpc++/impl/codegen/status.h
  include/grpc++/impl/codegen/status_code_enum.h
  include/grpc++/impl/codegen/string_ref.h
  include/grpc++/impl/codegen/stub_options.h
  include/grpc++/impl/codegen/sync_stream.h
  include/grpc++/impl/codegen/time.h
  include/grpc++/impl/grpc_library.h
  include/grpc++/impl/method_handler_impl.h
  include/grpc++/impl/rpc_method.h
  include/grpc++/impl/rpc_service_method.h
  include/grpc++/impl/serialization_traits.h
  include/grpc++/impl/server_builder_option.h
  include/grpc++/impl/server_builder_plugin.h
  include/grpc++/impl/server_initializer.h
  include/grpc++/impl/service_type.h
  include/grpc++/resource_quota.h
  include/grpc++/security/auth_context.h
  include/grpc++/security/auth_metadata_processor.h
  include/grpc++/security/credentials.h
  include/grpc++/security/server_credentials.h
  include/grpc++/server.h
  include/grpc++/server_builder.h
  include/grpc++/server_context.h
  include/grpc++/server_posix.h
  include/grpc++/support/async_stream.h
  include/grpc++/support/async_unary_call.h
  include/grpc++/support/byte_buffer.h
  include/grpc++/support/channel_arguments.h
  include/grpc++/support/config.h
  include/grpc++/support/slice.h
  include/grpc++/support/status.h
  include/grpc++/support/status_code_enum.h
  include/grpc++/support/string_ref.h
  include/grpc++/support/stub_options.h
  include/grpc++/support/sync_stream.h
  include/grpc++/support/time.h
  include/grpcpp/alarm.h
  include/grpcpp/channel.h
  include/grpcpp/client_context.h
  include/grpcpp/completion_queue.h
  include/grpcpp/create_channel.h
  include/grpcpp/create_channel_posix.h
  include/grpcpp/ext/call_metric_recorder.h
  include/grpcpp/ext/health_check_service_server_builder_option.h
  include/grpcpp/ext/server_metric_recorder.h
  include/grpcpp/generic/async_generic_service.h
  include/grpcpp/generic/callback_generic_service.h
  include/grpcpp/generic/generic_stub.h
  include/grpcpp/generic/generic_stub_callback.h
  include/grpcpp/grpcpp.h
  include/grpcpp/health_check_service_interface.h
  include/grpcpp/impl/call.h
  include/grpcpp/impl/call_op_set.h
  include/grpcpp/impl/call_op_set_interface.h
  include/grpcpp/impl/channel_argument_option.h
  include/grpcpp/impl/channel_interface.h
  include/grpcpp/impl/client_unary_call.h
  include/grpcpp/impl/codegen/async_generic_service.h
  include/grpcpp/impl/codegen/async_stream.h
  include/grpcpp/impl/codegen/async_unary_call.h
  include/grpcpp/impl/codegen/byte_buffer.h
  include/grpcpp/impl/codegen/call.h
  include/grpcpp/impl/codegen/call_op_set.h
  include/grpcpp/impl/codegen/call_op_set_interface.h
  include/grpcpp/impl/codegen/callback_common.h
  include/grpcpp/impl/codegen/channel_interface.h
  include/grpcpp/impl/codegen/client_callback.h
  include/grpcpp/impl/codegen/client_context.h
  include/grpcpp/impl/codegen/client_interceptor.h
  include/grpcpp/impl/codegen/client_unary_call.h
  include/grpcpp/impl/codegen/completion_queue.h
  include/grpcpp/impl/codegen/completion_queue_tag.h
  include/grpcpp/impl/codegen/config.h
  include/grpcpp/impl/codegen/config_protobuf.h
  include/grpcpp/impl/codegen/create_auth_context.h
  include/grpcpp/impl/codegen/delegating_channel.h
  include/grpcpp/impl/codegen/intercepted_channel.h
  include/grpcpp/impl/codegen/interceptor.h
  include/grpcpp/impl/codegen/interceptor_common.h
  include/grpcpp/impl/codegen/message_allocator.h
  include/grpcpp/impl/codegen/metadata_map.h
  include/grpcpp/impl/codegen/method_handler.h
  include/grpcpp/impl/codegen/method_handler_impl.h
  include/grpcpp/impl/codegen/proto_buffer_reader.h
  include/grpcpp/impl/codegen/proto_buffer_writer.h
  include/grpcpp/impl/codegen/proto_utils.h
  include/grpcpp/impl/codegen/rpc_method.h
  include/grpcpp/impl/codegen/rpc_service_method.h
  include/grpcpp/impl/codegen/security/auth_context.h
  include/grpcpp/impl/codegen/serialization_traits.h
  include/grpcpp/impl/codegen/server_callback.h
  include/grpcpp/impl/codegen/server_callback_handlers.h
  include/grpcpp/impl/codegen/server_context.h
  include/grpcpp/impl/codegen/server_interceptor.h
  include/grpcpp/impl/codegen/server_interface.h
  include/grpcpp/impl/codegen/service_type.h
  include/grpcpp/impl/codegen/slice.h
  include/grpcpp/impl/codegen/status.h
  include/grpcpp/impl/codegen/status_code_enum.h
  include/grpcpp/impl/codegen/string_ref.h
  include/grpcpp/impl/codegen/stub_options.h
  include/grpcpp/impl/codegen/sync.h
  include/grpcpp/impl/codegen/sync_stream.h
  include/grpcpp/impl/codegen/time.h
  include/grpcpp/impl/completion_queue_tag.h
  include/grpcpp/impl/create_auth_context.h
  include/grpcpp/impl/delegating_channel.h
  include/grpcpp/impl/generic_serialize.h
  include/grpcpp/impl/generic_stub_internal.h
  include/grpcpp/impl/grpc_library.h
  include/grpcpp/impl/intercepted_channel.h
  include/grpcpp/impl/interceptor_common.h
  include/grpcpp/impl/metadata_map.h
  include/grpcpp/impl/method_handler_impl.h
  include/grpcpp/impl/proto_utils.h
  include/grpcpp/impl/rpc_method.h
  include/grpcpp/impl/rpc_service_method.h
  include/grpcpp/impl/serialization_traits.h
  include/grpcpp/impl/server_builder_option.h
  include/grpcpp/impl/server_builder_plugin.h
  include/grpcpp/impl/server_callback_handlers.h
  include/grpcpp/impl/server_initializer.h
  include/grpcpp/impl/service_type.h
  include/grpcpp/impl/status.h
  include/grpcpp/impl/sync.h
  include/grpcpp/passive_listener.h
  include/grpcpp/ports_def.inc
  include/grpcpp/ports_undef.inc
  include/grpcpp/resource_quota.h
  include/grpcpp/security/audit_logging.h
  include/grpcpp/security/auth_context.h
  include/grpcpp/security/auth_metadata_processor.h
  include/grpcpp/security/authorization_policy_provider.h
  include/grpcpp/security/credentials.h
  include/grpcpp/security/server_credentials.h
  include/grpcpp/security/tls_certificate_provider.h
  include/grpcpp/security/tls_certificate_verifier.h
  include/grpcpp/security/tls_credentials_options.h
  include/grpcpp/security/tls_crl_provider.h
  include/grpcpp/security/tls_private_key_signer.h
  include/grpcpp/server.h
  include/grpcpp/server_builder.h
  include/grpcpp/server_context.h
  include/grpcpp/server_interface.h
  include/grpcpp/server_posix.h
  include/grpcpp/support/async_stream.h
  include/grpcpp/support/async_unary_call.h
  include/grpcpp/support/byte_buffer.h
  include/grpcpp/support/callback_common.h
  include/grpcpp/support/channel_arguments.h
  include/grpcpp/support/client_callback.h
  include/grpcpp/support/client_interceptor.h
  include/grpcpp/support/config.h
  include/grpcpp/support/global_callback_hook.h
  include/grpcpp/support/interceptor.h
  include/grpcpp/support/message_allocator.h
  include/grpcpp/support/method_handler.h
  include/grpcpp/support/proto_buffer_reader.h
  include/grpcpp/support/proto_buffer_writer.h
  include/grpcpp/support/server_callback.h
  include/grpcpp/support/server_interceptor.h
  include/grpcpp/support/slice.h
  include/grpcpp/support/status.h
  include/grpcpp/support/status_code_enum.h
  include/grpcpp/support/string_ref.h
  include/grpcpp/support/stub_options.h
  include/grpcpp/support/sync_stream.h
  include/grpcpp/support/time.h
  include/grpcpp/support/validate_service_config.h
  include/grpcpp/version_info.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc++_unsecure EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc_authorization_provider
  src/core/call/call_arena_allocator.cc
  src/core/call/call_filters.cc
  src/core/call/call_spine.cc
  src/core/call/call_state.cc
  src/core/call/client_call.cc
  src/core/call/interception_chain.cc
  src/core/call/message.cc
  src/core/call/metadata.cc
  src/core/call/metadata_batch.cc
  src/core/call/parsed_metadata.cc
  src/core/call/security_context.cc
  src/core/call/server_call.cc
  src/core/call/status_util.cc
  src/core/channelz/channel_trace.cc
  src/core/channelz/channelz.cc
  src/core/channelz/channelz_registry.cc
  src/core/channelz/property_list.cc
  src/core/channelz/text_encode.cc
  src/core/config/core_configuration.cc
  src/core/credentials/call/call_creds_util.cc
  src/core/credentials/call/composite/composite_call_credentials.cc
  src/core/credentials/call/json_util.cc
  src/core/credentials/call/plugin/plugin_credentials.cc
  src/core/credentials/transport/alts/check_gcp_environment.cc
  src/core/credentials/transport/alts/check_gcp_environment_linux.cc
  src/core/credentials/transport/alts/check_gcp_environment_no_op.cc
  src/core/credentials/transport/alts/check_gcp_environment_windows.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_client_options.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_options.cc
  src/core/credentials/transport/alts/grpc_alts_credentials_server_options.cc
  src/core/credentials/transport/composite/composite_channel_credentials.cc
  src/core/credentials/transport/security_connector.cc
  src/core/credentials/transport/tls/certificate_provider_registry.cc
  src/core/credentials/transport/tls/load_system_roots_fallback.cc
  src/core/credentials/transport/tls/load_system_roots_supported.cc
  src/core/credentials/transport/tls/load_system_roots_windows.cc
  src/core/credentials/transport/tls/tls_utils.cc
  src/core/credentials/transport/transport_credentials.cc
  src/core/ext/upb-gen/google/protobuf/any.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/duration.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/empty.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/timestamp.upb_minitable.c
  src/core/ext/upb-gen/google/rpc/status.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/channelz.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/promise.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/property_list.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/service.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/altscontext.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/handshaker.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/gcp/transport_security_common.upb_minitable.c
  src/core/ext/upbdefs-gen/google/protobuf/any.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/duration.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/empty.upbdefs.c
  src/core/ext/upbdefs-gen/google/protobuf/timestamp.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/channelz.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/promise.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/property_list.upbdefs.c
  src/core/ext/upbdefs-gen/src/proto/grpc/channelz/v2/service.upbdefs.c
  src/core/filter/auth/client_auth_filter.cc
  src/core/filter/auth/server_auth_filter.cc
  src/core/filter/blackboard.cc
  src/core/handshaker/endpoint_info/endpoint_info_handshaker.cc
  src/core/handshaker/handshaker.cc
  src/core/handshaker/handshaker_registry.cc
  src/core/handshaker/proxy_mapper_registry.cc
  src/core/handshaker/security/pipelined_secure_endpoint.cc
  src/core/handshaker/security/secure_endpoint.cc
  src/core/handshaker/security/security_handshaker.cc
  src/core/lib/address_utils/parse_address.cc
  src/core/lib/address_utils/sockaddr_utils.cc
  src/core/lib/channel/channel_args.cc
  src/core/lib/channel/channel_args_preconditioning.cc
  src/core/lib/channel/channel_stack.cc
  src/core/lib/channel/channel_stack_builder.cc
  src/core/lib/channel/channel_stack_builder_impl.cc
  src/core/lib/channel/connected_channel.cc
  src/core/lib/channel/promise_based_filter.cc
  src/core/lib/compression/compression.cc
  src/core/lib/compression/compression_internal.cc
  src/core/lib/compression/message_compress.cc
  src/core/lib/debug/trace.cc
  src/core/lib/debug/trace_flags.cc
  src/core/lib/event_engine/ares_resolver.cc
  src/core/lib/event_engine/cf_engine/cf_engine.cc
  src/core/lib/event_engine/cf_engine/cfsocket_listener.cc
  src/core/lib/event_engine/cf_engine/cfstream_endpoint.cc
  src/core/lib/event_engine/cf_engine/dns_service_resolver.cc
  src/core/lib/event_engine/channel_args_endpoint_config.cc
  src/core/lib/event_engine/default_event_engine.cc
  src/core/lib/event_engine/default_event_engine_factory.cc
  src/core/lib/event_engine/event_engine.cc
  src/core/lib/event_engine/posix_engine/ev_epoll1_linux.cc
  src/core/lib/event_engine/posix_engine/ev_poll_posix.cc
  src/core/lib/event_engine/posix_engine/event_poller_posix_default.cc
  src/core/lib/event_engine/posix_engine/file_descriptor_collection.cc
  src/core/lib/event_engine/posix_engine/internal_errqueue.cc
  src/core/lib/event_engine/posix_engine/lockfree_event.cc
  src/core/lib/event_engine/posix_engine/native_posix_dns_resolver.cc
  src/core/lib/event_engine/posix_engine/posix_endpoint.cc
  src/core/lib/event_engine/posix_engine/posix_engine.cc
  src/core/lib/event_engine/posix_engine/posix_engine_listener.cc
  src/core/lib/event_engine/posix_engine/posix_engine_listener_utils.cc
  src/core/lib/event_engine/posix_engine/posix_interface_posix.cc
  src/core/lib/event_engine/posix_engine/posix_interface_windows.cc
  src/core/lib/event_engine/posix_engine/posix_write_event_sink.cc
  src/core/lib/event_engine/posix_engine/set_socket_dualstack.cc
  src/core/lib/event_engine/posix_engine/tcp_socket_utils.cc
  src/core/lib/event_engine/posix_engine/timer.cc
  src/core/lib/event_engine/posix_engine/timer_heap.cc
  src/core/lib/event_engine/posix_engine/timer_manager.cc
  src/core/lib/event_engine/posix_engine/traced_buffer_list.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_eventfd.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_pipe.cc
  src/core/lib/event_engine/posix_engine/wakeup_fd_posix_default.cc
  src/core/lib/event_engine/resolved_address.cc
  src/core/lib/event_engine/shim.cc
  src/core/lib/event_engine/slice.cc
  src/core/lib/event_engine/slice_buffer.cc
  src/core/lib/event_engine/tcp_socket_utils.cc
  src/core/lib/event_engine/thread_pool/thread_count.cc
  src/core/lib/event_engine/thread_pool/thread_pool_factory.cc
  src/core/lib/event_engine/thread_pool/work_stealing_thread_pool.cc
  src/core/lib/event_engine/thready_event_engine/thready_event_engine.cc
  src/core/lib/event_engine/time_util.cc
  src/core/lib/event_engine/utils.cc
  src/core/lib/event_engine/windows/grpc_polled_fd_windows.cc
  src/core/lib/event_engine/windows/iocp.cc
  src/core/lib/event_engine/windows/native_windows_dns_resolver.cc
  src/core/lib/event_engine/windows/win_socket.cc
  src/core/lib/event_engine/windows/windows_endpoint.cc
  src/core/lib/event_engine/windows/windows_engine.cc
  src/core/lib/event_engine/windows/windows_listener.cc
  src/core/lib/event_engine/work_queue/basic_work_queue.cc
  src/core/lib/experiments/config.cc
  src/core/lib/experiments/experiments.cc
  src/core/lib/iomgr/buffer_list.cc
  src/core/lib/iomgr/call_combiner.cc
  src/core/lib/iomgr/cfstream_handle.cc
  src/core/lib/iomgr/closure.cc
  src/core/lib/iomgr/combiner.cc
  src/core/lib/iomgr/dualstack_socket_posix.cc
  src/core/lib/iomgr/endpoint.cc
  src/core/lib/iomgr/endpoint_cfstream.cc
  src/core/lib/iomgr/endpoint_pair_posix.cc
  src/core/lib/iomgr/endpoint_pair_windows.cc
  src/core/lib/iomgr/error.cc
  src/core/lib/iomgr/error_cfstream.cc
  src/core/lib/iomgr/ev_apple.cc
  src/core/lib/iomgr/ev_epoll1_linux.cc
  src/core/lib/iomgr/ev_poll_posix.cc
  src/core/lib/iomgr/ev_posix.cc
  src/core/lib/iomgr/event_engine_shims/closure.cc
  src/core/lib/iomgr/event_engine_shims/endpoint.cc
  src/core/lib/iomgr/event_engine_shims/tcp_client.cc
  src/core/lib/iomgr/exec_ctx.cc
  src/core/lib/iomgr/fork_posix.cc
  src/core/lib/iomgr/fork_windows.cc
  src/core/lib/iomgr/internal_errqueue.cc
  src/core/lib/iomgr/iocp_windows.cc
  src/core/lib/iomgr/iomgr.cc
  src/core/lib/iomgr/iomgr_internal.cc
  src/core/lib/iomgr/iomgr_posix.cc
  src/core/lib/iomgr/iomgr_posix_cfstream.cc
  src/core/lib/iomgr/iomgr_windows.cc
  src/core/lib/iomgr/lockfree_event.cc
  src/core/lib/iomgr/polling_entity.cc
  src/core/lib/iomgr/pollset.cc
  src/core/lib/iomgr/pollset_set.cc
  src/core/lib/iomgr/pollset_set_windows.cc
  src/core/lib/iomgr/pollset_windows.cc
  src/core/lib/iomgr/resolve_address.cc
  src/core/lib/iomgr/resolve_address_posix.cc
  src/core/lib/iomgr/resolve_address_windows.cc
  src/core/lib/iomgr/sockaddr_utils_posix.cc
  src/core/lib/iomgr/socket_factory_posix.cc
  src/core/lib/iomgr/socket_mutator.cc
  src/core/lib/iomgr/socket_utils_common_posix.cc
  src/core/lib/iomgr/socket_utils_linux.cc
  src/core/lib/iomgr/socket_utils_posix.cc
  src/core/lib/iomgr/socket_utils_windows.cc
  src/core/lib/iomgr/socket_windows.cc
  src/core/lib/iomgr/systemd_utils.cc
  src/core/lib/iomgr/tcp_client.cc
  src/core/lib/iomgr/tcp_client_cfstream.cc
  src/core/lib/iomgr/tcp_client_posix.cc
  src/core/lib/iomgr/tcp_client_windows.cc
  src/core/lib/iomgr/tcp_posix.cc
  src/core/lib/iomgr/tcp_server.cc
  src/core/lib/iomgr/tcp_server_posix.cc
  src/core/lib/iomgr/tcp_server_utils_posix_common.cc
  src/core/lib/iomgr/tcp_server_utils_posix_ifaddrs.cc
  src/core/lib/iomgr/tcp_server_utils_posix_noifaddrs.cc
  src/core/lib/iomgr/tcp_server_windows.cc
  src/core/lib/iomgr/tcp_windows.cc
  src/core/lib/iomgr/timer.cc
  src/core/lib/iomgr/timer_generic.cc
  src/core/lib/iomgr/timer_heap.cc
  src/core/lib/iomgr/timer_manager.cc
  src/core/lib/iomgr/unix_sockets_posix.cc
  src/core/lib/iomgr/unix_sockets_posix_noop.cc
  src/core/lib/iomgr/vsock.cc
  src/core/lib/iomgr/wakeup_fd_eventfd.cc
  src/core/lib/iomgr/wakeup_fd_nospecial.cc
  src/core/lib/iomgr/wakeup_fd_pipe.cc
  src/core/lib/iomgr/wakeup_fd_posix.cc
  src/core/lib/promise/activity.cc
  src/core/lib/promise/party.cc
  src/core/lib/promise/wait_set.cc
  src/core/lib/resource_quota/api.cc
  src/core/lib/resource_quota/arena.cc
  src/core/lib/resource_quota/memory_quota.cc
  src/core/lib/resource_quota/periodic_update.cc
  src/core/lib/resource_quota/resource_quota.cc
  src/core/lib/resource_quota/stream_quota.cc
  src/core/lib/resource_quota/thread_quota.cc
  src/core/lib/resource_tracker/resource_tracker.cc
  src/core/lib/security/authorization/audit_logging.cc
  src/core/lib/security/authorization/authorization_policy_provider_vtable.cc
  src/core/lib/security/authorization/evaluate_args.cc
  src/core/lib/security/authorization/grpc_authorization_engine.cc
  src/core/lib/security/authorization/grpc_authorization_policy_provider.cc
  src/core/lib/security/authorization/grpc_server_authz_filter.cc
  src/core/lib/security/authorization/matchers.cc
  src/core/lib/security/authorization/rbac_policy.cc
  src/core/lib/security/authorization/rbac_translator.cc
  src/core/lib/security/authorization/stdout_logger.cc
  src/core/lib/slice/percent_encoding.cc
  src/core/lib/slice/slice.cc
  src/core/lib/slice/slice_buffer.cc
  src/core/lib/slice/slice_string_helpers.cc
  src/core/lib/surface/byte_buffer.cc
  src/core/lib/surface/byte_buffer_reader.cc
  src/core/lib/surface/call.cc
  src/core/lib/surface/call_details.cc
  src/core/lib/surface/call_log_batch.cc
  src/core/lib/surface/call_utils.cc
  src/core/lib/surface/channel.cc
  src/core/lib/surface/channel_init.cc
  src/core/lib/surface/channel_stack_type.cc
  src/core/lib/surface/completion_queue.cc
  src/core/lib/surface/completion_queue_factory.cc
  src/core/lib/surface/connection_context.cc
  src/core/lib/surface/event_string.cc
  src/core/lib/surface/filter_stack_call.cc
  src/core/lib/surface/init_internally.cc
  src/core/lib/surface/lame_client.cc
  src/core/lib/surface/metadata_array.cc
  src/core/lib/surface/validate_metadata.cc
  src/core/lib/surface/version.cc
  src/core/lib/transport/call_final_info.cc
  src/core/lib/transport/connectivity_state.cc
  src/core/lib/transport/error_utils.cc
  src/core/lib/transport/status_conversion.cc
  src/core/lib/transport/timeout_encoding.cc
  src/core/lib/transport/transport.cc
  src/core/lib/transport/transport_op_string.cc
  src/core/load_balancing/lb_policy.cc
  src/core/load_balancing/lb_policy_registry.cc
  src/core/net/socket_mutator.cc
  src/core/resolver/endpoint_addresses.cc
  src/core/resolver/resolver.cc
  src/core/resolver/resolver_registry.cc
  src/core/service_config/service_config_parser.cc
  src/core/telemetry/call_tracer.cc
  src/core/telemetry/context_list_entry.cc
  src/core/telemetry/histogram_view.cc
  src/core/telemetry/instrument.cc
  src/core/telemetry/metrics.cc
  src/core/telemetry/stats.cc
  src/core/telemetry/stats_data.cc
  src/core/telemetry/tcp_tracer.cc
  src/core/transport/auth_context.cc
  src/core/tsi/alts/handshaker/transport_security_common_api.cc
  src/core/tsi/transport_security.cc
  src/core/tsi/transport_security_grpc.cc
  src/core/util/backoff.cc
  src/core/util/dump_args.cc
  src/core/util/event_log.cc
  src/core/util/gethostname_fallback.cc
  src/core/util/gethostname_host_name_max.cc
  src/core/util/gethostname_sysconf.cc
  src/core/util/glob.cc
  src/core/util/grpc_check.cc
  src/core/util/grpc_if_nametoindex_posix.cc
  src/core/util/grpc_if_nametoindex_unsupported.cc
  src/core/util/json/json_object_loader.cc
  src/core/util/json/json_reader.cc
  src/core/util/json/json_writer.cc
  src/core/util/latent_see.cc
  src/core/util/load_file.cc
  src/core/util/matchers.cc
  src/core/util/per_cpu.cc
  src/core/util/postmortem_emit.cc
  src/core/util/ref_counted_string.cc
  src/core/util/shared_bit_gen.cc
  src/core/util/status_helper.cc
  src/core/util/time.cc
  src/core/util/time_averaged_stats.cc
  src/core/util/uri.cc
  src/core/util/validation_errors.cc
  src/core/util/wait_for_single_owner.cc
  src/core/util/work_serializer.cc
)

target_compile_features(grpc_authorization_provider PUBLIC cxx_std_17)

set_target_properties(grpc_authorization_provider PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_authorization_provider PROPERTIES COMPILE_PDB_NAME "grpc_authorization_provider"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc_authorization_provider
    PRIVATE
      "GPR_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_authorization_provider.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_authorization_provider
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc_authorization_provider
  ${_gRPC_ALLTARGETS_LIBRARIES}
  upb_textformat_lib
  ${_gRPC_RE2_LIBRARIES}
  ${_gRPC_ZLIB_LIBRARIES}
  absl::no_destructor
  absl::btree
  absl::flat_hash_map
  absl::flat_hash_set
  absl::inlined_vector
  absl::node_hash_map
  absl::function_ref
  absl::hash
  absl::type_traits
  absl::statusor
  absl::string_view
  absl::span
  absl::utility
  ${_gRPC_CARES_LIBRARIES}
  gpr
  ${_gRPC_ADDRESS_SORTING_LIBRARIES}
)

foreach(_hdr
  include/grpc/byte_buffer.h
  include/grpc/byte_buffer_reader.h
  include/grpc/compression.h
  include/grpc/create_channel_from_endpoint.h
  include/grpc/credentials.h
  include/grpc/credentials_cpp.h
  include/grpc/event_engine/endpoint_config.h
  include/grpc/event_engine/event_engine.h
  include/grpc/event_engine/extensible.h
  include/grpc/event_engine/internal/memory_allocator_impl.h
  include/grpc/event_engine/internal/slice_cast.h
  include/grpc/event_engine/internal/write_event.h
  include/grpc/event_engine/memory_allocator.h
  include/grpc/event_engine/memory_request.h
  include/grpc/event_engine/port.h
  include/grpc/event_engine/slice.h
  include/grpc/event_engine/slice_buffer.h
  include/grpc/fork.h
  include/grpc/grpc.h
  include/grpc/grpc_audit_logging.h
  include/grpc/grpc_crl_provider.h
  include/grpc/grpc_posix.h
  include/grpc/grpc_security.h
  include/grpc/grpc_security_constants.h
  include/grpc/impl/call.h
  include/grpc/impl/channel_arg_names.h
  include/grpc/impl/codegen/byte_buffer.h
  include/grpc/impl/codegen/byte_buffer_reader.h
  include/grpc/impl/codegen/compression_types.h
  include/grpc/impl/codegen/connectivity_state.h
  include/grpc/impl/codegen/fork.h
  include/grpc/impl/codegen/grpc_types.h
  include/grpc/impl/codegen/propagation_bits.h
  include/grpc/impl/codegen/slice.h
  include/grpc/impl/codegen/status.h
  include/grpc/impl/compression_types.h
  include/grpc/impl/connectivity_state.h
  include/grpc/impl/grpc_types.h
  include/grpc/impl/propagation_bits.h
  include/grpc/impl/slice_type.h
  include/grpc/load_reporting.h
  include/grpc/passive_listener.h
  include/grpc/private_key_signer.h
  include/grpc/slice.h
  include/grpc/slice_buffer.h
  include/grpc/status.h
  include/grpc/support/workaround_list.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc_authorization_provider EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


add_library(grpc_plugin_support
  src/compiler/cpp_generator.cc
  src/compiler/csharp_generator.cc
  src/compiler/node_generator.cc
  src/compiler/objective_c_generator.cc
  src/compiler/php_generator.cc
  src/compiler/proto_parser_helper.cc
  src/compiler/python_generator.cc
  src/compiler/ruby_generator.cc
)

target_compile_features(grpc_plugin_support PUBLIC cxx_std_17)

set_target_properties(grpc_plugin_support PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpc_plugin_support PROPERTIES COMPILE_PDB_NAME "grpc_plugin_support"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpc_plugin_support.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpc_plugin_support
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpc_plugin_support
  ${_gRPC_ALLTARGETS_LIBRARIES}
  utf8_range_lib
  ${_gRPC_ZLIB_LIBRARIES}
  absl::algorithm_container
  absl::base
  absl::config
  absl::core_headers
  absl::dynamic_annotations
  absl::prefetch
  absl::cleanup
  absl::btree
  absl::fixed_array
  absl::flat_hash_map
  absl::flat_hash_set
  absl::layout
  absl::any_invocable
  absl::function_ref
  absl::overload
  absl::hash
  absl::absl_check
  absl::absl_log
  absl::memory
  absl::type_traits
  absl::bits
  absl::status
  absl::statusor
  absl::charset
  absl::cord
  absl::strings_internal
  absl::str_format
  absl::string_view
  absl::strings
  absl::synchronization
  absl::time
  absl::optional
  absl::span
  absl::utility
  ${_gRPC_PROTOBUF_LIBRARIES}
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
)

foreach(_hdr
  include/grpc++/impl/codegen/config_protobuf.h
  include/grpc/impl/codegen/port_platform.h
  include/grpc/support/port_platform.h
  include/grpcpp/impl/codegen/config_protobuf.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpc_plugin_support EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()


# grpcpp_channelz doesn't build with protobuf-lite
# See https://github.com/grpc/grpc/issues/19473
if(gRPC_BUILD_CODEGEN AND NOT gRPC_USE_PROTO_LITE)
add_library(grpcpp_channelz ${_gRPC_STATIC_WIN32}
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/channelz.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/channelz.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/channelz.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/channelz.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/channelz.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/service.grpc.pb.h
  src/cpp/server/channelz/channelz_service.cc
  src/cpp/server/channelz/channelz_service_plugin.cc
)

target_compile_features(grpcpp_channelz PUBLIC cxx_std_17)

set_target_properties(grpcpp_channelz PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpcpp_channelz PROPERTIES COMPILE_PDB_NAME "grpcpp_channelz"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpcpp_channelz
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpcpp_channelz.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpcpp_channelz
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpcpp_channelz
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
)

foreach(_hdr
  include/grpcpp/ext/channelz_service_plugin.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()
endif()

if(gRPC_BUILD_CODEGEN)

# grpcpp_channelz doesn't build with protobuf-lite, so no install required
# See https://github.com/grpc/grpc/issues/22826
if(gRPC_INSTALL AND NOT gRPC_USE_PROTO_LITE)
  install(TARGETS grpcpp_channelz EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_GRPCPP_OTEL_PLUGIN)

add_library(grpcpp_otel_plugin
  src/cpp/ext/otel/otel_client_call_tracer.cc
  src/cpp/ext/otel/otel_plugin.cc
  src/cpp/ext/otel/otel_server_call_tracer.cc
)

target_compile_features(grpcpp_otel_plugin PUBLIC cxx_std_17)

set_target_properties(grpcpp_otel_plugin PROPERTIES
  VERSION ${gRPC_CPP_VERSION}
  SOVERSION ${gRPC_CPP_SOVERSION}
)

if(WIN32 AND MSVC)
  set_target_properties(grpcpp_otel_plugin PROPERTIES COMPILE_PDB_NAME "grpcpp_otel_plugin"
    COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpcpp_otel_plugin
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
  if(gRPC_INSTALL)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/grpcpp_otel_plugin.pdb
      DESTINATION ${gRPC_INSTALL_LIBDIR} OPTIONAL
    )
  endif()
endif()

target_include_directories(grpcpp_otel_plugin
  PUBLIC $<INSTALL_INTERFACE:${gRPC_INSTALL_INCLUDEDIR}> $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)
target_link_libraries(grpcpp_otel_plugin
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++
  opentelemetry-cpp::api
)

foreach(_hdr
  include/grpcpp/ext/otel_plugin.h
)
  string(REPLACE "include/" "" _path ${_hdr})
  get_filename_component(_path ${_path} PATH)
  install(FILES ${_hdr}
    DESTINATION "${gRPC_INSTALL_INCLUDEDIR}/${_path}"
  )
endforeach()


if(gRPC_INSTALL)
  install(TARGETS grpcpp_otel_plugin EXPORT gRPCTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()

if(gRPC_BUILD_TESTS)

add_executable(grpc_cli
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/reflection/v1alpha/reflection.grpc.pb.h
  test/cpp/util/cli_call.cc
  test/cpp/util/cli_credentials.cc
  test/cpp/util/grpc_cli.cc
  test/cpp/util/grpc_tool.cc
  test/cpp/util/proto_file_parser.cc
  test/cpp/util/proto_reflection_descriptor_database.cc
  test/cpp/util/service_describer.cc
)
if(WIN32 AND MSVC)
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(grpc_cli
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
endif()
target_compile_features(grpc_cli PUBLIC cxx_std_17)
target_include_directories(grpc_cli
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_cli
  ${_gRPC_ALLTARGETS_LIBRARIES}
  absl::fixed_array
  absl::overload
  absl::charset
  grpc++
  ${_gRPC_PROTOBUF_PROTOC_LIBRARIES}
  grpc++_test_config
)


endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_CPP_PLUGIN)

add_executable(grpc_cpp_plugin
  src/compiler/cpp_plugin.cc
)
target_compile_features(grpc_cpp_plugin PUBLIC cxx_std_17)
target_include_directories(grpc_cpp_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_cpp_plugin
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_cpp_plugin EXPORT gRPCPluginTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_CSHARP_PLUGIN)

add_executable(grpc_csharp_plugin
  src/compiler/csharp_plugin.cc
)
target_compile_features(grpc_csharp_plugin PUBLIC cxx_std_17)
target_include_directories(grpc_csharp_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_csharp_plugin
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_csharp_plugin EXPORT gRPCPluginTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_NODE_PLUGIN)

add_executable(grpc_node_plugin
  src/compiler/node_plugin.cc
)
target_compile_features(grpc_node_plugin PUBLIC cxx_std_17)
target_include_directories(grpc_node_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_node_plugin
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_node_plugin EXPORT gRPCPluginTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_OBJECTIVE_C_PLUGIN)

add_executable(grpc_objective_c_plugin
  src/compiler/objective_c_plugin.cc
)
target_compile_features(grpc_objective_c_plugin PUBLIC cxx_std_17)
target_include_directories(grpc_objective_c_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_objective_c_plugin
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_objective_c_plugin EXPORT gRPCPluginTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_PHP_PLUGIN)

add_executable(grpc_php_plugin
  src/compiler/php_plugin.cc
)
target_compile_features(grpc_php_plugin PUBLIC cxx_std_17)
target_include_directories(grpc_php_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_php_plugin
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_php_plugin EXPORT gRPCPluginTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_PYTHON_PLUGIN)

add_executable(grpc_python_plugin
  src/compiler/python_plugin.cc
)
target_compile_features(grpc_python_plugin PUBLIC cxx_std_17)
target_include_directories(grpc_python_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_python_plugin
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_python_plugin EXPORT gRPCPluginTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_CODEGEN AND gRPC_BUILD_GRPC_RUBY_PLUGIN)

add_executable(grpc_ruby_plugin
  src/compiler/ruby_plugin.cc
)
target_compile_features(grpc_ruby_plugin PUBLIC cxx_std_17)
target_include_directories(grpc_ruby_plugin
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(grpc_ruby_plugin
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc_plugin_support
)



if(gRPC_INSTALL)
  install(TARGETS grpc_ruby_plugin EXPORT gRPCPluginTargets
    RUNTIME DESTINATION ${gRPC_INSTALL_BINDIR}
    BUNDLE DESTINATION  ${gRPC_INSTALL_BINDIR}
    LIBRARY DESTINATION ${gRPC_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${gRPC_INSTALL_LIBDIR}
  )
endif()

endif()
if(gRPC_BUILD_TESTS)

add_executable(http2_client
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  test/cpp/interop/http2_client.cc
)
if(WIN32 AND MSVC)
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(http2_client
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
endif()
target_compile_features(http2_client PUBLIC cxx_std_17)
target_include_directories(http2_client
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(http2_client
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(interop_client
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  test/core/credentials/call/oauth2/oauth2_utils.cc
  test/cpp/interop/backend_metrics_lb_policy.cc
  test/cpp/interop/client.cc
  test/cpp/interop/client_helper.cc
  test/cpp/interop/interop_client.cc
)
if(WIN32 AND MSVC)
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(interop_client
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
endif()
target_compile_features(interop_client PUBLIC cxx_std_17)
target_include_directories(interop_client
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_client
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(interop_server
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/empty.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/test.grpc.pb.h
  src/cpp/server/orca/orca_service.cc
  test/cpp/interop/interop_server.cc
  test/cpp/interop/interop_server_bootstrap.cc
  test/cpp/interop/server_helper.cc
)
if(WIN32 AND MSVC)
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(interop_server
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
endif()
target_compile_features(interop_server PUBLIC cxx_std_17)
target_include_directories(interop_server
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(interop_server
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(qps_json_driver
  ${_gRPC_PROTO_GENS_DIR}/src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/core/ext/transport/chaotic_good/chaotic_good_frame.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/core/ext/transport/chaotic_good/chaotic_good_frame.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/latent_see.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/latent_see.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/latent_see.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/latent_see.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/property_list.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/property_list.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/property_list.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/property_list.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/report_qps_scenario_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.h
  src/core/ext/transport/chaotic_good/chaotic_good.cc
  src/core/ext/transport/chaotic_good/client/chaotic_good_connector.cc
  src/core/ext/transport/chaotic_good/client_transport.cc
  src/core/ext/transport/chaotic_good/control_endpoint.cc
  src/core/ext/transport/chaotic_good/data_endpoints.cc
  src/core/ext/transport/chaotic_good/frame.cc
  src/core/ext/transport/chaotic_good/frame_header.cc
  src/core/ext/transport/chaotic_good/scheduler.cc
  src/core/ext/transport/chaotic_good/send_rate.cc
  src/core/ext/transport/chaotic_good/server/chaotic_good_server.cc
  src/core/ext/transport/chaotic_good/server_transport.cc
  src/core/ext/transport/chaotic_good/tcp_frame_header.cc
  src/core/ext/transport/chaotic_good/tcp_frame_transport.cc
  src/core/ext/transport/chaotic_good/tcp_ztrace_collector.cc
  src/core/ext/transport/chaotic_good_legacy/client/chaotic_good_connector.cc
  src/core/ext/transport/chaotic_good_legacy/client_transport.cc
  src/core/ext/transport/chaotic_good_legacy/control_endpoint.cc
  src/core/ext/transport/chaotic_good_legacy/data_endpoints.cc
  src/core/ext/transport/chaotic_good_legacy/frame.cc
  src/core/ext/transport/chaotic_good_legacy/frame_header.cc
  src/core/ext/transport/chaotic_good_legacy/server/chaotic_good_server.cc
  src/core/ext/transport/chaotic_good_legacy/server_transport.cc
  src/core/ext/upb-gen/google/protobuf/any.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/duration.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/empty.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/timestamp.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/property_list.upb_minitable.c
  src/core/util/seq_bit_set.cc
  src/cpp/latent_see/latent_see_client.cc
  src/cpp/latent_see/latent_see_service.cc
  test/cpp/qps/benchmark_config.cc
  test/cpp/qps/client_async.cc
  test/cpp/qps/client_callback.cc
  test/cpp/qps/client_sync.cc
  test/cpp/qps/driver.cc
  test/cpp/qps/parse_json.cc
  test/cpp/qps/qps_json_driver.cc
  test/cpp/qps/qps_server_builder.cc
  test/cpp/qps/qps_worker.cc
  test/cpp/qps/report.cc
  test/cpp/qps/server_async.cc
  test/cpp/qps/server_callback.cc
  test/cpp/qps/server_sync.cc
  test/cpp/qps/usage_timer.cc
)
if(WIN32 AND MSVC)
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(qps_json_driver
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
endif()
target_compile_features(qps_json_driver PUBLIC cxx_std_17)
target_include_directories(qps_json_driver
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps_json_driver
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()
if(gRPC_BUILD_TESTS)

add_executable(qps_worker
  ${_gRPC_PROTO_GENS_DIR}/src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/core/ext/transport/chaotic_good/chaotic_good_frame.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/core/ext/transport/chaotic_good/chaotic_good_frame.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/core/ext/transport/chaotic_good/chaotic_good_frame.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/latent_see.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/latent_see.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/latent_see.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/latent_see.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/property_list.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/property_list.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/property_list.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/channelz/v2/property_list.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/benchmark_service.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/control.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/messages.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/payloads.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/stats.grpc.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.cc
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.pb.h
  ${_gRPC_PROTO_GENS_DIR}/src/proto/grpc/testing/worker_service.grpc.pb.h
  src/core/ext/transport/chaotic_good/chaotic_good.cc
  src/core/ext/transport/chaotic_good/client/chaotic_good_connector.cc
  src/core/ext/transport/chaotic_good/client_transport.cc
  src/core/ext/transport/chaotic_good/control_endpoint.cc
  src/core/ext/transport/chaotic_good/data_endpoints.cc
  src/core/ext/transport/chaotic_good/frame.cc
  src/core/ext/transport/chaotic_good/frame_header.cc
  src/core/ext/transport/chaotic_good/scheduler.cc
  src/core/ext/transport/chaotic_good/send_rate.cc
  src/core/ext/transport/chaotic_good/server/chaotic_good_server.cc
  src/core/ext/transport/chaotic_good/server_transport.cc
  src/core/ext/transport/chaotic_good/tcp_frame_header.cc
  src/core/ext/transport/chaotic_good/tcp_frame_transport.cc
  src/core/ext/transport/chaotic_good/tcp_ztrace_collector.cc
  src/core/ext/transport/chaotic_good_legacy/client/chaotic_good_connector.cc
  src/core/ext/transport/chaotic_good_legacy/client_transport.cc
  src/core/ext/transport/chaotic_good_legacy/control_endpoint.cc
  src/core/ext/transport/chaotic_good_legacy/data_endpoints.cc
  src/core/ext/transport/chaotic_good_legacy/frame.cc
  src/core/ext/transport/chaotic_good_legacy/frame_header.cc
  src/core/ext/transport/chaotic_good_legacy/server/chaotic_good_server.cc
  src/core/ext/transport/chaotic_good_legacy/server_transport.cc
  src/core/ext/upb-gen/google/protobuf/any.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/duration.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/empty.upb_minitable.c
  src/core/ext/upb-gen/google/protobuf/timestamp.upb_minitable.c
  src/core/ext/upb-gen/src/proto/grpc/channelz/v2/property_list.upb_minitable.c
  src/core/util/seq_bit_set.cc
  src/cpp/latent_see/latent_see_service.cc
  test/cpp/qps/client_async.cc
  test/cpp/qps/client_callback.cc
  test/cpp/qps/client_sync.cc
  test/cpp/qps/qps_server_builder.cc
  test/cpp/qps/qps_worker.cc
  test/cpp/qps/server_async.cc
  test/cpp/qps/server_callback.cc
  test/cpp/qps/server_sync.cc
  test/cpp/qps/usage_timer.cc
  test/cpp/qps/worker.cc
)
if(WIN32 AND MSVC)
  if(BUILD_SHARED_LIBS)
    target_compile_definitions(qps_worker
    PRIVATE
      "GPR_DLL_IMPORTS"
      "GRPC_DLL_IMPORTS"
      "GRPCXX_DLL_IMPORTS"
    )
  endif()
endif()
target_compile_features(qps_worker PUBLIC cxx_std_17)
target_include_directories(qps_worker
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${_gRPC_ADDRESS_SORTING_INCLUDE_DIR}
    ${_gRPC_RE2_INCLUDE_DIR}
    ${_gRPC_SSL_INCLUDE_DIR}
    ${_gRPC_UPB_GENERATED_DIR}
    ${_gRPC_UPB_GRPC_GENERATED_DIR}
    ${_gRPC_UPB_INCLUDE_DIR}
    ${_gRPC_XXHASH_INCLUDE_DIR}
    ${_gRPC_ZLIB_INCLUDE_DIR}
    ${_gRPC_PROTO_GENS_DIR}
)

target_link_libraries(qps_worker
  ${_gRPC_ALLTARGETS_LIBRARIES}
  grpc++_test_config
  grpc++_test_util
)


endif()







if(gRPC_INSTALL)
  install(EXPORT gRPCTargets
    DESTINATION ${gRPC_INSTALL_CMAKEDIR}
    NAMESPACE gRPC::
  )
  if(gRPC_BUILD_CODEGEN)
    install(EXPORT gRPCPluginTargets
      DESTINATION ${gRPC_INSTALL_CMAKEDIR}
      NAMESPACE gRPC::
    )
  endif()
endif()

include(CMakePackageConfigHelpers)

configure_file(cmake/gRPCConfig.cmake.in
  gRPCConfig.cmake @ONLY)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/gRPCConfigVersion.cmake
  VERSION ${gRPC_CPP_VERSION}
  COMPATIBILITY AnyNewerVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/gRPCConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/gRPCConfigVersion.cmake
  DESTINATION ${gRPC_INSTALL_CMAKEDIR}
)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/Findc-ares.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/Findre2.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules/Findsystemd.cmake
  DESTINATION ${gRPC_INSTALL_CMAKEDIR}/modules
)

install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/etc/roots.pem
  DESTINATION ${gRPC_INSTALL_SHAREDIR})

# Function to generate pkg-config files.
function(generate_pkgconfig name description version requires requires_private
                            libs libs_private output_filename)
  set(PC_NAME "${name}")
  set(PC_DESCRIPTION "${description}")
  set(PC_VERSION "${version}")
  set(PC_REQUIRES "${requires}")
  set(PC_REQUIRES_PRIVATE "${requires_private}")
  set(PC_LIB "${libs}")
  set(PC_LIBS_PRIVATE "${libs_private}")
  set(output_filepath "${grpc_BINARY_DIR}/libs/opt/pkgconfig/${output_filename}")
  configure_file(
    "${grpc_SOURCE_DIR}/cmake/pkg-config-template.pc.in"
    "${output_filepath}"
    @ONLY)
  install(FILES "${output_filepath}"
    DESTINATION "${gRPC_INSTALL_LIBDIR}/pkgconfig")
endfunction()

# gpr .pc file
generate_pkgconfig(
  "gpr"
  "gRPC platform support library"
  "${gRPC_CORE_VERSION}"
  "absl_any_invocable absl_base absl_bind_front absl_bits absl_check absl_cleanup absl_config absl_cord absl_core_headers absl_flags absl_flags_marshalling absl_log absl_log_globals absl_log_severity absl_memory absl_optional absl_random_random absl_status absl_str_format absl_strings absl_synchronization absl_time"
  ""
  "-lgpr"
  ""
  "gpr.pc")

# grpc .pc file
generate_pkgconfig(
  "gRPC"
  "high performance general RPC framework"
  "${gRPC_CORE_VERSION}"
  "absl_algorithm_container absl_any_invocable absl_base absl_bind_front absl_bits absl_btree absl_check absl_cleanup absl_config absl_cord absl_core_headers absl_flags absl_flags_marshalling absl_flat_hash_map absl_flat_hash_set absl_function_ref absl_hash absl_inlined_vector absl_log absl_log_globals absl_log_severity absl_memory absl_no_destructor absl_node_hash_map absl_optional absl_random_bit_gen_ref absl_random_distributions absl_random_random absl_span absl_status absl_statusor absl_str_format absl_string_view absl_strings absl_synchronization absl_time absl_type_traits absl_utility gpr"
  "libcares openssl re2 zlib"
  "-lgrpc"
  "-laddress_sorting -lupb_textformat_lib -lupb_json_lib -lupb_reflection_lib -lupb_wire_lib -lupb_message_lib -lutf8_range_lib -lupb_mini_descriptor_lib -lupb_mini_table_lib -lupb_hash_lib -lupb_mem_lib -lupb_base_lib -lupb_lex_lib"
  "grpc.pc")

# grpc_unsecure .pc file
generate_pkgconfig(
  "gRPC unsecure"
  "high performance general RPC framework without SSL"
  "${gRPC_CORE_VERSION}"
  "absl_algorithm_container absl_any_invocable absl_base absl_bind_front absl_bits absl_btree absl_check absl_cleanup absl_config absl_cord absl_core_headers absl_flags absl_flags_marshalling absl_flat_hash_map absl_flat_hash_set absl_function_ref absl_hash absl_inlined_vector absl_log absl_log_globals absl_log_severity absl_memory absl_no_destructor absl_node_hash_map absl_optional absl_random_bit_gen_ref absl_random_distributions absl_random_random absl_span absl_status absl_statusor absl_str_format absl_string_view absl_strings absl_synchronization absl_time absl_type_traits absl_utility gpr"
  "libcares zlib"
  "-lgrpc_unsecure"
  "-laddress_sorting -lupb_textformat_lib -lupb_reflection_lib -lupb_wire_lib -lupb_message_lib -lutf8_range_lib -lupb_mini_descriptor_lib -lupb_mini_table_lib -lupb_hash_lib -lupb_mem_lib -lupb_base_lib -lupb_lex_lib"
  "grpc_unsecure.pc")

# grpc++ .pc file
generate_pkgconfig(
  "gRPC++"
  "C++ wrapper for gRPC"
  "${gRPC_CPP_VERSION}"
  "absl_absl_check absl_absl_log absl_algorithm_container absl_any_invocable absl_base absl_bind_front absl_bits absl_btree absl_check absl_cleanup absl_config absl_cord absl_core_headers absl_dynamic_annotations absl_flags absl_flags_marshalling absl_flat_hash_map absl_flat_hash_set absl_function_ref absl_hash absl_inlined_vector absl_layout absl_log absl_log_globals absl_log_severity absl_memory absl_no_destructor absl_node_hash_map absl_optional absl_prefetch absl_random_bit_gen_ref absl_random_distributions absl_random_random absl_span absl_status absl_statusor absl_str_format absl_string_view absl_strings absl_strings_internal absl_synchronization absl_time absl_type_traits absl_utility gpr grpc"
  "libcares openssl re2 zlib"
  "-lgrpc++"
  "-laddress_sorting -lupb_textformat_lib -lupb_json_lib -lupb_reflection_lib -lupb_wire_lib -lupb_message_lib -lutf8_range_lib -lupb_mini_descriptor_lib -lupb_mini_table_lib -lupb_hash_lib -lupb_mem_lib -lupb_base_lib -lupb_lex_lib"
  "grpc++.pc")

# grpc++_unsecure .pc file
generate_pkgconfig(
  "gRPC++ unsecure"
  "C++ wrapper for gRPC without SSL"
  "${gRPC_CPP_VERSION}"
  "absl_absl_check absl_absl_log absl_algorithm_container absl_any_invocable absl_base absl_bind_front absl_bits absl_btree absl_check absl_cleanup absl_config absl_cord absl_core_headers absl_dynamic_annotations absl_flags absl_flags_marshalling absl_flat_hash_map absl_flat_hash_set absl_function_ref absl_hash absl_inlined_vector absl_layout absl_log absl_log_globals absl_log_severity absl_memory absl_no_destructor absl_node_hash_map absl_optional absl_prefetch absl_random_bit_gen_ref absl_random_distributions absl_random_random absl_span absl_status absl_statusor absl_str_format absl_string_view absl_strings absl_strings_internal absl_synchronization absl_time absl_type_traits absl_utility gpr grpc_unsecure"
  "libcares zlib"
  "-lgrpc++_unsecure"
  "-laddress_sorting -lupb_textformat_lib -lupb_reflection_lib -lupb_wire_lib -lupb_message_lib -lutf8_range_lib -lupb_mini_descriptor_lib -lupb_mini_table_lib -lupb_hash_lib -lupb_mem_lib -lupb_base_lib -lupb_lex_lib"
  "grpc++_unsecure.pc")

if(gRPC_BUILD_GRPCPP_OTEL_PLUGIN)
  # grpcpp_otel_plugin .pc file
  generate_pkgconfig(
    "gRPC++ OpenTelemetry Plugin"
    "OpenTelemetry Plugin for gRPC C++"
    "${gRPC_CPP_VERSION}"
    "absl_absl_check absl_absl_log absl_algorithm_container absl_any_invocable absl_base absl_bind_front absl_bits absl_btree absl_check absl_cleanup absl_config absl_cord absl_core_headers absl_dynamic_annotations absl_flags absl_flags_marshalling absl_flat_hash_map absl_flat_hash_set absl_function_ref absl_hash absl_inlined_vector absl_layout absl_log absl_log_globals absl_log_severity absl_memory absl_no_destructor absl_node_hash_map absl_optional absl_prefetch absl_random_bit_gen_ref absl_random_distributions absl_random_random absl_span absl_status absl_statusor absl_str_format absl_string_view absl_strings absl_strings_internal absl_synchronization absl_time absl_type_traits absl_utility gpr grpc grpc++ opentelemetry_api"
    "libcares openssl re2 zlib"
    "-lgrpcpp_otel_plugin"
    "-laddress_sorting -lupb_textformat_lib -lupb_json_lib -lupb_reflection_lib -lupb_wire_lib -lupb_message_lib -lutf8_range_lib -lupb_mini_descriptor_lib -lupb_mini_table_lib -lupb_hash_lib -lupb_mem_lib -lupb_base_lib -lupb_lex_lib"
    "grpcpp_otel_plugin.pc")
endif()
