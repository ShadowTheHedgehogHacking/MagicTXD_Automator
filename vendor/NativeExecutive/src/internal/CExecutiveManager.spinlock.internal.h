/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.spinlock.internal.h
*  PURPOSE:     Cross-platform native spin-lock implementation for low-level locking
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _NAT_EXEC_SPIN_LOCK_IMPL_HEADER_
#define _NAT_EXEC_SPIN_LOCK_IMPL_HEADER_

#include "CExecutiveManager.spinlock.h"

#include <atomic>
#include <assert.h>

BEGIN_NATIVE_EXECUTIVE

// Use this primitive in better synchronization layers as stable foundation.
// But make sure that each region you use this in is constant CPU time and a small amount.
// This lock is very unfair because it relies on CPU instructions only and those
// do not allow for any queues of waiter-entries.
struct CSpinLockImpl : public CSpinLock
{
    inline CSpinLockImpl( void ) : isLockTaken( false )
    {
        return;
    }
    inline CSpinLockImpl( const CSpinLockImpl& ) = delete;
    inline CSpinLockImpl( CSpinLockImpl&& ) = delete;

    inline ~CSpinLockImpl( void )
    {
#ifdef _DEBUG
        assert( this->isLockTaken == false );
#endif //_DEBUG
    }

    // We cannot more or copy a spin-lock because it absolutely requires
    // to stay on the same memory location.
    inline CSpinLockImpl& operator = ( const CSpinLockImpl& ) = delete;
    inline CSpinLockImpl& operator = ( CSpinLockImpl&& ) = delete;

    inline void lock( void )
    {
        // Wait until the region of code is free.
        while ( true )
        {
            bool is_lock_taken = isLockTaken.exchange( true );

            if ( !is_lock_taken )
            {
                break;
            }
        }
    }

    inline bool tryLock( void )
    {
        // Attempt to enter the lock.
        // Very important to have because sometimes you need to enter
        // two contexts intertwined, so the weaker entry must try-only.
        bool is_lock_taken = isLockTaken.exchange( true );

        return ( is_lock_taken == false );
    }

    inline void unlock( void )
    {
        // Simply release the lock.
#ifdef _DEBUG
        bool wasLockTaken =
#endif //_DEBUG
        isLockTaken.exchange( false );

#ifdef _DEBUG
        assert( wasLockTaken == true );
#endif //_DEBUG
    }

private:
    std::atomic <bool> isLockTaken;
};

END_NATIVE_EXECUTIVE

#endif //_NAT_EXEC_SPIN_LOCK_IMPL_HEADER_
