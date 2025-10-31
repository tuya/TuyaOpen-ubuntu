/**
* @file tkl_memory.h
* @brief Common process - adapter the semaphore api provide by OS
* @version 0.1
* @date 2020-11-09
*
* @copyright Copyright 2021-2030 Tuya Inc. All Rights Reserved.
*
*/
#ifndef __TKL_MEMORY_H__
#define __TKL_MEMORY_H__

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief Alloc memory of system
*
* @param[in] size: memory size
*
* @note This API is used to alloc memory of system.
*
* @return the memory address malloced
*/
void *tkl_system_malloc(SIZE_T size);

/**
* @brief Free memory of system
*
* @param[in] ptr: memory point
*
* @note This API is used to free memory of system.
*
* @return void
*/
void tkl_system_free(void* ptr);

/**
 * @brief Allocate and clear the memory
 * 
 * @param[in]       nitems      the numbers of memory block
 * @param[in]       size        the size of the memory block
 *
 * @return the memory address calloced
 */
void *tkl_system_calloc(size_t nitems, size_t size);

/**
 * @brief Re-allocate the memory
 * 
 * @param[in]       nitems      source memory address
 * @param[in]       size        the size after re-allocate
 *
 * @return void
 */
void *tkl_system_realloc(void* ptr, size_t size);

/**
* @brief Get system free heap size
*
* @param none
*
* @return heap size
*/
int tkl_system_get_free_heap_size(void);

/**
 * @brief Alloc memory from PSRAM (stub for Ubuntu - uses regular malloc)
 *
 * @param[in] size: memory size
 *
 * @note On Ubuntu, PSRAM doesn't exist, so this is mapped to regular malloc
 *
 * @return the memory address malloced
 */
static inline void *tkl_system_psram_malloc(SIZE_T size)
{
    return tkl_system_malloc(size);
}

/**
 * @brief Free memory from PSRAM (stub for Ubuntu - uses regular free)
 *
 * @param[in] ptr: memory point
 *
 * @note On Ubuntu, PSRAM doesn't exist, so this is mapped to regular free
 *
 * @return void
 */
static inline void tkl_system_psram_free(void* ptr)
{
    tkl_system_free(ptr);
}

/**
 * @brief Get free PSRAM heap size (stub for Ubuntu)
 *
 * @return heap size
 */
static inline int tkl_system_get_psram_free_heap_size(void)
{
    return tkl_system_get_free_heap_size();
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif 

