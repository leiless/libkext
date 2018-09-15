/**
 * Created 18H26
 */

#include <sys/systm.h>
#include <libkern/OSAtomic.h>
#include <mach-o/loader.h>
#include <sys/vnode.h>

#include "libkext.h"

static void libkext_mstat(int opt)
{
    static volatile SInt64 cnt = 0;
    switch (opt) {
    case 0:
        if (OSDecrementAtomic64(&cnt) > 0) return;
        break;
    case 1:
        if (OSIncrementAtomic64(&cnt) >= 0) return;
        break;
    default:
        if (cnt == 0) return;
        break;
    }
#ifdef DEBUG
    panicf("FIXME: potential memleak  opt: %d cnt: %lld", opt, cnt);
#else
    LOG_BUG("FIXME: potential memleak  opt: %d cnt: %lld", opt, cnt);
#endif
}

void *libkext_malloc(size_t size, int flags)
{
    /* _MALLOC `type' parameter is a joke */
    void *addr = _MALLOC(size, M_TEMP, flags);
    if (likely(addr != NULL)) libkext_mstat(1);
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
 * @param sz0       Original size of the buffer
 * @param sz1       New size
 * @param flags     Flags to malloc
 * @return          Return NULL on fail  O.w. new allocated buffer
 *
 * NOTE:
 *  You should generally avoid allocate zero-length(new buffer size)
 *  the behaviour is implementation-defined(_MALLOC return NULL in such case)
 *
 * See:
 *  xnu/bsd/kern/kern_malloc.c@_REALLOC
 *  wiki.sei.cmu.edu/confluence/display/c/MEM04-C.+Beware+of+zero-length+allocations
 */
static void *libkext_realloc2(void *addr0, size_t sz0, size_t sz1, int flags)
{
    void *addr1;

    /*
     * [sic] _REALLOC(NULL, ...) is equivalent to _MALLOC(...)
     * XXX  in such case, we require its original size must be zero
     */
    if (addr0 == NULL) {
        kassert(sz0 == 0);
        addr1 = _MALLOC(sz1, M_TEMP, flags);
        goto out_exit;
    }

    if (unlikely(sz1 == sz0)) {
        addr1 = addr0;
        goto out_exit;
    }

    addr1 = _MALLOC(sz1, M_TEMP, flags);
    if (unlikely(addr1 == NULL))
        goto out_exit;

    memcpy(addr1, addr0, MIN(sz0, sz1));
    _FREE(addr0, M_TEMP);

out_exit:
    return addr1;
}

void *libkext_realloc(void *addr0, size_t sz0, size_t sz1, int flags)
{
    void *addr1 = libkext_realloc2(addr0, sz0, sz1, flags);
    /*
     * If addr0 is nonnull yet addr1 null  the reference shouldn't change
     *  since addr0 won't be free in such case
     */
    if (!addr0 && addr1) libkext_mstat(1);
    return addr1;
}

void libkext_mfree(void *addr)
{
    if (addr != NULL) libkext_mstat(0);
    _FREE(addr, M_TEMP);
}

/* XXX: call when all memory freed */
void libkext_massert(void)
{
    libkext_mstat(2);
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

/**
 * Extract UUID load command from a Mach-O address
 *
 * @addr    Mach-O starting address
 * @return  NULL if failed  o.w. a new allocated buffer
 *          You need to free the buffer explicitly by libkext_mfree
 */
char *libkext_uuid(vm_address_t addr)
{
    kassert_nonnull(addr);

    char *s = NULL;
    uint8_t *p = (void *) addr;
    struct mach_header *h = (struct mach_header *) p;
    struct load_command lc;
    uint32_t m;
    uint32_t i;

    memcpy(&m, p, sizeof(m));
    if (m == MH_MAGIC || m == MH_CIGAM) {
        p += sizeof(struct mach_header);
    } else if (m == MH_MAGIC_64 || m == MH_CIGAM_64) {
        p += sizeof(struct mach_header_64);
    } else {
        goto out_bad;
    }

    for (i = 0; i < h->ncmds; i++) {
        memcpy(&lc, p, sizeof(lc));
        if (lc.cmd == LC_UUID) {
            uint8_t u[16];
            memcpy(u, p + sizeof(lc), sizeof(u));
            s = libkext_malloc(37, M_NOWAIT);
            if (s == NULL) goto out_bad;
            (void) snprintf(s, 37, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                            u[0], u[1], u[2], u[3], u[4], u[5], u[6], u[7],
                            u[8], u[9], u[10], u[11], u[12], u[13], u[14], u[15]);
            break;
        } else {
            p += lc.cmdsize;
        }
    }

out_bad:
    return s;
}

/**
 * Read a file from local volume(won't follow symlink)
 * @path        file path
 * @buff        read buffer
 * @len         length of read buffer
 * @off         read offset
 * @rd          bytes read(set if success)  OUT NULLABLE
 * @return      0 if success  errno o.w.
 */
int file_read(const char *path,
                unsigned char *buff,
                size_t len,
                off_t off,
                size_t *read)
{
    errno_t e;
    int flag = VNODE_LOOKUP_NOFOLLOW | VNODE_LOOKUP_NOCROSSMOUNT;
    vnode_t vp;
    vfs_context_t ctx;
    uio_t auio;

    kassert_nonnull(path);
    kassert_nonnull(buff);

    ctx = vfs_context_create(NULL);
    if (ctx == NULL) {
        e = ENOMEM;
        goto out_oom;
    }

    auio = uio_create(1, off, UIO_SYSSPACE, UIO_READ);
    if (auio == NULL) {
        e = ENOMEM;
        goto out_ctx;
    }

    e = uio_addiov(auio, (user_addr_t) buff, len);
    if (e) goto out_uio;

    e = vnode_lookup(path, flag, &vp, ctx);
    if (e) goto out_uio;

    e = VNOP_READ(vp, auio, IO_NOAUTH, ctx);
    if (e) goto out_put;

    if (read != NULL) *read = len - uio_resid(auio);

out_put:
    vnode_put(vp);
out_uio:
    uio_free(auio);
out_ctx:
    vfs_context_rele(ctx);
out_oom:
    return e;
}
