#ifndef DFS_FILE_H
#define DFS_FILE_H

#if defined(__has_include)
#if __has_include(<fcntl.h>)
#include <fcntl.h>
#else
#error "QBoot host DFS stub requires POSIX <fcntl.h>."
#endif /* __has_include(<fcntl.h>) */
#if __has_include(<unistd.h>)
#include <unistd.h>
#else
#error "QBoot host DFS stub requires POSIX <unistd.h>."
#endif /* __has_include(<unistd.h>) */
#else
#include <fcntl.h>
#include <unistd.h>
#endif /* defined(__has_include) */

#endif /* DFS_FILE_H */
