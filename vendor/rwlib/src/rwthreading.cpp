#include "StdInc.h"

#include "rwthreading.hxx"

using namespace NativeExecutive;

namespace rw
{

threadingEnvRegister_t threadingEnv;

inline threadingEnvironment* GetThreadingEnv( Interface *engineInterface )
{
    return threadingEnv.GetPluginStruct( (EngineInterface*)engineInterface );
}

inline void* GetRWLockObject( rwlock *theLock )
{
    return ( theLock );
}

// Read/Write lock implementation.
void rwlock::enter_read( void )
{
    ((CReadWriteLock*)GetRWLockObject( this ))->EnterCriticalReadRegion();
}

void rwlock::leave_read( void )
{
    ((CReadWriteLock*)GetRWLockObject( this ))->LeaveCriticalReadRegion();
}

void rwlock::enter_write( void )
{
    ((CReadWriteLock*)GetRWLockObject( this ))->EnterCriticalWriteRegion();
}

void rwlock::leave_write( void )
{
    ((CReadWriteLock*)GetRWLockObject( this ))->LeaveCriticalWriteRegion();
}

bool rwlock::try_enter_read( void )
{
    return ((CReadWriteLock*)GetRWLockObject( this ))->TryEnterCriticalReadRegion();
}

bool rwlock::try_enter_write( void )
{
    return ((CReadWriteLock*)GetRWLockObject( this ))->TryEnterCriticalWriteRegion();
}

inline void* GetReentrantRWLockObject( reentrant_rwlock *theLock )
{
    return ( theLock );
}

struct rwlock_implementation : public rwlock
{
    static inline size_t GetStructSize( threadingEnvironment *threadEnv )
    {
        return threadEnv->nativeMan->GetReadWriteLockStructSize();
    }

    static inline rwlock* Construct( threadingEnvironment *threadEnv, void *mem )
    {
        rwlock_implementation *rwlock = (rwlock_implementation*)threadEnv->nativeMan->CreatePlacedReadWriteLock( mem );

        return rwlock;
    }

    static inline void Destroy( threadingEnvironment *threadEnv, rwlock *theLock )
    {
        threadEnv->nativeMan->ClosePlacedReadWriteLock( (CReadWriteLock*)theLock );
    }
};

// Reentrant Read/Write lock implementation.
void reentrant_rwlock::enter_read( void )
{
    ((CThreadReentrantReadWriteLock*)GetReentrantRWLockObject( this ))->LockRead();
}

void reentrant_rwlock::leave_read( void )
{
    ((CThreadReentrantReadWriteLock*)GetReentrantRWLockObject( this ))->UnlockRead();
}

void reentrant_rwlock::enter_write( void )
{
    ((CThreadReentrantReadWriteLock*)GetReentrantRWLockObject( this ))->LockWrite();
}

void reentrant_rwlock::leave_write( void )
{
    ((CThreadReentrantReadWriteLock*)GetReentrantRWLockObject( this ))->UnlockWrite();
}

bool reentrant_rwlock::try_enter_read( void )
{
    return ((CThreadReentrantReadWriteLock*)GetReentrantRWLockObject( this ))->TryLockRead();
}

bool reentrant_rwlock::try_enter_write( void )
{
    return ((CThreadReentrantReadWriteLock*)GetReentrantRWLockObject( this ))->TryLockWrite();
}

struct reentrant_rwlock_implementation : public reentrant_rwlock
{
    static inline size_t GetStructSize( threadingEnvironment *threadEnv )
    {
        return threadEnv->nativeMan->GetThreadReentrantReadWriteLockStructSize();
    }

    static reentrant_rwlock* Construct( threadingEnvironment *threadEnv, void *mem )
    {
        reentrant_rwlock_implementation *rwlock =
            (reentrant_rwlock_implementation*)threadEnv->nativeMan->CreatePlacedThreadReentrantReadWriteLock( mem );

        return rwlock;
    }

