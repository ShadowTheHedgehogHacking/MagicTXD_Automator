/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.memory.cpp
*  PURPOSE:     Straight-shot memory management.
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

// In NativeExecutive we pipe (nearly all) memory requests through a central
// provider. Memory requests have to be protected by a lock to be thread-safe.
// Thus we give those structures their own file.

#include "StdInc.h"

#include "internal/CExecutiveManager.event.internal.h"
#include "internal/CExecutiveManager.unfairmtx.internal.h"

#include <sdk/OSUtils.memheap.h>

#include "CExecutiveManager.eventplugin.hxx"

#include <cstdlib>

BEGIN_NATIVE_EXECUTIVE

// TODO: since we are going to need many events across native executive embedded into the system structure (yes, we want
//  to remove as many calls to malloc as possible), we should create a helper struct for embedding events, just like the
//  memory event.

static optional_struct_space <EventPluginRegister> _natExecMemoryEventReg;

struct natExecMemoryManager
{
    inline natExecMemoryManager( CExecutiveManagerNative *natExec ) : mtxMemLock( _natExecMemoryEventReg.get().GetEvent( natExec ) )
    {
        return;
    }

    inline void Initialize( CExecutiveManagerNative *natExec )
    {
        // TODO: allow the user to specify their own stuff.

        natExec->memoryIntf = &defaultAlloc;
    }

    inline void Shutdown( CExecutiveManagerNative *natExec )
    {
        natExec->memoryIntf = nullptr;
    }

    // Default memory allocator, in case the user does not supply us with their own.
    struct defaultMemAllocator : public MemoryInterface
    {
        void* Allocate( size_t memSize, size_t alignment ) override
        {
            return defaultMemHeap.Allocate( memSize, alignment );
        }

        bool Resize( void *memPtr, size_t reqSize ) override
        {
#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
            assert( defaultMemHeap.DoesOwnAllocation( memPtr ) == true );
#endif //_DEBUG

            return defaultMemHeap.SetAllocationSize( memPtr, reqSize );
        }

        void Free( void *memPtr ) override
        {
#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
            assert( defaultMemHeap.DoesOwnAllocation( memPtr ) == true );
#endif //_DEBUG

            defaultMemHeap.Free( memPtr );
        }

        NativeHeapAllocator defaultMemHeap;
    };

    defaultMemAllocator defaultAlloc;

    CUnfairMutexImpl mtxMemLock;
};

static optional_struct_space <PluginDependantStructRegister <natExecMemoryManager, executiveManagerFactory_t>> natExecMemoryEnv;

// Module API.
CUnfairMutex* CExecutiveManager::GetMemoryLock( void )
{
    CExecutiveManagerNative *natExec = (CExecutiveManagerNative*)this;

    natExecMemoryManager *memEnv = natExecMemoryEnv.get().GetPluginStruct( natExec );

    if ( !memEnv )
        return nullptr;

    return &memEnv->mtxMemLock;
}

void* CExecutiveManager::MemAlloc( size_t memSize, size_t alignment ) noexcept
{
    CExecutiveManagerNative *natExec = (CExecutiveManagerNative*)this;

    MemoryInterface *memIntf = natExec->memoryIntf;

    assert( memIntf != nullptr );

    // So we basically settled on the fact that memory allocation must not use locks that allocate
    // memory themselves because then a memory allocation would occur that would not be
    // protected under a lock itself, causing thread-insafety.

    CUnfairMutexContext ctxMemLock( natExec->GetMemoryLock() );

    return memIntf->Allocate( memSize, alignment );
}

bool CExecutiveManager::MemResize( void *memPtr, size_t reqSize ) noexcept
{
    CExecutiveManagerNative *natExec = (CExecutiveManagerNative*)this;

    MemoryInterface *memIntf = natExec->memoryIntf;

    assert( memIntf != nullptr );

    CUnfairMutexContext ctxMemLock( natExec->GetMemoryLock() );

    return memIntf->Resize( memPtr, reqSize );
}

void CExecutiveManager::MemFree( void *memPtr ) noexcept
{
    CExecutiveManagerNative *natExec = (CExecutiveManagerNative*)this;

    MemoryInterface *memIntf = natExec->memoryIntf;

    assert( memIntf != nullptr );

    CUnfairMutexContext ctxMemLock( natExec->GetMemoryLock() );

    memIntf->Free( memPtr );
}

// Access to the memory quota by the statistics API.
void _executive_manager_get_internal_mem_quota( CExecutiveManagerNative *nativeMan, size_t& usedBytesOut, size_t& metaBytesOut )
{
    natExecMemoryManager *memMan = natExecMemoryEnv.get().GetPluginStruct( nativeMan );

    assert( memMan != nullptr );

    NativeHeapAllocator::heapStats stats = memMan->defaultAlloc.defaultMemHeap.GetStatistics();

    // TODO: count in the global allocator hook if it is enabled.

    usedBytesOut = stats.usedBytes;
    metaBytesOut = stats.usedMetaBytes;
}

