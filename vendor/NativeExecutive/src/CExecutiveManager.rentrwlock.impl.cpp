/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.rentrwlock.impl.cpp
*  PURPOSE:     Read/Write re-entrant lock internal implementation main
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

// This file implements contextual reentrant Read/Write locks.
// They are required in the Eir Lua implementation because "lua_State"s can
// enter many object locks at a time from multiple OS threads.

// Our lock implementation is not native. It relies on dedicated "context structures"
// that should be put into objects that should be able to enter locks. We optimize
// for minimal no-runtime memory usage, meaning that if many locks are alive which
// are not being entered by threads all-the-time then their memory usage does not
// explode.

// It is important to note that lock contexts are assumed thread-safe.
// You can use contexts from a variable amount of threads BUT each context must
// only be used by one thread concurrently.
// If this cannot be guarranteed then YOU MUST employ another lock whenever said
// context is used!

#include "StdInc.h"

#include <sdk/Map.h>

#include "internal/CExecutiveManager.event.internal.h"
#include "CExecutiveManager.evtwait.hxx"

#include "internal/CExecutiveManager.spinlock.internal.h"

#ifdef _DEBUG
#include <assert.h>
#endif //_DEBUG

BEGIN_NATIVE_EXECUTIVE

// The modes that can be entered in this lock.
enum class eLockEnterMode
{
    READER,
    WRITER
};

struct _rwlock_standard_rent_ctx_item
{
    inline _rwlock_standard_rent_ctx_item( void )
    {
        this->countWriteContexts = 0;
        this->countReadContexts = 0;
    }

    // Need to specify those things because eir::Map does internal shifting.
    inline _rwlock_standard_rent_ctx_item( const _rwlock_standard_rent_ctx_item& ) = delete;
    inline _rwlock_standard_rent_ctx_item( _rwlock_standard_rent_ctx_item&& ) = default;

    inline ~_rwlock_standard_rent_ctx_item( void )
    {
        return;
    }

    inline _rwlock_standard_rent_ctx_item& operator = ( const _rwlock_standard_rent_ctx_item& ) = delete;
    inline _rwlock_standard_rent_ctx_item& operator = ( _rwlock_standard_rent_ctx_item&& ) = default;

    // We remember the count of read-ctxs and write-ctxs.
    // This is required so that we can support "upgrading" and "downgrading" to
    // and from WRITER.
    // For debugging we could implement an actual stack of items someday, but it
    // just hinders performance so not today!
    // Since this thing is thread-safe we do not need std::atomic.
    unsigned long countWriteContexts;
    unsigned long countReadContexts;
};

// Forward declaration because of cyclic dependancy.
struct _rwlock_standard_rent_data;

struct _rwlock_standard_rent_ctx
{
    inline _rwlock_standard_rent_ctx( CExecutiveManagerNative *nativeMan )
        : mapItems( eir::constr_with_alloc::DEFAULT, nativeMan )
    {
        this->nativeMan = nativeMan;
        this->waitingOnLock = nullptr;
    }

    // Meow. We take care of this in other functions.
    inline _rwlock_standard_rent_ctx( const _rwlock_standard_rent_ctx& ) = delete;
    inline _rwlock_standard_rent_ctx( _rwlock_standard_rent_ctx&& ) = delete;

    inline ~_rwlock_standard_rent_ctx( void );

    // Need to know the executive manager context.
    CExecutiveManagerNative *nativeMan;
    
    // For every lock that we enter there is data about it.
    // It stays only for as long as we are entered.
    eir::Map <_rwlock_standard_rent_data*, _rwlock_standard_rent_ctx_item, NatExecStandardObjectAllocator> mapItems;

    // This context could be waiting to enter a lock, so remember that.
    // Since each context can wait at a maximum of one lock, no map is required.
    _rwlock_standard_rent_data *waitingOnLock;
    CEvent *waitingThreadEvent;
    RwListEntry <_rwlock_standard_rent_ctx> waitingNode;
    eLockEnterMode waitingToEnterMode;

