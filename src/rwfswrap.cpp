#include "mainwindow.h"

// Helper function for accessing the global filesystem.
template <typename charType>
inline CFile* RawOpenGlobalFile( CFileSystem *fileSys, const charType *path, const charType *mode )
{
    CFile *theFile = NULL;

    CFileTranslator *accessPoint = fileSys->CreateSystemMinimumAccessPoint( path );

    if ( accessPoint )
    {
        try
        {
            theFile = accessPoint->Open( path, mode );
        }
        catch( ... )
        {
            delete accessPoint;

            throw;
        }

        delete accessPoint;
    }

    return theFile;
}

CFile* RawOpenGlobalFile( CFileSystem *fileSys, const filePath& path, const filePath& mode )
{
    return filePath_dispatchTrailing(
        path, mode,
        [&]( auto path, auto mode ) -> CFile*
    {
        return RawOpenGlobalFile( fileSys, path, mode );
    });
}

struct rwFileSystemStreamWrapEnv
{
    struct eirFileSystemMetaInfo
    {
        inline eirFileSystemMetaInfo( void )
        {
            this->theStream = NULL;
        }

        inline ~eirFileSystemMetaInfo( void )
        {
            return;
        }

        CFile *theStream;
    };

    struct eirFileSystemWrapperProvider : public rw::customStreamInterface, public rw::FileInterface
    {
        // *** rw::customStreamInterface IMPL
        void OnConstruct( rw::eStreamMode streamMode, void *userdata, void *membuf, size_t memSize ) const override
        {
            eirFileSystemMetaInfo *meta = new (membuf) eirFileSystemMetaInfo;

            meta->theStream = (CFile*)userdata;
        }

        void OnDestruct( void *memBuf, size_t memSize ) const override
        {
            eirFileSystemMetaInfo *meta = (eirFileSystemMetaInfo*)memBuf;

            meta->~eirFileSystemMetaInfo();
        }

        size_t Read( void *memBuf, void *out_buf, size_t readCount ) const override
        {
            eirFileSystemMetaInfo *meta = (eirFileSystemMetaInfo*)memBuf;

            return meta->theStream->Read( out_buf, readCount );
        }

        size_t Write( void *memBuf, const void *in_buf, size_t writeCount ) const override
        {
            eirFileSystemMetaInfo *meta = (eirFileSystemMetaInfo*)memBuf;

            return meta->theStream->Write( in_buf, writeCount );
        }

        void Skip( void *memBuf, rw::int64 skipCount ) const override
        {
            eirFileSystemMetaInfo *meta = (eirFileSystemMetaInfo*)memBuf;

            meta->theStream->SeekNative( skipCount, SEEK_CUR );
        }

        rw::int64 Tell( const void *memBuf ) const override
        {
            eirFileSystemMetaInfo *meta = (eirFileSystemMetaInfo*)memBuf;

            return meta->theStream->TellNative();
        }

        void Seek( void *memBuf, rw::int64 stream_offset, rw::eSeekMode seek_mode ) const override
        {
            int ansi_seek = SEEK_SET;

            if ( seek_mode == rw::RWSEEK_BEG )
            {
                ansi_seek = SEEK_SET;
            }
            else if ( seek_mode == rw::RWSEEK_CUR )
            {
                ansi_seek = SEEK_CUR;
            }
            else if ( seek_mode == rw::RWSEEK_END )
            {
                ansi_seek = SEEK_END;
            }
            else
            {
                assert( 0 );
            }

            eirFileSystemMetaInfo *meta = (eirFileSystemMetaInfo*)memBuf;

            meta->theStream->SeekNative( stream_offset, ansi_seek );
        }

        rw::int64 Size( const void *memBuf ) const override
        {
            eirFileSystemMetaInfo *meta = (eirFileSystemMetaInfo*)memBuf;

            return meta->theStream->GetSizeNative();
        }

        bool SupportsSize( const void *memBuf ) const override
        {
            return true;
        }

