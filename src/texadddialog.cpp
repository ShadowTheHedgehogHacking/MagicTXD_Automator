#include "mainwindow.h"

#include "qtrwutils.hxx"

#include "qtutils.h"
#include "languages.h"
#include "testmessage.h"

#include "texnameutils.hxx"

#ifdef _DEBUG
static const bool _lockdownPlatform = false;        // SET THIS TO TRUE FOR RELEASE.
#else
static const bool _lockdownPlatform = true;         // WE ARE RELEASING SOON.
#endif
static const size_t _recommendedPlatformMaxName = 32;
static const bool _enableMaskName = false;

inline QString calculateImageBaseName(QString fileName)
{
    // Determine the texture name.
    QFileInfo fileInfo(fileName);

    return fileInfo.baseName();
}

void TexAddDialog::SetCurrentPlatform( QString name )
{
    if (MagicLineEdit *editBox = dynamic_cast <MagicLineEdit*> ( this->platformSelectWidget ) )
    {
        editBox->setText( std::move( name ) );
    }
    else if ( QComboBox *comboBox = dynamic_cast <QComboBox*> ( this->platformSelectWidget ) )
    {
        comboBox->setCurrentText( std::move( name ) );
    }
}

QString TexAddDialog::GetCurrentPlatform(void)
{
    QString currentPlatform;

    if (MagicLineEdit *editBox = dynamic_cast <MagicLineEdit*> (this->platformSelectWidget))
    {
        currentPlatform = editBox->text();
    }
    else if (QComboBox *comboBox = dynamic_cast <QComboBox*> (this->platformSelectWidget))
    {
        currentPlatform = comboBox->currentText();
    }

    return currentPlatform;
}

void TexAddDialog::RwTextureAssignNewRaster(
    rw::TextureBase *texHandle, rw::Raster *newRaster,
    const std::string& texName, const std::string& maskName
)
{
    // Update names.
    texHandle->SetName( texName.c_str() );
    texHandle->SetMaskName( maskName.c_str() );

    // Replace raster handle.
    texHandle->SetRaster( newRaster );

    // We have to set proper filtering flags.
    texHandle->fixFiltering();
}

void TexAddDialog::releaseConvRaster(void)
{
    if (rw::Raster *convRaster = this->convRaster)
    {
        rw::DeleteRaster(convRaster);

        this->convRaster = nullptr;
    }
}

void TexAddDialog::clearTextureOriginal( void )
{
    // Remove any previous raster link.
    if ( rw::Raster *prevOrig = this->platformOrigRaster )
    {
        rw::DeleteRaster( prevOrig );

        this->platformOrigRaster = nullptr;
    }

    // Delete any texture link.
    if ( rw::TextureBase *texHandle = this->texHandle )
    {
        this->mainWnd->GetEngine()->DeleteRwObject( texHandle );

        this->texHandle = nullptr;
    }
}

rw::Raster* TexAddDialog::MakeRaster( void )
{
    rw::Interface *rwEngine = this->mainWnd->GetEngine();

    rw::Raster *platOrig = rw::CreateRaster( rwEngine );

    if ( platOrig )
    {
        try
        {
            // Set the platform of our raster.
            // If we have no platform, we fail.
            bool hasPlatform = false;

            QString currentPlatform = this->GetCurrentPlatform();

            if (currentPlatform.isEmpty() == false)
            {
                std::string ansiNativeName = qt_to_ansi( currentPlatform );

                // Set the platform.
                platOrig->newNativeData( ansiNativeName.c_str() );

                // We also want to set the version of our raster.
                if (rw::TexDictionary *texDictionary = this->mainWnd->currentTXD)
                {
                    platOrig->SetEngineVersion( texDictionary->GetEngineVersion() );
                }

                hasPlatform = true;
            }

            if ( !hasPlatform )
            {
                rw::DeleteRaster( platOrig );

                platOrig = nullptr;
            }
        }
        catch( ... )
        {
            rw::DeleteRaster( platOrig );

            throw;
        }
    }

    return platOrig;
}

void TexAddDialog::texAddImageImportMethods::OnWarning( rw::rwStaticString <char>&& msg ) const
{
    this->dialog->mainWnd->txdLog->addLogMessage( ansi_to_qt( msg ), LOGMSG_WARNING );
}

void TexAddDialog::texAddImageImportMethods::OnError( rw::rwStaticString <char>&& msg ) const
{
    this->dialog->mainWnd->txdLog->showError( ansi_to_qt( msg ) );
}

rw::Raster* TexAddDialog::texAddImageImportMethods::MakeRaster( void ) const
{
    return this->dialog->MakeRaster();
}

