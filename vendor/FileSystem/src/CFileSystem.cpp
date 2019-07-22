/*****************************************************************************
*
*  PROJECT:     Eir FileSystem
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        FileSystem/src/CFileSystem.cpp
*  PURPOSE:     File management
*  DEVELOPERS:  S2 Games <http://savage.s2games.com> (historical entry)
*
*  Get Eir FileSystem from https://osdn.net/projects/eirfs/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include <StdInc.h>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <chrono>

#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#endif

// Include internal header.
#include "fsinternal/CFileSystem.internal.h"

// Sub modules.
#include "fsinternal/CFileSystem.platform.h"
#include "fsinternal/CFileSystem.stream.raw.h"
#include "fsinternal/CFileSystem.translator.system.h"
#include "fsinternal/CFileSystem.platformutils.hxx"

#include "CFileSystem.Utils.hxx"

// Include native platform utilities.
#include "fsinternal/CFileSystem.internal.nativeimpl.hxx"

// Include threading utilities for CFileSystem class.
#include "CFileSystem.lock.hxx"

using namespace FileSystem;

CFileSystem *fileSystem = nullptr;
CFileTranslator *fileRoot = nullptr;

// Library internal thing.
CFileSystemNative *nativeFileSystem = nullptr;

// Create the class at runtime initialization.
CSystemCapabilities systemCapabilities;

// Constructor of the CFileSystem instance.
// Every driver should register itself in this.
fileSystemFactory_t _fileSysFactory;

// Allocator of plugin meta-data.
// This one is globally required.
static FSHeapAllocator _memAlloc;

NativeHeapAllocator FSHeapAllocator::heapAlloc;

/*=======================================
    CFileSystem

    Management class with root-access functions.
    These methods are root-access. Exposing them
    to a security-critical user-space context is
    not viable.
=======================================*/
static bool _hasBeenInitialized = false;

// Integrity check function.
// If this fails, then CFileSystem cannot boot.
inline bool _CheckLibraryIntegrity( void )
{
    // Check all data types.
    bool isValid = true;

    if ( sizeof(fsBool_t) != 1 ||
         sizeof(fsChar_t) != 1 || sizeof(fsUChar_t) != 1 ||
         sizeof(fsShort_t) != 2 || sizeof(fsUShort_t) != 2 ||
         sizeof(fsInt_t) != 4 || sizeof(fsUInt_t) != 4 ||
         sizeof(fsWideInt_t) != 8 || sizeof(fsUWideInt_t) != 8 ||
         sizeof(fsFloat_t) != 4 ||
         sizeof(fsDouble_t) != 8 )
    {
        isValid = false;
    }

#ifdef _DEBUG
    if ( !isValid )
    {
        // Notify the developer.
        assert( 0 );
    }
#endif //_DEBUG

    return isValid;
}

// Internal plugins.
fsLockProvider _fileSysLockProvider;

// Sub modules.
extern void registerFileSystemMemoryManagement( void );
extern void registerRandomGeneratorExtension( const fs_construction_params& params );
extern void registerFileSystemMemoryMappedStreams( void );
extern void registerFileDataPresenceManagement( const fs_construction_params& params );

extern void unregisterFileSystemMemoryManagement( void );
extern void unregisterFileSystemMemoryMappedStreams( void );
extern void unregisterRandomGeneratorExtension( void );
extern void unregisterFileDataPresenceManagement( void );

AINLINE void InitializeLibrary( const fs_construction_params& params )
{
    // Register addons.
    registerFileSystemMemoryManagement();
    registerRandomGeneratorExtension( params );
    _fileSysLockProvider.RegisterPlugin( params );
    registerFileSystemMemoryMappedStreams();
    registerFileDataPresenceManagement( params );

    CFileSystemNative::RegisterZIPDriver( params );
    CFileSystemNative::RegisterIMGDriver( params );
}

AINLINE void ShutdownLibrary( void )
{
    // Unregister all addons.
    CFileSystemNative::UnregisterIMGDriver();
    CFileSystemNative::UnregisterZIPDriver();

    unregisterFileDataPresenceManagement();
    unregisterFileSystemMemoryMappedStreams();
    _fileSysLockProvider.UnregisterPlugin();
    unregisterRandomGeneratorExtension();
    unregisterFileSystemMemoryManagement();
}

struct fs_constructor
{
    const fs_construction_params& params;

