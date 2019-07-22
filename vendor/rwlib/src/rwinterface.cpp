#include "StdInc.h"

#include "pluginutil.hxx"

#include <atomic>

#include "rwinterface.hxx"

#include "rwthreading.hxx"

namespace rw
{

// Factory for interfaces.
RwInterfaceFactory_t engineFactory;

RwObject::RwObject( Interface *engineInterface, void *construction_params )
{
    // Constructor that is called for creation.
    this->engineInterface = engineInterface;
    this->objVersion = engineInterface->GetVersion();   // when creating an object, we assign it the current version.
}

inline void SafeDeleteType( EngineInterface *engineInterface, RwTypeSystem::typeInfoBase*& theType )
{
    RwTypeSystem::typeInfoBase *theTypeVal = theType;

    if ( theTypeVal )
    {
        engineInterface->typeSystem.DeleteType( theTypeVal );

        theType = nullptr;
    }
}

struct rw_afterinit_t
{
    inline void Initialize( EngineInterface *engine )
    {
        engine->typeSystem.InitializeLockProvider();

        // Register the main RenderWare types.
        {
            engine->streamTypeInfo = engine->typeSystem.RegisterAbstractType <Stream> ( "stream" );
            engine->rwobjTypeInfo = engine->typeSystem.RegisterAbstractType <RwObject> ( "rwobj" );
            engine->textureTypeInfo = engine->typeSystem.RegisterStructType <TextureBase> ( "texture", engine->rwobjTypeInfo );
        }
    }

    inline void Shutdown( EngineInterface *engine )
    {
        // Unregister all types again.
        {
            SafeDeleteType( engine, engine->textureTypeInfo );
            SafeDeleteType( engine, engine->rwobjTypeInfo );
            SafeDeleteType( engine, engine->streamTypeInfo );
        }

        // We need to terminate the type system here.
        engine->typeSystem.Shutdown();
    }
};

EngineInterface::EngineInterface( void ) :
    applicationName( eir::constr_with_alloc::DEFAULT, this ),
    applicationVersion( eir::constr_with_alloc::DEFAULT, this ),
    applicationDescription( eir::constr_with_alloc::DEFAULT, this )
{
    // Set up the type system.
    this->typeSystem.lockProvider.engineInterface = this;

    // Remember to wait with type creation until the threading environment has registered!
    this->rwobjTypeInfo = nullptr;
    this->streamTypeInfo = nullptr;
    this->textureTypeInfo = nullptr;
}

EngineInterface::~EngineInterface( void )
{
    return;
}

rwLockProvider_t rwlockProvider;

void Interface::SetVersion( LibraryVersion version )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetVersion( version );
}

LibraryVersion Interface::GetVersion( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetVersion();
}

void Interface::SetApplicationInfo( const softwareMetaInfo& metaInfo )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    scoped_rwlock_writer <rwlock> lock( GetReadWriteLock( engineInterface ) );

    if ( const char *appName = metaInfo.applicationName )
    {
        engineInterface->applicationName = appName;
    }
    else
    {
        engineInterface->applicationName.Clear();
    }

    if ( const char *appVersion = metaInfo.applicationVersion )
    {
        engineInterface->applicationVersion = appVersion;
    }
    else
    {
        engineInterface->applicationVersion.Clear();
    }

    if ( const char *desc = metaInfo.description )
    {
        engineInterface->applicationDescription = desc;
    }
    else
    {
        engineInterface->applicationDescription.Clear();
    }
}

void Interface::SetMetaDataTagging( bool enabled )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetMetaDataTagging( enabled );
}

bool Interface::GetMetaDataTagging( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetMetaDataTagging();
}

rwStaticString <char> GetRunningSoftwareInformation( EngineInterface *engineInterface, bool outputShort )
{
    rwStaticString <char> infoOut;

    {
        scoped_rwlock_reader <rwlock> lock( GetReadWriteLock( engineInterface ) );

        const rwConfigBlock& cfgBlock = GetConstEnvironmentConfigBlock( engineInterface );

        // Only output anything if we enable meta data tagging.
        if ( cfgBlock.GetMetaDataTagging() )
        {
            // First put the software name.
            bool hasAppName = false;

            if ( engineInterface->applicationName.GetLength() != 0 )
            {
                infoOut += engineInterface->applicationName;

                hasAppName = true;
            }
            else
            {
                infoOut += "RenderWare (generic)";
            }

            infoOut += " [rwver: ";
            infoOut += engineInterface->GetVersion().toString();
            infoOut += "]";

            if ( hasAppName )
            {
                if ( engineInterface->applicationVersion.GetLength() != 0 )
                {
                    infoOut += " version: ";
                    infoOut += engineInterface->applicationVersion;
                }
            }

            if ( outputShort == false )
            {
                if ( engineInterface->applicationDescription.GetLength() != 0 )
                {
                    infoOut += " ";
                    infoOut += engineInterface->applicationDescription;
                }
            }
        }
    }

    return infoOut;
}

