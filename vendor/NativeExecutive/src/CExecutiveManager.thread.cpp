/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.thread.cpp
*  PURPOSE:     Thread abstraction layer for MTA
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include "StdInc.h"

#ifdef __linux__
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <linux/futex.h>
#include <sched.h>
#include <sys/wait.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#define gettid() syscall(SYS_gettid)
#define tkill(tid, sig) syscall(SYS_tkill, tid, sig)
#define futex(uaddr, futex_op, val, timeout, uaddr2, val3) \
    syscall(SYS_futex, uaddr, futex_op, val, timeout, uaddr2, val3)
#endif //__linux__

#include "CExecutiveManager.hazards.hxx"
#include "CExecutiveManager.native.hxx"
#include "CExecutiveManager.eventplugin.hxx"
#include "PluginUtils.hxx"

#include <sdk/Vector.h>
#include <sdk/Map.h>

#include "CExecutiveManager.thread.hxx"

#include "internal/CExecutiveManager.unfairmtx.internal.h"
#include "internal/CExecutiveManager.sem.internal.h"

BEGIN_NATIVE_EXECUTIVE

// We need some events for unfair mutexes.
static optional_struct_space <EventPluginRegister> _runningThreadListEventRegister;
static optional_struct_space <EventPluginRegister> _threadRuntimeReferenceLockEventRegister;
static optional_struct_space <EventPluginRegister> _tlsThreadToNativeInfoLockEventRegister;

#ifdef __linux__

static optional_struct_space <EventPluginRegister> _threadsToTermLockEventRegister;
static optional_struct_space <EventPluginRegister> _threadsToTermSemEventRegister;

#endif //__linux__

// Events for shared stuff.
optional_struct_space <EventPluginRegister> privateThreadEnvThreadReferenceLockEventRegister;
optional_struct_space <EventPluginRegister> privateThreadEnvThreadPluginsLockEventRegister;

// The private thread environment that is public to the entire library.
optional_struct_space <privateThreadEnvRegister_t> privateThreadEnv;

// We need a type for the thread ID.
#ifdef _WIN32
typedef DWORD threadIdType;
#elif defined(__linux__)
typedef pid_t threadIdType;
#else
#error Missing definition of the platform native thread id
#endif //CROSS PLATFORM CODE

struct nativeThreadPlugin
{
#ifdef _WIN32
    // THESE FIELDS MUST NOT BE MODIFIED.
    // (they are referenced from Assembler)
    Fiber *terminationReturn;   // if not nullptr, the thread yields to this state when it successfully terminated.
#endif //_WIN32

    nativeThreadPlugin( CExecThreadImpl *thread, CExecutiveManagerNative *manager );

    // You are free to modify from here.
    struct nativeThreadPluginInterface *manager;
    CExecThreadImpl *self;
    threadIdType codeThread;
#ifdef _WIN32
    HANDLE hThread;
#elif defined(__linux__)
    void *userStack;
    size_t userStackSize;
    // True if the thread has been started; used to simulate the first Resume that is necessary.
    volatile bool hasThreadStarted;
#else
#error No thread handle members implementation for this platform
#endif //CROSS PLATFORM CODE
    CUnfairMutexImpl mtxThreadLock;
    std::atomic <eThreadStatus> status;
    volatile bool hasThreadBeenInitialized;

    RwListEntry <nativeThreadPlugin> node;
};

struct thread_id_fetch
{
#ifdef _WIN32
    HANDLE hRunningThread = GetCurrentThread();
    threadIdType idRunningThread = GetThreadId( hRunningThread );
#elif defined(__linux__)
    threadIdType codeThread = gettid();
#else
#error no thread identification fetch implementation
#endif //CROSS PLATFORM CODE

    AINLINE bool is_current( nativeThreadPlugin *thread ) const
    {
        return ( thread->codeThread == get_current_id() );
    }

    AINLINE threadIdType get_current_id( void ) const
    {
#ifdef _WIN32
        return this->idRunningThread;
#elif defined(__linux__)
        return this->codeThread;
#endif
    }
};

struct nativeThreadPluginInterface : public privateThreadEnvironment::threadPluginContainer_t::pluginInterface
{
    RwList <nativeThreadPlugin> runningThreads;
    CUnfairMutexImpl mtxRunningThreadList;

    // Threads must not give up their runtime reference while the thread list is purged.
    // So introduce a lock.
    CUnfairMutexImpl mtxRuntimeReferenceRelease;

    // Need to have a per-thread mutex.
    DynamicEventPluginRegister <privateThreadEnvironment::threadPluginContainer_t> mtxThreadLockEventRegister;

    // Storage of native-thread to manager-struct relationship.
    eir::Map <threadIdType, nativeThreadPlugin*, NatExecStandardObjectAllocator> tlsThreadToNativeInfo;
    CUnfairMutexImpl mtxTLSThreadToNativeInfo;

#ifdef _WIN32
    // Nothing.
#elif defined(__linux__)
    CExecutiveManagerNative *self;
    char freestackmem_threadStack[ 1024 * sizeof(void*) ];  // has to be HUGE because the stack aint a joke.
    pid_t freestackmem_procid;

    eir::Vector <nativeThreadPlugin*, NatExecStandardObjectAllocator> threadsToTerm;
    CUnfairMutexImpl mtxThreadsToTermLock;
    CSemaphoreImpl semThreadsToTerm;

    size_t sysPageSize;

    // Events for certain thread things.
    DynamicEventPluginRegister <privateThreadEnvironment::threadPluginContainer_t> threadStartEventRegister;
    DynamicEventPluginRegister <privateThreadEnvironment::threadPluginContainer_t> threadRunningEventRegister;
#else
#error missing thread native TLS implementation
#endif //_WIN32

    bool isTerminating;

    // Safe runtime-reference releasing function.
    AINLINE void thread_end_of_life( CExecutiveManagerNative *manager, CExecThreadImpl *theThread, nativeThreadPlugin *nativeInfo )
    {
        CUnfairMutexContext mtxReleaseRuntimeReference( this->mtxRuntimeReferenceRelease );

        nativeInfo->status = THREAD_TERMINATED;

#ifdef __linux__
        // Report end of runtime using the event.
        {
            CEvent *eventRunning = this->threadRunningEventRegister.GetEvent( theThread );

            eventRunning->Set( false );
        }
#endif //__linux__

        manager->CloseThreadNative( theThread );
    }

#ifdef __linux__

    static void futex_wait_thread( pid_t *tid )
    {
        while ( true )
        {
            pid_t cur_tid = *tid;

            if ( cur_tid == 0 )
            {
                break;
            }

            int fut_error = futex( tid, FUTEX_WAIT, cur_tid, nullptr, nullptr, 0 );
            (void)fut_error;
        }
    }

