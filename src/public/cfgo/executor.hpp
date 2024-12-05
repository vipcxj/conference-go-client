#ifndef _SR_EXECUTOR_HPP_
#define _SR_EXECUTOR_HPP_

#include "cfgo/asio.hpp"
#include "cfgo/move_only_function.hpp"
#include <concepts>

namespace sr
{
    class executor_proxy_t
    {
    public:
        using executor_execute_t = cfgo::unique_function<void()>;
        using fun_t = std::function<void(executor_execute_t)>;
        
        template<typename F>
        requires std::convertible_to<F, fun_t>
        executor_proxy_t(F && f): fun_(std::make_shared<fun_t>(std::forward<F>(f))) {}
        executor_proxy_t(const executor_proxy_t & other) noexcept: fun_(other.fun_) {}
        executor_proxy_t & operator=(const executor_proxy_t & other) noexcept
        {
            fun_ = other.fun_;
            return *this;
        }
        executor_proxy_t(executor_proxy_t && other) noexcept: fun_(std::move(other.fun_)) {}
        executor_proxy_t & operator=(executor_proxy_t && other) noexcept
        {
            fun_ = std::move(other.fun_);
            return *this;
        }
        void operator()(executor_execute_t f) const
        {
            (*fun_)(std::move(f));
        }
        friend bool operator==(const executor_proxy_t & a, const executor_proxy_t & b) noexcept
        {
            return a.fun_ == b.fun_;
        }
        friend bool operator!=(const executor_proxy_t & a, const executor_proxy_t & b) noexcept
        {
            return !(a == b);
        }
    private:
        std::shared_ptr<fun_t> fun_;
    };

    template<typename T>
    concept asio_executor = asio::is_executor<T>::value;

    template<asio_executor Executor>
    class wrappable_executor
    {
    public:
        using duration_t = std::chrono::high_resolution_clock::duration;
        using inner_executor_type = Executor;

        template<typename OtherExecutor>
        requires std::convertible_to<OtherExecutor, Executor>
        explicit wrappable_executor(OtherExecutor && executor, executor_proxy_t proxy): 
            m_executor(std::forward<OtherExecutor>(executor)),
            m_proxy(std::move(proxy))
        {}

        wrappable_executor(const wrappable_executor& other) noexcept:
            m_executor(other.m_executor),
            m_proxy(other.m_proxy)
        {}

        template<asio_executor OtherExecutor>
        requires std::convertible_to<OtherExecutor, Executor>
        wrappable_executor(const wrappable_executor<OtherExecutor>& other) noexcept:
            m_executor(other.m_executor),
            m_proxy(other.m_proxy)
        {}

        wrappable_executor & operator=(const wrappable_executor & other) noexcept
        {
            m_executor = other.m_executor;
            m_proxy = other.m_proxy;
            return *this;
        }

        template<asio_executor OtherExecutor>
        requires std::convertible_to<OtherExecutor, Executor>
        wrappable_executor & operator=(const wrappable_executor<OtherExecutor> & other) noexcept
        {
            m_executor = other.m_executor;
            m_proxy = other.m_proxy;
            return *this;
        }

        wrappable_executor(wrappable_executor&& other) noexcept:
            m_executor(std::move(other.m_executor)),
            m_proxy(std::move(other.m_proxy))
        {}

        template<asio_executor OtherExecutor>
        requires std::convertible_to<OtherExecutor, Executor>
        wrappable_executor(wrappable_executor<OtherExecutor> && other) noexcept:
            m_executor(std::move(other.m_executor)),
            m_proxy(std::move(other.m_proxy))
        {}

        wrappable_executor & operator=(wrappable_executor && other) noexcept
        {
            m_executor = std::move(other.m_executor);
            m_proxy = std::move(other.m_proxy);
            return *this;
        }

        template<asio_executor OtherExecutor>
        requires std::convertible_to<OtherExecutor, Executor>
        wrappable_executor & operator=(wrappable_executor<OtherExecutor> && other) noexcept
        {
            m_executor = std::move(other.m_executor);
            m_proxy = std::move(other.m_proxy);
            return *this;
        }

