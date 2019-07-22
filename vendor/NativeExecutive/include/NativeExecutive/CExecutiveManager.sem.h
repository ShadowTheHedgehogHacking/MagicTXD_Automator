/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.sem.internal.h
*  PURPOSE:     Semaphore object header
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _NATEXEC_SEMAPHORE_HEADER_
#define _NATEXEC_SEMAPHORE_HEADER_

BEGIN_NATIVE_EXECUTIVE

// Very simple semaphore that just supports one-increment and one-decrement.
// Any other semaphore type would dependend on the threading-subsystem so 
// no thanks.
struct CSemaphore
{
    // Insert another resource into us.
    void Increment( void );
    // Decrement a resource away from us.
    void Decrement( void );

    // we decided against timed-decrement because it is not reliably possible
    // in the one-increment/one-decrement and unfair semaphore.
};

END_NATIVE_EXECUTIVE

#endif //_NATEXEC_SEMAPHORE_HEADER_