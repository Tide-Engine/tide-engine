typedef __UINT8_TYPE__ u8;
typedef __UINT16_TYPE__ u16;
typedef __UINT32_TYPE__ u32;
typedef u8 bytes16 __attribute__((vector_size(16)));
typedef signed char signs16 __attribute__((vector_size(16)));
typedef u16 shorts16 __attribute__((vector_size(32)));

static inline u32 portable_mask(signs16 comparison)
{
    const shorts16 weights = {1u,2u,4u,8u,16u,32u,64u,128u,256u,512u,1024u,2048u,4096u,8192u,16384u,32768u};
    const shorts16 wide = __builtin_convertvector(comparison, shorts16);
    return __builtin_reduce_or(wide & weights);
}

static inline u32 native_mask(signs16 comparison)
{
#if defined(__wasm_simd128__)
    return __builtin_wasm_bitmask_i8x16(comparison);
#elif defined(__SSE2__) && (defined(__i386__) || defined(__x86_64__))
    return (u32)__builtin_ia32_pmovmskb128(comparison);
#else
    return portable_mask(comparison);
#endif
}

u32 sparse_one(const u8 *p)
{
    bytes16 values;
    __builtin_memcpy(&values,p,sizeof values);
    return native_mask((signs16)(values != (bytes16){0}));
}

u32 sparse_two(const u8 *p, const u8 *q)
{
    bytes16 a,b;
    __builtin_memcpy(&a,p,sizeof a);
    __builtin_memcpy(&b,q,sizeof b);
    return native_mask((signs16)((a != (bytes16){0}) & (b == (bytes16){0})));
}

u32 sparse_portable(const u8 *p)
{
    bytes16 values;
    __builtin_memcpy(&values,p,sizeof values);
    return portable_mask((signs16)(values != (bytes16){0}));
}

u32 count_word(u32 word) { return (u32)__builtin_popcount(word); }
