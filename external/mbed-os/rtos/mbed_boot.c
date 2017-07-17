/* mbed Microcontroller Library
 * Copyright (c) 2006-2016 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* mbed OS boot sequence
 *
 * Most of mbed supported targets use default ARM Cortex M boot approach, where the core starts executing reset vector
 * after power up. Reset ISR is defined for each target by the vendor (basing on CMSIS template). Reset vector is
 * responsible for low level platform init and then calling in libc (__main). Depending on compiler and version of C
 * library, predefined function will be called which is implemented by mbed OS.
 *
 * There's number of functions, vendor and users can provide to setup the platform and/or inject a code to be executed
 *  before main():
 *      * Reset vector and SystemInit: Reset vector should do low level core and board initialization.
 *      * mbed_sdk_init: Higher level board init and making sure the board is ready for the mbed OS.
 *      * mbed_main: User's code to be executed before main().
 *      * main: Standard application code.
 *
 * Detailed boot procedures:
 *
 * For ARMCC:
 * ==========
 *
 * Reset (TARGET)
 *     -> SystemInit (TARGET)
 *     -> __main (LIBC)
 *         -> __rt_entry (MBED: rtos/mbed_boot.c)
 *             -> __user_setup_stackheap (LIBC)
 *             -> mbed_set_stack_heap (MBED: rtos/mbed_boot.c)
 *             -> mbed_cpy_nvic (MBED: rtos/mbed_boot.c)
 *             -> mbed_sdk_init (TARGET)
 *             -> _platform_post_stackheap_init (RTX)
 *                 -> osKernelInitialize (RTX)
 *             -> mbed_start_main (MBED: rtos/mbed_boot.c)
 *                 -> osThreadNew (RTX)
 *                     -> pre_main(MBED: rtos/mbed_boot.c)
 *                         -> __rt_lib_init (LIBC)
 *                         -> $Sub$$main (MBED: rtos/mbed_boot.c)
 *                             -> mbed_main (MBED: rtos/mbed_boot.c)
 *                             -> main (APP)
 *                 -> osKernelStart (RTX)
 *
 * In addition to the above, libc will use functions defined by RTX: __user_perthread_libspace, _mutex_initialize,
 * _mutex_acquire, _mutex_release, _mutex_free for details consult: ARM C and C++ Libraries and Floating-Point
 * Support User Guide.
 *
 * For MICROLIB:
 * ==========
 *
 * Reset (TARGET)
 *     -> SystemInit (TARGET)
 *     -> __main (LIBC)
 *         -> _main_init (MBED: rtos/mbed_boot.c)
 *             -> mbed_set_stack_heap (MBED: rtos/mbed_boot.c)
 *             -> mbed_cpy_nvic (MBED: rtos/mbed_boot.c)
 *             -> mbed_sdk_init (TARGET)
 *             -> osKernelInitialize (RTX)
 *             -> mbed_start_main (MBED: rtos/mbed_boot.c)
 *                 -> osThreadNew (RTX)
 *                     -> pre_main(MBED: rtos/mbed_boot.c)
 *                         -> __cpp_initialize__aeabi_ (LIBC)
 *                         -> $Sub$$main (MBED: rtos/mbed_boot.c)
 *                             -> mbed_main (MBED: rtos/mbed_boot.c)
 *                             -> main (APP)
 *                 -> osKernelStart (RTX)
 *
 * For GCC:
 * ========
 *
 * Reset (TARGET)
 *     -> SystemInit (TARGET)
 *     -> __main (LIBC)
 *         -> software_init_hook (MBED: rtos/mbed_boot.c)
 *             -> mbed_set_stack_heap (MBED: rtos/mbed_boot.c)
 *             -> mbed_cpy_nvic (MBED: rtos/mbed_boot.c)
 *             -> mbed_sdk_init (TARGET)
 *             -> osKernelInitialize (RTX)
 *             -> mbed_start_main (MBED: rtos/mbed_boot.c)
 *                 -> osThreadNew (RTX)
 *                     -> pre_main(MBED: rtos/mbed_boot.c)
 *                     -> __libc_init_array (LIBC)
 *                         -> __wrap_main (MBED: rtos/mbed_boot.c)
 *                             -> mbed_main (MBED: rtos/mbed_boot.c)
 *                             -> __real_main (APP)
 *                 -> osKernelStart (RTX)
 *
 * For IAR:
 * ========
 *
 * Reset (TARGET)
 *     -> SystemInit (TARGET)
 *     -> __iar_program_start
 *         -> __iar_init_core
 *         -> __iar_init_core
 *         -> __iar_init_vfp
 *         -> __low_level_init
 *         -> __iar_data_init3
 *         -> mbed_cpy_nvic (MBED: rtos/mbed_boot.c)
 *         -> mbed_sdk_init (TARGET)
 *         -> mbed_set_stack_heap (MBED: rtos/mbed_boot.c)
 *         -> osKernelInitialize (RTX)
 *         -> mbed_start_main (MBED: rtos/mbed_boot.c)
 *             -> osThreadNew (RTX)
 *             -> pre_main(MBED: rtos/mbed_boot.c)
 *                 -> __iar_dynamic_initialization
*                  -> main
 *             -> osKernelStart (RTX)
 *
 * Other notes:
 *
 *    * In addition to the above, libc will use functions defined in mbed_boot.c: __rtos_malloc_lock/unlock,
 *      __rtos_env_lock/unlock.
 *
 *    * First step after the execution is passed to mbed, software_init_hook for GCC and __rt_entry for ARMC is to
 *      initialize heap.
 *
 * Memory layout notes:
 * ====================
 *
 * IAR Default Memory layout notes:
 * -Heap defined by "HEAP" region in .icf file
 * -Interrupt stack defined by "CSTACK" region in .icf file
 * -Value INITIAL_SP is ignored
 *
 * IAR Custom Memory layout notes:
 * -There is no custom layout available for IAR - everything must be defined in
 *      the .icf file and use the default layout
 *
 *
 * GCC Default Memory layout notes:
 * -Block of memory from symbol __end__ to define INITIAL_SP used to setup interrupt
 *      stack and heap in the function set_stack_heap()
 * -ISR_STACK_SIZE can be overridden to be larger or smaller
 *
 * GCC Custom Memory layout notes:
 * -Heap can be explicitly placed by defining both HEAP_START and HEAP_SIZE
 * -Interrupt stack can be explicitly placed by defining both ISR_STACK_START and ISR_STACK_SIZE
 *
 *
 * ARM Memory layout
 * -Block of memory from end of region "RW_IRAM1" to define INITIAL_SP used to setup interrupt
 *      stack and heap in the function set_stack_heap()
 * -ISR_STACK_SIZE can be overridden to be larger or smaller
 *
 * ARM Custom Memory layout notes:
 * -Heap can be explicitly placed by defining both HEAP_START and HEAP_SIZE
 * -Interrupt stack can be explicitly placed by defining both ISR_STACK_START and ISR_STACK_SIZE
 *
 */

