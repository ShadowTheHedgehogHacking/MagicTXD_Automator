// TXD file actions that can be performed by Magic.TXD.
#include "mainwindow.h"

#include <thread>

MagicActionSystem::MagicActionSystem( NativeExecutive::CExecutiveManager *natExec )
{
    this->nativeExec = natExec;

    // Remember that it is okay to act like a spoiled brat inside of magic-txd and use
    // the lambda version of CreateThread. In realtime-critical code you must never do that
    // and instead allocate the runtime memory somewhere fixed.

    // Boot the sheduler.
    NativeExecutive::CExecThread *shedThread = NativeExecutive::CreateThreadL( natExec,
        [this, natExec]( NativeExecutive::CExecThread *theThread )
    {
        using namespace std::chrono;

        while ( true )
        {
            // Make sure to terminate.
            natExec->CheckHazardCondition();

            // Wait for active tasks.
            bool hasActionToken = false;
            actionToken token;
            {
                NativeExecutive::CReadWriteWriteContextSafe <> ctxFetchTask( this->lockActionQueue );

                this->condHasActions->Wait( ctxFetchTask );

                if ( this->actionQueue.empty() == false )
                {
                    token = std::move( this->actionQueue.back() );

                    this->actionQueue.pop_back();

                    hasActionToken = true;
                }
            }

            // If we have an action, we perform it!
            if ( hasActionToken )
            {
                try
                {
                    // Notify the system.
                    this->OnStartAction();

                    try
                    {
                        token.cb( this, token.ud );
                    }
                    catch( ... )
                    {
                        this->OnStopAction();

                        throw;
                    }

                    this->OnStopAction();
                }
                // If there was any known exception we want to continue anyway.
                // The user should be notified about the problem.
                catch( std::exception& except )
                {
                    this->ReportException( except );

                    // Continue.
                }
                catch( rw::RwException& except )
                {
                    this->ReportException( except );

                    // Continue.
                }
            }
        }
    }, 4096 );

    assert( shedThread != NULL );

    shedThread->Resume();

    this->shedulerThread = shedThread;
}

MagicActionSystem::~MagicActionSystem( void )
{
    NativeExecutive::CExecutiveManager *nativeExec = this->nativeExec;

    // Terminate the sheduler.
    NativeExecutive::CExecThread *shedThread = this->shedulerThread;

    shedThread->Terminate( true );

    nativeExec->CloseThread( shedThread );

    this->shedulerThread = NULL;
}

void MagicActionSystem::LaunchAction( actionRuntime_t cb, void *ud )
{
    NativeExecutive::CReadWriteWriteContext <> ctxPutAction( this->lockActionQueue );

    actionToken token;
    token.cb = cb;
    token.ud = ud;

    this->actionQueue.push_back( std::move( token ) );

    this->condHasActions->Signal();
}

// Specialization for MainWindow.
MainWindow::EditorActionSystem::EditorActionSystem( MainWindow *mainWnd )
    : MagicActionSystem( (NativeExecutive::CExecutiveManager*)rw::GetThreadingNativeManager( mainWnd->GetEngine() ) )
{
    return;
}

MainWindow::EditorActionSystem::~EditorActionSystem( void )
{
    return;
}

void MainWindow::EditorActionSystem::OnStartAction( void )
{
    return;
}

void MainWindow::EditorActionSystem::OnStopAction( void )
{
    return;
}

void MainWindow::EditorActionSystem::OnUpdateStatusMessage( const char *msg )
{
    return;
}

void MainWindow::EditorActionSystem::ReportException( const std::exception& except )
{
    return;
}

void MainWindow::EditorActionSystem::ReportException( const rw::RwException& except )
{
    return;
}