void TexAddDialog::loadPlatformOriginal(void)
{
    // If we have a converted raster, release it.
    this->releaseConvRaster();

    bool hasPreview = false;

    try
    {
        // Depends on what we have.
        if (this->dialog_type == CREATE_IMGPATH)
        {
            rw::Interface *rwEngine = this->mainWnd->GetEngine();

            // Open a stream to the image data.
            std::wstring unicodePathToImage = this->imgPath.toStdWString();

            rw::streamConstructionFileParamW_t wparam(unicodePathToImage.c_str());

            rw::Stream *imgStream = rwEngine->CreateStream(rw::RWSTREAMTYPE_FILE_W, rw::RWSTREAMMODE_READONLY, &wparam);

            if (imgStream)
            {
                try
                {
                    // Load it.
                    imageImportMethods::loadActionResult load_result;

                    bool couldLoad = this->impMeth.LoadImage( imgStream, this->img_exp, load_result );

                    if ( couldLoad )
                    {
                        rw::Raster *texRaster = load_result.texRaster;
                        rw::TextureBase *texHandle = load_result.texHandle;

                        try
                        {
                            // Since we have a new raster now, clear the previous gunk.
                            this->clearTextureOriginal();

                            // Proceed loading the stuff.
                            if ( texHandle )
                            {
                                // Put the raster into the correct platform, if wanted.
                                // This is becaue textures could have come with their own configuration.
                                // It is unlikely to be a problem for casual rasters.
                                {
                                    bool wantsToAdjustRaster = true;

                                    if ( this->isConstructing )
                                    {
                                        // If we are constructing, we actually do not want to adjust the raster all the time.
                                        wantsToAdjustRaster = false;

                                        if ( this->hasConfidentPlatform && this->mainWnd->adjustTextureChunksOnImport )
                                        {
                                            wantsToAdjustRaster = true;
                                        }
                                    }

                                    if ( wantsToAdjustRaster )
                                    {
                                        std::string ansiPlatformName = qt_to_ansi( this->GetCurrentPlatform() );

                                        rw::ConvertRasterTo( texRaster, ansiPlatformName.c_str() );
                                    }
                                    else
                                    {
                                        // We can update the platform here, without problems.
                                        this->SetCurrentPlatform( texRaster->getNativeDataTypeName() );
                                    }
                                }

                                // Also adjust the raster version.
                                if ( this->mainWnd->adjustTextureChunksOnImport )
                                {
                                    if ( rw::TexDictionary *currentTXD = this->mainWnd->currentTXD )
                                    {
                                        texHandle->SetEngineVersion( currentTXD->GetEngineVersion() );
                                    }
                                }
                            }
                        }
                        catch( ... )
                        {
                            // Since preparation of the raster/texture has failed, we have to delete the stuff.
                            load_result.cleanUpSuccessful();
                            throw;
                        }

                        // Store this raster.
                        // Since it comes with a special reference already, we do not have to cast one ourselves.
                        this->platformOrigRaster = texRaster;

                        // If there was a texture, we have to remember it too.
                        // It may contain unique properties.
                        if ( texHandle )
                        {
                            assert( this->texHandle == nullptr );

                            this->texHandle = texHandle;
                        }

                        // Success!
                        hasPreview = true;
                    }
                }
                catch (...)
                {
                    rwEngine->DeleteStream(imgStream);

                    throw;
                }

                rwEngine->DeleteStream(imgStream);
            }
        }
        else if (this->dialog_type == CREATE_RASTER)
        {
            // We always have a platform original.
            hasPreview = true;
        }
    }
    catch (rw::RwException& err)
    {
        // We do not care.
        // We simply failed to get a preview.
        hasPreview = false;

        // Probably should tell the user about this error, so we can fix it.
        mainWnd->txdLog->showError(QString("error while building preview: ") + ansi_to_qt(err.message));
    }

    this->hasPlatformOriginal = hasPreview;

    // If we have a preview, update the preview widget with its content.
    if (hasPreview)
    {
        this->UpdatePreview();
    }

    // Hide or show the changeable properties.
    this->propGenerateMipmaps->setVisible(hasPreview);

    // If we have no preview, then we also cannot push the data to the texture container.
    // This is why we should disable that possibility.
    this->applyButton->setDisabled(!hasPreview);

    // Hide or show the preview stuff.
    //this->previewGroupWidget->setVisible( hasPreview );
}

void TexAddDialog::updatePreviewWidget(void)
{
}

void TexAddDialog::createRasterForConfiguration(void)
{
    if (this->hasPlatformOriginal == false)
        return;

    // This function prepares the raster that will be given to the texture dictionary.

    bool hasConfiguredRaster = false;

    try
    {
        rw::eCompressionType compressionType = rw::RWCOMPRESS_NONE;

        rw::eRasterFormat rasterFormat = rw::RASTER_DEFAULT;
        rw::ePaletteType paletteType = rw::PALETTE_NONE;

        bool keepOriginal = this->platformOriginalToggle->isChecked();

        if (!keepOriginal)
        {
            // Now for the properties.
            if (this->platformCompressionToggle->isChecked())
            {
                // We are a compressed format, so determine what we actually are.
                QString selectedCompression = this->platformCompressionSelectProp->currentText();

                if (selectedCompression == "DXT1")
                {
                    compressionType = rw::RWCOMPRESS_DXT1;
                }
                else if (selectedCompression == "DXT2")
                {
                    compressionType = rw::RWCOMPRESS_DXT2;
                }
                else if (selectedCompression == "DXT3")
                {
                    compressionType = rw::RWCOMPRESS_DXT3;
                }
                else if (selectedCompression == "DXT4")
                {
                    compressionType = rw::RWCOMPRESS_DXT4;
                }
                else if (selectedCompression == "DXT5")
                {
                    compressionType = rw::RWCOMPRESS_DXT5;
                }
                else
                {
                    throw std::exception(); //"invalid compression type selected"
                }

                rasterFormat = rw::RASTER_DEFAULT;
                paletteType = rw::PALETTE_NONE;
            }
            else
            {
                compressionType = rw::RWCOMPRESS_NONE;

                // Now we have a valid raster format selected in the pixel format combo box.
                // We kinda need one.
                if (this->enablePixelFormatSelect)
                {
                    QString formatName = this->platformPixelFormatSelectProp->currentText();

                    std::string ansiFormatName = qt_to_ansi( formatName );

                    rasterFormat = rw::FindRasterFormatByName(ansiFormatName.c_str());

                    if (rasterFormat == rw::RASTER_DEFAULT)
                    {
                        throw std::exception(); //"invalid pixel format selected"
                    }
                }

                // And then we need to know whether it should be a palette or not.
                if (this->platformPaletteToggle->isChecked())
                {
                    // Alright, then we have to fetch a valid palette type.
                    QString paletteName = this->platformPaletteSelectProp->currentText();

                    if (paletteName == "PAL4")
                    {
                        // TODO: some archictures might prefer the MSB version.
                        // we should detect that automatically!

                        paletteType = rw::PALETTE_4BIT;
                    }
                    else if (paletteName == "PAL8")
                    {
                        paletteType = rw::PALETTE_8BIT;
                    }
                    else
                    {
                        throw std::exception(); //"invalid palette type selected"
                    }
                }
                else
                {
                    paletteType = rw::PALETTE_NONE;
                }
            }
        }

        // Create the raster.
        try
        {
            // Clear previous image data.
            this->releaseConvRaster();

            rw::Raster *convRaster = rw::CloneRaster(this->platformOrigRaster);

            this->convRaster = convRaster;

            // We must make sure that our raster is in the correct platform.
            {
                std::string currentPlatform = qt_to_ansi( this->GetCurrentPlatform() );

                rw::ConvertRasterTo(convRaster, currentPlatform.c_str());
            }

            // Format the raster appropriately.
            {
                if (compressionType != rw::RWCOMPRESS_NONE)
                {
                    // If the raster is already compressed, we want to decompress it.
                    // Very, very bad practice, but we allow it.
                    {
                        rw::eCompressionType curCompressionType = convRaster->getCompressionFormat();

                        if ( curCompressionType != rw::RWCOMPRESS_NONE )
                        {
                            convRaster->convertToFormat( rw::RASTER_8888 );
                        }
                    }

                    // Just compress it.
                    convRaster->compressCustom(compressionType);
                }
                else if (rasterFormat != rw::RASTER_DEFAULT)
                {
                    // We want a specialized format.
                    // Go ahead.
                    if (paletteType != rw::PALETTE_NONE)
                    {
                        // Palettize.
                        convRaster->convertToPalette(paletteType, rasterFormat);
                    }
                    else
                    {
                        // Let us convert to another format.
                        convRaster->convertToFormat(rasterFormat);
                    }
                }
            }

            // Success!
            hasConfiguredRaster = true;
        }
        catch (rw::RwException& except)
        {
            this->mainWnd->txdLog->showError(QString("failed to create raster: ") + ansi_to_qt(except.message));
        }
    }
    catch (std::exception& except)
    {
        // If we failed to push data to the output stage.
        this->mainWnd->txdLog->showError(QString("failed to create raster: ") + except.what());
    }

    // If we do not need a configured raster anymore, release it.
    if (!hasConfiguredRaster)
    {
        this->releaseConvRaster();
    }

    // Update the preview.
    this->UpdatePreview();
}

