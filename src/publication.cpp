#include "cfgo/publication.hpp"
#include "cfgo/track.hpp"
#include "cfgo/str_helper.hpp"
#include "cfgo/video/media_profile.hpp"
#include "cfgo/video/h264_profile_level_id.hpp"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <random>
#include <chrono>
#include <limits>

#include "boost/uuid/uuid_io.hpp"
#include "boost/uuid/uuid_generators.hpp"

template<>
struct std::hash<const AVCodecID>
{
    std::size_t operator()(const AVCodecID & k) const
    {
        using std::hash;
        return hash<int>()(static_cast<int>(k));
    }
};

namespace cfgo
{
    namespace impl
    {
        const int g_signaling_media_id_length = 16;
        const char g_signaling_media_id_valid_char[] =
            "0123456789"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz";

        /*
        * Sets the maximum size for a video fragment. Effective range is
        * 576-1470, with a lower value equating to more packets created,
        * but also better network compatability.
        */
        static uint16_t MAX_VIDEO_FRAGMENT_SIZE = 1400;

        std::string gen_signaling_media_id(std::mt19937 & gen)
        {
            std::string res;
            res.reserve(g_signaling_media_id_length);
            std::uniform_int_distribution<uint32_t> distrib(0, sizeof(g_signaling_media_id_valid_char) - 1);
            for (int i = 0; i < g_signaling_media_id_length; ++i) {
                res += g_signaling_media_id_valid_char[distrib(gen)];
            }
            return res;
        }

        const char * g_audio_mid = "0";
        const char * g_video_mid = "1";

        struct SupportedCodec {
            int order;
            AVCodecID codec_id;
            const char * profile;
        };

        template<typename K, typename V>
        std::unordered_map<V, K> map_back(const std::unordered_map<K, V> & map)
        {
            std::unordered_map<V, K> res {};
            for (auto & [key, value] : map)
            {
                res[value] = key;
            }
            return res;
        }

