#ifndef RTDBG_H
#define RTDBG_H

#include <stdio.h>

#ifndef DBG_TAG
#define DBG_TAG "qboot"
#endif

#define LOG_E(fmt, ...) fprintf(stderr, "[E/%s] " fmt "\n", DBG_TAG, ##__VA_ARGS__)
#define LOG_W(fmt, ...) fprintf(stderr, "[W/%s] " fmt "\n", DBG_TAG, ##__VA_ARGS__)
#define LOG_I(fmt, ...) fprintf(stdout, "[I/%s] " fmt "\n", DBG_TAG, ##__VA_ARGS__)
#define LOG_D(fmt, ...) fprintf(stdout, "[D/%s] " fmt "\n", DBG_TAG, ##__VA_ARGS__)

#endif /* RTDBG_H */
