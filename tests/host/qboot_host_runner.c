/**
 * @file qboot_host_runner.c
 * @brief Host-side QBoot L1 upgrade and update-manager simulation runner.
 */
#include "qboot_host_flash.h"
#include <errno.h>
#include <qboot_update.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Human-readable expectation for the final APP partition content. */
typedef enum
{
    QBOOT_HOST_APP_EXPECT_OLD = 0, /**< APP must still contain the old image. */
    QBOOT_HOST_APP_EXPECT_NEW,     /**< APP must contain the new image. */
    QBOOT_HOST_APP_EXPECT_ANY      /**< APP content is intentionally not asserted. */
} qboot_host_app_expect_t;

/** @brief Runner operating mode. */
typedef enum
{
    QBOOT_HOST_MODE_RELEASE = 0, /**< Run package receive and release flow. */
    QBOOT_HOST_MODE_UPDATE_MGR,  /**< Run direct update-manager state tests. */
    QBOOT_HOST_MODE_JUMP_STUB,   /**< Run host Cortex-M jump preparation tests. */
    QBOOT_HOST_MODE_FAKE_FLASH,  /**< Run direct fake-flash physical-semantics tests. */
    QBOOT_HOST_MODE_FS_BOUNDARY, /**< Run direct filesystem boundary tests. */
    QBOOT_HOST_MODE_SIGN_BOUNDARY, /**< Run direct release-sign position tests. */
    QBOOT_HOST_MODE_REPEAT_SEQUENCE, /**< Run multi-release sequence tests in one process. */
    QBOOT_HOST_MODE_FAULT_SEQUENCE /**< Run deterministic multi-fault replay tests. */
} qboot_host_mode_t;

/** @brief Parsed runner configuration. */
typedef struct
{
    qboot_host_mode_t mode;      /**< Runner mode. */
    const char *case_name;       /**< Case label used in logs. */
    const char *package_path;    /**< Input RBL package path. */
    const char *old_app_path;    /**< Old APP image path. */
    const char *new_app_path;    /**< New APP image path. */
    const char *fixture_dir;     /**< Fixture directory for sequence-mode cases. */
    const char *receive_mode;    /**< Receive offset mode. */
    rt_uint32_t chunk_size;      /**< Receive chunk size. */
    rt_uint32_t download_limit;  /**< Truncated receive length, or zero for full package. */
    rt_bool_t expect_receive;    /**< Expected receive result. */
    rt_bool_t expect_first;      /**< Expected first release result. */
    rt_bool_t expect_success;    /**< Expected final release result. */
    rt_bool_t expect_jump;       /**< Expected final jump-spy result. */
    rt_bool_t expect_sign;       /**< Expected final DOWNLOAD sign state. */
    rt_bool_t expect_sign_set;   /**< Whether expect_sign was explicitly set. */
    rt_bool_t replay;            /**< Run a second release after clearing transient faults. */
    rt_bool_t skip_first_jump;   /**< Simulate reset after sign before first jump. */
    rt_bool_t fault_before_receive; /**< Apply configured faults before receive. */
    rt_bool_t corrupt_sign_before_release; /**< Corrupt release sign before first release. */
    rt_bool_t corrupt_app_before_replay; /**< Corrupt APP before replay release. */
    rt_bool_t malloc_fail_enabled; /**< Enable rt_malloc failure injection. */
    rt_uint32_t malloc_fail_after; /**< Allocations to pass before rt_malloc failure. */
    rt_bool_t physical_flash;     /**< Enable host custom-flash physical constraints. */
    rt_bool_t inspect;           /**< Only parse and print package header JSON. */
    qboot_host_app_expect_t expect_app; /**< Expected APP content selector. */
    rt_bool_t fault_enabled[QBOOT_HOST_FAULT_COUNT]; /**< Fault flags by operation. */
    qboot_host_fault_target_t fault_target[QBOOT_HOST_FAULT_COUNT]; /**< Fault target. */
    rt_uint32_t fault_after[QBOOT_HOST_FAULT_COUNT]; /**< Pass count before fault. */
} qboot_host_args_t;

static rt_uint32_t s_mgr_reason;       /**< Update-manager fake reason storage. */
static int s_mgr_enter_count;          /**< enter_download callback count. */
static int s_mgr_leave_count;          /**< leave_download callback count. */
static int s_mgr_error_count;          /**< on_error callback count. */
static int s_mgr_last_error;           /**< Last error code reported by on_error. */
static int s_mgr_ready_count;          /**< on_ready_to_app callback count. */
static rt_bool_t s_mgr_app_valid;      /**< Fake app-valid callback value. */
static rt_bool_t s_mgr_recover_ok;     /**< Fake recover callback value. */
static int s_mgr_callback_behavior;    /**< Callback behavior selector for reentry tests. */

#define QBOOT_HOST_CB_BEHAVIOR_NONE          0
#define QBOOT_HOST_CB_BEHAVIOR_REENTER_START 1
#define QBOOT_HOST_CB_BEHAVIOR_ABORT_ENTER   2

static void qboot_host_usage(const char *prog)
{
    printf("usage: %s --case NAME --package RBL --old-app BIN --new-app BIN [options]\n", prog);
    printf("       %s --inspect --package RBL\n", prog);
    printf("       %s --mode update-mgr --case NAME\n", prog);
    printf("       %s --mode jump-stub|fake-flash|fs-boundary|sign-boundary --case NAME\n", prog);
    printf("       %s --mode repeat-sequence|fault-sequence --case NAME --fixture-dir DIR\n", prog);
}

/**
 * @brief Initialize runner arguments to release-mode defaults.
 *
 * @param args Output argument structure.
 */
static void qboot_host_args_init(qboot_host_args_t *args)
{
    rt_memset(args, 0, sizeof(*args));
    args->mode = QBOOT_HOST_MODE_RELEASE;
    args->case_name = "host-case";
    args->fixture_dir = "_ci/host-sim/fixtures";
    args->receive_mode = "normal";
    args->chunk_size = 256u;
    args->expect_receive = RT_TRUE;
    args->expect_first = RT_TRUE;
    args->expect_success = RT_TRUE;
    args->expect_jump = RT_TRUE;
    args->expect_app = QBOOT_HOST_APP_EXPECT_NEW;
}

static rt_bool_t qboot_host_parse_bool(const char *text, rt_bool_t *out)
{
    if (text == RT_NULL || out == RT_NULL)
    {
        return RT_FALSE;
    }
    if (strcmp(text, "1") == 0 || strcmp(text, "true") == 0 || strcmp(text, "yes") == 0)
    {
        *out = RT_TRUE;
        return RT_TRUE;
    }
    if (strcmp(text, "0") == 0 || strcmp(text, "false") == 0 || strcmp(text, "no") == 0)
    {
        *out = RT_FALSE;
        return RT_TRUE;
    }
    return RT_FALSE;
}

static rt_bool_t qboot_host_parse_u32(const char *text, rt_uint32_t *out)
{
    char *end = RT_NULL;
    unsigned long value;
    if (text == RT_NULL || out == RT_NULL || text[0] == '\0')
    {
        return RT_FALSE;
    }
    errno = 0;
    value = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' || value > 0xFFFFFFFFul)
    {
        return RT_FALSE;
    }
    *out = (rt_uint32_t)value;
    return RT_TRUE;
}

static rt_bool_t qboot_host_parse_app_expect(const char *text, qboot_host_app_expect_t *out)
{
    if (text == RT_NULL || out == RT_NULL)
    {
        return RT_FALSE;
    }
    if (strcmp(text, "old") == 0)
    {
        *out = QBOOT_HOST_APP_EXPECT_OLD;
        return RT_TRUE;
    }
    if (strcmp(text, "new") == 0)
    {
        *out = QBOOT_HOST_APP_EXPECT_NEW;
        return RT_TRUE;
    }
    if (strcmp(text, "any") == 0)
    {
        *out = QBOOT_HOST_APP_EXPECT_ANY;
        return RT_TRUE;
    }
    return RT_FALSE;
}

static rt_bool_t qboot_host_parse_fault_target(const char *text, qboot_host_fault_target_t *out)
{
    if (text == RT_NULL || out == RT_NULL)
    {
        return RT_FALSE;
    }
    if (strcmp(text, "any") == 0) { *out = QBOOT_HOST_FAULT_TARGET_ANY; return RT_TRUE; }
    if (strcmp(text, "app") == 0) { *out = QBOOT_HOST_FAULT_TARGET_APP; return RT_TRUE; }
    if (strcmp(text, "download") == 0) { *out = QBOOT_HOST_FAULT_TARGET_DOWNLOAD; return RT_TRUE; }
    if (strcmp(text, "factory") == 0) { *out = QBOOT_HOST_FAULT_TARGET_FACTORY; return RT_TRUE; }
    if (strcmp(text, "swap") == 0) { *out = QBOOT_HOST_FAULT_TARGET_SWAP; return RT_TRUE; }
    return RT_FALSE;
}

static rt_bool_t qboot_host_parse_fault(const char *text,
                                        qboot_host_fault_target_t *target,
                                        rt_uint32_t *after_hits)
{
    char buf[64];
    char *colon;
    if (text == RT_NULL || target == RT_NULL || after_hits == RT_NULL || strlen(text) >= sizeof(buf))
    {
        return RT_FALSE;
    }
    strcpy(buf, text);
    colon = strchr(buf, ':');
    if (colon == RT_NULL)
    {
        return RT_FALSE;
    }
    *colon = '\0';
    return qboot_host_parse_fault_target(buf, target) && qboot_host_parse_u32(colon + 1, after_hits);
}

