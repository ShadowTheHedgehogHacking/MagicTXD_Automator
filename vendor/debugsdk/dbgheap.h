/*****************************************************************************
*
*  PROJECT:     Multi Theft Auto v1.2
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        debugsdk/dbgheap.h
*  PURPOSE:     Heap management tools for error isolation & debugging
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef HEAP_DEBUG
#define HEAP_DEBUG

#include <stddef.h>

// Include the project dependent configuration.
// It has to be provided by the compilation module.
#include <debugsdk_config.h>

#ifdef USE_HEAP_DEBUGGING

#include <exception>
#include <new>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4290)
#endif //_MSC_VER

// Global allocator
void* operator new( size_t memSize );
void* operator new( size_t memSize, const std::nothrow_t& nothrow ) noexcept;
void* operator new[]( size_t memSize );
void* operator new[]( size_t memSize, const std::nothrow_t& nothrow ) noexcept;
void operator delete( void *ptr ) noexcept;
void operator delete[]( void *ptr ) noexcept;

// Malloc functions
void* DbgMalloc( size_t size );
void* DbgRealloc( void *ptr, size_t size );
bool DbgAllocGetSize( void *ptr, size_t& sizeOut );
void DbgFree( void *ptr );

#ifdef _MSC_VER
#pragma warning(pop)
#endif //_MSC_VER

#endif //USE_HEAP_DEBUGGING

typedef void (*pfnMemoryAllocWatch)( void *memPtr, size_t memSize );

void DbgHeap_Init( void );
void DbgHeap_Validate( void );
void DbgHeap_CheckActiveBlocks( void );
void DbgHeap_SetMemoryAllocationWatch( pfnMemoryAllocWatch allocWatchCallback );
void DbgHeap_Shutdown( void );

#endif //HEAP_DEBUG