    // On Linux we need a special signal thread for releasing stack memory after threads have terminated.
    // This is because stack memory is not handled by the OS itself, unlike in Windows.
    static int _linux_freeStackMem_thread( void *ud )
    {
        nativeThreadPluginInterface *nativeInfo = (nativeThreadPluginInterface*)ud;

        CExecutiveManagerNative *nativeMan = nativeInfo->self;

        while ( !nativeInfo->isTerminating )
        {
            nativeInfo->semThreadsToTerm.Decrement();

            // Check for any thread that needs cleanup.
            {
                CUnfairMutexContext ctxFetchTerm( nativeInfo->mtxThreadsToTermLock );

                for ( nativeThreadPlugin *termItem : nativeInfo->threadsToTerm )
                {
                    static_assert( sizeof(pid_t) == 4, "invalid machine pid_t word size" );

                    // Wait for the exit using our futex.
                    futex_wait_thread( &termItem->codeThread );

                    // Free the thread stack.
                    if ( void *stack = termItem->userStack )
                    {
                        int err_unmap = munmap( stack, termItem->userStackSize );

                        assert( err_unmap == 0 );

                        termItem->userStack = nullptr;
                    }

                    // Release the thread "runtime reference".
                    nativeInfo->thread_end_of_life( nativeMan, termItem->self, termItem );
                }

                nativeInfo->threadsToTerm.Clear();
            }
        }

        return 0;
    }

#endif //__linux__

    inline nativeThreadPluginInterface( CExecutiveManagerNative *nativeMan ) :
        mtxRunningThreadList( _runningThreadListEventRegister.get().GetEvent( nativeMan ) )
        , mtxRuntimeReferenceRelease( _threadRuntimeReferenceLockEventRegister.get().GetEvent( nativeMan ) )
        , tlsThreadToNativeInfo( eir::constr_with_alloc::DEFAULT, nativeMan )
        , mtxTLSThreadToNativeInfo( _tlsThreadToNativeInfoLockEventRegister.get().GetEvent( nativeMan ) )
#ifdef __linux__
        , threadsToTerm( eir::constr_with_alloc::DEFAULT, nativeMan )
        , mtxThreadsToTermLock( _threadsToTermLockEventRegister.get().GetEvent( nativeMan ) )
        , semThreadsToTerm( _threadsToTermSemEventRegister.get().GetEvent( nativeMan ) )
#endif //__linux__
    {
        return;
    }

    inline void Initialize( CExecutiveManagerNative *nativeMan )
    {
        privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( nativeMan );

        assert( threadEnv != nullptr );

        mtxThreadLockEventRegister.RegisterPlugin( threadEnv->threadPlugins );

#ifdef _WIN32
        // Nothing.
#elif defined(__linux__)
        threadStartEventRegister.RegisterPlugin( threadEnv->threadPlugins );
        threadRunningEventRegister.RegisterPlugin( threadEnv->threadPlugins );
#else
#error no implementation for native thread plugin interface init
#endif //CROSS PLATFORM CODE

        this->isTerminating = false;

#ifdef __linux__
        long pageSize = sysconf(_SC_PAGESIZE);

        assert( pageSize > 0 );

        this->sysPageSize = (size_t)pageSize;
        this->self = nativeMan;

        int maintainThreadSucc =
            clone(
                _linux_freeStackMem_thread, this->freestackmem_threadStack + countof( this->freestackmem_threadStack ),
                CLONE_SIGHAND|CLONE_THREAD|CLONE_PARENT|CLONE_VM|CLONE_CHILD_CLEARTID,
                this, nullptr, nullptr, &this->freestackmem_procid
            );

        assert( maintainThreadSucc > 0 );

        this->freestackmem_procid = (pid_t)maintainThreadSucc;
#endif //__linux__
    }

    inline void Shutdown( CExecutiveManagerNative *nativeMan )
    {
#ifdef _WIN32
        // Nothing.
#elif defined(__linux__)
        // Wait for maintainer thread termination.
        this->semThreadsToTerm.Increment();

        futex_wait_thread( &this->freestackmem_procid );

        // We simply forget the TLS mappings. No big deal.

        // Unregister thread runtime events.
        threadRunningEventRegister.UnregisterPlugin();
        threadStartEventRegister.UnregisterPlugin();
#else
#error no implementation for native thread plugin interface shutdown
#endif //CROSS PLATFORM CODE

        // Shutdown the per-thread plugins.
        mtxThreadLockEventRegister.UnregisterPlugin();
    }

    inline void TlsSetCurrentThreadInfo( nativeThreadPlugin *info )
    {
        thread_id_fetch id;

        CUnfairMutexContext ctxSetTLSInfo( this->mtxTLSThreadToNativeInfo );

        if ( info == nullptr )
        {
            tlsThreadToNativeInfo.RemoveByKey( id.get_current_id() );
        }
        else
        {
            tlsThreadToNativeInfo[ id.get_current_id() ] = info;
        }
    }

    inline nativeThreadPlugin* TlsGetCurrentThreadInfo( void )
    {
        nativeThreadPlugin *plugin = nullptr;

        {
            thread_id_fetch id;

            CUnfairMutexContext ctxGetTLSInfo( this->mtxTLSThreadToNativeInfo );

            auto findIter = tlsThreadToNativeInfo.Find( id.get_current_id() );

            if ( findIter != nullptr )
            {
                plugin = findIter->GetValue();
            }
        }

        return plugin;
    }

    inline void TlsCleanupThreadInfo( nativeThreadPlugin *info )
    {
        tlsThreadToNativeInfo.RemoveByKey( info->codeThread );
    }

    static void _ThreadProcCPP( nativeThreadPlugin *info )
    {
        CExecThreadImpl *threadInfo = info->self;

        // Put our executing thread information into our TLS value.
        info->manager->TlsSetCurrentThreadInfo( info );

        // Make sure we intercept termination requests!
        try
        {
            {
                CUnfairMutexContext mtxThreadLock( info->mtxThreadLock );

                // We are properly initialized now.
                info->hasThreadBeenInitialized = true;
            }

            // Enter the routine.
            threadInfo->entryPoint( threadInfo, threadInfo->userdata );
        }
        catch( ... )
        {
            // We have to safely quit.
        }

        // We are terminating.
        {
            CUnfairMutexContext mtxThreadLock( info->mtxThreadLock );
            CUnfairMutexContext mtxThreadState( threadInfo->mtxThreadStatus );

            info->status = THREAD_TERMINATING;
        }

        // Leave this proto. The native implementation has the job to set us terminated.
    }

#ifdef _WIN32
    // This is a C++ proto. We must leave into a ASM proto to finish operation.
    static DWORD WINAPI _Win32_ThreadProcCPP( LPVOID param )
    {
        _ThreadProcCPP( (nativeThreadPlugin*)param );

        return ERROR_SUCCESS;
    }
#endif

