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

// Placed inside the condVarNativeEnv::condVarThreadPlugin.
struct perThreadCondVarRegistration
{
    RwListEntry <perThreadCondVarRegistration> node;
};

struct CCondVarImpl : public CCondVar
{
    CCondVarImpl( CExecutiveManagerNative *execMan );
    ~CCondVarImpl( void );

    void Wait( CReadWriteWriteContextSafe <>& ctxLock );
    void Signal( void );

    CExecutiveManagerNative *manager;
    CReadWriteLock *lockAtomicCalls;
    RwList <perThreadCondVarRegistration> listWaitingThreads;
};

END_NATIVE_EXECUTIVE

#endif //_EXECUTIVE_MANAGER_CONDVAR_INTERNAL_
