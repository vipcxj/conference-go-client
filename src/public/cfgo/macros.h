#ifndef _CFGO_MACROS_H_
#define _CFGO_MACROS_H_

#ifndef NDEBUG
// Production builds should set NDEBUG=1
#define NDEBUG false
#else
#define NDEBUG true
#endif

#ifndef DEBUG
#define DEBUG !NDEBUG
#endif

#if DEBUG
    #pragma message("Debug mode detected")
#else
    #pragma message("Not debug mode detected")
#endif

#endif