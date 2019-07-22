// RenderWare Raster object.
#include "StdInc.h"

#include "txdread.raster.hxx"

namespace rw
{

/*
 * Native Texture
 */

uint32 GetNativeTextureMipmapCount( Interface *engineInterface, PlatformTexture *nativeTexture, texNativeTypeProvider *texTypeProvider )
{
    nativeTextureBatchedInfo info;

    texTypeProvider->GetTextureInfo( engineInterface, nativeTexture, info );

    return info.mipmapCount;
}

/*
 * Raster
 */

rwMainRasterEnv_t::pluginRegister_t rwMainRasterEnv_t::pluginRegister;

Raster* CreateRaster( Interface *intf )
{
    EngineInterface *engineInterface = (EngineInterface*)intf;

    rwMainRasterEnv_t *rasterEnv = rwMainRasterEnv_t::pluginRegister.GetPluginStruct( engineInterface );

    if ( rasterEnv )
    {
        RwTypeSystem::typeInfoBase *rasterTypeInfo = rasterEnv->handler.GetType();

        if ( rasterTypeInfo )
        {
            GenericRTTI *rtObj = engineInterface->typeSystem.Construct( engineInterface, rasterTypeInfo, nullptr );

            if ( rtObj )
            {
                Raster *theRaster = (Raster*)RwTypeSystem::GetObjectFromTypeStruct( rtObj );

                return theRaster;
            }
        }
        else
        {
            engineInterface->PushWarning( "no raster type info present in CreateRaster" );
        }
    }
    else
    {
        engineInterface->PushWarning( "no raster environment present in CreateRaster" );
    }

    return nullptr;
}

Raster* CloneRaster( const Raster *rasterToClone )
{
    EngineInterface *engineInterface = (EngineInterface*)rasterToClone->engineInterface;

    // We can clone generically.
    rw::Raster *newRaster = nullptr;

    const GenericRTTI *srcRtObj = RwTypeSystem::GetTypeStructFromConstObject( rasterToClone );

    if ( srcRtObj )
    {
        GenericRTTI *clonedRtObj = engineInterface->typeSystem.Clone( engineInterface, srcRtObj );

        if ( clonedRtObj )
        {
            newRaster = (rw::Raster*)RwTypeSystem::GetObjectFromTypeStruct( clonedRtObj );
        }
    }

    return newRaster;
}

Raster* AcquireRaster( Raster *theRaster )
{
    // Attempts to get a handle to this raster by referencing it.
    // This function could fail if the resource has reached its maximum refcount.

    Raster *returnObj = nullptr;

    if ( theRaster )
    {
        // TODO: implement ref count overflow security check.

        theRaster->refCount++;

        returnObj = theRaster;
    }

    return returnObj;
}

void DeleteRaster( Raster *theRaster )
{
    EngineInterface *engineInterface = (EngineInterface*)theRaster->engineInterface;

    // We use reference counting on rasters.
    theRaster->refCount--;

    if ( theRaster->refCount == 0 )
    {
        // Just delete it.
        GenericRTTI *rtObj = engineInterface->typeSystem.GetTypeStructFromAbstractObject( theRaster );

        if ( rtObj )
        {
            engineInterface->typeSystem.Destroy( engineInterface, rtObj );
        }
        else
        {
            engineInterface->PushWarning( "invalid raster object pushed to DeleteRaster" );
        }
    }
}

Raster::Raster( const Raster& right )
{
    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( &right ) );

    // Copy raster specifics.
    this->engineInterface = right.engineInterface;

    // Copy native platform data.
    PlatformTexture *platformTex = nullptr;

    if ( right.platformData )
    {
        platformTex = CloneNativeTexture( this->engineInterface, right.platformData );
    }

    this->platformData = platformTex;

    // Cloned rasters are stand-alone. Thus we reset reference counts to default.
    this->refCount = 1;
    this->constRefCount = 0;
}

Raster::~Raster( void )
{
    // We want to have nobody use us when we destroy ourselves.
    assert( this->refCount == 0 );
    assert( this->constRefCount == 0 );

    // Delete the platform data, if available.
    if ( PlatformTexture *platformTex = this->platformData )
    {
        DeleteNativeTexture( this->engineInterface, platformTex );

        this->platformData = nullptr;
    }
}

// Most important raster plugin, the threading consistency.
rasterConsistencyRegister_t rasterConsistencyRegister;

void registerRasterConsistency( void )
{
    rasterConsistencyRegister.RegisterPlugin( engineFactory );
}