QComboBox* TexAddDialog::createPlatformSelectComboBox(MainWindow *mainWnd)
{
    QComboBox *platformComboBox = new QComboBox();

    // Fill out the combo box with available platforms.
    {
        rw::platformTypeNameList_t unsortedPlatforms = rw::GetAvailableNativeTextureTypes(mainWnd->rwEngine);

        // We want to sort the platforms by importance.
        rw::rwStaticVector <rw::rwStaticString <char>> platforms = PlatformImportanceSort( mainWnd, unsortedPlatforms );

        size_t numPlatforms = platforms.GetCount();

        for ( size_t n = 0; n < numPlatforms; n++ )
        {
            const rw::rwStaticString <char>& platName = platforms[ numPlatforms - 1 - n ];

            platformComboBox->addItem( ansi_to_qt( platName ) );
        }
    }

    return platformComboBox;
}

#define LEFTPANELADDDIALOGWIDTH 230

// We need an environment to handle helper stuff.
struct texAddDialogEnv
{
    inline void Initialize( MainWindow *mainWnd )
    {
        RegisterHelperWidget( mainWnd, "dxt_warning", eHelperTextType::DIALOG_WITH_TICK, "Modify.Help.DXTNotice" );
        RegisterHelperWidget( mainWnd, "pal_warning", eHelperTextType::DIALOG_WITH_TICK, "Modify.Help.PALNotice" );
    }

    inline void Shutdown( MainWindow *mainWnd )
    {
        UnregisterHelperWidget( mainWnd, "pal_warning" );
        UnregisterHelperWidget( mainWnd, "dxt_warning" );
    }
};

void InitializeTextureAddDialogEnv( void )
{
    mainWindowFactory.RegisterDependantStructPlugin <texAddDialogEnv> ();
}

