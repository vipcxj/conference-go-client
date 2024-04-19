#include "cfgo/subscribation.hpp"
#include "impl/subscribation.hpp"


namespace cfgo
{
    Subscribation::Subscribation(const std::string& sub_id, const std::string& pub_id) : ImplBy<impl::Subscribation>(sub_id, pub_id) {}

    const std::string & Subscribation::sub_id() const noexcept {
        return impl()->subId;
    }
    const std::string & Subscribation::pub_id() const noexcept {
        return impl()->pubId;
    }
    std::vector<TrackPtr> & Subscribation::tracks() noexcept {
        return impl()->tracks;
    }
    const std::vector<TrackPtr> & Subscribation::tracks() const noexcept {
        return impl()->tracks;
    }
} // namespace cfgo