        ~wrappable_executor() noexcept {}

        friend bool operator==(const wrappable_executor & a, const wrappable_executor & b) noexcept
        {
            return a.m_executor == b.m_executor && a.m_proxy == b.m_proxy;
        }

        friend bool operator!=(const wrappable_executor & a, const wrappable_executor & b) noexcept
        {
            return !(a == b);
        }

        /// Obtain the underlying executor.
        inner_executor_type get_inner_executor() const noexcept
        {
            return m_executor;
        }

        template<typename Property>
        requires asio::can_query_v<const Executor &, Property>
        auto query(const Property& p) const noexcept(asio::is_nothrow_query_v<const Executor &, Property>) -> decltype(asio::query(std::declval<Executor>(), p))
        {
            return asio::query(m_executor, p);
        }

        template<typename Property>
        requires asio::can_require_v<const Executor &, Property> && (!std::is_convertible_v<Property, asio::execution::blocking_t::always_t>)
        auto require(const Property& p) const noexcept(asio::is_nothrow_require_v<const Executor &, Property>)
        {
            return wrappable_executor<asio::decay_t<asio::require_result_t<const Executor &, Property>>> (
                asio::require(m_executor, p),
                m_proxy
            );
        }

        template<typename Property>
        requires asio::can_prefer_v<const Executor &, Property> && (!std::is_convertible_v<Property, asio::execution::blocking_t::always_t>)
        auto prefer(const Property& p) const noexcept(asio::is_nothrow_prefer_v<const Executor &, Property>)
        {
            return wrappable_executor<asio::decay_t<asio::prefer_result_t<const Executor &, Property>>> (
                asio::prefer(m_executor, p),
                m_proxy
            );
        }

        /// Obtain the underlying execution context.
        asio::execution_context & context() const noexcept
        requires requires (const Executor & ex) {
            ex.context();
        }
        {
            return m_executor.context();
        }

        /// Inform the strand that it has some outstanding work to do.
        /**
         * The strand delegates this call to its underlying executor.
         */
        void on_work_started() const noexcept
        requires requires (const Executor & ex) {
            ex.on_work_started();
        }
        {
            m_executor.on_work_started();
        }

        /// Inform the strand that some work is no longer outstanding.
        /**
         * The strand delegates this call to its underlying executor.
         */
        void on_work_finished() const noexcept
        requires requires (const Executor & ex) {
            ex.on_work_finished();
        }
        {
            m_executor.on_work_finished();
        }

        /// Request the strand to invoke the given function object.
        /**
         * This function is used to ask the strand to execute the given function
         * object on its underlying executor. The function object will be executed
         * according to the properties of the underlying executor.
         *
         * @param f The function object to be called. The executor will make
         * a copy of the handler object as required. The function signature of the
         * function object must be: @code void function(); @endcode
         */
        template <typename Function>
        requires asio::traits::execute_member<const Executor &, Function>::is_valid
        void execute(Function&& f) const
        {
            m_executor.execute([proxy = m_proxy, f = std::move(f)] () mutable {
                proxy(std::move(f));
            });
        }

        /// Request the strand to invoke the given function object.
        /**
         * This function is used to ask the strand to execute the given function
         * object on its underlying executor. The function object will be executed
         * inside this function if the strand is not otherwise busy and if the
         * underlying executor's @c dispatch() function is also able to execute the
         * function before returning.
         *
         * @param f The function object to be called. The executor will make
         * a copy of the handler object as required. The function signature of the
         * function object must be: @code void function(); @endcode
         *
         * @param a An allocator that may be used by the executor to allocate the
         * internal storage needed for function invocation.
         */
        template <typename Function, typename Allocator>
        void dispatch(Function&& f, const Allocator& a) const
        {
            m_executor.dispatch([proxy = m_proxy, f = std::move(f)] () mutable {
                proxy(std::move(f));
            }, a);
        }

