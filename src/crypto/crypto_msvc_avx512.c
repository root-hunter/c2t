/* Build the AVX-512-only half of the MSVC SIMD implementation in its own
 * translation unit so the AVX2 fallback remains safe on non-AVX-512 CPUs. */
#define C2T_MSVC_AVX512_OBJECT 1
#include "crypto_msvc_simd.c"
