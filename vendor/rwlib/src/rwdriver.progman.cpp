#include "StdInc.h"

#include "rwdriver.progman.hxx"

namespace rw
{

void driverProgramManager::Initialize( EngineInterface *engineInterface )
{
    LIST_CLEAR( this->programs.root );
    LIST_CLEAR( this->nativeManagers.root );

    // We need a type for GPU programs.
    this->gpuProgTypeInfo = engineInterface->typeSystem.RegisterAbstractType <void*> ( "GPUProgram" );
}

void driverProgramManager::Shutdown( EngineInterface *engineInterface )
{
    // Make sure all programs have deleted themselves.
    assert( LIST_EMPTY( this->programs.root ) == true );
    assert( LIST_EMPTY( this->nativeManagers.root ) == true );

    // Delete the GPU program type.
    if ( RwTypeSystem::typeInfoBase *typeInfo = this->gpuProgTypeInfo )
    {
        engineInterface->typeSystem.DeleteType( typeInfo );

        this->gpuProgTypeInfo = nullptr;
    }
}

PluginDependantStructRegister <driverProgramManager, RwInterfaceFactory_t> driverProgramManagerReg;

// Sub modules.
extern void registerHLSLDriverProgramManager( void );

void registerDriverProgramManagerEnv( void )
{
    driverProgramManagerReg.RegisterPlugin( engineFactory );

    // And now for sub-modules.
    registerHLSLDriverProgramManager();
}

struct customNativeProgramTypeInterface final : public RwTypeSystem::typeInterface
{
    AINLINE customNativeProgramTypeInterface( size_t programSize, driverNativeProgramManager *nativeMan )
    {
        this->programSize = programSize;
        this->nativeMan = nativeMan;
    }

    void Construct( void *mem, EngineInterface *engineInterface, void *construct_params ) const override
    {
        const driverNativeProgramCParams *progParams = (const driverNativeProgramCParams*)construct_params;

        driverProgramHandle *progHandle = new (mem) driverProgramHandle( engineInterface, progParams->progType );

        try
        {
            nativeMan->ConstructProgram( engineInterface, progHandle->GetImplementation(), *progParams );
        }
        catch( ... )
        {
            progHandle->~driverProgramHandle();
            throw;
        }
    }

    void CopyConstruct( void *mem, const void *srcMem ) const override
    {
        const driverProgramHandle *srcObj = (const driverProgramHandle*)srcMem;

        EngineInterface *engineInterface = srcObj->engineInterface;

        driverProgramHandle *copyObj = new (mem) driverProgramHandle( engineInterface, srcObj->programType );

        try
        {
            nativeMan->CopyConstructProgram( copyObj->GetImplementation(), srcObj->GetImplementation() );
        }
        catch( ... )
        {
            copyObj->~driverProgramHandle();
            throw;
        }
    }

    void Destruct( void *mem ) const override
    {
        driverProgramHandle *natProg = (driverProgramHandle*)mem;

        nativeMan->DestructProgram( natProg->GetImplementation() );

        natProg->~driverProgramHandle();
    }

    size_t GetTypeSize( EngineInterface *engineInterface, void *construct_params ) const override
    {
        return this->programSize + sizeof(driverProgramHandle);
    }

    size_t GetTypeSizeByObject( EngineInterface *engineInterface, const void *mem ) const override
    {
        return this->programSize + sizeof(driverProgramHandle);
    }

