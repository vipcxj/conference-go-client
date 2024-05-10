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
    #if defined(_WIN32)
        #pragma message("Debug mode detected")
    #endif
#else
    #if defined(_WIN32)
        #pragma message("Not debug mode detected")
    #endif
#endif


#define _CFGO_CAT(v1, v2) v1##v2
#define CFGO_CAT(v1, v2) _CFGO_CAT(v1, v2)

#define _CFGO_QT(c) #c
#define CFGO_QT(c) _CFGO_QT(c)

#define _CFGO_MAKE_PTR_TYPE(TYPE) \
    TYPE(const TYPE &) = delete; \
    TYPE & operator = (const TYPE &) = delete
#define CFGO_MAKE_PTR_TYPE(TYPE) _CFGO_MAKE_PTR_TYPE(TYPE)

#define _CFGO_DEC_READONLY_COPY(TYPE, NAME) \
    public: \
        inline TYPE NAME() const \
        { \
            return m_##NAME; \
        } \
    private: \
        TYPE m_##NAME
#define CFGO_DEC_READONLY_COPY(TYPE, NAME) _CFGO_DEC_READONLY_COPY(TYPE, NAME)

#define _CFGO_DEC_READONLY_REF(TYPE, NAME) \
    public: \
        inline TYPE const & NAME() const \
        { \
            return m_##NAME; \
        } \
        inline TYPE & NAME() \
        { \
            return m_##NAME; \
        } \
    private: \
        TYPE m_##NAME
#define CFGO_DEC_READONLY_REF(TYPE, NAME) _CFGO_DEC_READONLY_REF(TYPE, NAME)

#define _CFGO_DEC_READWRITE_COPY(TYPE, NAME) \
    public: \
        inline TYPE NAME() const \
        { \
            return m_##NAME; \
        } \
        inline void set_##NAME(TYPE value) \
        { \
            m_##NAME = value; \
        } \
    private: \
        TYPE m_##NAME
#define CFGO_DEC_READWRITE_COPY(TYPE, NAME) _CFGO_DEC_READWRITE_COPY(TYPE, NAME)

#define _CFGO_DEC_READWRITE_REF(TYPE, NAME) \
    public: \
        inline TYPE const & NAME() const \
        { \
            return m_##NAME; \
        } \
        inline TYPE & NAME() \
        { \
            return m_##NAME; \
        } \
        inline void set_##NAME(const TYPE & value) \
        { \
            m_##NAME = value; \
        } \
        inline void set_##NAME(TYPE && value) \
        { \
            m_##NAME = std::move(value); \
        } \
    private: \
        TYPE m_##NAME
#define CFGO_DEC_READWRITE_REF(TYPE, NAME) _CFGO_DEC_READWRITE_REF(TYPE, NAME)

#define _CFGO_DEC_READONLY_COPY_WITH_DEFAULT(TYPE, NAME, DEFAULT) \
    public: \
        inline TYPE NAME() const \
        { \
            return m_##NAME; \
        } \
    private: \
        TYPE m_##NAME = DEFAULT
#define CFGO_DEC_READONLY_COPY_WITH_DEFAULT(TYPE, NAME, DEFAULT) _CFGO_DEC_READONLY_COPY_WITH_DEFAULT(TYPE, NAME, DEFAULT)

#define _CFGO_DEC_READONLY_REF_WITH_DEFAULT(TYPE, NAME, DEFAULT) \
    public: \
        inline TYPE const & NAME() const \
        { \
            return m_##NAME; \
        } \
        inline TYPE & NAME() \
        { \
            return m_##NAME; \
        } \
    private: \
        TYPE m_##NAME = DEFAULT;
#define CFGO_DEC_READONLY_REF_WITH_DEFAULT(TYPE, NAME, DEFAULT) _CFGO_DEC_READONLY_REF_WITH_DEFAULT(TYPE, NAME, DEFAULT)

#define _CFGO_DEC_READWRITE_COPY_WITH_DEFAULT(TYPE, NAME, DEFAULT) \
    public: \
        inline TYPE NAME() const \
        { \
            return m_##NAME; \
        } \
        inline void set_##NAME(TYPE value) \
        { \
            m_##NAME = value; \
        } \
    private: \
        TYPE m_##NAME = DEFAULT
#define CFGO_DEC_READWRITE_COPY_WITH_DEFAULT(TYPE, NAME, DEFAULT) _CFGO_DEC_READWRITE_COPY_WITH_DEFAULT(TYPE, NAME, DEFAULT)

#define _CFGO_DEC_READWRITE_REF_WITH_DEFAULT(TYPE, NAME, DEFAULT) \
    public: \
        inline TYPE const & NAME() const \
        { \
            return m_##NAME; \
        } \
        inline TYPE & NAME() \
        { \
            return m_##NAME; \
        } \
        inline void set_##NAME(const TYPE & value) \
        { \
            m_##NAME = value; \
        } \
        inline void set_##NAME(TYPE && value) \
        { \
            m_##NAME = std::move(value); \
        } \
    private: \
        TYPE m_##NAME = DEFAULT
#define CFGO_DEC_READWRITE_REF_WITH_DEFAULT(TYPE, NAME, DEFAULT) _CFGO_DEC_READWRITE_REF_WITH_DEFAULT(TYPE, NAME, DEFAULT)

#endif