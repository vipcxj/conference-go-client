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
    class source_location_pool
    {
        struct KeyHasher
        {
            std::size_t operator()(const std::source_location & loc) const
            {
                using std::size_t;
                using std::hash;
                size_t res = 17;
                res = res * 31 + hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(loc.file_name()));
                res = res * 31 + hash<std::uintptr_t>()(reinterpret_cast<std::uintptr_t>(loc.function_name()));
                res = res * 31 + hash<decltype(loc.line())>()(loc.line());
                res = res * 31 + hash<decltype(loc.column())>()(loc.column());
                return res;
            }
        };

        struct KeyEqualer
        {
            bool operator()(const std::source_location & loc1, const std::source_location & loc2) const
            {
                return loc1.file_name() == loc2.file_name()
                    && loc1.function_name() == loc2.function_name()
                    && loc1.line() == loc2.line()
                    && loc1.column() == loc2.column();
            }
        };

        using pool_type = std::unordered_map<std::source_location, std::int64_t, KeyHasher, KeyEqualer>;

        struct entries
        {
            std::atomic_int64_t& counter;
            pool_type &pool;
        };

        static entries get() noexcept
        {
            static std::atomic_int64_t g_counter {0};
            static pool_type g_pool {};
            return { g_counter, g_pool };
        } 
    };
    class close_allocator_tracer
    {
    private:
        struct closer_tracer_entry
        {
            std::uintptr_t parent = 0;
            bool close_loc_flag = false;
            std::source_location ctr_loc;
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

        static void ctor(
            std::uintptr_t key,
            std::uintptr_t parent
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            , std::source_location src_loc
            #endif
        ) noexcept
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