find_package(Catch2 CONFIG REQUIRED)

add_executable(
        aio_test
        thread.cpp
        channel.cpp
        ev/pipe.cpp
        http/url.cpp
        $<$<PLATFORM_ID:Linux>:ev/signal.cpp>
)

target_link_libraries(aio_test PRIVATE aio Catch2::Catch2WithMain)