/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.spinlock.internal.h
*  PURPOSE:     Spin-lock implementation for low-level locking
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _NAT_EXEC_SPIN_LOCK_OBJECT_HEADER_
#define _NAT_EXEC_SPIN_LOCK_OBJECT_HEADER_

BEGIN_NATIVE_EXECUTIVE

// For use by low-level primitives in constant CPU time code regions.
struct CSpinLock abstract
{
    void lock( void );
    bool tryLock( void );
    void unlock( void );
};

END_NATIVE_EXECUTIVE

#endif //_NAT_EXEC_SPIN_LOCK_OBJECT_HEADER_