    static void Destroy( threadingEnvironment *threadEnv, reentrant_rwlock *theLock )
    {
        threadEnv->nativeMan->ClosePlacedThreadReentrantReadWriteLock( (CThreadReentrantReadWriteLock*)theLock );
    }
};

void unfair_mutex::enter( void )
{
    ((CUnfairMutex*)this)->lock();
}

void unfair_mutex::leave( void )
{
    ((CUnfairMutex*)this)->unlock();
}

struct unfair_mutex_implementation : public unfair_mutex
{
    static inline size_t GetStructSize( threadingEnvironment *threadEnv )
    {
        return threadEnv->nativeMan->GetUnfairMutexStructSize();
    }

    static unfair_mutex* Construct( threadingEnvironment *threadEnv, void *mem )
    {
        return (unfair_mutex_implementation*)threadEnv->nativeMan->CreatePlacedUnfairMutex( mem );
    }

    static void Destroy( threadingEnvironment *threadEnv, unfair_mutex *mtx )
    {
        threadEnv->nativeMan->ClosePlacedUnfairMutex( (CUnfairMutex*)mtx );
    }
};

// Lock creation API.
rwlock* CreateReadWriteLock( Interface *engineInterface )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    size_t lockSize = rwlock_implementation::GetStructSize( threadEnv );

    void *lockMem = engineInterface->MemAllocate( lockSize );

    if ( !lockMem )
    {
        return nullptr;
    }

    try
    {
        return rwlock_implementation::Construct( threadEnv, lockMem );
    }
    catch( ... )
    {
        engineInterface->MemFree( lockMem );

        throw;
    }
}

void CloseReadWriteLock( Interface *engineInterface, rwlock *theLock )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    rwlock_implementation::Destroy( threadEnv, theLock );

    engineInterface->MemFree( theLock );
}

size_t GetReadWriteLockStructSize( Interface *engineInterface )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    return rwlock_implementation::GetStructSize( threadEnv );
}

rwlock* CreatePlacedReadWriteLock( Interface *engineInterface, void *mem )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    return rwlock_implementation::Construct( threadEnv, mem );
}

void ClosePlacedReadWriteLock( Interface *engineInterface, rwlock *theLock )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    rwlock_implementation::Destroy( threadEnv, theLock );
}

reentrant_rwlock* CreateReentrantReadWriteLock( Interface *engineInterface )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    size_t lockSize = reentrant_rwlock_implementation::GetStructSize( threadEnv );

    void *lockMem = engineInterface->MemAllocate( lockSize );

    if ( !lockMem )
    {
        return nullptr;
    }

    try
    {
        return reentrant_rwlock_implementation::Construct( threadEnv, lockMem );
    }
    catch( ... )
    {
        engineInterface->MemFree( lockMem );

        throw;
    }
}

void CloseReentrantReadWriteLock( Interface *engineInterface, reentrant_rwlock *theLock )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    reentrant_rwlock_implementation::Destroy( threadEnv, theLock );

    engineInterface->MemFree( theLock );
}

size_t GetReeentrantReadWriteLockStructSize( Interface *engineInterface )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    return reentrant_rwlock_implementation::GetStructSize( threadEnv );
}

reentrant_rwlock* CreatePlacedReentrantReadWriteLock( Interface *engineInterface, void *mem )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    return reentrant_rwlock_implementation::Construct( threadEnv, mem );
}

void ClosePlacedReentrantReadWriteLock( Interface *engineInterface, reentrant_rwlock *theLock )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    reentrant_rwlock_implementation::Destroy( threadEnv, theLock );
}

unfair_mutex* CreateUnfairMutex( Interface *engineInterface )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    size_t lockSize = unfair_mutex_implementation::GetStructSize( threadEnv );

    void *lockMem = engineInterface->MemAllocate( lockSize );

    if ( lockMem == nullptr )
    {
        return nullptr;
    }

    try
    {
        return unfair_mutex_implementation::Construct( threadEnv, lockMem );
    }
    catch( ... )
    {
        engineInterface->MemFree( lockMem );

        throw;
    }
}

void CloseUnfairMutex( Interface *engineInterface, unfair_mutex *mtx )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    unfair_mutex_implementation::Destroy( threadEnv, mtx );

    engineInterface->MemFree( mtx );
}

size_t GetUnfairMutexStructSize( Interface *engineInterface )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    return unfair_mutex_implementation::GetStructSize( threadEnv );
}

