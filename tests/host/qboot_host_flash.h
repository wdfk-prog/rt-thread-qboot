/**
 * @file qboot_host_flash.h
 * @brief Host storage simulator helpers for qboot CI tests.
 */
#ifndef QBOOT_HOST_FLASH_H
#define QBOOT_HOST_FLASH_H

#include <qboot.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Base address of the simulated custom flash image. */
#define QBOOT_HOST_FLASH_BASE 0x08000000u
/** @brief Total simulated custom flash size in bytes. */
#define QBOOT_HOST_FLASH_SIZE 0x00100000u
/** @brief Simulated physical erase sector size in bytes. */
#define QBOOT_HOST_FLASH_SECTOR_SIZE 0x00001000u

/** @brief Host storage operation selectors used for fault injection. */
typedef enum
{
    QBOOT_HOST_FAULT_OPEN = 0,   /**< Fail a matching open or partition lookup. */
    QBOOT_HOST_FAULT_READ,      /**< Fail a matching read operation. */
    QBOOT_HOST_FAULT_WRITE,     /**< Fail a matching write operation. */
    QBOOT_HOST_FAULT_ERASE,      /**< Fail a matching erase or truncate operation. */
    QBOOT_HOST_FAULT_SIGN_READ,  /**< Fail a matching release-sign read operation. */
    QBOOT_HOST_FAULT_SIGN_WRITE, /**< Fail a matching release-sign write operation. */
    QBOOT_HOST_FAULT_CLOSE,      /**< Fail a matching close operation after cleanup. */
    QBOOT_HOST_FAULT_COUNT       /**< Number of operation selectors. */
} qboot_host_fault_op_t;

/** @brief Target selector used by the host flash fault injector. */
typedef enum
{
    QBOOT_HOST_FAULT_TARGET_ANY = 0, /**< Match any target. */
    QBOOT_HOST_FAULT_TARGET_APP,      /**< Match the APP target. */
    QBOOT_HOST_FAULT_TARGET_DOWNLOAD, /**< Match the DOWNLOAD target. */
    QBOOT_HOST_FAULT_TARGET_FACTORY,  /**< Match the FACTORY target. */
    QBOOT_HOST_FAULT_TARGET_SWAP,     /**< Match the HPatchLite SWAP target. */
    QBOOT_HOST_FAULT_TARGET_COUNT     /**< Number of target selectors. */
} qboot_host_fault_target_t;

/** @brief Reset the complete simulated storage image to erased state. */
void qboot_host_flash_reset(void);

/** @brief Reset all configured host flash fault injection rules. */
void qboot_host_fault_reset(void);

/**
 * @brief Enable or disable physical flash write/erase constraints.
 *
 * When enabled, writes may only clear bits from 1 to 0 and erase ranges must
 * be sector-aligned.
 *
 * @param enable RT_TRUE to enable physical constraints, RT_FALSE to disable.
 */
void qboot_host_flash_physical_enable(rt_bool_t enable);

/**
 * @brief Configure an optional simulated flash program-unit alignment rule.
 *
 * A zero @p unit disables the rule. When non-zero and physical flash
 * constraints are enabled, write addresses and lengths must both be aligned to
 * @p unit.
 *
 * @param unit Program-unit size in bytes, or zero to disable alignment checks.
 */
void qboot_host_flash_program_unit_set(rt_uint32_t unit);

/**
 * @brief Host custom-backend flash read entry used by direct fake-flash tests.
 *
 * @param addr Absolute flash address.
 * @param buf  Output buffer.
 * @param len  Bytes to read.
 * @return RT_EOK on success, negative error code otherwise.
 */
rt_err_t qbt_custom_flash_read(rt_uint32_t addr, void *buf, rt_uint32_t len);

/**
 * @brief Host custom-backend flash write entry used by direct fake-flash tests.
 *
 * A zero-length write is accepted as a no-op after address validation.
 *
 * @param addr Absolute flash address.
 * @param buf  Input buffer.
 * @param len  Bytes to write.
 * @return RT_EOK on success, negative error code otherwise.
 */
rt_err_t qbt_custom_flash_write(rt_uint32_t addr, const void *buf, rt_uint32_t len);

/**
 * @brief Host custom-backend flash erase entry used by direct fake-flash tests.
 *
 * @param addr Absolute flash address.
 * @param len  Bytes to erase.
 * @return RT_EOK on success, negative error code otherwise.
 */
rt_err_t qbt_custom_flash_erase(rt_uint32_t addr, rt_uint32_t len);

/**
 * @brief Configure one host storage operation fault.
 *
 * @param op         Operation type to fail.
 * @param target     Target range to match.
 * @param after_hits Number of matching successful observations before failing.
 */
void qboot_host_fault_set(qboot_host_fault_op_t op,
                          qboot_host_fault_target_t target,
                          rt_uint32_t after_hits);

/**
 * @brief Configure rt_malloc() failure injection for host tests.
 *
 * @param after_hits Allocations to pass before the next rt_malloc() returns NULL.
 */
void qboot_host_rt_malloc_fail_after(rt_uint32_t after_hits);

/**
 * @brief Test and consume one fault rule by target id.
 *
 * @param op Operation type to test.
 * @param id Target identifier.
 * @return RT_TRUE when the current operation should fail, RT_FALSE otherwise.
 */