struct refCountPlugin
{
    std::atomic <unsigned long> refCount;

    inline void operator =( const refCountPlugin& right )
    {
        this->refCount.store( right.refCount );
    }

    inline void Initialize( GenericRTTI *obj )
    {
        this->refCount = 1; // we start off with one.
    }

    inline void Shutdown( GenericRTTI *obj )
    {
        // Has to be zeroed by the manager.
        assert( this->refCount == 0 );
    }

    inline void AddRef( void )
    {
        this->refCount++;
    }

    inline bool RemoveRef( void )
    {
        return ( this->refCount.fetch_sub( 1 ) == 1 );
    }
};

struct refCountManager
{
    inline void Initialize( EngineInterface *engineInterface )
    {
        this->pluginOffset =
            engineInterface->typeSystem.RegisterDependantStructPlugin <refCountPlugin> ( engineInterface->rwobjTypeInfo, RwTypeSystem::ANONYMOUS_PLUGIN_ID );
    }

    inline void Shutdown( EngineInterface *engineInterface )
    {
        engineInterface->typeSystem.UnregisterPlugin( engineInterface->rwobjTypeInfo, this->pluginOffset );
    }

    inline refCountPlugin* GetPluginStruct( EngineInterface *engineInterface, RwObject *obj )
    {
        GenericRTTI *rtObj = RwTypeSystem::GetTypeStructFromObject( obj );

        return RwTypeSystem::RESOLVE_STRUCT <refCountPlugin> ( engineInterface, rtObj, engineInterface->rwobjTypeInfo, this->pluginOffset );
    }

    RwTypeSystem::pluginOffset_t pluginOffset;
};

static PluginDependantStructRegister <refCountManager, RwInterfaceFactory_t> refCountRegister;

// Acquisition routine for objects, so that reference counting is increased, if needed.
// Can return nullptr if the reference count could not be increased.
RwObject* AcquireObject( RwObject *obj )
{
    EngineInterface *engineInterface = (EngineInterface*)obj->engineInterface;

    // Increase the reference count.
    if ( refCountManager *refMan = refCountRegister.GetPluginStruct( engineInterface ) )
    {
        if ( refCountPlugin *refCount = refMan->GetPluginStruct( engineInterface, obj ) )
        {
            // TODO: make sure that incrementing is actually possible.
            // we cannot increment if we would overflow the number, for instance.

            refCount->AddRef();
        }
    }

    return obj;
}

void ReleaseObject( RwObject *obj )
{
    EngineInterface *engineInterface = (EngineInterface*)obj->engineInterface;

    if ( refCountManager *refMan = refCountRegister.GetPluginStruct( (EngineInterface*)engineInterface ) )
    {
        if ( refCountPlugin *refCount = refMan->GetPluginStruct( engineInterface, obj ) )
        {
            (void)refCount;

            // We just delete the object.
            engineInterface->DeleteRwObject( obj );
        }
    }
}

uint32 GetRefCount( RwObject *obj )
{
    uint32 refCountNum = 1;    // If we do not support reference counting, this is actually a valid value.

    EngineInterface *engineInterface = (EngineInterface*)obj->engineInterface;

    // Increase the reference count.
    if ( refCountManager *refMan = refCountRegister.GetPluginStruct( engineInterface ) )
    {
        if ( refCountPlugin *refCount = refMan->GetPluginStruct( engineInterface, obj ) )
        {
            // We just delete the object.
            refCountNum = refCount->refCount;
        }
    }

    return refCountNum;
}

RwObject* Interface::ConstructRwObject( const char *typeName )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    RwObject *newObj = nullptr;

    if ( RwTypeSystem::typeInfoBase *rwobjTypeInfo = engineInterface->rwobjTypeInfo )
    {
        // Try to find a type that inherits from RwObject with this name.
        RwTypeSystem::typeInfoBase *rwTypeInfo = engineInterface->typeSystem.FindTypeInfo( typeName, rwobjTypeInfo );

        if ( rwTypeInfo )
        {
            // Try to construct us.
            GenericRTTI *rtObj = engineInterface->typeSystem.Construct( engineInterface, rwTypeInfo, nullptr );

            if ( rtObj )
            {
                // We are successful! Return the new object.
                newObj = (RwObject*)RwTypeSystem::GetObjectFromTypeStruct( rtObj );
            }
        }
    }

    return newObj;
}

