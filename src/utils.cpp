#include "cfgo/utils.hpp"
#include <system_error>
#ifdef _WIN32
#include <windows.h>    //GetModuleFileNameW
#else
#include <limits.h>
#include <unistd.h>     //readlink
#endif

namespace cfgo {

    std::filesystem::path getexepath()
    {
    #ifdef _WIN32
        wchar_t path[MAX_PATH] = { 0 };
        GetModuleFileNameW(NULL, path, MAX_PATH);
        return path;
    #else
        char result[PATH_MAX];
        ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
        return std::string(result, (count > 0) ? count : 0);
    #endif
    }

    std::filesystem::path getexedir()
    {
        using namespace std::filesystem;
        return weakly_canonical(path(getexepath()).parent_path());
    }

    std::string what(const std::exception_ptr &eptr)
    {
        if (!eptr) { throw std::bad_exception(); }

        try { std::rethrow_exception(eptr); }
        catch (const std::exception  &e)  { return e.what()   ; }
        catch (const std::string     &e)  { return e          ; }
        catch (const char            *e)  { return e          ; }
        catch (const std::error_code &e) { return e.message(); }
        catch (...)                      { return "who knows"; }
    }
}