    inline fs_constructor( const fs_construction_params& params ) : params( params )
    {
        return;
    }

    inline CFileSystemNative* Construct( void *mem ) const
    {
        return new (mem) CFileSystemNative( this->params );
    }
};

// Creators of the CFileSystem instance.
// Those are the entry points to this (static) library.
CFileSystem* CFileSystem::Create( const fs_construction_params& params )
{
    // Make sure that there is no second CFileSystem class alive.
    assert( _hasBeenInitialized == false );

    // Make sure our environment can run CFileSystem in the first place.
    bool isLibraryBootable = _CheckLibraryIntegrity();

    if ( !isLibraryBootable )
    {
        // We failed some critical integrity tests.
        return nullptr;
    }

    InitializeLibrary( params );

    CFileSystem *readyInstance = nullptr;

    try
    {
        // Create our CFileSystem instance!
        fs_constructor constructor( params );

        CFileSystemNative *instance = _fileSysFactory.ConstructTemplate( _memAlloc, constructor );

        if ( instance )
        {
            // Get the application current directory and store it in "fileRoot" global.
            try
            {
                // Every application should be able to access itself
                fileRoot = instance->CreateTranslator( params.fileRootPath );
            }
            catch( ... )
            {
                _fileSysFactory.Destroy( _memAlloc, instance );

                throw;
            }

            // We have initialized ourselves.
            _hasBeenInitialized = true;
        }

        readyInstance = instance;
    }
    catch( ... )
    {
        // We do not want to pass on exceptions.
        _hasBeenInitialized = false;

        // Continue.
    }

    if ( !_hasBeenInitialized )
    {
        ShutdownLibrary();
    }

    return readyInstance;
}

void CFileSystem::Destroy( CFileSystem *lib )
{
    CFileSystemNative *nativeLib = (CFileSystemNative*)lib;

    assert( nativeLib != nullptr );

    if ( nativeLib )
    {
        try
        {
            // Delete the main fileRoot access point.
            if ( CFileTranslator *rootAppDir = fileRoot )
            {
                delete rootAppDir;

                fileRoot = nullptr;
            }

            _fileSysFactory.Destroy( _memAlloc, nativeLib );

            ShutdownLibrary();
        }
        catch( ... )
        {
            // Must not throw exceptions.
            abort();

            throw;
        }

        // We have successfully destroyed FileSystem activity.
        _hasBeenInitialized = false;
    }
}

#ifdef _WIN32

struct MySecurityAttributes
{
    DWORD count;
    LUID_AND_ATTRIBUTES attr[2];
};

#endif //_WIN32

CFileSystem::CFileSystem( const fs_construction_params& params )
{
    // Set up members.
    m_includeAllDirsInScan = false;
#ifdef _WIN32
    m_hasDirectoryAccessPriviledge = false;
#endif //_WIN32
    m_doBufferAllRaw = false;
#ifdef _WIN32
    m_useExtendedPaths = true;
#endif //_WIN32

    // Set the global fileSystem variable.
    fileSystem = this;

#ifdef _WIN32
    // Check for legacy paths.
    // We will have to convert certain paths if that is the case.
    bool isLegacyOS = false;
    {
        OSVERSIONINFOEXA winInfo;
        winInfo.dwOSVersionInfoSize = sizeof(winInfo);
        winInfo.dwMajorVersion = 6;
        winInfo.dwMinorVersion = 1;

        // Check if we are the on the same branch.
        DWORDLONG verCompMask = 0;

        VER_SET_CONDITION( verCompMask, VER_MAJORVERSION, VER_EQUAL );
        VER_SET_CONDITION( verCompMask, VER_MINORVERSION, VER_GREATER_EQUAL );

        BOOL doesSatisfy =
            VerifyVersionInfoA(
                &winInfo,
                VER_MAJORVERSION | VER_MINORVERSION,
                verCompMask
            );

        if ( doesSatisfy == FALSE )
        {
            // Check if we are much newer.
            verCompMask = 0;

            VER_SET_CONDITION( verCompMask, VER_MAJORVERSION, VER_GREATER_EQUAL );

            doesSatisfy =
                VerifyVersionInfoA(
                    &winInfo,
                    VER_MAJORVERSION,
                    verCompMask
                );
        }

        isLegacyOS = ( doesSatisfy == FALSE );
    }

    this->m_win32HasLegacyPaths = isLegacyOS;
#endif //_WIN32
}