#include "cmsis.h"
#include "mbed_rtx.h"
#include "mbed_rtos_storage.h"
#include "cmsis_os2.h"
#include "mbed_toolchain.h"
#include "mbed_error.h"

/* Heap limits - only used if set */
extern unsigned char *mbed_heap_start;
extern uint32_t mbed_heap_size;

unsigned char *mbed_stack_isr_start = 0;
uint32_t mbed_stack_isr_size = 0;

WEAK void mbed_main(void);
void pre_main (void);

osThreadAttr_t _main_thread_attr;

/** The main thread's stack size can be configured by the application, if not explicitly specified it'll default to 4K */
#ifndef MBED_CONF_APP_MAIN_STACK_SIZE
#define MBED_CONF_APP_MAIN_STACK_SIZE 4096
#endif
MBED_ALIGN(8) char _main_stack[MBED_CONF_APP_MAIN_STACK_SIZE];
mbed_rtos_storage_thread_t _main_obj;

osMutexId_t               singleton_mutex_id;
mbed_rtos_storage_mutex_t singleton_mutex_obj;
osMutexAttr_t             singleton_mutex_attr;

/*
 * Sanity check values
 */
#if defined(__ICCARM__) &&                                  \
    (defined(HEAP_START) || defined(HEAP_SIZE) ||           \
     defined(ISR_STACK_START) && defined(ISR_STACK_SIZE))
    #error "No custom layout allowed for IAR. Use .icf file instead"