TexAddDialog::TexAddDialog(MainWindow *mainWnd, const dialogCreateParams& create_params, TexAddDialog::operationCallback_t cb) : QDialog(mainWnd), impMeth( this )
{
    this->mainWnd = mainWnd;

    this->isConstructing = true;

    this->dialog_type = create_params.type;

    this->cb = cb;

    this->setAttribute(Qt::WA_DeleteOnClose);
    this->setWindowModality(Qt::WindowModality::WindowModal);

    this->setWindowFlags( this->windowFlags() & ~Qt::WindowContextHelpButtonHint );

    // Create a raster handle that will hold platform original data.
    this->platformOrigRaster = nullptr;
    this->texHandle = nullptr;
    this->convRaster = nullptr;

    if (this->dialog_type == CREATE_IMGPATH)
    {
        QString imgPath = create_params.img_path.imgPath;

        // Determine what kind of path we have and deduce what the user expects it to be.
        // This way we can determine what messages the user should receive and when.
        {
            std::wstring wImgPath = imgPath.toStdWString();

            filePath extension;

            FileSystem::GetFileNameItem <FileSysCommonAllocator> ( wImgPath.c_str(), true, nullptr, &extension );

            this->img_exp = getRecommendedImageImportExpectation( extension );
        }

        // We want to load the raster on demand.
        this->platformOrigRaster = nullptr;

        this->imgPath = std::move( imgPath );
    }
    else if (this->dialog_type == CREATE_RASTER)
    {
        this->platformOrigRaster = rw::AcquireRaster(create_params.orig_raster.tex->GetRaster());
    }

    this->enableOriginal = true;
    this->enableRawRaster = true;
    this->enableCompressSelect = true;
    this->enablePaletteSelect = true;
    this->enablePixelFormatSelect = true;

    this->hasConfidentPlatform = false;

    this->wantsGoodPlatformSetting = true;

    // Calculate an appropriate texture name.
    QString textureBaseName;
    QString textureMaskName;

    if (this->dialog_type == CREATE_IMGPATH)
    {
        textureBaseName = calculateImageBaseName(this->imgPath);

        // screw mask name.
    }
    else if (this->dialog_type == CREATE_RASTER)
    {
        textureBaseName = ansi_to_qt(create_params.orig_raster.tex->GetName());
        textureMaskName = ansi_to_qt(create_params.orig_raster.tex->GetMaskName());
    }

    if (const QString *overwriteTexName = create_params.overwriteTexName)
    {
        textureBaseName = *overwriteTexName;
    }

    this->setWindowTitle(MAGIC_TEXT(create_params.actionDesc));

    QString curPlatformText;

    // Create our GUI interface.
    MagicLayout<QHBoxLayout> layout;
    layout.root->setAlignment(Qt::AlignTop);

    QVBoxLayout *leftPanelLayout = new QVBoxLayout();
    leftPanelLayout->setAlignment(Qt::AlignTop);
    { // Top Left (platform options)
      //QWidget *leftTopWidget = new QWidget();
        { // Names and Platform
            texture_name_validator *texNameValid = new texture_name_validator( this );

            QFormLayout *leftTopLayout = new QFormLayout();
            MagicLineEdit *texNameEdit = new MagicLineEdit(textureBaseName);
            texNameEdit->setMaxLength(_recommendedPlatformMaxName);
            //texNameEdit->setFixedWidth(LEFTPANELADDDIALOGWIDTH);
            texNameEdit->setFixedHeight(texNameEdit->sizeHint().height());
            texNameEdit->setValidator( texNameValid );
            this->textureNameEdit = texNameEdit;
            leftTopLayout->addRow(CreateLabelL("Modify.TexName"), texNameEdit);
            if (_enableMaskName)
            {
                MagicLineEdit *texMaskNameEdit = new MagicLineEdit(textureMaskName);
                //texMaskNameEdit->setFixedWidth(LEFTPANELADDDIALOGWIDTH);
                texMaskNameEdit->setFixedHeight(texMaskNameEdit->sizeHint().height());
                texMaskNameEdit->setMaxLength(_recommendedPlatformMaxName);
                texMaskNameEdit->setValidator( texNameValid );
                leftTopLayout->addRow(CreateLabelL("Modify.MskName"), texMaskNameEdit);
                this->textureMaskNameEdit = texMaskNameEdit;
            }
            else
            {
                this->textureMaskNameEdit = nullptr;
            }
            // If the current TXD already has a platform, we disable editing this platform and simply use it.
            bool lockdownPlatform = ( _lockdownPlatform && mainWnd->lockDownTXDPlatform );

            QString currentForcedPlatform = mainWnd->GetCurrentPlatform();

            this->hasConfidentPlatform = ( !currentForcedPlatform.isEmpty() );

            QWidget *platformDisplayWidget;
            if (lockdownPlatform == false || currentForcedPlatform.isEmpty())
            {
                QComboBox *platformComboBox = createPlatformSelectComboBox(mainWnd);
                //platformComboBox->setFixedWidth(LEFTPANELADDDIALOGWIDTH);

                connect(platformComboBox, (void (QComboBox::*)(const QString&))&QComboBox::activated, this, &TexAddDialog::OnPlatformSelect);

                platformDisplayWidget = platformComboBox;
                if (!currentForcedPlatform.isEmpty())
                {
                    platformComboBox->setCurrentText(currentForcedPlatform);
                }
                curPlatformText = platformComboBox->currentText();
            }
            else
            {
                // We do not want to allow editing.
                MagicLineEdit *platformDisplayEdit = new MagicLineEdit();
                //platformDisplayEdit->setFixedWidth(LEFTPANELADDDIALOGWIDTH);
                platformDisplayEdit->setDisabled(true);

                if ( currentForcedPlatform != NULL )
                {
                    platformDisplayEdit->setText( currentForcedPlatform );
                }

                platformDisplayWidget = platformDisplayEdit;
                curPlatformText = platformDisplayEdit->text();
            }
            this->platformSelectWidget = platformDisplayWidget;
            leftTopLayout->addRow(CreateLabelL("Modify.Plat"), platformDisplayWidget);

            this->platformHeaderLabel = CreateLabelL("Modify.RasFmt");

            leftTopLayout->addRow(platformHeaderLabel);

            //leftTopWidget->setLayout(leftTopLayout);
            //leftTopWidget->setObjectName("background_1");
            leftPanelLayout->addLayout(leftTopLayout);

        }
        //leftPanelLayout->addWidget(leftTopWidget);

        QFormLayout *groupContentFormLayout = new QFormLayout();
        { // Platform properties

            this->platformPropForm = groupContentFormLayout;
            QRadioButton *origRasterToggle = CreateRadioButtonL("Modify.Origin");

            connect(origRasterToggle, &QRadioButton::toggled, this, &TexAddDialog::OnPlatformFormatTypeToggle);

            this->platformOriginalToggle = origRasterToggle;
            groupContentFormLayout->addRow(origRasterToggle);
            QRadioButton *rawRasterToggle = CreateRadioButtonL("Modify.RawRas");
            this->platformRawRasterToggle = rawRasterToggle;
            rawRasterToggle->setChecked(true);

            connect(rawRasterToggle, &QRadioButton::toggled, this, &TexAddDialog::OnPlatformFormatTypeToggle);

            groupContentFormLayout->addRow(rawRasterToggle);
            this->platformRawRasterProp = rawRasterToggle;
            QRadioButton *compressionFormatToggle = CreateRadioButtonL("Modify.Comp");
            this->platformCompressionToggle = compressionFormatToggle;

            connect(compressionFormatToggle, &QRadioButton::toggled, this, &TexAddDialog::OnPlatformFormatTypeToggle);

            QComboBox *compressionFormatSelect = new QComboBox();
            //compressionFormatSelect->setFixedWidth(LEFTPANELADDDIALOGWIDTH - 18);

            connect(compressionFormatSelect, (void (QComboBox::*)(const QString&))&QComboBox::activated, this, &TexAddDialog::OnTextureCompressionSeelct);

            groupContentFormLayout->addRow(compressionFormatToggle, compressionFormatSelect);
            this->platformCompressionSelectProp = compressionFormatSelect;
            QRadioButton *paletteFormatToggle = CreateRadioButtonL("Modify.Pal");
            this->platformPaletteToggle = paletteFormatToggle;

            connect(paletteFormatToggle, &QRadioButton::toggled, this, &TexAddDialog::OnPlatformFormatTypeToggle);

            QComboBox *paletteFormatSelect = new QComboBox();
            //paletteFormatSelect->setFixedWidth(LEFTPANELADDDIALOGWIDTH - 18);
            paletteFormatSelect->addItem("PAL4");
            paletteFormatSelect->addItem("PAL8");

            connect(paletteFormatSelect, (void (QComboBox::*)(const QString&))&QComboBox::activated, this, &TexAddDialog::OnTexturePaletteTypeSelect);

            groupContentFormLayout->addRow(paletteFormatToggle, paletteFormatSelect);
            this->platformPaletteSelectProp = paletteFormatSelect;
            QComboBox *pixelFormatSelect = new QComboBox();
            //pixelFormatSelect->setFixedWidth(LEFTPANELADDDIALOGWIDTH - 18);

            // TODO: add API to fetch actually supported raster formats for a native texture.
            // even though RenderWare may have added a bunch of raster formats, the native textures
            // are completely liberal in inplementing any or not.
            pixelFormatSelect->addItem(rw::GetRasterFormatStandardName(rw::RASTER_1555));
            pixelFormatSelect->addItem(rw::GetRasterFormatStandardName(rw::RASTER_565));
            pixelFormatSelect->addItem(rw::GetRasterFormatStandardName(rw::RASTER_4444));
            pixelFormatSelect->addItem(rw::GetRasterFormatStandardName(rw::RASTER_LUM));
            pixelFormatSelect->addItem(rw::GetRasterFormatStandardName(rw::RASTER_8888));
            pixelFormatSelect->addItem(rw::GetRasterFormatStandardName(rw::RASTER_888));
            pixelFormatSelect->addItem(rw::GetRasterFormatStandardName(rw::RASTER_555));
            pixelFormatSelect->addItem(rw::GetRasterFormatStandardName(rw::RASTER_LUM_ALPHA));

            connect(pixelFormatSelect, (void (QComboBox::*)(const QString&))&QComboBox::activated, this, &TexAddDialog::OnTexturePixelFormatSelect);

            groupContentFormLayout->addRow(CreateLabelL("Modify.PixFmt"), pixelFormatSelect);
            this->platformPixelFormatSelectProp = pixelFormatSelect;
        }

        leftPanelLayout->addLayout(groupContentFormLayout);

        leftPanelLayout->addSpacing(12);

        { // Add some basic properties that exist no matter what platform.
            QCheckBox *generateMipmapsToggle = CreateCheckBoxL("Modify.GenML");
            generateMipmapsToggle->setChecked( mainWnd->addImageGenMipmaps );

            this->propGenerateMipmaps = generateMipmapsToggle;

            leftPanelLayout->addWidget(generateMipmapsToggle);
        }
    }
    layout.top->addLayout(leftPanelLayout);

    QVBoxLayout *rightPanelLayout = new QVBoxLayout();
    rightPanelLayout->setAlignment(Qt::AlignHCenter);
    { // Top right (preview options, preview image)
        QHBoxLayout *rightTopPanelLayout = new QHBoxLayout();
        scaledPreviewCheckBox = CreateCheckBoxL("Modify.Scaled");
        scaledPreviewCheckBox->setChecked(true);

        connect(scaledPreviewCheckBox, &QCheckBox::stateChanged, this, &TexAddDialog::OnScalePreviewStateChanged);

        fillPreviewCheckBox = CreateCheckBoxL("Modify.Fill");

        connect(fillPreviewCheckBox, &QCheckBox::stateChanged, this, &TexAddDialog::OnFillPreviewStateChanged);

        backgroundForPreviewCheckBox = CreateCheckBoxL("Modify.Bckgr");

        connect(backgroundForPreviewCheckBox, &QCheckBox::stateChanged, this, &TexAddDialog::OnPreviewBackgroundStateChanged);

        rightTopPanelLayout->addWidget(scaledPreviewCheckBox);
        rightTopPanelLayout->addWidget(fillPreviewCheckBox);
        rightTopPanelLayout->addWidget(backgroundForPreviewCheckBox);
        rightPanelLayout->addLayout(rightTopPanelLayout);

        this->previewScrollArea = new QScrollArea;
        this->previewScrollArea->setFrameShape(QFrame::NoFrame);
        this->previewScrollArea->setObjectName("background_2");
        this->previewLabel = new QLabel();
        this->previewLabel->setStyleSheet("background-color: rgba(255, 255, 255, 0);");
        this->previewScrollArea->setWidget(this->previewLabel);
        this->previewScrollArea->setAlignment(Qt::AlignCenter);
        this->previewScrollArea->setFixedSize(300, 300);

        rightPanelLayout->addWidget(this->previewScrollArea);

        this->previewInfoLabel = new QLabel();
        rightPanelLayout->addWidget(this->previewInfoLabel);

        rightPanelLayout->setAlignment(rightTopPanelLayout, Qt::AlignHCenter);
        rightPanelLayout->setAlignment(this->previewScrollArea, Qt::AlignHCenter);
        rightPanelLayout->setAlignment(this->previewInfoLabel, Qt::AlignHCenter);
    }
    layout.top->addLayout(rightPanelLayout);

    // Add control buttons at the bottom.
    QPushButton *cancelButton = CreateButtonL("Modify.Cancel");
    this->cancelButton = cancelButton;

    connect(cancelButton, &QPushButton::clicked, this, &TexAddDialog::OnCloseRequest);

    layout.bottom->addWidget(cancelButton);
    QPushButton *addButton = CreateButtonL(create_params.actionName);
    this->applyButton = addButton;

    connect(addButton, &QPushButton::clicked, this, &TexAddDialog::OnTextureAddRequest);

    layout.bottom->addWidget(addButton);

    this->setLayout(layout.root);

    // Do initial stuff.
    {
        if (curPlatformText.isEmpty() == false)
        {
            this->OnPlatformSelect(curPlatformText);
        }

        // Set focus on the apply button, so users can quickly add textures.
        this->applyButton->setDefault( true );

        // Setup the preview.
        this->scaledPreviewCheckBox->setChecked( mainWnd->texaddViewportScaled );
        this->fillPreviewCheckBox->setChecked( mainWnd->texaddViewportFill );
        this->backgroundForPreviewCheckBox->setChecked( mainWnd->texaddViewportBackground );
    }

    this->isConstructing = false;
}

