/*****************************************************************************
*
*  PROJECT:     Eir FileSystem
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        FileSystem/src/fsinternal/CFileSystem.internal.nativeimpl.hxx
*  PURPOSE:     Native implementation utilities to share across files
*
*  Get Eir FileSystem from https://osdn.net/projects/eirfs/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _FILESYSTEM_NATIVE_SHARED_IMPLEMENTATION_PRIVATE_
#define _FILESYSTEM_NATIVE_SHARED_IMPLEMENTATION_PRIVATE_

// Sub-modules.
#include "CFileSystem.platform.h"

#ifdef _WIN32

bool _FileWin32_IsDirectoryAbsolute( const char *pPath );
bool _FileWin32_IsDirectoryAbsolute( const wchar_t *pPath );
bool _FileWin32_DeleteDirectory( const char *path );
bool _FileWin32_DeleteDirectory( const wchar_t *path );
bool _FileWin32_DeleteFile( const char *path );
bool _FileWin32_DeleteFile( const wchar_t *path );
bool _FileWin32_CopyFile( const char *src, const char *dst );
bool _FileWin32_CopyFile( const wchar_t *src, const wchar_t *dst );
bool _FileWin32_RenameFile( const char *src, const char *dst );
bool _FileWin32_RenameFile( const wchar_t *src, const wchar_t *dst );
HANDLE _FileWin32_OpenDirectoryHandle( const filePath& absPath, eDirOpenFlags flags = DIR_FLAG_NONE );
bool _FileWin32_GetFileInformation( HANDLE fileHandle, filesysStats& statsOut );
bool _FileWin32_GetFileInformationByPath( const filePath& path, filesysStats& statsOut );
fsOffsetNumber_t _FileWin32_GetFileSize( const char *path );
fsOffsetNumber_t _FileWin32_GetFileSize( const wchar_t *path );

AINLINE filesysAttributes _FileWin32_GetAttributes( DWORD win32Attribs )
{
    filesysAttributes attribOut;

    attribOut.isSystem = ( win32Attribs & FILE_ATTRIBUTE_SYSTEM ) != 0;
    attribOut.isHidden = ( win32Attribs & FILE_ATTRIBUTE_HIDDEN ) != 0;
    attribOut.isTemporary = ( win32Attribs & FILE_ATTRIBUTE_TEMPORARY ) != 0;
    attribOut.isJunctionOrLink = ( win32Attribs & ( FILE_ATTRIBUTE_REPARSE_POINT ) ) != 0;

    bool isDirectory = ( ( win32Attribs & FILE_ATTRIBUTE_DIRECTORY ) != 0 );

    eFilesysItemType itemType;

    if ( isDirectory )
    {
        itemType = eFilesysItemType::DIRECTORY;
    }
    else
    {
        itemType = eFilesysItemType::FILE;
    }
    attribOut.type = itemType;

    return attribOut;
}

// Filesystem item iterator, for cross-platform support.
struct win32_fsitem_iterator
{
    struct info_data
    {
        decltype(WIN32_FIND_DATAW::cFileName) filename;
        bool isDirectory;
        filesysAttributes attribs;
    };

    filePath query;
    HANDLE findHandle;
    bool hasEnded;

    AINLINE win32_fsitem_iterator( const filePath& absDirPath )
    {
        // Create the query string to send to Windows.
        filePath query = absDirPath;
        query += FileSystem::GetAnyWildcardSelector <wchar_t> ();
        query.transform_to <wchar_t> ();

        // Initialize things.
        this->findHandle = INVALID_HANDLE_VALUE;
        this->hasEnded = false;

        // Remember the query string.
        this->query = std::move( query );
    }

private:
    AINLINE void Close( void )
    {
        HANDLE findHandle = this->findHandle;

        if ( findHandle != INVALID_HANDLE_VALUE )
        {
            FindClose( findHandle );

            this->findHandle = INVALID_HANDLE_VALUE;
        }

        this->hasEnded = true;
    }

public:
    AINLINE ~win32_fsitem_iterator( void )
    {
        this->Close();
    }

    AINLINE bool Next( info_data& dataOut )
    {
        if ( this->hasEnded )
            return false;

        HANDLE curFindHandle = this->findHandle;

        WIN32_FIND_DATAW findData;

        if ( curFindHandle == INVALID_HANDLE_VALUE )
        {
            curFindHandle = FindFirstFileW( this->query.w_str(), &findData );

            if ( curFindHandle == INVALID_HANDLE_VALUE )
            {
                return false;
            }

            this->findHandle = curFindHandle;
        }
        else
        {
            BOOL hasNext = FindNextFileW( curFindHandle, &findData );

            if ( hasNext == FALSE )
            {
                goto hasEnded;
            }
        }

        // Create an information structure.
        {
            info_data data;

            // Store all attributes.
            DWORD win32Attribs = findData.dwFileAttributes;
            data.attribs = _FileWin32_GetAttributes( win32Attribs );

            // Output the new info.
            memcpy( data.filename, findData.cFileName, sizeof( data.filename ) );
            data.isDirectory = ( data.attribs.type == eFilesysItemType::DIRECTORY );
            dataOut = std::move( data );
            return true;
        }

    hasEnded:
        this->Close();

        return false;
    }

    AINLINE void Rewind( void )
    {
        this->Close();

        this->hasEnded = false;
    }
};

#elif defined(__linux__)

#include <dirent.h>

bool _FileLinux_IsDirectoryAbsolute( const char *path );
bool _FileLinux_DeleteDirectory( const char *path );
bool _FileLinux_DeleteFile( const char *path );
bool _FileLinux_CopyFile( const char *src, const char *dst );
bool _FileLinux_RenameFile( const char *src, const char *dst );
int _FileLinux_StatFile( const char *src, filesysStats& statsOut );
fsOffsetNumber_t _FileLinux_GetFileSize( const char *src );

struct linux_fsitem_iterator
{
    struct info_data
    {
        decltype(dirent::d_name) filename;
        bool isDirectory;
        filesysAttributes attribs;
    };

    filePath absDirPath;
    DIR *iter;

    AINLINE linux_fsitem_iterator( filePath absDirPath ) : absDirPath( std::move( absDirPath ) )
    {
        auto ansiPath = this->absDirPath.convert_ansi <FSObjectHeapAllocator> ();

        this->iter = opendir( ansiPath.GetConstString() );
    }
    AINLINE linux_fsitem_iterator( const linux_fsitem_iterator& ) = delete;

    AINLINE ~linux_fsitem_iterator( void )
    {
        if ( DIR *iter = this->iter )
        {
            closedir( iter );
        }
    }

    AINLINE linux_fsitem_iterator& operator = ( const linux_fsitem_iterator& ) = delete;

    AINLINE bool Next( info_data& dataOut )
    {
        DIR *iter = this->iter;

        if ( iter == nullptr )
        {
            return false;
        }

    tryNextItem:
        struct dirent *entry = readdir( iter );

        if ( !entry )
            return false;

        filePath path = this->absDirPath;
        path += entry->d_name;
        path.transform_to <char> ();

        struct stat entry_stats;

        if ( stat( path.c_str(), &entry_stats ) == 0 )
        {
            info_data data;

            FSDataUtil::copy_impl( entry->d_name, entry->d_name + sizeof( data.filename ), data.filename );

            auto st_mode = entry_stats.st_mode;

            bool isDirectory = S_ISDIR( entry_stats.st_mode );

            // Fill out attributes.
            eFilesysItemType itemType;

            if ( isDirectory )
            {
                itemType = eFilesysItemType::DIRECTORY;
            }
            else if ( S_ISREG( st_mode ) )
            {
                itemType = eFilesysItemType::FILE;
            }
            else
            {
                itemType = eFilesysItemType::UNKNOWN;
            }
            data.attribs.type = itemType;
            // TODO: actually fill those our truthfully.
            data.attribs.isSystem = false;
            data.attribs.isHidden = false;
            data.attribs.isTemporary = false;
            data.attribs.isJunctionOrLink = false;

            data.isDirectory = isDirectory;

            dataOut = std::move( data );
            return true;
        }

        // Failed to do something, try next instead.
        goto tryNextItem;
    }

    AINLINE void Rewind( void )
    {
        if ( DIR *iter = this->iter )
        {
            rewinddir( iter );
        }
    }
};

#endif

#endif //_FILESYSTEM_NATIVE_SHARED_IMPLEMENTATION_PRIVATE_