static rt_bool_t qboot_host_read_file(const char *path, rt_uint8_t **data, rt_uint32_t *size)
{
    FILE *fp;
    long len;
    rt_uint8_t *buf;
    if (path == RT_NULL || data == RT_NULL || size == RT_NULL)
    {
        return RT_FALSE;
    }
    fp = fopen(path, "rb");
    if (fp == RT_NULL)
    {
        perror(path);
        return RT_FALSE;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (len = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return RT_FALSE;
    }
    buf = (rt_uint8_t *)malloc((size_t)len + 1u);
    if (buf == RT_NULL)
    {
        fclose(fp);
        return RT_FALSE;
    }
    if (len > 0 && fread(buf, 1, (size_t)len, fp) != (size_t)len)
    {
        free(buf);
        fclose(fp);
        return RT_FALSE;
    }
    fclose(fp);
    *data = buf;
    *size = (rt_uint32_t)len;
    return RT_TRUE;
}

/**
 * @brief Copy a fixed-width ASCII firmware header field to a C string.
 *
 * @param dst      Output string buffer.
 * @param dst_size Output buffer size in bytes.
 * @param src      Fixed-width source field.
 * @param width    Maximum bytes available in @p src.
 */
static void qboot_host_copy_ascii_field(char *dst, size_t dst_size, const rt_uint8_t *src, size_t width)
{
    size_t len = 0;

    if (dst == RT_NULL || dst_size == 0u || src == RT_NULL)
    {
        return;
    }
    while (len < width && src[len] != '\0')
    {
        len++;
    }
    if (len >= dst_size)
    {
        len = dst_size - 1u;
    }
    if (len > 0u)
    {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

/**
 * @brief Check whether a package uses HPatchLite compression.
 *
 * @param pkg  Package bytes including the RBL header.
 * @param size Package byte count.
 *
 * @return RT_TRUE for HPatchLite packages, RT_FALSE otherwise.
 */
static rt_bool_t qboot_host_pkg_is_hpatchlite(const rt_uint8_t *pkg, rt_uint32_t size)
{
    fw_info_t info;

    if (pkg == RT_NULL || size < sizeof(info))
    {
        return RT_FALSE;
    }
    memcpy(&info, pkg, sizeof(info));
    return ((info.algo & QBOOT_ALGO_CMPRS_MASK) == QBOOT_ALGO_CMPRS_HPATCHLITE) ? RT_TRUE : RT_FALSE;
}

static rt_bool_t qboot_host_expect_target(qbt_target_id_t id, const rt_uint8_t *expect, rt_uint32_t size)
{
    rt_uint8_t *buf;
    rt_bool_t ok;
    if (expect == RT_NULL)
    {
        return RT_FALSE;
    }
    buf = (rt_uint8_t *)malloc(size);
    if (buf == RT_NULL)
    {
        return RT_FALSE;
    }
    if (!qboot_host_flash_read_target(id, 0, buf, size))
    {
        free(buf);
        return RT_FALSE;
    }
    ok = (memcmp(buf, expect, size) == 0) ? RT_TRUE : RT_FALSE;
    free(buf);
    return ok;
}

/**
 * @brief Verify that HPatchLite release erased stale APP tail bytes.
 *
 * @param old_size Old APP byte count loaded before release.
 * @param new_size New APP byte count expected after release.
 *
 * @return RT_TRUE when the stale tail is absent or erased, RT_FALSE otherwise.
 */
static rt_bool_t qboot_host_expect_hpatch_tail_erased(rt_uint32_t old_size, rt_uint32_t new_size)
{
    rt_uint8_t buf[64];
    void *handle = RT_NULL;
    rt_uint32_t app_size = 0;
    rt_uint32_t check_end;
    rt_uint32_t pos;

    if (old_size <= new_size)
    {
        return RT_TRUE;
    }
    if (!qbt_target_open(QBOOT_TARGET_APP, &handle, &app_size, QBT_OPEN_READ))
    {
        return RT_FALSE;
    }
    if (_header_io_ops->size(handle, &app_size) != RT_EOK)
    {
        qbt_target_close(handle);
        return RT_FALSE;
    }
    qbt_target_close(handle);
    if (app_size <= new_size)
    {
        return RT_TRUE;
    }
    check_end = (old_size < app_size) ? old_size : app_size;
    for (pos = new_size; pos < check_end;)
    {
        rt_uint32_t chunk = check_end - pos;
        if (chunk > sizeof(buf))
        {
            chunk = sizeof(buf);
        }
        if (!qboot_host_flash_read_target(QBOOT_TARGET_APP, pos, buf, chunk))
        {
            return RT_FALSE;
        }
        for (rt_uint32_t i = 0; i < chunk; i++)
        {
#ifdef QBOOT_HOST_BACKEND_FS
            if (buf[i] != 0xFFu && buf[i] != 0x00u)
#else
            if (buf[i] != 0xFFu)
#endif /* QBOOT_HOST_BACKEND_FS */
            {
                return RT_FALSE;
            }
        }
        pos += chunk;
    }
    return RT_TRUE;
}

/**
 * @brief Build an absolute or relative fixture path for sequence tests.
 *
 * @param dst         Output path buffer.
 * @param dst_size    Output buffer size in bytes.
 * @param fixture_dir Directory that contains generated host fixtures.
 * @param name        Fixture file name.
 *
 * @return RT_TRUE on success, RT_FALSE if the result would be truncated.
 */
static rt_bool_t qboot_host_fixture_path(char *dst,
                                         size_t dst_size,
                                         const char *fixture_dir,
                                         const char *name)
{
    int written;

    if (dst == RT_NULL || dst_size == 0u || fixture_dir == RT_NULL || name == RT_NULL)
    {
        return RT_FALSE;
    }
    written = snprintf(dst, dst_size, "%s/%s", fixture_dir, name);
    return (written > 0 && (size_t)written < dst_size) ? RT_TRUE : RT_FALSE;
}

static int qboot_host_inspect_package(const char *path)
{
    rt_uint8_t *pkg = RT_NULL;
    rt_uint32_t pkg_size = 0;
    void *handle = RT_NULL;
    rt_uint32_t part_size = 0;
    fw_info_t info;
    rt_bool_t ok;

    if (!qboot_host_read_file(path, &pkg, &pkg_size))
    {
        return 2;
    }
    qboot_host_flash_reset();
    if (!qboot_host_flash_load(QBOOT_TARGET_DOWNLOAD, pkg, pkg_size) ||
        !qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
    {
        free(pkg);
        return 3;
    }
    rt_memset(&info, 0, sizeof(info));
    ok = qbt_fw_check(handle, part_size, "download", &info);
    qbt_target_close(handle);
    free(pkg);
    if (!ok)
    {
        return 4;
    }
    char part_name[sizeof(info.part_name) + 1u];
    char fw_ver[sizeof(info.fw_ver) + 1u];
    char prod_code[sizeof(info.prod_code) + 1u];

    qboot_host_copy_ascii_field(part_name, sizeof(part_name), info.part_name, sizeof(info.part_name));
    qboot_host_copy_ascii_field(fw_ver, sizeof(fw_ver), info.fw_ver, sizeof(info.fw_ver));
    qboot_host_copy_ascii_field(prod_code, sizeof(prod_code), info.prod_code, sizeof(info.prod_code));
    printf("{\"type\":\"%.4s\",\"algo\":%u,\"algo2\":%u,"
           "\"part_name\":\"%s\",\"fw_ver\":\"%s\",\"prod_code\":\"%s\","
           "\"pkg_crc\":%u,\"raw_crc\":%u,\"raw_size\":%u,\"pkg_size\":%u,\"hdr_crc\":%u}\n",
           info.type, (unsigned)info.algo, (unsigned)info.algo2,
           part_name, fw_ver, prod_code,
           (unsigned)info.pkg_crc, (unsigned)info.raw_crc,
           (unsigned)info.raw_size, (unsigned)info.pkg_size,
           (unsigned)info.hdr_crc);
    return 0;
}

static void qboot_host_apply_faults(const qboot_host_args_t *args)
{
    for (qboot_host_fault_op_t op = 0; op < QBOOT_HOST_FAULT_COUNT; op++)
    {
        if (args->fault_enabled[op])
        {
            qboot_host_fault_set(op, args->fault_target[op], args->fault_after[op]);
        }
    }
    if (args->malloc_fail_enabled)
    {
        qboot_host_rt_malloc_fail_after(args->malloc_fail_after);
    }
}

/**
 * @brief Run one package receive and release step.
 *
 * @param args          Parsed release-step arguments.
 * @param reset_storage Reset simulated storage before receiving the package.
 * @param load_old_app  Load @c old_app_path into APP before receiving the package.
 *
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_release_step(const qboot_host_args_t *args,
                                       rt_bool_t reset_storage,
                                       rt_bool_t load_old_app)
{
    rt_uint8_t *pkg = RT_NULL;
    rt_uint8_t *old_app = RT_NULL;
    rt_uint8_t *new_app = RT_NULL;
    rt_uint32_t pkg_size = 0;
    rt_uint32_t old_size = 0;
    rt_uint32_t new_size = 0;
    rt_uint32_t receive_size;
    rt_bool_t receive_ok;
    rt_bool_t hpatch_pkg;
    rt_bool_t first_ok = RT_FALSE;
    rt_bool_t final_ok = RT_FALSE;
    rt_bool_t sign_ok;
    rt_bool_t jump_ok;
    rt_bool_t app_ok = RT_TRUE;
    int exit_code = 1;

    if (!qboot_host_read_file(args->package_path, &pkg, &pkg_size) ||
        !qboot_host_read_file(args->old_app_path, &old_app, &old_size) ||
        !qboot_host_read_file(args->new_app_path, &new_app, &new_size))
    {
        goto cleanup;
    }

    hpatch_pkg = qboot_host_pkg_is_hpatchlite(pkg, pkg_size);

    if (reset_storage)
    {
        qboot_host_flash_reset();
    }
    else
    {
        qboot_host_fault_reset();
        qboot_host_jump_reset();
        qboot_host_flash_physical_enable(RT_FALSE);
        qboot_host_flash_program_unit_set(0u);
    }
    if (args->physical_flash)
    {
        qboot_host_flash_physical_enable(RT_TRUE);
    }
    if (load_old_app && !qboot_host_flash_load(QBOOT_TARGET_APP, old_app, old_size))
    {
        printf("QBOOT_HOST_FAIL %s load old app\n", args->case_name);
        goto cleanup;
    }

    receive_size = pkg_size;
    if (args->download_limit > 0u && args->download_limit < receive_size)
    {
        receive_size = args->download_limit;
    }
    if (args->fault_before_receive)
    {
        qboot_host_apply_faults(args);
    }
    receive_ok = qboot_host_receive_download_mode(pkg, receive_size, args->chunk_size, args->receive_mode);
    if (receive_ok != args->expect_receive)
    {
        printf("QBOOT_HOST_FAIL %s receive expected=%d actual=%d\n", args->case_name, args->expect_receive, receive_ok);
        goto cleanup;
    }

    if (receive_ok)
    {
        if (args->corrupt_sign_before_release && !qboot_host_download_corrupt_release_sign())
        {
            printf("QBOOT_HOST_FAIL %s corrupt release sign\n", args->case_name);
            goto cleanup;
        }
        if (!args->fault_before_receive)
        {
            qboot_host_apply_faults(args);
        }
        first_ok = qbt_ci_release_from_download(RT_TRUE);
        if (first_ok && !args->skip_first_jump)
        {
            qbt_jump_to_app();
        }
    }
    if (first_ok != args->expect_first)
    {
        printf("QBOOT_HOST_FAIL %s first expected=%d actual=%d\n", args->case_name, args->expect_first, first_ok);
        goto cleanup;
    }

    final_ok = first_ok;
    if (args->replay)
    {
        if (args->corrupt_app_before_replay && !qboot_host_corrupt_target_byte(QBOOT_TARGET_APP, 0u))
        {
            printf("QBOOT_HOST_FAIL %s corrupt APP before replay\n", args->case_name);
            goto cleanup;
        }
        qboot_host_fault_reset();
        qboot_host_jump_reset();
        final_ok = qbt_ci_release_from_download(RT_TRUE);
        if (final_ok)
        {
            qbt_jump_to_app();
        }
    }
    if (final_ok != args->expect_success)
    {
        printf("QBOOT_HOST_FAIL %s final expected=%d actual=%d\n", args->case_name, args->expect_success, final_ok);
        goto cleanup;
    }

    sign_ok = qboot_host_download_has_release_sign();
    if (sign_ok != args->expect_sign)
    {
        printf("QBOOT_HOST_FAIL %s sign expected=%d actual=%d\n", args->case_name, args->expect_sign, sign_ok);
        goto cleanup;
    }

    jump_ok = (qboot_host_jump_count() > 0) ? RT_TRUE : RT_FALSE;
    if (jump_ok != args->expect_jump)
    {
        printf("QBOOT_HOST_FAIL %s jump expected=%d actual=%d count=%d\n",
               args->case_name, args->expect_jump, jump_ok, qboot_host_jump_count());
        goto cleanup;
    }

    if (args->expect_app == QBOOT_HOST_APP_EXPECT_OLD)
    {
        app_ok = qboot_host_expect_target(QBOOT_TARGET_APP, old_app, old_size);
    }
    else if (args->expect_app == QBOOT_HOST_APP_EXPECT_NEW)
    {
        app_ok = qboot_host_expect_target(QBOOT_TARGET_APP, new_app, new_size);
    }
    if (!app_ok)
    {
        printf("QBOOT_HOST_FAIL %s app content mismatch\n", args->case_name);
        goto cleanup;
    }
    if (hpatch_pkg && args->expect_app == QBOOT_HOST_APP_EXPECT_NEW && final_ok &&
        !qboot_host_expect_hpatch_tail_erased(old_size, new_size))
    {
        printf("QBOOT_HOST_FAIL %s hpatch stale APP tail not erased\n", args->case_name);
        goto cleanup;
    }

    printf("QBOOT_HOST_CASE_PASS %s receive=%d first=%d final=%d sign=%d jump=%d\n",
           args->case_name, receive_ok, first_ok, final_ok, sign_ok, jump_ok);
    exit_code = 0;

cleanup:
    free(pkg);
    free(old_app);
    free(new_app);
    return exit_code;
}

/**
 * @brief Run one ordinary release case with fresh simulated storage.
 *
 * @param args Parsed release-case arguments.
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_release_case(const qboot_host_args_t *args)
{
    return qboot_host_run_release_step(args, RT_TRUE, RT_TRUE);
}

/** @brief One step inside an in-process repeat-upgrade sequence. */
typedef struct
{
    const char *package_name;       /**< Package fixture file name. */
    const char *old_app_name;       /**< Expected current APP file name. */
    const char *new_app_name;       /**< Expected new APP file name. */
    rt_bool_t expect_receive;       /**< Expected receive result. */
    rt_bool_t expect_first;         /**< Expected first release result. */
    rt_bool_t expect_success;       /**< Expected final release result. */
    rt_bool_t expect_jump;          /**< Expected jump result. */
    rt_bool_t expect_sign;          /**< Expected release-sign result. */
    qboot_host_app_expect_t expect_app; /**< Expected APP image after this step. */
} qboot_host_repeat_step_t;

/**
 * @brief Run one in-process repeat-upgrade sequence step.
 *
 * @param sequence_name Parent sequence case name.
 * @param fixture_dir   Directory that contains generated host fixtures.
 * @param step          Step descriptor.
 * @param index         Step index for logs.
 * @param load_old_app  Whether to initialize APP from @p step old image.
 *
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_repeat_step(const char *sequence_name,
                                      const char *fixture_dir,
                                      const qboot_host_repeat_step_t *step,
                                      rt_uint32_t index,
                                      rt_bool_t load_old_app)
{
    qboot_host_args_t args;
    char case_name[160];
    char package_path[512];
    char old_app_path[512];
    char new_app_path[512];
    int written;

    qboot_host_args_init(&args);
    written = snprintf(case_name, sizeof(case_name), "%s-%u",
                       sequence_name, (unsigned)index);
    if (written <= 0 || (size_t)written >= sizeof(case_name) ||
        !qboot_host_fixture_path(package_path, sizeof(package_path),
                                 fixture_dir, step->package_name) ||
        !qboot_host_fixture_path(old_app_path, sizeof(old_app_path),
                                 fixture_dir, step->old_app_name) ||
        !qboot_host_fixture_path(new_app_path, sizeof(new_app_path),
                                 fixture_dir, step->new_app_name))
    {
        printf("QBOOT_HOST_FAIL %s build fixture path\n", sequence_name);
        return 1;
    }

    args.case_name = case_name;
    args.package_path = package_path;
    args.old_app_path = old_app_path;
    args.new_app_path = new_app_path;
    args.expect_receive = step->expect_receive;
    args.expect_first = step->expect_first;
    args.expect_success = step->expect_success;
    args.expect_jump = step->expect_jump;
    args.expect_sign = step->expect_sign;
    args.expect_sign_set = RT_TRUE;
    args.expect_app = step->expect_app;
    return qboot_host_run_release_step(&args, index == 0u, load_old_app);
}

/**
 * @brief Run repeat and stale-state release cases without process-level reset.
 *
 * @param case_name   Repeat-sequence case name.
 * @param fixture_dir Directory that contains generated host fixtures.
 *
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_repeat_sequence_case(const char *case_name, const char *fixture_dir)
{
    static const qboot_host_repeat_step_t seq_a_to_b_to_c[] = {
        { "custom-none-full.rbl", "old_app.bin", "new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
        { "repeat-upgrade-b.rbl", "new_app.bin", "app_b.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
        { "repeat-upgrade-c-gzip.rbl", "app_b.bin", "app_c.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
    };
    static const qboot_host_repeat_step_t seq_fail_then_success[] = {
        { "custom-bad-crc.rbl", "old_app.bin", "new_app.bin", RT_TRUE, RT_FALSE, RT_FALSE, RT_FALSE, RT_FALSE, QBOOT_HOST_APP_EXPECT_OLD },
        { "custom-none-full.rbl", "old_app.bin", "new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
    };
    static const qboot_host_repeat_step_t seq_stale_download[] = {
        { "custom-none-full.rbl", "old_app.bin", "new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
        { "repeat-upgrade-b.rbl", "new_app.bin", "app_b.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
    };
    static const qboot_host_repeat_step_t seq_sign_rewritten[] = {
        { "custom-none-full.rbl", "old_app.bin", "new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
        { "repeat-upgrade-c-gzip.rbl", "new_app.bin", "app_c.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
    };
    static const qboot_host_repeat_step_t seq_gzip_aes_hpatch[] = {
        { "custom-gzip.rbl", "old_app.bin", "new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
        { "custom-aes-gzip-real.rbl", "new_app.bin", "aes_new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
        { "custom-hpatch-host-full-diff.rbl", "aes_new_app.bin", "hpatch_new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
    };
    static const qboot_host_repeat_step_t seq_hpatch_then_none[] = {
        { "custom-hpatch-host-full-diff.rbl", "old_app.bin", "hpatch_new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
        { "custom-none-full.rbl", "hpatch_new_app.bin", "new_app.bin", RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
    };
    const qboot_host_repeat_step_t *steps = RT_NULL;
    rt_uint32_t step_count = 0u;

#define SELECT_SEQUENCE(name, table)     do { if (strcmp(case_name, (name)) == 0) { steps = (table); step_count = (rt_uint32_t)(sizeof(table) / sizeof((table)[0])); } } while (0)

    SELECT_SEQUENCE("repeat-upgrade-a-to-b-to-c", seq_a_to_b_to_c);
    SELECT_SEQUENCE("repeat-upgrade-fail-then-success", seq_fail_then_success);
    SELECT_SEQUENCE("repeat-upgrade-success-then-stale-download-leftover", seq_stale_download);
    SELECT_SEQUENCE("repeat-upgrade-sign-rewritten-each-time", seq_sign_rewritten);
    SELECT_SEQUENCE("repeat-upgrade-gzip-then-aes-then-hpatch", seq_gzip_aes_hpatch);
    SELECT_SEQUENCE("repeat-upgrade-hpatch-then-none", seq_hpatch_then_none);
#undef SELECT_SEQUENCE

    if (steps == RT_NULL)
    {
        printf("QBOOT_HOST_FAIL unsupported repeat sequence case: %s\n", case_name);
        return 2;
    }
    for (rt_uint32_t i = 0; i < step_count; i++)
    {
        if (qboot_host_run_repeat_step(case_name, fixture_dir, &steps[i], i, i == 0u) != 0)
        {
            return 1;
        }
    }
    printf("QBOOT_HOST_CASE_PASS %s repeat-sequence-steps=%u\n", case_name, (unsigned)step_count);
    return 0;
}

/** @brief One step in a deterministic in-process fault replay sequence. */
typedef struct
{
    rt_bool_t fault_enabled;             /**< Whether to inject a fault for this step. */
    qboot_host_fault_op_t fault_op;      /**< Fault operation selector. */
    qboot_host_fault_target_t target;    /**< Fault target selector. */
    rt_uint32_t after_hits;              /**< Successful matching hits before failure. */
    rt_bool_t expect_success;            /**< Expected release result. */
    rt_bool_t expect_jump;               /**< Expected jump result. */
    rt_bool_t expect_sign;               /**< Expected release-sign result. */
    qboot_host_app_expect_t expect_app;  /**< Expected APP image after this step. */
} qboot_host_fault_sequence_step_t;

/**
 * @brief Check APP contents after one fault-sequence step.
 *
 * @param case_name Sequence case name.
 * @param step      Step expectation descriptor.
 * @param old_app   Expected old APP bytes.
 * @param old_size  Expected old APP size.
 * @param new_app   Expected new APP bytes.
 * @param new_size  Expected new APP size.
 *
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_expect_sequence_app(const char *case_name,
                                          const qboot_host_fault_sequence_step_t *step,
                                          const rt_uint8_t *old_app,
                                          rt_uint32_t old_size,
                                          const rt_uint8_t *new_app,
                                          rt_uint32_t new_size)
{
    rt_bool_t app_ok = RT_TRUE;

    if (step->expect_app == QBOOT_HOST_APP_EXPECT_OLD)
    {
        app_ok = qboot_host_expect_target(QBOOT_TARGET_APP, old_app, old_size);
    }
    else if (step->expect_app == QBOOT_HOST_APP_EXPECT_NEW)
    {
        app_ok = qboot_host_expect_target(QBOOT_TARGET_APP, new_app, new_size);
    }
    if (!app_ok)
    {
        printf("QBOOT_HOST_FAIL %s sequence app content mismatch\n", case_name);
        return 1;
    }
    return 0;
}

/**
 * @brief Run deterministic release attempts with multiple sequential faults.
 *
 * @param case_name   Sequence case name.
 * @param fixture_dir Directory that contains generated host fixtures.
 * @param steps       Step descriptors.
 * @param step_count  Number of entries in @p steps.
 *
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_fault_sequence_steps(const char *case_name,
                                               const char *fixture_dir,
                                               const qboot_host_fault_sequence_step_t *steps,
                                               rt_uint32_t step_count)
{
    char package_path[512];
    char old_app_path[512];
    char new_app_path[512];
    rt_uint8_t *pkg = RT_NULL;
    rt_uint8_t *old_app = RT_NULL;
    rt_uint8_t *new_app = RT_NULL;
    rt_uint32_t pkg_size = 0;
    rt_uint32_t old_size = 0;
    rt_uint32_t new_size = 0;
    int exit_code = 1;

    if (steps == RT_NULL || step_count == 0u ||
        !qboot_host_fixture_path(package_path, sizeof(package_path), fixture_dir, "custom-none-full.rbl") ||
        !qboot_host_fixture_path(old_app_path, sizeof(old_app_path), fixture_dir, "old_app.bin") ||
        !qboot_host_fixture_path(new_app_path, sizeof(new_app_path), fixture_dir, "new_app.bin") ||
        !qboot_host_read_file(package_path, &pkg, &pkg_size) ||
        !qboot_host_read_file(old_app_path, &old_app, &old_size) ||
        !qboot_host_read_file(new_app_path, &new_app, &new_size))
    {
        printf("QBOOT_HOST_FAIL %s sequence fixture setup\n", case_name);
        goto cleanup;
    }

    qboot_host_flash_reset();
    if (!qboot_host_flash_load(QBOOT_TARGET_APP, old_app, old_size) ||
        !qboot_host_receive_download(pkg, pkg_size, 257u))
    {
        printf("QBOOT_HOST_FAIL %s sequence initial download\n", case_name);
        goto cleanup;
    }

    for (rt_uint32_t i = 0; i < step_count; i++)
    {
        rt_bool_t release_ok;
        rt_bool_t sign_ok;
        rt_bool_t jump_ok;

        qboot_host_fault_reset();
        qboot_host_jump_reset();
        if (steps[i].fault_enabled)
        {
            qboot_host_fault_set(steps[i].fault_op, steps[i].target, steps[i].after_hits);
        }
        release_ok = qbt_ci_release_from_download(RT_TRUE);
        if (release_ok)
        {
            qbt_jump_to_app();
        }
        if (release_ok != steps[i].expect_success)
        {
            printf("QBOOT_HOST_FAIL %s sequence step=%u success expected=%d actual=%d\n",
                   case_name, (unsigned)i, steps[i].expect_success, release_ok);
            goto cleanup;
        }
        jump_ok = (qboot_host_jump_count() > 0) ? RT_TRUE : RT_FALSE;
        if (jump_ok != steps[i].expect_jump)
        {
            printf("QBOOT_HOST_FAIL %s sequence step=%u jump expected=%d actual=%d\n",
                   case_name, (unsigned)i, steps[i].expect_jump, jump_ok);
            goto cleanup;
        }
        sign_ok = qboot_host_download_has_release_sign();
        if (sign_ok != steps[i].expect_sign)
        {
            printf("QBOOT_HOST_FAIL %s sequence step=%u sign expected=%d actual=%d\n",
                   case_name, (unsigned)i, steps[i].expect_sign, sign_ok);
            goto cleanup;
        }
        if (qboot_host_expect_sequence_app(case_name, &steps[i], old_app, old_size,
                                           new_app, new_size) != 0)
        {
            goto cleanup;
        }
    }

    printf("QBOOT_HOST_CASE_PASS %s fault-sequence-steps=%u\n", case_name, (unsigned)step_count);
    exit_code = 0;

cleanup:
    free(pkg);
    free(old_app);
    free(new_app);
    return exit_code;
}

/**
 * @brief Run a named deterministic multi-fault reset/replay sequence.
 *
 * @param case_name   Test case name.
 * @param fixture_dir Directory that contains generated host fixtures.
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_fault_sequence_case(const char *case_name, const char *fixture_dir)
{
    static const qboot_host_fault_sequence_step_t erase_write_sign_success[] = {
        { RT_TRUE, QBOOT_HOST_FAULT_ERASE, QBOOT_HOST_FAULT_TARGET_APP, 0u, RT_FALSE, RT_FALSE, RT_FALSE, QBOOT_HOST_APP_EXPECT_ANY },
        { RT_TRUE, QBOOT_HOST_FAULT_WRITE, QBOOT_HOST_FAULT_TARGET_APP, 1u, RT_FALSE, RT_FALSE, RT_FALSE, QBOOT_HOST_APP_EXPECT_ANY },
        { RT_TRUE, QBOOT_HOST_FAULT_SIGN_WRITE, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u, RT_FALSE, RT_FALSE, RT_FALSE, QBOOT_HOST_APP_EXPECT_ANY },
        { RT_FALSE, QBOOT_HOST_FAULT_OPEN, QBOOT_HOST_FAULT_TARGET_ANY, 0u, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
    };
    static const qboot_host_fault_sequence_step_t read_write_signread_success[] = {
        { RT_TRUE, QBOOT_HOST_FAULT_READ, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u, RT_FALSE, RT_FALSE, RT_FALSE, QBOOT_HOST_APP_EXPECT_ANY },
        { RT_TRUE, QBOOT_HOST_FAULT_WRITE, QBOOT_HOST_FAULT_TARGET_APP, 0u, RT_FALSE, RT_FALSE, RT_FALSE, QBOOT_HOST_APP_EXPECT_ANY },
        { RT_TRUE, QBOOT_HOST_FAULT_SIGN_READ, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 1u, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
        { RT_FALSE, QBOOT_HOST_FAULT_OPEN, QBOOT_HOST_FAULT_TARGET_ANY, 0u, RT_TRUE, RT_TRUE, RT_TRUE, QBOOT_HOST_APP_EXPECT_NEW },
    };

    if (strcmp(case_name, "fault-sequence-erase-write-sign-success") == 0)
    {
        return qboot_host_run_fault_sequence_steps(case_name, fixture_dir,
                                                   erase_write_sign_success,
                                                   (rt_uint32_t)(sizeof(erase_write_sign_success) / sizeof(erase_write_sign_success[0])));
    }
    if (strcmp(case_name, "fault-sequence-read-write-signread-success") == 0)
    {
        return qboot_host_run_fault_sequence_steps(case_name, fixture_dir,
                                                   read_write_signread_success,
                                                   (rt_uint32_t)(sizeof(read_write_signread_success) / sizeof(read_write_signread_success[0])));
    }
    printf("QBOOT_HOST_FAIL unsupported fault sequence case: %s\n", case_name);
    return 2;
}

static rt_uint32_t update_get_reason(void) { return s_mgr_reason; }
static void update_set_reason(rt_uint32_t reason) { s_mgr_reason = reason; }
static rt_bool_t update_is_app_valid(void) { return s_mgr_app_valid; }
static void update_enter_download(void)
{
    s_mgr_enter_count++;
    if (s_mgr_callback_behavior == QBOOT_HOST_CB_BEHAVIOR_REENTER_START)
    {
        s_mgr_callback_behavior = QBOOT_HOST_CB_BEHAVIOR_NONE;
        qbt_update_mgr_on_start();
    }
    else if (s_mgr_callback_behavior == QBOOT_HOST_CB_BEHAVIOR_ABORT_ENTER)
    {
        s_mgr_callback_behavior = QBOOT_HOST_CB_BEHAVIOR_NONE;
        qbt_update_mgr_on_abort();
    }
}
static void update_leave_download(void) { s_mgr_leave_count++; }
static void update_on_error(int err)
{
    s_mgr_last_error = err;
    s_mgr_error_count++;
}
static void update_on_ready_to_app(void) { s_mgr_ready_count++; }
static rt_bool_t update_try_recover(void) { return s_mgr_recover_ok; }

static const qbt_update_ops_t s_update_ops = {
    .get_reason = update_get_reason,
    .set_reason = update_set_reason,
    .is_app_valid = update_is_app_valid,
    .enter_download = update_enter_download,
    .leave_download = update_leave_download,
    .on_error = update_on_error,
    .on_ready_to_app = update_on_ready_to_app,
    .try_recover = update_try_recover,
};

static int qboot_host_expect_state(const char *case_name, qbt_upd_state_t expect)
{
    qbt_upd_state_t actual = qbt_update_mgr_get_state();
    if (actual != expect)
    {
        printf("QBOOT_HOST_FAIL %s state expected=%d actual=%d\n", case_name, expect, actual);
        return 1;
    }
    printf("QBOOT_HOST_CASE_PASS %s state=%d reason=%u enter=%d leave=%d ready=%d error=%d\n",
           case_name, actual, (unsigned)s_mgr_reason, s_mgr_enter_count,
           s_mgr_leave_count, s_mgr_ready_count, s_mgr_error_count);
    return 0;
}

static int qboot_host_expect_update_counts(const char *case_name,
                                            qbt_upd_state_t expect_state,
                                            int expect_enter,
                                            int expect_leave,
                                            int expect_ready,
                                            int expect_error)
{
    if (qbt_update_mgr_get_state() != expect_state ||
        s_mgr_enter_count != expect_enter ||
        s_mgr_leave_count != expect_leave ||
        s_mgr_ready_count != expect_ready ||
        s_mgr_error_count != expect_error)
    {
        printf("QBOOT_HOST_FAIL %s state=%d/%d enter=%d/%d leave=%d/%d ready=%d/%d error=%d/%d\n",
               case_name, qbt_update_mgr_get_state(), expect_state,
               s_mgr_enter_count, expect_enter, s_mgr_leave_count, expect_leave,
               s_mgr_ready_count, expect_ready, s_mgr_error_count, expect_error);
        return 1;
    }
    return qboot_host_expect_state(case_name, expect_state);
}

/**
 * @brief Run one host Cortex-M jump stub case and validate its trace.
 *
 * @param case_name Test case name for diagnostics.
 * @param expect_ok Expected vector acceptance result.
 * @param stack_ptr Candidate APP initial stack pointer.
 * @param reset_vector Candidate APP reset vector.
 * @param vector_table Candidate APP vector table address.
 * @param expect_barriers Expected CPU barrier operation count on success.
 * @param expect_fpu_cleanup Expected FPU cleanup operation count on success.
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_expect_jump_trace(const char *case_name,
                                        rt_bool_t expect_ok,
                                        rt_uint32_t stack_ptr,
                                        rt_uint32_t reset_vector,
                                        rt_uint32_t vector_table,
                                        int expect_barriers,
                                        int expect_fpu_cleanup)
{
    const qboot_host_jump_trace_t *trace;
    rt_bool_t ok;

    qboot_host_jump_reset();
    ok = qboot_host_jump_stub_run(stack_ptr, reset_vector, vector_table);
    trace = qboot_host_jump_stub_trace();
    if (ok != expect_ok)
    {
        printf("QBOOT_HOST_FAIL %s jump expected=%d actual=%d\n", case_name, expect_ok, ok);
        return 1;
    }
    if (expect_ok && (trace->disable_irq_count != 1 || trace->deinit_count != 1 ||
                      trace->systick_clear_count != 1 || trace->nvic_clear_count != 128 ||
                      trace->set_control_count != 1 || trace->set_msp_count != 1 ||
                      trace->set_vtor_count != 1 || trace->barrier_count != expect_barriers ||
                      trace->fpu_cleanup_count != expect_fpu_cleanup ||
                      trace->msp_value != stack_ptr || trace->reset_vector != reset_vector ||
                      trace->vector_table != vector_table || trace->app_call_count != 1))
    {
        printf("QBOOT_HOST_FAIL %s invalid jump trace irq=%d deinit=%d systick=%d nvic=%d control=%d msp=%d vtor=%d barrier=%d fpu=%d app=%d values=%08X/%08X/%08X\n",
               case_name, trace->disable_irq_count, trace->deinit_count,
               trace->systick_clear_count, trace->nvic_clear_count,
               trace->set_control_count, trace->set_msp_count,
               trace->set_vtor_count, trace->barrier_count,
               trace->fpu_cleanup_count, trace->app_call_count,
               trace->msp_value, trace->reset_vector, trace->vector_table);
        return 1;
    }
    if (!expect_ok && (trace->disable_irq_count != 0 || trace->set_msp_count != 0 ||
                       trace->set_vtor_count != 0 || trace->barrier_count != 0 ||
                       trace->fpu_cleanup_count != 0 || trace->app_call_count != 0 ||
                       trace->msp_value != 0u || trace->reset_vector != 0u ||
                       trace->vector_table != 0u))
    {
        printf("QBOOT_HOST_FAIL %s invalid rejection trace\n", case_name);
        return 1;
    }
    printf("QBOOT_HOST_CASE_PASS %s jump_ok=%d irq=%d msp=%d vtor=%d barrier=%d fpu=%d nvic=%d systick=%d app=%d values=%08X/%08X/%08X\n",
           case_name, ok, trace->disable_irq_count, trace->set_msp_count,
           trace->set_vtor_count, trace->barrier_count,
           trace->fpu_cleanup_count, trace->nvic_clear_count,
           trace->systick_clear_count, trace->app_call_count,
           trace->msp_value, trace->reset_vector, trace->vector_table);
    return 0;
}

/**
 * @brief Run a named host Cortex-M jump preparation test.
 *
 * @param case_name Test case name.
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_jump_stub_case(const char *case_name)
{
    const rt_uint32_t valid_sp = 0x20008000u;
    const rt_uint32_t valid_reset = 0x08020101u;
    const rt_uint32_t valid_vtor = 0x08020000u;
    const int expected_barriers = 2;
    const int expected_fpu_cleanup = 1;

    if (strcmp(case_name, "jump-disable-irq-check") == 0 ||
        strcmp(case_name, "jump-clear-pending-irq-check") == 0 ||
        strcmp(case_name, "jump-deinit-systick-check") == 0 ||
        strcmp(case_name, "jump-systick-pending-cleared") == 0 ||
        strcmp(case_name, "jump-nvic-enable-bits-cleared") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, valid_sp, valid_reset,
                                            valid_vtor, expected_barriers,
                                            expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-msp-update-check") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, 0x20004000u, valid_reset,
                                            valid_vtor, expected_barriers,
                                            expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-vtor-update-check") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, valid_sp, valid_reset,
                                            0x08020400u, expected_barriers,
                                            expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-stack-pointer-ram-start-boundary") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, 0x20000000u, valid_reset,
                                            valid_vtor, expected_barriers,
                                            expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-stack-pointer-ram-end-boundary") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, 0x2400FFFCu, valid_reset,
                                            valid_vtor, expected_barriers,
                                            expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-reset-vector-thumb-bit-set-accepted") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, valid_sp, 0x08023FFDu,
                                            valid_vtor, expected_barriers,
                                            expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-vtor-alignment-cortex-m3-policy") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, valid_sp, valid_reset,
                                            0x08020100u, expected_barriers,
                                            expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-vtor-alignment-cortex-m7-policy") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, valid_sp, valid_reset,
                                            0x08020400u, expected_barriers,
                                            expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-fpu-state-cleanup-policy") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, valid_sp, valid_reset,
                                            valid_vtor, expected_barriers, 1);
    }
    if (strcmp(case_name, "jump-cache-barrier-before-branch-policy") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_TRUE, valid_sp, valid_reset,
                                            valid_vtor, 2, expected_fpu_cleanup);
    }
    if (strcmp(case_name, "jump-invalid-stack-pointer") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_FALSE, 0x10001000u, valid_reset,
                                            valid_vtor, 0, 0);
    }
    if (strcmp(case_name, "jump-reset-vector-thumb-bit-clear-rejected") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_FALSE, valid_sp, valid_reset & ~1u,
                                            valid_vtor, 0, 0);
    }
    if (strcmp(case_name, "jump-invalid-reset-vector") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_FALSE, valid_sp, 0x00000100u,
                                            valid_vtor, 0, 0);
    }
    if (strcmp(case_name, "jump-vector-table-unaligned") == 0)
    {
        return qboot_host_expect_jump_trace(case_name, RT_FALSE, valid_sp, valid_reset,
                                            valid_vtor + 4u, 0, 0);
    }
    printf("QBOOT_HOST_FAIL unsupported jump case: %s\n", case_name);
    return 2;
}



#ifdef QBOOT_HOST_BACKEND_CUSTOM
/**
 * @brief Run direct fake-flash physical semantics tests.
 *
 * @param case_name Test case name.
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_fake_flash_case(const char *case_name)
{
    rt_uint8_t data[16];
    rt_uint8_t readback[16];
    const rt_uint32_t app = QBOOT_APP_FLASH_ADDR;
    const rt_uint32_t sector = QBOOT_HOST_FLASH_SECTOR_SIZE;

    qboot_host_flash_reset();
    qboot_host_flash_physical_enable(RT_TRUE);
    rt_memset(data, 0xA5, sizeof(data));
    rt_memset(readback, 0, sizeof(readback));

    if (strcmp(case_name, "fake-flash-one-to-zero-only-write") == 0)
    {
        if (qbt_custom_flash_erase(app, sector) != RT_EOK ||
            qbt_custom_flash_write(app, data, sizeof(data)) != RT_EOK ||
            qbt_custom_flash_read(app, readback, sizeof(readback)) != RT_EOK ||
            memcmp(data, readback, sizeof(data)) != 0)
        {
            printf("QBOOT_HOST_FAIL %s physical 1-to-0 write\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-write-without-erase-fail") == 0)
    {
        rt_uint8_t zero = 0x00u;
        rt_uint8_t ff = 0xFFu;
        if (qbt_custom_flash_erase(app, sector) != RT_EOK ||
            qbt_custom_flash_write(app, &zero, 1u) != RT_EOK ||
            qbt_custom_flash_write(app, &ff, 1u) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s rejected rewrite without erase\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-sector-unaligned-erase") == 0)
    {
        if (qbt_custom_flash_erase(app + 1u, sector) == RT_EOK ||
            qbt_custom_flash_erase(app, sector - 1u) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted unaligned erase\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-cross-sector-write") == 0)
    {
        if (qbt_custom_flash_erase(app, sector * 2u) != RT_EOK ||
            qbt_custom_flash_write(app + sector - 8u, data, sizeof(data)) != RT_EOK ||
            qbt_custom_flash_read(app + sector - 8u, readback, sizeof(readback)) != RT_EOK ||
            memcmp(data, readback, sizeof(data)) != 0)
        {
            printf("QBOOT_HOST_FAIL %s cross-sector write\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-partition-nonzero-offset") == 0)
    {
        if (qbt_custom_flash_erase(app, sector) != RT_EOK ||
            qbt_custom_flash_write(app + 17u, data, sizeof(data)) != RT_EOK ||
            qbt_custom_flash_read(app + 17u, readback, sizeof(readback)) != RT_EOK ||
            memcmp(data, readback, sizeof(data)) != 0)
        {
            printf("QBOOT_HOST_FAIL %s nonzero offset readback\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-neighbor-partition-not-corrupted") == 0)
    {
        rt_uint8_t marker[4] = {0x55u, 0x66u, 0x77u, 0x88u};
        rt_uint8_t keep[4] = {0};
        if (qbt_custom_flash_erase(QBOOT_DOWNLOAD_FLASH_ADDR, sector) != RT_EOK ||
            qbt_custom_flash_write(QBOOT_DOWNLOAD_FLASH_ADDR, marker, sizeof(marker)) != RT_EOK ||
            qbt_custom_flash_erase(app, sector) != RT_EOK ||
            qbt_custom_flash_write(app, data, sizeof(data)) != RT_EOK ||
            qbt_custom_flash_read(QBOOT_DOWNLOAD_FLASH_ADDR, keep, sizeof(keep)) != RT_EOK ||
            memcmp(marker, keep, sizeof(marker)) != 0)
        {
            printf("QBOOT_HOST_FAIL %s neighbor partition changed\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-program-unit-aligned") == 0)
    {
        qboot_host_flash_program_unit_set(8u);
        if (qbt_custom_flash_erase(app, sector) != RT_EOK ||
            qbt_custom_flash_write(app + 8u, data, 8u) != RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s aligned program unit\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-program-unit-unaligned-rejected") == 0)
    {
        qboot_host_flash_program_unit_set(8u);
        if (qbt_custom_flash_erase(app, sector) != RT_EOK ||
            qbt_custom_flash_write(app + 1u, data, 8u) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted unaligned program unit\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-program-unit-cross-boundary-rejected") == 0)
    {
        qboot_host_flash_program_unit_set(8u);
        if (qbt_custom_flash_erase(app, sector * 2u) != RT_EOK ||
            qbt_custom_flash_write(app + sector - 8u, data, sizeof(data)) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted cross-sector program unit\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-erase-then-read-all-ff") == 0)
    {
        rt_memset(readback, 0, sizeof(readback));
        if (qbt_custom_flash_erase(app, sector) != RT_EOK ||
            qbt_custom_flash_read(app, readback, sizeof(readback)) != RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s erase/read\n", case_name);
            return 1;
        }
        for (size_t i = 0; i < sizeof(readback); i++)
        {
            if (readback[i] != 0xFFu)
            {
                printf("QBOOT_HOST_FAIL %s erased byte[%u]=0x%02x\n",
                       case_name, (unsigned)i, readback[i]);
                return 1;
            }
        }
    }
    else if (strcmp(case_name, "fake-flash-double-erase-idempotent-current-policy") == 0)
    {
        if (qbt_custom_flash_erase(app, sector) != RT_EOK ||
            qbt_custom_flash_erase(app, sector) != RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s double erase policy changed\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-program-timeout-current-policy") == 0)
    {
        qboot_host_fault_set(QBOOT_HOST_FAULT_WRITE, QBOOT_HOST_FAULT_TARGET_APP, 0u);
        if (qbt_custom_flash_write(app, data, sizeof(data)) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted injected program timeout\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-erase-timeout-current-policy") == 0)
    {
        qboot_host_fault_set(QBOOT_HOST_FAULT_ERASE, QBOOT_HOST_FAULT_TARGET_APP, 0u);
        if (qbt_custom_flash_erase(app, sector) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted injected erase timeout\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fake-flash-wear-count-not-exceeded-smoke") == 0)
    {
        for (rt_uint32_t i = 0; i < 4u; i++)
        {
            if (qbt_custom_flash_erase(app, sector) != RT_EOK)
            {
                printf("QBOOT_HOST_FAIL %s erase loop %u\n", case_name, (unsigned)i);
                return 1;
            }
        }
    }
    else
    {
        printf("QBOOT_HOST_FAIL unsupported fake flash case: %s\n", case_name);
        return 2;
    }
    printf("QBOOT_HOST_CASE_PASS %s physical=1\n", case_name);
    return 0;
}
#else
static int qboot_host_run_fake_flash_case(const char *case_name)
{
    RT_UNUSED(case_name);
    return 2;
}
#endif /* QBOOT_HOST_BACKEND_CUSTOM */

#ifdef QBOOT_HOST_BACKEND_FS
/**
 * @brief Prepare DOWNLOAD storage with a readable firmware header.
 *
 * @return RT_TRUE on success, RT_FALSE on setup failure.
 */
static rt_bool_t qboot_host_prepare_fs_download_header(void)
{
    fw_info_t info;

    rt_memset(&info, 0, sizeof(info));
    rt_memcpy(info.type, "RBL", 3u);
    rt_strcpy((char *)info.part_name, QBOOT_APP_PART_NAME);
    return qboot_host_flash_load(QBOOT_TARGET_DOWNLOAD,
                                 (const rt_uint8_t *)&info,
                                 (rt_uint32_t)sizeof(info));
}

/**
 * @brief Run direct filesystem backend boundary tests.
 *
 * @param case_name Test case name.
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_fs_boundary_case(const char *case_name)
{
    void *handle = RT_NULL;
    rt_uint32_t part_size = 0;
    rt_uint8_t data[8] = {0x10u, 0x21u, 0x32u, 0x43u, 0x54u, 0x65u, 0x76u, 0x87u};
    rt_uint8_t readback[8] = {0};
    rt_bool_t opened = RT_FALSE;
    rt_err_t result = -RT_ERROR;

    qboot_host_flash_reset();
    if (strcmp(case_name, "fs-mount-missing") == 0)
    {
        unlink(QBOOT_APP_FILE_PATH);
        unlink(QBOOT_APP_SIGN_FILE_PATH);
        unlink(QBOOT_DOWNLOAD_FILE_PATH);
        unlink(QBOOT_DOWNLOAD_SIGN_FILE_PATH);
        unlink(QBOOT_FACTORY_FILE_PATH);
        unlink("_ci/host-sim/fs/download.tmp");
        rmdir("_ci/host-sim/fs");
        opened = qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                                  QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC);
        if (opened)
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s opened missing directory\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-read-short-count") == 0)
    {
        FILE *fp = fopen(QBOOT_DOWNLOAD_FILE_PATH, "wb");
        if (fp == RT_NULL || fwrite(data, 1u, 2u, fp) != 2u)
        {
            if (fp != RT_NULL)
            {
                fclose(fp);
            }
            printf("QBOOT_HOST_FAIL %s setup\n", case_name);
            return 1;
        }
        fclose(fp);
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
        {
            printf("QBOOT_HOST_FAIL %s open\n", case_name);
            return 1;
        }
        result = _header_io_ops->read(handle, 0u, readback, sizeof(readback));
        qbt_target_close(handle);
        if (result == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted short read\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-size-after-truncate-zero") == 0)
    {
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                              QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
        {
            printf("QBOOT_HOST_FAIL %s open\n", case_name);
            return 1;
        }
        if (_header_io_ops->size(handle, &part_size) != RT_EOK || part_size != 0u)
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s size=%u\n", case_name, (unsigned)part_size);
            return 1;
        }
        qbt_target_close(handle);
    }
    else if (strcmp(case_name, "fs-close-reopen-readback") == 0)
    {
        if (!qboot_host_flash_load(QBOOT_TARGET_DOWNLOAD, data, sizeof(data)) ||
            !qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
        {
            printf("QBOOT_HOST_FAIL %s setup\n", case_name);
            return 1;
        }
        qbt_target_close(handle);
        handle = RT_NULL;
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ) ||
            _header_io_ops->read(handle, 0u, readback, sizeof(readback)) != RT_EOK ||
            memcmp(data, readback, sizeof(data)) != 0)
        {
            if (handle != RT_NULL)
            {
                qbt_target_close(handle);
            }
            printf("QBOOT_HOST_FAIL %s reopen readback\n", case_name);
            return 1;
        }
        qbt_target_close(handle);
    }
    else if (strcmp(case_name, "fs-write-short-count") == 0 ||
             strcmp(case_name, "fs-no-space-left") == 0)
    {
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                              QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
        {
            printf("QBOOT_HOST_FAIL %s open\n", case_name);
            return 1;
        }
        qboot_host_fault_set(QBOOT_HOST_FAULT_WRITE, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u);
        result = _header_io_ops->write(handle, 0u, data, sizeof(data));
        qbt_target_close(handle);
        if (result == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted failed write\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-path-too-long") == 0)
    {
        char long_path[512];
        rt_memset(long_path, 'x', sizeof(long_path));
        long_path[sizeof(long_path) - 1u] = '\0';
        FILE *fp = fopen(long_path, "rb");
        if (fp != RT_NULL)
        {
            fclose(fp);
            printf("QBOOT_HOST_FAIL %s opened overlong path\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-download-path-readonly") == 0 ||
             strcmp(case_name, "fs-sign-path-readonly") == 0 ||
             strcmp(case_name, "fs-reopen-fail-after-write-current-policy") == 0)
    {
        qboot_host_fault_set(QBOOT_HOST_FAULT_OPEN, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u);
        opened = qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                                 QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC);
        if (opened)
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s accepted injected open failure\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-download-and-sign-same-path") == 0)
    {
        if (strcmp(QBOOT_DOWNLOAD_FILE_PATH, QBOOT_DOWNLOAD_SIGN_FILE_PATH) == 0)
        {
            printf("QBOOT_HOST_FAIL %s download/sign paths alias\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-stale-temp-file-cleanup") == 0)
    {
        FILE *fp = fopen("_ci/host-sim/fs/download.tmp", "wb");
        if (fp == RT_NULL || fwrite(data, 1u, sizeof(data), fp) != sizeof(data))
        {
            if (fp != RT_NULL)
            {
                fclose(fp);
            }
            printf("QBOOT_HOST_FAIL %s setup temp\n", case_name);
            return 1;
        }
        fclose(fp);
        if (!qboot_host_flash_load(QBOOT_TARGET_DOWNLOAD, data, sizeof(data)) ||
            access("_ci/host-sim/fs/download.tmp", F_OK) != 0)
        {
            printf("QBOOT_HOST_FAIL %s stale temp policy changed\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-existing-sign-file-shorter-than-sign") == 0 ||
             strcmp(case_name, "fs-existing-sign-file-longer-than-sign") == 0)
    {
        FILE *fp;
        rt_uint32_t sign_word = QBOOT_RELEASE_SIGN_WORD;

        if (!qboot_host_prepare_fs_download_header())
        {
            printf("QBOOT_HOST_FAIL %s download context setup\n", case_name);
            return 1;
        }
        fp = fopen(QBOOT_DOWNLOAD_SIGN_FILE_PATH, "wb");
        if (fp == RT_NULL || fwrite(&sign_word, 1u, sizeof(sign_word), fp) != sizeof(sign_word))
        {
            if (fp != RT_NULL)
            {
                fclose(fp);
            }
            printf("QBOOT_HOST_FAIL %s valid sign setup\n", case_name);
            return 1;
        }
        fclose(fp);
        if (!qboot_host_download_has_release_sign())
        {
            printf("QBOOT_HOST_FAIL %s valid sign rejected\n", case_name);
            return 1;
        }

        fp = fopen(QBOOT_DOWNLOAD_SIGN_FILE_PATH, "wb");
        if (fp == RT_NULL)
        {
            printf("QBOOT_HOST_FAIL %s sign open\n", case_name);
            return 1;
        }
        if (strcmp(case_name, "fs-existing-sign-file-shorter-than-sign") == 0)
        {
            if (fwrite(&sign_word, 1u, 1u, fp) != 1u)
            {
                fclose(fp);
                printf("QBOOT_HOST_FAIL %s short sign write\n", case_name);
                return 1;
            }
        }
        else
        {
            if (fwrite(&sign_word, 1u, sizeof(sign_word), fp) != sizeof(sign_word) ||
                fwrite(data, 1u, sizeof(data), fp) != sizeof(data))
            {
                fclose(fp);
                printf("QBOOT_HOST_FAIL %s long sign write\n", case_name);
                return 1;
            }
        }
        fclose(fp);
        if (strcmp(case_name, "fs-existing-sign-file-shorter-than-sign") == 0)
        {
            if (qboot_host_download_has_release_sign())
            {
                printf("QBOOT_HOST_FAIL %s accepted short sign file\n", case_name);
                return 1;
            }
        }
        else if (!qboot_host_download_has_release_sign())
        {
            printf("QBOOT_HOST_FAIL %s rejected sign file with trailing bytes\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-existing-download-file-longer-than-package") == 0)
    {
        FILE *fp = fopen(QBOOT_DOWNLOAD_FILE_PATH, "wb");
        if (fp == RT_NULL || fwrite(data, 1u, sizeof(data), fp) != sizeof(data))
        {
            if (fp != RT_NULL)
            {
                fclose(fp);
            }
            printf("QBOOT_HOST_FAIL %s setup download\n", case_name);
            return 1;
        }
        fclose(fp);
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                             QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC) ||
            _header_io_ops->size(handle, &part_size) != RT_EOK || part_size != 0u)
        {
            if (handle != RT_NULL)
            {
                qbt_target_close(handle);
            }
            printf("QBOOT_HOST_FAIL %s stale download length\n", case_name);
            return 1;
        }
        qbt_target_close(handle);
    }
    else if (strcmp(case_name, "fs-close-fail-current-policy") == 0)
    {
        qboot_host_fault_set(QBOOT_HOST_FAULT_CLOSE, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u);
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                             QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
        {
            printf("QBOOT_HOST_FAIL %s open\n", case_name);
            return 1;
        }
        if (_header_io_ops->write(handle, 0u, data, sizeof(data)) != RT_EOK)
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s write before close fault\n", case_name);
            return 1;
        }
        result = _header_io_ops->close(handle);
        if (result == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted injected close failure\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-write-fail-after-download-write-current-policy") == 0 ||
             strcmp(case_name, "fs-write-fail-after-download-retry-current-policy") == 0 ||
             strcmp(case_name, "fs-write-fail-after-download-overwrite-current-policy") == 0)
    {
        qboot_host_fault_set(QBOOT_HOST_FAULT_WRITE, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u);
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                             QBT_OPEN_WRITE | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
        {
            printf("QBOOT_HOST_FAIL %s open\n", case_name);
            return 1;
        }
        result = _header_io_ops->write(handle, 0u, data, sizeof(data));
        qbt_target_close(handle);
        if (result == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted injected write failure\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-rename-temp-to-download-fail-current-policy") == 0 ||
             strcmp(case_name, "fs-temp-file-power-loss-before-rename") == 0)
    {
        FILE *fp = fopen("_ci/host-sim/fs/download.tmp", "wb");
        if (fp == RT_NULL || fwrite(data, 1u, sizeof(data), fp) != sizeof(data))
        {
            if (fp != RT_NULL)
            {
                fclose(fp);
            }
            printf("QBOOT_HOST_FAIL %s setup temp\n", case_name);
            return 1;
        }
        fclose(fp);
        if (access(QBOOT_DOWNLOAD_FILE_PATH, F_OK) == 0)
        {
            printf("QBOOT_HOST_FAIL %s temp created download unexpectedly\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-mount-lost-during-release") == 0 ||
             strcmp(case_name, "fs-unmount-before-replay") == 0 ||
             strcmp(case_name, "fs-directory-missing-created-or-rejected-policy") == 0)
    {
        unlink(QBOOT_DOWNLOAD_FILE_PATH);
        unlink(QBOOT_DOWNLOAD_SIGN_FILE_PATH);
        rmdir("_ci/host-sim/fs");
        opened = qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ);
        if (opened)
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s opened after mount loss\n", case_name);
            return 1;
        }
    }
    else if (strcmp(case_name, "fs-stale-temp-sign-file-ignored") == 0)
    {
        FILE *fp = fopen("_ci/host-sim/fs/download.sign.tmp", "wb");
        if (fp == RT_NULL || fwrite(data, 1u, sizeof(data), fp) != sizeof(data))
        {
            if (fp != RT_NULL)
            {
                fclose(fp);
            }
            printf("QBOOT_HOST_FAIL %s setup temp sign\n", case_name);
            return 1;
        }
        fclose(fp);
        if (qboot_host_download_has_release_sign())
        {
            printf("QBOOT_HOST_FAIL %s accepted stale temp sign\n", case_name);
            return 1;
        }
    }
    else
    {
        printf("QBOOT_HOST_FAIL unsupported fs boundary case: %s\n", case_name);
        return 2;
    }
    printf("QBOOT_HOST_CASE_PASS %s fs-boundary=1\n", case_name);
    return 0;
}
#else
static int qboot_host_run_fs_boundary_case(const char *case_name)
{
    RT_UNUSED(case_name);
    return 2;
}
#endif /* QBOOT_HOST_BACKEND_FS */

/**
 * @brief Build firmware-info context for release-sign position tests.
 *
 * @param info     Output firmware header context.
 * @param pkg_size Package payload size that determines sign position.
 */
static void qboot_host_make_sign_info(fw_info_t *info, rt_uint32_t pkg_size)
{
    rt_memset(info, 0, sizeof(*info));
    rt_memcpy(info->type, "RBL", 3u);
    rt_strcpy((char *)info->part_name, QBOOT_APP_PART_NAME);
    rt_strcpy((char *)info->fw_ver, "v-ci-sign");
#ifdef QBOOT_USING_PRODUCT_CODE
    rt_strcpy((char *)info->prod_code, QBOOT_PRODUCT_CODE);
#endif /* QBOOT_USING_PRODUCT_CODE */
    info->pkg_size = pkg_size;
    info->raw_size = 1u;
}

/**
 * @brief Run direct release-sign position and isolation tests.
 *
 * @param case_name Test case name.
 * @return 0 on pass, non-zero on failure.
 */
static int qboot_host_run_sign_boundary_case(const char *case_name)
{
    void *handle = RT_NULL;
    void *app_handle = RT_NULL;
    rt_uint32_t part_size = 0;
    rt_uint32_t app_size = 0;
    rt_uint32_t pkg_size = 4097u;
    fw_info_t info;
    rt_bool_t expect_write = RT_TRUE;
    rt_bool_t released = RT_FALSE;
    rt_uint8_t marker[4] = {0x5Au, 0xC3u, 0x3Cu, 0xA5u};
    rt_uint8_t readback[4] = {0};

    qboot_host_flash_reset();
    if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size,
                         QBT_OPEN_WRITE | QBT_OPEN_READ | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
    {
        printf("QBOOT_HOST_FAIL %s open download\n", case_name);
        return 1;
    }
    if (qbt_erase_with_feed(handle, 0u, part_size) != RT_EOK)
    {
        qbt_target_close(handle);
        printf("QBOOT_HOST_FAIL %s erase download\n", case_name);
        return 1;
    }

    if (strcmp(case_name, "sign-align-exact") == 0)
    {
        pkg_size = 4096u;
    }
    else if (strcmp(case_name, "sign-align-plus-padding") == 0)
    {
        pkg_size = 4097u;
    }
    else if (strcmp(case_name, "sign-at-partition-end-exact") == 0)
    {
        pkg_size = part_size - (rt_uint32_t)qboot_src_read_pos() - QBOOT_RELEASE_SIGN_ALIGN_SIZE;
    }
    else if (strcmp(case_name, "sign-write-cross-sector") == 0)
    {
        pkg_size = QBOOT_HOST_FLASH_SECTOR_SIZE - (rt_uint32_t)qboot_src_read_pos() - 2u;
    }
    else if (strcmp(case_name, "sign-position-out-of-range") == 0)
    {
        pkg_size = part_size;
        expect_write = RT_FALSE;
    }
    else if (strcmp(case_name, "sign-erase-does-not-corrupt-app-tail") == 0)
    {
        if (!qbt_target_open(QBOOT_TARGET_APP, &app_handle, &app_size,
                             QBT_OPEN_WRITE | QBT_OPEN_READ | QBT_OPEN_CREATE | QBT_OPEN_TRUNC))
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s open app\n", case_name);
            return 1;
        }
        if (qbt_erase_with_feed(app_handle, 0u, app_size) != RT_EOK ||
            _header_io_ops->write(app_handle, app_size - sizeof(marker), marker, sizeof(marker)) != RT_EOK)
        {
            qbt_target_close(app_handle);
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s write app tail marker\n", case_name);
            return 1;
        }
    }
    else
    {
        qbt_target_close(handle);
        printf("QBOOT_HOST_FAIL unsupported sign boundary case: %s\n", case_name);
        return 2;
    }

    qboot_host_make_sign_info(&info, pkg_size);
    if (qbt_release_sign_write(handle, QBOOT_DOWNLOAD_PART_NAME, &info) != expect_write)
    {
        if (app_handle != RT_NULL)
        {
            qbt_target_close(app_handle);
        }
        qbt_target_close(handle);
        printf("QBOOT_HOST_FAIL %s sign write expectation\n", case_name);
        return 1;
    }
    if (expect_write)
    {
        released = qbt_release_sign_check(handle, QBOOT_DOWNLOAD_PART_NAME, &info);
        if (!released)
        {
            if (app_handle != RT_NULL)
            {
                qbt_target_close(app_handle);
            }
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s sign check\n", case_name);
            return 1;
        }
    }
    if (app_handle != RT_NULL)
    {
        if (_header_io_ops->read(app_handle, app_size - sizeof(readback), readback, sizeof(readback)) != RT_EOK ||
            memcmp(marker, readback, sizeof(marker)) != 0)
        {
            qbt_target_close(app_handle);
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s app tail changed\n", case_name);
            return 1;
        }
        qbt_target_close(app_handle);
    }
    qbt_target_close(handle);
    printf("QBOOT_HOST_CASE_PASS %s sign-boundary=1 write=%d released=%d\n",
           case_name, expect_write, released);
    return 0;
}

static int qboot_host_run_update_mgr_case(const char *case_name)
{
    s_mgr_reason = QBT_UPD_REASON_REQ;
    s_mgr_enter_count = 0;
    s_mgr_leave_count = 0;
    s_mgr_error_count = 0;
    s_mgr_last_error = 0;
    s_mgr_ready_count = 0;
    s_mgr_app_valid = RT_FALSE;
    s_mgr_recover_ok = RT_FALSE;
    s_mgr_callback_behavior = QBOOT_HOST_CB_BEHAVIOR_NONE;

    if (strcmp(case_name, "update-mgr-start-finish") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_state(case_name, QBT_UPD_STATE_READY);
    }
    if (strcmp(case_name, "update-mgr-abort") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_on_abort();
        return qboot_host_expect_state(case_name, QBT_UPD_STATE_WAIT);
    }
    if (strcmp(case_name, "update-mgr-finish-fail") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_FALSE);
        if (s_mgr_error_count == 0)
        {
            printf("QBOOT_HOST_FAIL %s missing error callback\n", case_name);
            return 1;
        }
        return qboot_host_expect_state(case_name, QBT_UPD_STATE_WAIT);
    }
    if (strcmp(case_name, "update-mgr-register-app-valid") == 0)
    {
        s_mgr_reason = QBT_UPD_REASON_NONE;
        s_mgr_app_valid = RT_TRUE;
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        if (s_mgr_ready_count == 0)
        {
            printf("QBOOT_HOST_FAIL %s missing ready callback\n", case_name);
            return 1;
        }
        return qboot_host_expect_state(case_name, QBT_UPD_STATE_READY);
    }
    if (strcmp(case_name, "update-mgr-register-app-invalid") == 0)
    {
        s_mgr_reason = QBT_UPD_REASON_NONE;
        s_mgr_app_valid = RT_FALSE;
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 0, 0, 0, 0);
    }
    if (strcmp(case_name, "update-mgr-start-twice") == 0 ||
        strcmp(case_name, "concurrent-update-start-rejected") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_on_start();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_RECV, 1, 0, 0, 0);
    }
    if (strcmp(case_name, "update-mgr-finish-twice") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }
    if (strcmp(case_name, "update-mgr-finish-after-abort") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_on_abort();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 0);
    }
    if (strcmp(case_name, "update-mgr-abort-after-finish") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        qbt_update_mgr_on_abort();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }
    if (strcmp(case_name, "update-mgr-write-before-start") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 0, 0, 0, 0);
    }
    if (strcmp(case_name, "update-mgr-write-fail-then-abort") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_FALSE);
        qbt_update_mgr_on_abort();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 1);
    }
    if (strcmp(case_name, "update-mgr-write-fail-then-restart") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_FALSE);
        qbt_update_mgr_on_start();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_RECV, 2, 1, 0, 1);
    }
    if (strcmp(case_name, "update-mgr-partial-write-then-finish-current-policy") == 0 ||
        strcmp(case_name, "update-mgr-size-mismatch-on-finish-current-policy") == 0 ||
        strcmp(case_name, "update-mgr-finish-without-full-body-current-policy") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_set_total(100u);
        qbt_update_mgr_on_data_len(1u);
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }
    if (strcmp(case_name, "update-mgr-zero-total-size-current-policy") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_set_total(0u);
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }
    if (strcmp(case_name, "update-mgr-abort-clears-download-state") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_set_total(100u);
        qbt_update_mgr_on_data_len(8u);
        qbt_update_mgr_on_abort();
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 2, 2, 1, 0);
    }

    if (strcmp(case_name, "receive-abort-after-partial-body-then-restart") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_set_total(100u);
        qbt_update_mgr_on_data_len(32u);
        qbt_update_mgr_on_abort();
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 2, 2, 1, 0);
    }
    if (strcmp(case_name, "receive-finish-after-write-error-rejected") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_FALSE);
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 1);
    }
    if (strcmp(case_name, "receive-total-size-size_t-overflow-rejected") == 0 ||
        strcmp(case_name, "receive-offset-plus-size-overflow-rejected") == 0)
    {
        const qboot_store_desc_t *desc = qbt_target_desc(QBOOT_TARGET_DOWNLOAD);
        rt_uint8_t byte = 0xA5u;
        rt_uint32_t offset = 0u;
        rt_uint32_t size = 0xFFFFFFFFu;

        if (desc == RT_NULL || desc->flash_len < 2u)
        {
            printf("QBOOT_HOST_FAIL %s missing download descriptor\n", case_name);
            return 1;
        }
        if (strcmp(case_name, "receive-offset-plus-size-overflow-rejected") == 0)
        {
            offset = desc->flash_len - 1u;
            size = 2u;
        }

        qboot_host_flash_reset();
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        if (!qbt_update_mgr_download_begin())
        {
            printf("QBOOT_HOST_FAIL %s begin helper download\n", case_name);
            return 1;
        }
        if (qbt_update_mgr_download_write(offset, &byte, size))
        {
            printf("QBOOT_HOST_FAIL %s invalid helper write accepted\n", case_name);
            (void)qbt_update_mgr_download_finish(RT_FALSE);
            return 1;
        }
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 1);
    }
    if (strcmp(case_name, "update-helper-backend-size-smoke") == 0)
    {
        rt_uint8_t payload[37];
        void *handle = RT_NULL;
        rt_uint32_t part_size = 0u;
        fw_info_t info = {0};

        for (rt_uint32_t i = 0u; i < (rt_uint32_t)sizeof(payload); i++)
        {
            payload[i] = (rt_uint8_t)(0x30u + i);
        }

        qboot_host_flash_reset();
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        if (!qbt_update_mgr_download_begin())
        {
            printf("QBOOT_HOST_FAIL %s begin helper download\n", case_name);
            return 1;
        }
        if (!qbt_update_mgr_download_write(0u, payload, (rt_uint32_t)sizeof(payload)))
        {
            printf("QBOOT_HOST_FAIL %s write helper payload\n", case_name);
            return 1;
        }
        if (qbt_update_mgr_download_finish(RT_TRUE) != RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s finish helper download\n", case_name);
            return 1;
        }
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
        {
            printf("QBOOT_HOST_FAIL %s open download\n", case_name);
            return 1;
        }
        if (!qbt_fw_check(handle, part_size, QBOOT_DOWNLOAD_PART_NAME, &info))
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s check helper backend size\n", case_name);
            return 1;
        }
        qbt_target_close(handle);
        if (info.raw_size != part_size || info.pkg_size != part_size)
        {
            printf("QBOOT_HOST_FAIL %s backend size raw=%u pkg=%u size=%u\n",
                   case_name, (unsigned int)info.raw_size,
                   (unsigned int)info.pkg_size, (unsigned int)part_size);
            return 1;
        }
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }
    if (strcmp(case_name, "update-helper-abort-clears-session") == 0)
    {
        rt_uint8_t payload[37];
        void *handle = RT_NULL;
        rt_uint32_t part_size = 0u;
        fw_info_t info = {0};

        for (rt_uint32_t i = 0u; i < (rt_uint32_t)sizeof(payload); i++)
        {
            payload[i] = (rt_uint8_t)(0x40u + i);
        }

        qboot_host_flash_reset();
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        if (!qbt_update_mgr_download_begin())
        {
            printf("QBOOT_HOST_FAIL %s begin helper download\n", case_name);
            return 1;
        }
        if (!qbt_update_mgr_download_write(0u, payload, (rt_uint32_t)sizeof(payload)))
        {
            printf("QBOOT_HOST_FAIL %s write helper payload\n", case_name);
            return 1;
        }

        qbt_update_mgr_on_abort();

        if (qbt_update_mgr_download_write(0u, payload, (rt_uint32_t)sizeof(payload)))
        {
            printf("QBOOT_HOST_FAIL %s write accepted after abort\n", case_name);
            return 1;
        }
        if (qbt_update_mgr_download_finish(RT_TRUE) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s finish accepted after abort\n", case_name);
            return 1;
        }
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
        {
            printf("QBOOT_HOST_FAIL %s open download after abort\n", case_name);
            return 1;
        }
        if (qbt_fw_check(handle, part_size, QBOOT_DOWNLOAD_PART_NAME, &info))
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s aborted helper download marked valid\n", case_name);
            return 1;
        }
        qbt_target_close(handle);
        if (!qbt_update_mgr_download_begin())
        {
            printf("QBOOT_HOST_FAIL %s restart helper download after abort\n", case_name);
            return 1;
        }
        (void)qbt_update_mgr_download_finish(RT_FALSE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 2, 2, 0, 1);
    }
    if (strcmp(case_name, "update-helper-close-fail-rejected") == 0)
    {
        rt_uint8_t payload[37];
        void *handle = RT_NULL;
        rt_uint32_t part_size = 0u;
        fw_info_t info = {0};

        for (rt_uint32_t i = 0u; i < (rt_uint32_t)sizeof(payload); i++)
        {
            payload[i] = (rt_uint8_t)(0x50u + i);
        }

        qboot_host_flash_reset();
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        if (!qbt_update_mgr_download_begin())
        {
            printf("QBOOT_HOST_FAIL %s begin helper download\n", case_name);
            return 1;
        }
        if (!qbt_update_mgr_download_write(0u, payload, (rt_uint32_t)sizeof(payload)))
        {
            printf("QBOOT_HOST_FAIL %s write helper payload\n", case_name);
            return 1;
        }

        qboot_host_fault_set(QBOOT_HOST_FAULT_CLOSE, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u);
        if (qbt_update_mgr_download_finish(RT_TRUE) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted close failure\n", case_name);
            return 1;
        }
        if (s_mgr_last_error != -RT_ERROR)
        {
            printf("QBOOT_HOST_FAIL %s close error not propagated: %d\n",
                   case_name, s_mgr_last_error);
            return 1;
        }
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
        {
            printf("QBOOT_HOST_FAIL %s open download after close fault\n", case_name);
            return 1;
        }
        if (qbt_fw_check(handle, part_size, QBOOT_DOWNLOAD_PART_NAME, &info))
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s close-failed download marked valid\n", case_name);
            return 1;
        }
        qbt_target_close(handle);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 1);
    }
    if (strcmp(case_name, "update-helper-close-fail-on-reject-propagated") == 0)
    {
        rt_uint8_t payload[37];

        for (rt_uint32_t i = 0u; i < (rt_uint32_t)sizeof(payload); i++)
        {
            payload[i] = (rt_uint8_t)(0x60u + i);
        }

        qboot_host_flash_reset();
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        if (!qbt_update_mgr_download_begin())
        {
            printf("QBOOT_HOST_FAIL %s begin helper download\n", case_name);
            return 1;
        }
        if (!qbt_update_mgr_download_write(0u, payload, (rt_uint32_t)sizeof(payload)))
        {
            printf("QBOOT_HOST_FAIL %s write helper payload\n", case_name);
            return 1;
        }

        qboot_host_fault_set(QBOOT_HOST_FAULT_CLOSE, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u);
        if (qbt_update_mgr_download_finish(RT_FALSE) == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s accepted close failure on reject\n", case_name);
            return 1;
        }
        if (s_mgr_last_error != -RT_ERROR)
        {
            printf("QBOOT_HOST_FAIL %s reject close error not propagated: %d\n",
                   case_name, s_mgr_last_error);
            return 1;
        }
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 1);
    }
    if (strcmp(case_name, "update-helper-abort-close-fail-propagated") == 0)
    {
        rt_uint8_t payload[37];
        void *handle = RT_NULL;
        rt_uint32_t part_size = 0u;
        fw_info_t info = {0};

        for (rt_uint32_t i = 0u; i < (rt_uint32_t)sizeof(payload); i++)
        {
            payload[i] = (rt_uint8_t)(0x70u + i);
        }

        qboot_host_flash_reset();
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        if (!qbt_update_mgr_download_begin())
        {
            printf("QBOOT_HOST_FAIL %s begin helper download\n", case_name);
            return 1;
        }
        if (!qbt_update_mgr_download_write(0u, payload, (rt_uint32_t)sizeof(payload)))
        {
            printf("QBOOT_HOST_FAIL %s write helper payload\n", case_name);
            return 1;
        }

        qboot_host_fault_set(QBOOT_HOST_FAULT_CLOSE, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u);
        qbt_update_mgr_on_abort();
        if (s_mgr_last_error != -RT_ERROR)
        {
            printf("QBOOT_HOST_FAIL %s abort close error not propagated: %d\n",
                   case_name, s_mgr_last_error);
            return 1;
        }
        if (!qbt_target_open(QBOOT_TARGET_DOWNLOAD, &handle, &part_size, QBT_OPEN_READ))
        {
            printf("QBOOT_HOST_FAIL %s open download after abort close fault\n", case_name);
            return 1;
        }
        if (qbt_fw_check(handle, part_size, QBOOT_DOWNLOAD_PART_NAME, &info))
        {
            qbt_target_close(handle);
            printf("QBOOT_HOST_FAIL %s abort close-failed download marked valid\n",
                   case_name);
            return 1;
        }
        qbt_target_close(handle);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_ERROR, 1, 1, 0, 1);
    }
    if (strcmp(case_name, "update-helper-ready-close-fail-retries-before-ready") == 0)
    {
        rt_uint8_t payload[37];

        for (rt_uint32_t i = 0u; i < (rt_uint32_t)sizeof(payload); i++)
        {
            payload[i] = (rt_uint8_t)(0x80u + i);
        }

        qboot_host_flash_reset();
        qbt_update_mgr_register(&s_update_ops, 1u, 1u);
        if (!qbt_update_mgr_download_begin())
        {
            printf("QBOOT_HOST_FAIL %s begin helper download\n", case_name);
            return 1;
        }
        if (!qbt_update_mgr_download_write(0u, payload, (rt_uint32_t)sizeof(payload)))
        {
            printf("QBOOT_HOST_FAIL %s write helper payload\n", case_name);
            return 1;
        }

        s_mgr_app_valid = RT_TRUE;
        qboot_host_fault_set(QBOOT_HOST_FAULT_CLOSE, QBOOT_HOST_FAULT_TARGET_DOWNLOAD, 0u);
        qbt_update_mgr_poll(1u);
        if (s_mgr_last_error != -RT_ERROR)
        {
            printf("QBOOT_HOST_FAIL %s ready close error not propagated: %d\n",
                   case_name, s_mgr_last_error);
            return 1;
        }
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 0, 1, 1);
    }
    if (strcmp(case_name, "callback-null-all") == 0)
    {
        qbt_update_mgr_register(RT_NULL, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_IDLE, 0, 0, 0, 0);
    }
    if (strcmp(case_name, "callback-reentrant-update-rejected") == 0)
    {
        s_mgr_callback_behavior = QBOOT_HOST_CB_BEHAVIOR_REENTER_START;
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_RECV, 1, 0, 0, 0);
    }
    if (strcmp(case_name, "callback-abort-during-progress") == 0)
    {
        s_mgr_callback_behavior = QBOOT_HOST_CB_BEHAVIOR_ABORT_ENTER;
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 0);
    }
    if (strcmp(case_name, "callback-order-check") == 0 ||
        strcmp(case_name, "callback-count-check") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }

    if (strcmp(case_name, "callback-progress-monotonic") == 0 ||
        strcmp(case_name, "callback-progress-final-100-on-success") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_set_total(100u);
        qbt_update_mgr_on_data_len(40u);
        qbt_update_mgr_on_data_len(60u);
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }
    if (strcmp(case_name, "callback-progress-not-100-on-failure") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_set_total(100u);
        qbt_update_mgr_on_data_len(40u);
        (void)qbt_update_mgr_on_finish(RT_FALSE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 1);
    }
    if (strcmp(case_name, "callback-error-code-propagated") == 0)
    {
        rt_err_t result;

        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        result = qbt_update_mgr_on_finish(RT_FALSE);
        if (result != RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s finish result=%d\n", case_name, result);
            return 1;
        }
        if (s_mgr_last_error != -RT_ERROR)
        {
            printf("QBOOT_HOST_FAIL %s last_error=%d\n", case_name, s_mgr_last_error);
            return 1;
        }
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 1);
    }
    if (strcmp(case_name, "callback-abort-during-sign-phase") == 0 ||
        strcmp(case_name, "callback-abort-during-decompress-phase") == 0 ||
        strcmp(case_name, "callback-abort-during-hpatch-phase") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_on_abort();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 0);
    }
    if (strcmp(case_name, "callback-reentrant-finish-rejected") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }
    if (strcmp(case_name, "update-mgr-register-backend-after-start-rejected") == 0 ||
        strcmp(case_name, "update-mgr-register-backend-during-update-rejected") == 0 ||
        strcmp(case_name, "update-mgr-unregister-or-replace-backend-current-policy") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_RECV, 1, 0, 0, 0);
    }
    if (strcmp(case_name, "update-mgr-start-after-failed-finish") == 0 ||
        strcmp(case_name, "update-mgr-start-after-failed-abort") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        (void)qbt_update_mgr_on_finish(RT_FALSE);
        qbt_update_mgr_on_start();
        qbt_update_mgr_on_abort();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 2, 2, 0, 1);
    }
    if (strcmp(case_name, "update-mgr-finish-callback-reentrant-start-rejected") == 0 ||
        strcmp(case_name, "update-mgr-abort-callback-reentrant-start-rejected") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        if (strstr(case_name, "abort") != RT_NULL)
        {
            qbt_update_mgr_on_abort();
            return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 1, 1, 0, 0);
        }
        (void)qbt_update_mgr_on_finish(RT_TRUE);
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_READY, 1, 1, 1, 0);
    }
    if (strcmp(case_name, "update-mgr-multiple-contexts-current-policy") == 0 ||
        strcmp(case_name, "error-code-update-in-progress") == 0)
    {
        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        qbt_update_mgr_on_start();
        qbt_update_mgr_on_start();
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_RECV, 1, 0, 0, 0);
    }
    if (strcmp(case_name, "error-code-update-not-started") == 0)
    {
        rt_err_t result;

        qbt_update_mgr_register(&s_update_ops, 1000u, 1000u);
        result = qbt_update_mgr_on_finish(RT_TRUE);
        if (result == RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s finish accepted before start\n", case_name);
            return 1;
        }
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_WAIT, 0, 0, 0, 0);
    }
    if (strcmp(case_name, "backend-register-twice") == 0)
    {
        if (qboot_register_storage_ops() != RT_EOK || qboot_register_storage_ops() != RT_EOK)
        {
            printf("QBOOT_HOST_FAIL %s register storage ops twice\n", case_name);
            return 1;
        }
        return qboot_host_expect_update_counts(case_name, QBT_UPD_STATE_IDLE, 0, 0, 0, 0);
    }
    printf("QBOOT_HOST_FAIL unsupported update manager case: %s\n", case_name);
    return 2;
}