// Sub-modules.
#ifdef NATEXEC_GLOBALMEM_OVERRIDE

void registerGlobalMemoryOverrides( void );
void unregisterGlobalMemoryOverrides( void );

#endif //NATEXEC_GLOBALMEM_OVERRIDE

// Module init.
void registerMemoryManager( void )
{
#ifdef NATEXEC_GLOBALMEM_OVERRIDE
    registerGlobalMemoryOverrides();
#endif //NATEXEC_GLOBALMEM_OVERRIDE

    // First we need the event.
    _natExecMemoryEventReg.Construct( executiveManagerFactory );

    // Register the memory environment.
    natExecMemoryEnv.Construct( executiveManagerFactory );
}

void unregisterMemoryManager( void )
{
    // Unregister the memory environment.
    natExecMemoryEnv.Destroy();

    // Unregister the memory event.
    _natExecMemoryEventReg.Destroy();

#ifdef NATEXEC_GLOBALMEM_OVERRIDE
    unregisterGlobalMemoryOverrides();
#endif //NATEXEC_GLOBALMEM_OVERRIDE
}

#ifdef NATEXEC_GLOBALMEM_OVERRIDE

struct _global_alloc_memlock_data
{
    inline void Initialize( void )
    {
        size_t event_start_off = GetEventStartOff();

        size_t event_size = pubevent_get_size();

        size_t requiredSize = ( event_start_off + event_size );

        assert( requiredSize <= MAX_STATIC_SYNC_STRUCT_SIZE );

        void *evt_mem = data + event_start_off;

        pubevent_constructor( evt_mem );

        CEvent *evt = (CEvent*)evt_mem;

        new (data) CUnfairMutexImpl( evt );
    }

    inline void Shutdown( void )
    {
        CUnfairMutexImpl *mutex = (CUnfairMutexImpl*)this;

        mutex->~CUnfairMutexImpl();

        CEvent *evt = (CEvent*)( data + GetEventStartOff() );

        pubevent_destructor( evt );
    }

    inline CUnfairMutexImpl* GetMutex( void )
    {
        return (CUnfairMutexImpl*)this->data;
    }

private:
    static AINLINE size_t GetEventStartOff( void )
    {
        size_t event_alignment = pubevent_get_alignment();

        size_t event_start_off = ALIGN_SIZE( sizeof(CUnfairMutexImpl), event_alignment );

        return event_start_off;
    }

    // Opaque data for the unfair mutex + wait event.
    char data[ MAX_STATIC_SYNC_STRUCT_SIZE ];
};

#endif //NATEXEC_GLOBALMEM_OVERRIDE

END_NATIVE_EXECUTIVE

#ifdef NATEXEC_GLOBALMEM_OVERRIDE

static optional_struct_space <NativeHeapAllocator> _global_mem_alloc;
static NativeExecutive::_global_alloc_memlock_data _global_memlock;

BEGIN_NATIVE_EXECUTIVE

// The event subsystem has its own refcount so this is valid.
extern void registerEventManagement( void );
extern void unregisterEventManagement( void );

END_NATIVE_EXECUTIVE

static volatile size_t _overrides_refcnt = 0;

static void initializeGlobalMemoryOverrides( void )
{
    if ( _overrides_refcnt++ == 0 )
    {
        NativeExecutive::registerEventManagement();

        _global_mem_alloc.Construct();
        _global_memlock.Initialize();
    }
}

static void shutdownGlobalMemoryOverrides( void )
{
    if ( --_overrides_refcnt == 0 )
    {
        _global_memlock.Shutdown();
        _global_mem_alloc.Destroy();

        NativeExecutive::unregisterEventManagement();
    }
}

#if !defined(_MSC_VER) || !defined(_DEBUG)

// Need to make sure that malloc is initialized no-matter-what.
// Because the event management is its own isolated subsystem we can depend on it.

static volatile bool _malloc_has_initialized_overrides = false;

AINLINE void _prepare_overrides( void )
{
    if ( !_malloc_has_initialized_overrides )
    {
        _malloc_has_initialized_overrides = true;

        initializeGlobalMemoryOverrides();
    }
}

// Since the C++ programming language does use memory allocation in standard features such as
// exception throwing, we have to provide thread-safe memory allocation systems.
void* malloc( size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global malloc detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    // malloc could be spuriously called by either DLL init (on Linux) or the
    // throwing of an event. So make sure that we are prepared.
    _prepare_overrides();

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    // NativeHeapAllocator has a default alignment of std::max_align_t.
    return _global_mem_alloc.get().Allocate( memSize );
}

