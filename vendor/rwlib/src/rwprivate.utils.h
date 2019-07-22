// RenderWare private global include file containing misc utilities.
// Those should eventually be included in more specialized headers, provided they are worth it.

#ifndef _RENDERWARE_PRIVATE_INTERNAL_UTILITIES_
#define _RENDERWARE_PRIVATE_INTERNAL_UTILITIES_

template <size_t byteCount, typename numberType>
struct bytewiseMoveItem
{
    static_assert( sizeof( numberType ) >= byteCount, "invalid number size for bytewiseMoveItem!" );

    AINLINE bytewiseMoveItem( void ) : data()
    {
        return;
    }

    AINLINE void read( const void *srcArrayData, size_t targetArrayIndex, numberType& valueOut )
    {
        const void *srcPtr = ( (const char*)srcArrayData + byteCount * targetArrayIndex );

        if ( sizeof( numberType ) > byteCount )
        {
            this->value = 0;
        }

        memcpy( this->data, srcPtr, byteCount );

        valueOut = this->value;
    }

    AINLINE void write( void *dstArrayData, size_t targetArrayIndex, numberType value )
    {
        this->value = value;

        void *dstPtr = ( (char*)dstArrayData + byteCount * targetArrayIndex );

        memcpy( dstPtr, this->data, byteCount );
    }

private:
    union
    {
        char data[ byteCount ];
        endian::little_endian <numberType> value;
    };
};

template <size_t curByteIndex, template <size_t> class callbackType>
struct byteDepth_iterator
{
    template <typename... Args>
    AINLINE static bool fetch( Args... theArgs )
    {
        bool gotResult =
            byteDepth_iterator <curByteIndex - 1, callbackType>::fetch( theArgs... );

        if ( gotResult )
        {
            return true;
        }

        return callbackType <curByteIndex>::call( theArgs... );
    }
};

template <template <size_t> class callbackType>
struct byteDepth_iterator <0, callbackType>
{
    template <typename... Args>
    AINLINE static bool fetch( Args... theArgs )
    {
        return false;
    }
};

template <size_t curByteIndex>
struct fetchDepth_callback
{
    template <typename numType>
    AINLINE static bool call( const void *srcArrayData, rw::uint32 depth, rw::uint32 targetArrayIndex, numType& valueOut )
    {
        if ( depth == curByteIndex * 8 )
        {
            bytewiseMoveItem <curByteIndex, numType>().read( srcArrayData, targetArrayIndex, valueOut );
            return true;
        }

        return false;
    }
};

template <size_t curByteIndex>
struct putDepth_callback
{
    template <typename numType>
    AINLINE static bool call( void *dstArrayData, rw::uint32 depth, rw::uint32 targetArrayIndex, numType value )
    {
        if ( depth == curByteIndex * 8 )
        {
            bytewiseMoveItem <curByteIndex, numType>().write( dstArrayData, targetArrayIndex, value );
            return true;
        }

        return false;
    }
};

template <typename numType>
AINLINE void getDataByDepth( const void *srcArrayData, rw::uint32 depth, rw::uint32 targetArrayIndex, rw::eByteAddressingMode byteAddr, numType& valueOut )
{
    using namespace rw;

    // Perform the texel get.
    if (depth == 4)
    {
        // Get the src item.
        PixelFormat::palette4bit::trav_t travItem;

        // Put the dst item.
        if ( byteAddr == eByteAddressingMode::MOST_SIGNIFICANT )
        {
            const PixelFormat::palette4bit *dstData = (const PixelFormat::palette4bit*)srcArrayData;

            dstData->getvalue(targetArrayIndex, travItem);
        }
        else if ( byteAddr == eByteAddressingMode::LEAST_SIGNIFICANT )
        {
            const PixelFormat::palette4bit_lsb *dstData = (const PixelFormat::palette4bit_lsb*)srcArrayData;

            dstData->getvalue(targetArrayIndex, travItem);
        }
        else
        {
            travItem = 0;
        }

        valueOut = travItem;
    }
    else if (byteDepth_iterator <sizeof( numType ), fetchDepth_callback>::fetch( srcArrayData, depth, targetArrayIndex, valueOut ))
    {
        // OK.
    }
    else
    {
        throw RwException( "unknown bit depth for getting" );
    }
}

template <typename numType>
AINLINE void setDataByDepth( void *dstArrayData, rw::uint32 depth, rw::uint32 targetArrayIndex, rw::eByteAddressingMode byteAddr, numType value )
{
    using namespace rw;

    // Perform the texel set.
    if (depth == 4)
    {
        // Get the src item.
        PixelFormat::palette4bit::trav_t travItem = (PixelFormat::palette4bit::trav_t)value;

        if ( byteAddr == eByteAddressingMode::MOST_SIGNIFICANT )
        {
            // Put the dst item.
            PixelFormat::palette4bit *dstData = (PixelFormat::palette4bit*)dstArrayData;

            dstData->setvalue(targetArrayIndex, travItem);
        }
        else if ( byteAddr == eByteAddressingMode::LEAST_SIGNIFICANT )
        {
            // Put the dst item.
            PixelFormat::palette4bit_lsb *dstData = (PixelFormat::palette4bit_lsb*)dstArrayData;

            dstData->setvalue(targetArrayIndex, travItem);
        }
    }
    else if (byteDepth_iterator <sizeof( numType ), putDepth_callback>::fetch( dstArrayData, depth, targetArrayIndex, value ))
    {
        // OK.
    }
    else
    {
        throw RwException( "unknown bit depth for setting" );
    }
}

