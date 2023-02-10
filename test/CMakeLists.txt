include(FetchContent)

FetchContent_Declare(
        catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2
        GIT_TAG v3.2.0
)

FetchContent_MakeAvailable(catch2)

add_executable(
        aio_test
        ev/pipe.cpp
        ev/signal.cpp
        http/url.cpp
        $<$<NOT:$<BOOL:${ANDROID}>>:sync/channel.cpp>
)

target_link_libraries(aio_test aio Catch2::Catch2WithMain)