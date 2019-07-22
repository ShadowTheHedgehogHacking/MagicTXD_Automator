/*****************************************************************************
*
*  PROJECT:     Eir FileSystem
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        FileSystem/src/fsinternal/CFileSystem.FileDataPresence.cpp
*  PURPOSE:     File data presence scheduling
*
*  Get Eir FileSystem from https://osdn.net/projects/eirfs/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

// TODO: port to Linux.
// TODO: some users complained that removable devices were still queried; fix this.

#include "StdInc.h"

// Sub modules.
#include "CFileSystem.platform.h"
#include "CFileSystem.internal.h"
#include "CFileSystem.stream.memory.h"
#include "CFileSystem.translator.system.h"
#include "CFileSystem.platformutils.hxx"

#include "CFileSystem.lock.hxx"

#include "CFileSystem.Utils.hxx"

#include <sdk/PluginHelpers.h>
#include <sdk/Set.h>

// We need OS features to request device capabilities.

enum class eDiskMediaType
{
    UNKNOWN,
    ROTATING_SPINDLE,
    SOLID_STATE
};

#ifdef _WIN32
#include <winioctl.h>

inline eir::String <wchar_t, FileSysCommonAllocator> getDriveRootDesc( wchar_t driveChar )
{
    wchar_t rootBuf[4];
    rootBuf[0] = driveChar;
    rootBuf[1] = L':';
    rootBuf[2] = L'/';
    rootBuf[3] = L'\0';

    return ( rootBuf );
}
#endif //_WIN32

eDiskMediaType GetDiskMediaType( const wchar_t *diskDescriptor )
{
    // Let's see what this disk is about.
    // There are characteristic features that drives have that define them.

#ifdef _WIN32
    HANDLE volumeHandle = CreateFileW( diskDescriptor, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );

    if ( volumeHandle == INVALID_HANDLE_VALUE )
        return eDiskMediaType::UNKNOWN;

    eDiskMediaType mediaType = eDiskMediaType::UNKNOWN;

    try
    {
        // We need Windows 7 and above for this check.
        if ( fileSystem->m_win32HasLegacyPaths == false )
        {
            // Detect warm-up time. We think that devices without warm-up time are solid state.
            if ( mediaType == eDiskMediaType::UNKNOWN )
            {
                STORAGE_PROPERTY_QUERY query;
                query.PropertyId = StorageDeviceSeekPenaltyProperty;
                query.QueryType = PropertyStandardQuery;

                DEVICE_SEEK_PENALTY_DESCRIPTOR seekPenalty;

                DWORD query_bytesReturned;

                BOOL gotParam = DeviceIoControl(
                    volumeHandle, IOCTL_STORAGE_QUERY_PROPERTY,
                    &query, sizeof( query ),
                    &seekPenalty, sizeof( seekPenalty ),
                    &query_bytesReturned, NULL
                );

                if ( gotParam == TRUE &&
                     query_bytesReturned >= sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR) &&
                     seekPenalty.Version >= sizeof(DEVICE_SEEK_PENALTY_DESCRIPTOR) )
                {
                    bool hasOverhead = ( seekPenalty.IncursSeekPenalty != FALSE );

                    if ( !hasOverhead )
                    {
                        // If we have no overhead, we definately are a flash-based device.
                        // Those devices tend to wear out faster than other, so lets call them "solid state".
                        mediaType = eDiskMediaType::SOLID_STATE;
                    }
                    else
                    {
                        // If we do have overhead, we consider it being a rotating thing, because those tend to be like that.
                        // Common sense says that rotating things are more reliable because otherwise nobody would want a rotating thing over a solid thing.
                        // Even if the setting-up-thing is cheaper, it does justify dumping temporary files on it.
                        mediaType = eDiskMediaType::ROTATING_SPINDLE;
                    }
                }
            }

            // Check for TRIM command. I heard it is a good indicator for SSD.
            if ( mediaType == eDiskMediaType::UNKNOWN )
            {
                STORAGE_PROPERTY_QUERY query;
                query.PropertyId = StorageDeviceTrimProperty;
                query.QueryType = PropertyStandardQuery;

                DEVICE_TRIM_DESCRIPTOR trimDesc;

                DWORD trimDesc_bytesReturned;

                BOOL gotParam = DeviceIoControl(
                    volumeHandle, IOCTL_STORAGE_QUERY_PROPERTY,
                    &query, sizeof(query),
                    &trimDesc, sizeof(trimDesc),
                    &trimDesc_bytesReturned, NULL
                );

                if ( gotParam == TRUE &&
                     trimDesc_bytesReturned >= sizeof(trimDesc) &&
                     trimDesc.Version >= sizeof(trimDesc) )
                {
                    // I heard that only solid state things support TRIM, so a good assumption?
                    bool supportsTRIM = ( trimDesc.TrimEnabled == TRUE );

                    if ( supportsTRIM )
                    {
                        mediaType = eDiskMediaType::SOLID_STATE;
                    }
                }
            }
        }
    }
    catch( ... )
    {
        CloseHandle( volumeHandle );

        throw;
    }

    CloseHandle( volumeHandle );

    return mediaType;
#elif defined(__linux__)
    //TODO: actually implement this.
    return eDiskMediaType::UNKNOWN;
#else
    return eDiskMediaType::UNKNOWN;
#endif
}

inline bool IsDiskRemovable( const wchar_t *diskDescriptor_unc, const wchar_t *diskDescriptor_trail )
{
    bool isRemovable = false;

    // Not sure if this makes any sense on Linux, because on there you can simply unmount things, so
    // even hard wired things count as removable!

#ifdef _WIN32
    bool hasGottenRemovable = false;

    // Check the legacy API first.
    if ( !hasGottenRemovable )
    {
        UINT diskType = GetDriveTypeW( diskDescriptor_trail );

        if ( diskType == DRIVE_REMOVABLE ||
             diskType == DRIVE_REMOTE ||
             diskType == DRIVE_CDROM )
        {
            isRemovable = true;

            hasGottenRemovable = true;
        }
    }

    // Check things by volume handle.
    if ( !hasGottenRemovable )
    {
        HANDLE volumeHandle = CreateFileW( diskDescriptor_unc, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );

        if ( volumeHandle != INVALID_HANDLE_VALUE )
        {
            try
            {
                // Check the storage descriptor.
                if ( !hasGottenRemovable )
                {
                    STORAGE_PROPERTY_QUERY query;
                    query.PropertyId = StorageDeviceProperty;
                    query.QueryType = PropertyStandardQuery;

                    STORAGE_DEVICE_DESCRIPTOR devInfo;

                    DWORD devInfo_bytesReturned;

                    BOOL gotInfo = DeviceIoControl(
                        volumeHandle, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof( query ),
                        &devInfo, sizeof( devInfo ),
                        &devInfo_bytesReturned, nullptr
                    );

                    if ( gotInfo == TRUE &&
                         devInfo_bytesReturned >= sizeof(devInfo) &&
                         devInfo.Version >= sizeof(devInfo) )
                    {
                        // If this says that the drive is removable, sure it is!
                        bool isRemovable_desc = ( devInfo.RemovableMedia == TRUE );

                        if ( isRemovable_desc )
                        {
                            isRemovable = true;

                            hasGottenRemovable = true;
                        }
                    }
                }

                // Next check hot plug configuration.
                // This is a tricky check, as we iterate over every physical media attached to the volume.
                if ( !hasGottenRemovable )
                {
                    struct myVOLUME_DISK_EXTENTS
                    {
                        DWORD NumberOfDiskExtents;
                        DISK_EXTENT Extents[64];
                    };

                    myVOLUME_DISK_EXTENTS exts;

                    DWORD exts_bytesReturned;

                    BOOL gotExtentInfo = DeviceIoControl(
                        volumeHandle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                        nullptr, 0,
                        &exts, sizeof( exts ),
                        &exts_bytesReturned, nullptr
                    );

                    if ( gotExtentInfo == TRUE && exts_bytesReturned >= sizeof(DWORD) )
                    {
                        // Loop through all available.
                        const DWORD numExts = exts.NumberOfDiskExtents;

                        for ( DWORD n = 0; n < numExts; n++ )
                        {
                            // Get a real device info handle.
                            const DISK_EXTENT& extInfo = exts.Extents[ n ];

                            eir::String <wchar_t, FileSysCommonAllocator> physDescriptor( L"\\\\.\\PhysicalDrive" );

                            physDescriptor += eir::to_string <wchar_t, FileSysCommonAllocator> ( extInfo.DiskNumber );
                            {
                                HANDLE physHandle = CreateFileW( physDescriptor.GetConstString(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr );

                                if ( physHandle != INVALID_HANDLE_VALUE )
                                {
                                    STORAGE_HOTPLUG_INFO hotplug_info;

                                    DWORD hotplug_info_bytesReturned;

                                    BOOL gotInfo = DeviceIoControl(
                                        physHandle, IOCTL_STORAGE_GET_HOTPLUG_INFO,
                                        nullptr, 0,
                                        &hotplug_info, sizeof(hotplug_info),
                                        &hotplug_info_bytesReturned, nullptr
                                    );

                                    if ( gotInfo == TRUE &&
                                         hotplug_info_bytesReturned >= sizeof(hotplug_info) &&
                                         hotplug_info.Size >= sizeof(hotplug_info) )
                                    {
                                        // If the device counts as hot-pluggable, we want to treat it as removable.
                                        bool isHotplug = ( hotplug_info.MediaHotplug != 0 || hotplug_info.DeviceHotplug != 0 );
                                        bool isRemovable = ( hotplug_info.MediaRemovable != 0 );

                                        if ( isHotplug || isRemovable )
                                        {
                                            // OK.
                                            isRemovable = true;

                                            hasGottenRemovable = true;
                                            break;
                                        }
                                    }

                                    CloseHandle( physHandle );
                                }
                            }
                        }

                        // Alright.
                    }

                    // Apparently even the iteration through physical drives can lead to zero results.
                    // In that case, we have to try even harder...!
                }
            }
            catch( ... )
            {
                CloseHandle( volumeHandle );

                throw;
            }

            CloseHandle( volumeHandle );
        }
    }
#endif //_WIN32

    return isRemovable;
}

// Should request immutable information from non-removable disk drives in a management struct.
struct fileDataPresenceEnvInfo
{
    inline void Initialize( CFileSystemNative *fileSys )
    {
        this->fileSys = fileSys;
        this->hasInitializedDriveTrauma = false;

        // We should store an access device which is best suited to write temporary files to.
        // Our users will greatly appreciate this effort.
        // I know certain paranoid people that complain if you "trash [their] SSD drive".
        this->bestTempDriveRoot = nullptr;

        this->sysTempRoot = nullptr;        // allocated on demand inside temp drive root or global system root.
    }

    inline void Shutdown( CFileSystemNative *fileSys )
    {
        // We might want to even delete the folder of the application temp root, not sure.
        if ( CFileTranslator *tmpRoot = this->sysTempRoot )
        {
            delete tmpRoot;

            this->sysTempRoot = nullptr;
        }

        // Destroy our access to the temporary root again.
        if ( CFileTranslator *tmpRoot = this->bestTempDriveRoot )
        {
            delete tmpRoot;

            this->bestTempDriveRoot = nullptr;
        }

        this->hasInitializedDriveTrauma = false;
    }

    // We have to guard the access to the temporary root.
    // PLATFORM CODE.
    inline CFileTranslator* GetSystemTempDriveRoot( void )
    {
        // TODO: add lock here.

        if ( !this->hasInitializedDriveTrauma )
        {
            // Initialize things.
            CFileTranslator *bestTempDriveRoot = nullptr;

            try
            {
                struct drive_info
                {
                    eir::String <wchar_t, FSObjectHeapAllocator> driveRoot;
                    eDiskMediaType mediaType;
                    uint64_t diskSize;
                    uint64_t freeSpace;

                    bool operator < ( const drive_info& right ) const
                    {
                        return ( this->freeSpace > right.freeSpace );
                    }
                };

                eir::Set <drive_info, FSObjectHeapAllocator> media;

#ifdef _WIN32
                // On Windows we use the system API.
                const DWORD driveMask = GetLogicalDrives();

                for ( DWORD bit = 0; bit < 26; bit++ )
                {
                    // todo: put the drives that matter into a list.

                    const DWORD curBitMask = ( 1 << bit );

                    if ( ( driveMask & curBitMask ) != 0 )
                    {
                        // We got an active drive, so let us investigate.
                        const wchar_t driveRootChar = (wchar_t)( L'A' + bit );

                        wchar_t driveID[ 7 ];
                        driveID[0] = '\\';
                        driveID[1] = '\\';
                        driveID[2] = '.';
                        driveID[3] = '\\';
                        driveID[4] = driveRootChar;
                        driveID[5] = L':';
                        driveID[6] = L'\0';

                        wchar_t driveID_backslash[ 8 ];
                        driveID_backslash[0] = '\\';
                        driveID_backslash[1] = '\\';
                        driveID_backslash[2] = '.';
                        driveID_backslash[3] = '\\';
                        driveID_backslash[4] = driveRootChar;
                        driveID_backslash[5] = L':';
                        driveID_backslash[6] = L'\\';
                        driveID_backslash[7] = L'\0';

                        // Only care about the drive if not removable.
                        if ( IsDiskRemovable( driveID, driveID_backslash ) == false )
                        {
                            drive_info info;
                            info.driveRoot = getDriveRootDesc( driveRootChar );
                            info.mediaType = GetDiskMediaType( driveID );

                            // Calculate free space and stuff.
                            uint64_t freeSpaceNum = 0;
                            uint64_t totalSpaceNum = 0;
                            {
                                DWORD sectorsPerCluster;
                                DWORD sectorSize;
                                DWORD numFreeClusters;
                                DWORD numTotalClusters;

                                BOOL gotFreeSpace = GetDiskFreeSpaceW( driveID_backslash, &sectorsPerCluster, &sectorSize, &numFreeClusters, &numTotalClusters );

                                if ( gotFreeSpace == TRUE )
                                {
                                    freeSpaceNum = ( (uint64_t)numFreeClusters * sectorsPerCluster * sectorSize );
                                    totalSpaceNum = ( (uint64_t)numTotalClusters * sectorsPerCluster * sectorSize );
                                }
                            }

                            if ( freeSpaceNum > 0 )
                            {
                                info.freeSpace = freeSpaceNum;
                                info.diskSize = totalSpaceNum;

                                media.Insert( std::move( info ) );
                            }
                        }
                    }
                }
#elif defined(__linux__)
                // Linux version.
                //TODO.
#endif

                // We need to select the spindle drive with most free space as best place to put files at.
                // If we dont have any such spindle drive, we settle for any drive with most free space.
                for ( decltype(media)::iterator iter( media ); !iter.IsEnd(); iter.Increment() )
                {
                    const drive_info& info = iter.Resolve()->GetValue();

                    if ( info.mediaType == eDiskMediaType::ROTATING_SPINDLE )
                    {
                        if ( CFileTranslator *tmpRoot = fileSys->CreateTranslator( info.driveRoot.GetConstString() ) )
                        {
                            bestTempDriveRoot = tmpRoot;
                            break;
                        }
                    }
                }

                // If we could not find any of rotating spindle, we pick any other.
                if ( bestTempDriveRoot == nullptr )
                {
                    for ( decltype(media)::iterator iter( media ); !iter.IsEnd(); iter.Increment() )
                    {
                        const drive_info& info = iter.Resolve()->GetValue();

                        // Now try all other we did not touch before.
                        if ( info.mediaType != eDiskMediaType::ROTATING_SPINDLE )
                        {
                            if ( CFileTranslator *tempRoot = fileSys->CreateTranslator( info.driveRoot.GetConstString() ) )
                            {
                                bestTempDriveRoot = tempRoot;
                                break;
                            }
                        }
                    }
                }
            }
            catch( ... )
            {
                // Usually this must not happen.
                if ( bestTempDriveRoot )
                {
                    delete bestTempDriveRoot;
                }

                throw;
            }

            // If we could decide one, set it.
            if ( bestTempDriveRoot )
            {
                this->bestTempDriveRoot = bestTempDriveRoot;
            }

            // If we could not establish _any_ safe temporary root, we will have to allocate all files on RAM.
            // This is entirely possible but of course we wish to put files on disk to not stress the user too much.
            // Applications should display a warning that there is no efficient temp disk storage available.

            this->hasInitializedDriveTrauma = true;
        }

        return this->bestTempDriveRoot;
    }

    CFileTranslator *volatile sysTempRoot;

private:
    CFileSystemNative *fileSys;

    // Immutable properties fetched from the OS.
    bool hasInitializedDriveTrauma;

    CFileTranslator *bestTempDriveRoot;
};

static PluginDependantStructRegister <fileDataPresenceEnvInfo, fileSystemFactory_t> fileDataPresenceEnvInfoRegister;

static fsLockProvider _fileSysTmpDirLockProvider;

static void GetSystemTemporaryRootPath( filePath& absPathOut )
{
    filePath tmpDirBase;

#ifdef _WIN32
    wchar_t buf[2048];

    GetTempPathW( NUMELMS( buf ) - 1, buf );

    buf[ NUMELMS( buf ) - 1 ] = 0;

    // Transform the path into something we can recognize.
    tmpDirBase.insert( 0, buf, 2 );
    tmpDirBase += FileSystem::GetDirectorySeparator <wchar_t> ( true );

    normalNodePath normalPath = _File_NormalizeRelativePath( buf + 3, pathcheck_win32() );

    assert( normalPath.isFilePath == false && normalPath.backCount == 0 );

    _File_OutputPathTree( normalPath.travelNodes, normalPath.isFilePath, true, tmpDirBase );
#elif defined(__linux__)
    const char *dir = getenv("TEMPDIR");

    if ( !dir )
        tmpDirBase = "/tmp";
    else
        tmpDirBase = dir;

    tmpDirBase += '/';

    // On linux we cannot be sure that our directory exists.
    if ( !_File_CreateDirectory( tmpDirBase ) )
    {
        assert( 0 );

        exit( 7098 );
    }
#endif //OS DEPENDANT CODE

    absPathOut = std::move( tmpDirBase );
}

// PLATFORM CODE.
static bool IsPathOnSystemDrive( const wchar_t *sysPath )
{
#ifdef _WIN32
    // We define the system drive as host of the Windows directory.
    // This thing does only make sense on Windows systems anyway.
    // Refactoring so that we support both Linux and Windows has to be done again at some point.

    UINT charCount = GetWindowsDirectoryW( nullptr, 0 );

    eir::Vector <wchar_t, FileSysCommonAllocator> winDirPath;

    winDirPath.Resize( charCount + 1 );

    GetWindowsDirectoryW( (wchar_t*)winDirPath.GetData(), charCount );

    winDirPath[ charCount ] = L'\0';

    platform_rootPathType rootSysPath;
    {
        if ( !rootSysPath.buildFromSystemPath( sysPath, false ) )
        {
            return false;
        }
    }

    platform_rootPathType rootWinDirPath;
    {
        if ( !rootWinDirPath.buildFromSystemPath( winDirPath.GetData(), false ) )
        {
            return false;
        }
    }

    // Check whether the drives/volumes match.
    return rootSysPath.doesRootDescriptorMatch( rootWinDirPath );
#else
    // Else we just say that everything is on the system drive.
    // Might reapproach this sometime.
    return true;
#endif
}

// Temporary root management.
CFileTranslator* CFileSystem::GenerateTempRepository( void )
{
    CFileSystemNative *fileSys = (CFileSystemNative*)this;

    fileDataPresenceEnvInfo *envInfo = fileDataPresenceEnvInfoRegister.GetPluginStruct( fileSys );

    if ( !envInfo )
        return nullptr;

    filePath tmpDirBase;

    // Check whether we have a handle to the global temporary system storage.
    // If not, attempt to retrieve it.
    bool needsTempDirFetch = true;

    if ( !envInfo->sysTempRoot )
    {
#ifdef FILESYS_MULTI_THREADING
        NativeExecutive::CReadWriteWriteContextSafe <> consistency( _fileSysTmpDirLockProvider.GetReadWriteLock( this ) );
#endif //FILESYS_MULTI_THREADING

        if ( !envInfo->sysTempRoot )
        {
            // Check if we have a recommended system temporary root drive.
            // If we do, then we should get a temp root in there.
            // Otherwise we simply resort to the OS main temp dir.
            bool hasTempRoot = false;

            if ( !hasTempRoot )
            {
                if ( CFileTranslator *recTmpRoot = envInfo->GetSystemTempDriveRoot() )
                {
                    // Is it on the system drive?
                    filePath fullPathOfTemp;

                    recTmpRoot->GetFullPathFromRoot( L"", false, fullPathOfTemp );

                    fullPathOfTemp.transform_to <wchar_t> ();

                    if ( IsPathOnSystemDrive( fullPathOfTemp.w_str() ) == false )
                    {
                        // We can create a generic temporary root.
                        tmpDirBase = ( fullPathOfTemp + L"Temp/" );

                        // It of course has to succeed in creation, too!
                        if ( recTmpRoot->CreateDir( tmpDirBase ) )
                        {
                            hasTempRoot = true;
                        }
                    }
                }
            }

            if ( !hasTempRoot )
            {
                GetSystemTemporaryRootPath( tmpDirBase );

                hasTempRoot = true;
            }

            // If we have a temp root, we can do things.
            if ( !hasTempRoot )
                return nullptr;

            envInfo->sysTempRoot = fileSystem->CreateTranslator( tmpDirBase );

            // We failed to get the handle to the temporary storage, hence we cannot deposit temporary files!
            if ( !envInfo->sysTempRoot )
                return nullptr;

            needsTempDirFetch = false;
        }
    }

    if ( needsTempDirFetch )
    {
        bool success = envInfo->sysTempRoot->GetFullPath( "//", false, tmpDirBase );

        if ( !success )
            return nullptr;
    }

    // Generate a random sub-directory inside of the global OS temp directory.
    // We need to generate until we find a unique directory.
    unsigned int numOfTries = 0;

    filePath tmpDir;

    while ( numOfTries < 50 )
    {
        filePath tmpDir( tmpDirBase );

        auto randNum = eir::to_string <char, FSObjectHeapAllocator> ( fsrandom::getSystemRandom( this ) );

        tmpDir += "&$!reAr";
        tmpDir += randNum;
        tmpDir += "_/";

        if ( envInfo->sysTempRoot->Exists( tmpDir ) == false )
        {
            // Once we found a not existing directory, we must create and acquire a handle
            // to it. This operation can fail if somebody else happens to delete the directory
            // inbetween or snatched away the handle to the directory before us.
            // Those situations are very unlikely, but we want to make sure anyway, for quality's sake.

            // Make sure the temporary directory exists.
            bool creationSuccessful = _File_CreateDirectory( tmpDir );

            if ( creationSuccessful )
            {
                // Create the temporary root
                CFileTranslator *result = fileSystem->CreateTranslator( tmpDir, DIR_FLAG_EXCLUSIVE );

                if ( result )
                {
                    // Success!
                    return result;
                }
            }

            // Well, we failed for some reason, so try again.
        }

        numOfTries++;
    }

    // Nope. Maybe the user wants to try again?
    return nullptr;
}

void CFileSystem::DeleteTempRepository( CFileTranslator *repo )
{
    CFileSystemNative *fileSys = (CFileSystemNative*)this;

    fileDataPresenceEnvInfo *envInfo = fileDataPresenceEnvInfoRegister.GetPluginStruct( fileSys );

    CFileTranslator *sysTmp = nullptr;

    if ( envInfo )
    {
        sysTmp = envInfo->sysTempRoot;
    }

    // Keep deleting like usual.
    filePath pathOfDir;
    bool gotActualPath = false;

    try
    {
        gotActualPath = repo->GetFullPathFromRoot( L"//", false, pathOfDir );
    }
    catch( ... )
    {
        gotActualPath = false;

        // Continue.
    }

    // We can now release the handle to the directory.
    delete repo;

    // We can only really delete if we have the system temporary root.
    if ( sysTmp )
    {
        if ( gotActualPath )
        {
            // Delete us.
            sysTmp->Delete( pathOfDir );
        }
    }
}

CFileDataPresenceManager::CFileDataPresenceManager( CFileSystemNative *fileSys )
{
    LIST_CLEAR( activeFiles.root );

    this->fileSys = fileSys;

    this->onDiskTempRoot = nullptr;     // we initialize this on demand.

    this->maximumDataQuotaRAM = 0;
    this->hasMaximumDataQuotaRAM = false;
    this->fileMaxSizeInRAM = 0x40000;           // todo: properly calculate this value.

    this->percFileMemoryFadeIn = 0.667f;

    // Setup statistics.
    this->totalRAMMemoryUsageByFiles = 0;
}

CFileDataPresenceManager::~CFileDataPresenceManager( void )
{
    // Make sure everyone released active files beforehand.
    assert( LIST_EMPTY( activeFiles.root ) == true );

    // We should have no RAM usage by memory files.
    assert( this->totalRAMMemoryUsageByFiles == 0 );

    // Clean up the temporary root.
    if ( CFileTranslator *tmpRoot = this->onDiskTempRoot )
    {
        this->fileSys->DeleteTempRepository( tmpRoot );

        this->onDiskTempRoot = nullptr;
    }
}

void CFileDataPresenceManager::SetMaximumDataQuotaRAM( size_t maxQuota )
{
    this->maximumDataQuotaRAM = maxQuota;

    this->hasMaximumDataQuotaRAM = true;
}

void CFileDataPresenceManager::UnsetMaximumDataQuotaRAM( void )
{
    this->hasMaximumDataQuotaRAM = false;
}

CFileTranslator* CFileDataPresenceManager::GetLocalFileTranslator( void )
{
    // TODO: add lock here.

    if ( !this->onDiskTempRoot )
    {
        this->onDiskTempRoot = fileSys->GenerateTempRepository();
    }

    return this->onDiskTempRoot;
}

CFile* CFileDataPresenceManager::AllocateTemporaryDataDestination( fsOffsetNumber_t minimumExpectedSize )
{
    // TODO: take into account the minimumExpectedSize parameter!

    CFile *outFile = nullptr;
    {
        // We simply start out by putting the file into RAM.
        CMemoryMappedFile *memFile = new CMemoryMappedFile( this->fileSys );

        if ( memFile )
        {
            try
            {
                // Create our managed wrapper.
                swappableDestDevice *swapDevice = new swappableDestDevice( this, memFile, eFilePresenceType::MEMORY );

                if ( swapDevice )
                {
                    outFile = swapDevice;
                }
            }
            catch( ... )
            {
                delete memFile;

                throw;
            }

            if ( !outFile )
            {
                delete memFile;
            }
        }
    }
    return outFile;
}

void CFileDataPresenceManager::IncreaseRAMTotal( swappableDestDevice *memFile, fsOffsetNumber_t memSize )
{
    this->totalRAMMemoryUsageByFiles += memSize;
}

void CFileDataPresenceManager::DecreaseRAMTotal( swappableDestDevice *memFile, fsOffsetNumber_t memSize )
{
    this->totalRAMMemoryUsageByFiles -= memSize;
}

void CFileDataPresenceManager::NotifyFileSizeChange( swappableDestDevice *file, fsOffsetNumber_t newProposedSize )
{
    eFilePresenceType curPresence = file->presenceType;

    // Check if the file needs movement/where the file should be at.
    eFilePresenceType reqPresence = curPresence;
    {
        bool shouldSwapToLocalFile = false;
        bool shouldSwapToMemory = false;

        if ( curPresence == eFilePresenceType::MEMORY )
        {
            size_t sizeSwapToLocalFile = this->fileMaxSizeInRAM;

            // Check local maximum.
            if ( newProposedSize >= (fsOffsetNumber_t)sizeSwapToLocalFile )
            {
                shouldSwapToLocalFile = true;
            }

            if ( this->hasMaximumDataQuotaRAM )
            {
                // Check global maximum.
                if ( this->totalRAMMemoryUsageByFiles - file->lastRegisteredFileSize + newProposedSize > (fsOffsetNumber_t)this->maximumDataQuotaRAM )
                {
                    shouldSwapToLocalFile = true;
                }
            }
        }
        else if ( curPresence == eFilePresenceType::LOCALFILE )
        {
            // We can shrink enough so that we can get into memory again.
            size_t sizeMoveBackToMemory = this->GetFileMemoryFadeInSize();

            // Check local maximum.
            if ( newProposedSize < (fsOffsetNumber_t)sizeMoveBackToMemory )
            {
                shouldSwapToMemory = true;
            }

            // We do not do operations based on the global maximum.
        }

        if ( shouldSwapToLocalFile )
        {
            reqPresence = eFilePresenceType::LOCALFILE;
        }
        else if ( shouldSwapToMemory )
        {
            reqPresence = eFilePresenceType::MEMORY;
        }
    }

    // Have we even decided that a move makes sense?
    if ( curPresence == reqPresence )
        return;

    // Perform actions that have been decided.
    //bool hasSwapped = false;

    CFile *handleToMoveTo = nullptr;

    if ( reqPresence == eFilePresenceType::LOCALFILE )
    {
        if ( CFileTranslator *localTrans = this->GetLocalFileTranslator() )
        {
            // Try performing said operation once.
            CFile *tempFileOnDisk = this->fileSys->GenerateRandomFile( localTrans );

            // TODO: maybe ask for reliable random files?

            if ( tempFileOnDisk )
            {
                handleToMoveTo = tempFileOnDisk;

                //hasSwapped = true;
            }
        }
    }

    // If we have no handle, no point in continuing.
    if ( !handleToMoveTo )
        return;

    // Copy stuff over.
    try
    {
        CFile *currentDataSource = file->dataSource;

        fsOffsetNumber_t currentSeek = currentDataSource->TellNative();

        currentDataSource->Seek( 0, SEEK_SET );

        FileSystem::StreamCopy( *currentDataSource, *handleToMoveTo );

        handleToMoveTo->SeekNative( currentSeek, SEEK_SET );

        // Success!
        // (it does not matter if we succeed or not, the show must go on)
    }
    catch( ... )
    {
        delete handleToMoveTo;

        throw;
    }

    // Delete old source.
    delete file->dataSource;

    // Register the change.
    file->dataSource = handleToMoveTo;
    file->presenceType = reqPresence;

    // Terminate registration of previous presence type.
    if ( curPresence == eFilePresenceType::MEMORY )
    {
        this->totalRAMMemoryUsageByFiles -= file->lastRegisteredFileSize;

        file->lastRegisteredFileSize = 0;
    }
}

void CFileDataPresenceManager::UpdateFileSizeMetrics( swappableDestDevice *file )
{
    // TODO: maybe verify how often our assumption of file growth were incorrect, so that we
    // exceeded the total "allowed" RAM usage.

    if ( file->presenceType == eFilePresenceType::MEMORY )
    {
        CFile *currentDataSource = file->dataSource;

        fsOffsetNumber_t newFileSize = currentDataSource->GetSizeNative();

        this->totalRAMMemoryUsageByFiles -= file->lastRegisteredFileSize;
        this->totalRAMMemoryUsageByFiles += newFileSize;

        file->lastRegisteredFileSize = newFileSize;
    }
}

void CFileDataPresenceManager::CleanupLocalFile( CFile *file ) noexcept
{
    CFileTranslator *tmpRoot = this->onDiskTempRoot;

    filePath localFilePath;

    try
    {
        localFilePath = file->GetPath();
    }
    catch( ... )
    {
        // We simply cary on.
    }

    delete file;

    if ( tmpRoot && localFilePath.empty() == false )
    {
        tmpRoot->Delete( localFilePath );
    }
}

size_t CFileDataPresenceManager::swappableDestDevice::Read( void *buffer, size_t readCount )
{
    if ( !this->isReadable )
        return 0;

    return dataSource->Read( buffer, readCount );
}

size_t CFileDataPresenceManager::swappableDestDevice::Write( const void *buffer, size_t writeCount )
{
    if ( !this->isWriteable )
        return 0;

    // TODO: guard the size of this file; it must not overshoot certain limits or else the file has to be relocated.
    // TODO: add locks so this file type becomes thread-safe!

    CFileDataPresenceManager *manager = this->manager;

    bool isExpandingOp = false;
    fsOffsetNumber_t expandTo;
    {
        CFile *currentDataSource = this->dataSource;

        // Check if we would increase in size, and if then, check if we are still allowed to have our storage where it is.
        fsOffsetNumber_t currentSeek = currentDataSource->TellNative();
        fsOffsetNumber_t fsWriteCount = (fsOffsetNumber_t)( writeCount );

        fileStreamSlice_t opSlice( currentSeek, fsWriteCount );

        expandTo = ( opSlice.GetSliceEndPoint() + 1 );

        // Get the file bounds.
        fsOffsetNumber_t currentSize = currentDataSource->GetSizeNative();

        fileStreamSlice_t boundsSlice( 0, currentSize );

        // An intersection tells us if we try to access out-of-bounds data.
        eir::eIntersectionResult intResult = opSlice.intersectWith( boundsSlice );

        if ( intResult == eir::INTERSECT_BORDER_START ||
             intResult == eir::INTERSECT_FLOATING_END ||
             intResult == eir::INTERSECT_ENCLOSING )
        {
            isExpandingOp = true;
        }
    }

    // If we are expanding, then we should be wary of by how much.
    if ( isExpandingOp )
    {
        // Need to update file stability.
        manager->NotifyFileSizeChange( this, expandTo );
    }

    // Finish the write operation.
    size_t actualWriteCount = this->dataSource->Write( buffer, writeCount );

    // Update our file size.
    manager->UpdateFileSizeMetrics( this );

    return actualWriteCount;
}

bool CFileDataPresenceManager::swappableDestDevice::QueryStats( filesysStats& statsOut ) const noexcept
{
    statsOut.atime = this->meta_atime;
    statsOut.ctime = this->meta_ctime;
    statsOut.mtime = this->meta_mtime;
    statsOut.attribs.type = eFilesysItemType::FILE;
    statsOut.attribs.isTemporary = true;
    return true;
}

void CFileDataPresenceManager::swappableDestDevice::SetFileTimes( time_t atime, time_t ctime, time_t mtime )
{
    this->meta_atime = atime;
    this->meta_mtime = mtime;
    this->meta_ctime = ctime;
}

void CFileDataPresenceManager::swappableDestDevice::SetSeekEnd( void )
{
    // TODO: guard this truncation of file and manage the memory properly.

    // Will the file size change?
    bool willSizeChange = false;
    fsOffsetNumber_t newProposedSize;
    {
        CFile *currentDataSource = this->dataSource;

        fsOffsetNumber_t currentSeek = currentDataSource->TellNative();
        fsOffsetNumber_t currentFileSize = currentDataSource->GetSizeNative();

        if ( currentSeek != currentFileSize )
        {
            willSizeChange = true;

            newProposedSize = std::max( (fsOffsetNumber_t)0, currentSeek );
        }
    }

    if ( willSizeChange )
    {
        manager->NotifyFileSizeChange( this, newProposedSize );
    }

    dataSource->SetSeekEnd();

    // Update file size metrics.
    manager->UpdateFileSizeMetrics( this );
}

// Initialization of this module.
void registerFileDataPresenceManagement( const fs_construction_params& params )
{
    _fileSysTmpDirLockProvider.RegisterPlugin( params );

    fileDataPresenceEnvInfoRegister.RegisterPlugin( _fileSysFactory );
}

void unregisterFileDataPresenceManagement( void )
{
    fileDataPresenceEnvInfoRegister.UnregisterPlugin();

    _fileSysTmpDirLockProvider.UnregisterPlugin();
}