    // REQUIREMENT: WRITE ACCESS on lockThreadStatus of threadInfo handle
    void RtlTerminateThread( CExecutiveManager *manager, nativeThreadPlugin *threadInfo, CUnfairMutexContext& ctxLock, bool waitOnRemote )
    {
        CExecThreadImpl *theThread = threadInfo->self;

        assert( theThread->isRemoteThread == false );

        // If we are not the current thread, we must do certain precautions.
        bool isCurrentThread = theThread->IsCurrent();

        // Set our status to terminating.
        // The moment we set this the thread starts terminating.
        {
            CUnfairMutexContext ctxThreadStatus( theThread->mtxThreadStatus );

            threadInfo->status = THREAD_TERMINATING;
        }

        // Depends on whether we are the current thread or not.
        if ( isCurrentThread )
        {
            // Just do the termination.
            throw threadTerminationException( theThread );
        }
        else
        {
            // TODO: make hazard management thread safe, because I think there are some issues.

            // Terminate all possible hazards.
            {
                executiveHazardManagerEnv *hazardEnv = executiveHazardManagerEnvRegister.get().GetPluginStruct( (CExecutiveManagerNative*)manager );

                if ( hazardEnv )
                {
                    hazardEnv->PurgeThreadHazards( theThread );
                }
            }

            // We do not need the lock anymore.
            ctxLock.release();

            if ( waitOnRemote )
            {
#ifdef __linux__
                CEvent *evtRunning = this->threadRunningEventRegister.GetEvent( theThread );
#endif //__linux__

                // Wait for thread termination.
                while ( threadInfo->status != THREAD_TERMINATED )
                {
                    // Wait till the thread has really finished.
#ifdef _WIN32
                    WaitForSingleObject( threadInfo->hThread, INFINITE );
#elif defined(__linux__)
                    evtRunning->Wait();
#else
#error no thread wait for termination implementation
#endif //CROSS PLATFORM CODE
                }

                // If we return here, the thread must be terminated.
            }

            // TODO: allow safe termination of suspended threads.
        }

        // If we were the current thread, we cannot reach this point.
        assert( isCurrentThread == false );
    }

    bool OnPluginConstruct( CExecThreadImpl *thread, privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t pluginOffset, privateThreadEnvironment::threadPluginContainer_t::pluginDescriptor id ) override;
    void OnPluginDestruct( CExecThreadImpl *thread, privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t pluginOffset, privateThreadEnvironment::threadPluginContainer_t::pluginDescriptor id ) override;
};

#ifdef _WIN32
// Assembly routines for important thread events.
extern "C" DWORD WINAPI nativeThreadPluginInterface_ThreadProcCPP( LPVOID param )
{
    // This is an assembler compatible entry point.
    return nativeThreadPluginInterface::_Win32_ThreadProcCPP( param );
}

extern "C" void WINAPI nativeThreadPluginInterface_OnNativeThreadEnd( nativeThreadPlugin *nativeInfo )
{
    // The assembler finished using us, so do clean up work.
    CExecThreadImpl *theThread = nativeInfo->self;

    CExecutiveManagerNative *manager = theThread->manager;

    // NOTE: this is OKAY on Windows because we do not allocate the stack space ourselves!
    // On Linux for example we have to free the stack space using a different thread.

    // Officially terminated now.
    nativeInfo->manager->thread_end_of_life( manager, theThread, nativeInfo );
}
#elif defined(__linux__)

static int _linux_threadEntryPoint( void *in_ptr )
{
    nativeThreadPlugin *info = (nativeThreadPlugin*)in_ptr;

    CExecThreadImpl *nativeThread = info->self;

    nativeThreadPluginInterface *nativeMan = info->manager;

    // Wait for the real thread start event.
    {
        CEvent *eventStart = nativeMan->threadStartEventRegister.GetEvent( nativeThread );

        eventStart->Wait();
    }

    // Invoke thread runtime.
    {
        nativeThreadPluginInterface::_ThreadProcCPP( info );

        // There is a difference in implementation between Windows and Linux in that thread
        // runtime prematurely is reported finished using waiting-semantics under Linux.
        // This is not a problem for as long as things are thread-safe.
    }

    // We finished using the thread, so clean up.
    // This is done by notifying the termination runtime.
    {
        CUnfairMutexContext ctxPushTermThread( nativeMan->mtxThreadsToTermLock );

        nativeMan->threadsToTerm.AddToBack( info );

        nativeMan->semThreadsToTerm.Increment();
    }

    return 0;
}

#endif //CROSS PLATFORM CODE

bool nativeThreadPluginInterface::OnPluginConstruct( CExecThreadImpl *thread, privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t pluginOffset, privateThreadEnvironment::threadPluginContainer_t::pluginDescriptor id )
{
    // Cannot create threads if we are terminating!
    if ( this->isTerminating )
    {
        return false;
    }

    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)thread->manager;

    void *info_ptr = privateThreadEnvironment::threadPluginContainer_t::RESOLVE_STRUCT <void> ( thread, pluginOffset );

    nativeThreadPlugin *info = new (info_ptr) nativeThreadPlugin( thread, nativeMan );

    // Give ourselves a self reference pointer.
    info->self = thread;
    info->manager = this;

#ifdef _WIN32
    // If we are not a remote thread...
    HANDLE hOurThread = nullptr;

    if ( !thread->isRemoteThread )
    {
        // ... create a local thread!
        DWORD threadIdOut;

        LPTHREAD_START_ROUTINE startRoutine = nullptr;

#if defined(_M_IX86)
        startRoutine = (LPTHREAD_START_ROUTINE)_thread86_procNative;
#elif defined(_M_AMD64)
        startRoutine = (LPTHREAD_START_ROUTINE)_thread64_procNative;
#endif

        if ( startRoutine == nullptr )
            return false;

        HANDLE hThread = ::CreateThread( nullptr, (SIZE_T)thread->stackSize, startRoutine, info, CREATE_SUSPENDED, &threadIdOut );

        if ( hThread == nullptr )
            return false;

        hOurThread = hThread;
    }
    info->hThread = hOurThread;
    info->codeThread = GetThreadId( hOurThread );
