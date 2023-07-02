find_package(Catch2 CONFIG REQUIRED)

add_executable(
        aio_test
        thread.cpp
        channel.cpp
        ev/pipe.cpp
        ev/event.cpp
        ev/timer.cpp
        ev/signal.cpp
        ev/buffer.cpp
        http/url.cpp
        http/request.cpp
        net/dns.cpp
        net/net.cpp
        net/stream.cpp
        net/dgram.cpp
        $<$<NOT:$<BOOL:${AIO_DISABLE_SSL}>>:net/ssl.cpp>
        $<$<PLATFORM_ID:Windows>:main.cpp>
)

target_link_libraries(aio_test PRIVATE aio $<IF:$<PLATFORM_ID:Windows>,Catch2::Catch2,Catch2::Catch2WithMain>)