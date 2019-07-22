/*****************************************************************************
*
*  PROJECT:     Eir FileSystem
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        FileSystem/src/fsinternal/CFileSystem.stream.buffered.cpp
*  PURPOSE:     Buffered stream utilities for block-based streaming
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

#include <sdk/MemoryUtils.stream.h>

#include "CFileSystem.stream.chunkbuf.h"

/*===================================================
    CBufferedStreamWrap

    An extension of the raw file that uses buffered IO.
    Since, in reality, hardware is sector based, the
    preferred way of writing data to disk is using buffers.

    Always prefer this class instead of CRawFile!
    Only use raw communication if you intend to put your
    own buffering!

    While a file stream is wrapped, the usage of the toBeWrapped
    pointer outside of the wrapper class leads to
    undefined behavior.

    I have not properly documented this buffered system yet.
    Until I have, change of this class is usually not permitted
    other than by me (in fear of breaking anything).

    Arguments:
        toBeWrapped - stream pointer that should be buffered
        deleteOnQuit - if true, toBeWrapped is deleted aswell
                       when this class is deleted

    Cool Ideas:
    -   Create more interfaces that wrap FileSystem streams
        so applying attributes to streams is a simple as
        wrapping a virtual class
===================================================*/

static constexpr size_t PREFERRED_BUF_SIZE = 1024;

CBufferedStreamWrap::CBufferedStreamWrap( CFile *toBeWrapped, bool deleteOnQuit ) : underlyingStream( toBeWrapped )
{
    assert( toBeWrapped != nullptr );

    this->internalIOBuffer = fileSystem->MemAlloc( PREFERRED_BUF_SIZE, 1 );
    this->internalIOBufferSize = PREFERRED_BUF_SIZE;

    // We have no valid regions at the start.

    fsOffsetNumber_t fileSeek = toBeWrapped->TellNative();

    this->fileSeek = fileSeek;

    // Get the real buffer position.
    this->bufOffset = ( fileSeek - ( fileSeek % (fsOffsetNumber_t)PREFERRED_BUF_SIZE ) );

    this->terminateUnderlyingData = deleteOnQuit;
}

CBufferedStreamWrap::~CBufferedStreamWrap( void )
{
    // Push any pending buffer operations onto disk space.
    this->FlushIOBuffer();

    // Delete our IO buffer.
    fileSystem->MemFree( this->internalIOBuffer );

    if ( terminateUnderlyingData == true )
    {
        delete underlyingStream;
    }

    this->underlyingStream = nullptr;
}

bool CBufferedStreamWrap::UpdateIOBufferPosition( fsOffsetNumber_t reqBufPos )
{
    if ( reqBufPos != this->bufOffset )
    {
        // Write any data to disk that is pending.
        // This will clear the validity buffer.
        this->FlushIOBuffer();

        // Clear the validity buffer since we have new/unknown bytes.
        this->internalIOValidity.Clear();

        // Need to reposition our buffer.
        this->bufOffset = reqBufPos;

        return true;
    }

    return false;
}

