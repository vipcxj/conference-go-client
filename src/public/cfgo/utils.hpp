#ifndef _CFGO_UTILS_HPP_
#define _CFGO_UTILS_HPP_
#include <string>
#include <exception>
#include <memory>

// We haven't checked which filesystem to include yet
#ifndef INCLUDE_STD_FILESYSTEM_EXPERIMENTAL

// Check for feature test macro for <filesystem>
#   if defined(__cpp_lib_filesystem)
#       define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 0

// Check for feature test macro for <experimental/filesystem>
#   elif defined(__cpp_lib_experimental_filesystem)
#       define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 1

// We can't check if headers exist...
// Let's assume experimental to be safe
#   elif !defined(__has_include)
#       define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 1

// Check if the header "<filesystem>" exists
#   elif __has_include(<filesystem>)

// If we're compiling on Visual Studio and are not compiling with C++17, we need to use experimental
#       ifdef _MSC_VER

// Check and include header that defines "_HAS_CXX17"
#           if __has_include(<yvals_core.h>)
#               include <yvals_core.h>

// Check for enabled C++17 support
#               if defined(_HAS_CXX17) && _HAS_CXX17
// We're using C++17, so let's use the normal version
#                   define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 0
#               endif
#           endif

// If the marco isn't defined yet, that means any of the other VS specific checks failed, so we need to use experimental
#           ifndef INCLUDE_STD_FILESYSTEM_EXPERIMENTAL
#               define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 1
#           endif

// Not on Visual Studio. Let's use the normal version
#       else // #ifdef _MSC_VER
#           define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 0
#       endif

// Check if the header "<filesystem>" exists
#   elif __has_include(<experimental/filesystem>)
#       define INCLUDE_STD_FILESYSTEM_EXPERIMENTAL 1

// Fail if neither header is available with a nice error message
#   else
#       error Could not find system header "<filesystem>" or "<experimental/filesystem>"
#   endif

// We priously determined that we need the exprimental version
#   if INCLUDE_STD_FILESYSTEM_EXPERIMENTAL
// Include it
#       include <experimental/filesystem>

// We need the alias from std::experimental::filesystem to std::filesystem
namespace std {
    namespace filesystem = experimental::filesystem;
}

// We have a decent compiler and can use the normal version
#   else
// Include it
#       include <filesystem>
#   endif

#endif // #ifndef INCLUDE_STD_FILESYSTEM_EXPERIMENTAL

#include "cfgo/alias.hpp"

namespace cfgo
{
    std::filesystem::path getexepath();
    std::filesystem::path getexedir();
    std::string what(const std::exception_ptr &eptr = std::current_exception());
    constexpr const char * THIS_IS_IMPOSSIBLE = "This is impossible!";
    constexpr const char * NOT_IMPLEMENTED_YET = "The method not implemented yet!";

    template <typename T>
    using impl_ptr = std::shared_ptr<T>;
    template <typename T>
    class ImplBy
    {
    public:
        explicit ImplBy(impl_ptr<T> impl) : mImpl(std::move(impl)) {}
        template <typename... Args>
        explicit ImplBy(Args&&... args) : mImpl(std::make_shared<T>(std::forward<Args>(args)...)) {}
        ImplBy(ImplBy<T> &&cc) = default;
        ImplBy(const ImplBy<T> &) = default;

        virtual ~ImplBy() = default;

        ImplBy &operator=(ImplBy<T> &&cc) = default;
        ImplBy &operator=(const ImplBy<T> &) = default;

    protected:
        impl_ptr<T> & impl() noexcept { return mImpl; }
        const impl_ptr<T> & impl() const noexcept { return mImpl; }

    private:
        impl_ptr<T> mImpl;
    };

    template <typename T>
    class UniqueImplBy
    {
    public:
        explicit UniqueImplBy(impl_ptr<T> impl) : mImpl(std::move(impl)) {}
        template <typename... Args>
        explicit UniqueImplBy(Args&&... args) : mImpl(std::make_shared<T>(std::forward<Args>(args)...)) {}
        UniqueImplBy(UniqueImplBy<T> &&cc) = default;
        UniqueImplBy(const UniqueImplBy<T> &) = delete;

        virtual ~UniqueImplBy() = default;

        UniqueImplBy &operator=(UniqueImplBy<T> &&cc) = default;
        UniqueImplBy &operator=(const UniqueImplBy<T> &) = delete;

    protected:
        impl_ptr<T> & impl() noexcept { return mImpl; }
        const impl_ptr<T> & impl() const noexcept { return mImpl; }

    private:
        impl_ptr<T> mImpl;
    };

}

#endif