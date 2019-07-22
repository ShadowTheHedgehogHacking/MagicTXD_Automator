/*****************************************************************************
*
*  PROJECT:     Eir FileSystem
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        FileSystem/src/fsinternal/CFileSystem.translator.system.win32.cpp
*  PURPOSE:     Windows implementation of the local system translator
*
*  Get Eir FileSystem from https://osdn.net/projects/eirfs/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#include "StdInc.h"

// Sub-modules.
#include "CFileSystem.platform.h"

#include "CFileSystem.internal.nativeimpl.hxx"

#ifdef _WIN32

bool _FileWin32_IsDirectoryAbsolute( const char *pPath )
{
    DWORD dwAttributes = GetFileAttributesA(pPath);

    if (dwAttributes == INVALID_FILE_ATTRIBUTES)
        return false;

    return (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool _FileWin32_IsDirectoryAbsolute( const wchar_t *pPath )
{
    DWORD dwAttributes = GetFileAttributesW(pPath);

    if (dwAttributes == INVALID_FILE_ATTRIBUTES)
        return false;

    return (dwAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool _FileWin32_DeleteDirectory( const char *path )
{
    return RemoveDirectoryA( path ) != FALSE;
}

bool _FileWin32_DeleteDirectory( const wchar_t *path )
{
    return RemoveDirectoryW( path ) != FALSE;
}

bool _FileWin32_DeleteFile( const char *path )
{
    return DeleteFileA( path ) != FALSE;
}

bool _FileWin32_DeleteFile( const wchar_t *path )
{
    return DeleteFileW( path ) != FALSE;
}

bool _FileWin32_CopyFile( const char *src, const char *dst )
{
    return CopyFileA( src, dst, FALSE ) != FALSE;
}

bool _FileWin32_CopyFile( const wchar_t *src, const wchar_t *dst )
{
    return CopyFileW( src, dst, FALSE ) != FALSE;
}

bool _FileWin32_RenameFile( const char *src, const char *dst )
{
    return MoveFileA( src, dst ) != FALSE;
}

bool _FileWin32_RenameFile( const wchar_t *src, const wchar_t *dst )
{
    return MoveFileW( src, dst ) != FALSE;
}

HANDLE _FileWin32_OpenDirectoryHandle( const filePath& absPath, eDirOpenFlags flags )
{
    HANDLE dir = INVALID_HANDLE_VALUE;

    // Determine the share mode.
    DWORD dwShareMode = 0;

    if ( ( flags & DIR_FLAG_EXCLUSIVE ) == 0 )
    {
        dwShareMode |= ( FILE_SHARE_READ | FILE_SHARE_WRITE );
    }

    DWORD accessMode = 0;

    if ( ( flags & DIR_FLAG_NO_READ ) == 0 )
    {
        accessMode |= GENERIC_READ;
    }

    if ( ( flags & DIR_FLAG_WRITABLE ) != 0 )
    {
        accessMode |= GENERIC_WRITE;
    }

    if ( const char *sysPath = absPath.c_str() )
    {
        dir = CreateFileA( sysPath, accessMode, dwShareMode, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr );
    }
    else if ( const wchar_t *sysPath = absPath.w_str() )
    {
        dir = CreateFileW( sysPath, accessMode, dwShareMode, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr );
    }
    else
    {
        // For unknown char types.
        auto widePath = absPath.convert_unicode <FSObjectHeapAllocator> ();

        dir = CreateFileW( widePath.GetConstString(), accessMode, dwShareMode, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr );
    }

    return dir;
}

// Getting file information on the Windows platform reliably.
bool _FileWin32_GetFileInformation( HANDLE fileHandle, filesysStats& statsOut )
{
    BY_HANDLE_FILE_INFORMATION info;

    if ( GetFileInformationByHandle( fileHandle, &info ) == FALSE )
        return false;

    statsOut.atime = info.ftLastAccessTime.dwLowDateTime;
    statsOut.ctime = info.ftCreationTime.dwLowDateTime;
    statsOut.mtime = info.ftLastWriteTime.dwLowDateTime;
    statsOut.attribs = _FileWin32_GetAttributes( info.dwFileAttributes );
    return true;
}

// Helper for file paths.
bool _FileWin32_GetFileInformationByPath( const filePath& path, filesysStats& statsOut )
{
    HANDLE fileHandle = _FileWin32_OpenDirectoryHandle( path, DIR_FLAG_NO_READ );

    bool success = false;

    if ( fileHandle != INVALID_HANDLE_VALUE )
    {
        success = _FileWin32_GetFileInformation( fileHandle, statsOut );

        CloseHandle( fileHandle );
    }

    return success;
}

template <typename charType>
AINLINE HANDLE _FileWin32_OpenInformationHandle( const charType *path )
{
    if constexpr ( std::is_same <charType, char>::value )
    {
        return CreateFileA( path, GENERIC_READ, 0, nullptr, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, 0 );
    }
    else if constexpr ( std::is_same <charType, wchar_t>::value )
    {
        return CreateFileW( path, GENERIC_READ, 0, nullptr, FILE_SHARE_READ | FILE_SHARE_WRITE, OPEN_EXISTING, 0 );
    }
    else
    {
        return INVALID_HANDLE_VALUE;
    }
}

template <typename charType>
fsOffsetNumber_t _FileWin32_Gen_GetFileSize( const charType *path )
{
    HANDLE fileHandle = _FileWin32_OpenInformationHandle( path );

    if ( fileHandle == INVALID_HANDLE_VALUE )
    {
        return 0;
    }

    union
    {
        LARGE_INTEGER win32_filesize;
        std::int64_t native_filesize;
    };

    BOOL gotSize = GetFileSizeEx( fileHandle, &win32_filesize );

    CloseHandle( fileHandle );

    if ( gotSize == FALSE )
    {
        return 0;
    }

    return native_filesize;
}

fsOffsetNumber_t _FileWin32_GetFileSize( const char *path )
{
    return _FileWin32_Gen_GetFileSize( path );
}

fsOffsetNumber_t _FileWin32_GetFileSize( const wchar_t *path )
{
    return _FileWin32_Gen_GetFileSize( path );
}

#endif //_WIN32