#ifndef _CFGO_GST_CU_CUH_
#define _CFGO_GST_CU_CUH_

#include <cuda.h>
#include <cuda_fp16.h>

#define cudaCheckErrors(msg) \
    do { \
        cudaError_t __err = cudaGetLastError(); \
        if (__err != cudaSuccess) { \
            fprintf(stderr, "Fatal error: %s (%s at %s:%d)\n", \
                msg, cudaGetErrorString(__err), \
                __FILE__, __LINE__); \
            fprintf(stderr, "*** FAILED - ABORTING\n"); \
            exit(1); \
        } \
    } while (0)

namespace cfgo
{
    namespace gst
    {
        void copy_frame(unsigned char * d_src, int stride, unsigned char * d_target, int frame, int index, CUstream stream);
        void copy_ai_input(unsigned char * d_src, int src_index, half * d_target, int tgt_index, CUstream stream);
    } // namespace gst
    
} // namespace cfgo

#endif