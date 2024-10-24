#include "cfgo/publication.hpp"
#include "unordered_map"
#include "algorithm"

namespace cfgo
{
    namespace impl
    {
        struct Publication
        {
            using Tracks = std::vector<std::optional<msg::Track>>;
            SinkUPtr m_sink;
            RTCTracks m_rtc_tracks;
            Tracks m_tracks;
            int m_binded {0};
            Labels m_labels;

            Publication(SinkUPtr sink, Labels labels):
                m_sink(std::move(sink)),
                m_rtc_tracks(m_sink->get_rtc_tracks()),
                m_tracks(Tracks(m_rtc_tracks.size())),
                m_labels(std::move(labels))
            {}

            bool bind(const msg::Track & track)
            {
                auto iter = std::find_if(m_rtc_tracks.begin(), m_rtc_tracks.end(), [mid = track.bindId](const RTCTrackPtr & rtc_track) {
                    return rtc_track->mid() == mid;
                });
                if (iter != m_rtc_tracks.end())
                {
                    auto i = iter - m_rtc_tracks.begin();
                    if (!m_tracks[i])
                    {
                        m_tracks[i] = track;
                        ++ m_binded;
                        return true;
                    }
                }
                return false;
            }

            bool ready() const noexcept
            {
                return m_binded == m_rtc_tracks.size();
            }

            PubMsgPtr create_publish_msg()
            {
                auto msg = allocate_tracers::make_unique_skip_n<msg::PublishAddMessage>(1);
                for (auto & track : m_rtc_tracks)
                {
                    msg->tracks.push_back(msg::TrackToPublish {
                        .type = track->description().type(),
                        .bindId = track->mid(),
                        .labels = m_labels
                    });
                }
                return msg;
            }

            Sink & sink()
            {
                return *m_sink;
            }
        };
        
    } // namespace impl

    Publication::Publication(SinkUPtr sink, Labels labels): ImplBy(std::move(sink), std::move(labels)) {}
    bool Publication::bind(const msg::Track & track) const
    {
        return impl()->bind(track);
    }
    bool Publication::ready() const noexcept
    {
        return impl()->ready();
    }
    PubMsgPtr Publication::create_publish_msg() const
    {
        return impl()->create_publish_msg();
    }
    Sink & Publication::sink() const
    {
        return impl()->sink();
    }
    
} // namespace cfgo
