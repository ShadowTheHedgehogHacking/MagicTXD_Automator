/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.thread.internal.h
*  PURPOSE:     Internal implementation of threads
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _EXECUTIVE_MANAGER_THREADS_INTERNAL_
#define _EXECUTIVE_MANAGER_THREADS_INTERNAL_

#include <atomic>

#include "CExecutiveManager.unfairmtx.internal.h"

#define THREAD_PLUGIN_NATIVE        0x00000000      // plugin id for OS implementation

BEGIN_NATIVE_EXECUTIVE

class CExecutiveManagerNative;

struct CExecThreadImpl : public CExecThread
{
    CExecThreadImpl( CExecutiveManagerNative *manager, bool isRemoteThread, void *userdata, size_t stackSize, threadEntryPoint_t entryPoint );
    ~CExecThreadImpl( void );

    // Must be performed on the current thread!
    // Requirement: READ ACCESS to lockThreadStatus.
    void CheckTerminationRequest( void );

    // Requirement: READ ACCESS to lockThreadStatus.
    eThreadStatus GetStatusNative( void ) const;

    CExecutiveManagerNative *manager;

    // These parameters are only valid if this thread is not a remote thread!
    threadEntryPoint_t entryPoint;
    void *userdata;
    size_t stackSize;

    CUnfairMutexImpl mtxThreadStatus;   // take this lock if you want to prevent the thread from switching state

    bool isRemoteThread;        // true if NativeExecutive does not control the underlying OS thread.

    // CExecThread is a user-mode handle to an OS thread, which is a shared resource.
    // In this situation, where usage of the handle is unpredictable, reference counting is required.
    std::atomic <unsigned long> refCount;

    // As long as a thread is running/active it holds a runtime reference.
    // That runtime reference MUST BE CLEARED atomically with the thread
    // switching to terminated state.

    RwListEntry <CExecThreadImpl> managerNode;
};

END_NATIVE_EXECUTIVE

#endif //_EXECUTIVE_MANAGER_THREADS_INTERNAL_
