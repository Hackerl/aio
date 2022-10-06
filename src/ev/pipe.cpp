#include <aio/ev/pipe.h>

aio::ev::PairedBuffer::PairedBuffer(bufferevent *bev) : Buffer(bev) {

}

void aio::ev::PairedBuffer::close() {
    bufferevent_flush(mBev, EV_WRITE, BEV_FINISHED);
    Buffer::close();
}

std::array<std::shared_ptr<aio::ev::IBuffer>, 2> aio::ev::pipe(const Context &context) {
    bufferevent *pair[2];
    bufferevent_pair_new(context.base, BEV_OPT_DEFER_CALLBACKS, pair);

    return {std::make_shared<PairedBuffer>(pair[0]), std::make_shared<PairedBuffer>(pair[1])};
}