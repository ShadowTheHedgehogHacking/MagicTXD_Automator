#pragma once

#include "shared.h"

struct MassExportModule abstract : public MessageReceiver
{
    enum eOutputType
    {
        OUTPUT_PLAIN,
        OUTPUT_TXDNAME,
        OUTPUT_FOLDERS
    };

    struct run_config
    {
        rw::rwStaticString <wchar_t> gameRoot = L"export_in/";
        rw::rwStaticString <wchar_t> outputRoot = L"export_out/";
        rw::rwStaticString <char> recImgFormat = "PNG";
        eOutputType outputType = OUTPUT_TXDNAME;
    };

    inline MassExportModule( rw::Interface *rwEngine )
    {
        this->rwEngine = rwEngine;
    }

    inline rw::Interface* GetEngine( void ) const
    {
        return rwEngine;
    }

    bool ApplicationMain( const run_config& cfg );

    // We do not care about messages.
    void OnMessage( const rw::rwStaticString <char>& msg ) override
    {
        return;
    }
    void OnMessage( const rw::rwStaticString <wchar_t>& msg ) override
    {
        return;
    }

    virtual void OnProcessingFile( const std::wstring& fileName ) = 0;
    
private:
    rw::Interface *rwEngine;
};