TexAddDialog::~TexAddDialog(void)
{
    // Remove the raster that we created.
    // Remember that it is reference counted.
    this->clearTextureOriginal();

    this->releaseConvRaster();

    // Remember properties that count for any raster format.
    this->mainWnd->addImageGenMipmaps = this->propGenerateMipmaps->isChecked();
}

void TexAddDialog::UpdatePreview() {
    rw::Raster *previewRaster = this->GetDisplayRaster();
    if (previewRaster) {
        try {
            int w, h;
            {
                // Put the contents of the platform original into the preview widget.
                // We want to transform the raster into a bitmap, basically.
                QPixmap pixmap = convertRWBitmapToQPixmap( previewRaster->getBitmap() );

                w = pixmap.width(), h = pixmap.height();

                this->previewLabel->setPixmap(pixmap);
            }

            if (scaledPreviewCheckBox->isChecked()) {
                int maxLen = w > h ? w : h;
                if (maxLen > 300 || fillPreviewCheckBox->isChecked()) {
                    float factor = 300.0f / maxLen;
                    w = (float)w * factor;
                    h = (float)h * factor;
                }
                this->previewLabel->setScaledContents(true);
            }
            else
                this->previewLabel->setScaledContents(false);
            this->previewLabel->setFixedSize(w, h);
        }
        catch (rw::RwException& except) {
            this->mainWnd->txdLog->showError(QString("failed to create preview: ") + ansi_to_qt(except.message));
            this->ClearPreview();
            // Continue normal execution.
        }
    }
    else
        ClearPreview();
}