static AINLINE void* _realloc_impl( void *memptr, size_t memSize )
{
    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
    if ( memptr != nullptr )
    {
        assert( _global_mem_alloc.get().DoesOwnAllocation( memptr ) == true );
    }
#endif //_DEBUG

    // NativeHeapAllocator has a default alignment of std::max_align_t.
    return _global_mem_alloc.get().Realloc( memptr, memSize );
}

void* realloc( void *memptr, size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global realloc detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    return _realloc_impl( memptr, memSize );
}

static AINLINE void _free_impl( void *memptr )
{
    if ( memptr != nullptr )
    {
        NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
        assert( _global_mem_alloc.get().DoesOwnAllocation( memptr ) == true );
#endif //_DEBUG

        _global_mem_alloc.get().Free( memptr );
    }
}

void free( void *memptr )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global free detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    _free_impl( memptr );
}

static AINLINE void* _calloc_impl( size_t cnt, size_t memSize )
{
    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    size_t actualSize = ( cnt * memSize );

    void *memBlock = _global_mem_alloc.get().Allocate( memSize * cnt );

    if ( memBlock != nullptr )
    {
        memset( memBlock, 0, actualSize );
    }

    return memBlock;
}

void* calloc( size_t cnt, size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global calloc detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    return _calloc_impl( cnt, memSize );
}

#ifdef _MSC_VER

static AINLINE void* _expand_impl( void *memptr, size_t memSize )
{
    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
    assert( _global_mem_alloc.get().DoesOwnAllocation( memptr ) == true );
#endif //_DEBUG

    bool couldResize = _global_mem_alloc.get().SetAllocationSize( memptr, memSize );

    if ( couldResize == false )
    {
        return nullptr;
    }

    return memptr;
}

void*
#ifdef _DEBUG
__expand
#else
_expand
#endif //_DEBUG
( void *memptr, size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global _expand detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    return _expand_impl( memptr, memSize );
}

#ifdef _DEBUG
#pragma comment(linker, "/alternatename:_expand=__expand")
#endif //_DEBUG

static AINLINE size_t _msize_impl( void *memptr )
{
    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
    assert( _global_mem_alloc.get().DoesOwnAllocation( memptr ) == true );
#endif //_DEBUG

    return _global_mem_alloc.get().GetAllocationSize( memptr );
}

size_t _msize( void *memptr )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global _msize detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    return _msize_impl( memptr );
}

#endif //_MSC_VER

// Popular C11 function.
void* aligned_alloc( size_t alignment, size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global aligned_alloc detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( memSize, alignment );
}

#ifdef __linux__

void* memalign( size_t alignment, size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global memalign detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( memSize, alignment );
}

int posix_memalign( void **ptr, size_t alignment, size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global posix_memalign detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    void *newptr = _global_mem_alloc.get().Allocate( memSize, alignment );

    if ( newptr == nullptr )
    {
        return ENOMEM;
    }

    *ptr = newptr;

    return 0;
}

void* valloc( size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global valloc detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( memSize, pagesize );
}

void* pvalloc( size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global pvalloc detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( ALIGN_SIZE( memSize, pagesize ), pagesize );
}

size_t malloc_usable_size( void *ptr )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global malloc_usable_size detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _prepare_overrides();

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().GetAllocationSize( ptr );
}

#endif //__linux__

#endif //DEBUG FUNCTIONS NOT FOR MSVC.

// Also the new operators.
void* operator new ( size_t memSize )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator new detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( memSize );
}

void* operator new ( size_t memSize, const std::nothrow_t& nothrow_value ) noexcept
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator new (nothrow) detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( memSize );
}

void* operator new ( size_t memSize, std::align_val_t alignment )
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator new (with align) detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( memSize, (size_t)alignment );
}

void* operator new ( size_t memSize, std::align_val_t alignment, const std::nothrow_t& ntv ) noexcept
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator new (nothrow, with align) detected.\n" );
#endif //NNATEXEC_LOG_GLOBAL_ALLOC

    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( memSize, (size_t)alignment );
}

void* operator new [] ( size_t memSize )
{ return operator new ( memSize ); }

void* operator new [] ( size_t memSize, const std::nothrow_t& nothrow_value ) noexcept
{ return operator new ( memSize, nothrow_value ); }

void* operator new [] ( size_t memSize, std::align_val_t alignment )
{ return operator new ( memSize, alignment ); }

void* operator new [] ( size_t memSize, std::align_val_t alignment, const std::nothrow_t& ntv ) noexcept
{ return operator new ( memSize, alignment, ntv ); }

AINLINE static void _free_common_pointer( void *ptr )
{
    if ( ptr != nullptr )
    {
        NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
        assert( _global_mem_alloc.get().DoesOwnAllocation( ptr ) == true );
#endif //_DEBUG

        _global_mem_alloc.get().Free( ptr );
    }
}