    size_t programSize;
    driverNativeProgramManager *nativeMan;
};

// Native manager registration API.
bool RegisterNativeProgramManager( EngineInterface *engineInterface, const char *nativeName, driverNativeProgramManager *manager, size_t programSize )
{
    bool success = false;

    if ( driverProgramManager *progMan = driverProgramManagerReg.GetPluginStruct( engineInterface ) )
    {
        if ( RwTypeSystem::typeInfoBase *gpuProgTypeInfo = progMan->gpuProgTypeInfo )
        {
            (void)gpuProgTypeInfo;

            // Only register if the native name is not taken already.
            bool isAlreadyTaken = ( progMan->FindNativeManager( nativeName ) != nullptr );

            if ( !isAlreadyTaken )
            {
                if ( manager->nativeManData.isRegistered == false )
                {
                    // We need to create a type for our native program.

                    // Attempt to create the native program type.
                    RwTypeSystem::typeInfoBase *nativeProgType =
                        engineInterface->typeSystem.RegisterCommonTypeInterface <customNativeProgramTypeInterface> (
                            nativeName, progMan->gpuProgTypeInfo, programSize, manager
                        );

                    if ( nativeProgType )
                    {
                        // Time to put us into position :)
                        manager->nativeManData.nativeType = nativeProgType;
                        LIST_INSERT( progMan->nativeManagers.root, manager->nativeManData.node );

                        manager->nativeManData.isRegistered = true;

                        success = true;
                    }
                }
            }
        }
    }

    return success;
}

bool UnregisterNativeProgramManager( EngineInterface *engineInterface, const char *nativeName )
{
    bool success = false;

    if ( driverProgramManager *progMan = driverProgramManagerReg.GetPluginStruct( engineInterface ) )
    {
        driverNativeProgramManager *nativeMan = progMan->FindNativeManager( nativeName );

        if ( nativeMan )
        {
            // Delete the type associated with this native program manager.
            engineInterface->typeSystem.DeleteType( nativeMan->nativeManData.nativeType );

            // Well, unregister the thing that the runtime requested us to.
            LIST_REMOVE( nativeMan->nativeManData.node );

            nativeMan->nativeManData.isRegistered = false;

            success = true;
        }
    }

    return success;
}

// Program API :)
DriverProgram* CompileNativeProgram( Interface *intf, const char *nativeName, const char *entryPointName, eDriverProgType progType, const char *shaderSrc, size_t shaderSize )
{
    EngineInterface *engineInterface = (EngineInterface*)intf;

    driverProgramHandle *handle = nullptr;

    if ( driverProgramManager *progMan = driverProgramManagerReg.GetPluginStruct( engineInterface ) )
    {
        // Find the native compiler for this shader code.
        driverNativeProgramManager *nativeMan = progMan->FindNativeManager( nativeName );

        if ( nativeMan )
        {
            // Create our program object and compile it.
            driverProgramHandle *progHandle = nullptr;

            driverNativeProgramCParams cparams;
            cparams.progType = progType;

            GenericRTTI *rtObj = engineInterface->typeSystem.Construct( engineInterface, nativeMan->nativeManData.nativeType, &cparams );

            if ( rtObj )
            {
                progHandle = (driverProgramHandle*)RwTypeSystem::GetObjectFromTypeStruct( rtObj );
            }

            if ( progHandle )
            {
                // Now the compilation.
                try
                {
                    nativeMan->CompileProgram( progHandle->GetImplementation(), entryPointName, shaderSrc, shaderSize );
                }
                catch( ... )
                {
                    engineInterface->typeSystem.Destroy( engineInterface, rtObj );
                    throw;
                }

                handle = progHandle;
            }
        }
    }

    return handle;
}

void DeleteDriverProgram( DriverProgram *program )
{
    driverProgramHandle *natProg = (driverProgramHandle*)program;

    EngineInterface *engineInterface = natProg->engineInterface;

    // Simply delete the dynamic object.
    GenericRTTI *rtObj = RwTypeSystem::GetTypeStructFromObject( natProg );

    if ( rtObj )
    {
        engineInterface->typeSystem.Destroy( engineInterface, rtObj );
    }
}

// Get the native program type manager through the object type info.
inline driverNativeProgramManager* GetNativeManager( const driverProgramHandle *handle )
{
    const GenericRTTI *rtObj = RwTypeSystem::GetTypeStructFromConstObject( handle );

    if ( rtObj )
    {
        RwTypeSystem::typeInfoBase *typeInfo = RwTypeSystem::GetTypeInfoFromTypeStruct( rtObj );

        customNativeProgramTypeInterface *natTypeInfo = dynamic_cast <customNativeProgramTypeInterface*> ( typeInfo->tInterface );

        if ( natTypeInfo )
        {
            return natTypeInfo->nativeMan;
        }
    }

    return nullptr;
}

const void* DriverProgram::GetBytecodeBuffer( void ) const
{
    const driverProgramHandle *natProg = (const driverProgramHandle*)this;

    if ( driverNativeProgramManager *nativeMan = GetNativeManager( natProg ) )
    {
        return nativeMan->ProgramGetBytecodeBuffer( natProg->GetImplementation() );
    }

    return 0;
}

size_t DriverProgram::GetBytecodeSize( void ) const
{
    const driverProgramHandle *natProg = (const driverProgramHandle*)this;

    if ( driverNativeProgramManager *nativeMan = GetNativeManager( natProg ) )
    {
        return nativeMan->ProgramGetBytecodeSize( natProg->GetImplementation() );
    }

    return 0;
}

};