void TexAddDialog::ClearPreview() {
    this->previewLabel->clear();
    this->previewLabel->setFixedSize(300, 300);
}

void TexAddDialog::UpdateAccessability(void)
{
    // We have to disable or enable certain platform property selectable fields if you toggle a certain format type.
    // This is to guide the user into the right direction, that a native texture cannot have multiple format types at once.

    bool wantsPixelFormatAccess = false;
    bool wantsCompressionAccess = false;
    bool wantsPaletteAccess = false;

    if (this->platformOriginalToggle->isChecked())
    {
        // We want nothing.
    }
    else if (this->platformRawRasterToggle->isChecked())
    {
        wantsPixelFormatAccess = true;
    }
    else if (this->platformCompressionToggle->isChecked())
    {
        wantsCompressionAccess = true;
    }
    else if (this->platformPaletteToggle->isChecked())
    {
        wantsPixelFormatAccess = true;
        wantsPaletteAccess = true;
    }

    // Now disable or enable stuff.
    this->platformPixelFormatSelectProp->setDisabled(!wantsPixelFormatAccess);
    this->platformCompressionSelectProp->setDisabled(!wantsCompressionAccess);
    this->platformPaletteSelectProp->setDisabled(!wantsPaletteAccess);

    // TODO: maybe clear combo boxes aswell?
}

void TexAddDialog::OnPlatformFormatTypeToggle(bool checked)
{
    if (checked != true)
        return;

    // Depending on the thing we clicked, we want to send some help text.
    if ( !this->isConstructing )
    {
        QObject *clickedOn = sender();

        if ( clickedOn == this->platformCompressionToggle )
        {
            TriggerHelperWidget( this->mainWnd, "dxt_warning", this );
        }
        else if ( clickedOn == this->platformPaletteToggle )
        {
            TriggerHelperWidget( this->mainWnd, "pal_warning", this );
        }
    }

    // Since we switched the platform format type, we have to adjust the accessability.
    // The accessability change must not swap items around on the GUI. Rather it should
    // disable items that make no sense.
    this->UpdateAccessability();

    // Since we switched the format type, the texture encoding has changed.
    // Update the preview.
    this->createRasterForConfiguration();
}

void TexAddDialog::OnTextureCompressionSeelct(const QString& newCompression)
{
    this->createRasterForConfiguration();
}

void TexAddDialog::OnTexturePaletteTypeSelect(const QString& newPaletteType)
{
    this->createRasterForConfiguration();
}

void TexAddDialog::OnTexturePixelFormatSelect(const QString& newPixelFormat)
{
    this->createRasterForConfiguration();
}

