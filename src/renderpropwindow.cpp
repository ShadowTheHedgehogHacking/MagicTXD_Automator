#include "mainwindow.h"
#include "renderpropwindow.h"

#include "qtinteroputils.hxx"

#include "qtutils.h"
#include "languages.h"

struct addrToNatural
{
    rw::eRasterStageAddressMode mode;
    std::string natural;

    inline bool operator == ( const rw::eRasterStageAddressMode& right ) const
    {
        return ( right == this->mode );
    }

    inline bool operator == ( const QString& right ) const
    {
        return ( qstring_native_compare( right, this->natural.c_str() ) );
    }
};

typedef naturalModeList <addrToNatural> addrToNaturalList_t;

static const addrToNaturalList_t addrToNaturalList =
{
    { rw::RWTEXADDRESS_WRAP, "wrap" },
    { rw::RWTEXADDRESS_CLAMP, "clamp" },
    { rw::RWTEXADDRESS_MIRROR, "mirror" }
};

struct filterToNatural
{
    rw::eRasterStageFilterMode mode;
    std::string natural;
    bool isMipmap;

    inline bool operator == ( const rw::eRasterStageFilterMode& right ) const
    {
        return ( right == this->mode );
    }

    inline bool operator == ( const QString& right ) const
    {
        return ( qstring_native_compare( right, this->natural.c_str() ) );
    }
};

typedef naturalModeList <filterToNatural> filterToNaturalList_t;

static const filterToNaturalList_t filterToNaturalList =
{
    { rw::RWFILTER_POINT, "point", false },
    { rw::RWFILTER_LINEAR, "linear", false },
    { rw::RWFILTER_POINT_POINT, "point_mip_point", true },
    { rw::RWFILTER_POINT_LINEAR, "point_mip_linear", true },
    { rw::RWFILTER_LINEAR_POINT, "linear_mip_point", true },
    { rw::RWFILTER_LINEAR_LINEAR, "linear_mip_linear", true }
};

inline QComboBox* createAddressingBox( void )
{
    QComboBox *addrSelect = new QComboBox();

    for ( const addrToNatural& item : addrToNaturalList )
    {
        addrSelect->addItem( QString::fromStdString( item.natural ) );
    }

    addrSelect->setMinimumWidth( 200 );

    return addrSelect;
}

QComboBox* RenderPropWindow::createFilterBox( void ) const
{
    // We assume that the texture we are editing does not get magically modified while
    // this dialog is open.

    bool hasMipmaps = false;
    {
        if ( TexInfoWidget *texInfo = this->texInfo )
        {
            if ( rw::TextureBase *texHandle = texInfo->GetTextureHandle() )
            {
                if ( rw::Raster *texRaster = texHandle->GetRaster() )
                {
                    hasMipmaps = ( texRaster->getMipmapCount() > 1 );
                }
            }
        }
    }

    QComboBox *filterSelect = new QComboBox();

    for ( const filterToNatural& item : filterToNaturalList )
    {
        bool isMipmapProp = item.isMipmap;

        if ( isMipmapProp == hasMipmaps )
        {
            filterSelect->addItem( QString::fromStdString( item.natural ) );
        }
    }

    return filterSelect;
}

RenderPropWindow::RenderPropWindow( MainWindow *mainWnd, TexInfoWidget *texInfo ) : QDialog( mainWnd )
{
    this->setWindowFlags( this->windowFlags() & ~Qt::WindowContextHelpButtonHint );

    this->mainWnd = mainWnd;
    this->texInfo = texInfo;

    this->setAttribute( Qt::WA_DeleteOnClose );
    this->setWindowModality( Qt::WindowModal );

    // Decide what status to give the dialog first.
    rw::eRasterStageFilterMode begFilterMode = rw::RWFILTER_POINT;
    rw::eRasterStageAddressMode begUAddrMode = rw::RWTEXADDRESS_WRAP;
    rw::eRasterStageAddressMode begVAddrMode = rw::RWTEXADDRESS_WRAP;

    if ( texInfo )
    {
        if ( rw::TextureBase *texHandle = texInfo->GetTextureHandle() )
        {
            begFilterMode = texHandle->GetFilterMode();
            begUAddrMode = texHandle->GetUAddressing();
            begVAddrMode = texHandle->GetVAddressing();
        }
    }

    // We want to put rows of combo boxes.
    // Best put them into a form layout.
    MagicLayout<QFormLayout> layout(this);

    QComboBox *filteringSelectBox = createFilterBox();
    {
        QString natural;

        bool gotNatural = filterToNaturalList.getNaturalFromMode( begFilterMode, natural );

        if ( gotNatural )
        {
            filteringSelectBox->setCurrentText( natural );
        }
    }

    this->filterComboBox = filteringSelectBox;

    connect( filteringSelectBox, (void (QComboBox::*)( const QString& ))&QComboBox::activated, this, &RenderPropWindow::OnAnyPropertyChange );

    layout.top->addRow( CreateLabelL( "Main.SetupRP.Filter" ), filteringSelectBox );

    QComboBox *uaddrSelectBox = createAddressingBox();
    {
        QString natural;

        bool gotNatural = addrToNaturalList.getNaturalFromMode( begUAddrMode, natural );

        if ( gotNatural )
        {
            uaddrSelectBox->setCurrentText( natural );
        }
    }

    this->uaddrComboBox = uaddrSelectBox;

    connect( uaddrSelectBox, (void (QComboBox::*)( const QString& ))&QComboBox::activated, this, &RenderPropWindow::OnAnyPropertyChange );

    layout.top->addRow( CreateLabelL( "Main.SetupRP.UAddr" ), uaddrSelectBox );

    QComboBox *vaddrSelectBox = createAddressingBox();
    {
        QString natural;

        bool gotNatural = addrToNaturalList.getNaturalFromMode( begVAddrMode, natural );

        if ( gotNatural )
        {
            vaddrSelectBox->setCurrentText( natural );
        }
    }

    this->vaddrComboBox = vaddrSelectBox;

    connect( vaddrSelectBox, (void (QComboBox::*)( const QString& ))&QComboBox::activated, this, &RenderPropWindow::OnAnyPropertyChange );

    layout.top->addRow( CreateLabelL( "Main.SetupRP.VAddr" ), vaddrSelectBox );

    // And now add the usual buttons.
    QPushButton *buttonSet = CreateButtonL( "Main.SetupRP.Set" );
    layout.bottom->addWidget( buttonSet );

    this->buttonSet = buttonSet;

    connect( buttonSet, &QPushButton::clicked, this, &RenderPropWindow::OnRequestSet );

    QPushButton *buttonCancel = CreateButtonL( "Main.SetupRP.Cancel" );
    layout.bottom->addWidget( buttonCancel );

    connect( buttonCancel, &QPushButton::clicked, this, &RenderPropWindow::OnRequestCancel );

    mainWnd->renderPropDlg = this;

    RegisterTextLocalizationItem( this );

    // Initialize the dialog.
    this->UpdateAccessibility();
}

