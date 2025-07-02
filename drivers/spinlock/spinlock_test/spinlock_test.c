/*
 *  Copyright (C) 2023 Texas Instruments Incorporated - http://www.ti.com/
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/**
 *   \file       spinlock_test.c
 *   \brief      Spinlock app to demonstrate functionality by acquiring and releasing different locks.
**/

/* ========================================================================== */
/*                             Include Files                                  */
/* ========================================================================== */

#include <stdio.h>
#include <stdint.h>
#include <drivers/spinlock.h>
#include <drivers/hw_include/csl_types.h>
#include "ti_drivers_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"

/* ========================================================================== */
/*                                 Macros                                     */
/* ========================================================================== */

#define MAX_NUM_SPINLOCKS       (256U)

/* ========================================================================== */
/*                          Function Declarations                             */
/* ========================================================================== */

void SpinlockApp_main(void);
int32_t SpinlockApp_acquireLockTest(void);
int32_t SpinlockApp_releaseLockTest(void);

/* ========================================================================== */
/*                          Function Definitions                              */
/* ========================================================================== */

int32_t SpinlockApp_releaseLockTest(void)
{
    int32_t  testStatus = CSL_PASS;
    uint32_t prevSpinlockVal = SPINLOCK_LOCK_STATUS_FREE, status = CSL_PASS, lock_num = 0;

    /* Resetting SpinLock module to ensure all locks are free */
    Spinlock_moduleReset(CSL_SPINLOCK0_BASE);

    DebugP_log("\nSpinlock test for freeing the lock\n\r");
    DebugP_log("Case 1: Releasing a taken lock \n\r");

    /* Acquiring lock before releasing it */
    prevSpinlockVal = Spinlock_lock(CSL_SPINLOCK0_BASE, lock_num);

    /* Releasing taken lock */
    Spinlock_unlock(CSL_SPINLOCK0_BASE, lock_num);

    /* To check the current status of the Spinlock, Spinlock_lock is used which does so by trying to acquire the lock */
    /* Checking if lock is free */
    prevSpinlockVal = Spinlock_lock(CSL_SPINLOCK0_BASE, lock_num);

    if (SPINLOCK_LOCK_STATUS_FREE == prevSpinlockVal)
    {
        DebugP_log("Lock released successfully\n\r");
    }
    else if (SPINLOCK_LOCK_STATUS_INUSE == prevSpinlockVal)
    {
        DebugP_log("Lock not released successfully\n\r");
        status = CSL_EFAIL;
    }

    if(CSL_PASS == status)
    {
        lock_num = 1;
        DebugP_log("Case 2: Releasing a free lock\n");

        /* Releasing a free lock */
        Spinlock_unlock(CSL_SPINLOCK0_BASE, lock_num);

        /* Checking if lock is free */
        prevSpinlockVal = Spinlock_lock(CSL_SPINLOCK0_BASE, lock_num);

        if (SPINLOCK_LOCK_STATUS_FREE == prevSpinlockVal)
        {
            DebugP_log("Lock released successfully\n\r");
            DebugP_log("Released locks successfully. Mark test as Pass.\n\r");
        }
        else if (SPINLOCK_LOCK_STATUS_INUSE == prevSpinlockVal)
        {
            DebugP_log("%d",lock_num);
            DebugP_log("Lock not released successfully\n\r");
            DebugP_log("Could not release free lock. Mark test as FAIL.\n\r");
            testStatus = CSL_EFAIL;
        }
    }
    else
    {
        DebugP_log(" Could not release taken lock. Mark test as FAIL.\n\r");
        testStatus = CSL_EFAIL;
    }

    return testStatus;
}

int32_t SpinlockApp_acquireLockTest(void)
{
    int32_t  testStatus = CSL_PASS;
    uint32_t status = 0, lock_num = 0;
    uint32_t numLocAcquired = 0;

    DebugP_log("\n\rOn ");
    DebugP_log("\n\rSpinlock App\n");

    /* Acquiring all available spinlocks */
    for (lock_num = 0; lock_num < MAX_NUM_SPINLOCKS; lock_num++)
    {
        /* Check if lock is free and acquire the lock */
        status = Spinlock_lock(CSL_SPINLOCK0_BASE, lock_num);

        if (SPINLOCK_LOCK_STATUS_FREE == status)
        {
            DebugP_log("\n\r");
            DebugP_log(" acquired lock number:");
            DebugP_log("%d",lock_num);
            numLocAcquired++;
        }
        else if (SPINLOCK_LOCK_STATUS_INUSE == status)
        {
            DebugP_log("\n\r");
            DebugP_log(" could not acquire lock number:");
            DebugP_log("%d",lock_num);
            DebugP_log(", lock is not free");
        }
    }
    DebugP_log("\n\r");

    if (numLocAcquired == MAX_NUM_SPINLOCKS)
    {
        DebugP_log("Acquired all available locks. Mark test as Pass.\n\r");
        DebugP_log("This testcase requires board power cycle.\n\r");
    }
    else
    {
        DebugP_log("Could not acquire all available locks. Mark test as FAIL.\n\r");
        DebugP_log("This testcase requires board power cycle.\n\r");
        testStatus = CSL_EFAIL;
    }

    return testStatus;
}

void SpinlockApp_main(void)
{
    int32_t  testStatus = CSL_PASS;

    /* Open drivers to open the UART driver for console */
    Drivers_open();
    Board_driversOpen();

    /* Test to acquire all available Spinlocks */
    testStatus += SpinlockApp_acquireLockTest();
    /* Test to release locks (free and taken) */
    testStatus += SpinlockApp_releaseLockTest();

    if(CSL_PASS == testStatus)
    {
        DebugP_log("\nAll tests have passed..\n\r");
    }
    else{
        DebugP_log("\nTest Failed..\n\r");
    }

    Board_driversClose();
    Drivers_close();

    return;
}

