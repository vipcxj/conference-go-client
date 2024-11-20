#include "cfgo/publication.hpp"
#include "cfgo/track.hpp"
#include "cfgo/str_helper.hpp"
#include "cfgo/video/media_profile.hpp"
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <random>
#include <chrono>
#include <limits>

#include "boost/uuid/uuid_io.hpp"
#include "boost/uuid/uuid_generators.hpp"

namespace cfgo
{
    namespace impl
    {
        struct SupportedCodec {
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
            {"AV1", AV_CODEC_ID_AV1},
            {"VP9", AV_CODEC_ID_VP9},
            {"opus", AV_CODEC_ID_OPUS},
            {"PCMU", AV_CODEC_ID_PCM_MULAW},
            {"PCMA", AV_CODEC_ID_PCM_ALAW},
        };
        const std::unordered_map<const AVCodecID, std::string> g_codec_id_to_name = map_back(g_name_to_codec_id);
        const std::map<int, SupportedCodec> g_supported_video_codecs = {
            {96, { .codec_id = AV_CODEC_ID_VP8, .profile = "" }},
            {106, { .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f" }},
            {108, { .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f" }},
            {102, { .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f" }},
            {104, { .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f" }},
            {127, { .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f" }},
            {39, { .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=4d001f" }},
            {45, { .codec_id = AV_CODEC_ID_AV1, .profile = "" }},
            {98, { .codec_id = AV_CODEC_ID_VP9, .profile = "profile-id=0" }},
            {100, { .codec_id = AV_CODEC_ID_VP9, .profile = "profile-id=2" }},
            {112, { .codec_id = AV_CODEC_ID_H264, .profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=64001f" }},
        };
        const std::map<int, SupportedCodec> g_supported_audio_codecs = {
            {111, { .codec_id = AV_CODEC_ID_OPUS, .profile = "" }},
            {0, { .codec_id = AV_CODEC_ID_PCM_MULAW, .profile = "" }},
            {8, { .codec_id = AV_CODEC_ID_PCM_ALAW, .profile = "" }},
        };

        rtc::Description::Video create_video(std::string mid, uint32_t ssrc)
        {
            auto video = rtc::Description::Video(std::move(mid));
            // from pion payload types.
            for (auto & [pt, codec] : g_supported_video_codecs)
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
            // video.addVP8Codec(96);
            // // video.addRtxCodec(97, 96, 90000);
            // video.addH264Codec(106, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
            // // video.addRtxCodec(107, 106, 90000);
            // video.addH264Codec(108, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f");
            // // video.addRtxCodec(109, 108, 90000);
            // video.addH264Codec(102, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f");
            // // video.addRtxCodec(103, 102, 90000);
            // video.addH264Codec(104, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42001f");
            // // video.addRtxCodec(105, 104, 90000);
            // video.addH264Codec(127, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=4d001f");
            // // video.addRtxCodec(125, 127, 90000);
            // video.addH264Codec(39, "level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=4d001f");
            // // video.addRtxCodec(40, 39, 90000);
            // video.addAV1Codec(45);
            // // video.addRtxCodec(46, 45, 90000);
            // video.addVP9Codec(98, "profile-id=0");
            // // video.addRtxCodec(99, 98, 90000);
            // video.addVP9Codec(100, "profile-id=2");
            // // video.addRtxCodec(101, 100, 90000);
            // video.addH264Codec(112, "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=64001f");
            // // video.addRtxCodec(113, 112, 90000);
            video.addSSRC(ssrc, boost::uuids::to_string(boost::uuids::random_generator()()));
            return video;
        }

        rtc::Description::Audio create_audio(std::string mid, uint32_t ssrc)
        {
            auto audio = rtc::Description::Audio(std::move(mid));
            // from pion payload types.
            // acc is not supported by pion, so not list here.

            for (auto & [pt, codec] : g_supported_audio_codecs)
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
            audio.addSSRC(ssrc, boost::uuids::to_string(boost::uuids::random_generator()()));
            return audio;
        }

        struct Publication;

        std::shared_ptr<rtc::Track> create_track(std::weak_ptr<Publication> weak_self, rtc::PeerConnection & peer, const rtc::Description::Media & media);

        video::media_codec_and_profile_t parse_track_media(const rtc::Description::Media & media)
        {
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
            codec_and_profile.profile.set_profile("payload_type", pt);
            codec_and_profile.profile.set_profile("ssrc", ssrc);
            if (rtp_map->clockRate > 0)
            {
                codec_and_profile.profile.set_profile("clock_rate", media.bitrate());
            }
            if (media.bitrate() > 0)
            {
                codec_and_profile.profile.set_profile("bit_rate", media.bitrate());
            }
            return codec_and_profile;
        }

        struct Publication : public std::enable_shared_from_this<Publication>
        {
            std::vector<cfgo::Track> m_tracks;
            int m_binded {0};
            video::media_source_ptr_t m_src;
            Labels m_labels;
            std::unordered_set<uint32_t> m_ssrcs;
            std::mt19937 m_gen;
            bool m_has_setup = false;

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
                    auto mid = boost::uuids::to_string(boost::uuids::random_generator()());
                    cfgo::RtcTrackPtr rtc_track_ptr;
                    if (media_type == AVMediaType::AVMEDIA_TYPE_VIDEO)
                    {
                        auto desc = create_video(mid, gen_ssrc());
                        rtc_track_ptr = create_track(weak_from_this(), peer, desc);
                    }
                    else if (media_type == AVMediaType::AVMEDIA_TYPE_AUDIO)
                    {
                        auto desc = create_audio(mid, gen_ssrc());
                        rtc_track_ptr = create_track(weak_from_this(), peer, desc);
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

            bool ready() const noexcept
            {
                return m_binded == m_tracks.size();
            }

            void start()
            {
                for (auto & track : m_tracks)
                {
                    if (track)
                    {
                        auto desc = track.track()->description();
                        auto pts = desc.payloadTypes();
                        if (pts.empty())
                        {
                            throw cpptrace::runtime_error("no media available in track desc");
                        }
                        auto pt = pts[0];
                        auto rtp_map = desc.rtpMap(pt);
                    }
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

        std::shared_ptr<rtc::Track> create_track(std::weak_ptr<Publication> weak_self, rtc::PeerConnection & peer, const rtc::Description::Media & media)
        {
            auto track = peer.addTrack(media);
            auto pli_handler = std::make_shared<rtc::PliHandler>([weak_self]() {
                if (auto self = weak_self.lock())
                {
                    // self->mark_pli();
                }
            });
            auto nack_handler = std::make_shared<rtc::RtcpNackResponder>(32);
            nack_handler->addToChain(pli_handler);
            track->setMediaHandler(nack_handler);
            return track;
        }
        
    } // namespace impl

    Publication::Publication(video::media_source_ptr_t media_source, Labels labels): ImplBy(std::move(media_source), std::move(labels)) {}

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
    void Publication::start() const
    {
        impl()->start();
    }
    PubMsgPtr Publication::create_publish_msg() const
    {
        return impl()->create_publish_msg();
    }
    
} // namespace cfgo
