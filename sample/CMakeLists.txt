if (NOT DISABLE_SSL)
    add_executable(aio_ws ws/main.cpp)
    target_link_libraries(aio_ws aio)
endif ()