#elif defined(__linux__)
    // Need to initialize the state events.
    CEvent *eventStartThread = this->threadStartEventRegister.GetEvent( thread );
    eventStartThread->Set( false );
    CEvent *eventRunningThread = this->threadRunningEventRegister.GetEvent( thread );
    eventRunningThread->Set( false );

    pid_t our_thread_id = -1;
    void *our_userStack = nullptr;
    size_t our_userStackSize = 0;

    if ( !thread->isRemoteThread )
    {
        // On linux we use the native clone syscall to create a thread.
        size_t theStackSize = thread->stackSize;

        // If the user was undecided, then we just set it to some good value instead.
        if ( theStackSize == 0 )
        {
            theStackSize = 2 << 17;
        }

        // Make sure the stack size is aligned properly.
        size_t sysPageSize = this->sysPageSize;
        theStackSize = ALIGN( theStackSize, sysPageSize, sysPageSize );

        void *stack_mem = mmap( nullptr, theStackSize, PROT_READ|PROT_WRITE, MAP_UNINITIALIZED|MAP_PRIVATE|MAP_STACK|MAP_ANONYMOUS, -1, 0 );

        if ( stack_mem == MAP_FAILED )
        {
            return false;
        }

        // Initially take the runtime lock.
        // This is to prevent the thread from starting till the user wants to.
        eventStartThread->Set( true );
        eventRunningThread->Set( true );

        // We actually return the end of stack pointer, because we assume stack __always__ grows downwards.
        // TODO: this is not true all the time.
        void *stack_beg_ptr = ( (char*)stack_mem + theStackSize );

        int clone_res =
            clone(
                _linux_threadEntryPoint, stack_beg_ptr,
                /*SIGCHLD|CLONE_PTRACE|*/CLONE_SIGHAND|CLONE_THREAD|CLONE_PARENT|CLONE_VM|CLONE_CHILD_CLEARTID|CLONE_FILES|CLONE_FS,
                info, nullptr, nullptr, &info->codeThread
            );

        if ( clone_res == -1 )
        {
            eventStartThread->Set( false );
            eventRunningThread->Set( false );

            munmap( stack_mem, theStackSize );
            return false;
        }

        our_thread_id = (pid_t)clone_res;
        our_userStack = stack_mem;
        our_userStackSize = theStackSize;

        info->hasThreadStarted = false;
    }
    else
    {
        // Since we do not control this thread we just return nothing.
        info->hasThreadStarted = true;
    }
    info->codeThread = our_thread_id;
    info->userStack = our_userStack;
    info->userStackSize = our_userStackSize;
#else
#error No thread creation implementation
#endif //CROSS PLATFORM CODE

    // NOTE: we initialize remote threads in the GetCurrentThread routine!

#ifdef _WIN32
    // This field is used by the runtime dispatcher to execute a "controlled return"
    // from different threads.
    info->terminationReturn = nullptr;
#endif //_WIN32

    info->hasThreadBeenInitialized = false;

    // We must let the thread terminate itself.
    // So it is mandatory to give it a reference,
    // also called the "runtime reference".
    thread->refCount++;

    // We assume the thread is (always) running if its a remote thread.
    // Otherwise we know that it starts suspended.
    info->status = ( !thread->isRemoteThread ) ? THREAD_SUSPENDED : THREAD_RUNNING;

    // Add it to visibility.
    {
        CUnfairMutexContext ctxThreadList( this->mtxRunningThreadList );

        LIST_INSERT( runningThreads.root, info->node );
    }
    return true;
}

void nativeThreadPluginInterface::OnPluginDestruct( CExecThreadImpl *thread, privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t pluginOffset, privateThreadEnvironment::threadPluginContainer_t::pluginDescriptor id )
{
    nativeThreadPlugin *info = privateThreadEnvironment::threadPluginContainer_t::RESOLVE_STRUCT <nativeThreadPlugin> ( thread, pluginOffset );

    // We must destroy the handle only if we are terminated.
    if ( !thread->isRemoteThread )
    {
        assert( info->status == THREAD_TERMINATED );
    }

    // Remove the thread from visibility.
    this->TlsCleanupThreadInfo( info );
    {
        CUnfairMutexContext ctxThreadList( this->mtxRunningThreadList );

        LIST_REMOVE( info->node );
    }

    // Close OS resources.
#ifdef _WIN32
    CloseHandle( info->hThread );
#elif defined(__linux__)
    // We should have released our stack already.
    assert( info->userStack == nullptr );
#else
#error No implementation for thread info shutdown
#endif //CROSS PLATFORM CODE

    // Destroy the plugin.
    info->~nativeThreadPlugin();
}

// todo: add other OSes too when it becomes necessary.

struct privateNativeThreadEnvironment
{
    nativeThreadPluginInterface _nativePluginInterface;

    privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t nativePluginOffset;

    inline privateNativeThreadEnvironment( CExecutiveManagerNative *natExec ) : _nativePluginInterface( natExec )
    {
        return;
    }

    inline void Initialize( CExecutiveManagerNative *manager )
    {
        privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( manager );

        assert( threadEnv != nullptr );

        _nativePluginInterface.Initialize( manager );

        this->nativePluginOffset =
            threadEnv->threadPlugins.RegisterPlugin( sizeof( nativeThreadPlugin ), THREAD_PLUGIN_NATIVE, &_nativePluginInterface );
    }

    inline void Shutdown( CExecutiveManagerNative *manager )
    {
        privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( manager );

        assert( threadEnv != nullptr );

        // Notify ourselves that we are terminating.
        _nativePluginInterface.isTerminating = true;

        // Shutdown all currently yet active threads.
        while ( !LIST_EMPTY( manager->threads.root ) )
        {
            CExecThreadImpl *thread = LIST_GETITEM( CExecThreadImpl, manager->threads.root.next, managerNode );

            manager->CloseThread( thread );
        }

        if ( privateThreadEnvironment::threadPluginContainer_t::IsOffsetValid( this->nativePluginOffset ) )
        {
            threadEnv->threadPlugins.UnregisterPlugin( this->nativePluginOffset );
        }

        _nativePluginInterface.Shutdown( manager );
    }
};

static optional_struct_space <PluginDependantStructRegister <privateNativeThreadEnvironment, executiveManagerFactory_t>> privateNativeThreadEnvironmentRegister;

nativeThreadPlugin::nativeThreadPlugin( CExecThreadImpl *thread, CExecutiveManagerNative *natExec ) :
    mtxThreadLock( privateNativeThreadEnvironmentRegister.get().GetPluginStruct( natExec )->_nativePluginInterface.mtxThreadLockEventRegister.GetEvent( thread ) )
{
    return;
}

inline nativeThreadPlugin* GetNativeThreadPlugin( CExecutiveManagerNative *manager, CExecThreadImpl *theThread )
{
    privateNativeThreadEnvironment *nativeThreadEnv = privateNativeThreadEnvironmentRegister.get().GetPluginStruct( manager );

    if ( nativeThreadEnv )
    {
        return privateThreadEnvironment::threadPluginContainer_t::RESOLVE_STRUCT <nativeThreadPlugin> ( theThread, nativeThreadEnv->nativePluginOffset );
    }

    return nullptr;
}