    // No way around making contexts thread-safe.
    // But we do not advise using them concurrently due to performance impact.
    CSpinLockImpl lockAtomic;
};

struct _rwlock_standard_rent_data
{
    inline _rwlock_standard_rent_data( void )
    {
        this->countWriters = 0;
        this->countReaders = 0;
        // The list of waiting contexts is initialized automatically.
        this->countWaitingWriters = 0;
        this->countWaitingReaders = 0;
    }

    inline ~_rwlock_standard_rent_data( void )
    {
#ifdef _DEBUG
        // Make sure that the lock is not being used anymore.
        assert( this->countWriters == 0 );
        assert( this->countReaders == 0 );
        assert( LIST_EMPTY( this->waitingContexts.root ) == true );
        assert( this->countWaitingWriters == 0 );
        assert( this->countWaitingReaders == 0 );
#endif //_DEBUG
    }

    // Each thread needs storage in this lock on-demand so that life-idle is not taking too much RAM.
    // But since the contexts are stored on their own memory we need no list of them.
    unsigned long countWriters;
    unsigned long countReaders;

    // Contexts can wait to enter a lock, so we need to remember their attempts in-order.
    RwList <_rwlock_standard_rent_ctx> waitingContexts;

    // To optimize the check for currently waiting writers and readers we remember the count of writers
    // and readers.
    unsigned long countWaitingWriters;
    unsigned long countWaitingReaders;

    // The maximum-time operations are O(1) and O(ln(entered-contexts)).
    // VERY IMPORTANT: WE DO NOT ALLOCATE _rwlock_standard_rent_ctx_item UNDER THIS LOCK BECAUSE IT IS CONTEXT-LOCAL!
    // Thus we make a huge gamble by using spin-locks only.
    CSpinLockImpl lockAtomic;
};

_rwlock_standard_rent_ctx::~_rwlock_standard_rent_ctx( void )
{
    // No, we cannot be released from waiting by destruction.
    // Do it cleanly, dawg!
    assert( this->waitingOnLock == nullptr );

    // For any locks that we still hold, clean up.
    // This is actually safe to do.
    mapItems.WalkNodes(
        [&]( decltype(mapItems)::Node *node )
    {
        _rwlock_standard_rent_data *leftover_lock = node->GetKey();
        _rwlock_standard_rent_ctx_item& entry_item = node->GetValue();
            
        CSpinLockContext ctxLeaveLock( leftover_lock->lockAtomic );

        leftover_lock->countReaders -= entry_item.countReadContexts;
        leftover_lock->countWriters -= entry_item.countWriteContexts;
    });
}

bool _rwlock_rent_standard_is_supported( void )
{
    // TODO: properly consult this.
    return pubevent_is_available();
}

size_t _rwlock_rent_standard_get_size( void )
{
    return sizeof(_rwlock_standard_rent_data);
}

size_t _rwlock_rent_standard_get_alignment( void )
{
    return alignof(_rwlock_standard_rent_data);
}

void _rwlock_rent_standard_constructor( void *mem, CExecutiveManagerNative *nativeMan )
{
    // Very simple, meow.
    new (mem) _rwlock_standard_rent_data();
}

void _rwlock_rent_standard_destructor( void *mem, CExecutiveManagerNative *nativeMan )
{
    ((_rwlock_standard_rent_data*)mem)->~_rwlock_standard_rent_data();
}

static inline bool _is_first_waiting_by_type( _rwlock_standard_rent_data *lock, eLockEnterMode type )
{
    if ( LIST_EMPTY( lock->waitingContexts.root ) )
        return false;

    _rwlock_standard_rent_ctx *waitingContext = LIST_GETITEM( _rwlock_standard_rent_ctx, lock->waitingContexts.root.next, waitingNode );

    return ( waitingContext->waitingToEnterMode == type );
}

