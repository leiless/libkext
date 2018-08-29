/**
 * Minimalist's kernel extension library
 *
 * Created 18H26  lynnl
 */

#ifndef LIBKEXT_H
#define LIBKEXT_H

#include <sys/types.h>

#define KEXT_NAME "TOFILL"

/*
 * Used to indicate unused function parameters
 * see: <sys/cdefs.h>#__unused
 */
#define UNUSED(arg0, ...)   (void) ((void) arg0, ##__VA_ARGS__)

/* G for GCC-specific */
#define GMIN(a, b) ({       \
    typeof (a) _a = (a);    \
    typeof (b) _b = (b);    \
    _a < _b ? _a : _b;      \
})

#define GMAX(a, b) ({       \
    typeof (a) _a = (a);    \
    typeof (b) _b = (b);    \
    _a > _b ? _a : _b;      \
})

#define panicf(fmt, ...)                \
    panic("\n" fmt "\n%s@%s#L%d\n\n",   \
            ##__VA_ARGS__, __BASE_FILE__, __FUNCTION__, __LINE__)

#ifdef DEBUG
/*
 * NOTE: Do NOT use any multi-nary conditional/logical operator inside assertion
 *       like operators && || ?:  it's extremely EVIL
 *       Separate them  each statement per line
 */
#define kassert(ex) (ex) ? (void) 0 : panicf("Assert `%s' failed", #ex)
/**
 * @ex      the expression
 * @fmt     panic message format
 *
 * Example: kassertf(sz > 0, "Why size %zd nonpositive?", sz);
 */
#define kassertf(ex, fmt, ...) \
    (ex) ? (void) 0 : panicf("Assert `%s' failed: " fmt, #ex, ##__VA_ARGS__)
#else
#define kassert(ex)             (void) ((void) (ex))
#define kassertf(ex, fmt, ...)  (void) ((void) (ex), ##__VA_ARGS__)
#endif

#define kassert_nonnull(ptr) kassert(((void *) ptr) != NULL)

/**
 * Branch predictions
 * see: linux/include/linux/compiler.h
 */
#define likely(x)       __builtin_expect(!!(x), 1)
#define unlikely(x)     __builtin_expect(!!(x), 0)

/**
 * os_log() is only available on macOS 10.12 or newer
 *  thus os_log do have compatibility issue  use printf instead
 *
 * XNU kernel version of printf() don't recognize some rarely used specifiers
 *  like h, i, j, t  use unrecognized spcifier may raise kernel panic
 *
 * Feel free to print NULL as %s  it checked explicitly by kernel-printf
 *
 * see: xnu/osfmk/kern/printf.c#printf
 */
#define LOG_TEMPL(lvl, fmt, ...) \
    printf(KEXT_NAME " [" lvl "] " fmt "\n", ##__VA_ARGS__)

#define LOG(fmt, ...)        printf(KEXT_NAME ": " fmt "\n", ##__VA_ARGS__)
#ifdef DEBUG
#define LOG_DBG(fmt, ...)    LOG_TEMPL("DBG", fmt, ##__VA_ARGS__)
#else
#define LOG_DBG(fmt, ...)    (void) ((void) 0, ##__VA_ARGS__)
#endif
#define LOG_INF(fmt, ...)    LOG_TEMPL("INF", fmt, ##__VA_ARGS__)
#define LOG_ERR(fmt, ...)    LOG_TEMPL("ERR", fmt, ##__VA_ARGS__)
#define LOG_BUG(fmt, ...)    LOG_TEMPL("BUG", fmt, ##__VA_ARGS__)
#define LOG_NIL(fmt, ...)    (void) ((void) 0, ##__VA_ARGS__)

void *libkext_malloc(size_t, int);
void *libkext_realloc(void *, size_t, size_t, int);
void libkext_mfree(void *);
void libkext_memck(void);

int libkext_get_kcb(void);
int libkext_put_kcb(void);
int libkext_read_kcb(void);

char *libkext_uuid(vm_address_t);

#endif /* LIBKEXT_H */
