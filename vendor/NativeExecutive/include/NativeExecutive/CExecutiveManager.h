/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.h
*  PURPOSE:     MTA thread and fiber execution manager for workload smoothing
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _EXECUTIVE_MANAGER_
#define _EXECUTIVE_MANAGER_

#include <sdk/PluginFactory.h>
#include <sdk/rwlist.hpp>

// Namespace simplification definitions.
#define BEGIN_NATIVE_EXECUTIVE      namespace NativeExecutive {
#define END_NATIVE_EXECUTIVE        }

BEGIN_NATIVE_EXECUTIVE

// Forward declarations.
class CExecThread;
class CFiber;
class CExecTask;

typedef ptrdiff_t threadPluginOffset;

END_NATIVE_EXECUTIVE

#include "CExecutiveManager.thread.h"
#include "CExecutiveManager.fiber.h"
#include "CExecutiveManager.task.h"
#include "CExecutiveManager.rwlock.h"
#include "CExecutiveManager.spinlock.h"
#include "CExecutiveManager.cond.h"
#include "CExecutiveManager.event.h"
#include "CExecutiveManager.unfairmtx.h"
#include "CExecutiveManager.sem.h"

BEGIN_NATIVE_EXECUTIVE

namespace ExecutiveManager
{
    // Function used by the system for performance measurements.
    double GetPerformanceTimer( void );

    struct threadPluginDescriptor
    {
        // We do not allow registration of specific plugin IDs, so there are no members.

        inline threadPluginDescriptor( unsigned int pluginId )
        {
            // Nothing to do here for now.
            return;
        }

        template <typename structType>
        static structType* RESOLVE_STRUCT( CExecThread *execThread, threadPluginOffset offset )
        {
            return (structType*)execThread->ResolvePluginMemory( offset );
        }

        template <typename structType>
        static const structType* RESOLVE_STRUCT( const CExecThread *execThread, threadPluginOffset offset )
        {
            return (const structType*)execThread->ResolvePluginMemory( offset );
        }
    };

    struct threadPluginInterface
    {
        virtual ~threadPluginInterface( void )       {}

        virtual bool OnPluginConstruct( CExecThread *object, threadPluginOffset pluginOffset, threadPluginDescriptor pluginId ) = 0;
        virtual void OnPluginDestruct( CExecThread *object, threadPluginOffset pluginOffset, threadPluginDescriptor pluginId ) = 0;
        virtual bool OnPluginAssign( CExecThread *dstObject, const CExecThread *srcObject, threadPluginOffset pluginOffset, threadPluginDescriptor pluginid ) = 0;
        virtual void DeleteOnUnregister( void )
        {
            return;
        }
    };
};

// Plugin API definitions.
typedef ExecutiveManager::threadPluginInterface threadPluginInterface;
typedef ExecutiveManager::threadPluginDescriptor threadPluginDescriptor;

#define DEFAULT_GROUP_MAX_EXEC_TIME     16

// You should not throw this exception in user-programs, but it is thrown by the executive manager.
struct fiberTerminationException
{
    inline fiberTerminationException( CFiber *fiber )
    {
        this->fiber = fiber;
    }

    CFiber *fiber;
};

// An exception that can be thrown by fibers during resume if they are terminated by an unknown exception.
struct fiberUnhandledException
{
    inline fiberUnhandledException( CFiber *fiber )
    {
        this->fiber = fiber;
    }

    CFiber *fiber;
};

// Memory manager for piping all memory requests through.
// All calls to this interfacec will be protected by the native executive memory lock.
// Fetch it from the manager if you want to safely use the memory allocator.
struct MemoryInterface
{
    virtual void* Allocate( size_t memSize, size_t alignment ) = 0;
    virtual bool Resize( void *memPtr, size_t reqSize ) = 0;
    virtual void Free( void *memPtr ) = 0;
};

