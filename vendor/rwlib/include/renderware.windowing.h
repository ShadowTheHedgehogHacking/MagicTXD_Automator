/*
    RenderWare windowing system abstractions.
    This is an utility library you can use for cross-platform window creation.
    Every platform is assumed to have capabilities of creaing a render surface.
*/

namespace rw
{

struct Window : public RwObject
{
    Window( Interface *engineInterface, void *construction_params );
    Window( const Window& right );
    ~Window( void );

    void SetVisible( bool vis );
    bool IsVisible( void ) const;

    inline uint32 GetClientWidth( void ) const
    {
        return this->clientWidth;
    }

    inline uint32 GetClientHeight( void ) const
    {
        return this->clientHeight;
    }

    void SetClientSize( uint32 clientWidth, uint32 clientHeight );

protected:
    uint32 clientWidth, clientHeight;
};

// Window management functions.
Window* MakeWindow( Interface *engineInterface, uint32 clientWidth, uint32 clientHeight );

// Calling this function is mandatory in your game loop, if you are using the RW windowing system.
void PulseWindowingSystem( Interface *engineInterface );

void YieldExecution( uint32 ms );

} // namespace rw