inline const nativeThreadPlugin* GetConstNativeThreadPlugin( const CExecutiveManager *manager, const CExecThreadImpl *theThread )
{
    const privateNativeThreadEnvironment *nativeThreadEnv = privateNativeThreadEnvironmentRegister.get().GetConstPluginStruct( (const CExecutiveManagerNative*)manager );

    if ( nativeThreadEnv )
    {
        return privateThreadEnvironment::threadPluginContainer_t::RESOLVE_STRUCT <nativeThreadPlugin> ( theThread, nativeThreadEnv->nativePluginOffset );
    }

    return nullptr;
}

// For the Windows.h header gunk.
#undef CreateEvent

CExecThreadImpl::CExecThreadImpl( CExecutiveManagerNative *manager, bool isRemoteThread, void *userdata, size_t stackSize, threadEntryPoint_t entryPoint )
    : mtxThreadStatus( manager->CreateEvent() )
{
    this->manager = manager;
    this->isRemoteThread = isRemoteThread;
    this->userdata = userdata;
    this->stackSize = stackSize;
    this->entryPoint = entryPoint;

    // During construction we must not have a reference to ourselves.
    this->refCount = 0;

    LIST_INSERT( manager->threads.root, managerNode );
}

CExecThreadImpl::~CExecThreadImpl( void )
{
    //CExecutiveManagerNative *nativeMan = this->manager;

    assert( this->refCount == 0 );

    LIST_REMOVE( managerNode );

    // Clean-up the event of the mutex.
    manager->CloseEvent( this->mtxThreadStatus.get_event() );
}

CExecutiveManager* CExecThread::GetManager( void )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)this;

    return nativeThread->manager;
}

eThreadStatus CExecThreadImpl::GetStatusNative( void ) const
{
    eThreadStatus status = THREAD_TERMINATED;

    const nativeThreadPlugin *info = GetConstNativeThreadPlugin( this->manager, this );

    if ( info )
    {
        status = info->status;
    }

    return status;
}

eThreadStatus CExecThread::GetStatus( void ) const
{
    const CExecThreadImpl *nativeThread = (const CExecThreadImpl*)this;

    return nativeThread->GetStatusNative();
}

// WARNING: terminating threads in general is very naughty and causes shit to go haywire!
// No matter what thread state, this function guarrantees to terminate a thread cleanly according to
// C++ stack unwinding logic!
// Termination of a thread is allowed to be executed by another thread (e.g. the "main" thread).
// NOTE: logic has been changed to be secure. now proper terminating depends on a contract between runtime
// and the NativeExecutive library.
bool CExecThread::Terminate( bool waitOnRemote )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)this;

    bool returnVal = false;

    nativeThreadPlugin *info = GetNativeThreadPlugin( nativeThread->manager, nativeThread );

    if ( info && info->status != THREAD_TERMINATED )
    {
        // We cannot terminate a terminating thread.
        if ( info->status != THREAD_TERMINATING )
        {
            CUnfairMutexContext ctxThreadLock( info->mtxThreadLock );

            if ( info->status != THREAD_TERMINATING && info->status != THREAD_TERMINATED )
            {
                // Termination depends on what kind of thread we face.
                if ( nativeThread->isRemoteThread )
                {
                    bool hasTerminated = false;

#ifdef _WIN32
                    // Remote threads must be killed just like that.
                    BOOL success = TerminateThread( info->hThread, ERROR_SUCCESS );

                    hasTerminated = ( success == TRUE );
#elif defined(__linux__)
                    // TODO. the docs say that this pid_t system sucks because it is suspect to
                    // ID-override causing kill of random threads/processes. Need to solve this
                    // somehow.
                    int success = tkill( info->codeThread, SIGKILL );

                    hasTerminated = ( success == 0 );
#else
#error No implementation for thread kill
#endif

                    if ( hasTerminated )
                    {
                        // Put the status as terminated.
                        {
                            CUnfairMutexContext ctxThreadStatus( nativeThread->mtxThreadStatus );

                            info->status = THREAD_TERMINATED;
                        }

                        // Return true.
                        returnVal = true;
                    }
                }
                else
                {
                    privateNativeThreadEnvironment *nativeEnv = privateNativeThreadEnvironmentRegister.get().GetPluginStruct( (CExecutiveManagerNative*)nativeThread->manager );

                    if ( nativeEnv )
                    {
                        // User-mode threads have to be cleanly terminated.
                        // This means going down the exception stack.
                        nativeEnv->_nativePluginInterface.RtlTerminateThread( nativeThread->manager, info, ctxThreadLock, waitOnRemote );

                        // We may not actually get here!
                    }

                    // We have successfully terminated the thread.
                    returnVal = true;
                }
            }
        }
    }

    return returnVal;
}

void CExecThreadImpl::CheckTerminationRequest( void )
{
    // Must be performed on the current thread!

    // If we are terminating, we probably should do that.
    if ( this->GetStatusNative() == THREAD_TERMINATING )
    {
        // We just throw a thread termination exception.
        throw threadTerminationException( this );   // it is kind of not necessary to pass the thread handle, but okay.
    }
}

bool CExecThread::Suspend( void )
{
    bool returnVal = false;

#ifdef _WIN32
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)this;

    nativeThreadPlugin *info = GetNativeThreadPlugin( nativeThread->manager, nativeThread );

    // We cannot suspend a remote thread.
    if ( !nativeThread->isRemoteThread )
    {
        if ( info && info->status == THREAD_RUNNING )
        {
            CUnfairMutexContext ctxThreadLock( info->mtxThreadLock );

            if ( info->status == THREAD_RUNNING )
            {
                BOOL success = SuspendThread( info->hThread );

                if ( success == TRUE )
                {
                    CUnfairMutexContext ctxThreadStatus( nativeThread->mtxThreadStatus );

                    info->status = THREAD_SUSPENDED;

                    returnVal = true;
                }
            }
        }
    }
#elif defined(__linux__)
    // There is no thread suspension on Linux.
    returnVal = false;
#else
#error No thread suspend implementation
#endif

    return returnVal;
}

