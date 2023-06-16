find_package(Catch2 CONFIG REQUIRED)

add_executable(
        aio_test
        thread.cpp
        channel.cpp
        ev/pipe.cpp
        ev/buffer.cpp
        http/url.cpp
        net/stream.cpp
        $<$<PLATFORM_ID:Linux>:ev/signal.cpp>
        $<$<PLATFORM_ID:Windows>:main.cpp>
)

target_link_libraries(aio_test PRIVATE aio $<IF:$<PLATFORM_ID:Windows>,Catch2::Catch2,Catch2::Catch2WithMain>)