static inline bool _can_spawn_reader_local( _rwlock_standard_rent_data *lock, _rwlock_standard_rent_ctx_item& enterContext )
{
    unsigned long otherWriterCount = ( lock->countWriters - enterContext.countWriteContexts );   // subtract ourselves.

    return ( otherWriterCount == 0 );
}

static inline bool _can_spawn_reader_global( _rwlock_standard_rent_data *lock, _rwlock_standard_rent_ctx_item& enterContext )
{
    return ( _can_spawn_reader_local( lock, enterContext ) && lock->countWaitingWriters == 0 );
}

void _rwlock_rent_standard_enter_read( void *mem, void *ctxMem )
{
    bool hasToWait = false;

    CEvent *evtWait = nullptr;
    {
        _rwlock_standard_rent_data *lock = (_rwlock_standard_rent_data*)mem;

        _rwlock_standard_rent_ctx *ctx = (_rwlock_standard_rent_ctx*)ctxMem;

        CSpinLockContext ctxAtomicContext( ctx->lockAtomic );

        // Fetch or allocate data that is associated with our current lock.
        // REMEMBER THAT THIS OPERATION COULD FAIL IF INSUFFICIENT MEMORY (exception throw).
        _rwlock_standard_rent_ctx_item& lockCtxData = ctx->mapItems[ lock ];
    
        CSpinLockContext ctxAtomicLock( lock->lockAtomic );

        // Check if we can enter the lock with read mode or have to wait.
        // If we enter waiting-state then we are resurrected when the cause
        // of our wait has left the lock.
        hasToWait = ( _can_spawn_reader_global( lock, lockCtxData ) == false );

        if ( hasToWait )
        {
            // It is a violation of rules if the context is already waiting.
            // Stop thinking otherwise, it makes no sense. You'd be asking for data corruption in your own code.
            assert( ctx->waitingOnLock == nullptr );

            CExecutiveManagerNative *nativeMan = ctx->nativeMan;
            CExecThreadImpl *nativeThread = (CExecThreadImpl*)nativeMan->GetCurrentThread();

            assert( nativeThread != nullptr );

            evtWait = GetCurrentThreadWaiterEvent( nativeMan, nativeThread );

            // Register us as waiting.
            ctx->waitingToEnterMode = eLockEnterMode::READER;
            ctx->waitingOnLock = lock;
            ctx->waitingThreadEvent = evtWait;
            LIST_APPEND( lock->waitingContexts.root, ctx->waitingNode );

            // Update the meta-data in the lock itself.
            lock->countWaitingReaders++;

            // Mark the waiting event.
            evtWait->Set( true );
            
            // We will be woken up by somebody that leaves the lock.
            hasToWait = true;
        }
        else
        {
            // Update the lock status because we just enter.
            lock->countReaders++;
            lockCtxData.countReadContexts++;
        }
    }

    if ( hasToWait )
    {
        // Zzz...
        evtWait->Wait();
    }
}

static inline bool _can_spawn_writer_local( _rwlock_standard_rent_data *lock, _rwlock_standard_rent_ctx_item& lockCtxData )
{
    unsigned long otherWriterCount = ( lock->countWriters - lockCtxData.countWriteContexts );
    unsigned long otherReaderCount = ( lock->countReaders - lockCtxData.countReadContexts );

    return ( otherWriterCount == 0 && otherReaderCount == 0 );
}

static inline bool _can_spawn_writer_global( _rwlock_standard_rent_data *lock, _rwlock_standard_rent_ctx_item& lockCtxData )
{
    return ( _can_spawn_writer_local( lock, lockCtxData ) && lock->countWaitingWriters == 0 && lock->countWaitingReaders == 0 );
}

