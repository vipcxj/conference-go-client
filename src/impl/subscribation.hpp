#ifndef _CFGO_IMPL_SUBSCRIBATION_HPP_
#define _CFGO_IMPL_SUBSCRIBATION_HPP_

#include <memory>
#include <string>
#include <vector>
#include "cfgo/track.hpp"

namespace cfgo
{
    namespace impl
    {
        struct Subscribation {
            using Ptr = std::shared_ptr<Subscribation>;
            std::string subId;
            std::string pubId;
            std::vector<cfgo::Track::Ptr> tracks;

            Subscribation(const std::string& sub_id, const std::string& pub_id);
        };        
    } // namespace impl

} // namespace cfgo


#endif