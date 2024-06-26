#include "cfgo/track.hpp"
#include "impl/track.hpp"

namespace cfgo
{
    Track::Track(std::nullptr_t state): ImplBy(std::shared_ptr<impl::Track>(nullptr)) {}
    Track::Track(const msg_ptr & msg, int cache_capicity): ImplBy<impl::Track>(msg, cache_capicity) {}

    const std::string& Track::type() const noexcept {
        return impl()->type;
    }
    const std::string& Track::pub_id() const noexcept {
        return impl()->pubId;
    }
    const std::string& Track::global_id() const noexcept {
        return impl()->globalId;
    }
    const std::string& Track::bind_id() const noexcept {
        return impl()->bindId;
    }
    const std::string& Track::rid() const noexcept {
        return impl()->rid;
    }
    const std::string& Track::stream_id() const noexcept {
        return impl()->streamId;
    }
    std::map<std::string, std::string> & Track::labels() noexcept {
        return impl()->labels;
    }
    const std::map<std::string, std::string> & Track::labels() const noexcept {
        return impl()->labels;
    }
    std::shared_ptr<rtc::Track> & Track::track() noexcept {
        return impl()->track;
    }
    const std::shared_ptr<rtc::Track> & Track::track() const noexcept {
        return impl()->track;
    }
    auto Track::await_open_or_closed(const close_chan & close_ch) const -> asio::awaitable<bool>
    {
        return impl()->await_open_or_closed(close_ch);
    }
    auto Track::await_msg(MsgType msg_type, const close_chan &  close_ch) const -> asio::awaitable<MsgPtr>
    {
        return impl()->await_msg(msg_type, close_ch);
    }
    Track::MsgPtr Track::receive_msg(MsgType msg_type) const {
        return impl()->receive_msg(msg_type);
    }
    void * Track::get_gst_caps(int pt) const
    {
        return impl()->get_gst_caps(pt);
    }
    void Track::set_on_data(const OnDataCb & cb) const
    {
        impl()->set_on_data(cb);
    }
    void Track::set_on_data(OnDataCb && cb) const
    {
        impl()->set_on_data(std::move(cb));
    }
    void Track::unset_on_data() const noexcept
    {
        impl()->unset_on_data();
    }
    void Track::set_on_stat(const OnStatCb & cb) const
    {
        impl()->set_on_stat(cb);
    }
    void Track::set_on_stat(OnStatCb && cb) const
    {
        impl()->set_on_stat(std::move(cb));
    }
    void Track::unset_on_stat() const noexcept
    {
        impl()->unset_on_stat();
    }
    std::uint64_t Track::get_rtp_drops_bytes() const noexcept
    {
        return impl()->get_rtp_drops_bytes();
    }
    std::uint32_t Track::get_rtp_drops_packets() const noexcept
    {
        return impl()->get_rtp_drops_packets();
    }
    std::uint64_t Track::get_rtp_receives_bytes() const noexcept
    {
        return impl()->get_rtp_receives_bytes();
    }
    std::uint32_t Track::get_rtp_receives_packets() const noexcept
    {
        return impl()->get_rtp_receives_packets();
    }
    float Track::get_rtp_drop_bytes_rate() const noexcept
    {
        return impl()->get_rtp_drop_bytes_rate();
    }
    float Track::get_rtp_drop_packets_rate() const noexcept
    {
        return impl()->get_rtp_drop_packets_rate();
    }
    std::uint32_t Track::get_rtp_packet_mean_size() const noexcept
    {
        return impl()->get_rtp_packet_mean_size();
    }
    void Track::reset_rtp_data() const noexcept
    {
        impl()->reset_rtp_data();
    }
    std::uint64_t Track::get_rtcp_drops_bytes() const noexcept
    {
        return impl()->get_rtcp_drops_bytes();
    }
    std::uint32_t Track::get_rtcp_drops_packets() const noexcept
    {
        return impl()->get_rtcp_drops_packets();
    }
    std::uint64_t Track::get_rtcp_receives_bytes() const noexcept
    {
        return impl()->get_rtcp_receives_bytes();
    }
    std::uint32_t Track::get_rtcp_receives_packets() const noexcept
    {
        return impl()->get_rtcp_receives_packets();
    }
    float Track::get_rtcp_drop_bytes_rate() const noexcept
    {
        return impl()->get_rtcp_drop_bytes_rate();
    }
    float Track::get_rtcp_drop_packets_rate() const noexcept
    {
        return impl()->get_rtcp_drop_packets_rate();
    }
    std::uint32_t Track::get_rtcp_packet_mean_size() const noexcept
    {
        return impl()->get_rtcp_packet_mean_size();
    }
    void Track::reset_rtcp_data() const noexcept
    {
        impl()->reset_rtcp_data();
    }
    float Track::get_drop_bytes_rate() const noexcept
    {
        return impl()->get_drop_bytes_rate();
    }
    float Track::get_drop_packets_rate() const noexcept
    {
        return impl()->get_drop_packets_rate();
    }

} // namespace cfgo
