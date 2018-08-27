/**
 * Created 18H26
 */

#include <sys/systm.h>
#include <libkern/OSAtomic.h>
#include "libkexti.h"

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
void *libkext_realloc(void *addr0, size_t size0, size_t size1, int flags)
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

static int __kcb(int opt)
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
int get_kcb(void)
{
    return __kcb(0);
}

/**
 * Decrease refcnt of activated kext callbacks
 * @return      refcnt before put o.w.
 */
int put_kcb(void)
{
    return __kcb(1);
}

/**
 * Read refcnt of activated kext callbacks
 */
int read_kcb(void)
{
    return __kcb(2);
}
