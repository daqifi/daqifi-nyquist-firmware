#pragma once

#include <stdint.h>

#ifdef	__cplusplus
extern "C" {
#endif

/**
* Defines the underlying type to use for the lock handle
* TODO: Change this to something specific
*/
typedef uint32_t LockHandle;

/**
* Defines the function prototype for a function that initializes a lock
* @param LockHandle [out] Storage for the lock handle 
*/
typedef void (*LockInitFunctionPtr)(LockHandle*);

/**
* Defines the function prototype for a function that destroys a lock
* @param LockHandle The lock to operate on
*/
typedef void (*LockDestroyFunctionPtr)(const LockHandle*);

/**
* Defines the function prototype for a function that implements the Lock behavior
* NOTE: This should be recursive
* @param LockHandle The lock to operate on
*/
typedef void (*LockFunctionPtr)(const LockHandle*);

/**
* Defines the function prototype for a function that implements the Unlock behavior
* NOTE: This should be recursive
* @param LockHandle The lock to operate on
*/
typedef void (*UnlockFunctionPtr)(const LockHandle*);

/**
 * Implements the lock
 */
typedef struct s_LockProvider
{
    /**
     * A token to pass to the underlying implementations to uniquely identify this lock
     */
    LockHandle Handle;

    /**
     * Called to initialize the lock
     */
    LockInitFunctionPtr LockInitFxn;

    /**
    * Called to destroy a lock
    */
    LockDestroyFunctionPtr LockDestroyFxn;

    /**
     * Called to lock the resource
     */
    LockFunctionPtr LockFxn;

    /**
     * Called to unlock the resource
     */
    UnlockFunctionPtr UnlockFxn;
} LockProvider;

#ifdef	__cplusplus
}
#endif
