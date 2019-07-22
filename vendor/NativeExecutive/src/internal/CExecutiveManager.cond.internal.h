/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.cond.internal.h
*  PURPOSE:     Internal implementation header of conditional variables
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _EXECUTIVE_MANAGER_CONDVAR_INTERNAL_
#define _EXECUTIVE_MANAGER_CONDVAR_INTERNAL_

#include <atomic>

BEGIN_NATIVE_EXECUTIVE

struct condVarNativeEnv;

// Placed inside the condVarNativeEnv::condVarThreadPlugin.
struct perThreadCondVarRegistration
{
    RwListEntry <perThreadCondVarRegistration> node;

    void unwait( CExecutiveManagerNative *nativeMan, condVarNativeEnv *env );
};

struct CCondVarImpl : public CCondVar
{
    CCondVarImpl( CExecutiveManagerNative *execMan );
    ~CCondVarImpl( void );

    void Wait( CReadWriteWriteContextSafe <>& ctxLock );
    void Wait( CSpinLockContext& ctxLock );
    bool WaitTimed( CReadWriteWriteContextSafe <>& ctxLock, unsigned int waitMS );
    bool WaitTimed( CSpinLockContext& ctxLock, unsigned int waitMS );
    size_t Signal( void );
    size_t SignalCount( size_t maxWakeUpCount );

private:
    // Establish a wait-ctx.
    template <typename callbackType>
    AINLINE bool establish_wait_ctx( const callbackType& cb );

public:
    CExecutiveManagerNative *manager;
    CReadWriteLock *lockAtomicCalls;
    RwList <perThreadCondVarRegistration> listWaitingThreads;
};

END_NATIVE_EXECUTIVE

#endif //_EXECUTIVE_MANAGER_CONDVAR_INTERNAL_