RwObject* Interface::CloneRwObject( const RwObject *srcObj )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    RwObject *newObj = nullptr;

    // We simply use our type system to do the job.
    const GenericRTTI *rttiObj = engineInterface->typeSystem.GetTypeStructFromConstAbstractObject( srcObj );

    if ( rttiObj )
    {
        GenericRTTI *newRtObj = engineInterface->typeSystem.Clone( engineInterface, rttiObj );

        if ( newRtObj )
        {
            newObj = (RwObject*)RwTypeSystem::GetObjectFromTypeStruct( newRtObj );
        }
    }

    return newObj;
}

void Interface::DeleteRwObject( RwObject *obj )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    // Delete it using the type system.
    GenericRTTI *rttiObj = engineInterface->typeSystem.GetTypeStructFromAbstractObject( obj );

    if ( rttiObj )
    {
        // By default, we can destroy.
        bool canDestroy = true;

        // If we have the refcount plugin, we want to handle things with it.
        if ( refCountManager *refMan = refCountRegister.GetPluginStruct( engineInterface ) )
        {
            if ( refCountPlugin *refCountObj = RwTypeSystem::RESOLVE_STRUCT <refCountPlugin> ( engineInterface, rttiObj, engineInterface->rwobjTypeInfo, refMan->pluginOffset ) )
            {
                canDestroy = refCountObj->RemoveRef();
            }
        }

        if ( canDestroy )
        {
            engineInterface->typeSystem.Destroy( engineInterface, rttiObj );
        }
    }
}

void Interface::GetObjectTypeNames( rwobjTypeNameList_t& listOut ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    if ( RwTypeSystem::typeInfoBase *rwobjTypeInfo = engineInterface->rwobjTypeInfo )
    {
        for ( RwTypeSystem::type_iterator iter = engineInterface->typeSystem.GetTypeIterator(); !iter.IsEnd(); iter.Increment() )
        {
            RwTypeSystem::typeInfoBase *item = iter.Resolve();

            if ( item != rwobjTypeInfo )
            {
                if ( engineInterface->typeSystem.IsTypeInheritingFrom( rwobjTypeInfo, item ) )
                {
                    listOut.AddToBack( item->name );
                }
            }
        }
    }
}

bool Interface::IsObjectRegistered( const char *typeName ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    if ( RwTypeSystem::typeInfoBase *rwobjTypeInfo = engineInterface->rwobjTypeInfo )
    {
        // Try to find a type that inherits from RwObject with this name.
        RwTypeSystem::typeInfoBase *rwTypeInfo = engineInterface->typeSystem.FindTypeInfo( typeName, rwobjTypeInfo );

        if ( rwTypeInfo )
        {
            return true;
        }
    }

    return false;
}

const char* Interface::GetObjectTypeName( const RwObject *rwObj ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    const char *typeName = "unknown";

    const GenericRTTI *rtObj = engineInterface->typeSystem.GetTypeStructFromConstAbstractObject( rwObj );

    if ( rtObj )
    {
        RwTypeSystem::typeInfoBase *typeInfo = RwTypeSystem::GetTypeInfoFromTypeStruct( rtObj );

        // Return its type name.
        // This is an IMMUTABLE property, so we are safe.
        typeName = typeInfo->name;
    }

    return typeName;
}

void Interface::SetWarningManager( WarningManagerInterface *warningMan )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetWarningManager( warningMan );
}

WarningManagerInterface* Interface::GetWarningManager( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetWarningManager();
}

void Interface::SetWarningLevel( int level )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetWarningLevel( level );
}

int Interface::GetWarningLevel( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetWarningLevel();
}

void Interface::SetIgnoreSecureWarnings( bool doIgnore )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetIgnoreSecureWarnings( doIgnore );
}

bool Interface::GetIgnoreSecureWarnings( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetIgnoreSecureWarnings();
}

bool Interface::SetPaletteRuntime( ePaletteRuntimeType palRunType )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    return GetEnvironmentConfigBlock( engineInterface ).SetPaletteRuntime( palRunType );
}

ePaletteRuntimeType Interface::GetPaletteRuntime( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetPaletteRuntime();
}

void Interface::SetDXTRuntime( eDXTCompressionMethod dxtRunType )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetDXTRuntime( dxtRunType );
}

eDXTCompressionMethod Interface::GetDXTRuntime( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetDXTRuntime();
}

void Interface::SetFixIncompatibleRasters( bool doFix )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetFixIncompatibleRasters( doFix );
}

bool Interface::GetFixIncompatibleRasters( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetFixIncompatibleRasters();
}

void Interface::SetCompatTransformNativeImaging( bool transfEnable )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetCompatTransformNativeImaging( transfEnable );
}

bool Interface::GetCompatTransformNativeImaging( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetCompatTransformNativeImaging();
}