void operator delete ( void *ptr ) noexcept
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator delete detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _free_common_pointer( ptr );
}

void operator delete ( void *ptr, const std::nothrow_t& ntv ) noexcept
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator delete (nothrow) detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _free_common_pointer( ptr );
}

void operator delete ( void *ptr, size_t memSize ) noexcept
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator delete (with size) detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _free_common_pointer( ptr );
}

void operator delete ( void *ptr, std::align_val_t alignment ) noexcept
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator delete (with alignment) detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _free_common_pointer( ptr );
}

void operator delete ( void *ptr, std::align_val_t alignment, const std::nothrow_t& ntv ) noexcept
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator delete (with alignment, nothrow) detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _free_common_pointer( ptr );
}

void operator delete ( void *ptr, size_t memSize, std::align_val_t alignment ) noexcept
{
#ifdef NATEXEC_LOG_GLOBAL_ALLOC
    printf( "call to global operator delete (with size, with alignment) detected.\n" );
#endif //NATEXEC_LOG_GLOBAL_ALLOC

    _free_common_pointer( ptr );
}

void operator delete [] ( void *ptr ) noexcept
{ operator delete ( ptr ); }

void operator delete [] ( void *ptr, const std::nothrow_t& ntv ) noexcept
{ operator delete ( ptr, ntv ); }

void operator delete [] ( void *ptr, size_t memSize ) noexcept
{ operator delete ( ptr, memSize ); }

void operator delete [] ( void *ptr, std::align_val_t alignment ) noexcept
{ operator delete ( ptr, alignment ); }

void operator delete [] ( void *ptr, std::align_val_t alignment, const std::nothrow_t& ntv ) noexcept
{ operator delete ( ptr, alignment, ntv ); }

void operator delete [] ( void *ptr, size_t memSize, std::align_val_t alignment ) noexcept
{ operator delete ( ptr, memSize, alignment ); }

// TODO: add back hooks for _malloc_dbg, _realloc_dbg, _calloc_dbg, _expand_dbg, _msize_dbg, _free_dbg when MSVC team
// has added override-support for these.

#endif //NATEXEC_GLOBALMEM_OVERRIDE

BEGIN_NATIVE_EXECUTIVE

void* NatExecGlobalStaticAlloc::Allocate( void *refPtr, size_t memSize, size_t alignment )
{
#ifdef NATEXEC_GLOBALMEM_OVERRIDE
    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

    return _global_mem_alloc.get().Allocate( memSize, alignment );
#else
    return CRTHeapAllocator::Allocate( nullptr, memSize, alignment );
#endif //NATEXEC_GLOBALMEM_OVERRIDE
}

bool NatExecGlobalStaticAlloc::Resize( void *refPtr, void *memPtr, size_t memSize )
{
#ifdef NATEXEC_GLOBALMEM_OVERRIDE
    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
    assert( _global_mem_alloc.get().DoesOwnAllocation( memPtr ) == true );
#endif //_DEBUG

    return _global_mem_alloc.get().SetAllocationSize( memPtr, memSize );
#else

    // TODO: actually implement this using the Win32 flag for HeapReAlloc:
    // HEAP_REALLOC_IN_PLACE_ONLY

    return CRTHeapAllocator::Resize( nullptr, memPtr, memSize );
#endif //NATEXEC_GLOBALMEM_OVERRIDE
}

void NatExecGlobalStaticAlloc::Free( void *refPtr, void *memPtr )
{
#ifdef NATEXEC_GLOBALMEM_OVERRIDE
    NativeExecutive::CUnfairMutexContext ctxMemLock( _global_memlock.GetMutex() );

#if defined(_DEBUG) && !defined(NATEXEC_NO_HEAPPTR_VERIFY)
    assert( _global_mem_alloc.get().DoesOwnAllocation( memPtr ) == true );
#endif //_DEBUG

    _global_mem_alloc.get().Free( memPtr );
#else
    CRTHeapAllocator::Free( nullptr, memPtr );
#endif //NATEXEC_GLOBALMEM_OVERRIDE
}

#ifdef NATEXEC_GLOBALMEM_OVERRIDE

// Global memory overrides module init.
void registerGlobalMemoryOverrides( void )
{
    // This module must be initialized before any other runtime object so that memory allocation goes directly
    // through it. This is usually achieved by overriding the application entry point symbol.

    initializeGlobalMemoryOverrides();
}

void unregisterGlobalMemoryOverrides( void )
{
    shutdownGlobalMemoryOverrides();
}

#endif //NATEXEC_GLOBALMEM_OVERRIDE

END_NATIVE_EXECUTIVE