        const std::unordered_map<std::string, const AVCodecID> g_name_to_codec_id = {
            {"VP8", AV_CODEC_ID_VP8},
            {"H264", AV_CODEC_ID_H264},
            {"VP9", AV_CODEC_ID_VP9},
            {"opus", AV_CODEC_ID_OPUS},
            {"PCMU", AV_CODEC_ID_PCM_MULAW},
            {"PCMA", AV_CODEC_ID_PCM_ALAW},
        };
        const std::unordered_map<const AVCodecID, std::string> g_codec_id_to_name = map_back(g_name_to_codec_id);
        const std::map<int, SupportedCodec> g_supported_video_codecs = {
            {98,  { .order = 1, .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f" }},
            {100, { .order = 2, .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f" }},
            {102, { .order = 3, .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f" }},
            {104, { .order = 4, .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=64001f" }},
            {106, { .order = 5, .codec_id = AV_CODEC_ID_VP8, .profile = "" }},
            {108, { .order = 7, .codec_id = AV_CODEC_ID_VP9, .profile = "profile-id=0" }},
            {110, { .order = 8, .codec_id = AV_CODEC_ID_VP9, .profile = "profile-id=2" }},
        };
        const std::map<int, SupportedCodec> g_supported_audio_codecs = {
            {111, { .order = 1, .codec_id = AV_CODEC_ID_OPUS, .profile = "" }},
            {0,   { .order = 2, .codec_id = AV_CODEC_ID_PCM_MULAW, .profile = "" }},
            {8,   { .order = 3, .codec_id = AV_CODEC_ID_PCM_ALAW, .profile = "" }},
        };

        std::shared_ptr<rtc::RtpPacketizer> get_rtp_packetizer_for(AVCodecID codec, uint32_t ssrc, const std::string & cname, int pt, int clock_rate, int pkg_size)
        {
            auto rtp_config = std::make_shared<rtc::RtpPacketizationConfig>(ssrc, cname, pt, clock_rate);
            switch (codec)
            {
            case AV_CODEC_ID_H264:
                return std::make_shared<rtc::H264RtpPacketizer>(rtc::H264RtpPacketizer::Separator::StartSequence, rtp_config, pkg_size);
            case AV_CODEC_ID_H265:
                return std::make_shared<rtc::H265RtpPacketizer>(rtc::H265RtpPacketizer::Separator::StartSequence, rtp_config, pkg_size);
            case AV_CODEC_ID_AV1:
                return std::make_shared<rtc::AV1RtpPacketizer>(rtc::AV1RtpPacketizer::Packetization::TemporalUnit, rtp_config, pkg_size);
            case AV_CODEC_ID_OPUS:
            case AV_CODEC_ID_AAC:
            case AV_CODEC_ID_PCM_MULAW:
            case AV_CODEC_ID_PCM_ALAW:
                return std::make_shared<rtc::RtpPacketizer>(rtp_config);
            default:
                return nullptr;
            }
        }

        rtc::Description::Video create_video(std::string stream_id, std::string cname, uint32_t ssrc)
        {
            auto video = rtc::Description::Video(g_video_mid);
            std::vector<std::pair<int, SupportedCodec>> video_codecs(g_supported_video_codecs.begin(), g_supported_video_codecs.end());
            std::sort(video_codecs.begin(), video_codecs.end(), [] (const auto & lhs, const auto & rhs) {
                return lhs.second.order < rhs.second.order;
            });
            // from pion payload types.
            for (auto & [pt, codec] : video_codecs)
            {
                if (codec.profile[0] == '\0')
                {
                    video.addVideoCodec(pt, g_codec_id_to_name.at(codec.codec_id));
                }
                else
                {
                    video.addVideoCodec(pt, g_codec_id_to_name.at(codec.codec_id), codec.profile);
                }
            }
            auto track_id = stream_id + "-video";
            video.addSSRC(ssrc, std::move(cname), std::move(stream_id), std::move(track_id));
            return video;
        }

        rtc::Description::Audio create_audio(std::string stream_id, std::string cname, uint32_t ssrc)
        {
            auto audio = rtc::Description::Audio(g_audio_mid);
            std::vector<std::pair<int, SupportedCodec>> audio_codecs(g_supported_audio_codecs.begin(), g_supported_audio_codecs.end());
            std::sort(audio_codecs.begin(), audio_codecs.end(), [] (const auto & lhs, const auto & rhs) {
                return lhs.second.order < rhs.second.order;
            });
            for (auto & [pt, codec] : audio_codecs)
            {
                if (codec.profile[0] == '\0')
                {
                    audio.addAudioCodec(pt, g_codec_id_to_name.at(codec.codec_id));
                }
                else
                {
                    audio.addAudioCodec(pt, g_codec_id_to_name.at(codec.codec_id), codec.profile);
                }
            }
            auto track_id = stream_id + "-audio";
            audio.addSSRC(ssrc, std::move(cname), std::move(stream_id), std::move(track_id));
            return audio;
        }

        struct Publication;

        struct Publication : public std::enable_shared_from_this<Publication>
        {
            std::vector<cfgo::Track> m_tracks;
            std::vector<video::media_receiver_ptr_t> m_receivers;
            int m_binded {0};
            video::media_source_ptr_t m_src;
            Labels m_labels;
            std::unordered_set<uint32_t> m_ssrcs;
            std::mt19937 m_gen;
            close_chan m_closer;
            bool m_has_setup = false;
            bool m_has_start = false;
            int m_width = 0;
            int m_height = 0;
            int m_fps = 0;
            int64_t m_bit_rate = 0;
            bool m_prefer_packetizer = true;
            std::exception_ptr m_err;

            uint32_t gen_ssrc()
            {
                std::uniform_int_distribution<uint32_t> distrib(3000, std::numeric_limits<uint32_t>::max());
                auto ssrc = distrib(m_gen);
                while (m_ssrcs.contains(ssrc))
                {
                    ssrc = distrib(m_gen);
                }
                m_ssrcs.insert(ssrc);
                return ssrc;
            }

            Publication(video::media_source_ptr_t media_source, Labels labels)
            : m_src(std::move(media_source)), m_labels(std::move(labels)), m_gen(std::chrono::high_resolution_clock::now().time_since_epoch().count())
            {}

            int & width()
            {
                return m_width;
            }
            int width() const
            {
                return m_width;
            }
            int & height()
            {
                return m_height;
            }
            int height() const
            {
                return m_height;
            }
            int & fps()
            {
                return m_fps;
            }
            int fps() const
            {
                return m_fps;
            }
            int64_t & bit_rate()
            {
                return m_bit_rate;
            }
            int64_t bit_rate() const
            {
                return m_bit_rate;
            }

            std::shared_ptr<rtc::Track> create_track(rtc::PeerConnection & peer, const rtc::Description::Media & media);

            video::media_codec_and_profile_t prepare_track(const RtcTrackPtr & track);

            void setup(rtc::PeerConnection & peer)
            {
                if (m_has_setup)
                {
                    throw cpptrace::runtime_error("already setup");
                }
                m_has_setup = true;
                for (int i = 0; i < m_src->nb_streams(); i++)
                {
                    auto media_type = m_src->stream_media_type(i);
                    cfgo::RtcTrackPtr rtc_track_ptr;
                    if (media_type == AVMediaType::AVMEDIA_TYPE_VIDEO)
                    {
                        auto desc = create_video(gen_signaling_media_id(m_gen), gen_signaling_media_id(m_gen), gen_ssrc());
                        rtc_track_ptr = peer.addTrack(desc);
                    }
                    else if (media_type == AVMediaType::AVMEDIA_TYPE_AUDIO)
                    {
                        auto desc = create_audio(gen_signaling_media_id(m_gen), gen_signaling_media_id(m_gen), gen_ssrc());
                        rtc_track_ptr = peer.addTrack(desc);
                    }
                    if (rtc_track_ptr)
                    {
                        assert(rtc_track_ptr->direction() == rtc::Description::Direction::SendOnly || rtc_track_ptr->direction() == rtc::Description::Direction::SendRecv);
                        m_tracks.emplace_back(std::nullopt, rtc_track_ptr, 1, 1, 8, 1, 1, 16);
                    }
                    else
                    {
                        m_tracks.push_back(nullptr);
                    }
                }
                m_receivers = std::vector<video::media_receiver_ptr_t>(m_tracks.size(), nullptr);
            }

            bool bind(const msg::Track & meta)
            {
                for (auto & track : m_tracks)
                {
                    if (track && track.track()->mid() == meta.bindId)
                    {
                        if (track.has_meta())
                        {
                            throw cpptrace::runtime_error("repeated bind track meta");
                        }
                        track.prepare_meta(meta);
                        ++ m_binded;
                        return true;
                    }
                }
                return false;
            }

            video::media_receiver_ptr_t find_receiver_by_track(RtcTrackPtr rtc_track_ptr)
            {
                int i = 0;
                for (auto & track : m_tracks)
                {
                    if (track.track() == rtc_track_ptr)
                    {
                        return m_receivers.at(i);
                    }
                    ++i;
                }
                return nullptr;
            }

            bool ready() const noexcept
            {
                return m_binded == m_tracks.size();
            }

            auto start() -> asio::awaitable<void>
            {
                auto self = shared_from_this();
                if (m_has_start)
                {
                    co_return;
                }
                m_has_start = true;
                auto executor = co_await asio::this_coro::executor;
                int i = 0;
                for (auto & track : m_tracks)
                {
                    if (track)
                    {
                        auto desc = track.track()->description();
                        auto key = prepare_track(track.track());
                        auto receiver = self->m_src->acquire_receiver(i, key);
                        self->m_receivers.at(i) = receiver;
                        asio::co_spawn(executor, log_error([receiver, track, self]() -> asio::awaitable<void> {
                            try
                            {
                                if (!co_await track.await_open_or_close(self->m_closer))
                                {
                                    co_return;
                                }
                                if (track.is_closed())
                                {
                                    co_return;
                                }
                                do
                                {
                                    auto pkt_ptr = co_await receiver->request_pkt(self->m_closer);
                                    if (!co_await track.await_send_msg(std::move(pkt_ptr), self->m_closer))
                                    {
                                        co_return;
                                    }
                                } while (true);
                            }
                            catch(const CancelError & e) {}
                            catch(...)
                            {
                                if (!self->m_closer.is_closed())
                                {
                                    self->m_closer.close("error");
                                }
                                if (!self->m_err)
                                {
                                    self->m_err = std::current_exception();
                                }
                                CFGO_ERROR(what());
                            }
                        }), asio::detached);
                    }
                    ++i;
                }
            }

            auto wait_closed(close_chan closer) const -> asio::awaitable<void>
            {
                auto self = shared_from_this();
                co_await m_closer.await(closer);
                if (self->m_err)
                {
                    std::rethrow_exception(self->m_err);
                }
            }

            PubMsgPtr create_publish_msg()
            {
                auto msg = allocate_tracers::make_unique_skip_n<msg::PublishAddMessage>(1);
                for (auto & track : m_tracks)
                {
                    if (track)
                    {
                        msg->tracks.push_back(msg::TrackToPublish {
                            .type = track.track()->description().type(),
                            .bindId = track.track()->mid(),
                            .labels = m_labels
                        });
                    }
                }
                return msg;
            }
        };

        video::media_codec_and_profile_t Publication::prepare_track(const RtcTrackPtr & track)
        {
            auto media = track->description();
            auto pts = media.payloadTypes();
            if (pts.empty())
            {
                throw cpptrace::runtime_error("no media available in track desc");
            }
            auto pt = pts.at(0);
        
            auto ssrcs = media.getSSRCs();
            if (ssrcs.empty())
            {
                throw cpptrace::runtime_error("no ssrc available in track desc");
            }
            auto ssrc = ssrcs.at(0);
            auto cname = media.getCNameForSsrc(ssrc).value();
            auto rtp_map = media.rtpMap(pt);
            auto codec_iter = g_name_to_codec_id.find(rtp_map->format);
            if (codec_iter == g_name_to_codec_id.end())
            {
                throw cpptrace::runtime_error(fmt::format("unknown codec name {}", rtp_map->format));
            }
            auto codec_id = codec_iter->second;
            std::string profile {};
            if (!rtp_map->fmtps.empty())
            {
                profile = str_join(rtp_map->fmtps, ';');
            }
            video::media_codec_and_profile_t codec_and_profile {codec_id, profile};
            codec_and_profile.profile.remove_profile("level-asymmetry-allowed");
            if (codec_id == AV_CODEC_ID_H264)
            {
                codec_and_profile.profile.consume_profile<std::string>("profile-level-id", [](const std::string & key, const std::string & value, video::media_profile_t * profile) {
                    auto profile_level_id = video::parse_h264_profile_level_id(value);
                    if (profile_level_id)
                    {
                        profile->set_profile("h264_level", (int) profile_level_id->level);
                        profile->set_profile("h264_profile", profile_level_id->profile);
                        profile->set_profile("bit_rate", profile_level_id->max_bit_rate());
                    }
                    else
                    {
                        throw cpptrace::runtime_error(std::format("invalid profile-level-id: {}", value));
                    }
                });
            }
            if (m_width > 0)
            {
                codec_and_profile.profile.set_profile("width", m_width);
            }
            if (m_height > 0)
            {
                codec_and_profile.profile.set_profile("height", m_height);
            }
            if (m_fps > 0)
            {
                codec_and_profile.profile.set_profile("fps", m_fps);
            }
            if (m_bit_rate > 0)
            {
                media.setBitrate(m_bit_rate);
                codec_and_profile.profile.set_profile("bit_rate", m_bit_rate);
            }
            else if (media.bitrate() > 0)
            {
                codec_and_profile.profile.set_profile("bit_rate", media.bitrate());
            }

            RtcTrackWPtr weak_track = track;
            auto pli_handler = std::make_shared<rtc::PliHandler>([weak_self = weak_from_this(), weak_track]() {
                if (auto self = weak_self.lock())
                {
                    if (auto track = weak_track.lock())
                    {
                        auto receiver = self->find_receiver_by_track(track);
                        if (receiver)
                        {
                            receiver->request_key_frame();
                        }
                    }
                }
            });
            auto nack_handler = std::make_shared<rtc::RtcpNackResponder>(32);
            std::shared_ptr<rtc::RtpPacketizer> packetizer = get_rtp_packetizer_for(codec_id, ssrc, cname, pt, rtp_map->clockRate, MAX_VIDEO_FRAGMENT_SIZE);
            bool use_ffmpet_rtp_muxer = !m_prefer_packetizer || !packetizer;
            if (use_ffmpet_rtp_muxer)
            {
                codec_and_profile.profile.set_profile("ofmt", "rtp");
                codec_and_profile.profile.set_profile("payload_type", pt);
                codec_and_profile.profile.set_profile("ssrc", static_cast<int32_t>(ssrc));
                if (rtp_map->clockRate > 0)
                {
                    if (media.type() == "video")
                    {
                        codec_and_profile.profile.set_profile("time_scale", rtp_map->clockRate);
                    }
                    else if (media.type() == "audio")
                    {
                        codec_and_profile.profile.set_profile("sample_rate", rtp_map->clockRate);
                    }
                }
                nack_handler->addToChain(pli_handler);
                track->setMediaHandler(nack_handler);
            }
            else
            {
                auto sr_reporter = std::make_shared<rtc::RtcpSrReporter>(packetizer->rtpConfig);
                packetizer->addToChain(sr_reporter);
                packetizer->addToChain(pli_handler);
                packetizer->addToChain(nack_handler);
                track->setMediaHandler(packetizer);
            }
            return codec_and_profile;
        }
        
    } // namespace impl

    Publication::Publication(video::media_source_ptr_t media_source, Labels labels): ImplBy(std::move(media_source), std::move(labels)) {}

    int & Publication::width()
    {
        return impl()->width();
    }
    int Publication::width() const
    {
        return impl()->width();
    }
    int & Publication::height()
    {
        return impl()->height();
    }
    int Publication::height() const
    {
        return impl()->height();
    }
    int & Publication::fps()
    {
        return impl()->fps();
    }
    int Publication::fps() const
    {
        return impl()->fps();
    }
    int64_t & Publication::bit_rate()
    {
        return impl()->bit_rate();
    }
    int64_t Publication::bit_rate() const
    {
        return impl()->bit_rate();
    }

    void Publication::setup(rtc::PeerConnection & peer) const
    {
        impl()->setup(peer);
    }

    bool Publication::bind(const msg::Track & track) const
    {
        return impl()->bind(track);
    }
    bool Publication::ready() const noexcept
    {
        return impl()->ready();
    }
    auto Publication::start() const -> asio::awaitable<void>
    {
        return impl()->start();
    }
    auto Publication::wait_closed(close_chan closer) const -> asio::awaitable<void>
    {
        return impl()->wait_closed(std::move(closer));
    }
    PubMsgPtr Publication::create_publish_msg() const
    {
        return impl()->create_publish_msg();
    }
    
} // namespace cfgo