unfair_mutex* CreatePlacedUnfairMutex( Interface *engineInterface, void *mem )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    return unfair_mutex_implementation::Construct( threadEnv, mem );
}

void ClosePlacedUnfairMutex( Interface *engineInterface, unfair_mutex *mtx )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    unfair_mutex_implementation::Destroy( threadEnv, mtx );
}

// Thread API.
struct nativeexec_traverse
{
    threadEntryPoint_t ep;
    Interface *engineInterface;
    void *ud;
};

static void nativeexec_thread_entry( CExecThread *thisThread, void *ud )
{
    nativeexec_traverse *info = (nativeexec_traverse*)ud;

    threadEntryPoint_t ep = info->ep;
    Interface *engineInterface = info->engineInterface;
    void *user_ud = info->ud;

    // We do not need the userdata anymore.
    engineInterface->MemFree( ud );

    try
    {
        // Call into the usermode callback.
        ep( (thread_t)thisThread, engineInterface, user_ud );
    }
    catch( RwException& except )
    {
        // Post a warning that a thread ended in an exception.
        engineInterface->PushWarning( "fatal thread exception: " + except.message );

        // We cleanly terminate.
    }
}

thread_t MakeThread( Interface *engineInterface, threadEntryPoint_t entryPoint, void *ud )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    nativeexec_traverse *cb_info = (nativeexec_traverse*)engineInterface->MemAllocate( sizeof( nativeexec_traverse ) );
    cb_info->ep = entryPoint;
    cb_info->engineInterface = engineInterface;
    cb_info->ud = ud;

    CExecThread *threadHandle = threadEnv->nativeMan->CreateThread( nativeexec_thread_entry, cb_info );

    return (thread_t)threadHandle;

    // Threads are created suspended. You have to kick them off using rw::ResumeThread.

    // Remember to close thread handles.
}

void CloseThread( Interface *engineInterface, thread_t threadHandle )
{
    // WARNING: doing this is an unsafe operation in certain circumstances.
    // always cleanly terminate threads instead of doing this.

    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    CExecThread *theThread = (CExecThread*)threadHandle;

    threadEnv->nativeMan->CloseThread( theThread );
}

thread_t AcquireThread( Interface *engineInterface, thread_t threadHandle )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    CExecThread *theThread = (CExecThread*)threadHandle;

    return (thread_t)threadEnv->nativeMan->AcquireThread( theThread );
}

bool ResumeThread( Interface *engineInterface, thread_t threadHandle )
{
    CExecThread *theThread = (CExecThread*)threadHandle;

    return theThread->Resume();
}

bool SuspendThread( Interface *engineInterface, thread_t threadHandle )
{
    CExecThread *theThread = (CExecThread*)threadHandle;

    return theThread->Suspend();
}

void JoinThread( Interface *engineInterface, thread_t threadHandle )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    CExecThread *theThread = (CExecThread*)threadHandle;

    threadEnv->nativeMan->JoinThread( theThread );
}

void TerminateThread( Interface *engineInterface, thread_t threadHandle, bool waitOnRemote )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    CExecThread *theThread = (CExecThread*)threadHandle;

    threadEnv->nativeMan->TerminateThread( theThread, waitOnRemote );
}

void CheckThreadHazards( Interface *engineInterface )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    threadEnv->nativeMan->CheckHazardCondition();
}

void ThreadingMarkAsTerminating( EngineInterface *engineInterface )
{
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    threadEnv->nativeMan->MarkAsTerminating();
}

void PurgeActiveThreadingObjects( EngineInterface *engineInterface )
{
    // USE WITH FRIGGIN CAUTION.
    threadingEnvironment *threadEnv = GetThreadingEnv( engineInterface );

    threadEnv->nativeMan->PurgeActiveRuntimes();
    threadEnv->nativeMan->PurgeActiveThreads();
}

void* GetThreadingNativeManager( Interface *intf )
{
    EngineInterface *engineInterface = (EngineInterface*)intf;

    return GetNativeExecutive( engineInterface );
}

// Module initialization.
void registerThreadingEnvironment( void )
{
    threadingEnv.RegisterPlugin( engineFactory );
}

};
