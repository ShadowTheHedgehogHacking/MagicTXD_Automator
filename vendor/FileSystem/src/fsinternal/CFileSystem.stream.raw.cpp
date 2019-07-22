/*****************************************************************************
*
*  PROJECT:     Eir FileSystem
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        FileSystem/src/fsinternal/CFileSystem.stream.raw.cpp
*  PURPOSE:     Raw OS filesystem file link
*
*  Get Eir FileSystem from https://osdn.net/projects/eirfs/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/
#include <StdInc.h>

// Include internal header.
#include "CFileSystem.internal.h"

// Sub modules.
#include "CFileSystem.platform.h"
#include "CFileSystem.stream.raw.h"

#include "CFileSystem.internal.nativeimpl.hxx"

using namespace FileSystem;

enum eNumberConversion
{
    NUMBER_LITTLE_ENDIAN,
    NUMBER_BIG_ENDIAN
};

// Utilities for number splitting.
static inline unsigned int CalculateNumberSplitCount( size_t toBeSplitNumberSize, size_t nativeNumberSize )
{
    return (unsigned int)( toBeSplitNumberSize / nativeNumberSize );
}

template <typename nativeNumberType, typename splitNumberType>
static inline nativeNumberType* GetNumberSector(
            splitNumberType& numberToBeSplit,
            unsigned int index,
            eNumberConversion splitConversion, eNumberConversion nativeConversion )
{
    // Extract the sector out of numberToBeSplit.
    nativeNumberType& nativeNumberPartial = *( (nativeNumberType*)&numberToBeSplit + index );

    return &nativeNumberPartial;
}

template <typename splitNumberType, typename nativeNumberType>
AINLINE void SplitIntoNativeNumbers(
            splitNumberType numberToBeSplit,
            nativeNumberType *nativeNumbers,
            unsigned int maxNativeNumbers, unsigned int& nativeCount,
            eNumberConversion toBeSplitConversion, eNumberConversion nativeConversion )
{
    // todo: add endian-ness support.
    assert( toBeSplitConversion == nativeConversion );

    // We assume a lot of things here.
    // - a binary number system
    // - endian integer system
    // else this routine will produce garbage.

    // On Visual Studio 2008, this routine optimizes down completely into compiler intrinsics.

    const size_t nativeNumberSize = sizeof( nativeNumberType );
    const size_t toBeSplitNumberSize = sizeof( splitNumberType );

    // Calculate the amount of numbers we can fit into the array.
    unsigned int splitNumberCount = std::min( CalculateNumberSplitCount( toBeSplitNumberSize, nativeNumberSize ), maxNativeNumbers );
    unsigned int actualWriteCount = 0;

    for ( unsigned int n = 0; n < splitNumberCount; n++ )
    {
        // Write it into the array.
        nativeNumbers[ actualWriteCount++ ] = *GetNumberSector <nativeNumberType> ( numberToBeSplit, n, toBeSplitConversion, nativeConversion );
    }

    // Notify the runtime about how many numbers we have successfully written.
    nativeCount = actualWriteCount;
}

template <typename splitNumberType, typename nativeNumberType>
AINLINE void ConvertToWideNumber(
            splitNumberType& wideNumberOut,
            nativeNumberType *nativeNumbers, unsigned int numNativeNumbers,
            eNumberConversion wideConversion, eNumberConversion nativeConversion )
{
    // todo: add endian-ness support.
    assert( wideConversion == nativeConversion );

    // we assume the same deals as SplitIntoNativeNumbers.
    // else this routine is garbage.

    // On Visual Studio 2008, this routine optimizes down completely into compiler intrinsics.

    const size_t nativeNumberSize = sizeof( nativeNumberType );
    const size_t toBeSplitNumberSize = sizeof( splitNumberType );

    // Calculate the amount of numbers we need to put together.
    unsigned int splitNumberCount = CalculateNumberSplitCount( toBeSplitNumberSize, nativeNumberSize );

    for ( unsigned int n = 0; n < splitNumberCount; n++ )
    {
        // Write it into the number.
        nativeNumberType numberToWrite = (nativeNumberType)0;

        if ( n < numNativeNumbers )
        {
            numberToWrite = nativeNumbers[ n ];
        }

        *GetNumberSector <nativeNumberType> ( wideNumberOut, n, wideConversion, nativeConversion ) = numberToWrite;
    }
}

/*===================================================
    CRawFile

    This class represents a file on the system.
    As long as it is present, the file is opened.

    WARNING: using this class directly is discouraged,
        as it uses direct methods of writing into
        hardware. wrap it with CBufferedStreamWrap instead!

    fixme: Port to mac
===================================================*/

CRawFile::CRawFile( filePath absFilePath, filesysAccessFlags flags ) : m_access( std::move( flags ) ), m_path( std::move( absFilePath ) )
{
    return;
}

CRawFile::~CRawFile( void )
{
#ifdef _WIN32
    CloseHandle( m_file );
#elif defined(__linux__)
    close( m_fileIndex );
#else
#error no OS file destructor implementation
#endif //OS DEPENDANT CODE
}

