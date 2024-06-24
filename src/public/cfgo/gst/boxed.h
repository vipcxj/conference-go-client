#ifndef _CFGO_GST_BOXED_HPP_
#define _CFGO_GST_BOXED_HPP_

#ifdef __cplusplus
extern "C" {
#endif

#define _CFGO_DECLARE_BOXED_PTR_LIKE(Type, type) \
    struct CfgoBoxed ## Type; \
    GType cfgo_boxed_ ## type ## _get_type(void); \
    CfgoBoxed ## Type * cfgo_boxed_ ## type ## _copy(CfgoBoxed ## Type * ptr); \
    void cfgo_boxed_ ## type ## _free(CfgoBoxed ## Type * ptr);
#define CFGO_DECLARE_BOXED_PTR_LIKE(Type, type) _CFGO_DECLARE_BOXED_PTR_LIKE(Type, type)

#define _CFGO_DECLARE_CPP_BOXED_PTR_LIKE(TYPE, Type, type) \
    struct CfgoBoxed ## Type { \
        TYPE ptr = nullptr; \
    }; \
    CfgoBoxed ## Type * cfgo_boxed_ ## type ## _make(const TYPE & inst);
#define CFGO_DECLARE_CPP_BOXED_PTR_LIKE(TYPE, Type, type) _CFGO_DECLARE_CPP_BOXED_PTR_LIKE(TYPE, Type, type)

#define _CFGO_DEFINE_BOXED_PTR_LIKE(TYPE, Type, type) \
    CfgoBoxed ## Type * cfgo_boxed_ ## type ## _make(const TYPE & inst) \
    { \
        CfgoBoxed ## Type * copy_ptr = new CfgoBoxed ## Type(); \
        copy_ptr->ptr = inst; \
        return copy_ptr; \
    } \
    CfgoBoxed ## Type * cfgo_boxed_ ## type ## _copy(CfgoBoxed ## Type * ptr) \
    { \
        CfgoBoxed ## Type * copy_ptr = new CfgoBoxed ## Type(); \
        copy_ptr->ptr = ptr->ptr; \
        return copy_ptr; \
    } \
    void cfgo_boxed_ ## type ## _free(CfgoBoxed ## Type * ptr) \
    { \
        if (ptr) \
        { \
            delete ptr; \
        } \
    } \
    G_DEFINE_BOXED_TYPE(CfgoBoxed ## Type, cfgo_boxed_ ## type, cfgo_boxed_ ## type ## _copy, cfgo_boxed_ ## type ## _free)
#define CFGO_DEFINE_BOXED_PTR_LIKE(TYPE, Type, type) _CFGO_DEFINE_BOXED_PTR_LIKE(TYPE, Type, type)

#ifdef __cplusplus
}
#endif

#endif