void TexAddDialog::OnPlatformSelect(const QString& _)
{
    // Update what options make sense to the user.
    this->UpdateAccessability();

    // Reload the preview image with what the platform wants us to see.
    this->loadPlatformOriginal();   // ALLOWED TO CHANGE THE PLATFORM.

    QString newText = this->GetCurrentPlatform();

    // We want to show the user properties based on what this platform supports.
    // So we fill the fields.

    bool hasPreview = this->hasPlatformOriginal;

    std::string ansiNativeName = qt_to_ansi( newText );

    rw::nativeRasterFormatInfo formatInfo;

    // Decide what to do.
    bool enableOriginal = true;
    bool enableRawRaster = true;
    bool enableCompressSelect = false;
    bool enablePaletteSelect = false;
    bool enablePixelFormatSelect = true;

    bool supportsDXT1 = true;
    bool supportsDXT2 = true;
    bool supportsDXT3 = true;
    bool supportsDXT4 = true;
    bool supportsDXT5 = true;

    if ( hasPreview )
    {
        bool gotFormatInfo = rw::GetNativeTextureFormatInfo(this->mainWnd->rwEngine, ansiNativeName.c_str(), formatInfo);

        if (gotFormatInfo)
        {
            if (formatInfo.isCompressedFormat)
            {
                // We are a fixed compressed format, so we will pass pixels with high quality to the pipeline.
                enableRawRaster = false;
                enableCompressSelect = true;    // decide later.
                enablePaletteSelect = false;
                enablePixelFormatSelect = false;
            }
            else
            {
                // We are a dynamic raster, so whatever goes best.
                enableRawRaster = true;
                enableCompressSelect = true;    // we decide this later again.
                enablePaletteSelect = formatInfo.supportsPalette;
                enablePixelFormatSelect = true;
            }

            supportsDXT1 = formatInfo.supportsDXT1;
            supportsDXT2 = formatInfo.supportsDXT2;
            supportsDXT3 = formatInfo.supportsDXT3;
            supportsDXT4 = formatInfo.supportsDXT4;
            supportsDXT5 = formatInfo.supportsDXT5;
        }
    }
    else
    {
        // If there is no preview, we want nothing.
        enableOriginal = false;
        enableRawRaster = false;
        enableCompressSelect = false;
        enablePaletteSelect = false;
        enablePixelFormatSelect = false;
    }

    // Decide whether enabling the compression select even makes sense.
    // If we have no compression supported, then it makes no sense.
    if (enableCompressSelect)
    {
        enableCompressSelect =
            (supportsDXT1 || supportsDXT2 || supportsDXT3 || supportsDXT4 || supportsDXT5);
    }

    // Do stuff.
    this->platformOriginalToggle->setVisible(enableOriginal);

    if (QWidget *partnerWidget = this->platformPropForm->labelForField(this->platformOriginalToggle))
    {
        partnerWidget->setVisible(enableOriginal);
    }

    this->platformRawRasterProp->setVisible(enableRawRaster);

    if (QWidget *partnerWidget = this->platformPropForm->labelForField(this->platformRawRasterProp))
    {
        partnerWidget->setVisible(enableRawRaster);
    }

    this->platformCompressionSelectProp->setVisible(enableCompressSelect);

    if (QWidget *partnerWidget = this->platformPropForm->labelForField(this->platformCompressionSelectProp))
    {
        partnerWidget->setVisible(enableCompressSelect);
    }

    this->platformPaletteSelectProp->setVisible(enablePaletteSelect);

    if (QWidget *partnerWidget = this->platformPropForm->labelForField(this->platformPaletteSelectProp))
    {
        partnerWidget->setVisible(enablePaletteSelect);
    }

    this->platformPixelFormatSelectProp->setVisible(enablePixelFormatSelect);

    if (QWidget *partnerWidget = this->platformPropForm->labelForField(this->platformPixelFormatSelectProp))
    {
        partnerWidget->setVisible(enablePixelFormatSelect);
    }

    // If no option is visible, hide the label.
    bool shouldHideLabel = ( !enableOriginal && !enableRawRaster && !enableCompressSelect && !enablePaletteSelect && !enablePixelFormatSelect );

    this->platformHeaderLabel->setVisible( !shouldHideLabel );

    this->enableOriginal = enableOriginal;
    this->enableRawRaster = enableRawRaster;
    this->enableCompressSelect = enableCompressSelect;
    this->enablePaletteSelect = enablePaletteSelect;
    this->enablePixelFormatSelect = enablePixelFormatSelect;

    // Fill in fields depending on capabilities.
    if (enableCompressSelect)
    {
        QString currentText = this->platformCompressionSelectProp->currentText();

        this->platformCompressionSelectProp->clear();

        if (supportsDXT1)
        {
            this->platformCompressionSelectProp->addItem("DXT1");
        }

        if (supportsDXT2)
        {
            this->platformCompressionSelectProp->addItem("DXT2");
        }

        if (supportsDXT3)
        {
            this->platformCompressionSelectProp->addItem("DXT3");
        }

        if (supportsDXT4)
        {
            this->platformCompressionSelectProp->addItem("DXT4");
        }

        if (supportsDXT5)
        {
            this->platformCompressionSelectProp->addItem("DXT5");
        }

        this->platformCompressionSelectProp->setCurrentText( currentText );
    }

    // If none of the visible toggles are selected, select the first visible toggle.
    bool anyToggle = false;

    if (this->platformRawRasterToggle->isVisible() && this->platformRawRasterToggle->isChecked())
    {
        anyToggle = true;
    }

    if (!anyToggle)
    {
        if (this->platformCompressionToggle->isVisible() && this->platformCompressionToggle->isChecked())
        {
            anyToggle = true;
        }
    }

    if (!anyToggle)
    {
        if (this->platformPaletteToggle->isVisible() && this->platformPaletteToggle->isChecked())
        {
            anyToggle = true;
        }
    }

    if (!anyToggle)
    {
        if (this->platformOriginalToggle->isVisible() && this->platformOriginalToggle->isChecked())
        {
            anyToggle = true;
        }
    }

    // Now select the first one if we still have no selected toggle.
    if (!anyToggle)
    {
        bool selectedToggle = false;

        if (!selectedToggle)
        {
            if ( this->platformOriginalToggle->isVisible() )
            {
                this->platformOriginalToggle->setChecked( true );

                selectedToggle = true;
            }
        }

        if (!selectedToggle)
        {
            if (this->platformRawRasterToggle->isVisible())
            {
                this->platformRawRasterToggle->setChecked(true);

                selectedToggle = true;
            }
        }

        if (!selectedToggle)
        {
            if (this->platformCompressionToggle->isVisible())
            {
                this->platformCompressionToggle->setChecked(true);

                selectedToggle = true;
            }
        }

        if (!selectedToggle)
        {
            if (this->platformPaletteToggle->isVisible())
            {
                this->platformPaletteToggle->setChecked(true);

                selectedToggle = true;
            }
        }

        if (!selectedToggle)
        {
            if (this->platformOriginalToggle->isVisible())
            {
                this->platformOriginalToggle->setChecked(true);

                selectedToggle = true;
            }
        }

        // Well, we do not _have_ to select one.
    }

    // Raster settings update.
    {
        rw::Raster *origRaster = this->platformOrigRaster;

        // The user wants to know about the original raster format, so display an info string.
        if (hasPreview)
        {
            this->previewInfoLabel->setVisible( true );
            this->previewInfoLabel->setText( TexInfoWidget::getDefaultRasterInfoString( origRaster ) );
        }
        else
        {
            this->previewInfoLabel->setVisible( false );
        }

        // If we still want a good start setting, we can now determine it.
        if (hasPreview && this->wantsGoodPlatformSetting)
        {
            // Initially set the configuration that is best for the image.
            // This is what the user normally wants anyway.
            bool hasSet = false;

            if (origRaster->isCompressed())
            {
                // If the raster is DXT compression, we can select it.
                rw::eCompressionType comprType = origRaster->getCompressionFormat();

                bool hasFound = false;

                if (comprType == rw::RWCOMPRESS_DXT1)
                {
                    this->platformCompressionSelectProp->setCurrentText("DXT1");

                    hasFound = true;
                }
                else if (comprType == rw::RWCOMPRESS_DXT2)
                {
                    this->platformCompressionSelectProp->setCurrentText("DXT2");

                    hasFound = true;
                }
                else if (comprType == rw::RWCOMPRESS_DXT3)
                {
                    this->platformCompressionSelectProp->setCurrentText("DXT3");

                    hasFound = true;
                }
                else if (comprType == rw::RWCOMPRESS_DXT4)
                {
                    this->platformCompressionSelectProp->setCurrentText("DXT4");

                    hasFound = true;
                }
                else if (comprType == rw::RWCOMPRESS_DXT5)
                {
                    this->platformCompressionSelectProp->setCurrentText("DXT5");

                    hasFound = true;
                }

                if (hasFound)
                {
                    this->platformCompressionToggle->setChecked(true);

                    hasSet = true;
                }
            }
            else
            {
                // Set palette type and raster format, if available.
                {
                    rw::ePaletteType paletteType = origRaster->getPaletteType();

                    bool hasFound = false;

                    if (paletteType == rw::PALETTE_4BIT || paletteType == rw::PALETTE_4BIT_LSB)
                    {
                        this->platformPaletteSelectProp->setCurrentText("PAL4");

                        hasFound = true;
                    }
                    else if (paletteType == rw::PALETTE_8BIT)
                    {
                        this->platformPaletteSelectProp->setCurrentText("PAL8");

                        hasFound = true;
                    }

                    if (hasFound && !hasSet)
                    {
                        this->platformPaletteToggle->setChecked(true);

                        hasSet = true;
                    }
                }

                // Now raster format.
                {
                    rw::eRasterFormat rasterFormat = origRaster->getRasterFormat();

                    if (rasterFormat != rw::RASTER_DEFAULT)
                    {
                        this->platformPixelFormatSelectProp->setCurrentText(
                            rw::GetRasterFormatStandardName(rasterFormat)
                            );

                        if (!hasSet)
                        {
                            this->platformRawRasterToggle->setChecked(true);

                            hasSet = true;
                        }
                    }
                }
            }

            // If nothing was select, we are best off original.
            if (!hasSet)
            {
                this->platformOriginalToggle->setChecked(true);
            }

            // Done.
            this->wantsGoodPlatformSetting = false;
        }
    }

    // We want to create a raster special to the configuration.
    this->createRasterForConfiguration();
}

