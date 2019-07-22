/*
    RenderWare threading and synchronization module.

    Since modern systems are highly parallel, we have to support stable execution
    of rwtools. This improves responsibility of programs using our library and
    makes them scale way better. It is in our best interest to make us of threading
    whereever the system wants us to.
*/

namespace rw
{

// Locks for synchronizing threads.
// Creation and destruction has to happen on the same engine interface.
struct rwlock abstract
{
    void enter_read( void );
    void leave_read( void );

    void enter_write( void );
    void leave_write( void );

    bool try_enter_read( void );
    bool try_enter_write( void );
};

// Same as rwlock, but is reentrant on the same thread handle.
struct reentrant_rwlock abstract
{
    void enter_read( void );
    void leave_read( void );

    void enter_write( void );
    void leave_write( void );

    bool try_enter_read( void );
    bool try_enter_write( void );
};

// Thread-independent unfair mutex.
struct unfair_mutex abstract
{
    void enter( void );
    void leave( void );
};

// Scoped lock objects.
template <typename lockType = rwlock>
struct scoped_rwlock_reader
{
    inline scoped_rwlock_reader( lockType *lock )
    {
        if ( lock )
        {
            lock->enter_read();
        }

        this->theLock = lock;
    }

    inline ~scoped_rwlock_reader( void )
    {
        if ( lockType *lock = this->theLock )
        {
            lock->leave_read();

            this->theLock = nullptr;
        }
    }

private:
    lockType *theLock;
};

template <typename lockType = rwlock>
struct scoped_rwlock_writer
{
    inline scoped_rwlock_writer( lockType *lock )
    {
        if ( lock )
        {
            lock->enter_write();
        }

        this->theLock = lock;
    }

    inline ~scoped_rwlock_writer( void )
    {
        if ( lockType *lock = this->theLock )
        {
            lock->leave_write();

            this->theLock = nullptr;
        }
    }

private:
    lockType *theLock;
};

// This is a thread handle.
typedef void* thread_t;

typedef void (*threadEntryPoint_t)( thread_t threadHandle, Interface *engineInterface, void *ud );

// Threading lock API.
rwlock* CreateReadWriteLock( Interface *engineInterface );
void CloseReadWriteLock( Interface *engineInterface, rwlock *theLock );

size_t GetReadWriteLockStructSize( Interface *engineInterface );
rwlock* CreatePlacedReadWriteLock( Interface *engineInterface, void *mem );
void ClosePlacedReadWriteLock( Interface *engineInterface, rwlock *theLock );

reentrant_rwlock* CreateReentrantReadWriteLock( Interface *engineInterface );
void CloseReentrantReadWriteLock( reentrant_rwlock *theLock );

size_t GetReentrantReadWriteLockStructSize( Interface *engineInterface );
reentrant_rwlock* CreatePlaceReeentrantReadWriteLock( Interface *engineInterface, void *mem );
void ClosePlacedReentrantReadWriteLock( Interface *engineInterface, reentrant_rwlock *theLock );

unfair_mutex* CreateUnfairMutex( Interface *engineInterface );
void CloseUnfairMutex( Interface *engineInterface, unfair_mutex *mtx );

size_t GetUnfairMutexStructSize( Interface *engineInterface );
unfair_mutex* CreatePlacedUnfairMutex( Interface *engineInterface, void *mem );
void ClosePlacedUnfairMutex( Interface *engineInterface, unfair_mutex *mtx );

// Thread creation API.
thread_t MakeThread( Interface *engineInterface, threadEntryPoint_t entryPoint, void *ud );
void CloseThread( Interface *engineInterface, thread_t threadHandle );

thread_t AcquireThread( Interface *engineInterface, thread_t threadHandle );

bool ResumeThread( Interface *engineInterface, thread_t threadHandle );
bool SuspendThread( Interface *engineInterface, thread_t theadHandle );
void JoinThread( Interface *engineInterface, thread_t threadHandle );
void TerminateThread( Interface *engineInterface, thread_t threadHandle, bool waitOnRemote = true );

void CheckThreadHazards( Interface *engineInterface );

void* GetThreadingNativeManager( Interface *engineInterface );

} // namespace rw