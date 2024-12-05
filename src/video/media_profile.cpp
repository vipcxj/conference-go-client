#include "cfgo/video/media_profile.hpp"
#include "cfgo/str_helper.hpp"

namespace cfgo
{
    namespace video
    {
        MediaProfile::MediaProfile(std::string profile_string)
        {
            auto profiles = str_split(profile_string, ';');
            for (auto & profile : profiles)
            {
                auto pos = profile.find('=');
                if (pos != std::string::npos)
                {
                    m_profiles.insert_or_assign(profile.substr(0, pos), profile.substr(pos + 1));
                }
                else
                {
                    m_profiles.insert_or_assign(profile, "");
                }
            }
        }
    } // namespace video
    
} // namespace cfgo
