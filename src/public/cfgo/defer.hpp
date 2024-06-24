#ifndef _CFGO_DEFER_HPP_
#define _CFGO_DEFER_HPP_

#include <unordered_map>

namespace cfgo
{
    namespace detail
    {
        template <typename F>
        class defer_raii
        {
        public:
            // copy/move construction and any kind of assignment would lead to the cleanup function getting
            // called twice. We can't have that.
            defer_raii(defer_raii &&) = delete;
            defer_raii(const defer_raii &) = delete;
            defer_raii &operator=(const defer_raii &) = delete;
            defer_raii &operator=(defer_raii &&) = delete;

            // construct the object from the given callable
            template <typename FF>
            defer_raii(FF &&f) : cleanup_function(std::forward<FF>(f)) {}

            // when the object goes out of scope call the cleanup function
            ~defer_raii() { cleanup_function(); }

        private:
            F cleanup_function;
        };
    } // namespace detail

    template <typename F>
    detail::defer_raii<F> defer(F &&f)
    {
        return {std::forward<F>(f)};
    }

    class defers_when_fail
    {
    public:
        // copy/move construction and any kind of assignment would lead to the cleanup function getting
        // called twice. We can't have that.
        defers_when_fail(defers_when_fail &&) = delete;
        defers_when_fail(const defers_when_fail &) = delete;
        defers_when_fail &operator=(const defers_when_fail &) = delete;
        defers_when_fail &operator=(defers_when_fail &&) = delete;

        defers_when_fail() : m_success(false), m_i(0) {}
        ~defers_when_fail()
        {
            if (m_success)
            {
                return;
            }
            for (auto &&f : m_cleanups)
            {
                f.second();
            }
        }
        int add_defer(std::function<void()> f)
        {
            int i = m_i++;
            m_cleanups[i] = std::move(f);
            return i;
        };
        void remove_defer(int i)
        {
            m_cleanups.erase(i);
        }
        void success()
        {
            m_success = true;
        }

    private:
        int m_i;
        bool m_success;
        std::unordered_map<int, std::function<void()>> m_cleanups;
    };

#define DEFER_ACTUALLY_JOIN(x, y) x##y
#define DEFER_JOIN(x, y) DEFER_ACTUALLY_JOIN(x, y)
#ifdef __COUNTER__
#define DEFER_UNIQUE_VARNAME(x) DEFER_JOIN(x, __COUNTER__)
#else
#define DEFER_UNIQUE_VARNAME(x) DEFER_JOIN(x, __LINE__)
#endif

#define DEFER(lambda__) [[maybe_unused]] const auto &DEFER_UNIQUE_VARNAME(defer_object) = cfgo::defer([&]() lambda__)
#define DEFERS_WHEN_FAIL(name) \
    cfgo::defers_when_fail name {}
#define ADD_DEFER(name, lambda__) name.add_defer([&]() lambda__)
} // namespace cfgo

#endif