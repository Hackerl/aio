#include <aio/ev/pipe.h>

aio::ev::PairedBuffer::PairedBuffer(bufferevent *bev) : Buffer(bev) {

}

aio::ev::PairedBuffer::~PairedBuffer() {
    if (mClosed)
        return;

    bufferevent_flush(mBev, EV_WRITE, BEV_FINISHED);
}

void aio::ev::PairedBuffer::close() {
    bufferevent_flush(mBev, EV_WRITE, BEV_FINISHED);
    Buffer::close();
}

std::string aio::ev::PairedBuffer::getError() {
    return mError;
}

void aio::ev::PairedBuffer::throws(std::string_view error) {
    mError = error;

    if (std::shared_ptr<PairedBuffer> buffer = mPartner.lock()) {
        buffer->mError = error;
        bufferevent_trigger_event(buffer->mBev, BEV_EVENT_ERROR, BEV_OPT_DEFER_CALLBACKS);
    }

    bufferevent_trigger_event(mBev, BEV_EVENT_ERROR, 0);
}

std::array<std::shared_ptr<aio::ev::IPairedBuffer>, 2> aio::ev::pipe(const std::shared_ptr<Context> &context) {
    bufferevent *pair[2];

    if (bufferevent_pair_new(context->base(), BEV_OPT_DEFER_CALLBACKS, pair) < 0)
        return {nullptr, nullptr};

    std::shared_ptr<aio::ev::PairedBuffer> buffers[2] = {
            std::make_shared<PairedBuffer>(pair[0]),
            std::make_shared<PairedBuffer>(pair[1])
    };

    /*
     * The data will be transmitted to the peer immediately,
     * so the read operation of the peer will return immediately without event callback,
     * and a delayed read event will be triggered when the event loop is running.
     * */
    evbuffer_defer_callbacks(bufferevent_get_output(pair[0]), context->base());
    evbuffer_defer_callbacks(bufferevent_get_output(pair[1]), context->base());

    buffers[0]->mPartner = buffers[1];
    buffers[1]->mPartner = buffers[0];

    return {buffers[0], buffers[1]};
}