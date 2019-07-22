/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.rwlock.h
*  PURPOSE:     Read/Write lock synchronization object
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _NATIVE_EXECUTIVE_READ_WRITE_LOCK_
#define _NATIVE_EXECUTIVE_READ_WRITE_LOCK_

BEGIN_NATIVE_EXECUTIVE

/*
    Synchronization object - the "Read/Write" lock

    Use this sync object if you have a data structure that requires consistency in a multi-threaded environment.
    Just like any other sync object it prevents instruction reordering where it changes the designed functionality.
    But the speciality of this object is that it allows two access modes.

    In typical data structure development, read operations do not change the state of an object. This allows
    multiple threads to run concurrently and still keep the logic of the data intact. This assumption is easily
    warded off if the data structure keeps shadow data for optimization purposes (mutable variables).

    Then there is the writing mode. In this mode threads want exclusive access to a data structure, as concurrent
    modification on a data structure is a daunting task and most often is impossible to solve fast and clean.

    By using this object to mark critical read and write regions in your code, you easily make it thread-safe.
    Thread-safety is the future, as silicon has reached its single-threaded performance peak.

    Please make sure that you use this object in an exception-safe way to prevent dead-locks! This structure does
    not support recursive acquisition, so be careful how you do things!
*/
struct CReadWriteLock abstract
{
    // Shared access to data structures.
    void EnterCriticalReadRegion( void );
    void LeaveCriticalReadRegion( void );

    // Exclusive access to data structures.
    void EnterCriticalWriteRegion( void );
    void LeaveCriticalWriteRegion( void );

    // Attempting to enter the lock while preventing a wait scenario.
    bool TryEnterCriticalReadRegion( void );
    bool TryEnterCriticalWriteRegion( void );
};

/*
    Synchronization object - the fair "Read/Write" lock

    This synchronization object is same as the regular read/write lock but with an additional promise:
    threads that enter this lock leave it in the same order as they entered it. Thus the lock is fair
    in a sense that it does not forget the order of timely arrivals.

    I admit that the inclusion of this lock type was promoted by the availability of an internal
    implementation.
*/
struct CFairReadWriteLock abstract
{
    // Shared access to data structures.
    void EnterCriticalReadRegion( void );
    void LeaveCriticalReadRegion( void );

    // Exclusive access to data structures.
    void EnterCriticalWriteRegion( void );
    void LeaveCriticalWriteRegion( void );

    // Attempting to enter the lock while preventing a wait scenario.
    bool TryEnterCriticalReadRegion( void );
    bool TryEnterCriticalWriteRegion( void );
};

/*
    Synchronization object - the reentrant "Read/Write" lock

    This is a variant of CReadWriteLock but it is reentrant. This means that write contexts can be entered multiple times
    on one thread, leaving them the same amount of times to unlock again.

    Due to the reentrance feature this lock is slower than CReadWriteLock.
    It uses a context structure to remember recursive accesses.
*/
struct CReentrantReadWriteContext abstract
{
    unsigned long GetReadContextCount( void ) const;
    unsigned long GetWriteContextCount( void ) const;
};

struct CReentrantReadWriteLock abstract
{
    // Shared access to data structures.
    void LockRead( CReentrantReadWriteContext *ctx );
    void UnlockRead( CReentrantReadWriteContext *ctx );

    // Exclusive access to data structures.
    void LockWrite( CReentrantReadWriteContext *ctx );
    void UnlockWrite( CReentrantReadWriteContext *ctx );

    // Attempting to enter this lock.
    bool TryLockRead( CReentrantReadWriteContext *ctx );
    bool TryLockWrite( CReentrantReadWriteContext *ctx );
};

/*
    Helper of the reentrant Read/Write lock which automatically uses the current thread context.
    Used quite often so we provide this out-of-the-box.
    You can query the thread-context manually if you want to use the generic lock instead.
*/
struct CThreadReentrantReadWriteLock abstract
{
    void LockRead( void );
    void UnlockRead( void );

    void LockWrite( void );
    void UnlockWrite( void );

    bool TryLockRead( void );
    bool TryLockWrite( void );
};

// Lock context helpers for exception safe and correct code region marking.
template <typename rwLockType = CReadWriteLock>
struct CReadWriteReadContext
{
    inline CReadWriteReadContext( rwLockType *theLock )
    {
#ifdef _DEBUG
        assert( theLock != nullptr );
#endif //_DEBUG

        theLock->EnterCriticalReadRegion();

        this->theLock = theLock;
    }
    inline CReadWriteReadContext( const CReadWriteReadContext& ) = delete;
    inline CReadWriteReadContext( CReadWriteReadContext&& ) = delete;