static rt_bool_t qboot_host_parse_args(int argc, char **argv, qboot_host_args_t *args)
{
    qboot_host_fault_target_t fault_target;
    rt_uint32_t fault_after;

    qboot_host_args_init(args);
    for (int i = 1; i < argc; i++)
    {
        const char *opt = argv[i];
#define NEED_ARG() do { if (i + 1 >= argc) return RT_FALSE; } while (0)
        if (strcmp(opt, "--help") == 0)
        {
            qboot_host_usage(argv[0]);
            exit(0);
        }
        else if (strcmp(opt, "--mode") == 0)
        {
            NEED_ARG();
            opt = argv[++i];
            if (strcmp(opt, "release") == 0) { args->mode = QBOOT_HOST_MODE_RELEASE; }
            else if (strcmp(opt, "update-mgr") == 0) { args->mode = QBOOT_HOST_MODE_UPDATE_MGR; }
            else if (strcmp(opt, "jump-stub") == 0) { args->mode = QBOOT_HOST_MODE_JUMP_STUB; }
            else if (strcmp(opt, "fake-flash") == 0) { args->mode = QBOOT_HOST_MODE_FAKE_FLASH; }
            else if (strcmp(opt, "fs-boundary") == 0) { args->mode = QBOOT_HOST_MODE_FS_BOUNDARY; }
            else if (strcmp(opt, "sign-boundary") == 0) { args->mode = QBOOT_HOST_MODE_SIGN_BOUNDARY; }
            else if (strcmp(opt, "repeat-sequence") == 0) { args->mode = QBOOT_HOST_MODE_REPEAT_SEQUENCE; }
            else if (strcmp(opt, "fault-sequence") == 0) { args->mode = QBOOT_HOST_MODE_FAULT_SEQUENCE; }
            else { return RT_FALSE; }
        }
        else if (strcmp(opt, "--inspect") == 0) { args->inspect = RT_TRUE; }
        else if (strcmp(opt, "--case") == 0) { NEED_ARG(); args->case_name = argv[++i]; }
        else if (strcmp(opt, "--package") == 0) { NEED_ARG(); args->package_path = argv[++i]; }
        else if (strcmp(opt, "--old-app") == 0) { NEED_ARG(); args->old_app_path = argv[++i]; }
        else if (strcmp(opt, "--new-app") == 0) { NEED_ARG(); args->new_app_path = argv[++i]; }
        else if (strcmp(opt, "--fixture-dir") == 0) { NEED_ARG(); args->fixture_dir = argv[++i]; }
        else if (strcmp(opt, "--receive-mode") == 0) { NEED_ARG(); args->receive_mode = argv[++i]; }
        else if (strcmp(opt, "--chunk") == 0) { NEED_ARG(); if (!qboot_host_parse_u32(argv[++i], &args->chunk_size)) return RT_FALSE; }
        else if (strcmp(opt, "--download-limit") == 0) { NEED_ARG(); if (!qboot_host_parse_u32(argv[++i], &args->download_limit)) return RT_FALSE; }
        else if (strcmp(opt, "--expect-receive") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->expect_receive)) return RT_FALSE; }
        else if (strcmp(opt, "--expect-first-success") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->expect_first)) return RT_FALSE; }
        else if (strcmp(opt, "--expect-success") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->expect_success)) return RT_FALSE; }
        else if (strcmp(opt, "--expect-jump") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->expect_jump)) return RT_FALSE; }
        else if (strcmp(opt, "--expect-sign") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->expect_sign)) return RT_FALSE; args->expect_sign_set = RT_TRUE; }
        else if (strcmp(opt, "--expect-app") == 0) { NEED_ARG(); if (!qboot_host_parse_app_expect(argv[++i], &args->expect_app)) return RT_FALSE; }
        else if (strcmp(opt, "--replay") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->replay)) return RT_FALSE; }
        else if (strcmp(opt, "--skip-first-jump") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->skip_first_jump)) return RT_FALSE; }
        else if (strcmp(opt, "--fault-before-receive") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->fault_before_receive)) return RT_FALSE; }
        else if (strcmp(opt, "--corrupt-sign-before-release") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->corrupt_sign_before_release)) return RT_FALSE; }
        else if (strcmp(opt, "--corrupt-app-before-replay") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->corrupt_app_before_replay)) return RT_FALSE; }
        else if (strcmp(opt, "--malloc-fail-after") == 0) { NEED_ARG(); if (!qboot_host_parse_u32(argv[++i], &args->malloc_fail_after)) return RT_FALSE; args->malloc_fail_enabled = RT_TRUE; }
        else if (strcmp(opt, "--physical-flash") == 0) { NEED_ARG(); if (!qboot_host_parse_bool(argv[++i], &args->physical_flash)) return RT_FALSE; }
        else if (strcmp(opt, "--fail-open") == 0) { NEED_ARG(); if (!qboot_host_parse_fault(argv[++i], &fault_target, &fault_after)) return RT_FALSE; args->fault_enabled[QBOOT_HOST_FAULT_OPEN] = RT_TRUE; args->fault_target[QBOOT_HOST_FAULT_OPEN] = fault_target; args->fault_after[QBOOT_HOST_FAULT_OPEN] = fault_after; }
        else if (strcmp(opt, "--fail-read") == 0) { NEED_ARG(); if (!qboot_host_parse_fault(argv[++i], &fault_target, &fault_after)) return RT_FALSE; args->fault_enabled[QBOOT_HOST_FAULT_READ] = RT_TRUE; args->fault_target[QBOOT_HOST_FAULT_READ] = fault_target; args->fault_after[QBOOT_HOST_FAULT_READ] = fault_after; }
        else if (strcmp(opt, "--fail-write") == 0) { NEED_ARG(); if (!qboot_host_parse_fault(argv[++i], &fault_target, &fault_after)) return RT_FALSE; args->fault_enabled[QBOOT_HOST_FAULT_WRITE] = RT_TRUE; args->fault_target[QBOOT_HOST_FAULT_WRITE] = fault_target; args->fault_after[QBOOT_HOST_FAULT_WRITE] = fault_after; }
        else if (strcmp(opt, "--fail-erase") == 0) { NEED_ARG(); if (!qboot_host_parse_fault(argv[++i], &fault_target, &fault_after)) return RT_FALSE; args->fault_enabled[QBOOT_HOST_FAULT_ERASE] = RT_TRUE; args->fault_target[QBOOT_HOST_FAULT_ERASE] = fault_target; args->fault_after[QBOOT_HOST_FAULT_ERASE] = fault_after; }
        else if (strcmp(opt, "--fail-sign-read") == 0) { NEED_ARG(); if (!qboot_host_parse_fault(argv[++i], &fault_target, &fault_after)) return RT_FALSE; args->fault_enabled[QBOOT_HOST_FAULT_SIGN_READ] = RT_TRUE; args->fault_target[QBOOT_HOST_FAULT_SIGN_READ] = fault_target; args->fault_after[QBOOT_HOST_FAULT_SIGN_READ] = fault_after; }
        else if (strcmp(opt, "--fail-sign-write") == 0) { NEED_ARG(); if (!qboot_host_parse_fault(argv[++i], &fault_target, &fault_after)) return RT_FALSE; args->fault_enabled[QBOOT_HOST_FAULT_SIGN_WRITE] = RT_TRUE; args->fault_target[QBOOT_HOST_FAULT_SIGN_WRITE] = fault_target; args->fault_after[QBOOT_HOST_FAULT_SIGN_WRITE] = fault_after; }
        else if (strcmp(opt, "--fail-close") == 0) { NEED_ARG(); if (!qboot_host_parse_fault(argv[++i], &fault_target, &fault_after)) return RT_FALSE; args->fault_enabled[QBOOT_HOST_FAULT_CLOSE] = RT_TRUE; args->fault_target[QBOOT_HOST_FAULT_CLOSE] = fault_target; args->fault_after[QBOOT_HOST_FAULT_CLOSE] = fault_after; }
        else { return RT_FALSE; }
