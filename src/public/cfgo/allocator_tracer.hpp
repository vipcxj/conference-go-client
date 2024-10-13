#ifndef _CFGO_ALLOCATOR_TRACER_HPP_
#define _CFGO_ALLOCATOR_TRACER_HPP_

#include <cstdint>
#include <atomic>
#include <concepts>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <source_location>
#include <typeindex>

#include "boost/core/demangle.hpp"

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

    class signal_allocator_tracer
    {
    private:
        struct signal_tracer_ref
        {
            std::atomic_int64_t& raw_msg_ref_count;
            std::atomic_int64_t& raw_acker_ref_count;
            std::atomic_int64_t& sig_msg_ref_count;
            std::atomic_int64_t& sig_acker_ref_count;
        };
        
        static signal_tracer_ref global_tracer() noexcept
        {
            static std::atomic_int64_t g_raw_msg_ref_count;
            static std::atomic_int64_t g_raw_acker_ref_count;
            static std::atomic_int64_t g_sig_msg_ref_count;
            static std::atomic_int64_t g_sig_acker_ref_count;

            return {
                g_raw_msg_ref_count,
                g_raw_acker_ref_count,
                g_sig_msg_ref_count,
                g_sig_acker_ref_count
            };
        }
    public:

        static void raw_msg_ctor() noexcept
        {
            auto tracer = global_tracer();
            tracer.raw_msg_ref_count ++;
        }

        static void raw_msg_dtor() noexcept
        {
            auto tracer = global_tracer();
            tracer.raw_msg_ref_count --;
        }

        static std::int64_t raw_msg_ref_count() noexcept
        {
            auto tracer = global_tracer();
            return tracer.raw_msg_ref_count.load();
        }

        static void raw_acker_ctor() noexcept
        {
            auto tracer = global_tracer();
            tracer.raw_acker_ref_count ++;
        }

        static void raw_acker_dtor() noexcept
        {
            auto tracer = global_tracer();
            tracer.raw_acker_ref_count --;
        }

        static std::int64_t raw_acker_ref_count() noexcept
        {
            auto tracer = global_tracer();
            return tracer.raw_acker_ref_count.load();
        }

        static void sig_msg_ctor() noexcept
        {
            auto tracer = global_tracer();
            tracer.sig_msg_ref_count ++;
        }

        static void sig_msg_dtor() noexcept
        {
            auto tracer = global_tracer();
            tracer.sig_msg_ref_count --;
        }

        static std::int64_t sig_msg_ref_count() noexcept
        {
            auto tracer = global_tracer();
            return tracer.sig_msg_ref_count.load();
        }

        static void sig_acker_ctor() noexcept
        {
            auto tracer = global_tracer();
            tracer.sig_acker_ref_count ++;
        }

        static void sig_acker_dtor() noexcept
        {
            auto tracer = global_tracer();
            tracer.sig_acker_ref_count --;
        }

        static std::int64_t sig_acker_ref_count() noexcept
        {
            auto tracer = global_tracer();
            return tracer.sig_acker_ref_count.load();
        }
    };

    template<typename T>
    concept allocate_tracer_support_detail = std::bool_constant<(T::allocate_tracer_detail)>::value;

    struct allocator_tracers
    {

        struct object_deleter;
        struct object_entry
        {
            object_entry(std::weak_ptr<void> weak_ptr): m_weak_ptr(std::move(weak_ptr)) {}
            void unref()
            {
                if (auto ptr = m_weak_ptr.lock())
                {
                    std::get_deleter<object_deleter>(ptr)->entry = nullptr;
                }
            }
        private:
            std::weak_ptr<void> m_weak_ptr;
        };

        struct tracer_entry
        {
            tracer_entry(std::string type, bool detail): m_type(std::move(type)), m_detail(detail) {}
            tracer_entry(const tracer_entry &) = delete;
            tracer_entry & operator= (const tracer_entry &) = delete;

            ~tracer_entry()
            {
                std::lock_guard lk(m_mux);
                for (auto [_, entry] : m_entries)
                {
                    entry.unref();
                }
                m_entries.clear();
                m_ref_count = 0;
            }

            void add_entry(std::shared_ptr<void> ptr)
            {
                m_ref_count ++;
                if (m_detail)
                {
                    std::lock_guard lk(m_mux);
                    m_entries.emplace(reinterpret_cast<std::uintptr_t>(ptr.get()), std::weak_ptr<void>(ptr));
                }
            }

            void remove_entry(void * pointer)
            {
                -- m_ref_count;
                if (m_detail)
                {
                    std::lock_guard lk(m_mux);
                    m_entries.erase(reinterpret_cast<std::uintptr_t>(pointer));
                }
            }

            std::uint64_t ref_count() const noexcept
            {
                return m_ref_count.load();
            }

            bool has_detail() const noexcept
            {
                return m_detail;
            }
        private:
            std::string m_type;
            bool m_detail;
            std::atomic_uint64_t m_ref_count {0};
            mutable mutex m_mux;
            std::unordered_map<std::uintptr_t, object_entry> m_entries;
        };

        struct object_deleter
        {
            void operator()(void * ptr)
            {
                if (entry)
                {
                    entry->remove_entry(ptr);
                }
                delete ptr;
            }

            tracer_entry * entry = nullptr;
        };

        struct tracer_state
        {
            #ifdef CFGO_ALLOCATE_TRACER_DETAIL
            static constexpr bool DEFAULT_DETAIL = true;
            #else
            static constexpr bool DEFAULT_DETAIL = false;
            #endif

            ~tracer_state()
            {
                std::lock_guard lk(m_mux);
                m_entries.clear();
            }

            template<typename T>
            requires allocate_tracer_support_detail<T>
            static constexpr bool is_detail()
            {
                return T::allocate_tracer_detail;
            }

            template<typename T>
            static constexpr bool is_detail()
            {
                return DEFAULT_DETAIL;
            }

            template<typename T>
            tracer_entry & entry(const T * pointer)
            {
                return _entry(typeid(pointer), is_detail<T>());
            }

            const tracer_entry * entry(const std::type_info & type_id)
            {
                std::lock_guard lk(m_mux);
                auto iter = m_entries.find(std::type_index(type_id));
                return iter != m_entries.end() ? &iter->second : nullptr;
            }

            template<typename T, typename... Args>
            std::shared_ptr<T> make_shared(Args &&... args)
            {
                auto ptr = new T(std::forward<Args>(args)...);
                auto & entry = this->entry(ptr);
                auto sptr = std::shared_ptr<T>(ptr, object_deleter{ .entry = &entry });
                entry.add_entry(sptr);
                return sptr;
            }
        private:
            mutable mutex m_mux;
            std::unordered_map<std::type_index, tracer_entry> m_entries;

            tracer_entry & _entry(const std::type_info & type_id, bool detail)
            {
                std::lock_guard lk(m_mux);
                auto [iter, _] = m_entries.try_emplace(std::type_index(type_id), boost::core::demangle(type_id.name()), detail);
                return iter->second;
            }
        };
    };
    
} // namespace cfgo

#endif