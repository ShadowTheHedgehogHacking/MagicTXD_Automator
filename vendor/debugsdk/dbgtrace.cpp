/*****************************************************************************
*
*  PROJECT:     Multi Theft Auto v1.2
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        debugsdk/dbgtrace.cpp
*  PURPOSE:     Win32 exception tracing tool for error isolation
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include <debugsdk_config.h>

#ifdef _DEBUG_TRACE_LIBRARY_

#include "dbgtrace.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <intrin.h>
#include <winternl.h>

#define NUMELMS(x)      ( sizeof(x) / sizeof(*x) )

#include <sdk/MemoryUtils.h>
#include <CFileSystem.common.stl.h>

// TODO: think about getting this to use Eir types.
#include <sstream>

AINLINE std::string to_string_hex( size_t num )
{
    std::stringstream stream;

    stream << std::hex << num;

    return stream.str();
}

// Specialized memory array.
template <typename dataType>
struct dbgtraceIterativeGrowableArrayManager
{
    AINLINE void InitField( dataType& theField )
    {
        return;
    }
};

struct dbgtracePrivateHeapAllocator
{
    inline dbgtracePrivateHeapAllocator( HANDLE heapHandle )
    {
        this->heapHandle = heapHandle;
    }

    inline dbgtracePrivateHeapAllocator( dbgtracePrivateHeapAllocator&& right )
    {
        this->heapHandle = right.heapHandle;

        right.heapHandle = nullptr;
    }

    inline ~dbgtracePrivateHeapAllocator( void )
    {
        return;
    }

    inline void* Allocate( size_t memSize, unsigned int flags )
    {
        return HeapAlloc( this->heapHandle, 0, memSize );
    }

    inline void* Realloc( void *memPtr, size_t memSize, unsigned int flags )
    {
        return HeapReAlloc( this->heapHandle, 0, memPtr, memSize );
    }

    inline void Free( void *memPtr )
    {
        HeapFree( this->heapHandle, 0, memPtr );
    }

private:
    HANDLE heapHandle;
};

template <typename dataType>
using dbgtraceGrowableArray = growableArrayEx <dataType, 8, 0, dbgtraceIterativeGrowableArrayManager <dataType>, size_t, dbgtracePrivateHeapAllocator>;

#include <assert.h>

#ifndef AINLINE
#define AINLINE __forceinline
#endif //AINLINE

#include <Dbghelp.h>

// If you want to use this library, make sure to put /SAFESEH:NO in your command-line linker options!

#define DBGHELP_DLL_NAME    "dbghelp.dll"

// Prototype definitions of functions that we need from Dbghelp.dll
typedef decltype( &SymInitialize ) pfnSymInitialize;
typedef decltype( &SymCleanup ) pfnSymCleanup;
typedef decltype( &SymSetOptions ) pfnSymSetOptions;
typedef decltype( &SymFromAddr ) pfnSymFromAddr;
typedef decltype( &SymGetLineFromAddr ) pfnSymGetLineFromAddr;
typedef decltype( &SymFunctionTableAccess ) pfnSymFunctionTableAccess;
typedef decltype( &SymGetModuleBase ) pfnSymGetModuleBase;
typedef decltype( &StackWalk ) pfnStackWalk;

// Include vendor libraries.
#include "dbgtrace.vendor.hwbrk.hxx"

namespace DbgTrace
{
    static const char* GetValidDebugSymbolPath( void )
    {
        // TODO: allow the debuggee to set this path using a FileSearch dialog.
        return "C:\\Users\\The_GTA\\Desktop\\mta_green\\symbols";
    }

    static bool _isDebugManagerInitialized = false;

    struct Win32DebugManager
    {
        volatile bool isInitialized;
        volatile bool isInsideDebugPhase;
        CRITICAL_SECTION debugLock;
        volatile HANDLE contextProcess;

        // Debug library pointers.
        HMODULE hDebugHelpLib;
        pfnSymInitialize            d_SymInitialize;
        pfnSymCleanup               d_SymCleanup;
        pfnSymSetOptions            d_SymSetOptions;
        pfnSymFromAddr              d_SymFromAddr;
        pfnSymGetLineFromAddr       d_SymGetLineFromAddr;
        pfnSymFunctionTableAccess   d_SymFunctionTableAccess;
        pfnSymGetModuleBase         d_SymGetModuleBase;
        pfnStackWalk                d_StackWalk;

        AINLINE Win32DebugManager( void )
        {
            isInitialized = false;
            isInsideDebugPhase = false;
            contextProcess = NULL;

            // Zero out debug pointers.
            this->hDebugHelpLib = NULL;

            this->d_SymInitialize = NULL;
            this->d_SymCleanup = NULL;
            this->d_SymSetOptions = NULL;
            this->d_SymFromAddr = NULL;
            this->d_SymGetLineFromAddr = NULL;
            this->d_SymFunctionTableAccess = NULL;
            this->d_SymGetModuleBase = NULL;
            this->d_StackWalk = NULL;

            InitializeCriticalSection( &debugLock );

            _isDebugManagerInitialized = true;
        }

        AINLINE ~Win32DebugManager( void )
        {
            _isDebugManagerInitialized = false;

            Shutdown();

            // Delete the debug library.
            if ( HMODULE hDebugLib = this->hDebugHelpLib )
            {
                FreeLibrary( hDebugLib );

                this->hDebugHelpLib = NULL;
            }

            DeleteCriticalSection( &debugLock );
        }

        AINLINE bool AttemptInitialize( void )
        {
            if ( isInitialized )
                return true;

            // We need the debug help library, if we do not have it already.
            HMODULE hDebugHelp = this->hDebugHelpLib;

            if ( hDebugHelp == NULL )
            {
                // Attempt to load it.
                hDebugHelp = LoadLibraryA( DBGHELP_DLL_NAME );

                // Store it if we got a valid handle.
                if ( hDebugHelp != NULL )
                {
                    this->hDebugHelpLib = hDebugHelp;
                }
            }

            // Only proceed if we got the handle.
            if ( hDebugHelp == NULL )
                return false;

            // Attempt to get the function handles.
            this->d_SymInitialize =             (pfnSymInitialize)GetProcAddress( hDebugHelp, "SymInitialize" );
            this->d_SymCleanup =                (pfnSymCleanup)GetProcAddress( hDebugHelp, "SymCleanup" );
            this->d_SymSetOptions =             (pfnSymSetOptions)GetProcAddress( hDebugHelp, "SymSetOptions" );
            this->d_SymFromAddr =               (pfnSymFromAddr)GetProcAddress( hDebugHelp, "SymFromAddr" );
            this->d_SymGetLineFromAddr =        (pfnSymGetLineFromAddr)GetProcAddress( hDebugHelp, "SymGetLineFromAddr" );
            this->d_SymFunctionTableAccess =    (pfnSymFunctionTableAccess)GetProcAddress( hDebugHelp, "SymFunctionTableAccess" );
            this->d_SymGetModuleBase =          (pfnSymGetModuleBase)GetProcAddress( hDebugHelp, "SymGetModuleBase" );
            this->d_StackWalk =                 (pfnStackWalk)GetProcAddress( hDebugHelp, "StackWalk" );

            bool successful = false;

            if ( pfnSymInitialize _SymInitialize = this->d_SymInitialize )
            {
                contextProcess = GetCurrentProcess();

                const char *debSymbPath = GetValidDebugSymbolPath();

                __try
                {
                    BOOL initializeSuccessful =
                        _SymInitialize( contextProcess, debSymbPath, true );

                    successful = ( initializeSuccessful == TRUE );
                }
                __except( EXCEPTION_EXECUTE_HANDLER )
                {
                    // We are unsuccessful.
                    successful = false;
                }
            }

            if ( successful )
            {
                isInitialized = successful;
            }

            return successful;
        }

        AINLINE void Shutdown( void )
        {
            assert( isInsideDebugPhase == false );

            if ( !isInitialized )
                return;

            if ( pfnSymCleanup _SymCleanup = this->d_SymCleanup )
            {
                _SymCleanup( contextProcess );
            }

            isInitialized = false;
        }

        AINLINE bool Begin( void )
        {
            // DbgHelp.dll is single-threaded, hence we must get a lock.
            EnterCriticalSection( &debugLock );

            assert( isInsideDebugPhase == false );

            bool isInitialized = AttemptInitialize();

            if ( !isInitialized )
            {
                LeaveCriticalSection( &debugLock );
            }
            else
            {
                if ( pfnSymSetOptions _SymSetOptions = this->d_SymSetOptions )
                {
                    // If we successfully initialized the debug library, set it up properly.
                    _SymSetOptions(
                        SYMOPT_DEFERRED_LOADS |
                        SYMOPT_LOAD_LINES |
                        SYMOPT_UNDNAME
                    );
                }

                isInsideDebugPhase = true;
            }

            return isInitialized;
        }

        AINLINE bool IsInDebugPhase( void ) const
        {
            return this->isInsideDebugPhase;
        }

        AINLINE void End( void )
        {
            assert( isInsideDebugPhase == true );

            LeaveCriticalSection( &debugLock );

            isInsideDebugPhase = false;
        }

        struct InternalSymbolInfo : public SYMBOL_INFO
        {
            TCHAR Name_extended[255];
        };

        AINLINE void GetDebugInfoForAddress( void *addrPtr, CallStackEntry& csInfo )
        {
            const SIZE_T addrAsOffset = (SIZE_T)addrPtr;

            if ( pfnSymFromAddr _SymFromAddr = this->d_SymFromAddr )
            {
                // Get Information about the stack frame contents.
                InternalSymbolInfo addrSymbolInfo;
                addrSymbolInfo.SizeOfStruct = sizeof( SYMBOL_INFO );
                addrSymbolInfo.MaxNameLen = NUMELMS( addrSymbolInfo.Name_extended ) + NUMELMS( addrSymbolInfo.Name );

                DWORD64 displacementPtr;
                
                BOOL symbolFetchResult = _SymFromAddr( contextProcess, addrAsOffset, &displacementPtr, &addrSymbolInfo );

                if ( symbolFetchResult == TRUE )
                {
                    csInfo.symbolName = addrSymbolInfo.Name;
                }
            }

            if ( pfnSymGetLineFromAddr _SymGetLineFromAddr = this->d_SymGetLineFromAddr )
            {
                // Get Information about the line and the file of the context.
                IMAGEHLP_LINE symbolLineInfo;
                symbolLineInfo.SizeOfStruct = sizeof( IMAGEHLP_LINE );

                DWORD displacementPtr;

                BOOL symbolLineFetchResult = _SymGetLineFromAddr( contextProcess, addrAsOffset, &displacementPtr, &symbolLineInfo );

                if ( symbolLineFetchResult == TRUE )
                {
                    csInfo.symbolFile = symbolLineInfo.FileName;
                    csInfo.symbolFileLine = symbolLineInfo.LineNumber;
                }
            }
        }
    };

    static char _debugManAllocSpace[ sizeof( Win32DebugManager ) ];

    static Win32DebugManager *debugMan = NULL;

    struct Win32EnvSnapshot : public IEnvSnapshot
    {
        static BOOL CALLBACK MemoryReadFunction(
            HANDLE hProcess, SIZE_T lpBaseAddress, PVOID lpBuffer, DWORD nSize, LPDWORD lpNumberOfBytesRead
        )
        {
            assert( hProcess == NULL );

            __try
            {
                memmove( lpBuffer, (void*)lpBaseAddress, nSize );
            }
            __except( EXCEPTION_EXECUTE_HANDLER )
            {
                *lpNumberOfBytesRead = 0;
                return FALSE;
            }

            *lpNumberOfBytesRead = nSize;
            return TRUE;
        }

        static PVOID CALLBACK FunctionTableAccess( HANDLE hProcess, SIZE_T addrBase )
        {
            assert( hProcess == NULL );

            PVOID result = NULL;

            if ( debugMan )
            {
                if ( pfnSymFunctionTableAccess _SymFunctionTableAccess = debugMan->d_SymFunctionTableAccess )
                {
                    result = _SymFunctionTableAccess( GetCurrentProcess(), addrBase );
                }
            }

            return result;
        }

        static SIZE_T CALLBACK GetModuleBaseFunction( HANDLE hProcess, SIZE_T Address )
        {
            assert( hProcess == NULL );

            SIZE_T base = 0;

            if ( debugMan )
            {
                if ( pfnSymGetModuleBase _SymGetModuleBase = debugMan->d_SymGetModuleBase )
                {
                    base = _SymGetModuleBase( GetCurrentProcess(), Address );
                }
            }

            return base;
        }

        AINLINE Win32EnvSnapshot( const CONTEXT& theContext )
        {
            // Save the context.
            runtimeContext = theContext;

            if ( !debugMan )
                return;

            // Construct the call-stack using debug information.
            bool isDebugLibraryInitialized = debugMan->Begin();

            if ( isDebugLibraryInitialized )
            {
                if ( pfnStackWalk _StackWalk = debugMan->d_StackWalk )
                {
                    // Build the call stack.
                    CONTEXT walkContext = theContext;
                    
                    // Walk through the call frames.
                    STACKFRAME outputFrame;
                    memset( &outputFrame, 0, sizeof( outputFrame ) );

                    DWORD machineType;

#ifdef _M_IX86
                    machineType = IMAGE_FILE_MACHINE_I386;

                    outputFrame.AddrPC.Offset =     runtimeContext.Eip;
                    outputFrame.AddrPC.Mode =       AddrModeFlat;
                    outputFrame.AddrFrame.Offset =  runtimeContext.Ebp;
                    outputFrame.AddrFrame.Mode =    AddrModeFlat;
                    outputFrame.AddrStack.Offset =  runtimeContext.Esp;
                    outputFrame.AddrStack.Mode =    AddrModeFlat;
#else

                    machineType = IMAGE_FILE_MACHINE_AMD64;

                    outputFrame.AddrPC.Offset =     runtimeContext.Rip;
                    outputFrame.AddrPC.Mode =       AddrModeFlat;
                    outputFrame.AddrFrame.Offset =  runtimeContext.Rbp;
                    outputFrame.AddrFrame.Mode =    AddrModeFlat;
                    outputFrame.AddrStack.Offset =  runtimeContext.Rsp;
                    outputFrame.AddrStack.Mode =    AddrModeFlat;

#endif

                    while (
                        _StackWalk( machineType, NULL, NULL, &outputFrame, &walkContext,
                            MemoryReadFunction,
                            FunctionTableAccess,
                            GetModuleBaseFunction,
                            NULL ) )
                    {
                        // Get the offset as pointer.
                        void *offsetPtr = (void*)outputFrame.AddrPC.Offset;

                        // Construct a call stack entry.
                        CallStackEntry contextRuntimeInfo( offsetPtr );

                        if ( isDebugLibraryInitialized )
                        {
                            debugMan->GetDebugInfoForAddress( offsetPtr, contextRuntimeInfo );
                        }

                        callstack.push_back( contextRuntimeInfo );
                    }
                }

                // If we have been using the symbol runtime, free its resources.
                debugMan->End();
            }
        }

        AINLINE ~Win32EnvSnapshot( void )
        {
            return;
        }

        Win32EnvSnapshot* Clone( void )
        {
            return new Win32EnvSnapshot( runtimeContext );
        }

        void RestoreTo( void )
        {
            SetThreadContext( GetCurrentThread(), &runtimeContext );
        }

        callStack_t GetCallStack( void )
        {
            return callstack;
        }

        inline std::string GetTrimmedString( const std::string& theString, size_t maxLen )
        {
            std::string output;

            size_t stringLength = theString.length();

            if ( stringLength > maxLen )
            {
                // Include the ending part.
                const std::string endingPart = "...";

                size_t newMaxLen = ( maxLen - endingPart.length() );

                // Return a trimmed version.
                output = endingPart + theString.substr( stringLength - newMaxLen, newMaxLen );
            }
            else
            {
                output = theString;
            }

            return output;
        }

        std::string ToString( void )
        {
            std::string outputBuffer = "Call Frames:\n";

            int n = 1;

            // List all call-stack frames into the output buffer.
            for ( callStack_t::const_iterator iter = callstack.begin(); iter != callstack.end(); iter++ )
            {
                const CallStackEntry& csInfo = *iter;

                outputBuffer += std::to_string( n++ );
                outputBuffer += "-- ";
                {
                    const std::string symbolName = csInfo.GetSymbolName();

                    if ( symbolName.size() == 0 )
                    {
                        outputBuffer += "[0x";
                        outputBuffer += to_string_hex( (size_t)csInfo.codePtr );
                        outputBuffer += "]";
                    }
                    else
                    {
                        outputBuffer += symbolName;
                        outputBuffer += " at 0x";
                        outputBuffer += to_string_hex( (size_t)csInfo.codePtr );
                    }
                }
                outputBuffer += " (";
                {
                    std::string fileName = csInfo.GetFileName();

                    // Since the filename can be pretty long, it needs special attention.
                    eir::MultiString <CRTHeapAllocator> directoryPart;
                    
                    auto fileNameItem = FileSystem::GetFileNameItem <CRTHeapAllocator> ( fileName.c_str(), true, &directoryPart, nullptr );

                    // Trim the directory.
                    auto ansiDirPart = directoryPart.convert_ansi <CRTHeapAllocator> ();
                    auto ansiFileNameItem = fileNameItem.convert_ansi <CRTHeapAllocator> ();

                    outputBuffer += GetTrimmedString( ansiDirPart.GetConstString(), 20 );
                    outputBuffer.append( ansiFileNameItem.GetConstString() );
                }
                outputBuffer += ":";
                outputBuffer += std::to_string( csInfo.GetLineNumber() );
                outputBuffer += ")\n";
            }

            return outputBuffer;
        }

        CONTEXT runtimeContext;
        callStack_t callstack;
    };

    static bool CaptureRuntimeContext( CONTEXT& outputContext )
    {
        bool successful = false;

        CONTEXT runtimeContext;
        memset( &runtimeContext, 0, sizeof( runtimeContext ) );

        RtlCaptureContext( &runtimeContext );

        outputContext = runtimeContext;

        successful = true;

        return successful;
    }

    IEnvSnapshot* CreateEnvironmentSnapshot( void )
    {
        if ( _isDebugManagerInitialized == false )
        {
            return NULL;
        }

        if ( !debugMan )
        {
            // Just making sure.
            return NULL;
        }

        if ( debugMan->IsInDebugPhase() )
        {
            // If the debug manager is busy already, we cannot continue.
            return NULL;
        }

        CONTEXT theContext;
#if 0
        memset( &theContext, 0, sizeof( theContext ) );

        theContext.ContextFlags = CONTEXT_CONTROL;
#endif

        bool gotContext = CaptureRuntimeContext( theContext );

        IEnvSnapshot *returnObj = NULL;

        if ( gotContext )
        {
            returnObj = new Win32EnvSnapshot( theContext );
        }

        return returnObj;
    }

    IEnvSnapshot* CreateEnvironmentSnapshotFromContext( const CONTEXT *const runtimeContext )
    {
        try
        {
            return new Win32EnvSnapshot( *runtimeContext );
        }
        catch( ... )
        {
            return NULL;
        }
    }

    // Here because of legacy support.
    struct NT_EXCEPTION_REGISTRATION_RECORD
    {
        NT_EXCEPTION_REGISTRATION_RECORD *Next;
        PEXCEPTION_ROUTINE Handler;
    };

    struct DBG_NT_TIB
    {
        NT_EXCEPTION_REGISTRATION_RECORD *ExceptionList;
        PVOID StackBase;
        PVOID StackLimit;
        PVOID SubSystemTib;
        PVOID FiberData;
        PVOID ArbitraryUserPointer;
        DBG_NT_TIB *Self;
    };

    // Thanks to NirSoft!
    namespace TIB_HELPER
    {
        static NT_EXCEPTION_REGISTRATION_RECORD* GetInvalidExceptionRecord( void )
        {
            return (NT_EXCEPTION_REGISTRATION_RECORD*)-1;
        }

        inline void PushExceptionRegistration( DBG_NT_TIB& tib, NT_EXCEPTION_REGISTRATION_RECORD *recordEntry )
        {
            // Make sure we execute the exception record we had before registering this one after
            // the one that is being added now.
            recordEntry->Next = tib.ExceptionList;

            // Set the given record as the current exception handler.
            tib.ExceptionList = recordEntry;
        }

        inline NT_EXCEPTION_REGISTRATION_RECORD* GetExceptionRegistrationTop( DBG_NT_TIB& tib )
        {
            return tib.ExceptionList;
        }

        inline void PopExceptionRegistration( DBG_NT_TIB& tib )
        {
            // We just remove the top most handler, if there is one.
            if ( tib.ExceptionList == GetInvalidExceptionRecord() )
                return;

            tib.ExceptionList = tib.ExceptionList->Next;
        }
    }

    static DBG_NT_TIB& GetThreadEnvironmentBlock( void )
    {
        DBG_NT_TIB *blockPtr = (DBG_NT_TIB*)NtCurrentTeb();

        return *blockPtr;
    }

#define EH_NONCONTINUABLE   0x01
#define EH_UNWINDING        0x02
#define EH_EXIT_UNWIND      0x04
#define EH_STACK_INVALID    0x08
#define EH_NESTED_CALL      0x10

    static DbgTraceStackSpace *stackSpace = NULL;

    static HANDLE _privateHeap = NULL;

    struct Win32ExceptionRegistrationRecord : public NT_EXCEPTION_REGISTRATION_RECORD
    {
        typedef dbgtraceGrowableArray <IExceptionHandler*> exceptions_t;

        exceptions_t registeredExceptions;

        static EXCEPTION_DISPOSITION NTAPI StaticHandlerRoutine(
            EXCEPTION_RECORD*   ExceptionRecord,
            void*               EstablisherFrame,
            CONTEXT*            ContextRecord,
            void*               DispatcherContext )
        {
            Win32ExceptionRegistrationRecord *thisPtr = (Win32ExceptionRegistrationRecord*)EstablisherFrame;

            bool hasBeenHandled = false;

            if ( !( ExceptionRecord->ExceptionFlags & ( EH_UNWINDING | EH_EXIT_UNWIND ) ) )
            {
                IEnvSnapshot *envSnapShot = CreateEnvironmentSnapshotFromContext( ContextRecord );

                const size_t itemCount = thisPtr->registeredExceptions.GetCount();

                for ( size_t n = 0; n < itemCount; n++ )
                {
                    IExceptionHandler *const userHandler = thisPtr->registeredExceptions.Get( n );

                    bool userHandled = userHandler->OnException( ExceptionRecord->ExceptionCode, envSnapShot );

                    if ( userHandled )
                    {
                        hasBeenHandled = true;
                    }
                }
            }

            return ( hasBeenHandled ) ? ExceptionContinueExecution : ExceptionContinueSearch;
        }

        inline Win32ExceptionRegistrationRecord( void ) : registeredExceptions( dbgtracePrivateHeapAllocator( _privateHeap ) )
        {
            this->Handler = StaticHandlerRoutine;
            this->Next = TIB_HELPER::GetInvalidExceptionRecord();
        }

        inline ~Win32ExceptionRegistrationRecord( void )
        {
            return;
        }

        inline void PushRecord( void )
        {
            TIB_HELPER::PushExceptionRegistration( GetThreadEnvironmentBlock(), this );
        }

        inline void PopRecord( void )
        {
            DBG_NT_TIB& threadBlock = GetThreadEnvironmentBlock();

            assert( TIB_HELPER::GetExceptionRegistrationTop( threadBlock ) == this );

            TIB_HELPER::PopExceptionRegistration( threadBlock );
        }

        inline bool IsUserHandlerRegistered( IExceptionHandler *theHandler )
        {
            return this->registeredExceptions.Find( theHandler );
        }

        inline void RegisterUserHandler( IExceptionHandler *theHandler )
        {
            if ( !IsUserHandlerRegistered( theHandler ) )
            {
                this->registeredExceptions.AddItem( theHandler );
            }
        }

        inline void UnregisterUserHandler( IExceptionHandler *theHandler )
        {
            this->registeredExceptions.RemoveItem( theHandler );
        }

        // Construction is very special for this container: it must be allocated on stack space on Win32.
        // Otherwise SEH will fault for us.
        inline void* operator new( size_t memSize )
        {
            assert( memSize < sizeof( DbgTraceStackSpace ) );

            return stackSpace;
        }

        inline void operator delete( void *memPtr )
        {
            return;
        }
    };

    static Win32ExceptionRegistrationRecord *runtimeRecord;

    void __declspec(nothrow) InitializeExceptionSystem( void )
    {
        // Create the debugging environment.
        debugMan = new (_debugManAllocSpace) Win32DebugManager();

        assert( debugMan != NULL );

        // Need a private heap for critical allocations.
        _privateHeap = HeapCreate( 0, 0, 0 );

        assert( _privateHeap != NULL );

        // We need an initial runtime record.
        runtimeRecord = new Win32ExceptionRegistrationRecord();

        runtimeRecord->PushRecord();
    }

    void __declspec(nothrow) ShutdownExceptionSystem( void )
    {
        runtimeRecord->PopRecord();

        delete runtimeRecord;

        // Destroy the private heap freeing all the memory this module used, hopefully.
        HeapDestroy( _privateHeap );

        // Delete the debugging environment.
        debugMan->~Win32DebugManager();

        debugMan = NULL;
    }

    void RegisterExceptionHandler( IExceptionHandler *handler )
    {
        runtimeRecord->RegisterUserHandler( handler );
    }

    void UnregisterExceptionHandler( IExceptionHandler *handler )
    {
        runtimeRecord->UnregisterUserHandler( handler );
    }
};

void DbgTrace_Init( DbgTraceStackSpace& stackSpace )
{
    // Set a private pointer that will keep pointing to that allocated stack space.
    DbgTrace::stackSpace = &stackSpace;

    DbgTrace::InitializeExceptionSystem();
}

void DbgTrace_InitializeGlobalDebug( void )
{
    // Put special debug code here.
    //SetHardwareBreakpoint( GetCurrentThread(), HWBRK_TYPE_WRITE, HWBRK_SIZE_4, (void*)0x001548E0 );
}

void DbgTrace_Shutdown( void )
{
    DbgTrace::ShutdownExceptionSystem();
}

#endif //_DEBUG_TRACE_LIBRARY_