size_t CBufferedStreamWrap::Read( void *buffer, size_t readCount )
{
    // TODO: if data is read from an invalid buffer region while data was previously written
    //  past that invalid region then the read will fail in the current implementation, but
    //  honestly we should flush the buffer then, so the stream implementation will decide
    //  what to fill it with.

    // If we are not opened for reading rights, this operation should not do anything.
    if ( !IsReadable() )
        return 0;

    fsOffsetNumber_t fsRealReadCount = SeekPointerUtil::RegressToSeekType <fsOffsetNumber_t> ( readCount );

    if ( fsRealReadCount == 0 )
        return 0;

    // Calculate the buffer position of the current seek position.
    fsOffsetNumber_t beginFileSeek = this->fileSeek;

    size_t ioBufSize = this->internalIOBufferSize;

    chunked_iterator <fsOffsetNumber_t> chunk_iter( beginFileSeek, beginFileSeek + fsRealReadCount, (fsOffsetNumber_t)ioBufSize );

    CFile *underlyingStream = this->underlyingStream;

    void *ioBuf = this->internalIOBuffer;

    size_t totalBytesRead = 0;

    // Has not bursted yet.
    bool firstBurst = true;

    while ( !chunk_iter.IsEnd() )
    {
        fsOffsetNumber_t curFileSeek = chunk_iter.GetCurrentOffset();
        fsOffsetNumber_t fsBufPos = chunk_iter.GetCurrentChunkOffset();
        fsOffsetNumber_t fsBufSize = chunk_iter.GetCurrentChunkEndCount();

        // Adjust the buffer.
        fsOffsetNumber_t reqBufPos = ( curFileSeek - fsBufPos );

        bool changedIOBufPos = this->UpdateIOBufferPosition( reqBufPos );

        size_t completeBufPos = (size_t)fsBufPos;
        size_t completeBufSize = (size_t)fsBufSize;

        // If the validity buffer is empty and we switched buffer positions, then attempt
        //  to burst read the buffer to the max, so we prepare for future reads.
        if ( changedIOBufPos || ( firstBurst && this->internalIOValidity.IsEmpty() ) )
        {
            void *ioBufPtr = ( (char*)ioBuf + completeBufPos );

            int couldSeek = underlyingStream->SeekNative( curFileSeek, SEEK_SET );

            if ( couldSeek == 0 )
            {
                size_t realChunkEndCount = ( ioBufSize - completeBufPos );

                size_t burstReadCount = underlyingStream->Read( ioBufPtr, realChunkEndCount );

                // Add it to validity.
                this->internalIOValidity.Insert( { completeBufPos, burstReadCount }, ioBufDataState::COMMITTED );

                firstBurst = false;
            }
        }

        // If we have data inside our user buffer, we return that.
        // Otherwise we fetch data and remember it as committed.
        RwList <ioBufRegionMetaData> commit_list;

        try
        {
            this->internalIOValidity.ScanSharedSlices( { completeBufPos, completeBufSize },
                [&]( const bufSlice_t& slice, const ioBufRegionMetaData *data )
            {
                size_t bufPos = slice.GetSliceStartPoint();
                size_t bufSize = slice.GetSliceSize();

                void *bufPtr = ( (char*)ioBuf + bufPos );

                size_t actualReadCount = 0;

                if ( data != nullptr )
                {
                    // Just read from our user buffer.
                    actualReadCount = bufSize;
                }
                else
                {
                    // Read from the file.
                    underlyingStream->SeekNative( curFileSeek, SEEK_SET );

                    actualReadCount = underlyingStream->Read( bufPtr, bufSize );

                    // If any reading error occured, we bail.
                    if ( actualReadCount == 0 )
                    {
                        throw eir_exception();
                    }

                    // Remember to mark this region as COMMITTED.
                    ioBufRegionMetaData *new_data = this->internalIOValidity.PrepareDecommittedNode( slice, ioBufDataState::COMMITTED );

                    LIST_APPEND( commit_list.root, new_data->node );
                }

                // Put it into their user buffer.
                memcpy( (char*)buffer + totalBytesRead, bufPtr, actualReadCount );

                // Increment the count of read bytes.
                totalBytesRead += actualReadCount;

                // If we did not read everything, then quit.
                if ( actualReadCount != bufSize )
                {
                    throw eir_exception();
                }
            }, true );
        }
        catch( ... )
        {
            // Something went wrong so clean-up.
            while ( LIST_EMPTY( commit_list.root ) == false )
            {
                ioBufRegionMetaData *data = LIST_GETITEM( ioBufRegionMetaData, commit_list.root.next, node );

                LIST_REMOVE( data->node );

                this->internalIOValidity.DeleteDecommittedNode( data );
            }

            goto endOfReading;
        }

        // Commit all nodes (nothing can go wrong because atomic).
        while ( LIST_EMPTY( commit_list.root ) == false )
        {
            ioBufRegionMetaData *data = LIST_GETITEM( ioBufRegionMetaData, commit_list.root.next, node );

            LIST_REMOVE( data->node );

            this->internalIOValidity.RecommitData( data, ioBufDataState::COMMITTED );
        }

        // Next chunk.
        chunk_iter.Increment();
    }

endOfReading:
    // Update our seek.
    this->fileSeek += totalBytesRead;

    return totalBytesRead;
}