AINLINE void moveDataByDepth( void *dstArrayData, const void *srcArrayData, rw::uint32 depth, rw::eByteAddressingMode addrMode, rw::uint32 targetArrayIndex, rw::uint32 srcArrayIndex )
{
    using namespace rw;

    // Perform the texel movement.
    if (depth == 4)
    {
        if ( addrMode == eByteAddressingMode::MOST_SIGNIFICANT )
        {
            // Get the src item.
            PixelFormat::palette4bit::trav_t travItem;

            PixelFormat::palette4bit *srcData = (PixelFormat::palette4bit*)srcArrayData;

            srcData->getvalue(srcArrayIndex, travItem);

            // Put the dst item.
            PixelFormat::palette4bit *dstData = (PixelFormat::palette4bit*)dstArrayData;

            dstData->setvalue(targetArrayIndex, travItem);
        }
        else if ( addrMode == eByteAddressingMode::LEAST_SIGNIFICANT )
        {
            // Get the src item.
            PixelFormat::palette4bit_lsb::trav_t travItem;

            PixelFormat::palette4bit_lsb *srcData = (PixelFormat::palette4bit_lsb*)srcArrayData;

            srcData->getvalue(srcArrayIndex, travItem);

            // Put the dst item.
            PixelFormat::palette4bit_lsb *dstData = (PixelFormat::palette4bit_lsb*)dstArrayData;

            dstData->setvalue(targetArrayIndex, travItem);
        }
    }
    else if (depth == 8)
    {
        // Get the src item.
        PixelFormat::palette8bit *srcData = (PixelFormat::palette8bit*)srcArrayData;

        PixelFormat::palette8bit::trav_t travItem;

        srcData->getvalue(srcArrayIndex, travItem);

        // Put the dst item.
        PixelFormat::palette8bit *dstData = (PixelFormat::palette8bit*)dstArrayData;

        dstData->setvalue(targetArrayIndex, travItem);
    }
    else if (depth == 16)
    {
        typedef PixelFormat::typedcolor <uint16> theColor;

        // Get the src item.
        theColor *srcData = (theColor*)srcArrayData;

        theColor::trav_t travItem;

        srcData->getvalue(srcArrayIndex, travItem);

        // Put the dst item.
        theColor *dstData = (theColor*)dstArrayData;

        dstData->setvalue(targetArrayIndex, travItem);
    }
    else if (depth == 24)
    {
        struct colorStruct
        {
            uint8 x, y, z;
        };

        typedef PixelFormat::typedcolor <colorStruct> theColor;

        // Get the src item.
        theColor *srcData = (theColor*)srcArrayData;

        theColor::trav_t travItem;

        srcData->getvalue(srcArrayIndex, travItem);

        // Put the dst item.
        theColor *dstData = (theColor*)dstArrayData;

        dstData->setvalue(targetArrayIndex, travItem);
    }
    else if (depth == 32)
    {
        typedef PixelFormat::typedcolor <uint32> theColor;

        // Get the src item.
        theColor *srcData = (theColor*)srcArrayData;

        theColor::trav_t travItem;

        srcData->getvalue(srcArrayIndex, travItem);

        // Put the dst item.
        theColor *dstData = (theColor*)dstArrayData;

        dstData->setvalue(targetArrayIndex, travItem);
    }
    else if (depth == 64)
    {
        typedef PixelFormat::typedcolor <uint64> theColor;

        // Get the src item.
        theColor *srcData = (theColor*)srcArrayData;

        theColor::trav_t travItem;

        srcData->getvalue(srcArrayIndex, travItem);

        // Put the dst item.
        theColor *dstData = (theColor*)dstArrayData;

        dstData->setvalue(targetArrayIndex, travItem);
    }
    else if (depth == 128)
    {
        struct sixteen_bytes
        {
            char data[16];
        };

        typedef PixelFormat::typedcolor <sixteen_bytes> theColor;

        // Get the src item.
        theColor *srcData = (theColor*)srcArrayData;

        theColor::trav_t travItem;

        srcData->getvalue(srcArrayIndex, travItem);

        // Put the dst item.
        theColor *dstData = (theColor*)dstArrayData;

        dstData->setvalue(targetArrayIndex, travItem);
    }
    else
    {
        throw RwException( "unknown bit depth for movement" );
    }
}

// RW specific stuff.
namespace rw
{

inline bool isRwObjectInheritingFrom( EngineInterface *engineInterface, const RwObject *rwObj, RwTypeSystem::typeInfoBase *baseType )
{
    const GenericRTTI *rtObj = engineInterface->typeSystem.GetTypeStructFromConstAbstractObject( rwObj );

    if ( rtObj )
    {
        // Check whether the type of the dynamic object matches the required type.
        RwTypeSystem::typeInfoBase *objTypeInfo = RwTypeSystem::GetTypeInfoFromTypeStruct( rtObj );

        if ( engineInterface->typeSystem.IsTypeInheritingFrom( baseType, objTypeInfo ) )
        {
            return true;
        }
    }

    return false;
}

}

#endif //_RENDERWARE_PRIVATE_INTERNAL_UTILITIES_
