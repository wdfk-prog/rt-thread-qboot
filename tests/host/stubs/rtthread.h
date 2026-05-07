#ifndef RTTHREAD_H
#define RTTHREAD_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif /* _GNU_SOURCE */

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int rt_err_t;
typedef int rt_bool_t;
typedef int8_t rt_int8_t;
typedef uint8_t rt_uint8_t;
typedef int16_t rt_int16_t;
typedef uint16_t rt_uint16_t;
typedef int32_t rt_int32_t;
typedef uint32_t rt_uint32_t;
typedef size_t rt_size_t;
typedef uint32_t rt_tick_t;
typedef void *rt_thread_t;

#ifndef RT_NULL
#define RT_NULL NULL
#endif
#ifndef RT_TRUE
#define RT_TRUE 1
#endif
#ifndef RT_FALSE
#define RT_FALSE 0
#endif
#ifndef RT_EOK
#define RT_EOK 0
#endif
#ifndef RT_ERROR
#define RT_ERROR 1
#endif
#ifndef RT_ENOSPC
#define RT_ENOSPC ENOSPC
#endif
#ifndef RT_ENOSYS
#define RT_ENOSYS ENOSYS
#endif

#define rt_weak __attribute__((weak))
#define RT_UNUSED(x) ((void)(x))
#define RT_ALIGN(size, align) (((size) + (align) - 1) & ~((align) - 1))
#define RT_ALIGN_DOWN(size, align) ((size) & ~((align) - 1))
#define INIT_APP_EXPORT(fn)

#define rt_memcpy  memcpy
#define rt_memset  memset
#define rt_memmove memmove
#define rt_memcmp  memcmp
#define rt_strcmp  strcmp
#define rt_strcpy  strcpy
#define rt_strlen  strlen
#ifdef QBOOT_CI_HOST_TEST
/** @brief Host-test rt_malloc wrapper with optional fault injection. */
void *qboot_host_rt_malloc(size_t size);

/** @brief Host-test rt_free wrapper paired with qboot_host_rt_malloc(). */
void qboot_host_rt_free(void *ptr);

#define rt_malloc  qboot_host_rt_malloc
#define rt_free    qboot_host_rt_free
#else
#define rt_malloc  malloc
#define rt_free    free
#endif /* QBOOT_CI_HOST_TEST */

static inline void rt_kprintf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

static inline rt_tick_t rt_tick_get(void)
{
    static rt_tick_t tick;
    return ++tick;
}

static inline rt_tick_t rt_tick_from_millisecond(rt_int32_t ms)
{
    return (rt_tick_t)ms;
}

static inline void rt_thread_mdelay(rt_int32_t ms)
{
    RT_UNUSED(ms);
}

static inline void rt_hw_cpu_reset(void)
{
    fprintf(stderr, "rt_hw_cpu_reset called in host test\n");
    abort();
}

static inline rt_thread_t rt_thread_create(const char *name, void (*entry)(void *parameter),
                                           void *parameter, rt_uint32_t stack_size,
                                           rt_uint8_t priority, rt_uint32_t tick)
{
    RT_UNUSED(name);
    RT_UNUSED(entry);
    RT_UNUSED(parameter);
    RT_UNUSED(stack_size);
    RT_UNUSED(priority);
    RT_UNUSED(tick);
    return RT_NULL;
}

static inline rt_err_t rt_thread_startup(rt_thread_t thread)
{
    RT_UNUSED(thread);
    return RT_EOK;
}

#ifdef __cplusplus
}
#endif

#endif /* RTTHREAD_H */