void Raster::SetEngineVersion( LibraryVersion version )
{
    scoped_rwlock_writer <rwlock> rasterConsistency( GetRasterLock( this ) );

    // Make sure we are mutable.
    NativeCheckRasterMutable( this );

    PlatformTexture *platformTex = this->platformData;

    if ( !platformTex )
    {
        throw RwException( "no native data" );
    }

    Interface *engineInterface = this->engineInterface;

    texNativeTypeProvider *texProvider = GetNativeTextureTypeProvider( engineInterface, platformTex );

    if ( !texProvider )
    {
        throw RwException( "invalid native data" );
    }

    texProvider->SetTextureVersion( engineInterface, platformTex, version );
}

LibraryVersion Raster::GetEngineVersion( void ) const
{
    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( this ) );

    PlatformTexture *platformTex = this->platformData;

    if ( !platformTex )
    {
        throw RwException( "no native data" );
    }

    Interface *engineInterface = this->engineInterface;

    texNativeTypeProvider *texProvider = GetNativeTextureTypeProvider( engineInterface, platformTex );

    if ( !texProvider )
    {
        throw RwException( "invalid native data" );
    }

    return texProvider->GetTextureVersion( platformTex );
}

void Raster::newNativeData( const char *typeName )
{
    scoped_rwlock_writer <rwlock> rasterConsistency( GetRasterLock( this ) );

    // Make sure we are mutable.
    NativeCheckRasterMutable( this );

    if ( this->platformData != nullptr )
        return;

    Interface *engineInterface = this->engineInterface;

    RwTypeSystem::typeInfoBase *nativeTypeInfo = GetNativeTextureType( engineInterface, typeName );

    if ( nativeTypeInfo )
    {
        // Create a new native data.
        PlatformTexture *nativeTex = CreateNativeTexture( engineInterface, nativeTypeInfo );

        // Store stuff.
        this->platformData = nativeTex;
    }
}

void Raster::clearNativeData( void )
{
    scoped_rwlock_writer <rwlock> rasterConsistency( GetRasterLock( this ) );

    // Make sure we are mutable.
    NativeCheckRasterMutable( this );

    PlatformTexture *platformTex = this->platformData;

    if ( platformTex == nullptr )
        return;

    Interface *engineInterface = this->engineInterface;

    DeleteNativeTexture( engineInterface, platformTex );

    // We have no more native data.
    this->platformData = nullptr;
}

bool Raster::hasNativeDataOfType( const char *typeName ) const
{
    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( this ) );

    PlatformTexture *platformTex = this->platformData;

    if ( !platformTex )
        return false;

    //Interface *engineInterface = this->engineInterface;

    GenericRTTI *rtObj = RwTypeSystem::GetTypeStructFromObject( platformTex );

    RwTypeSystem::typeInfoBase *typeInfo = RwTypeSystem::GetTypeInfoFromTypeStruct( rtObj );

    return ( strcmp( typeInfo->name, typeName ) == 0 );
}

const char* Raster::getNativeDataTypeName( void ) const
{
    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( this ) );

    PlatformTexture *platformTex = this->platformData;

    if ( !platformTex )
        return nullptr;

    //Interface *engineInterface = this->engineInterface;

    GenericRTTI *rtObj = RwTypeSystem::GetTypeStructFromObject( platformTex );

    RwTypeSystem::typeInfoBase *typeInfo = RwTypeSystem::GetTypeInfoFromTypeStruct( rtObj );

    return ( typeInfo->name );
}

// Constant reference management.
void Raster::addConstRef( void )
{
    // WHY WE CAN TAKE A reader-lock INSTEAD OF A writer-lock :
    //  The reader-lock ensures that no writer-activity is running when doing the immutability flag.
    //  Reader-activity does not harm the runtime, because it is immutable anyway.
    //  When using a reader-lock, we now have a sense for constRefCount being an atomic variable!

    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( this ) );

    // When the raster has a const ref count != 0, then it is classified as immutable.
    // Immutable rasters cannot be modified in any way.
    // The runtime will use this feature when it wants to use image data asynchronously
    // or across function calls.

    this->constRefCount++;
}

void Raster::remConstRef( void )
{
    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( this ) );

    if ( this->constRefCount == 0 )
    {
        // Make sure the user does not do anything stupid.
        throw RwException( "attempt to decrease constant ref count of Raster while it is not const referenced" );
    }

    this->constRefCount--;
}

bool Raster::isImmutable( void ) const
{
    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( this ) );

    return NativeIsRasterImmutable( this );
}

