#pragma once

#include "shared.h"

struct TxdBuildModule abstract : public MessageReceiver, public rw::WarningManagerInterface
{
    inline TxdBuildModule( rw::Interface *rwEngine )
    {
        this->rwEngine = rwEngine;
    }

    struct run_config
    {
        rw::rwStaticString <wchar_t> gameRoot = L"massbuild_in/";
        rw::rwStaticString <wchar_t> outputRoot = L"massbuild_out/";

        rwkind::eTargetPlatform targetPlatform = rwkind::PLATFORM_PC;
        rwkind::eTargetGame targetGame = rwkind::GAME_GTASA;

        bool generateMipmaps = false;
        int curMipMaxLevel = 32;

        bool doCompress = false;
        float compressionQuality = 1.0f;
        bool doPalettize = false;
        rw::ePaletteType paletteType = rw::PALETTE_NONE;
    };

    bool RunApplication( const run_config& cfg );

protected:
    void OnWarning( rw::rwStaticString <char>&& msg ) override;

private:
    rw::Interface *rwEngine;
};