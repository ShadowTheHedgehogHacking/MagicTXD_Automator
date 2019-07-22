/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.sem.cpp
*  PURPOSE:     Cross-platform native semaphore implementation
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include "StdInc.h"

#include "internal/CExecutiveManager.sem.internal.h"

#include <limits>

BEGIN_NATIVE_EXECUTIVE

void CSemaphore::Increment( void )
{
    CSemaphoreImpl *natSem = (CSemaphoreImpl*)this;

    CSpinLockContext ctxSem( natSem->lockAtomic );

    if ( std::numeric_limits <size_t>::max() == natSem->cur_count )
    {
        // Error: too much semaphore incrementation.
        throw native_executive_exception();

        // TODO: maybe make the semaphore wait as long as the increment is impossible.
    }

    natSem->cur_count++;

    // Since our count increased, somebody could now decrement this semaphore.
    // So unwait any thread.
    natSem->evtWaiter->Set( false );
}

void CSemaphore::Decrement( void )
{
    CSemaphoreImpl *natSem = (CSemaphoreImpl*)this;

    // If the decrement is impossible, then we wait for that time until we can.
    CEvent *evtWaiter = natSem->evtWaiter;

tryDecrementAgain:
    CSpinLockContext ctxSem( natSem->lockAtomic );

    size_t prevCount = natSem->cur_count;

    if ( prevCount == 0 )
    {
        ctxSem.Suspend();

        evtWaiter->Wait();

        goto tryDecrementAgain;
    }

    natSem->cur_count = ( prevCount - 1 );

    // If we zeroed ourselves out, then next threads have to wait.
    if ( prevCount == 1 )
    {
        evtWaiter->Set( true );
    }
}

CSemaphore* CExecutiveManager::CreateSemaphore( void )
{
    CEvent *evt = this->CreateEvent();

    if ( evt == nullptr )
    {
        return nullptr;
    }

    NatExecStandardObjectAllocator memAlloc( this );

    return eir::dyn_new_struct <CSemaphoreImpl> ( memAlloc, nullptr, evt );
}

void CExecutiveManager::CloseSemaphore( CSemaphore *sem )
{
    CSemaphoreImpl *natSem = (CSemaphoreImpl*)sem;

    CEvent *evt = natSem->evtWaiter;

    this->CloseEvent( evt );

    NatExecStandardObjectAllocator memAlloc( this );

    eir::dyn_del_struct <CSemaphoreImpl> ( memAlloc, nullptr, natSem );
}

size_t CExecutiveManager::GetSemaphoreStructSize( void )
{
    return sizeof(CSemaphoreImpl);
}

size_t CExecutiveManager::GetSemaphoreAlignment( void )
{
    return alignof(CSemaphoreImpl);
}

CSemaphore* CExecutiveManager::CreatePlacedSemaphore( void *mem )
{
    CEvent *evt = this->CreateEvent();

    if ( evt == nullptr )
    {
        return nullptr;
    }

    return new (mem) CSemaphoreImpl( evt );
}

void CExecutiveManager::ClosePlacedSemaphore( CSemaphore *sem )
{
    CSemaphoreImpl *natSem = (CSemaphoreImpl*)sem;

    CEvent *evt = natSem->evtWaiter;

    this->CloseEvent( evt );

    natSem->~CSemaphoreImpl();
}

END_NATIVE_EXECUTIVE