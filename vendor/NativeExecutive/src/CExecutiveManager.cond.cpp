/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.cond.cpp
*  PURPOSE:     Hazard-safe conditional variable implementation
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include "StdInc.h"

#include "CExecutiveManager.cond.h"

#include "CExecutiveManager.cond.hxx"
#include "CExecutiveManager.evtwait.hxx"

BEGIN_NATIVE_EXECUTIVE

optional_struct_space <condNativeEnvRegister_t> condNativeEnvRegister;

void condVarNativeEnv::condVarThreadPlugin::Initialize( CExecThreadImpl *thread )
{
    //CExecutiveManagerNative *manager = thread->manager;

    // We initially do not wait on any condition variable.
    this->waitingOnVar = nullptr;
}

void condVarNativeEnv::condVarThreadPlugin::Shutdown( CExecThreadImpl *thread )
{
    //CExecutiveManagerNative *manager = thread->manager;

    // Make sure we are not waiting on any condVar anymore.
    // This is guaranteed by the thread logic and hazard management system.
    assert( this->waitingOnVar == nullptr );
}

void condVarNativeEnv::condVarThreadPlugin::TerminateHazard( void )
{
    // If we get here then the hazard it correctly initialized.
    // This is made secure because we take the threadState lock in the Wait method.

    // Wake the thread.
    // The mechanism of the conditional variable will make sure he cannot get into
    // waiting state again.
    CCondVarImpl *waitingOnVar = this->waitingOnVar;

    assert( waitingOnVar != nullptr );

    waitingOnVar->Signal();
}

CCondVarImpl::CCondVarImpl( CExecutiveManagerNative *manager )
{
    this->manager = manager;

    this->lockAtomicCalls = manager->CreateReadWriteLock();

    assert( this->lockAtomicCalls != nullptr );
}

CCondVarImpl::~CCondVarImpl( void )
{
    // TODO: wake up all threads that could be waiting at this conditional variable.

    manager->CloseReadWriteLock( this->lockAtomicCalls );
}

void CCondVarImpl::Wait( CReadWriteWriteContextSafe <>& ctxLock )
{
    CExecutiveManagerNative *nativeMan = this->manager;

    condVarNativeEnv *condEnv = condNativeEnvRegister.get().GetPluginStruct( nativeMan );

    assert( condEnv != nullptr );

    CExecThreadImpl *nativeThread = (CExecThreadImpl*)nativeMan->GetCurrentThread();

    // Could be nullptr if the environment is terminating.
    assert( nativeThread != nullptr );

    condVarNativeEnv::condVarThreadPlugin *threadCondEnv = condEnv->GetThreadCondEnv( nativeThread );

    assert( threadCondEnv != nullptr );

    // Get the thread waiter event.
    CEvent *evtWaiter = GetCurrentThreadWaiterEvent( manager, nativeThread );

    // Put the thread into waiting hazard mode.
    {
        // We must not let the thread switch from RUNNING into TERMINATING state here.
        // * if the thread is RUNNING then we can make him wait.
        // * if the thread is TERMINATING then we must throw an exception to kill it.

        CUnfairMutexContext ctxThreadState( nativeThread->mtxThreadStatus );
        CReadWriteWriteContextSafe <> ctxWaitCall( this->lockAtomicCalls );

        // Only problem could be termination request, since a wait would obstruct it.
        nativeThread->CheckTerminationRequest();

        // FROM HERE ON, we cannot prematurely trigger hazard term request.

        // We set ourselves to wait for a signal.
        // This thing can only be released by a call to Signal (and possible hazard resolver).
        evtWaiter->Set( true );

        // We need to know what conditional variable we wait on.
        threadCondEnv->waitingOnVar = this;

        // Register ourselves in the conditional variable waiter list.
        LIST_INSERT( this->listWaitingThreads.root, threadCondEnv->condRegister.node );

        // Make sure that our hazard can be resolved.
        PushHazard( nativeMan, threadCondEnv );
    }

    // Release all locks because we are safe.
    CReadWriteLock *userLock = ctxLock.GetCurrentLock();

    ctxLock.Suspend();

    // Do the wait.
    evtWaiter->Wait();

    // We have been revived by a signal, so let us continue.
    ctxLock = userLock;

    // Remove the thread from waiting hazard mode.
    {
        CUnfairMutexContext ctxThreadState( nativeThread->mtxThreadStatus );

        // Remove our hazard again.
        PopHazard( nativeMan );

        // Not waiting on any thread anymore.
        threadCondEnv->waitingOnVar = nullptr;

        // We could have woken up by hazard-check, in which case we probably are asked to terminate.
        nativeThread->CheckTerminationRequest();
    }
}

void CCondVarImpl::Signal( void )
{
    CExecutiveManagerNative *nativeMan = this->manager;

    condVarNativeEnv *condEnv = condNativeEnvRegister.get().GetPluginStruct( nativeMan );

    if ( !condEnv )
        return;

    // We need to have a sure-fire go ahead for the list of waiting threads.
    CReadWriteWriteContextSafe <> ctxSignalCall( this->lockAtomicCalls );

    // The idea is to wake all threads that reside in the waiting list.
    // To do that we first reference all threads, so we can safely use their data + plugins.
    // (the list of waiting threads will be allowed to mutate when we release em from their sleep).
    LIST_FOREACH_BEGIN( perThreadCondVarRegistration, this->listWaitingThreads.root, node )

        condVarNativeEnv::condVarThreadPlugin *threadPlugin = LIST_GETITEM( condVarNativeEnv::condVarThreadPlugin, item, condRegister );

        CExecThreadImpl *nativeThread = condEnv->BackResolveThread( threadPlugin );

        // Set the thread to not wait anymore.
        // Should open the floodgates.
        CEvent *evtWaiter = GetCurrentThreadWaiterEvent( nativeMan, nativeThread );

        evtWaiter->Set( false );

    LIST_FOREACH_END

    // We have no more waiting threads.
    LIST_CLEAR( this->listWaitingThreads.root );
}

void CCondVar::Wait( CReadWriteWriteContextSafe <>& ctxLock )   { ((CCondVarImpl*)this)->Wait( ctxLock ); }
void CCondVar::Signal( void )                                   { ((CCondVarImpl*)this)->Signal(); }

CExecutiveManager* CCondVar::GetManager( void )
{
    CCondVarImpl *condNative = (CCondVarImpl*)this;

    return condNative->manager;
}

CCondVar* CExecutiveManager::CreateConditionVariable( void )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    NatExecStandardObjectAllocator memAlloc( nativeMan );

    CCondVarImpl *condVar = eir::dyn_new_struct <CCondVarImpl> ( memAlloc, nullptr, nativeMan );

    return condVar;
}

void CExecutiveManager::CloseConditionVariable( CCondVar *condVar )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    NatExecStandardObjectAllocator memAlloc( nativeMan );

    CCondVarImpl *nativeCondVar = (CCondVarImpl*)condVar;

    eir::dyn_del_struct <CCondVarImpl> ( memAlloc, nullptr, nativeCondVar );
}

void registerConditionalVariables( void )
{
    condNativeEnvRegister.Construct( executiveManagerFactory );
}

void unregisterConditionalVariables( void )
{
    condNativeEnvRegister.Destroy();
}

END_NATIVE_EXECUTIVE