        /// Request the strand to invoke the given function object.
        /**
         * This function is used to ask the executor to execute the given function
         * object. The function object will never be executed inside this function.
         * Instead, it will be scheduled by the underlying executor's defer function.
         *
         * @param f The function object to be called. The executor will make
         * a copy of the handler object as required. The function signature of the
         * function object must be: @code void function(); @endcode
         *
         * @param a An allocator that may be used by the executor to allocate the
         * internal storage needed for function invocation.
         */
        template <typename Function, typename Allocator>
        void post(Function&& f, const Allocator& a) const
        {
            m_executor.post([proxy = m_proxy, f = std::move(f)] () mutable {
                proxy(std::move(f));
            }, a);
        }

        /// Request the strand to invoke the given function object.
        /**
         * This function is used to ask the executor to execute the given function
         * object. The function object will never be executed inside this function.
         * Instead, it will be scheduled by the underlying executor's defer function.
         *
         * @param f The function object to be called. The executor will make
         * a copy of the handler object as required. The function signature of the
         * function object must be: @code void function(); @endcode
         *
         * @param a An allocator that may be used by the executor to allocate the
         * internal storage needed for function invocation.
         */
        template <typename Function, typename Allocator>
        void defer(Function&& f, const Allocator& a) const
        {
            m_executor.defer([proxy = m_proxy, f = std::move(f)] () mutable {
                proxy(std::move(f));
            }, a);
        }

    private:
        Executor m_executor;
        executor_proxy_t m_proxy;
    };
    
} // namespace sr

#ifdef STANDALONE_ASIO

    namespace asio
    {
        namespace traits
        {

#else

namespace boost
{
    namespace asio
    {
        namespace traits
        {

#endif

            template <typename Executor>
            struct equality_comparable<sr::wrappable_executor<Executor>>
            {
                static constexpr bool is_valid = true;
                static constexpr bool is_noexcept = true;
            };

            template <typename Executor, typename Function>
            struct execute_member<sr::wrappable_executor<Executor>, Function,
                enable_if_t<
                traits::execute_member<const Executor&, Function>::is_valid
                >>
            {
                static constexpr bool is_valid = true;
                static constexpr bool is_noexcept = false;
                typedef void result_type;
            };

            template <typename Executor, typename Property>
            struct query_member<sr::wrappable_executor<Executor>, Property,
                enable_if_t<
                can_query_v<const Executor&, Property>
                >>
            {
                static constexpr bool is_valid = true;
                static constexpr bool is_noexcept =
                    is_nothrow_query_v<const Executor &, Property>;
                typedef conditional_t<
                    is_convertible<Property, execution::blocking_t>::value,
                    execution::blocking_t, query_result_t<Executor, Property>> result_type;
            };

            template <typename Executor, typename Property>
            struct require_member<sr::wrappable_executor<Executor>, Property,
                enable_if_t<
                can_require_v<const Executor&, Property>
                    && !is_convertible<Property, execution::blocking_t::always_t>::value
                >>
            {
                static constexpr bool is_valid = true;
                static constexpr bool is_noexcept =
                    is_nothrow_require<Executor, Property>::value;
                typedef sr::wrappable_executor<decay_t<require_result_t<Executor, Property>>> result_type;
            };

            template <typename Executor, typename Property>
            struct prefer_member<sr::wrappable_executor<Executor>, Property,
                enable_if_t<
                can_prefer_v<const Executor&, Property>
                    && !is_convertible<Property, execution::blocking_t::always_t>::value
                >>
            {
                static constexpr bool is_valid = true;
                static constexpr bool is_noexcept =
                    is_nothrow_prefer<Executor, Property>::value;
                typedef sr::wrappable_executor<decay_t<prefer_result_t<Executor, Property>>> result_type;
            };

#ifdef STANDALONE_ASIO

        }
    }

#else
        }
    }
}

#endif

#endif