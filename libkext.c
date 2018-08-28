/**
 * Created 18H26
 */

#include <sys/systm.h>
#include <libkern/OSAtomic.h>
#include "libkext.h"

static void libkext_mref(int opt)
{
    static volatile SInt64 cnt = 0;
    switch (opt) {
    case 0:
        if (OSDecrementAtomic64(&cnt) > 0) return;
        break;
    case 1:
        if (OSIncrementAtomic64(&cnt) >= 0) return;
        break;
    case 2:
        if (cnt == 0) return;
        break;
    }
    /* You may use DEBUG macro for production */
    panic("\n%s#L%d (potential memleak)  opt: %d cnt: %llu\n",
            __func__, __LINE__, opt, cnt);
}

void *libkext_malloc(size_t size, int flags)
{
    /* _MALLOC `type' parameter is a joke */
    void *addr = _MALLOC(size, M_TEMP, flags);
    if (likely(addr != NULL)) libkext_mref(1);
    return addr;
}

/**
 * Poor replica of _REALLOC() in XNU
 *
 * /System/Library/Frameworks/Kernel.framework/Resources/SupportedKPIs-all-archs.txt
 * Listed all supported KPI  as it revealed
 *  _MALLOC and _FREE both supported  where _REALLOC not exported by Apple
 *
 * @param addr0     Address needed to reallocation
 * @param size0     Original size of the buffer
 * @param size1     New size
 * @param flags     Flags to malloc
 * @return          Return NULL on fail  O.w. new allocated buffer
 *
 * NOTE:
 *  You should generally avoid allocate zero-length(new buffer size)
 *  the behaviour is implementation-defined  though in XNU it merely return NULL
 *
 * See:
 *  xnu/bsd/kern/kern_malloc.c@_REALLOC
 *  wiki.sei.cmu.edu/confluence/display/c/MEM04-C.+Beware+of+zero-length+allocations
 */
static void *libkext_realloc2(void *addr0, size_t size0, size_t size1, int flags)
{
    void *addr1;

    /*
     * [sic] _REALLOC(NULL, ...) is equivalent to _MALLOC(...)
     * XXX  for such case, its original size must be zero
     */
    if (addr0 == NULL) {
        kassert(size0 == 0);
        addr1 = _MALLOC(size1, M_TEMP, flags);
        goto out_exit;
    }

    if (unlikely(size1 == size0)) {
        addr1 = addr0;
        if (flags & M_ZERO) bzero(addr1, size1);
        goto out_exit;
    }

    addr1 = _MALLOC(size1, M_TEMP, flags);
    if (unlikely(addr1 == NULL))
        goto out_exit;

    memcpy(addr1, addr0, MIN(size0, size1));
    _FREE(addr0, M_TEMP);

out_exit:
    return addr1;
}

void *libkext_realloc(void *addr0, size_t size0, size_t size1, int flags)
{
    void *addr1 = libkext_realloc2(addr0, size0, size1, flags);
    if (!!addr0 ^ !!addr1) libkext_mref(!!addr1);
    return addr1;
}

void libkext_mfree(void *addr)
{
    if (addr != NULL) libkext_mref(0);
    _FREE(addr, M_TEMP);
}

/* XXX: call when all memory freed */
void libkext_memck(void)
{
    libkext_mref(2);
}

/*
 * TODO: the internal i should be exported
 */
static int kcb(int opt)
{
    static volatile SInt i = 0;
    SInt read;
    switch (opt) {
    case 0:
out_cas:
        read = i;
        if (read < 0) return -1;
        if (!OSCompareAndSwap(read, read + 1, &i))
            goto out_cas;
        return read;
    case 1:
        read = OSDecrementAtomic(&i);
        kassert(read > 0);
        return read;
    }
    return i;
}

/**
 * Increase refcnt of activated kext callbacks
 * @return      -1 if failed to get
 *              refcnt before get o.w.
 */
int libkext_get_kcb(void)
{
    return kcb(0);
}

/**
 * Decrease refcnt of activated kext callbacks
 * @return      refcnt before put o.w.
 */
int libkext_put_kcb(void)
{
    return kcb(1);
}

/**
 * Read refcnt of activated kext callbacks
 */
int libkext_read_kcb(void)
{
    return kcb(2);
}
