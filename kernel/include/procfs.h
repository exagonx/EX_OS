/* =============================================================================
 * kernel/include/procfs.h — /proc (voce 45, kernel 0.290). See kernel/fs/procfs.c.
 * ============================================================================= */
#ifndef PROCFS_H
#define PROCFS_H

#include "vfs.h"

/* All of them take the path inside /proc ("/" , "/self/stat"), like the other
 * filesystems. Negative errno when it is not there. */
int procfs_stat(const char *interno, VfsStat *st);
int procfs_read(const char *interno, void *buf, uint32_t size, uint32_t offset);
/* Fills up to `cap` entries from the `da`-th; how many. ident is the bare
 * number: the VFS adds the mount. */
int procfs_readdir(const char *interno, VfsDirEntry *out, uint32_t cap, uint32_t da);

#endif /* PROCFS_H */
