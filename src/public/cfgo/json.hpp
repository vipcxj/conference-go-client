#ifndef _CFGO_JSON_HPP_
#define _CFGO_JSON_HPP_

#include "nlohmann/json.hpp"

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


#endif