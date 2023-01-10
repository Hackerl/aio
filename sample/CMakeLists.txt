if (NOT DISABLE_SSL)
    add_executable(aio_ws ws/main.cpp)
    target_link_libraries(aio_ws aio)

    add_executable(aio_ssl ssl/main.cpp)
    target_link_libraries(aio_ssl aio)
endif ()

add_executable(aio_tcp tcp/main.cpp)
target_link_libraries(aio_tcp aio)

add_executable(aio_http http/main.cpp)
target_link_libraries(aio_http aio)