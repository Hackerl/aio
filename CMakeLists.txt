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

set(JSON_BuildTests OFF CACHE BOOL "disable json library tests")

include(FindOpenSSL)
include(FetchContent)

FetchContent_Declare(
        zero
        GIT_REPOSITORY https://github.com/Hackerl/zero
        GIT_TAG 36592c4db8005419153c6f95a2614f81ff62adcb
)

FetchContent_MakeAvailable(zero)

FetchContent_Declare(
        libcurl
        GIT_REPOSITORY https://github.com/curl/curl
        GIT_TAG curl-7_85_0
)

FetchContent_MakeAvailable(libcurl)

FetchContent_Declare(
        libevent
        GIT_REPOSITORY https://github.com/libevent/libevent
        GIT_TAG release-2.1.12-stable
)

FetchContent_MakeAvailable(libevent)

FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json
        GIT_TAG v3.11.2
)

FetchContent_MakeAvailable(json)

add_library(
        aio
        src/ev/buffer.cpp
        src/ev/pipe.cpp
        src/ev/event.cpp
        src/ev/timer.cpp
        src/net/ssl.cpp
        src/net/stream.cpp
        src/http/url.cpp
        src/http/request.cpp
        src/http/websocket.cpp
)

target_include_directories(aio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(
        aio
        zero
        libcurl
        nlohmann_json
        event_core_static
        event_openssl_static
        event_extra_static
        OpenSSL::SSL
        OpenSSL::Crypto
)