size_t CRawFile::Read( void *pBuffer, size_t readCount )
{
#ifdef _WIN32
    DWORD dwBytesRead;

    if (readCount == 0)
        return 0;

    BOOL readComplete = ReadFile(m_file, pBuffer, (DWORD)readCount, &dwBytesRead, NULL);

    if ( readComplete == FALSE )
        return 0;

    return dwBytesRead;
#elif defined(__linux__)
    ssize_t actualReadCount = read( this->m_fileIndex, pBuffer, readCount );

    if ( actualReadCount < 0 )
    {
        return 0;
    }

    return (size_t)actualReadCount;
#else
#error no OS file read implementation
#endif //OS DEPENDANT CODE
}

size_t CRawFile::Write( const void *pBuffer, size_t writeCount )
{
#ifdef _WIN32
    DWORD dwBytesWritten;

    if (writeCount == 0)
        return 0;

    BOOL writeComplete = WriteFile(m_file, pBuffer, (DWORD)writeCount, &dwBytesWritten, nullptr);

    if ( writeComplete == FALSE )
        return 0;

    return dwBytesWritten;
#elif defined(__linux__)
    ssize_t actualWriteCount = write( this->m_fileIndex, pBuffer, writeCount );

    if ( actualWriteCount < 0 )
    {
        return 0;
    }

    return (size_t)actualWriteCount;
#else
#error no OS file write implementation
#endif //OS DEPENDANT CODE
}

int CRawFile::Seek( long iOffset, int iType )
{
#ifdef _WIN32
    if (SetFilePointer(m_file, iOffset, nullptr, iType) == INVALID_SET_FILE_POINTER)
        return -1;
    return 0;
#elif defined(__linux__)
    off_t new_off = lseek( m_fileIndex, iOffset, iType );

    return ( new_off != (off_t)-1 ? 0 : -1 );
#else
#error no OS file seek implementation
#endif //OS DEPENDANT CODE
}

int CRawFile::SeekNative( fsOffsetNumber_t iOffset, int iType )
{
#ifdef _WIN32
    // Split our offset into two DWORDs.
    LONG numberParts[ 2 ];
    unsigned int splitCount = 0;

    SplitIntoNativeNumbers( iOffset, numberParts, NUMELMS(numberParts), splitCount, NUMBER_LITTLE_ENDIAN, NUMBER_LITTLE_ENDIAN );

    // Tell the OS.
    // Using the preferred method.
    DWORD resultVal = INVALID_SET_FILE_POINTER;

    if ( splitCount == 1 )
    {
        resultVal = SetFilePointer( this->m_file, numberParts[0], nullptr, iType );
    }
    else if ( splitCount >= 2 )
    {
        resultVal = SetFilePointer( this->m_file, numberParts[0], &numberParts[1], iType );
    }

    if ( resultVal == INVALID_SET_FILE_POINTER )
        return -1;

    return 0;
#elif defined(__linux__)
    off64_t new_off = lseek64( m_fileIndex, (off64_t)iOffset, iType );

    return ( new_off != (off64_t)-1 ? 0 : -1 );
#else
#error no OS file seek native implementation
#endif //OS DEPENDANT CODE
}

long CRawFile::Tell( void ) const noexcept
{
#ifdef _WIN32
    LARGE_INTEGER posToMoveTo;
    posToMoveTo.LowPart = 0;
    posToMoveTo.HighPart = 0;

    LARGE_INTEGER currentPos;

    BOOL success = SetFilePointerEx( this->m_file, posToMoveTo, &currentPos, FILE_CURRENT );

    if ( success == FALSE )
        return -1;

    return (long)( currentPos.LowPart );
#elif defined(__linux__)
    return (long)lseek( this->m_fileIndex, 0, SEEK_CUR );
#else
#error no OS file tell implementation
#endif //OS DEPENDANT CODE
}

fsOffsetNumber_t CRawFile::TellNative( void ) const noexcept
{
#ifdef _WIN32
    LARGE_INTEGER posToMoveTo;
    posToMoveTo.LowPart = 0;
    posToMoveTo.HighPart = 0;

    union
    {
        LARGE_INTEGER currentPos;
        DWORD currentPos_split[ sizeof( LARGE_INTEGER ) / sizeof( DWORD ) ];
    };

    BOOL success = SetFilePointerEx( this->m_file, posToMoveTo, &currentPos, FILE_CURRENT );

    if ( success == FALSE )
        return (fsOffsetNumber_t)0;

    // Create a FileSystem number.
    fsOffsetNumber_t resultNumber = (fsOffsetNumber_t)0;

    ConvertToWideNumber( resultNumber, &currentPos_split[0], NUMELMS(currentPos_split), NUMBER_LITTLE_ENDIAN, NUMBER_LITTLE_ENDIAN );

    return resultNumber;
#elif defined(__linux__)
    return (fsOffsetNumber_t)lseek64( this->m_fileIndex, 0, SEEK_CUR );
#else
#error no OS file tell native implementation
#endif //OS DEPENDANT CODE
}