bool CExecThread::Resume( void )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)this;

    bool returnVal = false;

    CExecutiveManagerNative *nativeMan = nativeThread->manager;

    nativeThreadPlugin *info = GetNativeThreadPlugin( nativeMan, nativeThread );

    // We cannot resume a remote thread.
    if ( !nativeThread->isRemoteThread )
    {
        if ( info && info->status == THREAD_SUSPENDED )
        {
            CUnfairMutexContext ctxThreadLock( info->mtxThreadLock );

            if ( info->status == THREAD_SUSPENDED )
            {
                bool hasResumed = false;

#ifdef _WIN32
                BOOL success = ResumeThread( info->hThread );

                hasResumed = ( success == TRUE );
#elif defined(__linux__)
                // We want to support initial resumption of Linux threads.
                if ( !info->hasThreadStarted )
                {
                    // Should be sufficient to have the threadLock.

                    nativeThreadPluginInterface *nativeThreadMan = &privateNativeThreadEnvironmentRegister.get().GetPluginStruct( nativeMan )->_nativePluginInterface;

                    // Mark our thread to start running.
                    CEvent *eventStart = nativeThreadMan->threadStartEventRegister.GetEvent( nativeThread );
                    eventStart->Set( false );

                    info->hasThreadStarted = true;

                    hasResumed = true;
                }
#else
#error No thread resume implementation
#endif

                if ( hasResumed )
                {
                    CUnfairMutexContext ctxThreadStatus( nativeThread->mtxThreadStatus );

                    info->status = THREAD_RUNNING;

                    returnVal = true;
                }
            }
        }
    }

    return returnVal;
}

bool CExecThread::IsCurrent( void )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)this;

    return ( nativeThread->manager->IsCurrentThread( nativeThread ) );
}

void* CExecThread::ResolvePluginMemory( threadPluginOffset offset )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)this;

    return privateThreadEnvironment::threadPluginContainer_t::RESOLVE_STRUCT <void> ( nativeThread, offset );
}

const void* CExecThread::ResolvePluginMemory( threadPluginOffset offset ) const
{
    const CExecThreadImpl *nativeThread = (const CExecThreadImpl*)this;

    return privateThreadEnvironment::threadPluginContainer_t::RESOLVE_STRUCT <void> ( nativeThread, offset );
}

bool CExecThread::IsPluginOffsetValid( threadPluginOffset offset )
{
    return privateThreadEnvironment::threadPluginContainer_t::IsOffsetValid( offset );
}

threadPluginOffset CExecThread::GetInvalidPluginOffset( void )
{
    return privateThreadEnvironment::threadPluginContainer_t::INVALID_PLUGIN_OFFSET;
}

struct threadObjectConstructor
{
    inline threadObjectConstructor( CExecutiveManagerNative *manager, bool isRemoteThread, void *userdata, size_t stackSize, CExecThread::threadEntryPoint_t entryPoint )
    {
        this->manager = manager;
        this->isRemoteThread = isRemoteThread;
        this->userdata = userdata;
        this->stackSize = stackSize;
        this->entryPoint = entryPoint;
    }

    inline CExecThreadImpl* Construct( void *mem ) const
    {
        return new (mem) CExecThreadImpl( this->manager, this->isRemoteThread, this->userdata, this->stackSize, this->entryPoint );
    }

    CExecutiveManagerNative *manager;
    bool isRemoteThread;
    void *userdata;
    size_t stackSize;
    CExecThread::threadEntryPoint_t entryPoint;
};

threadPluginOffset CExecutiveManager::RegisterThreadPlugin( size_t pluginSize, threadPluginInterface *pluginInterface )
{
    struct threadPluginInterface_pipe : public privateThreadEnvironment::threadPluginContainer_t::pluginInterface
    {
        inline threadPluginInterface_pipe( CExecutiveManagerNative *nativeMan, ExecutiveManager::threadPluginInterface *publicIntf )
        {
            this->nativeMan = nativeMan;
            this->publicIntf = publicIntf;
        }

        bool OnPluginConstruct( CExecThreadImpl *nativeThread, privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t pluginOff, privateThreadEnvironment::threadPluginContainer_t::pluginDescriptor pluginDesc ) override
        {
            return this->publicIntf->OnPluginConstruct( nativeThread, pluginOff, ExecutiveManager::threadPluginDescriptor( pluginDesc.pluginId ) );
        }

        void OnPluginDestruct( CExecThreadImpl *nativeThread, privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t pluginOff, privateThreadEnvironment::threadPluginContainer_t::pluginDescriptor pluginDesc ) override
        {
            this->publicIntf->OnPluginDestruct( nativeThread, pluginOff, ExecutiveManager::threadPluginDescriptor( pluginDesc.pluginId ) );
        }

        bool OnPluginAssign( CExecThreadImpl *dstNativeThread, const CExecThreadImpl *srcNativeThread, privateThreadEnvironment::threadPluginContainer_t::pluginOffset_t pluginOff, privateThreadEnvironment::threadPluginContainer_t::pluginDescriptor pluginDesc ) override
        {
            return this->publicIntf->OnPluginAssign( dstNativeThread, srcNativeThread, pluginOff, ExecutiveManager::threadPluginDescriptor( pluginDesc.pluginId ) );
        }

        void DeleteOnUnregister( void )
        {
            NatExecStandardObjectAllocator memAlloc( this->nativeMan );

            eir::dyn_del_struct <threadPluginInterface_pipe> ( memAlloc, nullptr, this );
        }

        CExecutiveManagerNative *nativeMan;
        ExecutiveManager::threadPluginInterface *publicIntf;
    };

    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( nativeMan );

    if ( !threadEnv )
        return privateThreadEnvironment::threadPluginContainer_t::INVALID_PLUGIN_OFFSET;

    NatExecStandardObjectAllocator memAlloc( nativeMan );

    threadPluginInterface_pipe *threadIntf = eir::dyn_new_struct <threadPluginInterface_pipe> ( memAlloc, nullptr, nativeMan, pluginInterface );

    assert( threadIntf != nullptr );

    return threadEnv->threadPlugins.RegisterPlugin( pluginSize, privateThreadEnvironment::threadPluginContainer_t::ANONYMOUS_PLUGIN_ID, threadIntf );
}

void CExecutiveManager::UnregisterThreadPlugin( threadPluginOffset offset )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( nativeMan );

    if ( !threadEnv )
    {
        return;
    }

    threadEnv->threadPlugins.UnregisterPlugin( offset );
}

