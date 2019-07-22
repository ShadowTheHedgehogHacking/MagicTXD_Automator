/* You can write ONE header per C BLOCk using these macros */
#define SKIP_HEADER()\
	uint32 bytesWritten = 0;\
	uint32 headerPos = rw.tellp();\
    rw.seekp(0x0C, std::ios::cur);

#define WRITE_HEADER(chunkType)\
	uint32 oldPos = rw.tellp();\
    rw.seekp(headerPos, std::ios::beg);\
	header.setType( (chunkType) );\
	header.setLength( bytesWritten );\
	bytesWritten += header.write(rw);\
	writtenBytesReturn = bytesWritten;\
    rw.seekp(oldPos, std::ios::beg);

template <typename allocatorType>
inline void writeStringIntoBufferSafe(
    rw::Interface *engineInterface,
    const eir::String <char, allocatorType>& theString,
    char *buf, size_t bufSize,
    const rw::rwString <char>& texName,
    const char *dbgName
)
{
    size_t nameLen = theString.GetLength();

    if (nameLen >= bufSize)
    {
        engineInterface->PushWarning( "texture " + texName + " has been written using truncated " + dbgName );

        nameLen = bufSize - 1;
    }

    memcpy( buf, theString.GetConstString(), nameLen );

    // Pad with zeroes (which will also zero-terminate).
    memset( buf + nameLen, 0, bufSize - nameLen );
}

inline rw::uint32 writePartialStreamSafe( rw::Stream *output, const void *srcData, rw::uint32 srcDataSize, rw::uint32 streamSize )
{
    rw::uint32 streamWriteCount = std::min(srcDataSize, streamSize);

    output->write( srcData, streamWriteCount );

    rw::uint32 writeCount = streamWriteCount;

    // Write the remainder, if required.
    if (srcDataSize < streamSize)
    {
        rw::uint32 leftDataSize = ( streamSize - srcDataSize );

        for ( rw::uint32 n = 0; n < leftDataSize; n++ )
        {
            rw::uint8 writeData = 0;

            output->write( &writeData, sizeof( writeData ) );
        }

        writeCount += leftDataSize;
    }

    return writeCount;
}

inline rw::uint32 writePartialBlockSafe( rw::BlockProvider& outputProvider, const void *srcData, rw::uint32 srcDataSize, rw::uint32 streamSize )
{
    rw::uint32 streamWriteCount = std::min(srcDataSize, streamSize);

    outputProvider.write( srcData, streamWriteCount );

    rw::uint32 writeCount = streamWriteCount;

    // Write the remainder, if required.
    if (srcDataSize < streamSize)
    {
        rw::uint32 leftDataSize = ( streamSize - srcDataSize );

        for ( rw::uint32 n = 0; n < leftDataSize; n++ )
        {
            outputProvider.writeUInt8( 0 );
        }

        writeCount += leftDataSize;
    }

    return writeCount;
}

namespace rw
{

struct HeaderInfo
{
    // Packed library version.
    struct PackedLibraryVersion_rev1
    {
        endian::little_endian <uint32> packedVer;
    };

    struct PackedLibraryVersion_rev2
    {
        endian::little_endian <uint16> buildNumber;
        endian::little_endian <uint16> packedVer;
    };

    struct PackedLibraryVersion
    {
        // Can be rev1 or rev2.
        uint32 _version;

        inline PackedLibraryVersion_rev1& GetRevision1( void )
        {
            return *(PackedLibraryVersion_rev1*)&this->_version;
        }

        inline PackedLibraryVersion_rev2& GetRevision2( void )
        {
            return *(PackedLibraryVersion_rev2*)&this->_version;
        }

        inline bool isNewStyle( void )
        {
            return ( GetRevision2().packedVer != 0 );
        }
    };

private:
	uint32 type;
	uint32 length;

    PackedLibraryVersion packedVersion;

public:
	void read(std::istream &rw);
	uint32 write(std::ostream &rw);

    void setVersion(const LibraryVersion& version);
    LibraryVersion getVersion(void) const;

    void setType(uint32 type)               { this->type = type; }
    uint32 getType(void) const              { return this->type; }

    void setLength(uint32 length)           { this->length = length; }
    uint32 getLength(void) const            { return this->length; }
};

void ChunkNotFound(CHUNK_TYPE chunk, uint32 address);
uint32 writeInt8(int8 tmp, std::ostream &rw);
uint32 writeUInt8(uint8 tmp, std::ostream &rw);
uint32 writeInt16(int16 tmp, std::ostream &rw);
uint32 writeUInt16(uint16 tmp, std::ostream &rw);
uint32 writeInt32(int32 tmp, std::ostream &rw);
uint32 writeUInt32(uint32 tmp, std::ostream &rw);
uint32 writeFloat32(float32 tmp, std::ostream &rw);
int8 readInt8(std::istream &rw);
uint8 readUInt8(std::istream &rw);
int16 readInt16(std::istream &rw);
uint16 readUInt16(std::istream &rw);
int32 readInt32(std::istream &rw);
uint32 readUInt32(std::istream &rw);
float32 readFloat32(std::istream &rw);

std::string getChunkName(uint32 i);

// Helper functions.
inline void checkAhead( Stream *stream, int64 count )
{
    // Check whether we have those bytes.
    int64 curPos = stream->tell();
    int64 streamSize = stream->size();

    int64 availableBytes = ( streamSize - curPos );

    if ( availableBytes < count )
    {
        throw RwException( "stream does not have required bytes" );
    }
}

inline void putc_stream( rw::Stream *theStream, char val, size_t count )
{
    for ( size_t n = 0; n < count; n++ )
    {
        theStream->write( &val, 1 );
    }
}

inline void skipAvailable( Stream *stream, int64 skipCount )
{
    // Check availability.
    checkAhead( stream, skipCount );

    // We are okay. Just skip ahead.
    stream->skip( skipCount );
}

}