size_t CBufferedStreamWrap::Write( const void *buffer, size_t writeCount )
{
    // If we are not opened for writing rights, this operation should not do anything.
    if ( !IsWriteable() )
        return 0;

    fsOffsetNumber_t realWriteCount = SeekPointerUtil::RegressToSeekType <fsOffsetNumber_t> ( writeCount );

    if ( realWriteCount == 0 )
        return 0;

    fsOffsetNumber_t beginFileSeek = this->fileSeek;

    chunked_iterator <fsOffsetNumber_t> chunk_iter( beginFileSeek, beginFileSeek + realWriteCount, (fsOffsetNumber_t)this->internalIOBufferSize );

    //CFile *underlyingStream = this->underlyingStream;

    void *ioBuf = this->internalIOBuffer;

    size_t totalWriteCount = 0;

    while ( !chunk_iter.IsEnd() )
    {
        fsOffsetNumber_t curFileSeek = chunk_iter.GetCurrentOffset();
        fsOffsetNumber_t fsBufPos = chunk_iter.GetCurrentChunkOffset();
        fsOffsetNumber_t fsBufSize = chunk_iter.GetCurrentChunkEndCount();

        // Adjust the buffer.
        fsOffsetNumber_t reqBufPos = ( curFileSeek - fsBufPos );

        this->UpdateIOBufferPosition( reqBufPos );

        // Write to our internal buffer (we put to file at a later date, lazily).
        size_t bufPos = (size_t)fsBufPos;
        size_t bufSize = (size_t)fsBufSize;

        const char *userBufPtr = ( (const char*)buffer + totalWriteCount );
        char *ioBufPtr = ( (char*)ioBuf + bufPos );

        memcpy( ioBufPtr, userBufPtr, bufSize );

        // Mark the validity.
        this->internalIOValidity.Insert( { bufPos, bufSize }, ioBufDataState::DIRTY );

        // Increment the write count.
        totalWriteCount += bufSize;

        chunk_iter.Increment();
    }

    // Update our seek.
    this->fileSeek += totalWriteCount;

    return totalWriteCount;
}

int CBufferedStreamWrap::Seek( long iOffset, int iType )
{
    // Update the seek with a normal number.
    // This is a faster method than SeekNative.
    fsOffsetNumber_t offsetBase = 0;

    if ( iType == SEEK_CUR )
    {
        offsetBase = this->fileSeek;
    }
    else if ( iType == SEEK_SET )
    {
        offsetBase = 0;
    }
    else if ( iType == SEEK_END )
    {
        offsetBase = underlyingStream->GetSize();
    }
    else
    {
        return -1;
    }

    this->fileSeek = (long)( offsetBase + iOffset );

    // We update the buffer on demand.

    return 0;
}

int CBufferedStreamWrap::SeekNative( fsOffsetNumber_t iOffset, int iType )
{
    // Update the seek with a bigger number.
    fsOffsetNumber_t offsetBase = 0;

    if ( iType == SEEK_CUR )
    {
        offsetBase = this->fileSeek;
    }
    else if ( iType == SEEK_SET )
    {
        offsetBase = 0;
    }
    else if ( iType == SEEK_END )
    {
        offsetBase = (seekType_t)underlyingStream->GetSizeNative();
    }
    else
    {
        return -1;
    }

    this->fileSeek = (fsOffsetNumber_t)( offsetBase + iOffset );

    return 0;
}

long CBufferedStreamWrap::Tell( void ) const noexcept
{
    return (long)this->fileSeek;
}

fsOffsetNumber_t CBufferedStreamWrap::TellNative( void ) const noexcept
{
    return (fsOffsetNumber_t)this->fileSeek;
}

bool CBufferedStreamWrap::IsEOF( void ) const noexcept
{
    this->FlushIOBuffer();

    // Update the underlying stream's seek ptr and see if it finished.
    CFile *underlyingStream = this->underlyingStream;

    underlyingStream->SeekNative( this->fileSeek, SEEK_SET );

    return underlyingStream->IsEOF();
}

bool CBufferedStreamWrap::QueryStats( filesysStats& statsOut ) const noexcept
{
    this->FlushIOBuffer();

    // Redirect this functionality to the underlying stream.
    // We are not supposed to modify any of these logical attributes.
    return underlyingStream->QueryStats( statsOut );
}

