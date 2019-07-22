/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.cond.h
*  PURPOSE:     Hazard-safe conditional variable implementation
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _EXECUTIVE_MANAGER_CONDITIONAL_
#define _EXECUTIVE_MANAGER_CONDITIONAL_

BEGIN_NATIVE_EXECUTIVE

struct CSpinLockContext;

// Flood-gate style conditional variable.
// It comes with hazard-safety: if thread is asked to terminate then conditional variable will not wait.
struct CCondVar abstract
{
    void Wait( CReadWriteWriteContextSafe <>& ctxLock );
    void Wait( CSpinLockContext& ctxLock );
    // Returns true if the thread has been woken up by signal.
    // If the thread was woken up by time-out but the signal happened anyway then true is returned.
    bool WaitTimed( CReadWriteWriteContextSafe <>& ctxLock, unsigned int waitMS );
    bool WaitTimed( CSpinLockContext& ctxLock, unsigned int waitMS );
    // Wakes up all waiting threads and returns the amount of threads woken up.
    size_t Signal( void );
    // Wakes up at most maxWakeUpCount threads and returns the amount actually woken up.
    size_t SignalCount( size_t maxWakeUpCount );

    CExecutiveManager* GetManager( void );
};

END_NATIVE_EXECUTIVE

#endif //_EXECUTIVE_MANAGER_CONDITIONAL_