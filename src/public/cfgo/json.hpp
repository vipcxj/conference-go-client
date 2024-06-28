#ifndef _CFGO_JSON_HPP_
#define _CFGO_JSON_HPP_

#include "nlohmann/json.hpp"
#include <optional>

namespace cfgo
{
    template<typename T>
    concept JsonSeriable = requires(T self, nlohmann::json j) {
        j["test"] = self;
    };

    template<typename T>
    concept JsonDeseriable = requires(T self, nlohmann::json j) {
        j.template get<T>();
    };
} // namespace cfgo

namespace nlohmann
{
    template <typename T>
    struct adl_serializer<std::optional<T>> {
        static void from_json(const json & j, std::optional<T>& opt) {
            if(j.is_null()) {
                opt = std::nullopt;
            } else {
                opt = j.get<T>();
            }
        }
        static void to_json(json & json, std::optional<T> t) {
            if (t) {
                json = *t;
            } else {
                json = nullptr;
            }
        }
    };
} // namespace nlohmann



#endif