#endif
#if defined(HEAP_START) && !defined(HEAP_SIZE)
    #error "HEAP_SIZE must be defined if HEAP_START is defined"
#endif
#if defined(ISR_STACK_START) && !defined(ISR_STACK_SIZE)
    #error "ISR_STACK_SIZE must be defined if ISR_STACK_START is defined"
#endif
#if defined(HEAP_SIZE) && !defined(HEAP_START)
    #error "HEAP_START must be defined if HEAP_SIZE is defined"
#endif

/* IAR - INITIAL_SP and HEAP_START ignored as described in Memory layout notes above
 */
#if !defined(__ICCARM__) && !defined(INITIAL_SP) && !defined(HEAP_START)
    #error "no target defined"
#endif

/* Interrupt stack and heap always defined for IAR
 * Main thread defined here
 */
#if defined(__ICCARM__)
    #pragma section="CSTACK"
    #pragma section="HEAP"
    #define HEAP_START          ((unsigned char*)__section_begin("HEAP"))
    #define HEAP_SIZE           ((uint32_t)__section_size("HEAP"))
    #define ISR_STACK_START     ((unsigned char*)__section_begin("CSTACK"))
    #define ISR_STACK_SIZE      ((uint32_t)__section_size("CSTACK"))
#endif

/* Define heap region if it has not been defined already */
#if !defined(HEAP_START)
    #if defined(__ICCARM__)
        #error "Heap should already be defined for IAR"
    #elif defined(__CC_ARM)
        extern uint32_t          Image$$RW_IRAM1$$ZI$$Limit[];
        #define HEAP_START      ((unsigned char*)Image$$RW_IRAM1$$ZI$$Limit)
        #define HEAP_SIZE       ((uint32_t)((uint32_t)INITIAL_SP - (uint32_t)HEAP_START))
    #elif defined(__GNUC__)
        extern uint32_t         __end__[];
        #define HEAP_START      ((unsigned char*)__end__)
        #define HEAP_SIZE       ((uint32_t)((uint32_t)INITIAL_SP - (uint32_t)HEAP_START))
    #endif
#endif

/* Define stack sizes if they haven't been set already */
#if !defined(ISR_STACK_SIZE)
    #define ISR_STACK_SIZE ((uint32_t)1024)
#endif

/*
 * mbed_set_stack_heap purpose is to set the following variables:
 * -mbed_heap_start
 * -mbed_heap_size
 * -mbed_stack_isr_start
 * -mbed_stack_isr_size
 */
void mbed_set_stack_heap(void) {

    unsigned char *free_start = HEAP_START;
    uint32_t free_size = HEAP_SIZE;

#ifdef ISR_STACK_START
    /* Interrupt stack explicitly specified */
    mbed_stack_isr_size = ISR_STACK_SIZE;
    mbed_stack_isr_start = ISR_STACK_START;
#else
    /* Interrupt stack -  reserve space at the end of the free block */
    mbed_stack_isr_size = ISR_STACK_SIZE < free_size ? ISR_STACK_SIZE : free_size;
    mbed_stack_isr_start = free_start + free_size - mbed_stack_isr_size;
    free_size -= mbed_stack_isr_size;
#endif

    /* Heap - everything else */
    mbed_heap_size = free_size;
    mbed_heap_start = free_start;
}