CExecThread* CExecutiveManager::CreateThread( CExecThread::threadEntryPoint_t entryPoint, void *userdata, size_t stackSize )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    // We must not create new threads if the environment is terminating!
    if ( nativeMan->isTerminating )
    {
        return nullptr;
    }

    // Get the general thread environment.
    privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( nativeMan );

    if ( !threadEnv )
    {
        return nullptr;
    }

    // No point in creating threads if we have no native implementation.
    if ( privateNativeThreadEnvironmentRegister.get().IsRegistered() == false )
        return nullptr;

    CExecThread *threadInfo = nullptr;

    // Construct the thread.
    {
        // We are about to reference a new thread, so lock here.
        CUnfairMutexContext ctxThreadCreate( threadEnv->mtxThreadReferenceLock );

        // Make sure we synchronize access to plugin containers!
        // This only has to happen when the API has to be thread-safe.
        CUnfairMutexContext ctxThreadPlugins( threadEnv->mtxThreadPluginsLock );

        try
        {
            threadObjectConstructor threadConstruct( nativeMan, false, userdata, stackSize, entryPoint );

            NatExecStandardObjectAllocator memAlloc( nativeMan );

            CExecThreadImpl *nativeThread = threadEnv->threadPlugins.ConstructTemplate( memAlloc, threadConstruct );

            if ( nativeThread )
            {
                // Give a referenced handle to the runtime.
                nativeThread->refCount++;

                threadInfo = nativeThread;
            }
        }
        catch( ... )
        {
            // TODO: add an exception that can be thrown if the construction of threads failed.
            threadInfo = nullptr;
        }
    }

    return threadInfo;
}

void CExecutiveManager::TerminateThread( CExecThread *thread, bool waitOnRemote )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)thread;

    nativeThread->Terminate( waitOnRemote );
}

void CExecutiveManager::JoinThread( CExecThread *thread )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)thread;

    CExecutiveManagerNative *nativeMan = nativeThread->manager;

    nativeThreadPlugin *info = GetNativeThreadPlugin( nativeMan, nativeThread );

    if ( info )
    {
#ifdef __linux__
        nativeThreadPluginInterface *nativeThreadMan = &privateNativeThreadEnvironmentRegister.get().GetPluginStruct( nativeMan )->_nativePluginInterface;

        CEvent *eventRunning = nativeThreadMan->threadRunningEventRegister.GetEvent( nativeThread );

        // We should wait till the lock of the thread runtime is taken and left.
        eventRunning->Wait();
#elif defined(_WIN32)
        // Wait for completion of the thread.
        WaitForSingleObject( info->hThread, INFINITE );
#else
#error No thread join wait-for implementation
#endif //CROSS PLATFORM CODE

        // Had to be set by the thread itself.
        assert( info->status == THREAD_TERMINATED );
    }
}

bool CExecutiveManager::IsCurrentThread( CExecThread *thread ) const
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    CExecThreadImpl *nativeThread = (CExecThreadImpl*)thread;

    if ( nativeMan->isTerminating == false )
    {
        // Really simple check actually.
        privateNativeThreadEnvironment *nativeEnv = privateNativeThreadEnvironmentRegister.get().GetPluginStruct( nativeMan );

        if ( nativeEnv )
        {
            nativeThreadPlugin *nativeInfo = GetNativeThreadPlugin( nativeMan, nativeThread );

            return thread_id_fetch().is_current( nativeInfo );
        }
    }

    return false;
}

CExecThread* CExecutiveManager::GetCurrentThread( void )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    CExecThreadImpl *currentThread = nullptr;

    // Only allow retrieval if the environment is not terminating.
    if ( nativeMan->isTerminating == false )
    {
        // Get our native interface (if available).
        privateNativeThreadEnvironment *nativeEnv = privateNativeThreadEnvironmentRegister.get().GetPluginStruct( nativeMan );

        if ( nativeEnv )
        {
            thread_id_fetch helper;

            // If we have an accelerated TLS slot, try to get the handle from it.
            if ( nativeThreadPlugin *tlsInfo = nativeEnv->_nativePluginInterface.TlsGetCurrentThreadInfo() )
            {
                currentThread = tlsInfo->self;
            }
            else
            {
                CUnfairMutexContext ctxRunningThreadList( nativeEnv->_nativePluginInterface.mtxRunningThreadList );

                // Else we have to go the slow way by checking every running thread information in existance.
                LIST_FOREACH_BEGIN( nativeThreadPlugin, nativeEnv->_nativePluginInterface.runningThreads.root, node )
                    if ( helper.is_current( item ) )
                    {
                        currentThread = item->self;
                        break;
                    }
                LIST_FOREACH_END
            }

            if ( currentThread && currentThread->GetStatus() == THREAD_TERMINATED )
            {
                return nullptr;
            }

            // If we have not found a thread handle representing this native thread, we should create one.
            if ( currentThread == nullptr &&
                 nativeEnv->_nativePluginInterface.isTerminating == false && nativeMan->isTerminating == false )
            {
                // Need to fetch the general thread environment.
                privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( nativeMan );

                if ( threadEnv != nullptr )
                {
                    // Create the thread.
                    CExecThreadImpl *newThreadInfo = nullptr;
                    {
                        // Since we are about to create a new thread reference, we must lock.
                        // We can later think about how to optimize this.
                        CUnfairMutexContext ctxThreadCreate( threadEnv->mtxThreadReferenceLock );

                        CUnfairMutexContext ctxThreadPlugins( threadEnv->mtxThreadPluginsLock );

                        try
                        {
                            threadObjectConstructor threadConstruct( nativeMan, true, nullptr, 0, nullptr );

                            NatExecStandardObjectAllocator memAlloc( nativeMan );

                            newThreadInfo = threadEnv->threadPlugins.ConstructTemplate( memAlloc, threadConstruct );
                        }
                        catch( ... )
                        {
                            newThreadInfo = nullptr;
                        }
                    }

                    if ( newThreadInfo )
                    {
                        bool successPluginCreation = false;

                        // Our plugin must have been successfully intialized to continue.
                        if ( nativeThreadPlugin *plugInfo = GetNativeThreadPlugin( nativeMan, newThreadInfo ) )
                        {
                            bool gotIdentificationSuccess = false;

#ifdef _WIN32
                            // Open another thread handle and put it into our native plugin.
                            HANDLE newHandle = nullptr;

                            BOOL successClone = DuplicateHandle(
                                GetCurrentProcess(), helper.hRunningThread,
                                GetCurrentProcess(), &newHandle,
                                0, FALSE, DUPLICATE_SAME_ACCESS
                            );

                            gotIdentificationSuccess = ( successClone == TRUE );
#elif defined(__linux__)
                            // Since thread ids are not reference counted on Linux, we do not care and simply succeed.
                            gotIdentificationSuccess = true;
#else
#error no thread remote handle fetch implementation
#endif //CROSS PLATFORM CODE

                            if ( gotIdentificationSuccess )
                            {
                                // Always remember the thread id.
                                plugInfo->codeThread = helper.get_current_id();

#ifdef _WIN32
                                // Put the new handle into our plugin structure.
                                plugInfo->hThread = newHandle;
#elif defined(__linux__)
                                // Nothing.
#else
#error no thread identification store implementation
#endif //CROSS PLATFORM CODE

                                // Set our plugin information into our Tls slot (if available).
                                nativeEnv->_nativePluginInterface.TlsSetCurrentThreadInfo( plugInfo );

                                // Return it.
                                currentThread = newThreadInfo;

                                successPluginCreation = true;
                            }
                        }

                        if ( successPluginCreation == false )
                        {
                            // Delete the thread object again.
                            CloseThread( newThreadInfo );
                        }
                    }
                }
            }
        }
    }

    return currentThread;
}

