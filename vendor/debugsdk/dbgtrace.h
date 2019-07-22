/*****************************************************************************
*
*  PROJECT:     Multi Theft Auto v1.2
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        debugsdk/dbgtrace.h
*  PURPOSE:     Win32 exception tracing tool for error isolation
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _DEBUG_TRACE_LIBRARY_HEADER_
#define _DEBUG_TRACE_LIBRARY_HEADER_

#include <string>
#include <list>

// DbgTrace API.
namespace DbgTrace
{
    struct CallStackEntry
    {
        inline CallStackEntry( void *addrPtr )
        {
            this->codePtr = addrPtr;
            this->symbolName = "";
            this->symbolFile = "";
            this->symbolFileLine = -1;
        }

        inline std::string GetFileName( void ) const
        {
            if ( symbolFile.size() == 0 )
            {
                return "unknown";
            }

            return symbolFile;
        }

        inline std::string GetSymbolName( void ) const
        {
            return symbolName;
        }

        inline int GetLineNumber( void ) const
        {
            return symbolFileLine;
        }

        void *codePtr;                  // address of code that the runtime is positioned at (required)
        std::string symbolName;         // name of the segment that the runtime is in (length zero if not given)
        std::string symbolFile;         // name of the file that belongs to this segment (length zero if not given)
        unsigned int symbolFileLine;    // line number inside of the symbol file (-1 if none given)
    };
    typedef std::list <CallStackEntry> callStack_t;

    struct IEnvSnapshot
    {
        virtual ~IEnvSnapshot( void )   {}

        // Clones this context.
        virtual IEnvSnapshot* Clone( void ) = 0;

        // Restores the running thread to this context.
        // This function never returns.
        virtual void RestoreTo( void ) = 0;

        // Obtains the callstack of this snapshot.
        // For this, the whole stack is being traversed.
        virtual callStack_t GetCallStack( void ) = 0;

        // Returns a string representation of this context's contents.
        // This is useful for debugging purposes.
        virtual std::string ToString( void ) = 0;
    };

    IEnvSnapshot*   CreateEnvironmentSnapshot( void );

    struct IExceptionHandler
    {
        virtual ~IExceptionHandler( void )          {}

        // Returns whether the exception has been handled.
        // This means that we do not have to walk down the exception stack.
        virtual bool OnException( unsigned int error_code, IEnvSnapshot *snapshot ) = 0;
    };

    void RegisterExceptionHandler( IExceptionHandler *handler );
    void UnregisterExceptionHandler( IExceptionHandler *handler );
};

// While DbgTrace is running it must have a slot on the stack allocated to catch exceptions properly.
// This is an implementation dependant feature.
struct DbgTraceStackSpace
{
#ifdef _WIN32
    // Reserved for DbgTrace.
    char reserved[512];
#endif //_WIN32
};

// Module initialization.
void DbgTrace_Init( DbgTraceStackSpace& stackSpace );
void DbgTrace_InitializeGlobalDebug( void );
void DbgTrace_Shutdown( void );

#endif //_DEBUG_TRACE_LIBRARY_HEADER_