void CBufferedStreamWrap::SetFileTimes( time_t atime, time_t ctime, time_t mtime )
{
    // Attempt to modify the stream's meta data.
    underlyingStream->SetFileTimes( atime, ctime, mtime );
}

void CBufferedStreamWrap::SetSeekEnd( void )
{
    // Finishes the stream at the given offset.
    CFile *underlyingStream = this->underlyingStream;

    fsOffsetNumber_t fileSeek = this->fileSeek;

    underlyingStream->SeekNative( fileSeek, SEEK_SET );
    underlyingStream->SetSeekEnd();

    // Invalidate buffer contents past the seek pointer so they do not get written.
    fsOffsetNumber_t bufOffset = this->bufOffset;
    size_t ioBufSize = this->internalIOBufferSize;

    if ( fileSeek < bufOffset + (fsOffsetNumber_t)ioBufSize )
    {
        if ( fileSeek < bufOffset )
        {
            this->internalIOValidity.Clear();
        }
        else
        {
            size_t fileSpaceBufOff = (size_t)( fileSeek - bufOffset );

            this->internalIOValidity.Remove( { fileSpaceBufOff, ioBufSize - fileSpaceBufOff } );
        }
    }
}

size_t CBufferedStreamWrap::GetSize( void ) const noexcept
{
    // Must flush our buffer to get a proper file-size.
    this->FlushIOBuffer();

    return underlyingStream->GetSize();
}

fsOffsetNumber_t CBufferedStreamWrap::GetSizeNative( void ) const noexcept
{
    this->FlushIOBuffer();

    return underlyingStream->GetSizeNative();
}

void CBufferedStreamWrap::FlushIOBuffer( void ) const
{
    // Get the contents of our buffer onto disk space (if required).
    if ( this->internalIOValidity.IsEmpty() == false )
    {
        const void *ioBuf = this->internalIOBuffer;

        fsOffsetNumber_t ioBufOffset = this->bufOffset;

        RwList <ioBufRegionMetaData> refresh_list;

        for ( const ioBufRegionMetaData& data : this->internalIOValidity )
        {
            // We only care about dirty data.
            if ( data.GetDataState() != ioBufDataState::DIRTY )
                continue;

            bufSlice_t slice = data.GetNodeSlice();

            size_t curOff = slice.GetSliceStartPoint();
            size_t curSize = slice.GetSliceSize();

            underlyingStream->SeekNative( ioBufOffset + curOff, SEEK_SET );

            // We do not mind if anything fails to write.
            this->underlyingStream->Write( (const char*)ioBuf + curOff, curSize );

            // Remember this data pointer so that we flip it to COMITTED state
            //  at the end of this loop.
            LIST_APPEND( refresh_list.root, data.node );
        }

        // Actually flip all byte slices that we comitted into COMMITTED data state
        //  instead of wiping the entire validity buffer
        while ( LIST_EMPTY( refresh_list.root ) == false )
        {
            ioBufRegionMetaData *data = LIST_GETITEM( ioBufRegionMetaData, refresh_list.root.next, node );

            LIST_REMOVE( data->node );

            this->internalIOValidity.DetachData( data );

            data->state = ioBufDataState::COMMITTED;

            this->internalIOValidity.RecommitData( data, ioBufDataState::COMMITTED );
        }

        // Do not have to clear the validity buffer because every byte that we know is committed now.
    }
}

void CBufferedStreamWrap::Flush( void )
{
    // Write stuff to the disk.
    this->FlushIOBuffer();

    // Actually remove our validity because flush is a strong operation.
    this->internalIOValidity.Clear();

    // Write the remaining OS buffers.
    underlyingStream->Flush();
}

filePath CBufferedStreamWrap::GetPath( void ) const
{
    return underlyingStream->GetPath();
}

bool CBufferedStreamWrap::IsReadable( void ) const noexcept
{
    return underlyingStream->IsReadable();
}

bool CBufferedStreamWrap::IsWriteable( void ) const noexcept
{
    return underlyingStream->IsWriteable();
}

CFile* CFileSystem::WrapStreamBuffered( CFile *stream, bool deleteOnQuit )
{
    return new CBufferedStreamWrap( stream, deleteOnQuit );
}