#undef NEED_ARG
    }
    if (args->mode == QBOOT_HOST_MODE_UPDATE_MGR ||
        args->mode == QBOOT_HOST_MODE_JUMP_STUB ||
        args->mode == QBOOT_HOST_MODE_FAKE_FLASH ||
        args->mode == QBOOT_HOST_MODE_FS_BOUNDARY ||
        args->mode == QBOOT_HOST_MODE_SIGN_BOUNDARY ||
        args->mode == QBOOT_HOST_MODE_REPEAT_SEQUENCE ||
        args->mode == QBOOT_HOST_MODE_FAULT_SEQUENCE)
    {
        return RT_TRUE;
    }
    if (args->package_path == RT_NULL)
    {
        return RT_FALSE;
    }
    if (args->inspect)
    {
        return RT_TRUE;
    }
    if (args->old_app_path == RT_NULL || args->new_app_path == RT_NULL)
    {
        return RT_FALSE;
    }
    if (!args->expect_sign_set)
    {
        args->expect_sign = args->expect_success;
    }
    return RT_TRUE;
}

int main(int argc, char **argv)
{
    qboot_host_args_t args;
    if (!qboot_host_parse_args(argc, argv, &args))
    {
        qboot_host_usage(argv[0]);
        return 2;
    }
    if (qboot_register_storage_ops() != RT_EOK)
    {
        fprintf(stderr, "qboot_register_storage_ops failed\n");
        return 1;
    }
    if (qbot_algo_startup() != RT_EOK)
    {
        fprintf(stderr, "qbot_algo_startup failed\n");
        return 1;
    }
    if (args.mode == QBOOT_HOST_MODE_UPDATE_MGR)
    {
        return qboot_host_run_update_mgr_case(args.case_name);
    }
    if (args.mode == QBOOT_HOST_MODE_JUMP_STUB)
    {
        return qboot_host_run_jump_stub_case(args.case_name);
    }
    if (args.mode == QBOOT_HOST_MODE_FAKE_FLASH)
    {
        return qboot_host_run_fake_flash_case(args.case_name);
    }
    if (args.mode == QBOOT_HOST_MODE_FS_BOUNDARY)
    {
        return qboot_host_run_fs_boundary_case(args.case_name);
    }
    if (args.mode == QBOOT_HOST_MODE_SIGN_BOUNDARY)
    {
        return qboot_host_run_sign_boundary_case(args.case_name);
    }
    if (args.mode == QBOOT_HOST_MODE_REPEAT_SEQUENCE)
    {
        return qboot_host_run_repeat_sequence_case(args.case_name, args.fixture_dir);
    }
    if (args.mode == QBOOT_HOST_MODE_FAULT_SEQUENCE)
    {
        return qboot_host_run_fault_sequence_case(args.case_name, args.fixture_dir);
    }
    if (args.inspect)
    {
        return qboot_host_inspect_package(args.package_path);
    }
    return qboot_host_run_release_case(&args);
}
