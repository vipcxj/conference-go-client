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
#include "cfgo/algorithm.hpp"


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
    class close_allocate_tracer
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
            #ifdef CFGO_CLOSER_ALLOCATE_TRACER_DETAIL
            mutex& mux;
            closer_tracer_entries_type& entries;
            closer_tracer_locs_type& locs;
            #endif
        };
        
        static closer_tracer_ref global_closer_tracer() noexcept
        {
            static std::atomic_int64_t g_ref_count;
            #ifdef CFGO_CLOSER_ALLOCATE_TRACER_DETAIL
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
        #ifdef CFGO_CLOSER_ALLOCATE_TRACER_DETAIL
            std::uintptr_t key,
            std::uintptr_t parent,
            std::source_location src_loc
        #endif
        ) noexcept
        {
            auto tracer = global_closer_tracer();
            tracer.ref_count ++;
            #ifdef CFGO_CLOSER_ALLOCATE_TRACER_DETAIL
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
        #ifdef CFGO_CLOSER_ALLOCATE_TRACER_DETAIL
            std::uintptr_t key
        #endif
        ) noexcept
        {
            auto tracer = global_closer_tracer();
            tracer.ref_count --;
            #ifdef CFGO_CLOSER_ALLOCATE_TRACER_DETAIL
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

        #ifdef CFGO_CLOSER_ALLOCATE_TRACER_DETAIL

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

    class signal_allocate_tracer
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

    struct allocate_tracers
    {

        template<typename T>
        struct object_deleter;
        struct object_entry
        {
        };

        struct tracer_entry
        {
            tracer_entry(std::string type, bool detail): m_type(std::move(type)), m_detail(detail) {}
            tracer_entry(const tracer_entry &) = delete;
            tracer_entry & operator= (const tracer_entry &) = delete;

            void add_entry(std::uintptr_t pointer)
            {
                m_ref_count ++;
                if (m_detail)
                {
                    std::lock_guard lk(m_mux);
                    m_entries.emplace(pointer, object_entry {});
                }
            }

            void remove_entry(std::uintptr_t pointer)
            {
                -- m_ref_count;
                if (m_detail)
                {
                    std::lock_guard lk(m_mux);
                    m_entries.erase(pointer);
                }
            }

            std::uint64_t ref_count() const noexcept
            {
                return m_ref_count.load();
            }

            const std::string & type_name() const noexcept
            {
                return m_type;
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

        template<typename T>
        struct object_deleter
        {
            constexpr object_deleter() noexcept = default;
            object_deleter(std::weak_ptr<tracer_entry> weak_ptr) noexcept: weak_entry(std::move(weak_ptr)) {}
            template<typename U>
            requires std::convertible_to<U*, T*>
            object_deleter(const object_deleter<U> &) noexcept { }

            void operator()(T * ptr)
            {
                if (auto entry = weak_entry.lock())
                {
                    entry->remove_entry(reinterpret_cast<std::uintptr_t>(ptr));
                }
                static_assert(!std::is_void<T>::value, "can't delete pointer to incomplete type");
                static_assert(sizeof(T) > 0, "can't delete pointer to incomplete type");
                delete ptr;
            }

            std::weak_ptr<tracer_entry> weak_entry;
        };

        struct tracer_state
        {
            #ifdef CFGO_GENERAL_ALLOCATE_TRACER_DETAIL
            static constexpr bool DEFAULT_DETAIL = true;
            #else
            static constexpr bool DEFAULT_DETAIL = false;
            #endif

            static tracer_state & instance()
            {
                static tracer_state state;
                return state;
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
            std::shared_ptr<tracer_entry> make_entry(const T * pointer)
            {
                return _entry(typeid(*const_cast<T *>(pointer)), is_detail<T>());
            }

            template<typename T>
            std::shared_ptr<tracer_entry> get_entry(const T * pointer)
            {
                return get_entry(typeid(*const_cast<T *>(pointer)));
            }

            std::shared_ptr<tracer_entry> get_entry(const std::type_info & type_id) const
            {
                std::lock_guard lk(m_mux);
                auto iter = m_entries.find(std::type_index(type_id));
                return iter != m_entries.end() ? iter->second : nullptr;
            }

            std::uint64_t ref_count(const std::type_info & type_id) const
            {
                std::lock_guard lk(m_mux);
                auto iter = m_entries.find(std::type_index(type_id));
                return iter != m_entries.end() ? iter->second->ref_count() : 0;
            }

            template<typename T, typename... Args>
            std::shared_ptr<T> make_shared(Args &&... args)
            {
                auto ptr = new T(std::forward<Args>(args)...);
                auto entry = this->make_entry(ptr);
                auto sptr = std::shared_ptr<T>(ptr, object_deleter<T>{ entry });
                entry->add_entry(reinterpret_cast<std::uintptr_t>(ptr));
                return sptr;
            }

            template<typename T, typename... Args>
            std::unique_ptr<T, object_deleter<T>> make_unique(Args &&... args)
            {
                auto ptr = new T(std::forward<Args>(args)...);
                auto entry = this->make_entry(ptr);
                auto uptr = std::unique_ptr<T, object_deleter<T>>(ptr, object_deleter<T>{ entry });
                entry->add_entry(reinterpret_cast<std::uintptr_t>(ptr));
                return std::move(uptr);
            }

        private:
            mutable mutex m_mux;
            std::unordered_map<std::type_index, std::shared_ptr<tracer_entry>> m_entries;

            tracer_state() {}
            tracer_state(const tracer_state &) = delete;
            tracer_state & operator= (const tracer_state &) = delete;

            std::shared_ptr<tracer_entry> _entry(const std::type_info & type_id, bool detail)
            {
                std::lock_guard lk(m_mux);
                auto [iter, _] = m_entries.emplace(std::type_index(type_id), std::make_shared<tracer_entry>(boost::core::demangle(type_id.name()), detail));
                return iter->second;
            }

            friend struct allocate_tracers;
        };

        #ifdef CFGO_GENERAL_ALLOCATE_TRACER
        template<typename T, typename... Args>
        static std::shared_ptr<T> make_shared(Args &&... args)
        {
            return tracer_state::instance().make_shared<T>(std::forward<Args>(args)...);
        }

        template<typename T>
        using unique_ptr = std::unique_ptr<T, object_deleter<T>>;

        template<typename T, typename... Args>
        static std::unique_ptr<T, object_deleter<T>> make_unique(Args &&... args)
        {
            return tracer_state::instance().make_unique<T>(std::forward<Args>(args)...);
        }

        static std::uint64_t ref_count(const std::type_info & type_id)
        {
            return tracer_state::instance().ref_count(type_id);
        }

        using tracer_entry_result_set = std::vector<std::reference_wrapper<const tracer_entry>>;

        static void collect_max_n_ref_count(tracer_entry_result_set & result, int n)
        {
            auto & state = tracer_state::instance();
            std::lock_guard lk(state.m_mux);
            using iter_t = decltype(state.m_entries)::iterator; // std::ranges::borrowed_iterator_t<decltype(state.m_entries)>;
            result.clear();
            for (auto & iter : max_n_elements(state.m_entries, n, [](iter_t iter1, iter_t iter2) -> bool {
                return iter1->second->ref_count() > iter2->second->ref_count();
            }))
            {
                result.push_back(*iter->second);
            }
        }
        #else
        template<typename T, typename... Args>
        static std::shared_ptr<T> make_shared(Args &&... args)
        {
            return std::make_shared<T>(std::forward<Args>(args)...);
        }

        template<typename T>
        using unique_ptr = std::unique_ptr<T>;

        template<typename T, typename... Args>
        static std::unique_ptr<T> make_unique(Args &&... args)
        {
            return std::make_unique<T>(std::forward<Args>(args)...);
        }

        static constexpr std::uint64_t ref_count(const std::type_info & type_id)
        {
            return 0;
        }

        static void collect_max_n_ref_count(std::vector<std::reference_wrapper<const tracer_entry>>& result, int n)
        {}
        #endif


    };
    
} // namespace cfgo

#endif