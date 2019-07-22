/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.unfairmtx.cpp
*  PURPOSE:     Cross-platform native unfair mutex implementation that relies on OS thread sheduler.
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include "StdInc.h"

#include "internal/CExecutiveManager.unfairmtx.internal.h"

BEGIN_NATIVE_EXECUTIVE

void CUnfairMutex::lock( void )
{
    CUnfairMutexImpl *nativeMutex = (CUnfairMutexImpl*)this;

    nativeMutex->lock();
}

void CUnfairMutex::unlock( void )
{
    CUnfairMutexImpl *nativeMutex = (CUnfairMutexImpl*)this;

    nativeMutex->unlock();
}

CUnfairMutex* CExecutiveManager::CreateUnfairMutex( void )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    NatExecStandardObjectAllocator memAlloc( nativeMan );

    CEvent *evtWaiter = nativeMan->CreateEvent();

    if ( !evtWaiter )
        return nullptr;
    
    try
    {
        return eir::dyn_new_struct <CUnfairMutexImpl> ( memAlloc, nullptr, evtWaiter );
    }
    catch( ... )
    {
        nativeMan->CloseEvent( evtWaiter );

        throw;
    }
}

void CExecutiveManager::CloseUnfairMutex( CUnfairMutex *mtx )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    NatExecStandardObjectAllocator memAlloc( nativeMan );

    CUnfairMutexImpl *nativeMutex = (CUnfairMutexImpl*)mtx;

    CEvent *evtWaiter = nativeMutex->get_event();
    
    // First destroy the mutex.
    eir::dyn_del_struct <CUnfairMutexImpl> ( memAlloc, nullptr, nativeMutex );

    // Clean-up the event.
    nativeMan->CloseEvent( evtWaiter );
}

size_t CExecutiveManager::GetUnfairMutexStructSize( void )
{
    return sizeof(CUnfairMutex);
}

size_t CExecutiveManager::GetUnfairMutexAlignment( void )
{
    return alignof(CUnfairMutex);
}

CUnfairMutex* CExecutiveManager::CreatePlacedUnfairMutex( void *mem )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    CEvent *evt = nativeMan->CreateEvent();

    if ( evt == nullptr )
    {
        return nullptr;
    }
    
    try
    {
        return new (mem) CUnfairMutexImpl( evt );
    }
    catch( ... )
    {
        nativeMan->CloseEvent( evt );

        throw;
    }
}

void CExecutiveManager::ClosePlacedUnfairMutex( CUnfairMutex *mtx )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    CUnfairMutexImpl *nativeMtx = (CUnfairMutexImpl*)mtx;

    CEvent *evtWaiter = nativeMtx->get_event();

    nativeMtx->~CUnfairMutexImpl();

    nativeMan->CloseEvent( evtWaiter );
}

END_NATIVE_EXECUTIVE