static void mbed_cpy_nvic(void)
{
    /* If vector address in RAM is defined, copy and switch to dynamic vectors. Exceptions for M0 which doesn't have
    VTOR register and for A9 for which CMSIS doesn't define NVIC_SetVector; in both cases target code is
    responsible for correctly handling the vectors.
    */
#if !defined(__CORTEX_M0) && !defined(__CORTEX_A9)
#ifdef NVIC_RAM_VECTOR_ADDRESS
    uint32_t *old_vectors = (uint32_t *)SCB->VTOR;
    uint32_t *vectors = (uint32_t*)NVIC_RAM_VECTOR_ADDRESS;
    for (int i = 0; i < NVIC_NUM_VECTORS; i++) {
        vectors[i] = old_vectors[i];
    }
    SCB->VTOR = (uint32_t)NVIC_RAM_VECTOR_ADDRESS;
#endif /* NVIC_RAM_VECTOR_ADDRESS */
#endif /* !defined(__CORTEX_M0) && !defined(__CORTEX_A9) */
}

/* mbed_main is a function that is called before main()
 * mbed_sdk_init() is also a function that is called before main(), but unlike
 * mbed_main(), it is not meant for user code, but for the SDK itself to perform
 * initializations before main() is called.
 */
WEAK void mbed_main(void) {

}

/* This function can be implemented by the target to perform higher level target initialization, before the mbed OS or
 * RTX is started.
 */
void mbed_sdk_init(void);
WEAK void mbed_sdk_init(void) {
}

void mbed_start_main(void)
{
    _main_thread_attr.stack_mem = _main_stack;
    _main_thread_attr.stack_size = sizeof(_main_stack);
    _main_thread_attr.cb_size = sizeof(_main_obj);
    _main_thread_attr.cb_mem = &_main_obj;
    _main_thread_attr.priority = osPriorityNormal;
    _main_thread_attr.name = "main_thread";
    osThreadId_t result = osThreadNew((osThreadFunc_t)pre_main, NULL, &_main_thread_attr);
    if ((void *)result == NULL) {
        error("Pre main thread not created");
    }

    osKernelStart();
}

/******************** Toolchain specific code ********************/

#if defined (__CC_ARM)

/* Common for both ARMC and MICROLIB */
int $Super$$main(void);
int $Sub$$main(void) {
    mbed_main();
    return $Super$$main();
}

#if defined (__MICROLIB)  /******************** MICROLIB ********************/

int main(void);
void _main_init (void) __attribute__((section(".ARM.Collect$$$$000000FF")));
void $Super$$__cpp_initialize__aeabi_(void);

void _main_init (void) {
    mbed_set_stack_heap();
    /* Copy the vector table to RAM only if uVisor is not in use. */
#if !(defined(FEATURE_UVISOR) && defined(TARGET_UVISOR_SUPPORTED))
    mbed_cpy_nvic();
#endif
    mbed_sdk_init();
    osKernelInitialize();
    mbed_start_main();
    for (;;);
}

void $Sub$$__cpp_initialize__aeabi_(void)
{
    /* This should invoke C++ initializers prior _main_init, we keep this empty and
     * invoke them after _main_init, when the RTX is already initilized.
     */
}

void pre_main()
{
    singleton_mutex_attr.name = "singleton_mutex";
    singleton_mutex_attr.attr_bits = osMutexRecursive | osMutexPrioInherit | osMutexRobust;
    singleton_mutex_attr.cb_size = sizeof(singleton_mutex_obj);
    singleton_mutex_attr.cb_mem = &singleton_mutex_obj;
    singleton_mutex_id = osMutexNew(&singleton_mutex_attr);

    $Super$$__cpp_initialize__aeabi_();
    main();
}

#else /******************** ARMC ********************/

#include <rt_misc.h>
extern __value_in_regs struct __argc_argv __rt_lib_init(unsigned heapbase, unsigned heaptop);
extern __value_in_regs struct __initial_stackheap __user_setup_stackheap(void);
extern void _platform_post_stackheap_init (void);
extern int main(int argc, char* argv[]);

