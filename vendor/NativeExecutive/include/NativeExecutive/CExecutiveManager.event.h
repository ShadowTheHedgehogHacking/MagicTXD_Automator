/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.event.h
*  PURPOSE:     Public header of the (waitable) event object.
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _NAT_EXEC_EVENT_HEADER_
#define _NAT_EXEC_EVENT_HEADER_

BEGIN_NATIVE_EXECUTIVE

// Simple waiter-gate that is thread-safe.
// Optimized to take like-zero resources.
// Default state when created: false (do not wait)
class CEvent abstract
{
public:
    void        Set( bool shouldWait );
    void        Wait( void );
    bool        WaitTimed( unsigned int msTimeout );
};

END_NATIVE_EXECUTIVE

#endif //_NAT_EXEC_EVENT_HEADER_