rt_bool_t qboot_host_fault_check_id(qboot_host_fault_op_t op, qbt_target_id_t id);

/**
 * @brief Test and consume one fault rule by target selector.
 *
 * @param op     Operation type to test.
 * @param target Target selector to match.
 * @return RT_TRUE when the current operation should fail, RT_FALSE otherwise.
 */
rt_bool_t qboot_host_fault_check_target(qboot_host_fault_op_t op,
                                        qboot_host_fault_target_t target);

/**
 * @brief Load bytes into a storage target and erase the rest of its range.
 *
 * @param id   Target identifier.
 * @param data Input bytes.
 * @param size Number of bytes to write.
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qboot_host_flash_load(qbt_target_id_t id, const rt_uint8_t *data, rt_uint32_t size);

/**
 * @brief Read bytes from a storage target.
 *
 * @param id   Target identifier.
 * @param off  Target offset.
 * @param data Output buffer.
 * @param size Number of bytes to read.
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qboot_host_flash_read_target(qbt_target_id_t id, rt_uint32_t off, rt_uint8_t *data, rt_uint32_t size);

/**
 * @brief Receive package bytes into the DOWNLOAD target in fixed-size chunks.
 *
 * @param data       Package bytes.
 * @param size       Package byte count.
 * @param chunk_size Maximum chunk size per write.
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qboot_host_receive_download(const rt_uint8_t *data, rt_uint32_t size, rt_uint32_t chunk_size);

/**
 * @brief Receive package bytes through the host receive path.
 *
 * @param data       Package bytes.
 * @param size       Package byte count.
 * @param chunk_size Maximum chunk size per write.
 * @param mode       Receive mode: normal, duplicate-first, offset-repeat,
 *                   offset-gap, out-of-order, offset-overlap, or
 *                   same-offset-different-data. Offset policy is exercised
 *                   through the protocol adapter simulation; qboot helper
 *                   does not reject ordering policy by itself.
 * @return RT_TRUE when the receive path accepts all chunks, RT_FALSE otherwise.
 */
rt_bool_t qboot_host_receive_download_mode(const rt_uint8_t *data,
                                           rt_uint32_t size,
                                           rt_uint32_t chunk_size,
                                           const char *mode);

/** @brief Test whether the DOWNLOAD release sign has been written. */
rt_bool_t qboot_host_download_has_release_sign(void);

/** @brief Write a corrupt release sign into DOWNLOAD. */
rt_bool_t qboot_host_download_corrupt_release_sign(void);

/**
 * @brief Corrupt one byte inside a target.
 *
 * @param id  Target identifier.
 * @param off Byte offset to corrupt.
 * @return RT_TRUE on success, RT_FALSE otherwise.
 */
rt_bool_t qboot_host_corrupt_target_byte(qbt_target_id_t id, rt_uint32_t off);


/** @brief Host-side Cortex-M jump preparation trace. */
typedef struct
{
    int disable_irq_count;       /**< Number of simulated interrupt-disable operations. */
    int deinit_count;            /**< Number of simulated peripheral-deinit operations. */
    int systick_clear_count;     /**< Number of simulated SysTick cleanup operations. */
    int nvic_clear_count;        /**< Number of simulated NVIC cleanup operations. */
    int set_control_count;       /**< Number of simulated CONTROL writes. */
    int set_msp_count;           /**< Number of simulated MSP writes. */
    int set_vtor_count;          /**< Number of simulated VTOR writes. */
    int barrier_count;           /**< Number of simulated CPU barrier operations. */
    int fpu_cleanup_count;       /**< Number of simulated FPU cleanup operations. */
    int app_call_count;          /**< Number of simulated APP branch operations. */
    rt_uint32_t msp_value;       /**< Last simulated MSP value. */
    rt_uint32_t reset_vector;    /**< Last simulated reset vector. */
    rt_uint32_t vector_table;    /**< Last simulated vector table address. */
} qboot_host_jump_trace_t;

/** @brief Reset the host Cortex-M jump preparation trace. */
void qboot_host_jump_stub_reset(void);

/**
 * @brief Simulate and validate the Cortex-M jump preparation sequence.
 *
 * @param stack_ptr Candidate APP initial stack pointer.
 * @param reset_vector Candidate APP reset vector.
 * @param vector_table Candidate APP vector table address.
 * @return RT_TRUE when the vector table is accepted and all preparation steps run.
 */
rt_bool_t qboot_host_jump_stub_run(rt_uint32_t stack_ptr,
                                   rt_uint32_t reset_vector,
                                   rt_uint32_t vector_table);

/** @brief Return the recorded host Cortex-M jump preparation trace. */
const qboot_host_jump_trace_t *qboot_host_jump_stub_trace(void);

/** @brief Reset the host jump spy state. */
void qboot_host_jump_reset(void);

/** @brief Return the number of recorded jump calls. */
int qboot_host_jump_count(void);

#ifdef QBOOT_HOST_BACKEND_FAL
/** @brief Reset host FAL partition images. */
void qboot_host_fal_reset(void);
#endif /* QBOOT_HOST_BACKEND_FAL */

#ifdef __cplusplus
}
#endif

#endif /* QBOOT_HOST_FLASH_H */