void pre_main (void)
{
    singleton_mutex_attr.name = "singleton_mutex";
    singleton_mutex_attr.attr_bits = osMutexRecursive | osMutexPrioInherit | osMutexRobust;
    singleton_mutex_attr.cb_size = sizeof(singleton_mutex_obj);
    singleton_mutex_attr.cb_mem = &singleton_mutex_obj;
    singleton_mutex_id = osMutexNew(&singleton_mutex_attr);

    __rt_lib_init((unsigned)mbed_heap_start, (unsigned)(mbed_heap_start + mbed_heap_size));

    main(0, NULL);
}

/* The single memory model is checking for stack collision at run time, verifing
   that the heap pointer is underneath the stack pointer.
   With the RTOS there is not only one stack above the heap, there are multiple
   stacks and some of them are underneath the heap pointer.
*/
#pragma import(__use_two_region_memory)

/* Called by the C library */
void __rt_entry (void) {
    __user_setup_stackheap();
    mbed_set_stack_heap();
    /* Copy the vector table to RAM only if uVisor is not in use. */
#if !(defined(FEATURE_UVISOR) && defined(TARGET_UVISOR_SUPPORTED))
    mbed_cpy_nvic();
#endif
    mbed_sdk_init();
    _platform_post_stackheap_init();
    mbed_start_main();
}

#endif /* ARMC */
#elif defined (__GNUC__) /******************** GCC ********************/
#include <stdio.h>
#include <stdlib.h>

extern int main(int argc, char* argv[]);
extern void __libc_init_array (void);
extern int __real_main(void);


struct __lock {
    osMutexId_t               id;
    mbed_rtos_storage_mutex_t obj;
    osMutexAttr_t             attr;
};

struct __lock   __lock___sinit_recursive_mutex;
struct __lock   __lock___sfp_recursive_mutex;
struct __lock   __lock___at_quick_exit_mutex;
struct __lock   __lock___malloc_recursive_mutex;
struct __lock   __lock___env_recursive_mutex;
struct __lock   __lock___tz_mutex;
struct __lock   __lock___arc4random_mutex;

static int      main_running;

/* newlib reentrancy data. */
static struct _reent      os_libspace[OS_THREAD_LIBSPACE_NUM] \
__attribute__((section(".bss.os")));

static osThreadId_t       os_libspace_id[OS_THREAD_LIBSPACE_NUM] \
__attribute__((section(".bss.os")));

#ifdef  FEATURE_UVISOR
#include "uvisor-lib/uvisor-lib.h"
#endif/* FEATURE_UVISOR */

static void init_lock(struct __lock* pLock, const char* pName);
static void init_recursive_lock(struct __lock* pLock, const char* pName);


int __wrap_main(void) {
    mbed_main();
    return __real_main();
}

void pre_main(void)
{
    main_running = 1;

    singleton_mutex_attr.name = "singleton_mutex";
    singleton_mutex_attr.attr_bits = osMutexRecursive | osMutexPrioInherit | osMutexRobust;
    singleton_mutex_attr.cb_size = sizeof(singleton_mutex_obj);
    singleton_mutex_attr.cb_mem = &singleton_mutex_obj;
    singleton_mutex_id = osMutexNew(&singleton_mutex_attr);

    init_recursive_lock(&__lock___sinit_recursive_mutex, "sinit_mutex");
    init_recursive_lock(&__lock___sfp_recursive_mutex, "sfp_mutex");
    init_recursive_lock(&__lock___malloc_recursive_mutex, "malloc_mutex");
    init_recursive_lock(&__lock___env_recursive_mutex, "env_mutex");
    init_lock(&__lock___at_quick_exit_mutex, "at_quick_exit_mutex");
    init_lock(&__lock___tz_mutex, "tz_mutex");
    init_lock(&__lock___arc4random_mutex, "arc4random_mutex");

    __libc_init_array();

    main(0, NULL);
}

