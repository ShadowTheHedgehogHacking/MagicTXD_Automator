#ifndef _RENDERWARE_SERIALIZATION_PRIVATE_
#define _RENDERWARE_SERIALIZATION_PRIVATE_

namespace rw
{

enum eSerializationTypeMode
{
    RWSERIALIZE_INHERIT,
    RWSERIALIZE_ISOF
};

// Main chunk serialization interface.
// Allows you to store data in the RenderWare ecosystem, be officially registering it.
struct serializationProvider abstract
{
    inline serializationProvider( void )
    {
        this->managerData.isRegistered = false;
    }

    inline ~serializationProvider( void )
    {
        if ( this->managerData.isRegistered )
        {
            LIST_REMOVE( this->managerData.managerNode );
        }
    }

    // This interface is used to save the contents of the RenderWare engine to disk.
    virtual void            Serialize( Interface *engineInterface, BlockProvider& outputProvider, RwObject *objectToSerialize ) const = 0;
    virtual void            Deserialize( Interface *engineInterface, BlockProvider& inputProvider, RwObject *objectToDeserialize ) const = 0;

    // Do not access this data. It is off-limits for you.
    struct
    {
        RwListEntry <serializationProvider> managerNode;

        uint32 chunkID;
        eSerializationTypeMode mode;
        RwTypeSystem::typeInfoBase *rwType;

        bool isRegistered;
    } managerData;
};

// Serialization registration API.
bool RegisterSerialization( Interface *engineInterface, uint32 chunkID, RwTypeSystem::typeInfoBase *rwType, serializationProvider *serializer, eSerializationTypeMode mode );
bool UnregisterSerialization( Interface *engineInterface, uint32 chunkID, RwTypeSystem::typeInfoBase *rwType, serializationProvider *serializer );

};

#endif //_RENDERWARE_SERIALIZATION_PRIVATE_