#include <aio/ev/pipe.h>

aio::ev::PairedBuffer::PairedBuffer(bufferevent *bev, std::shared_ptr<std::string> error)
        : Buffer(bev), mError(std::move(error)) {

}

aio::ev::PairedBuffer::~PairedBuffer() {
    if (mClosed)
        return;

    bufferevent_flush(mBev, EV_WRITE, BEV_FINISHED);
}

nonstd::expected<void, aio::Error> aio::ev::PairedBuffer::close() {
    bufferevent_flush(mBev, EV_WRITE, BEV_FINISHED);
    return Buffer::close();
}

std::string aio::ev::PairedBuffer::getError() {
    return *mError;
}

void aio::ev::PairedBuffer::throws(std::string_view error) {
    *mError = error;

    if (bufferevent *buffer = bufferevent_pair_get_partner(mBev))
        bufferevent_trigger_event(buffer, BEV_EVENT_ERROR, BEV_OPT_DEFER_CALLBACKS);

    bufferevent_trigger_event(mBev, BEV_EVENT_ERROR, 0);
}

std::array<zero::ptr::RefPtr<aio::ev::IPairedBuffer>, 2> aio::ev::pipe(const std::shared_ptr<Context> &context) {
    bufferevent *pair[2];

    if (bufferevent_pair_new(context->base(), BEV_OPT_DEFER_CALLBACKS, pair) < 0)
        return {nullptr, nullptr};

    std::shared_ptr<std::string> error = std::make_shared<std::string>();

    zero::ptr::RefPtr<aio::ev::PairedBuffer> buffers[2] = {
            zero::ptr::makeRef<PairedBuffer>(pair[0], error),
            zero::ptr::makeRef<PairedBuffer>(pair[1], error)
    };

    /*
     * The data will be transmitted to the peer immediately,
     * so the read operation of the peer will return immediately without event callback,
     * and a delayed read event will be triggered when the event loop is running.
     * */
    evbuffer_defer_callbacks(bufferevent_get_output(pair[0]), context->base());
    evbuffer_defer_callbacks(bufferevent_get_output(pair[1]), context->base());

    return {buffers[0], buffers[1]};
}