        // *** rw::FileInterface IMPL.
        filePtr_t OpenStream( const char *streamPath, const char *mode ) override
        {
            return (filePtr_t)RawOpenGlobalFile( this->nativeFileSystem, streamPath, mode );
        }

        void CloseStream( filePtr_t ptr ) override
        {
            CFile *outFile = (CFile*)ptr;

            delete outFile;
        }

        filePtr_t OpenStreamW( const wchar_t *streamPath, const wchar_t *mode ) override
        {
            return (filePtr_t)RawOpenGlobalFile( this->nativeFileSystem, streamPath, mode );
        }

        size_t ReadStream( filePtr_t ptr, void *outBuf, size_t readCount ) override
        {
            CFile *theFile = (CFile*)ptr;

            return theFile->Read( outBuf, readCount );
        }

        size_t WriteStream( filePtr_t ptr, const void *outBuf, size_t writeCount ) override
        {
            CFile *theFile = (CFile*)ptr;

            return theFile->Write( outBuf, writeCount );
        }

        bool SeekStream( filePtr_t ptr, long streamOffset, int type ) override
        {
            CFile *theFile = (CFile*)ptr;

            int iSeekMode;

            if ( type == rw::RWSEEK_BEG )
            {
                iSeekMode = SEEK_SET;
            }
            else if ( type == rw::RWSEEK_CUR )
            {
                iSeekMode = SEEK_CUR;
            }
            else if ( type == rw::RWSEEK_END )
            {
                iSeekMode = SEEK_END;
            }
            else
            {
                return false;
            }

            int seekSuccess = theFile->Seek( streamOffset, iSeekMode );

            return ( seekSuccess == 0 );
        }

        long TellStream( filePtr_t ptr ) override
        {
            CFile *theFile = (CFile*)ptr;

            return theFile->Tell();
        }

        bool IsEOFStream( filePtr_t ptr ) override
        {
            CFile *theFile = (CFile*)ptr;

            return theFile->IsEOF();
        }

        long SizeStream( filePtr_t ptr ) override
        {
            CFile *theFile = (CFile*)ptr;

            return theFile->GetSize();
        }

        void FlushStream( filePtr_t ptr ) override
        {
            CFile *theFile = (CFile*)ptr;

            return theFile->Flush();
        }

        CFileSystem *nativeFileSystem;
    };

    eirFileSystemWrapperProvider eirfs_file_wrap;

    inline void Initialize( MainWindow *mainwnd )
    {
        CFileSystem *fileSys = mainwnd->fileSystem;

        rw::Interface *rwEngine = mainwnd->GetEngine();

        // Register the native file system wrapper type.
        eirfs_file_wrap.nativeFileSystem = fileSys;

        rwEngine->RegisterStream( "eirfs_file", sizeof( eirFileSystemMetaInfo ), &eirfs_file_wrap );

        // For backwards compatibility with Windows XP we want to skip the CRT for all kinds of file operations
        // because the Visual Studio Windows XP compatibility layer is broken.
        rwEngine->SetFileInterface( &eirfs_file_wrap );
    }

    inline void Shutdown( MainWindow *mainwnd )
    {
        rw::Interface *rwEngine = mainwnd->GetEngine();

        rwEngine->SetFileInterface( NULL );

        // Streams are unregistered automatically when the engine is destroyed.
        // TODO: could be dangerous. unregistering is way cleaner.
    }
};

rw::Stream* RwStreamCreateTranslated( rw::Interface *rwEngine, CFile *eirStream )
{
    rw::streamConstructionCustomParam_t customParam( "eirfs_file", eirStream );

    rw::Stream *result = rwEngine->CreateStream( rw::RWSTREAMTYPE_CUSTOM, rw::RWSTREAMMODE_READWRITE, &customParam );

    return result;
}

void InitializeRWFileSystemWrap( void )
{
    mainWindowFactory.RegisterDependantStructPlugin <rwFileSystemStreamWrapEnv> ();
}