void* Raster::getNativeInterface( void )
{
    // The native interface offers a direct way of access to the native texture.
    // Those are to be used with extreme caution, because security measures of the Raster object are disabled.
    // Be careful.

    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( this ) );

    PlatformTexture *platformTex = this->platformData;

    if ( !platformTex )
        return nullptr;

    Interface *engineInterface = this->engineInterface;

    texNativeTypeProvider *texProvider = GetNativeTextureTypeProvider( engineInterface, platformTex );

    if ( !texProvider )
        return nullptr;

    return texProvider->GetNativeInterface( platformTex );
}

void* Raster::getDriverNativeInterface( void )
{
    scoped_rwlock_reader <rwlock> rasterConsistency( GetRasterLock( this ) );

    PlatformTexture *platformTex = this->platformData;

    if ( !platformTex )
        return nullptr;

    Interface *engineInterface = this->engineInterface;

    const texNativeTypeProvider *texProvider = GetNativeTextureTypeProvider( engineInterface, platformTex );

    if ( !texProvider )
        return nullptr;

    return texProvider->GetDriverNativeInterface();
}

// Initializator for TXD plugins, as it cannot be done statically.
#ifdef RWLIB_INCLUDE_NATIVETEX_ATC_MOBILE
extern void registerATCNativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_ATC_MOBILE
#ifdef RWLIB_INCLUDE_NATIVETEX_D3D8
extern void registerD3D8NativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_D3D8
#ifdef RWLIB_INCLUDE_NATIVETEX_D3D9
extern void registerD3D9NativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_D3D9
#ifdef RWLIB_INCLUDE_NATIVETEX_S3TC_MOBILE
extern void registerMobileDXTNativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_S3TC_MOBILE
#ifdef RWLIB_INCLUDE_NATIVETEX_PLAYSTATION2
extern void registerPS2NativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_PLAYSTATION2
#ifdef RWLIB_INCLUDE_NATIVETEX_POWERVR_MOBILE
extern void registerPVRNativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_POWERVR_MOBILE
#ifdef RWLIB_INCLUDE_NATIVETEX_UNC_MOBILE
extern void registerMobileUNCNativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_UNC_MOBILE
#ifdef RWLIB_INCLUDE_NATIVETEX_XBOX
extern void registerXBOXNativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_XBOX
#ifdef RWLIB_INCLUDE_NATIVETEX_GAMECUBE
extern void registerGCNativePlugin( void );
#endif //RWLIB_INCLUDE_NATIVETEX_GAMECUBE
#ifdef RWLIB_INCLUDE_NATIVETEX_PSP
extern void registerPSPNativeTextureType( void );
#endif //RWLIB_INCLUDE_NATIVETEX_PSP

void registerNativeTexturePlugins( void )
{
    // Register the raster environment.
    // We set it up as plugin to take away the big gunk from the master header.
    rwMainRasterEnv_t::pluginRegister.RegisterPlugin( engineFactory );

    // Optional plugins.
    registerRasterConsistency();

    // First get the main raster serialization into the system.
    nativeTextureStreamStore.RegisterPlugin( engineFactory );

    // Now register sub module plugins.
#ifdef RWLIB_INCLUDE_NATIVETEX_ATC_MOBILE
    registerATCNativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_ATC_MOBILE
#ifdef RWLIB_INCLUDE_NATIVETEX_D3D8
    registerD3D8NativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_D3D8
#ifdef RWLIB_INCLUDE_NATIVETEX_D3D9
    registerD3D9NativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_D3D9
#ifdef RWLIB_INCLUDE_NATIVETEX_S3TC_MOBILE
    registerMobileDXTNativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_S3TC_MOBILE
#ifdef RWLIB_INCLUDE_NATIVETEX_PLAYSTATION2
    registerPS2NativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_PLAYSTATION2
#ifdef RWLIB_INCLUDE_NATIVETEX_POWERVR_MOBILE
    registerPVRNativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_POWERVR_MOBILE
#ifdef RWLIB_INCLUDE_NATIVETEX_UNC_MOBILE
    registerMobileUNCNativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_UNC_MOBILE
#ifdef RWLIB_INCLUDE_NATIVETEX_XBOX
    registerXBOXNativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_XBOX
#ifdef RWLIB_INCLUDE_NATIVETEX_GAMECUBE
    registerGCNativePlugin();
#endif //RWLIB_INCLUDE_NATIVETEX_GAMECUBE
#ifdef RWLIB_INCLUDE_NATIVETEX_PSP
    registerPSPNativeTextureType();
#endif //RWLIB_INCLUDE_NATIVETEX_PSP
}

};