void software_init_hook(void)
{
    mbed_set_stack_heap();
    /* Copy the vector table to RAM only if uVisor is not in use. */
#if !(defined(FEATURE_UVISOR) && defined(TARGET_UVISOR_SUPPORTED))
    mbed_cpy_nvic();
#endif
    mbed_sdk_init();
    osKernelInitialize();
    /* uvisor_lib_init calls RTOS functions, so must be called after the RTOS has
     * been initialized. */
#ifdef   FEATURE_UVISOR
    int return_code;

    return_code = uvisor_lib_init();
    if (return_code) {
        mbed_die();
    }
#endif/* FEATURE_UVISOR */
    mbed_start_main();
}

static void init_lock_common(struct __lock* pLock, const char* pName, uint32_t attributeBits)
{
    pLock->attr.name = pName;
    pLock->attr.attr_bits = attributeBits;
    pLock->attr.cb_size = sizeof(pLock->obj);
    pLock->attr.cb_mem = &pLock->obj;
    pLock->id = osMutexNew(&pLock->attr);
}

static void init_lock(struct __lock* pLock, const char* pName)
{
    init_lock_common(pLock, pName, osMutexPrioInherit | osMutexRobust);
}

static void init_recursive_lock(struct __lock* pLock, const char* pName)
{
    init_lock_common(pLock, pName, osMutexRecursive | osMutexPrioInherit | osMutexRobust);
}

void __retarget_lock_init(struct __lock** lock)
{
    *lock = (struct __lock*)malloc(sizeof(**lock));
    if (*lock) {
        init_lock(*lock, "newlib_dynamic_mutex");
    }
    else {
        error("newlib mutex init is out of memory\r\n");
    }
}

void __retarget_lock_init_recursive(struct __lock **lock)
{
    *lock = (struct __lock*)malloc(sizeof(**lock));
    if (*lock) {
        init_recursive_lock(*lock, "newlib_dynamic_recursive_mutex");
    }
    else {
        error("newlib mutex init is out of memory\r\n");
    }
}

void __retarget_lock_close(struct __lock* lock)
{
    if (lock) {
        osMutexDelete(lock->id);
        free(lock);
    }
}

void __retarget_lock_close_recursive(struct __lock* lock)
{
    if (lock) {
        osMutexDelete(lock->id);
        free(lock);
    }
}

void __retarget_lock_acquire(struct __lock* lock)
{
    osMutexAcquire(lock->id, osWaitForever);
}

void __retarget_lock_acquire_recursive(struct __lock* lock)
{
    osMutexAcquire(lock->id, osWaitForever);
}

int __retarget_lock_try_acquire(struct __lock* lock)
{
    return osMutexAcquire(lock->id, 0);
}

int __retarget_lock_try_acquire_recursive(struct __lock* lock)
{
    return osMutexAcquire(lock->id, 0);
}

void __retarget_lock_release(struct __lock* lock)
{
    osMutexRelease(lock->id);
}

void __retarget_lock_release_recursive(struct __lock* lock)
{
    osMutexRelease(lock->id);
}


/* The full newlib library provides versions of these locking routines which
   call the above __retarget_lock*() functions but newlib-nano doesn't. The
   following routines are taken from the full newlib sources. */
#ifdef _REENT_SMALL

void __env_lock(struct _reent *ptr)
{
    __retarget_lock_acquire_recursive(&__lock___env_recursive_mutex);
}

void __env_unlock(struct _reent *ptr)
{
    __retarget_lock_release_recursive(&__lock___env_recursive_mutex);
}

void __sfp_lock_acquire(void)
{
    __retarget_lock_acquire_recursive(&__lock___sfp_recursive_mutex);
}

void __sfp_lock_release(void)
{
    __retarget_lock_release_recursive(&__lock___sfp_recursive_mutex);
}

void __sinit_lock_acquire(void)
{
    __retarget_lock_acquire_recursive(&__lock___sinit_recursive_mutex);
}

void __sinit_lock_release(void)
{
    __retarget_lock_release_recursive(&__lock___sinit_recursive_mutex);
}

void __malloc_lock(struct _reent *ptr)
{
    __retarget_lock_acquire_recursive(&__lock___malloc_recursive_mutex);
}

