/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.rwlock.hxx
*  PURPOSE:     Read/Write lock private internal implementation header
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _NAT_EXEC_PRIVATE_RWLOCK_IMPL_HEADER_
#define _NAT_EXEC_PRIVATE_RWLOCK_IMPL_HEADER_

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#undef NOMINMAX
#define NOMINMAX
#ifndef _MSC_VER
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif //_MSC_VER
#include <Windows.h>
#endif //_WIN32

BEGIN_NATIVE_EXECUTIVE

// Lock implementation anonymous functions.
struct readWriteLockEnv_t;

typedef void (*rwlockimpl_construct)( void *lockmem, CExecutiveManagerNative *nativeMan );
typedef void (*rwlockimpl_destroy)( void *lockmem, CExecutiveManagerNative *nativeMan );
typedef void (*rwlockimpl_enter_read)( void *lockmem );
typedef void (*rwlockimpl_leave_read)( void *lockmem );
typedef void (*rwlockimpl_enter_write)( void *lockmem );
typedef void (*rwlockimpl_leave_write)( void *lockmem );
typedef bool (*rwlockimpl_try_enter_read)( void *lockmem );
typedef bool (*rwlockimpl_try_enter_write)( void *lockmem );

// Macro helpers for Win32 library loading.
#define METHODNAME_GLOBAL( funcName ) _dynfunc_##funcName
#define METHODDECL_GLOBAL( funcName ) \
    decltype(&funcName) METHODNAME_GLOBAL( funcName ) = nullptr;
#define METHODDECL_FETCH( module, funcName ) \
    METHODNAME_GLOBAL( funcName ) = (decltype(&funcName))GetProcAddress( module, #funcName );

// Helper for calling an anonymous constructor on memory using an allocator.
template <typename allocatorType, typename constrType, typename... Args>
inline void* dyn_anon_construct( allocatorType& memAlloc, const constrType& constructor, size_t memSize, size_t memAlignment, Args&&... theArgs )
{
    if ( !constructor )
        return nullptr;

    void *mem = memAlloc.Allocate( nullptr, memSize, memAlignment );

    if ( !mem )
        return nullptr;

    try
    {
        constructor( mem, std::forward <Args> ( theArgs )... );
    }
    catch( ... )
    {
        memAlloc.Free( nullptr, mem );
        throw;
    }

    return mem;
}

// Export the public read write lock interface.
size_t pubrwlock_get_size( void );
size_t pubrwlock_get_alignment( void );
bool pubrwlock_is_supported( void );

END_NATIVE_EXECUTIVE

#endif //_NAT_EXEC_PRIVATE_RWLOCK_IMPL_HEADER_