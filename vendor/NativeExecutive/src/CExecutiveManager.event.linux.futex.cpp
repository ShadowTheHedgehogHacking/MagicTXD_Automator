/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.event.linux.futex.cpp
*  PURPOSE:     Linux event implementation using fast user-space mutex (futex)
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

// Linux has been supporting waiting-on-address since the dawn of time, thus it
// has been superior to Windows (until Windows 8 that is).

#include "StdInc.h"

#ifdef __linux__

#include <unistd.h>
#include <sys/syscall.h>
#include <linux/futex.h>

#include <time.h>
#include <limits.h>
#include <chrono>

#include <atomic>

#define futex(uaddr, futex_op, val, timeout, uaddr2, val3) \
    syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3)

BEGIN_NATIVE_EXECUTIVE

struct _event_linux_futex
{
    std::atomic <int> value;  // 0 if can pass, 1 if must wait
};

bool _event_linux_futex_is_supported( void )
{
    return true;
}

size_t _event_linux_futex_get_size( void )
{
    return sizeof(_event_linux_futex);
}

size_t _event_linux_futex_get_alignment( void )
{
    return alignof(_event_linux_futex);
}

void _event_linux_futex_constructor( void *mem )
{
    _event_linux_futex *item = (_event_linux_futex*)mem;

    item->value = 0;
}

void _event_linux_futex_destructor( void *mem )
{
    // Nothíng to do.
}

void _event_linux_futex_set( void *mem, bool shouldWait )
{
    _event_linux_futex *item = (_event_linux_futex*)mem;

    if ( shouldWait )
    {
        item->value = 1;
    }
    else
    {
        item->value = 0;

        int woken = futex( &item->value, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT_MAX, NULL, NULL, 0 );

        assert( woken >= 0 );
    }
}

void _event_linux_futex_wait( void *mem )
{
    _event_linux_futex *item = (_event_linux_futex*)mem;

    while ( item->value != 0 )
    {
        futex( &item->value, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 1, NULL, NULL, 0 );
    }
}

bool _event_linux_futex_wait_timed( void *mem, unsigned int msTimeout )
{
    _event_linux_futex *item = (_event_linux_futex*)mem;

    using namespace std::chrono;

    time_point <high_resolution_clock, milliseconds> end_time =
        time_point_cast <milliseconds> ( high_resolution_clock::now() ) + milliseconds( msTimeout );

    while ( item->value != 0 )
    {
        time_point <high_resolution_clock, milliseconds> cur_time =
            time_point_cast <milliseconds> ( high_resolution_clock::now() );

        long waitMS = (long)( end_time - cur_time ).count();

        if ( waitMS <= 0 )
        {
            return false;
        }

        timespec dur_wait;
        dur_wait.tv_sec = (time_t)( waitMS / 1000 );
        dur_wait.tv_nsec = ( ( waitMS % 1000 ) * 1000 );

        futex( &item->value, FUTEX_WAIT | FUTEX_PRIVATE_FLAG, 0, &dur_wait, NULL, 0 );
    }

    return true;
}

void _event_linux_futex_init( void )
{
    return;
}

void _event_linux_futex_shutdown( void )
{
    return;
}

END_NATIVE_EXECUTIVE

#endif //__linux__