void __malloc_unlock(struct _reent *ptr)
{
    __retarget_lock_release_recursive(&__lock___malloc_recursive_mutex);
}

void __tz_lock(void)
{
    __retarget_lock_acquire(&__lock___tz_mutex);
}

void __tz_unlock(void)
{
    __retarget_lock_release(&__lock___tz_mutex);
}

/* There are some newlib-nano routines which don't call the required locking
   routines at all so wrap them with the necessary locks. */
uint32_t __real_arc4random(void);
uint32_t __wrap_arc4random(void)
{
    uint32_t val;

    __retarget_lock_acquire(&__lock___arc4random_mutex);
        val = __real_arc4random();
    __retarget_lock_release(&__lock___arc4random_mutex);

    return val;
}

void __real_arc4random_buf(void *buf, size_t n);
void __wrap_arc4random_buf(void *buf, size_t n)
{
    __retarget_lock_acquire(&__lock___arc4random_mutex);
        __real_arc4random_buf(buf, n);
    __retarget_lock_release(&__lock___arc4random_mutex);
}

FILE * __real___sfp(struct _reent *d);
FILE * __wrap___sfp(struct _reent *d)
{
    FILE* pFile;
    
    __sfp_lock_acquire();
        pFile = __real___sfp(d);
    __sfp_lock_release();

    return pFile;
}

void __real___sinit(struct _reent *s);
void __wrap___sinit(struct _reent *s)
{
    __sinit_lock_acquire();
        __real___sinit(s);
    __sinit_lock_release();
}

int __real_at_quick_exit(void (*func)(void));
int __wrap_at_quick_exit(void (*func)(void))
{
    int ret;

    __retarget_lock_acquire(&__lock___at_quick_exit_mutex);
        ret = __real_at_quick_exit(func);
    __retarget_lock_release(&__lock___at_quick_exit_mutex);

    return ret;
}


#endif /* _REENT_SMALL */


/* Return thread specific reentrant data for newlib. newlib starts out using
   the _global_impure_ptr instance that it statically allocates and this code 
   re-uses that instance for the main thread. */
struct _reent* __user_perthread_libspace(osThreadId_t id)
{
    uint32_t     n;

    if (!main_running) {
        return _global_impure_ptr;
    }
    
    if (id == (osThreadId_t)&_main_obj) {
        return _global_impure_ptr;
    }
    
    for (n = 0U; n < OS_THREAD_LIBSPACE_NUM; n++) {
        if (os_libspace_id[n] == NULL) {
            os_libspace_id[n] = id;
            os_libspace[n] = (struct _reent)_REENT_INIT(os_libspace[n]);
            return &os_libspace[n];
        }
        if (os_libspace_id[n] == id) {
            return &os_libspace[n];
        }
    }
    
    if (n == OS_THREAD_LIBSPACE_NUM) {
        osRtxErrorNotify(osRtxErrorClibSpace, id);
    }
    
    return _global_impure_ptr;
}

#endif

#if defined(TOOLCHAIN_IAR) /******************** IAR ********************/

extern void* __vector_table;
extern int  __low_level_init(void);
extern void __iar_data_init3(void);
extern __weak void __iar_init_core( void );
extern __weak void __iar_init_vfp( void );
extern void __iar_dynamic_initialization(void);
extern void mbed_sdk_init(void);
extern int main(void);
extern void exit(int arg);

static uint8_t low_level_init_needed;

void pre_main(void)
{
    singleton_mutex_attr.name = "singleton_mutex";
    singleton_mutex_attr.attr_bits = osMutexRecursive | osMutexPrioInherit | osMutexRobust;
    singleton_mutex_attr.cb_size = sizeof(singleton_mutex_obj);
    singleton_mutex_attr.cb_mem = &singleton_mutex_obj;
    singleton_mutex_id = osMutexNew(&singleton_mutex_attr);

    if (low_level_init_needed) {
        __iar_dynamic_initialization();
    }
    mbed_main();
    main();
}

