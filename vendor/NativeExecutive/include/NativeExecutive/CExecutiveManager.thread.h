/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.thread.h
*  PURPOSE:     Thread abstraction layer for MTA
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _EXECUTIVE_MANAGER_THREADS_
#define _EXECUTIVE_MANAGER_THREADS_

BEGIN_NATIVE_EXECUTIVE

enum eThreadStatus
{
    THREAD_SUSPENDED,       // either initial status or stopped by user-mode Suspend()
    THREAD_RUNNING,         // active on the OS sheduler
    THREAD_TERMINATING,     // active on the OS sheduler AND seeking closest path to termination
    THREAD_TERMINATED       // halted.
};

class CExecutiveManager;

class CExecThread abstract
{
protected:
    ~CExecThread( void ) = default;

public:
    typedef void (*threadEntryPoint_t)( CExecThread *thisThread, void *userdata );

    CExecutiveManager* GetManager( void );

    eThreadStatus GetStatus( void ) const;

    // Ask a thread to shut down, gracefully if possible.
    bool Terminate( bool waitOnRemote = true );

    bool Suspend( void );
    bool Resume( void );

    // Returns true if the running native OS thread is identified with this thread object.
    bool IsCurrent( void );

    // Returns the fiber that is currently running on this thread.
    // If there are multiple fibers nested then the top-most is returned.
    CFiber* GetCurrentFiber( void );
    bool IsFiberRunningHere( CFiber *fiber );

    // Plugin API.
    void* ResolvePluginMemory( threadPluginOffset offset );
    const void* ResolvePluginMemory( threadPluginOffset offset ) const;

    static bool IsPluginOffsetValid( threadPluginOffset offset );
    static threadPluginOffset GetInvalidPluginOffset( void );
};

END_NATIVE_EXECUTIVE

#endif //_EXECUTIVE_MANAGER_THREADS_