    inline ~CReadWriteReadContext( void )
    {
        this->theLock->LeaveCriticalReadRegion();
    }

    inline CReadWriteReadContext& operator = ( const CReadWriteReadContext& ) = delete;
    inline CReadWriteReadContext& operator = ( CReadWriteReadContext&& ) = delete;

private:
    rwLockType *theLock;
};

template <typename rwLockType = CReadWriteLock>
struct CReadWriteWriteContext
{
    inline CReadWriteWriteContext( rwLockType *theLock )
    {
#ifdef _DEBUG
        assert( theLock != nullptr );
#endif //_DEBUG

        theLock->EnterCriticalWriteRegion();

        this->theLock = theLock;
    }
    inline CReadWriteWriteContext( const CReadWriteWriteContext& ) = delete;
    inline CReadWriteWriteContext( CReadWriteWriteContext&& ) = delete;

    inline ~CReadWriteWriteContext( void )
    {
        this->theLock->LeaveCriticalWriteRegion();
    }

    inline CReadWriteWriteContext& operator = ( const CReadWriteWriteContext& ) = delete;
    inline CReadWriteWriteContext& operator = ( CReadWriteWriteContext&& ) = delete;

private:
    rwLockType *theLock;
};

// Variants of locking contexts that accept a NULL pointer.
template <typename rwLockType = CReadWriteLock>
struct CReadWriteReadContextSafe
{
    inline CReadWriteReadContextSafe( rwLockType *theLock )
    {
        if ( theLock )
        {
            theLock->EnterCriticalReadRegion();
        }

        this->theLock = theLock;
    }
    inline CReadWriteReadContextSafe( rwLockType& theLock )     // variant without nullptr possibility.
    {
        theLock.EnterCriticalReadRegion();

        this->theLock = &theLock;
    }

    inline CReadWriteReadContextSafe( const CReadWriteReadContextSafe& ) = delete;  // rwlock type is not reentrant (unless otherwise specified)
    inline CReadWriteReadContextSafe( CReadWriteReadContextSafe&& right )
    {
        this->theLock = right.theLock;

        right.theLock = nullptr;
    }

    inline void Suspend( void )
    {
        if ( rwLockType *theLock = this->theLock )
        {
            theLock->LeaveCriticalReadRegion();

            this->theLock = nullptr;
        }
    }

    inline ~CReadWriteReadContextSafe( void )
    {
        this->Suspend();
    }

    inline CReadWriteReadContextSafe& operator = ( const CReadWriteReadContextSafe& ) = delete;
    inline CReadWriteReadContextSafe& operator = ( CReadWriteReadContextSafe&& right )
    {
        this->Suspend();

        this->theLock = right.theLock;

        right.theLock = nullptr;

        return *this;
    }

    inline rwLockType* GetCurrentLock( void )
    {
        return this->theLock;
    }

private:
    rwLockType *theLock;
};

template <typename rwLockType = CReadWriteLock>
struct CReadWriteWriteContextSafe
{
    inline CReadWriteWriteContextSafe( rwLockType *theLock )
    {
        if ( theLock )
        {
            theLock->EnterCriticalWriteRegion();
        }
        
        this->theLock = theLock;
    }
    inline CReadWriteWriteContextSafe( rwLockType& theLock )    // variant without nullptr possibility.
    {
        theLock.EnterCriticalWriteRegion();

        this->theLock = &theLock;
    }

    inline CReadWriteWriteContextSafe( const CReadWriteWriteContextSafe& right ) = delete;
    inline CReadWriteWriteContextSafe( CReadWriteWriteContextSafe&& right )
    {
        this->theLock = right.theLock;

        right.theLock = nullptr;
    }

    inline void Suspend( void )
    {
        if ( rwLockType *theLock = this->theLock )
        {
            theLock->LeaveCriticalWriteRegion();

            this->theLock = nullptr;
        }
    }

    inline ~CReadWriteWriteContextSafe( void )
    {
        this->Suspend();
    }

    inline CReadWriteWriteContextSafe& operator = ( const CReadWriteWriteContextSafe& right ) = delete;
    inline CReadWriteWriteContextSafe& operator = ( CReadWriteWriteContextSafe&& right )
    {
        this->Suspend();

        this->theLock = right.theLock;

        right.theLock = nullptr;

        return *this;
    }

    inline rwLockType* GetCurrentLock( void )
    {
        return this->theLock;
    }

private:
    rwLockType *theLock;
};

END_NATIVE_EXECUTIVE

#endif //_NATIVE_EXECUTIVE_READ_WRITE_LOCK_