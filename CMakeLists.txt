cmake_minimum_required(VERSION 3.17)
project(aio)

set(CMAKE_CXX_STANDARD 17)

option(AIO_DISABLE_SSL "disable ssl support" OFF)
option(AIO_DISABLE_TESTS "disable aio unit test" OFF)
option(AIO_DISABLE_SAMPLES "disable aio samples" OFF)
option(AIO_EMBED_CA_CERT "use embedded CA certificates" OFF)

set(CMAKE_POSITION_INDEPENDENT_CODE TRUE)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

set(AIO_VERSION 1.0.2)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(zero CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(Libevent CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_library(
        aio
        STATIC
        src/context.cpp
        src/ev/buffer.cpp
        src/ev/pipe.cpp
        src/ev/event.cpp
        src/ev/timer.cpp
        src/ev/signal.cpp
        src/net/stream.cpp
        src/http/url.cpp
        src/http/request.cpp
        $<$<NOT:$<BOOL:${AIO_DISABLE_SSL}>>:src/net/ssl.cpp>
        $<$<NOT:$<BOOL:${AIO_DISABLE_SSL}>>:src/http/websocket.cpp>
)

if (AIO_EMBED_CA_CERT)
    target_compile_definitions(aio PUBLIC AIO_EMBED_CA_CERT)
endif ()

target_include_directories(
        aio
        PUBLIC
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(
        aio
        PUBLIC
        zero::zero
        CURL::libcurl
        libevent::core
        libevent::extra
        nlohmann_json::nlohmann_json
        $<$<PLATFORM_ID:Linux,Android>:libevent::pthreads>
        $<$<NOT:$<BOOL:${AIO_DISABLE_SSL}>>:libevent::openssl>
)

if (AIO_DISABLE_SSL)
    set(
            EXCLUDE_HEADERS
            ${EXCLUDE_HEADERS}
            PATTERN "net/ssl.h" EXCLUDE
            PATTERN "http/websocket.h" EXCLUDE
    )
endif ()

install(
        DIRECTORY
        include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        ${EXCLUDE_HEADERS}
)

install(
        TARGETS aio
        EXPORT ${PROJECT_NAME}Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(
        EXPORT ${PROJECT_NAME}Targets
        NAMESPACE aio::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

configure_package_config_file(
        cmake/${PROJECT_NAME}-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        VERSION ${AIO_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

if (NOT AIO_DISABLE_SAMPLES)
    add_subdirectory(sample)
endif ()

if (NOT AIO_DISABLE_TESTS)
    add_subdirectory(test)
endif ()