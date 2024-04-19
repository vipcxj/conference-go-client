#ifndef _CFGO_COROUTINE_CONCEPTS_HPP_
#define _CFGO_COROUTINE_CONCEPTS_HPP_

#include <type_traits>
#include <concepts>
#include <coroutine>

namespace cfgo
{
    using namespace std;

    template<typename _Tp>
    struct __is_valid_await_suspend_return_type : false_type {};

    template<>
    struct __is_valid_await_suspend_return_type<bool> : true_type {};

    template<>
    struct __is_valid_await_suspend_return_type<void> : true_type {};

    template<typename _Promise>
    struct __is_valid_await_suspend_return_type<coroutine_handle<_Promise>> : true_type {};

    template<typename _Tp>
    concept _AwaitSuspendReturnType = __is_valid_await_suspend_return_type<_Tp>::value;

    template<typename _Tp>
    concept Awaiter = requires(_Tp && __awaiter, coroutine_handle<void> __h)
    {
        // await_ready() result must be contextually convertible to bool.
        __awaiter.await_ready() ? void() : void();
        __awaiter.await_suspend(__h);
        requires _AwaitSuspendReturnType<decltype(
            __awaiter.await_suspend(__h)
        )>;
        __awaiter.await_resume();
    };
    
    template<typename _Tp, typename _Result>
    concept AwaiterOf = 
        Awaiter<_Tp> &&
        requires(_Tp && __awaiter)
        {
            { __awaiter.await_resume() } -> convertible_to<_Result>;
        };

    template<typename _Tp>
    concept _WeakHasMemberCoAwait = requires(_Tp && __awaitable)
    {
        static_cast<_Tp &&>(__awaitable).operator co_await();
    };
    
    template<typename _Tp>
    concept _WeakHasNonMemberCoAwait = requires(_Tp && __awaitable)
    {
        operator co_await(static_cast<_Tp &&>(__awaitable));
    };

    template<_WeakHasMemberCoAwait _Tp>
    decltype(auto) get_awaiter(_Tp && __awaitable) noexcept(noexcept(static_cast<_Tp &&>(__awaitable).operator co_await()))
    {
        return static_cast<_Tp &&>(__awaitable).operator co_await();
    }
    
    template<_WeakHasNonMemberCoAwait _Tp>
    decltype(auto) get_awaiter(_Tp && __awaitable) noexcept(noexcept(operator co_await(static_cast<_Tp &&>(__awaitable))))
    {
        return operator co_await(static_cast<_Tp &&>(__awaitable));
    }
    
    template<typename _Tp>
        requires (!_WeakHasNonMemberCoAwait<_Tp>) && (! _WeakHasMemberCoAwait<_Tp>)
    _Tp && get_awaiter(_Tp && __awaitable) noexcept
    {
        return static_cast<_Tp &&>(__awaitable);
    }
    
    template<typename _Tp>
    struct awaiter_type
    {
        using type = decltype(get_awaiter(std::declval<_Tp>()));
    };
    
    template<typename _Tp>
    using awaiter_type_t = typename awaiter_type<_Tp>::type;
    
    template<typename _Tp>
    concept Awaitable = 
        movable<_Tp> && 
        requires(_Tp && __awaitable)
        {
            { get_awaiter(static_cast<_Tp &&>(__awaitable)) } -> Awaiter;
        };
        
    template<typename _Tp, typename _Result>
    concept AwaitableOf = 
        Awaitable<_Tp> && 
        requires(_Tp && __awaitable)
        {
            { get_awaiter(static_cast<_Tp &&>(__awaitable)) } -> AwaiterOf<_Result>;
        };
    
    template<typename _Tp>
    struct await_result {};
    
    template<Awaitable _Tp>
    struct await_result<_Tp>
    {
        using type = decltype(std::declval<awaiter_type_t<_Tp> &>().await_resume());
    };
    
    template<typename _Tp>
    using await_result_t = typename await_result<_Tp>::type;
} // namespace cfgo


#endif