#ifndef _CFGO_SUBSCRIBATION_HPP_
#define _CFGO_SUBSCRIBATION_HPP_

#include <memory>
#include <string>
#include <vector>
#include "cfgo/alias.hpp"
#include "cfgo/utils.hpp"

namespace cfgo
{
    namespace impl
    {
        struct Subscribation;
    } // namespace impl
    
    struct Subscribation : ImplBy<impl::Subscribation> {
        using Ptr = std::shared_ptr<Subscribation>;

        Subscribation(const std::string& sub_id, const std::string& pub_id);

        const std::string & sub_id() const noexcept;
        const std::string & pub_id() const noexcept;
        std::vector<TrackPtr> & tracks() noexcept;
        const std::vector<TrackPtr> & tracks() const noexcept;
    };
} // namespace cfgo


#endif