// FileSystem wrapper for all of Qt.
// Uses private implementation headers of the Qt framework to handle all file
// requests that originate from Qt itself (resource requests, etc).

#include "mainwindow.h"

#include <QtCore/private/qabstractfileengine_p.h>

#include <qdatetime.h>

#include <sdk/GlobPattern.h>

struct FileSystemQtFileEngineIterator final : public QAbstractFileEngineIterator
{
    AINLINE FileSystemQtFileEngineIterator( QDir::Filters filters, const QStringList& nameFilters, QStringList list )
        : QAbstractFileEngineIterator( std::move( filters ), nameFilters ), filenames( std::move( list ) )
    {
        return;
    }

    QString next( void ) override
    {
        if ( !hasNext() )
        {
            return QString();
        }

        this->entryIndex++;

        return currentFilePath();
    }

    bool hasNext( void ) const override
    {
        return ( this->entryIndex + 1 < this->filenames.size() );
    }

    QString currentFileName( void ) const override
    {
        if ( this->entryIndex >= this->filenames.size() )
        {
            return QString();
        }

        return this->filenames[ this->entryIndex ];
    }

private:
    int entryIndex = 0;
    QStringList filenames;
};

// Translators that are visited in-order for files.
static eir::Vector <CFileTranslator*, FileSysCommonAllocator> translators;

struct FileSystemQtFileEngine final : public QAbstractFileEngine
{
    // Since there is no reason to access files outside of our executable,
    // we simply can look up all important translators from an internal
    // registry. There may be a case when fonts and system stuff must be
    // accessed through strange paths, so be prepared to add-back a
    // translator member.

    inline FileSystemQtFileEngine( const QString& fileName ) noexcept
    {
        this->location = qt_to_filePath( fileName );
        this->dataFile = nullptr;
    }

    inline void _closeDataFile( void ) noexcept
    {
        if ( CFile *dataFile = this->dataFile )
        {
            delete dataFile;

            this->dataFile = nullptr;
        }
    }

    ~FileSystemQtFileEngine( void )
    {
        this->_closeDataFile();
    }

    static inline filePath get_app_path( const filePath& input )
    {
        filePath relpath_appRoot;

        if ( sysAppRoot != nullptr && sysAppRoot->GetRelativePathFromRoot( input, true, relpath_appRoot ) )
        {
            // Prepend the global translator root descriptor.
            return "//" + relpath_appRoot;
        }

        return input;
    }

    inline CFile* open_first_translator_file( const filePath& thePath, const filesysOpenMode& mode ) noexcept
    {
        filePath appPath = get_app_path( thePath );

        size_t transCount = translators.GetCount();

        for ( size_t n = 0; n < transCount; n++ )
        {
            CFileTranslator *trans = translators[ n ];

            CFile *openFile = trans->Open( appPath, mode );

            if ( openFile != nullptr )
            {
                return openFile;
            }
        }

        return nullptr;
    }

    bool open( QIODevice::OpenMode openMode ) noexcept override
    {
        // We do not support certain things.
        if ( openMode & QIODevice::OpenModeFlag::Append )
        {
            return false;
        }
        if ( openMode & QIODevice::OpenModeFlag::NewOnly )
        {
            return false;
        }
        // ignore text.
        // ignore unbuffered.

        // Create the file opening mode.
        filesysOpenMode mode;
        mode.access.allowRead = ( openMode & QIODevice::OpenModeFlag::ReadOnly );
        mode.access.allowWrite = ( openMode & QIODevice::OpenModeFlag::WriteOnly );
        mode.seekAtEnd = ( openMode & QIODevice::OpenModeFlag::Append );

        eFileOpenDisposition openType;

        if ( openMode & QIODevice::OpenModeFlag::NewOnly )
        {
            openType = eFileOpenDisposition::CREATE_NO_OVERWRITE;
        }
        else if ( openMode & QIODevice::OpenModeFlag::ExistingOnly )
        {
            // TODO: maybe add truncate-if-existing logic.

            openType = eFileOpenDisposition::OPEN_EXISTS;
        }
        else
        {
            if ( openMode & QIODevice::OpenModeFlag::Truncate )
            {
                openType = eFileOpenDisposition::CREATE_OVERWRITE;
            }
            else
            {
                openType = eFileOpenDisposition::OPEN_OR_CREATE;
            }
        }

        mode.openDisposition = openType;

        CFile *openedFile = open_first_translator_file( this->location, mode );

        if ( openedFile )
        {
            this->_closeDataFile();

            this->dataFile = openedFile;

            return true;
        }

        return false;
    }

    bool close( void ) noexcept override
    {
        this->_closeDataFile();

        return true;
    }