// THIS ROUTINE MUST BE CALLED WHEN THE LOCK IS ATOMICALLY PROTECTED.
static inline void _check_wake_waiters( _rwlock_standard_rent_data *lock )
{
    // We have to unwait items until we hit the first one that cannot be woken because the
    // currently woken items contradict its spawning.
    while ( true )
    {
        if ( LIST_EMPTY( lock->waitingContexts.root ) )
            return;

        _rwlock_standard_rent_ctx *waitingCtx = LIST_GETITEM( _rwlock_standard_rent_ctx, lock->waitingContexts.root.next, waitingNode );

        // We employ a dual lock of both context-atomic and lock-atomic here.
        // This is the hard portion of the lock, while the soft portion is in
        // the rarely used move-ctx routine.
        // Because we entered the lock-atomic, no context can start moving, thus
        // no context has invalid/partial state in the routine.
        // Also waitingCtx cannot have been preempted before we take ctxWaiterResolve,
        // thus waitingCtx must still be a waiting context in our routine.
        // In conclusion, our routine is working fine.

        CSpinLockContext ctxWaiterResolve( waitingCtx->lockAtomic );

        eLockEnterMode curMode = waitingCtx->waitingToEnterMode;

        // Check if we can unwait this item.
        bool canUnwait = false;

        if ( curMode == eLockEnterMode::WRITER )
        {
            canUnwait = _can_spawn_writer_local( lock, waitingCtx->mapItems[ lock ] );
        }
        else if ( curMode == eLockEnterMode::READER )
        {
            canUnwait = _can_spawn_reader_local( lock, waitingCtx->mapItems[ lock ] );
        }

        // Waiting items must be fairly sheduled in-order.
        if ( !canUnwait )
            break;

        // Unwait our item.
        waitingCtx->waitingThreadEvent->Set( false );
        waitingCtx->waitingThreadEvent = nullptr;   // just for fun.
        waitingCtx->waitingOnLock = nullptr;

        LIST_REMOVE( waitingCtx->waitingNode );

        _rwlock_standard_rent_ctx_item& lockCtxData = waitingCtx->mapItems[ lock ];

        // Remember that less items are waiting and register the item.
        if ( curMode == eLockEnterMode::WRITER )
        {
            lock->countWaitingWriters--;
            
            lock->countWriters++;
            lockCtxData.countWriteContexts++;
        }
        else if ( curMode == eLockEnterMode::READER )
        {
            lock->countWaitingReaders--;
            
            lock->countReaders++;
            lockCtxData.countReadContexts++;
        }
        else
        {
            assert( 0 );
        }
    }
}

static inline void _garbage_collect_ctx_item(
    _rwlock_standard_rent_data *lock,
    _rwlock_standard_rent_ctx *ctx,
    _rwlock_standard_rent_ctx_item& ctx_item
)
{
    if ( ctx_item.countReadContexts == 0 && ctx_item.countWriteContexts == 0 )
    {
        ctx->mapItems.RemoveByKey( lock );
    }
}

void _rwlock_rent_standard_leave_read( void *mem, void *ctxMem )
{
    // The reader portion of this lock is not really different than from
    // the regular read/write lock (in logic at least).

    _rwlock_standard_rent_data *lock = (_rwlock_standard_rent_data*)mem;

    _rwlock_standard_rent_ctx *ctx = (_rwlock_standard_rent_ctx*)ctxMem;
    
    CSpinLockContext ctxAtomicContext( ctx->lockAtomic );

    // Fetch or allocate data that is associated with our current lock.
    _rwlock_standard_rent_ctx_item& lockCtxData = ctx->mapItems[ lock ];

    CSpinLockContext ctxAtomicLock( lock->lockAtomic );
    
    // For every leave that we do there must have been an enter by the same context.
    // This is in strong contrast to the regular read/write lock which allows enter and leave
    // to be cast from different context.
    assert( lockCtxData.countReadContexts > 0 );
    assert( lock->countReaders > 0 );

    lockCtxData.countReadContexts--;
    lock->countReaders--;

    // If we are not used anymore, then remove the registration, to save memory.
    _garbage_collect_ctx_item( lock, ctx, lockCtxData );

    // If the lock has no more readers, then a writer could be woken up.
    // We do that in a managed routine.
    // NOTE: we know that the context that was previously running cannot be registered as
    // waiter in this lock in the meantime, meow.
    _check_wake_waiters( lock );
}