bool CRawFile::IsEOF( void ) const noexcept
{
#if defined(_WIN32) || defined(__linux__)
    // Check that the current file seek is beyond or equal the maximum size.
    return this->TellNative() >= this->GetSizeNative();
#else
#error no OS file end-of implementation
#endif //OS DEPENDANT CODE
}

bool CRawFile::QueryStats( filesysStats& statsOut ) const noexcept
{
#ifdef _WIN32
    return _FileWin32_GetFileInformation( m_file, statsOut );
#elif defined(__linux__)
    struct stat linux_stats;

    if ( fstat( this->m_fileIndex, &linux_stats ) != 0 )
    {
        return false;
    }

    statsOut.atime = linux_stats.st_atime;
    statsOut.ctime = linux_stats.st_ctime;
    statsOut.mtime = linux_stats.st_mtime;
    statsOut.attribs.type = eFilesysItemType::FILE;
    // TODO: improve the returned attributes by more information.
    return true;
#else
#error no OS file stat implementation
#endif //OS DEPENDANT CODE
}

#ifdef _WIN32
inline static void TimetToFileTime( time_t t, LPFILETIME pft )
{
    LONGLONG ll = Int32x32To64(t, 10000000) + 116444736000000000;
    pft->dwLowDateTime = (DWORD) ll;
    pft->dwHighDateTime = ll >>32;
}
#endif //_WIN32

void CRawFile::SetFileTimes( time_t atime, time_t ctime, time_t mtime )
{
#ifdef _WIN32
    FILETIME win32_ctime;
    FILETIME win32_atime;
    FILETIME win32_mtime;

    TimetToFileTime( ctime, &win32_ctime );
    TimetToFileTime( atime, &win32_atime );
    TimetToFileTime( mtime, &win32_mtime );

    SetFileTime( m_file, &win32_ctime, &win32_atime, &win32_mtime );
#elif defined(__linux__)
    struct utimbuf timeBuf;
    timeBuf.actime = atime;
    timeBuf.modtime = mtime;

    auto ansiFilePath = this->m_path.convert_ansi <FSObjectHeapAllocator> ();

    utime( ansiFilePath.GetConstString(), &timeBuf );
#else
#error no OS file push stat implementation
#endif //OS DEPENDANT CODE
}

void CRawFile::SetSeekEnd( void )
{
#ifdef _WIN32
    SetEndOfFile( m_file );
#elif defined(__linux__)
    int fd = this->m_fileIndex;

    off64_t curseek = lseek64( fd, 0, SEEK_CUR );

    int success = ftruncate64( fd, curseek );

    assert( success == 0 );
#else
#error no OS file set seek end implementation
#endif //OS DEPENDANT CODE
}

size_t CRawFile::GetSize( void ) const noexcept
{
#ifdef _WIN32
    return (size_t)GetFileSize( m_file, nullptr );
#elif defined(__linux__)
    struct stat fileInfo;

    int success = fstat( this->m_fileIndex, &fileInfo );

    if ( success != 0 )
    {
        return 0;
    }

    return fileInfo.st_size;
#else
#error no OS file get size implementation
#endif //OS DEPENDANT CODE
}

fsOffsetNumber_t CRawFile::GetSizeNative( void ) const noexcept
{
#ifdef _WIN32
    union
    {
        LARGE_INTEGER fileSizeOut;
        DWORD fileSizeOut_split[ sizeof( LARGE_INTEGER ) / sizeof( DWORD ) ];
    };

    BOOL success = GetFileSizeEx( this->m_file, &fileSizeOut );

    if ( success == FALSE )
        return (fsOffsetNumber_t)0;

    // Convert to a FileSystem native number.
    fsOffsetNumber_t bigFileSizeNumber = (fsOffsetNumber_t)0;

    ConvertToWideNumber( bigFileSizeNumber, &fileSizeOut_split[0], NUMELMS(fileSizeOut_split), NUMBER_LITTLE_ENDIAN, NUMBER_LITTLE_ENDIAN );

    return bigFileSizeNumber;
#elif defined(__linux__)
    struct stat64 large_stat;

    int success = fstat64( this->m_fileIndex, &large_stat );

    if ( success != 0 )
    {
        return 0;
    }

    return large_stat.st_size;
#else
#error no OS file get size native implementation
#endif //OS DEPENDANT CODE
}

void CRawFile::Flush( void )
{
#ifdef _WIN32
    FlushFileBuffers( m_file );
#elif defined(__linux__)
    fsync( this->m_fileIndex );
    fdatasync( this->m_fileIndex );
#else
#error no OS file flush implementation
#endif //OS DEPENDANT CODE
}

filePath CRawFile::GetPath( void ) const
{
    return m_path;
}

bool CRawFile::IsReadable( void ) const noexcept
{
    return m_access.allowRead;
}

bool CRawFile::IsWriteable( void ) const noexcept
{
    return m_access.allowWrite;
}