struct executiveStatistics
{
    // NOTE that a snapshot of the executive manager does not have to
    // be consistent. For example, after collecting the memory usage count
    // the library is allowed to spawn new threads before collecting the
    // thread count, hereby creating an inconsistency.

    // Global statistics.
    size_t realOverallMemoryUsage = 0;
    size_t metaOverallMemoryUsage = 0;
    size_t numThreadHandles = 0;
    size_t numFibers = 0;

    // Object size statistics.
    size_t structSizeManager = 0;
    size_t structSizeThread = 0;
    size_t structSizeFiber = 0;
};

class CExecutiveGroup;

class CExecutiveManager abstract
{
public:
    // Public factory API.
    static CExecutiveManager* Create( void );
    static void Delete( CExecutiveManager *manager );

    // USE WITH CAUTION.
    void            PurgeActiveRuntimes ( void );
    void            PurgeActiveThreads  ( void );

    void            MarkAsTerminating   ( void );

    // Memory management API.
    CUnfairMutex*   GetMemoryLock       ( void );
    void*           MemAlloc            ( size_t memSize, size_t alignment ) noexcept;
    bool            MemResize           ( void *memPtr, size_t reqSize ) noexcept;
    void            MemFree             ( void *memPtr ) noexcept;

    // Plugin API.
    threadPluginOffset    RegisterThreadPlugin( size_t pluginSize, threadPluginInterface *intf );
    void            UnregisterThreadPlugin( threadPluginOffset offset );

    CExecThread*    CreateThread        ( CExecThread::threadEntryPoint_t proc, void *userdata, size_t stackSize = 0 );
    void            TerminateThread     ( CExecThread *thread, bool waitOnRemote = true );
    void            JoinThread          ( CExecThread *thread );
    bool            IsCurrentThread     ( CExecThread *thread ) const;
    CExecThread*    GetCurrentThread    ( void );
    CExecThread*    AcquireThread       ( CExecThread *thread );
    void            CloseThread         ( CExecThread *thread );

    unsigned int    GetParallelCapability( void ) const;

    void            CheckHazardCondition( void );

    CFiber*         CreateFiber         ( CFiber::fiberexec_t proc, void *userdata, size_t stackSize = 0 );
    void            TerminateFiber      ( CFiber *fiber );
    void            CloseFiber          ( CFiber *fiber );

    CFiber*         GetCurrentFiber     ( void );

    CEvent*         CreateEvent         ( void );
    void            CloseEvent          ( CEvent *evtObj );

    CSpinLock*      CreateSpinLock      ( void );
    void            CloseSpinLock       ( CSpinLock *lock );

    CUnfairMutex*   CreateUnfairMutex   ( void );
    void            CloseUnfairMutex    ( CUnfairMutex *mtx );

    size_t          GetUnfairMutexStructSize    ( void );
    size_t          GetUnfairMutexAlignment     ( void );
    CUnfairMutex*   CreatePlacedUnfairMutex     ( void *mem );
    void            ClosePlacedUnfairMutex      ( CUnfairMutex *mtx );

    CExecutiveGroup*    CreateGroup     ( void );
    void            CloseGroup          ( CExecutiveGroup *group );

    void            DoPulse             ( void );

    CExecTask*      CreateTask          ( CExecTask::taskexec_t proc, void *userdata, size_t stackSize = 0 );
    void            CloseTask           ( CExecTask *task );

    // Methods for managing synchronization objects.
    // Semaphores.
    CSemaphore*     CreateSemaphore     ( void );
    void            CloseSemaphore      ( CSemaphore *sem );

    size_t          GetSemaphoreStructSize  ( void );
    size_t          GetSemaphoreAlignment   ( void );
    CSemaphore*     CreatePlacedSemaphore   ( void *mem );
    void            ClosePlacedSemaphore    ( CSemaphore *sem );

    // Read/Write locks.
    CReadWriteLock* CreateReadWriteLock ( void );
    void            CloseReadWriteLock  ( CReadWriteLock *theLock );