CFileSystem::~CFileSystem( void )
{
    // Zero the main FileSystem access point.
    fileSystem = nullptr;
}

bool CFileSystem::IsInLegacyMode( void ) const
{
#ifdef _WIN32
    if ( this->m_win32HasLegacyPaths )
        return true;
#endif //_WIN32

    return false;
}

bool CFileSystem::CanLockDirectories( void )
{
#ifdef FILESYS_MULTI_THREADING
    NativeExecutive::CReadWriteWriteContextSafe <> consistency( _fileSysLockProvider.GetReadWriteLock( this ) );
#endif //FILESYS_MULTI_THREADING

    // We should set special priviledges for the application if
    // running under Win32.
#ifdef _WIN32
    // We assume getting the priviledge once is enough.
    if ( !m_hasDirectoryAccessPriviledge )
    {
        HANDLE token;

        // We need SE_BACKUP_NAME to gain directory access on Windows
        if ( OpenProcessToken( GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token ) == TRUE )
        {
            MySecurityAttributes priv;

            priv.count = 2; // we want to request two priviledges.

            BOOL backupRequest = LookupPrivilegeValueA( nullptr, "SeBackupPrivilege", &priv.attr[0].Luid );

            priv.attr[0].Attributes = SE_PRIVILEGE_ENABLED;

            BOOL restoreRequest = LookupPrivilegeValueA( nullptr, "SeRestorePrivilege", &priv.attr[1].Luid );

            priv.attr[1].Attributes = SE_PRIVILEGE_ENABLED;

            if ( backupRequest == TRUE && restoreRequest == TRUE )
            {
                BOOL success = AdjustTokenPrivileges( token, false, (TOKEN_PRIVILEGES*)&priv, sizeof( priv ), nullptr, nullptr );

                if ( success == TRUE )
                {
                    m_hasDirectoryAccessPriviledge = true;
                }
            }

            CloseHandle( token );
        }
    }
    return m_hasDirectoryAccessPriviledge;
#elif defined(__linux__)
    // We assume that we can always lock directories under Unix.
    // This is actually a lie, because it does not exist.
    return true;
#else
    // No idea about directory locking on other environments.
#error Missing implementation for CFileSystem::CanLockDirectories
#endif //OS DEPENDENT CODE
}

template <typename charType>
bool CFileSystemNative::GenGetSystemRootDescriptor( const charType *path, filePath& descOut ) const
{
    platform_rootPathType rootPath;

    bool gotRootPath = rootPath.buildFromSystemPath( path, false );

    if ( !gotRootPath )
    {
        return false;
    }

    descOut = rootPath.RootDescriptor();
    return true;
}

bool CFileSystem::GetSystemRootDescriptor( const char *path, filePath& descOut ) const      { return ((CFileSystemNative*)this)->GenGetSystemRootDescriptor( path, descOut ); }
bool CFileSystem::GetSystemRootDescriptor( const wchar_t *path, filePath& descOut ) const   { return ((CFileSystemNative*)this)->GenGetSystemRootDescriptor( path, descOut ); }
bool CFileSystem::GetSystemRootDescriptor( const char8_t *path, filePath& descOut ) const   { return ((CFileSystemNative*)this)->GenGetSystemRootDescriptor( path, descOut ); }

