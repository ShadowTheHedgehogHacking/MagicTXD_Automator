/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.native.hxx
*  PURPOSE:     Includes native module definitions for NativeExecutive.
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _NATIVE_EXECUTIVE_INTERNAL_
#define _NATIVE_EXECUTIVE_INTERNAL_

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif //_WIN32

#if defined(_M_IX86)

// Fiber routines.
extern "C" void __stdcall _fiber86_retHandler( NativeExecutive::FiberStatus *userdata );
extern "C" void __cdecl _fiber86_eswitch( NativeExecutive::Fiber *from, NativeExecutive::Fiber *to );
extern "C" void __cdecl _fiber86_qswitch( NativeExecutive::Fiber *from, NativeExecutive::Fiber *to );

// Thread routines.
extern "C" DWORD WINAPI _thread86_procNative( LPVOID lpThreadParameter );

#elif defined(_M_AMD64)

// Fiber routines.
extern "C" void __cdecl _fiber64_procStart( void );
extern "C" void __cdecl _fiber64_eswitch( NativeExecutive::Fiber *from, NativeExecutive::Fiber *to );
extern "C" void __cdecl _fiber64_qswitch( NativeExecutive::Fiber *from, NativeExecutive::Fiber *to );

// Fiber special routines not to be called directly.
extern "C" void __stdcall _fiber64_term( void );

// Thread routines.
extern "C" DWORD WINAPI _thread64_procNative( LPVOID lpThreadParameter );

#endif

#endif //_NATIVE_EXECUTIVE_INTERNAL_