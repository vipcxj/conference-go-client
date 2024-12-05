#ifndef _CFGO_VIDEO_MEDIA_PROFILE_HPP_
#define _CFGO_VIDEO_MEDIA_PROFILE_HPP_

#include <map>
#include <string>
#include <cstdint>
#include <cpptrace/cpptrace.hpp>
#include "cfgo/fmt.hpp"

namespace cfgo
{
    namespace video
    {
        template <typename T>
        concept Character = 
            std::same_as<T, char> ||
            std::same_as<T, signed char> ||
            std::same_as<T, unsigned char> ||
            std::same_as<T, wchar_t> ||
            std::same_as<T, char8_t> ||
            std::same_as<T, char16_t> ||
            std::same_as<T, char32_t>;

        template <typename T>
        concept AlwaysFalse = std::false_type::value;

        template<typename T>
        T pair_to_t(const std::map<std::string, std::string>::const_iterator & value_pair)
        {
            if constexpr(std::convertible_to<std::string, T>)
            {
                return T{value_pair->second};
            }
            else if constexpr(Character<T>)
            {
                static_assert(AlwaysFalse<T>, "unsupport profile type");
            }
            else if constexpr(std::same_as<T, bool>)
            {
                if (value_pair->second.empty() || value_pair->second == "true" || value_pair->second == "TRUE" || value_pair->second == "1")
                {
                    return true;
                }
                else if (value_pair->second == "false" || value_pair->second == "FALSE" || value_pair->second == "0")
                {
                    return false;
                }
                else
                {
                    throw cpptrace::runtime_error(fmt::format("could not convert the profile value {} of key {} to bool type", value_pair->second, value_pair->first));
                }
            }
            else if constexpr(std::is_integral_v<T>)
            {
                if constexpr(std::is_unsigned_v<T>)
                {
                    auto v = std::stoull(value_pair->second);
                    if (v > std::numeric_limits<T>::max())
                    {
                        throw cpptrace::runtime_error(
                            fmt::format("numeric overflow when convert the profile value {} of key to some int type"),
                            value_pair->second,
                            value_pair->first
                        );
                    }
                    else
                    {
                        return static_cast<T>(v);
                    }
                }
                else
                {
                    auto v = std::stoll(value_pair->second);
                    if (v > std::numeric_limits<T>::max() || v < std::numeric_limits<T>::min())
                    {
                        throw cpptrace::runtime_error(fmt::format(
                            "numeric overflow when convert the profile value {} of key {} to some int type",
                            value_pair->second,
                            value_pair->first
                        ));
                    }
                    else
                    {
                        return static_cast<T>(v);
                    } 
                }
            }
            else if constexpr(std::is_floating_point_v<T>)
            {
                auto v = std::stold(value_pair->second);
                if (v > std::numeric_limits<T>::max() || v < std::numeric_limits<T>::min())
                {
                    throw cpptrace::runtime_error(fmt::format(
                        "numeric overflow when convert the profile value {} of key to some float type",
                        value_pair->second,
                        value_pair->first
                    ));
                }
                else
                {
                    return static_cast<T>(v);
                }
            }
            else
            {
                static_assert(AlwaysFalse<T>, "unsupport profile type ");
            }
        }

        struct MediaProfile
        {
            std::map<std::string, std::string> m_profiles;

            MediaProfile(std::string profile_string);

            friend bool operator ==(const MediaProfile & lhs, const MediaProfile & rhs)
            {
                return lhs.m_profiles.size() == rhs.m_profiles.size() && std::equal(lhs.m_profiles.begin(), lhs.m_profiles.end(), rhs.m_profiles.begin());
            }

            void extract_keys(const std::vector<std::string> & keys, std::unordered_map<std::string, std::string> & out)
            {
                for (auto & key : keys)
                {
                    auto iter = m_profiles.find(key);
                    if (iter != m_profiles.end())
                    {
                        out.insert(*iter);
                    }
                }
            }

            template<typename T>
            requires requires(const T & v) { std::to_string(v); }
            void set_profile(std::string_view key, const T & value)
            {
                m_profiles.insert_or_assign(std::string {key}, std::to_string(value));
            }

            void set_profile(std::string_view key, std::string_view value)
            {
                m_profiles.insert_or_assign(std::string {key}, std::string {value});
            }

            template<typename T>
            // requires std::convertible_to<U, T>
            T get_profile(const std::string & key, const T & default_value) const
            {
                auto value_pair = m_profiles.find(key);
                if (value_pair != m_profiles.end())
                {
                    return pair_to_t<T>(value_pair);
                }
                else
                {
                    return default_value;
                }
            }

            bool remove_profile(const std::string & key)
            {
                auto value_pair = m_profiles.find(key);
                if (value_pair != m_profiles.end())
                {
                    m_profiles.erase(value_pair);
                    return true;
                }
                else
                {
                    return false;
                }
            }

            template<typename T, typename F>
            requires requires (F && consumer, const std::string & k, const T & t, MediaProfile * self) { consumer(k, t, self); }
            bool consume_profile(const std::string & key, F && consumer)
            {
                auto value_pair = m_profiles.find(key);
                if (value_pair != m_profiles.end())
                {
                    auto key = value_pair->first;
                    auto value = pair_to_t<T>(value_pair);
                    m_profiles.erase(value_pair);
                    consumer(key, value, this);
                    return true;
                }
                else
                {
                    return false;
                }
            }
        };

        using media_profile_t = MediaProfile;

    } // namespace video
    
} // namespace cfgo

template<>
struct std::hash<cfgo::video::media_profile_t>
{
    std::size_t operator()(const cfgo::video::media_profile_t & k) const
    {
        using std::hash;
        if (k.m_profiles.empty())
        {
            return 0;
        }
        auto iter = k.m_profiles.begin();
        std::size_t res = (hash<std::string>{}(iter->first) ^ (hash<std::string>{}(iter->second) << 1)) >> 1;
        ++ iter;
        while (iter != k.m_profiles.end())
        {
            res ^= ((hash<std::string>{}(iter->first) ^ (hash<std::string>{}(iter->second) << 1)) >> 1) << 1;
            res = res >> 1;
            ++ iter;
        }
        return res;
    }
};


#endif