void TexAddDialog::OnTextureAddRequest(bool checked)
{
    // This is where we want to go.
    // Decide the format that the runtime has requested.

    rw::Raster *displayRaster = this->GetDisplayRaster();

    if (displayRaster)
    {
        texAddOperation desc;

        // We can either add a raster or a texture chunk.
        rw::TextureBase *texHandle = this->texHandle;

        std::string texName = qt_to_ansi( this->textureNameEdit->text() );
        std::string maskName;

        if ( this->textureMaskNameEdit )
        {
            maskName = qt_to_ansi( this->textureMaskNameEdit->text() );
        }

        // Maybe generate mipmaps.
        if (this->propGenerateMipmaps->isChecked())
        {
            try
            {
                displayRaster->generateMipmaps( 0xFFFFFFFF, rw::MIPMAPGEN_DEFAULT );
            }
            catch( rw::RwException& )
            {
                // We do not have to be able to generate mipmaps.
            }
        }

        if ( texHandle != NULL )
        {
            // Initialize the texture handle.
            RwTextureAssignNewRaster( texHandle, displayRaster, texName, maskName );

            desc.add_texture.texHandle = texHandle;

            desc.add_type = texAddOperation::ADD_TEXCHUNK;
        }
        else
        {
            desc.add_raster.texName = std::move( texName );
            desc.add_raster.maskName = std::move( maskName );

            desc.add_raster.raster = displayRaster;

            desc.add_type = texAddOperation::ADD_RASTER;
        }

        this->cb(desc);
    }

    // Close ourselves.
    this->close();
}

void TexAddDialog::OnCloseRequest(bool checked)
{
    // We want to save some persistence related configurations.
    {
        mainWnd->texaddViewportScaled = this->scaledPreviewCheckBox->isChecked();
        mainWnd->texaddViewportFill = this->fillPreviewCheckBox->isChecked();
        mainWnd->texaddViewportBackground = this->backgroundForPreviewCheckBox->isChecked();
    }

    // The user doesnt want to do it anymore.
    this->close();
}

void TexAddDialog::OnPreviewBackgroundStateChanged(int state) {
    if (state == Qt::Unchecked) {
        this->previewLabel->setStyleSheet("background-color: rgba(255, 255, 255, 0);");
    }
    else {
        this->previewLabel->setStyleSheet("background-image: url(\"" + mainWnd->m_appPathForStyleSheet + "/resources/viewBackground.png\");");
    }
}

void TexAddDialog::OnScalePreviewStateChanged(int state) {
    if (state == Qt::Unchecked && this->fillPreviewCheckBox->isChecked()) {
        this->fillPreviewCheckBox->setChecked(false);
    }
    else {
        rw::Raster *previewRaster = this->GetDisplayRaster();
        if (previewRaster) {
            rw::uint32 w, h;
            previewRaster->getSize(w, h);
            if (state == Qt::Unchecked) {
                this->previewLabel->setFixedSize(w, h);
                this->previewLabel->setScaledContents(false);
            }
            else {
                int maxLen = w > h ? w : h;
                if (maxLen > 300 || this->fillPreviewCheckBox->isChecked()) {
                    float factor = 300.0f / maxLen;
                    w = (float)w * factor;
                    h = (float)h * factor;
                }
                this->previewLabel->setFixedSize(w, h);
                this->previewLabel->setScaledContents(true);
            }
        }
    }
}

void TexAddDialog::OnFillPreviewStateChanged(int state) {
    if (state == Qt::Checked && !this->scaledPreviewCheckBox->isChecked()) {
        this->scaledPreviewCheckBox->setChecked(true);
    }
    else {
        rw::Raster *previewRaster = this->GetDisplayRaster();
        if (previewRaster) {
            rw::uint32 w, h;
            previewRaster->getSize(w, h);
            if (!this->scaledPreviewCheckBox->isChecked()) {
                this->previewLabel->setFixedSize(w, h);
                this->previewLabel->setScaledContents(false);
            }
            else {
                int maxLen = w > h ? w : h;
                if (maxLen > 300 || state == Qt::Checked) {
                    float factor = 300.0f / maxLen;
                    w = (float)w * factor;
                    h = (float)h * factor;
                }
                this->previewLabel->setFixedSize(w, h);
                this->previewLabel->setScaledContents(true);
            }
        }
    }
}
