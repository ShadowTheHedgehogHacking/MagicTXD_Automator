#pragma once

#include <NativeExecutive/CExecutiveManager.h>

#include <condition_variable>

// Actions provider system.
// It allows for multiple tasks to be processed in a batch.
struct MagicActionSystem abstract
{
    MagicActionSystem( NativeExecutive::CExecutiveManager *natExec );
    ~MagicActionSystem( void );

    typedef void (*actionRuntime_t)( MagicActionSystem *system, void *ud );

    void LaunchAction( actionRuntime_t cb, void *ud );

protected:
    virtual void OnStartAction( void ) = 0;
    virtual void OnStopAction( void ) = 0;

    virtual void OnUpdateStatusMessage( const char *statusString ) = 0;

    virtual void ReportException( const std::exception& except ) = 0;
    virtual void ReportException( const rw::RwException& except ) = 0;

private:
    NativeExecutive::CExecutiveManager *nativeExec;

    // There is no point in using more than one thread in the task sheduling since
    // we have to obey a queue of tasks. Speedup of actions should be achieved
    // internally in magic-rw.
    NativeExecutive::CExecThread *shedulerThread;

    // List of tasks to be taken.
    NativeExecutive::CReadWriteLock *lockActionQueue;
    NativeExecutive::CCondVar *condHasActions;

    struct actionToken
    {
        actionRuntime_t cb;
        void *ud;
    };

    std::list <actionToken> actionQueue;
};
