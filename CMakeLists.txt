cmake_minimum_required(VERSION 3.17)
project(aio)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

set(EVENT__LIBRARY_TYPE STATIC CACHE STRING "disable shared library")
set(EVENT__DISABLE_TESTS ON CACHE BOOL "disable event tests")
set(EVENT__DISABLE_SAMPLES ON CACHE BOOL "disable event samples")
set(EVENT__DISABLE_BENCHMARK ON CACHE BOOL "disable event benchmark")

set(CURL_DISABLE_TESTS ON CACHE BOOL "disable curl tests")
set(BUILD_CURL_EXE OFF CACHE BOOL "disable curl executable")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "disable curl shared library")

include(FindOpenSSL)
include(FetchContent)

FetchContent_Declare(
        zero
        GIT_REPOSITORY https://github.com/Hackerl/zero
        GIT_TAG 8ca7ca2c7d98dc2a69cbcb4af8a305df49d814cf
)

FetchContent_MakeAvailable(zero)

FetchContent_Declare(
        libcurl
        GIT_REPOSITORY https://github.com/curl/curl
        GIT_TAG curl-7_82_0
)

FetchContent_MakeAvailable(libcurl)

FetchContent_Declare(
        libevent
        GIT_REPOSITORY https://github.com/libevent/libevent
        GIT_TAG release-2.1.12-stable
)

FetchContent_MakeAvailable(libevent)

add_library(aio src/ev/buffer.cpp src/ev/pipe.cpp src/ev/timer.cpp src/http/request.cpp)

target_include_directories(aio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(aio zero libcurl event_core_static event_openssl_static event_extra_static OpenSSL::SSL OpenSSL::Crypto)

add_executable(test main.cpp)
target_link_libraries(test aio)