/*
    RenderWare block serialization helpers.
*/

namespace rw
{

enum eBlockMode
{
    RWBLOCKMODE_WRITE,
    RWBLOCKMODE_READ
};

struct RwBlockException : public RwException
{
    inline RwBlockException( const char *msg ) : RwException( msg )
    {
        return;
    }
};

struct BlockProvider
{
    typedef sliceOfData <int64> streamMemSlice_t;

    BlockProvider( Stream *contextStream, rw::eBlockMode blockMode );
    BlockProvider( Stream *contextStream, rw::eBlockMode blockMode, bool ignoreBlockRegions );

    inline BlockProvider( BlockProvider *parentProvider )
    {
        this->parent = parentProvider;
        this->blockMode = parentProvider->blockMode;
        this->isInContext = false;
        this->contextStream = nullptr;
        this->ignoreBlockRegions = parentProvider->ignoreBlockRegions;
    }

    inline BlockProvider( BlockProvider *parentProvider, bool ignoreBlockRegions )
    {
        this->parent = parentProvider;
        this->blockMode = parentProvider->blockMode;
        this->isInContext = false;
        this->contextStream = nullptr;
        this->ignoreBlockRegions = ignoreBlockRegions;
    }

    BlockProvider( const BlockProvider& right ) = delete;

    inline ~BlockProvider( void )
    {
        assert( this->isInContext == false );
    }

protected:
    BlockProvider *parent;

    eBlockMode blockMode;
    bool isInContext;

    Stream *contextStream;

    bool ignoreBlockRegions;

    // Processing context of this stream.
    // This is stored for important points.
    struct Context
    {
        inline Context( void )
        {
            this->chunk_id = 0;
            this->chunk_beg_offset = -1;
            this->chunk_beg_offset_absolute = -1;
            this->chunk_length = -1;

            this->context_seek = -1;
        }

        uint32 chunk_id;
        int64 chunk_beg_offset;
        int64 chunk_beg_offset_absolute;
        int64 chunk_length;

        int64 context_seek;
        
        LibraryVersion chunk_version;
    };

    Context blockContext;

public:
    // Entering a block (i.e. parsing the header and setting limits).
    void EnterContext( void );
    void LeaveContext( void ) noexcept;

    inline bool inContext( void ) const
    {
        return this->isInContext;
    }
    
    // Block modification API.
    void read( void *out_buf, size_t readCount );
    void write( const void *in_buf, size_t writeCount );

    void skip( size_t skipCount );
    int64 tell( void ) const;
    int64 tell_absolute( void ) const;

    void seek( int64 pos, eSeekMode mode );

    // Public verification API.
    void check_read_ahead( size_t readCount ) const;

protected:
    // Special helper algorithms.
    void read_native( void *out_buf, size_t readCount );
    void write_native( const void *in_buf, size_t writeCount );

    void skip_native( size_t skipCount );

    void seek_native( int64 pos, eSeekMode mode );
    int64 tell_native( void ) const;
    int64 tell_absolute_native( void ) const;

    Interface* getEngineInterface( void ) const;

public:
    // Block meta-data API.
    uint32 getBlockID( void ) const;
    int64 getBlockLength( void ) const;
    LibraryVersion getBlockVersion( void ) const;

    void setBlockID( uint32 id );
    void setBlockVersion( LibraryVersion version );

    inline bool doesIgnoreBlockRegions( void ) const
    {
        return this->ignoreBlockRegions;
    }

    inline bool hasParent( void ) const
    {
        return ( this->parent != nullptr );
    }

    // Helper functions.
    template <typename structType>
    inline void writeStruct( const structType& theStruct )      { this->write( &theStruct, sizeof( theStruct ) ); }

    inline void writeUInt8( endian::little_endian <uint8> val )     { this->writeStruct( val ); }
    inline void writeUInt16( endian::little_endian <uint16> val )   { this->writeStruct( val ); }
    inline void writeUInt32( endian::little_endian <uint32> val )   { this->writeStruct( val ); }
    inline void writeUInt64( endian::little_endian <uint64> val )   { this->writeStruct( val ); }

    inline void writeInt8( endian::little_endian <int8> val )       { this->writeStruct( val );}
    inline void writeInt16( endian::little_endian <int16> val )     { this->writeStruct( val ); }
    inline void writeInt32( endian::little_endian <int32> val )     { this->writeStruct( val ); }
    inline void writeInt64( endian::little_endian <int64> val )     { this->writeStruct( val ); }

    template <typename structType>
    inline void readStruct( structType& outStruct )                 { this->read( &outStruct, sizeof( outStruct ) ); }

    inline endian::little_endian <uint8> readUInt8( void )          { endian::little_endian <uint8> val; this->readStruct( val ); return val; }
    inline endian::little_endian <uint16> readUInt16( void )        { endian::little_endian <uint16> val; this->readStruct( val ); return val; }
    inline endian::little_endian <uint32> readUInt32( void )        { endian::little_endian <uint32> val; this->readStruct( val ); return val; }
    inline endian::little_endian <uint64> readUInt64( void )        { endian::little_endian <uint64> val; this->readStruct( val ); return val; }

    inline endian::little_endian <int8> readInt8( void )            { endian::little_endian <int8> val; this->readStruct( val ); return val; }
    inline endian::little_endian <int16> readInt16( void )          { endian::little_endian <int16> val; this->readStruct( val ); return val; }
    inline endian::little_endian <int32> readInt32( void )          { endian::little_endian <int32> val; this->readStruct( val ); return val; }
    inline endian::little_endian <int64> readInt64( void )          { endian::little_endian <int64> val; this->readStruct( val ); return val; }

protected:
    // Validation API.
    void verifyLocalStreamAccess( const streamMemSlice_t& requestedMemory ) const;
    void verifyStreamAccess( const streamMemSlice_t& requestedMemory ) const;
};

} // namespace rw