void _rwlock_rent_standard_enter_write( void *mem, void *ctxMem )
{
    bool hasToWait = false;

    CEvent *evtWaiter = nullptr;
    {
        _rwlock_standard_rent_data *lock = (_rwlock_standard_rent_data*)mem;

        _rwlock_standard_rent_ctx *ctx = (_rwlock_standard_rent_ctx*)ctxMem;

        CSpinLockContext ctxAtomicContext( ctx->lockAtomic );

        // Fetch the context registration for our lock.
        // Important to not do under lock-atomic because expensive memory allocation.
        _rwlock_standard_rent_ctx_item& lockCtxData = ctx->mapItems[ lock ];

        CSpinLockContext ctxLockAtomic( lock->lockAtomic );

        // See if we have to wait as writer.
        // We need to wait if there are any writers or readers currently in the critical section or
        // anything else is already waiting in the queue.
        hasToWait = ( _can_spawn_writer_global( lock, lockCtxData ) == false );

        if ( hasToWait )
        {
            // Fetch runtime data.
            CExecutiveManagerNative *nativeMan = ctx->nativeMan;
            CExecThreadImpl *nativeThread = (CExecThreadImpl*)nativeMan->GetCurrentThread();

            assert( nativeThread != nullptr );

            evtWaiter = GetCurrentThreadWaiterEvent( nativeMan, nativeThread );

            // Register us as waiting writer.
            ctx->waitingToEnterMode = eLockEnterMode::WRITER;
            ctx->waitingOnLock = lock;
            ctx->waitingThreadEvent = evtWaiter;
            LIST_APPEND( lock->waitingContexts.root, ctx->waitingNode );

            // Update lock meta-data.
            lock->countWaitingWriters++;

            // Mark ourselves as waiting.
            evtWaiter->Set( true );

            // Tell our runtime.
            hasToWait = true;
        }
        else
        {
            // Enter us.
            lockCtxData.countWriteContexts++;
            lock->countWriters++;
        }
    }

    if ( hasToWait )
    {
        evtWaiter->Wait();
    }
}

void _rwlock_rent_standard_leave_write( void *mem, void *ctxMem )
{
    _rwlock_standard_rent_data *lock = (_rwlock_standard_rent_data*)mem;

    _rwlock_standard_rent_ctx *ctx = (_rwlock_standard_rent_ctx*)ctxMem;

    CSpinLockContext ctxAtomicContext( ctx->lockAtomic );

    // Fetch or allocate data that is associated with our current lock.
    _rwlock_standard_rent_ctx_item& lockCtxData = ctx->mapItems[ lock ];

    CSpinLockContext ctxAtomicLock( lock->lockAtomic );

    // Debug some things so that nothing goes haywire (hopefully).
    assert( lockCtxData.countWriteContexts > 0 );
    assert( lock->countWriters > 0 );

    lockCtxData.countWriteContexts--;
    lock->countWriters--;

    // If we are not used anymore, then remove the registration, to save memory.
    _garbage_collect_ctx_item( lock, ctx, lockCtxData );

    // Once again check if anyone should be woken up.
    _check_wake_waiters( lock );
}

bool _rwlock_rent_standard_try_enter_read( void *mem, void *ctxMem )
{
    // Operate very similar to the regular read-enter, but do not wait.

    _rwlock_standard_rent_data *lock = (_rwlock_standard_rent_data*)mem;

    _rwlock_standard_rent_ctx *ctx = (_rwlock_standard_rent_ctx*)ctxMem;

    CSpinLockContext ctxAtomicContext( ctx->lockAtomic );

    // Fetch or allocate data that is associated with our current lock.
    // REMEMBER THAT THIS OPERATION COULD FAIL IF INSUFFICIENT MEMORY (exception throw).
    _rwlock_standard_rent_ctx_item& lockCtxData = ctx->mapItems[ lock ];
    
    CSpinLockContext ctxAtomicLock( lock->lockAtomic );

    // Check if we can enter the lock with read mode or have to wait.
    // If we enter waiting-state then we are resurrected when the cause
    // of our wait has left the lock.
    bool canEnter = _can_spawn_reader_global( lock, lockCtxData );

    if ( canEnter )
    {
        // Update the meta-data.
        lockCtxData.countReadContexts++;
        lock->countReaders++;
    }

    return canEnter;
}