    size_t          GetReadWriteLockStructSize  ( void );
    CReadWriteLock* CreatePlacedReadWriteLock   ( void *mem );
    void            ClosePlacedReadWriteLock    ( CReadWriteLock *theLock );

    // Fair Read/Write locks.
    CFairReadWriteLock* CreateFairReadWriteLock ( void );
    void                CloseFairReadWriteLock  ( CFairReadWriteLock *theLock );

    size_t              GetFairReadWriteLockStructSize  ( void );
    CFairReadWriteLock* CreatePlacedFairReadWriteLock   ( void *mem );
    void                ClosePlacedFairReadWriteLock    ( CFairReadWriteLock *theLock );

    CReentrantReadWriteLock*    CreateReentrantReadWriteLock( void );
    void                        CloseReentrantReadWriteLock ( CReentrantReadWriteLock *theLock );

    size_t                      GetReentrantReadWriteLockStructSize ( void );
    size_t                      GetReentrantReadWriteLockAlignment  ( void );
    CReentrantReadWriteLock*    CreatePlacedReentrantReadWriteLock  ( void *mem );
    void                        ClosePlacedReentrantReadWriteLock   ( CReentrantReadWriteLock *theLock );

    // Reentrant Read/Write lock contexts are structs that can enter said lock recursively.
    CReentrantReadWriteContext*     CreateReentrantReadWriteContext ( void );
    void                            CloseReentrantReadWriteContext  ( CReentrantReadWriteContext *ctx );
    
    size_t                      GetReentrantReadWriteContextStructSize  ( void );
    size_t                      GetReentrantReadWriteContextAlignment   ( void );
    CReentrantReadWriteContext* CreatePlacedReentrantReadWriteContext   ( void *mem );
    void                        ClosePlacedReentrantReadWriteContext    ( CReentrantReadWriteContext *ctx );
    void                        MoveReentrantReadWriteContext   ( CReentrantReadWriteContext *dstCtx, CReentrantReadWriteContext *srcCtx );

    // Thread-local reentrant Read/Write lock helper, to save some typing.
    CThreadReentrantReadWriteLock*  CreateThreadReentrantReadWriteLock  ( void );
    void                            CloseThreadReentrantReadWriteLock   ( CThreadReentrantReadWriteLock *lock );
    CReentrantReadWriteContext*     GetThreadReentrantReadWriteContext  ( void );

    size_t                          GetThreadReentrantReadWriteLockStructSize   ( void );
    size_t                          GetThreadReentrantReadWriteLockAlignment    ( void );
    CThreadReentrantReadWriteLock*  CreatePlacedThreadReentrantReadWriteLock    ( void *mem );
    void                            ClosePlacedThreadReentrantReadWriteLock     ( CThreadReentrantReadWriteLock *lock );

    // Condition variables.
    CCondVar*       CreateConditionVariable ( void );
    void            CloseConditionVariable  ( CCondVar *var );

    // Statistics API.
    executiveStatistics CollectStatistics   ( void );
};

// Exception that gets thrown by threads when they terminate.
struct threadTerminationException
{
    inline threadTerminationException( CExecThread *theThread )
    {
        this->terminatedThread = theThread;
    }

    inline ~threadTerminationException( void )
    {
        return;
    }

    CExecThread *terminatedThread;
};

class CExecutiveGroup
{
public:
    void AddFiber( CFiber *fiber );

    void SetMaximumExecutionTime( double ms );
    double GetMaximumExecutionTime( void ) const;

    void DoPulse( void );

    void SetPerfMultiplier( double mult );
    double GetPerfMultiplier( void ) const;
};

END_NATIVE_EXECUTIVE

#include "CExecutiveManager.memory.h"
#include "CExecutiveManager.hazards.h"
#include "CExecutiveManager.threadplugins.h"

#include "CExecutiveManager.qol.h"

#endif //_EXECUTIVE_MANAGER_
