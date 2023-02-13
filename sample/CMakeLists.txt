if (NOT AIO_DISABLE_SSL)
    add_executable(aio_ws ws/main.cpp)
    target_link_libraries(aio_ws aio)

    if (NOT WIN32)
        add_executable(aio_ssl_client ssl/client.cpp)
        target_link_libraries(aio_ssl_client aio)

        add_executable(aio_ssl_server ssl/server.cpp)
        target_link_libraries(aio_ssl_server aio)
    endif ()
endif ()

if (NOT WIN32)
    add_executable(aio_tcp_client tcp/client.cpp)
    target_link_libraries(aio_tcp_client aio)

    add_executable(aio_tcp_server tcp/server.cpp)
    target_link_libraries(aio_tcp_server aio)
endif ()

add_executable(aio_http http/main.cpp)
target_link_libraries(aio_http aio)