CExecThread* CExecutiveManager::AcquireThread( CExecThread *thread )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)thread;

    // Add a reference and return a new handle to the thread.

    // TODO: make sure that we do not overflow the refCount.

    unsigned long prevRefCount = nativeThread->refCount++;

    assert( prevRefCount != 0 );

    // We have a new handle.
    return thread;
}

void CExecutiveManagerNative::CloseThreadNative( CExecThreadImpl *nativeThread )
{
    // Get the general thread environment.
    privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( this );

    if ( !threadEnv )
        return;

    // Changing thread reference count is unsafe so we lock here.
    CUnfairMutexContext ctxThreadClose( threadEnv->mtxThreadReferenceLock );

    // Decrease the reference count.
    unsigned long prevRefCount = nativeThread->refCount--;

    if ( prevRefCount == 1 )
    {
        // Kill the thread.
        CUnfairMutexContext ctxThreadPlugins( threadEnv->mtxThreadPluginsLock );

        NatExecStandardObjectAllocator memAlloc( this );

        threadEnv->threadPlugins.Destroy( memAlloc, nativeThread );
    }
}

void CExecutiveManager::CloseThread( CExecThread *thread )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    CExecThreadImpl *nativeThread = (CExecThreadImpl*)thread;

    if ( nativeThread->refCount == 1 )
    {
        // Only allow this from the current thread if we are a remote thread.
        if ( IsCurrentThread( nativeThread ) )
        {
            if ( !nativeThread->isRemoteThread )
            {
                // TODO: handle this more gracefully.
                *(char*)nullptr = 0;
            }
        }
    }

    nativeMan->CloseThreadNative( nativeThread );
}

static eir::Vector <CExecThreadImpl*, NatExecStandardObjectAllocator> get_active_threads( CExecutiveManagerNative *nativeMan )
{
    eir::Vector <CExecThreadImpl*, NatExecStandardObjectAllocator> threadList( nullptr, 0, nativeMan );

    privateThreadEnvironment *threadEnv = privateThreadEnv.get().GetPluginStruct( nativeMan );

    if ( threadEnv )
    {
        // We need a hard lock on global all-thread status change here.
        // No threads can be added or closed if we hold this lock.
        CUnfairMutexContext ctxThreadPurge( threadEnv->mtxThreadReferenceLock );

        LIST_FOREACH_BEGIN( CExecThreadImpl, nativeMan->threads.root, managerNode )

            CExecThreadImpl *thread = (CExecThreadImpl*)nativeMan->AcquireThread( item );

            if ( thread )
            {
                threadList.AddToBack( thread );
            }

        LIST_FOREACH_END
    }

    return threadList;
}

// You must not be using any threads anymore when calling this function because
// it cleans up their references.
void CExecutiveManager::PurgeActiveThreads( void )
{
    CExecutiveManagerNative *nativeMan = (CExecutiveManagerNative*)this;

    privateNativeThreadEnvironment *natThreadEnv = privateNativeThreadEnvironmentRegister.get().GetPluginStruct( nativeMan );

    if ( natThreadEnv == nullptr )
    {
        return;
    }

    auto threadList = get_active_threads( nativeMan );

    // Destroy all the threads.
    threadList.Walk(
        [&]( size_t idx, CExecThreadImpl *thread )
    {
        // Is it our thread?
        if ( thread->isRemoteThread == false )
        {
            // Wait till the thread has absolutely finished running by joining it.
            this->JoinThread( thread );
        }

        // If we take this lock then we know that any thread which was releasing
        // it's runtime reference has finished releasing it (part of setting state to TERMINATED).
        // Thus it cannot have any more runtime reference! Safe to release all references.
        CUnfairMutexContext ctxReleaseRemainder( natThreadEnv->_nativePluginInterface.mtxRuntimeReferenceRelease );

        unsigned long refsToRelease = ( thread->refCount - 1 );

        for ( unsigned long n = 0; n < refsToRelease; n++ )
        {
            CloseThread( thread );
        }

        // Remove our own reference aswell.
        CloseThread( thread );

        // We could performance-improve this process in the future.
    });
}

unsigned int CExecutiveManager::GetParallelCapability( void ) const
{
#ifdef _WIN32
    SYSTEM_INFO sysInfo;
    GetSystemInfo( &sysInfo );

    return sysInfo.dwNumberOfProcessors;
#else
    // TODO: add support for more systems.
    return 0;
#endif
}

void CExecutiveManager::CheckHazardCondition( void )
{
    CExecThreadImpl *nativeThread = (CExecThreadImpl*)GetCurrentThread();

    // There is no hazard if NativeExecutive is terminating.
    if ( nativeThread == nullptr )
    {
        return;
    }

    nativeThread->CheckTerminationRequest();
}

void registerThreadPlugin( void )
{
    // Register the events that are required for the mutexes.
    _runningThreadListEventRegister.Construct( executiveManagerFactory );
    _threadRuntimeReferenceLockEventRegister.Construct( executiveManagerFactory );
    _tlsThreadToNativeInfoLockEventRegister.Construct( executiveManagerFactory );

#ifdef __linux__

    _threadsToTermLockEventRegister.Construct( executiveManagerFactory );
    _threadsToTermSemEventRegister.Construct( executiveManagerFactory );

#endif //__linux__

    // Register shared events.
    privateThreadEnvThreadReferenceLockEventRegister.Construct( executiveManagerFactory );
    privateThreadEnvThreadPluginsLockEventRegister.Construct( executiveManagerFactory );

    // Register the general thread environment and the native thread environment.
    privateThreadEnv.Construct( executiveManagerFactory ),
    privateNativeThreadEnvironmentRegister.Construct( executiveManagerFactory );
}

void unregisterThreadPlugin( void )
{
    // Must unregister plugins in-order.
    privateNativeThreadEnvironmentRegister.Destroy();
    privateThreadEnv.Destroy();

    // Unregister shared stuff.
    privateThreadEnvThreadPluginsLockEventRegister.Destroy();
    privateThreadEnvThreadReferenceLockEventRegister.Destroy();

    // Unregister the events.
#ifdef __linux__

    _threadsToTermSemEventRegister.Destroy();
    _threadsToTermLockEventRegister.Destroy();

#endif //__linux__

    _tlsThreadToNativeInfoLockEventRegister.Destroy();
    _threadRuntimeReferenceLockEventRegister.Destroy();
    _runningThreadListEventRegister.Destroy();
}

END_NATIVE_EXECUTIVE