    bool flush( void ) noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            dataFile->Flush();
            return true;
        }

        return false;
    }

    bool syncToDisk( void ) noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            dataFile->Flush();
            return true;
        }

        return false;
    }

    qint64 size( void ) const noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            return dataFile->GetSizeNative();
        }

        filePath appPath = get_app_path( this->location );

        return (qint64)fileRoot->Size( appPath );
    }

    qint64 pos( void ) const noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            return dataFile->TellNative();
        }

        return 0;
    }

    bool seek( qint64 pos ) noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            return ( dataFile->SeekNative( pos, SEEK_SET ) == 0 );
        }

        return false;
    }

    bool isSequential( void ) const noexcept override
    {
        // Assumingly we will only have non-sequential streams here?
        return false;
    }

    bool remove( void ) noexcept override
    {
        bool hasRemoved = false;

        size_t numTranslators = translators.GetCount();

        filePath appPath = get_app_path( this->location );

        for ( size_t n = 0; n < numTranslators; n++ )
        {
            CFileTranslator *trans = translators[ n ];

            bool couldRemove = trans->Delete( appPath );

            if ( couldRemove )
            {
                hasRemoved = true;
            }
        }

        return hasRemoved;
    }

    bool copy( const QString& newName ) noexcept override
    {
        filesysOpenMode readOpen;
        FileSystem::ParseOpenMode( "rb", readOpen );

        FileSystem::filePtr srcFile = open_first_translator_file( this->location, readOpen );

        if ( !srcFile.is_good() )
            return false;

        filesysOpenMode writeOpen;
        FileSystem::ParseOpenMode( "wb", writeOpen );

        FileSystem::filePtr dstFile = open_first_translator_file( qt_to_filePath( newName ), writeOpen );

        if ( !dstFile.is_good() )
            return false;

        FileSystem::StreamCopy( *srcFile, *dstFile );
        return true;
    }

    bool rename( const QString& newName ) noexcept override
    {
        size_t transCount = translators.GetCount();

        filePath srcAppPath = get_app_path( this->location );
        filePath dstAppPath = get_app_path( qt_to_filePath( newName ) );

        for ( size_t n = 0; n < transCount; n++ )
        {
            CFileTranslator *trans = translators[ n ];

            if ( trans->Rename( srcAppPath, dstAppPath ) )
            {
                return true;
            }
        }

        return false;
    }

    bool renameOverwrite( const QString& newName ) noexcept override
    {
        // Maybe implement this.
        return false;
    }

    bool link( const QString& newName ) noexcept override
    {
        // Maybe implement this.
        return false;
    }

    static AINLINE filePath make_dir_path( filePath path )
    {
        if ( FileSystem::IsPathDirectory( path ))
        {
            return path;
        }

        return path + "/";
    }

    bool mkdir( const QString& dirName, bool createParentDirectories ) const noexcept override
    {
        if ( translators.GetCount() == 0 )
            return false;

        // We always create parent directories; we could implement a switch to disable it.

        CFileTranslator *first = translators[ 0 ];

        filePath appPath = get_app_path( make_dir_path( qt_to_filePath( dirName ) ) );

        return first->CreateDir( appPath );
    }

    bool rmdir( const QString& dirName, bool recurseParentDirectories ) const noexcept override
    {
        // Ignore deleting empty parent directories for now; as said above we do things on demand.
        // Also we can delete files using this function; maybe implement a switch in FileSystem?

        bool hasDeleted = false;

        size_t transCount = translators.GetCount();

        filePath fsDirName = get_app_path( make_dir_path( qt_to_filePath( dirName ) ) );

        for ( size_t n = 0; n < transCount; n++ )
        {
            CFileTranslator *trans = translators[ n ];

            if ( trans->Delete( fsDirName ) )
            {
                hasDeleted = true;
            }
        }

        return hasDeleted;
    }

    bool setSize( qint64 size ) noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            dataFile->SeekNative( size, SEEK_SET );
            dataFile->SetSeekEnd();
            return true;
        }

        return false;
    }

    bool caseSensitive( void ) const noexcept override
    {
        // Just return what the platform mandates.
        return fileRoot->IsCaseSensitive();
    }

    bool isRelativePath( void ) const noexcept override
    {
        filePath desc;

        bool isRootPath = fileSystem->GetSystemRootDescriptor( this->location, desc );

        if ( isRootPath )
        {
            return false;
        }

        // TODO: What about translator root paths?#

        return true;
    }

    struct _filePath_comparator
    {
        static inline bool is_less_than( const filePath& left, const filePath& right )
        {
            return left.compare( right, true ) == eir::eCompResult::LEFT_LESS;
        }
    };

    QStringList entryList( QDir::Filters filters, const QStringList& filterNames ) const noexcept override
    {
        // First make all GLOB patterns for filename matching.
        eir::PathPatternEnv <wchar_t, FileSysCommonAllocator> patternEnv(
            filters & QDir::Filter::CaseSensitive
        );

        eir::Vector <decltype(patternEnv)::filePattern_t, FileSysCommonAllocator> patterns;

        for ( const QString& patDesc : filterNames )
        {
            rw::rwStaticString <wchar_t> widePatDesc = qt_to_widerw( patDesc );

            auto pattern = patternEnv.CreatePattern( widePatDesc.GetConstString() );

            patterns.AddToBack( std::move( pattern ) );
        }

        size_t numPatterns = patterns.GetCount();

        // Prepare filtering options for the fs scan.
        scanFilteringFlags flags;
        flags.noDirectory = ( filters & QDir::Filter::Dirs ) == false;
        flags.noFile = ( filters & QDir::Filter::Files ) == false;
        // ignore QDir::Filter::Drives
        flags.noJunctionOrLink = ( filters & QDir::Filter::NoSymLinks );
        // ignore QDir::Filter::Readable
        // ignore QDir::Filter::Writable
        // ignore QDir::Filter::Executable
        // ignore QDir::Filter::Modified
        flags.noHidden = ( filters & QDir::Filter::Hidden ) == false;
        flags.noSystem = ( filters & QDir::Filter::System ) == false;
        flags.noPatternOnDirs = ( filters & QDir::Filter::AllDirs );
        flags.noCurrentDirDesc = ( filters & QDir::Filter::NoDot );
        // QDir::Filter::CaseSensitive is passed to the patternEnv.
        flags.noParentDirDesc = ( filters & QDir::Filter::NoDotDot );

        // Create a set of all available files on the translators and then turn it into a string list.

        eir::Set <filePath, FileSysCommonAllocator, _filePath_comparator> fileNameSet;

        size_t transCount = translators.GetCount();

        filePath dirPath = get_app_path( make_dir_path( this->location ) );

        for ( size_t n = 0; n < transCount; n++ )
        {
            CFileTranslator *trans = translators[ n ];

            FileSystem::dirIterator dirIter = trans->BeginDirectoryListing( dirPath, "*", flags );

            if ( !dirIter.is_good() )
                continue;

            CDirectoryIterator::item_info info;

            while ( dirIter->Next( info ) )
            {
                // Does it match the patterns?
                bool hasPatternConflict = false;

                if ( info.attribs.type == eFilesysItemType::DIRECTORY && flags.noPatternOnDirs )
                {
                    hasPatternConflict = false;
                }
                else
                {
                    for ( size_t n = 0; n < numPatterns; n++ )
                    {
                        const decltype(patternEnv)::filePattern_t& pattern = patterns[ n ];

                        if ( patternEnv.MatchPattern( info.filename, pattern ) == false )
                        {
                            hasPatternConflict = true;
                            break;
                        }
                    }
                }

                if ( hasPatternConflict )
                {
                    continue;
                }

                fileNameSet.Insert( std::move( info.filename ) );
            }
        }

        // Convert our result names into a QStringList.
        QStringList result;

        for ( const filePath& name : fileNameSet )
        {
            QString qtName = filePath_to_qt( name );

            result.append( std::move( qtName ) );
        }

        return result;
    }

    FileFlags fileFlags( FileFlags type ) const noexcept override
    {
        filesysStats objStats;

        bool gotStats = false;

        size_t numTrans = translators.GetCount();

        filePath appPath = get_app_path( this->location );

        for ( size_t n = 0; n < numTrans; n++ )
        {
            CFileTranslator *trans = translators[ n ];

            if ( trans->QueryStats( appPath, objStats ) )
            {
                gotStats = true;
                break;
            }

            if ( trans->QueryStats( make_dir_path( appPath ), objStats ) )
            {
                gotStats = true;
                break;
            }
        }

        FileFlags flagsOut = 0;

        if ( gotStats )
        {
            flagsOut |= FileFlag::ExistsFlag;

            eFilesysItemType itemType = objStats.attribs.type;

            if ( itemType == eFilesysItemType::FILE )
            {
                flagsOut |= FileFlag::FileType;
            }
            else if ( itemType == eFilesysItemType::DIRECTORY )
            {
                flagsOut |= FileFlag::DirectoryType;
            }

            if ( objStats.attribs.isHidden )
            {
                flagsOut |= FileFlag::HiddenFlag;
            }
            if ( objStats.attribs.isJunctionOrLink )
            {
                flagsOut |= FileFlag::LinkType;
            }
        }

        return flagsOut;
    }

    bool setPermissions( uint perms ) noexcept override
    {
        // Not supported because it is an unix-only permission model.
        return false;
    }

    QByteArray id( void ) const noexcept override
    {
        // ???
        return QByteArray();
    }

    QString fileName( FileName file = DefaultName ) const noexcept override
    {
        QString result;

        if ( file == FileName::DefaultName )
        {
            result = filePath_to_qt( this->location );
        }
        else if ( file == FileName::BaseName )
        {
            filePath baseName = FileSystem::GetFileNameItem( this->location, true );

            result = filePath_to_qt( baseName );
        }
        else if ( file == FileName::PathName )
        {
            filePath dirName;

            FileSystem::GetFileNameItem( this->location, false, &dirName );

            result = filePath_to_qt( dirName );
        }
        else if ( file == FileName::AbsoluteName )
        {
            filePath absPath;
            fileRoot->GetFullPath( this->location, true, absPath );

            result = filePath_to_qt( absPath );
        }
        else if ( file == FileName::AbsolutePathName )
        {
            filePath absPath;
            fileRoot->GetFullPath( this->location, false, absPath );

            result = filePath_to_qt( absPath );
        }
        else
        {
            result = filePath_to_qt( this->location );
        }

        return result;
    }

    uint ownerId( FileOwner owner ) const noexcept override
    {
        // Some linux-only bs.
        return 0;
    }

    QString owner( FileOwner owner ) const noexcept override
    {
        // Some linux-only bs.
        return "";
    }

    bool setFileTime( const QDateTime& newData, FileTime time ) noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            time_t timeval = newData.toTime_t();

            // TODO: maybe allow setting just one of the times?

            dataFile->SetFileTimes( timeval, timeval, timeval );
            return true;
        }

        return false;
    }

    QDateTime fileTime( FileTime time ) const noexcept override
    {
        // TODO: maybe allow returing a specific time instead of just the modification time?
        if ( CFile *dataFile = this->dataFile )
        {
            filesysStats fileStats;

            if ( dataFile->QueryStats( fileStats ) )
            {
                return QDateTime::fromTime_t( fileStats.ctime );
            }
        }

        filePath appPath = get_app_path( this->location );

        filesysStats fileStats;

        if ( fileRoot->QueryStats( appPath, fileStats ) )
        {
            return QDateTime::fromTime_t( fileStats.ctime );
        }

        return QDateTime();
    }

    void setFileName( const QString& file ) noexcept override
    {
        this->location = qt_to_filePath( file );
    }

    int handle( void ) const noexcept override
    {
        // Not supported.
        return -1;
    }

    bool cloneTo( QAbstractFileEngine *target ) noexcept override
    {
        if ( FileSystemQtFileEngine *targetEngine = dynamic_cast <FileSystemQtFileEngine*> ( target ) )
        {
            targetEngine->location = this->location;

            // TODO: what about the opened file?

            return true;
        }

        return false;
    }

    QAbstractFileEngineIterator* beginEntryList( QDir::Filters filters, const QStringList& filterNames ) noexcept override
    {
        return new FileSystemQtFileEngineIterator( filters, filterNames, this->entryList( filters, filterNames ) );
    }

    QAbstractFileEngineIterator* endEntryList( void ) noexcept override
    {
        // TODO: what is this?
        return nullptr;
    }

    qint64 read( char *data, qint64 maxlen ) noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            return (qint64)dataFile->Read( data, (size_t)maxlen );
        }

        return 0;
    }

    qint64 readLine( char *data, qint64 maxlen ) noexcept override
    {
        // Do we have to implement this?
        return 0;
    }

    qint64 write( const char *data, qint64 len ) noexcept override
    {
        if ( CFile *dataFile = this->dataFile )
        {
            return (qint64)dataFile->Write( data, (size_t)len );
        }

        return 0;
    }

    bool extension( Extension extension, const ExtensionOption *option, ExtensionReturn *output ) noexcept override
    {
        return false;
    }

    bool supportsExtension( Extension extension ) const noexcept override
    {
        return false;
    }

private:
    filePath location;
    CFile *dataFile;
};

void register_file_translator( CFileTranslator *source )
{
    if ( translators.Find( source ) == false )
    {
        translators.AddToBack( source );
    }
}

void unregister_file_translator( CFileTranslator *source )
{
    translators.RemoveByValue( source );
}

struct FileSystemQtFileEngineHandler : public QAbstractFileEngineHandler
{
    QAbstractFileEngine* create( const QString& fileName ) const override
    {
        return new FileSystemQtFileEngine( fileName );
    }
};

static optional_struct_space <FileSystemQtFileEngineHandler> filesys_qt_wrap;

void registerQtFileSystem( void )
{
    filesys_qt_wrap.Construct();
}

void unregisterQtFileSystem( void )
{
    filesys_qt_wrap.Destroy();
}
