/*****************************************************************************
*
*  PROJECT:     Eir FileSystem
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        FileSystem/include/CFileSystem.qol.h
*  PURPOSE:     Helpers for runtime constructs typical to C++
*
*  Get Eir FileSystem from https://osdn.net/projects/eirfs/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _FILESYSTEM_QUALITY_OF_LIFE_
#define _FILESYSTEM_QUALITY_OF_LIFE_

namespace FileSystem
{

// Smart pointer for a CFileSystem instance.
struct fileSysInstance
{
    inline fileSysInstance( void )
    {
        using namespace FileSystem;

        // Creates a CFileSystem without any special things.
        fs_construction_params params;
        params.nativeExecMan = nullptr;

        CFileSystem *fileSys = CFileSystem::Create( params );

        if ( !fileSys )
        {
            throw filesystem_exception( eGenExceptCode::RESOURCE_UNAVAILABLE );
        }

        this->fileSys = fileSys;
    }

    inline fileSysInstance( const fs_construction_params& params )
    {
        using namespace FileSystem;

        CFileSystem *fileSys = CFileSystem::Create( params );

        if ( !fileSys )
        {
            throw filesystem_exception( eGenExceptCode::RESOURCE_UNAVAILABLE );
        }

        this->fileSys = fileSys;
    }

    inline ~fileSysInstance( void )
    {
        CFileSystem::Destroy( this->fileSys );
    }

    // Direct availability.
    inline CFileSystem& inst( void )
    {
        return *fileSystem;
    }

    inline operator CFileSystem* ( void )
    {
        return &inst();
    }

    inline CFileSystem* operator -> ( void )
    {
        return &inst();
    }

private:
    CFileSystem *fileSys;
};

// File translator.
struct fileTrans
{
    template <typename charType>
    inline fileTrans( CFileSystem *fileSys, const charType *path, eDirOpenFlags dirFlags = DIR_FLAG_NONE )
    {
        this->theTrans = fileSys->CreateTranslator( path, dirFlags );
    }

    inline fileTrans( CFileSystem *fileSys, const filePath& path, eDirOpenFlags dirFlags = DIR_FLAG_NONE )
    {
        this->theTrans = fileSys->CreateTranslator( path, dirFlags );
    }

    inline fileTrans( CFileTranslator *fileTrans )
    {
        this->theTrans = fileTrans;
    }

    inline fileTrans( fileTrans&& right ) noexcept
    {
        this->theTrans = right.theTrans;

        right.theTrans = nullptr;
    }

    inline ~fileTrans( void )
    {
        if ( CFileTranslator *theTrans = this->theTrans )
        {
            delete theTrans;
        }
    }

    inline bool is_good( void ) const
    {
        return ( this->theTrans != nullptr );
    }

    inline CFileTranslator& inst( void )
    {
        CFileTranslator *theTrans = this->theTrans;

        if ( !theTrans )
        {
            throw filesystem_exception( eGenExceptCode::RESOURCE_UNAVAILABLE );
        }

        return *theTrans;
    }

    inline operator CFileTranslator* ( void )
    {
        return &inst();
    }

    inline CFileTranslator* operator -> ( void )
    {
        return &inst();
    }
    
private:
    CFileTranslator *theTrans;
};

// Archived file translator.
struct archiveTrans
{
    inline archiveTrans( CArchiveTranslator *fileTrans )
    {
        this->theTrans = fileTrans;
    }

    inline archiveTrans( archiveTrans&& right ) noexcept
    {
        this->theTrans = right.theTrans;

        right.theTrans = nullptr;
    }

    inline ~archiveTrans( void )
    {
        if ( CArchiveTranslator *theTrans = this->theTrans )
        {
            delete theTrans;
        }
    }

    inline bool is_good( void ) const
    {
        return ( this->theTrans != nullptr );
    }

    inline CArchiveTranslator& inst( void )
    {
        CArchiveTranslator *theTrans = this->theTrans;

        if ( !theTrans )
        {
            throw filesystem_exception( eGenExceptCode::RESOURCE_UNAVAILABLE );
        }

        return *theTrans;
    }

    inline operator CArchiveTranslator* ( void )
    {
        return &inst();
    }

    inline CArchiveTranslator* operator -> ( void )
    {
        return &inst();
    }
    
private:
    CArchiveTranslator *theTrans;
};

// File pointer.
struct filePtr
{
    // Since files could be unavailable very frequently we make it a habbit of the user to check for availability explicitly (theFile could be nullptr).

    template <typename charType>
    inline filePtr( CFileTranslator *fileTrans, const charType *path, const charType *mode, eFileOpenFlags fileFlags = FILE_FLAG_NONE )
    {
        this->theFile = fileTrans->Open( path, mode, fileFlags );
    }

    inline filePtr( CFileTranslator *fileTrans, const filePath& path, const filePath& mode, eFileOpenFlags fileFlags = FILE_FLAG_NONE )
    {
        this->theFile = fileTrans->Open( path, mode, fileFlags );
    }

    inline filePtr( CFile *theFile )
    {
        this->theFile = theFile;
    }

    inline filePtr( filePtr&& right ) noexcept
    {
        this->theFile = right.theFile;

        right.theFile = nullptr;
    }

    inline ~filePtr( void )
    {
        if ( CFile *theFile = this->theFile )
        {
            delete theFile;
        }
    }

    inline filePtr& operator = ( filePtr&& right ) noexcept
    {
        if ( CFile *oldFile = this->theFile )
        {
            delete oldFile;
        }

        this->theFile = right.theFile;

        right.theFile = nullptr;

        return *this;
    }

    inline bool is_good( void ) const
    {
        return ( this->theFile != nullptr );
    }

    inline CFile& inst( void )
    {
        CFile *theFile = this->theFile;

        if ( theFile == nullptr )
        {
            throw filesystem_exception( eGenExceptCode::RESOURCE_UNAVAILABLE );
        }

        return *theFile;
    }

    inline operator CFile* ( void )
    {
        return &inst();
    }

    inline CFile* operator -> ( void )
    {
        return &inst();
    }

private:
    CFile *theFile;
};

// Helper for the directory iterator that you can get at each translator.
struct dirIterator
{
    inline dirIterator( CDirectoryIterator *iter )
    {
        this->iterator = iter;
    }
    inline dirIterator( const dirIterator& ) = delete;
    inline dirIterator( dirIterator&& right ) noexcept
    {
        this->iterator = right.iterator;

        right.iterator = nullptr;
    }
    
    inline void clear( void )
    {
        if ( CDirectoryIterator *iter = this->iterator )
        {
            delete iter;

            this->iterator = nullptr;
        }
    }

    inline ~dirIterator( void )
    {
        this->clear();
    }

    inline bool is_good( void ) const
    {
        return ( this->iterator != nullptr );
    }

    inline dirIterator& operator = ( const dirIterator& ) = delete;
    inline dirIterator& operator = ( dirIterator&& right ) noexcept
    {
        this->clear();

        this->iterator = right.iterator;

        right.iterator = nullptr;

        return *this;
    }

    inline CDirectoryIterator& inst( void )
    {
        CDirectoryIterator *iter = this->iterator;

        if ( iter == nullptr )
        {
            throw filesystem_exception( eGenExceptCode::RESOURCE_UNAVAILABLE );
        }

        return *iter;
    }

    inline operator CDirectoryIterator* ( void )
    {
        return &inst();
    }

    inline CDirectoryIterator* operator -> ( void )
    {
        return &inst();
    }

private:
    CDirectoryIterator *iterator;
};

}

#endif //_FILESYSTEM_QUALITY_OF_LIFE_