RenderPropWindow::~RenderPropWindow( void )
{
    this->mainWnd->renderPropDlg = NULL;

    UnregisterTextLocalizationItem( this );
}

void RenderPropWindow::updateContent( MainWindow *mainWnd )
{
    this->setWindowTitle( MAGIC_TEXT("Main.SetupRP.Desc") );
}

void RenderPropWindow::OnRequestSet( bool checked )
{
    // Update the texture.
    if ( TexInfoWidget *texInfo = this->texInfo )
    {
        if ( rw::TextureBase *texHandle = texInfo->GetTextureHandle() )
        {
            // Just set the properties that we recognize.
            bool hasChanged = false;
            {
                rw::eRasterStageFilterMode filterMode;

                bool hasProp = filterToNaturalList.getModeFromNatural( this->filterComboBox->currentText(), filterMode );

                if ( hasProp )
                {
                    texHandle->SetFilterMode( filterMode );

                    hasChanged = true;
                }
            }

            {
                rw::eRasterStageAddressMode uaddrMode;

                bool hasProp = addrToNaturalList.getModeFromNatural( this->uaddrComboBox->currentText(), uaddrMode );
                    
                if ( hasProp )
                {
                    texHandle->SetUAddressing( uaddrMode );

                    hasChanged = true;
                }
            }

            {
                rw::eRasterStageAddressMode vaddrMode;

                bool hasProp = addrToNaturalList.getModeFromNatural( this->vaddrComboBox->currentText(), vaddrMode );

                if ( hasProp )
                {
                    texHandle->SetVAddressing( vaddrMode );

                    hasChanged = true;
                }
            }

            if ( hasChanged )
            {
                // Well, we changed something.
                mainWnd->NotifyChange();
            }
        }
    }

    this->close();
}

void RenderPropWindow::OnRequestCancel( bool checked )
{
    this->close();
}

void RenderPropWindow::UpdateAccessibility( void )
{
    // Only allow setting if we actually change from the original values.
    bool allowSet = true;

    if ( TexInfoWidget *texInfo = this->texInfo )
    {
        if ( rw::TextureBase *texHandle = texInfo->GetTextureHandle() )
        {
            rw::eRasterStageFilterMode curFilterMode = texHandle->GetFilterMode();
            rw::eRasterStageAddressMode curUAddrMode = texHandle->GetUAddressing();
            rw::eRasterStageAddressMode curVAddrMode = texHandle->GetVAddressing();

            bool havePropsChanged = false;

            if ( !havePropsChanged )
            {
                rw::eRasterStageFilterMode selFilterMode;
                    
                bool hasProp = filterToNaturalList.getModeFromNatural( this->filterComboBox->currentText(), selFilterMode );

                if ( hasProp && curFilterMode != selFilterMode )
                {
                    havePropsChanged = true;
                }
            }

            if ( !havePropsChanged )
            {
                rw::eRasterStageAddressMode selUAddrMode;

                bool hasProp = addrToNaturalList.getModeFromNatural( this->uaddrComboBox->currentText(), selUAddrMode );

                if ( hasProp && curUAddrMode != selUAddrMode )
                {
                    havePropsChanged = true;
                }
            }

            if ( !havePropsChanged )
            {
                rw::eRasterStageAddressMode selVAddrMode;

                bool hasProp = addrToNaturalList.getModeFromNatural( this->vaddrComboBox->currentText(), selVAddrMode );

                if ( hasProp && curVAddrMode != selVAddrMode )
                {
                    havePropsChanged = true;
                }
            }

            if ( !havePropsChanged )
            {
                allowSet = false;
            }
        }
    }

    this->buttonSet->setDisabled( !allowSet );
}