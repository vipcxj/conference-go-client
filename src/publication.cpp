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
            RtcTracks m_rtc_tracks;
            Tracks m_tracks;
            int m_binded {0};
            Labels m_labels;

            Publication(Labels labels): m_labels(std::move(labels)) {}

            void add_track(RtcTrackPtr track)
            {
                m_rtc_tracks.push_back(std::move(track));
            }

            bool bind(const msg::Track & track)
            {
                if (m_tracks.size() < m_rtc_tracks.size())
                {
                    m_tracks = Tracks(m_rtc_tracks.size(), std::nullopt);
                }
                
                auto iter = std::find_if(m_rtc_tracks.begin(), m_rtc_tracks.end(), [mid = track.bindId](const RtcTrackPtr & rtc_track) {
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
        };
        
    } // namespace impl

    Publication::Publication(Labels labels): ImplBy(std::move(labels)) {}
    void Publication::add_track(RtcTrackPtr track) const
    {
        impl()->add_track(std::move(track));
    }
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
    
} // namespace cfgo