void Interface::SetPreferPackedSampleExport( bool preferPacked )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetPreferPackedSampleExport( preferPacked );
}

bool Interface::GetPreferPackedSampleExport( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetPreferPackedSampleExport();
}

void Interface::SetDXTPackedDecompression( bool packedDecompress )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetDXTPackedDecompression( packedDecompress );
}

bool Interface::GetDXTPackedDecompression( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetDXTPackedDecompression();
}

void Interface::SetIgnoreSerializationBlockRegions( bool doIgnore )
{
    EngineInterface *engineInterface = (EngineInterface*)this;

    GetEnvironmentConfigBlock( engineInterface ).SetIgnoreSerializationBlockRegions( doIgnore );
}

bool Interface::GetIgnoreSerializationBlockRegions( void ) const
{
    const EngineInterface *engineInterface = (const EngineInterface*)this;

    return GetConstEnvironmentConfigBlock( engineInterface ).GetIgnoreSerializationBlockRegions();
}

// Static library object that takes care of initializing the module dependencies properly.
extern void registerConfigurationEnvironment( void );
extern void registerThreadingEnvironment( void );
extern void registerWarningHandlerEnvironment( void );
extern void registerEventSystem( void );
extern void registerTXDPlugins( void );
extern void registerObjectExtensionsPlugins( void );
extern void registerSerializationPlugins( void );
extern void registerStreamGlobalPlugins( void );
extern void registerFileSystemDataRepository( void );
extern void registerImagingPlugin( void );
extern void registerNativeImagePluginEnvironment( void );
extern void registerWindowingSystem( void );
extern void registerDriverEnvironment( void );
extern void registerDrawingLayerEnvironment( void );

extern void registerConfigurationBlockDispatching( void );

static bool hasInitialized = false;

static bool VerifyLibraryIntegrity( void )
{
    // We need to standardize the number formats.
    // One way to check that is to make out their size, I guess.
    // Then there is also the problem of endianness, which we do not check here :(
    // For that we have to add special handling into the serialization environments.
    return
        ( sizeof(uint8) == 1 &&
          sizeof(uint16) == 2 &&
          sizeof(uint32) == 4 &&
          sizeof(uint64) == 8 &&
          sizeof(int8) == 1 &&
          sizeof(int16) == 2 &&
          sizeof(int32) == 4 &&
          sizeof(int64) == 8 &&
          sizeof(float32) == 4 );
}

// Interface creation for the RenderWare engine.
Interface* CreateEngine( LibraryVersion theVersion )
{
    if ( hasInitialized == false )
    {
        // Verify data constants before we create a valid engine.
        if ( VerifyLibraryIntegrity() )
        {
            // Very important environment.
            registerThreadingEnvironment();

            // Configuration comes first.
            registerConfigurationEnvironment();

            // Safe to initialize the remainder of the engine now.
            engineFactory.RegisterDependantStructPlugin <rw_afterinit_t> ();

            // Initialize our plugins first.
            refCountRegister.RegisterPlugin( engineFactory );
            rwlockProvider.RegisterPlugin( engineFactory );

            // Now do the main modules.
            registerWarningHandlerEnvironment();
            registerEventSystem();
            registerStreamGlobalPlugins();
            registerFileSystemDataRepository();
            registerSerializationPlugins();
            registerObjectExtensionsPlugins();
            registerTXDPlugins();
            registerImagingPlugin();
            registerNativeImagePluginEnvironment();
            registerWindowingSystem();
            registerDriverEnvironment();                // THREAD PLUGINS INCLUDED
            registerDrawingLayerEnvironment();

            // After all plugins registered themselves, we know that
            // each configuration entry is properly initialized.
            // Now we can create a configuration block in the interface!
            registerConfigurationBlockDispatching();    // THREAD PLUGIN INCLUDED

            hasInitialized = true;
        }
    }

    Interface *engineOut = nullptr;

    if ( hasInitialized == true )
    {
        // Create a specialized engine depending on the version.
        RwStaticMemAllocator memAlloc;
        
        engineOut = engineFactory.ConstructArgs( memAlloc );

        if ( engineOut )
        {
            engineOut->SetVersion( theVersion );
        }
    }

    return engineOut;
}

void DeleteEngine( Interface *theEngine )
{
    assert( hasInitialized == true );

    EngineInterface *engineInterface = (EngineInterface*)theEngine;

    // Kill everything threading related, so we can terminate.
    ThreadingMarkAsTerminating( engineInterface );      // disallow creation of new threading objects
    PurgeActiveThreadingObjects( engineInterface );     // destroy all available threading objects

    // Destroy the engine again.
    RwStaticMemAllocator memAlloc;

    engineFactory.Destroy( memAlloc, engineInterface );
}

};
