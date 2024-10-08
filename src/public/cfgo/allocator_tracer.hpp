#ifndef _CFGO_ALLOCATOR_TRACER_HPP_
#define _CFGO_ALLOCATOR_TRACER_HPP_

#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <source_location>

#include "cfgo/alias.hpp"

namespace cfgo
{
    class close_allocator_tracer
    {
    private:
        struct closer_tracer_entry
        {
            std::uintptr_t parent = 0;
            bool close_loc_flag = false;
            std::source_location close_loc;
        };

        using closer_tracer_entries_type = std::unordered_map<std::uintptr_t, closer_tracer_entry>;

        struct closer_tracer_ref
        {
            std::atomic_int64_t& ref_count;
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            mutex& mux;
            closer_tracer_entries_type& entries;
            #endif
        };
        
        static closer_tracer_ref global_closer_tracer() noexcept
        {
            static std::atomic_int64_t g_ref_count;
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            static mutex g_mux;
            static closer_tracer_entries_type g_entries;
            return { g_ref_count, g_mux, g_entries };
            #else
            return { g_ref_count };
            #endif
        }
    public:

        static void ctor(std::uintptr_t key, std::uintptr_t parent) noexcept
        {
            auto tracer = global_closer_tracer();
            tracer.ref_count ++;
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            {
                std::lock_guard lk(tracer.mux);
                tracer.entries[key] = { .parent = parent };
            }
            #endif
        }

        static void dtor(std::uintptr_t key) noexcept
        {
            auto tracer = global_closer_tracer();
            tracer.ref_count --;
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            {
                std::lock_guard lk(tracer.mux);
                tracer.entries.erase(key);
            }
            #endif
        }

        static int64_t ref_count()
        {
            return global_closer_tracer().ref_count.load();
        }

        #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
        static void update_close_loc(std::uintptr_t key, const std::source_location & loc)
        {
            auto tracer = global_closer_tracer();
            std::lock_guard lk(tracer.mux);
            auto & entry = tracer.entries[key];
            entry.close_loc_flag = true;
            entry.close_loc = loc;
        }

        static std::optional<std::source_location> get_close_loc(std::uintptr_t key)
        {
            auto tracer = global_closer_tracer();
            std::lock_guard lk(tracer.mux);
            auto & entry = tracer.entries[key];
            if (entry.close_loc_flag)
            {
                return entry.close_loc;
            }
            else
            {
                return std::nullopt;
            }
        } 
        #endif
    };
    
} // namespace cfgo


#endif