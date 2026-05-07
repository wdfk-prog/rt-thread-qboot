#ifndef DFS_FS_H
#define DFS_FS_H

#if defined(QBOOT_HOST_MISSING_DFS_PACKAGE)
/* Keep config-matrix DFS dependency checks independent from missing files. */
#error "QBOOT filesystem backend requires the RT-Thread DFS package"
#endif /* defined(QBOOT_HOST_MISSING_DFS_PACKAGE) */

#endif /* DFS_FS_H */