template <typename charType>
CFileTranslator* CFileSystemNative::GenCreateTranslator( const charType *path, eDirOpenFlags flags )
{
    // Without access to directory locking, this function can not execute.
    if ( !CanLockDirectories() )
        return nullptr;

    // THREAD-SAFE, because this function does not use shared-state variables.

    platform_rootPathType rootPath;

    try
    {
        // Check for the "//" application root path descriptor.
        character_env_iterator_tozero <charType> iter( path );

        if ( iter.IsEnd() == false )
        {
            auto first_char = iter.ResolveAndIncrement();

            if ( first_char == '/' && iter.IsEnd() == false && iter.Resolve() == '/' )
            {
                iter.Increment();

                const charType *nodePath = iter.GetPointer();

                filePath appRootPath;
                _FileSys_AppendApplicationRootDirectory( appRootPath );

                appRootPath += nodePath;

                bool couldResolve = appRootPath.char_dispatch(
                    [&]( const auto *appRoot )
                {
                    return rootPath.buildFromSystemPath( appRoot, false );
                });

                if ( couldResolve )
                {
                    goto rootPathGot;
                }

                return nullptr;
            }
        }

        // Try to build a mere system root path.
        if ( rootPath.buildFromSystemPath( path, false ) )
        {
            goto rootPathGot;
        }

        // Try a relative path from the current system directory.
        filePath cwdRoot;
        _FileSys_AppendCurrentWorkingDirectory( cwdRoot );
        cwdRoot += '\\';       // I guess it turns out the platforms return a backslash in their cwd?
        cwdRoot += path;

        bool couldResolve = cwdRoot.char_dispatch(
            [&]( const auto *cwdRoot )
        {
            return rootPath.buildFromSystemPath( cwdRoot, false );
        });

        if ( !couldResolve )
        {
            throw filesystem_exception( eGenExceptCode::INVALID_SYSPARAM );
        }
    }
    catch( filesystem_exception& except )
    {
        if ( except.code == eGenExceptCode::ILLEGAL_PATHCHAR )
        {
            return nullptr;
        }

        throw;
    }
    catch( codepoint_exception& )
    {
        return nullptr;
    }

rootPathGot:
    bool slashDirection;
    filePath root;

#ifdef _WIN32
    bool shouldBeExtended = fileSystem->m_useExtendedPaths;

    slashDirection = rootPath.DecideSlashDirectionExtended( shouldBeExtended );
    root = rootPath.RootDescriptorExtended( shouldBeExtended );
#else
    root = rootPath.RootDescriptor();
    slashDirection = rootPath.DecideSlashDirection();
#endif //OS DEPENDANT CODE

    _File_OutputPathTree( rootPath.RootNodes(), rootPath.IsFilePath(), slashDirection, root );

#ifdef _WIN32
    HANDLE dir = _FileWin32_OpenDirectoryHandle( root, flags );

    if ( dir == INVALID_HANDLE_VALUE )
        return nullptr;
#elif defined(__linux__)
    root.transform_to <char> ();

    DIR *dir = opendir( root.to_char <char> () );

    if ( dir == nullptr )
        return nullptr;
#else
#error Missing implementation for CFileSystem::CreateTranslator handle creation
#endif //OS DEPENDANT CODE

    try
    {
        CSystemFileTranslator *pTranslator = new CSystemFileTranslator( std::move( rootPath ) );

#ifdef _WIN32
        pTranslator->m_rootHandle = dir;
        pTranslator->m_curDirHandle = nullptr;
#elif defined(__linux__)
        pTranslator->m_rootHandle = dir;
        pTranslator->m_curDirHandle = nullptr;
#else
#error Missing implementation for CFileSystem::CreateTranslator handle acquisition
#endif //OS DEPENDANT CODE

        return pTranslator;
    }
    catch( ... )
    {
        // Release the OS handle.
#ifdef _WIN32
        CloseHandle( dir );
#elif defined(__linux__)
        closedir( dir );
#else
#error Missing implementation for CFileSystem::CreateTranslator exception cleanup
#endif //OS DEPENDANT CODE

        throw;
    }
}

CFileTranslator* CFileSystem::CreateTranslator( const char *path, eDirOpenFlags flags )         { return ((CFileSystemNative*)this)->GenCreateTranslator( path, flags ); }
CFileTranslator* CFileSystem::CreateTranslator( const wchar_t *path, eDirOpenFlags flags )      { return ((CFileSystemNative*)this)->GenCreateTranslator( path, flags ); }
CFileTranslator* CFileSystem::CreateTranslator( const char8_t *path, eDirOpenFlags flags )      { return ((CFileSystemNative*)this)->GenCreateTranslator( path, flags ); }

template <typename charType>
AINLINE CFileTranslator* CFileSystemNative::GenCreateSystemMinimumAccessPoint( const charType *path, eDirOpenFlags flags )
{
    platform_rootPathType rootPath;

    if ( !rootPath.buildFromSystemPath( path, false ) )
        return nullptr;

    filePath root = rootPath.RootDescriptor();

    dirNames tree = std::move( rootPath.RootNodes() );
    bool bFile = rootPath.IsFilePath();

    if ( bFile )
    {
        tree.RemoveFromBack();
    }

    // Try creating in the root itself.
    if ( CFileTranslator *rootTrans = CreateTranslator( root ) )
    {
        return rootTrans;
    }

    bool slashDir = rootPath.DecideSlashDirection();

    // Try creating the translator starting from the root.
    size_t n = 0;

    while ( true )
    {
        if ( n >= tree.GetCount() )
            break;

        const filePath& curAdd = tree[ n++ ];

        root += curAdd;
        root += FileSystem::GetDirectorySeparator <char> ( slashDir );

        CFileTranslator *tryTrans = CreateTranslator( root );

        if ( tryTrans )
        {
            return tryTrans;
        }
    }

    return nullptr;
}