#pragma required=__vector_table
void __iar_program_start( void )
{
    __iar_init_core();
    __iar_init_vfp();

    uint8_t low_level_init_needed_local;

    low_level_init_needed_local = __low_level_init();
    if (low_level_init_needed_local) {
        __iar_data_init3();

    /* Copy the vector table to RAM only if uVisor is not in use. */
#if !(defined(FEATURE_UVISOR) && defined(TARGET_UVISOR_SUPPORTED))
    mbed_cpy_nvic();
#endif
    mbed_sdk_init();
  }

  mbed_set_stack_heap();

  /* Store in a global variable after RAM has been initialized */
  low_level_init_needed = low_level_init_needed_local;

  osKernelInitialize();

  mbed_start_main();
}

/* Thread safety */
static osMutexId_t               std_mutex_id_sys[_MAX_LOCK] = {0};
static mbed_rtos_storage_mutex_t std_mutex_sys[_MAX_LOCK] = {0};
#define _FOPEN_MAX 10
static osMutexId_t               std_mutex_id_file[_FOPEN_MAX] = {0};
static mbed_rtos_storage_mutex_t std_mutex_file[_FOPEN_MAX] = {0};

void __iar_system_Mtxinit(__iar_Rmtx *mutex) /* Initialize a system lock */
{
    osMutexAttr_t attr;
    uint32_t index;
    for (index = 0; index < _MAX_LOCK; index++) {
        if (0 == std_mutex_id_sys[index]) {
            attr.name = "system_mutex";
            attr.cb_mem = &std_mutex_sys[index];
            attr.cb_size = sizeof(std_mutex_sys[index]);
            attr.attr_bits = osMutexRecursive | osMutexPrioInherit | osMutexRobust;
            std_mutex_id_sys[index] = osMutexNew(&attr);
            *mutex = (__iar_Rmtx*)&std_mutex_id_sys[index];
            return;
        }
    }

    /* This should never happen */
    error("Not enough mutexes\n");
}

void __iar_system_Mtxdst(__iar_Rmtx *mutex) /* Destroy a system lock */
{
    osMutexDelete(*(osMutexId_t*)*mutex);
    *mutex = 0;
}

void __iar_system_Mtxlock(__iar_Rmtx *mutex) /* Lock a system lock */
{
    osMutexAcquire(*(osMutexId_t*)*mutex, osWaitForever);
}

void __iar_system_Mtxunlock(__iar_Rmtx *mutex) /* Unlock a system lock */
{
    osMutexRelease(*(osMutexId_t*)*mutex);
}

void __iar_file_Mtxinit(__iar_Rmtx *mutex) /* Initialize a file lock */
{
    osMutexAttr_t attr;
    uint32_t index;
    for (index = 0; index < _FOPEN_MAX; index++) {
        if (0 == std_mutex_id_file[index]) {
            attr.name = "file_mutex";
            attr.cb_mem = &std_mutex_file[index];
            attr.cb_size = sizeof(std_mutex_file[index]);
            attr.attr_bits = osMutexRecursive | osMutexPrioInherit | osMutexRobust;
            std_mutex_id_file[index] = osMutexNew(&attr);
            *mutex = (__iar_Rmtx*)&std_mutex_id_file[index];
            return;
        }
    }
    /* The variable _FOPEN_MAX needs to be increased */
    error("Not enough mutexes\n");
}

void __iar_file_Mtxdst(__iar_Rmtx *mutex) /* Destroy a file lock */
{
    osMutexDelete(*(osMutexId_t*)*mutex);
    *mutex = 0;
}

void __iar_file_Mtxlock(__iar_Rmtx *mutex) /* Lock a file lock */
{
    osMutexAcquire(*(osMutexId_t*)*mutex, osWaitForever);
}

void __iar_file_Mtxunlock(__iar_Rmtx *mutex) /* Unlock a file lock */
{
    osMutexRelease(*(osMutexId_t*)*mutex);
}

#endif