bool _rwlock_rent_standard_try_enter_write( void *mem, void *ctxMem )
{
    _rwlock_standard_rent_data *lock = (_rwlock_standard_rent_data*)mem;

    _rwlock_standard_rent_ctx *ctx = (_rwlock_standard_rent_ctx*)ctxMem;

    CSpinLockContext ctxAtomicContext( ctx->lockAtomic );

    // Fetch the context registration for our lock.
    // Important to not do under lock-atomic because expensive memory allocation.
    _rwlock_standard_rent_ctx_item& lockCtxData = ctx->mapItems[ lock ];

    CSpinLockContext ctxLockAtomic( lock->lockAtomic );

    // See if we have to wait as writer.
    // We need to wait if there are any writers or readers currently in the critical section or
    // anything else is already waiting in the queue.
    bool canEnter = _can_spawn_writer_global( lock, lockCtxData );

    if ( canEnter )
    {
        // Update the writer count.
        lockCtxData.countWriteContexts++;
        lock->countWriters++;
    }

    return canEnter;
}

size_t _rwlock_rent_standard_ctx_get_size( void )
{
    return sizeof(_rwlock_standard_rent_ctx);
}

size_t _rwlock_rent_standard_ctx_get_alignment( void )
{
    return alignof(_rwlock_standard_rent_ctx);
}

void _rwlock_rent_standard_ctx_constructor( void *mem, CExecutiveManagerNative *nativeMan )
{
    new (mem) _rwlock_standard_rent_ctx( nativeMan );
}

void _rwlock_rent_standard_ctx_destructor( void *mem, CExecutiveManagerNative *nativeMan )
{
    ((_rwlock_standard_rent_ctx*)mem)->~_rwlock_standard_rent_ctx();
}

void _rwlock_rent_standard_ctx_move( void *dstMem, void *srcMem )
{
#ifdef _DEBUG
    // TROOLOLOLOLOLO.
    assert( dstMem != srcMem );
#endif //_DEBUG

    // Safely move the context.
    _rwlock_standard_rent_ctx *dstCtx = (_rwlock_standard_rent_ctx*)dstMem;
    _rwlock_standard_rent_ctx *srcCtx = (_rwlock_standard_rent_ctx*)srcMem;

retryTakingLocks:
    CSpinLockContext ctxDstAtomic( dstCtx->lockAtomic );
    CSpinLockContext ctxSrcAtomic( srcCtx->lockAtomic );

    // Must not wait on a lock for the context that you want to override.
    assert( dstCtx->waitingOnLock == nullptr );

    // Now take the lock that is problematic.
    _rwlock_standard_rent_data *waitingOnLock = srcCtx->waitingOnLock;
    {
        bool couldTakeLock = waitingOnLock->lockAtomic.tryLock();

        if ( !couldTakeLock )
        {
            goto retryTakingLocks;
        }
    }

    dstCtx->nativeMan = srcCtx->nativeMan;  // meow.
    dstCtx->mapItems = std::move( srcCtx->mapItems );

    if ( waitingOnLock )
    {
        dstCtx->waitingNode.moveFrom( std::move( srcCtx->waitingNode ) );
        dstCtx->waitingToEnterMode = srcCtx->waitingToEnterMode;
        dstCtx->waitingThreadEvent = srcCtx->waitingThreadEvent;
    }

    dstCtx->waitingOnLock = waitingOnLock;

    // Clear out the source item.
    srcCtx->waitingOnLock = nullptr;

    // Remove the locks.
    waitingOnLock->lockAtomic.unlock();
}

END_NATIVE_EXECUTIVE