CFileTranslator* CFileSystem::CreateSystemMinimumAccessPoint( const char *path, eDirOpenFlags flags )       { return ((CFileSystemNative*)this)->GenCreateSystemMinimumAccessPoint( path, flags ); }
CFileTranslator* CFileSystem::CreateSystemMinimumAccessPoint( const wchar_t *path, eDirOpenFlags flags )    { return ((CFileSystemNative*)this)->GenCreateSystemMinimumAccessPoint( path, flags ); }
CFileTranslator* CFileSystem::CreateSystemMinimumAccessPoint( const char8_t *path, eDirOpenFlags flags )    { return ((CFileSystemNative*)this)->GenCreateSystemMinimumAccessPoint( path, flags ); }

CArchiveTranslator* CFileSystem::GetArchiveTranslator( CFileTranslator *fileTrans )
{
    return dynamic_cast <CArchiveTranslator*> ( fileTrans );
}

CFile* CFileSystem::GenerateRandomFile( CFileTranslator *root, bool forcedReliability )
{
    // If forcedReliability == true, the runtime tells us that it cannot cope with failure
    // where success was probable to happen.

retryForReliability:
    // We try 42 times to create a randomly named file.
    unsigned long numAttempts = 0;

    while ( numAttempts++ < 42 )
    {
        // Generate some random filename.
        eir::String <char, FSObjectHeapAllocator> fileName = "$rnd";
        fileName += eir::to_string <char, FSObjectHeapAllocator> ( fsrandom::getSystemRandom( this ) );

        CFile *genFile = root->Open( fileName.GetConstString(), "wb+" );

        if ( genFile )
        {
            return genFile;
        }
    }

    if ( forcedReliability )
    {
        using namespace std::chrono_literals;

        // We probably should wait a little.
        std::this_thread::sleep_for( 5ms );

        goto retryForReliability;
    }

    // We failed. This is a valid outcome.
    return nullptr;
}

bool CFileSystem::IsDirectory( const char *path )
{
    return File_IsDirectoryAbsolute( path );
}

bool CFileSystem::Exists( const char *path )
{
    struct stat tmp;

    return stat( path, &tmp ) == 0;
}

size_t CFileSystem::Size( const char *path )
{
    struct stat stats;

    if ( stat( path, &stats ) != 0 )
        return 0;

    return stats.st_size;
}

// Utility to quickly load data from files on the local filesystem.
// Do not export it into user-space since this function has no security restrictions.
bool CFileSystem::ReadToBuffer( const char *path, fsDataBuffer& output )
{
#ifdef _WIN32
    HANDLE file = CreateFileA( path, GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr );

    if ( file == INVALID_HANDLE_VALUE )
        return false;

    size_t size = GetFileSize( file, nullptr );

    if ( size != 0 )
    {
        output.Resize( size );

        DWORD _pf;

        ReadFile( file, output.GetData(), (DWORD)size, &_pf, nullptr );
    }
    else
        output.Clear();

    CloseHandle( file );
    return true;
#elif defined(__linux__)
    int iReadFile = open( path, O_RDONLY, 0 );

    if ( iReadFile == -1 )
        return false;

    struct stat read_info;

    if ( fstat( iReadFile, &read_info ) != 0 )
        return false;

    if ( read_info.st_size != 0 )
    {
        output.Resize( read_info.st_size );

        ssize_t numRead = read( iReadFile, &output[0], read_info.st_size );

        if ( numRead == 0 )
        {
            close( iReadFile );
            return false;
        }

        if ( numRead != read_info.st_size )
        {
            output.Resize( numRead );
        }
    }
    else
    {
        output.Clear();
    }

    close( iReadFile );
    return true;
#else
#error Missing CFileSystem::ReadToBuffer implementation
#endif //OS DEPENDANT CODE
}
