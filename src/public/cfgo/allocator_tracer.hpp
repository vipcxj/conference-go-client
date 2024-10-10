#ifndef _CFGO_ALLOCATOR_TRACER_HPP_
#define _CFGO_ALLOCATOR_TRACER_HPP_

#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <source_location>

#include "cfgo/alias.hpp"
#include "cfgo/utils.hpp"


namespace cfgo
{
    struct source_location_pool
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

        using id_type = std::size_t;
        using pool_type = std::unordered_map<std::source_location, id_type, KeyHasher, KeyEqualer>;
        using locs_type = std::vector<std::source_location>;

        struct entries
        {
            mutex &mux;
            pool_type &pool;
            locs_type &locs;

            id_type get_id(const std::source_location & loc)
            {
                std::lock_guard lk(mux);
                id_type id;
                auto iter = pool.find(loc);
                if (iter == pool.end())
                {
                    id = locs.size();
                    locs.push_back(loc);
                    pool[loc] = id;
                }
                else
                {
                    id = iter->second;
                }
                return id;
            }

            const std::source_location & get_loc(id_type id) const
            {
                std::lock_guard lk(mux);
                return locs[id];
            }
        };

        static entries get() noexcept
        {
            static mutex g_mux {};
            static pool_type g_pool {};
            static locs_type g_locs {};
            return { g_mux, g_pool, g_locs };
        } 
    };

    using loc_id_type = source_location_pool::id_type;
    class close_allocator_tracer
    {
    private:
        struct closer_tracer_entry
        {
            std::uintptr_t parent = 0;
            bool close_loc_flag = false;
            loc_id_type ctr_loc_id;
            loc_id_type close_loc_id;
        };

    public:
        using closer_tracer_entries_type = std::unordered_map<std::uintptr_t, closer_tracer_entry>;
        using closer_tracer_locs_type = std::unordered_map<loc_id_type, std::size_t>;

    private:
        struct closer_tracer_ref
        {
            std::atomic_int64_t& ref_count;
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            mutex& mux;
            closer_tracer_entries_type& entries;
            closer_tracer_locs_type& locs;
            #endif
        };
        
        static closer_tracer_ref global_closer_tracer() noexcept
        {
            static std::atomic_int64_t g_ref_count;
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            static mutex g_mux;
            static closer_tracer_entries_type g_entries;
            static closer_tracer_locs_type g_locs;
            return { g_ref_count, g_mux, g_entries, g_locs };
            #else
            return { g_ref_count };
            #endif
        }
    public:

        static void ctor(
        #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            std::uintptr_t key,
            std::uintptr_t parent,
            std::source_location src_loc
        #endif
        ) noexcept
        {
            auto tracer = global_closer_tracer();
            tracer.ref_count ++;
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            {
                std::lock_guard lk(tracer.mux);
                auto ctr_loc_id = source_location_pool::get().get_id(src_loc);
                tracer.entries[key] = { .parent = parent, .ctr_loc_id = ctr_loc_id };
                auto iter = tracer.locs.find(ctr_loc_id);
                if (iter == tracer.locs.end())
                {
                    tracer.locs[ctr_loc_id] = 1;
                }
                else
                {
                    ++ iter->second;
                }
            }
            #endif
        }

        static void dtor(
        #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            std::uintptr_t key
        #endif
        ) noexcept
        {
            auto tracer = global_closer_tracer();
            tracer.ref_count --;
            #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES
            std::lock_guard lk(tracer.mux);
            auto iter = tracer.entries.find(key);
            if (iter != tracer.entries.end())
            {
                auto ctr_id = iter->second.ctr_loc_id;
                tracer.entries.erase(iter);
                auto & ctr_ref_count = tracer.locs[ctr_id];
                -- ctr_ref_count;
                if (ctr_ref_count == 0)
                {
                    tracer.locs.erase(ctr_id);
                }
            }
            #endif
        }

        static int64_t ref_count()
        {
            return global_closer_tracer().ref_count.load();
        }

        #ifdef CFGO_CLOSER_ALLOCATOR_TRACER_USE_ENTRIES

        static std::optional<std::source_location> get_ctr_loc(std::uintptr_t key)
        {
            auto tracer = global_closer_tracer();
            std::lock_guard lk(tracer.mux);
            auto iter = tracer.entries.find(key);
            if (iter != tracer.entries.end())
            {
                return source_location_pool::get().get_loc(iter->second.ctr_loc_id);
            }
            else
            {
                return std::nullopt;
            }
        }

        static void update_close_loc(std::uintptr_t key, const std::source_location & loc)
        {
            auto tracer = global_closer_tracer();
            auto loc_pool = source_location_pool::get();
            std::lock_guard lk(tracer.mux);
            auto iter = tracer.entries.find(key);
            if (iter != tracer.entries.end())
            {
                iter->second.close_loc_flag = true;
                iter->second.close_loc_id = loc_pool.get_id(loc);
            }
        }

        static std::optional<std::source_location> get_close_loc(std::uintptr_t key)
        {
            auto tracer = global_closer_tracer();
            std::lock_guard lk(tracer.mux);
            auto iter = tracer.entries.find(key);
            if (iter != tracer.entries.end() && iter->second.close_loc_flag)
            {
                return source_location_pool::get().get_loc(iter->second.close_loc_id);
            }
            else
            {
                return std::nullopt;
            }
        }

        static void collect_ctr_src_locs_with_max_n_ref_count(std::size_t n, std::vector<std::pair<loc_id_type, std::size_t>> & locs_ref_result)
        {
            auto tracer = global_closer_tracer();
            std::lock_guard lk(tracer.mux);
            using iter_t = std::ranges::borrowed_iterator_t<decltype(tracer.locs)>;
            locs_ref_result.clear();
            for (auto & iter : max_n_elements(tracer.locs, n, [](iter_t iter1, iter_t iter2) -> bool {
                return iter1->second > iter2->second;
            }))
            {
                locs_ref_result.push_back(std::make_pair(iter->first, iter->second));
            }
        }

        static void collect_parent_ctr_src_locs(loc_id_type ctr_src_loc_id, std::unordered_set<loc_id_type> & result)
        {
            auto tracer = global_closer_tracer();
            std::lock_guard lk(tracer.mux);
            for (auto [_, entry] : tracer.entries)
            {
                if (entry.ctr_loc_id == ctr_src_loc_id)
                {
                    auto parent_iter = tracer.entries.find(entry.parent);
                    if (parent_iter != tracer.entries.end())
                    {
                        result.insert(parent_iter->second.ctr_loc_id);
                    }
                }
            }
        }

        static void collect_child_ctr_src_locs(loc_id_type ctr_src_loc_id, std::unordered_set<loc_id_type> & result)
        {
            auto tracer = global_closer_tracer();
            std::lock_guard lk(tracer.mux);
            std::unordered_set<std::uintptr_t> parents {};
            for (auto [address, entry] : tracer.entries)
            {
                if (entry.ctr_loc_id == ctr_src_loc_id)
                {
                    parents.insert(address);
                }
            }
            for (auto [_, entry] : tracer.entries)
            {
                if (parents.contains(entry.parent))
                {
                    result.insert(entry.ctr_loc_id);
                }
            }
        }
        #endif
    };
    
} // namespace cfgo


#endif