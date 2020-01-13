#pragma once

#include <renderware.h>

#include <QtWidgets/QDialog>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>

#include "qtutils.h"
#include "languages.h"

#include <sdk/UniChar.h>

// We want to have a simple window which can be used by the user to export all textures of a TXD.

struct ExportAllWindow : public QDialog, public magicTextLocalizationItem
{
private:
    inline static rw::rwStaticVector <rw::rwStaticString <char>> GetAllSupportedImageFormats( rw::TexDictionary *texDict )
    {
        rw::rwStaticVector <rw::rwStaticString <char>> formatsOut;

        rw::Interface *engineInterface = texDict->GetEngine();

        // The only formats that we question support for are native formats.
        // Currently, each native texture can export one native format.
        // Hence the only supported formats can be formats that are supported from the first texture on.
        bool isFirstRaster = true;

        for ( rw::TexDictionary::texIter_t iter( texDict->GetTextureIterator() ); !iter.IsEnd(); iter.Increment() )
        {
            rw::TextureBase *texture = iter.Resolve();

            rw::Raster *texRaster = texture->GetRaster();

            if ( texRaster )
            {
                // If we are the first raster we encounter, we add all of its formats to our list.
                // Otherwise we check for support on each raster until no more support is guarranteed.
                rw::nativeImageRasterResults_t reg_natTex;

                const char *nativeName = texRaster->getNativeDataTypeName();

                rw::GetNativeImageTypesForNativeTexture( engineInterface, nativeName, reg_natTex );

                if ( isFirstRaster )
                {
                    size_t numFormats = reg_natTex.GetCount();

                    for ( size_t n = 0; n < numFormats; n++ )
                    {
                        formatsOut.AddToBack( reg_natTex[n] );
                    }

                    isFirstRaster = false;
                }
                else
                {
                    // If we have no more formats, we quit.
                    if ( formatsOut.GetCount() == 0 )
                    {
                        break;
                    }

                    // Remove anything thats not part of the supported.
                    rw::rwStaticVector <rw::rwStaticString <char>> newList;

                    for ( const auto& nativeFormat : reg_natTex )
                    {
                        for ( const auto& formstr : formatsOut )
                        {
                            if ( formstr == nativeFormat )
                            {
                                newList.AddToBack( nativeFormat );
                                break;
                            }
                        }
                    }

                    formatsOut = std::move( newList );
                }
            }
        }

        // After that come the formats that every raster supports anyway.
        rw::registered_image_formats_t availImageFormats;

        rw::GetRegisteredImageFormats( engineInterface, availImageFormats );

        for ( const rw::registered_image_format& format : availImageFormats )
        {
            const char *defaultExt = nullptr;

            bool gotDefaultExt = rw::GetDefaultImagingFormatExtension( format.num_ext, format.ext_array, defaultExt );

            if ( gotDefaultExt )
            {
                formatsOut.AddToBack( defaultExt );
            }
        }

        // And of course, the texture chunk, that is always supported.
        formatsOut.AddToBack( "RWTEX" );

        return formatsOut;
    }

public:
    ExportAllWindow( MainWindow *mainWnd, rw::TexDictionary *texDict ) : QDialog( mainWnd )
    {
        this->mainWnd = mainWnd;
        this->texDict = texDict;
        setWindowFlags( windowFlags() & ~Qt::WindowContextHelpButtonHint );
        setWindowModality( Qt::WindowModal );
        this->setAttribute( Qt::WA_DeleteOnClose );
        // Basic window with a combo box and a button to accept.
        MagicLayout<QFormLayout> layout(this);

        QComboBox *formatSelBox = new QComboBox();
        // List formats that are supported by every raster in a TXD.
        {
            auto formatsSupported = GetAllSupportedImageFormats( texDict );
            for ( const auto& format : formatsSupported )
            {
                formatSelBox->addItem( ansi_to_qt( format ) );
            }
        }

        // Select the last used format, if it exists.
        {
            int foundLastUsed = formatSelBox->findText( ansi_to_qt( mainWnd->lastUsedAllExportFormat ), Qt::MatchExactly );
            if ( foundLastUsed != -1 )
                formatSelBox->setCurrentIndex( foundLastUsed );
        }
        this->formatSelBox = formatSelBox;
        layout.top->addRow(CreateLabelL( "Main.ExpAll.Format" ), formatSelBox);

        // Add the button row last.
        QPushButton *buttonExport = CreateButtonL( "Main.ExpAll.Export" );

        connect( buttonExport, &QPushButton::clicked, this, &ExportAllWindow::OnRequestExport );

        layout.bottom->addWidget( buttonExport );
        QPushButton *buttonCancel = CreateButtonL( "Main.ExpAll.Cancel" );

        connect( buttonCancel, &QPushButton::clicked, this, &ExportAllWindow::OnRequestCancel );

        layout.bottom->addWidget( buttonCancel );
        this->setMinimumWidth( 250 );

        RegisterTextLocalizationItem( this );
    }

    ~ExportAllWindow( void )
    {
        UnregisterTextLocalizationItem( this );
    }

    void updateContent( MainWindow *mainWnd ) override
    {
        this->setWindowTitle( getLanguageItemByKey( "Main.ExpAll.Desc" ) );
    }

public slots:
    void OnRequestExport( bool checked )
    {
        bool shouldClose = false;

        rw::TexDictionary *texDict = this->texDict;

        rw::Interface *engineInterface = texDict->GetEngine();


                        // If we successfully did any export, we can close.
                        bool hasExportedAnything = false;

                        for ( rw::TexDictionary::texIter_t iter( texDict->GetTextureIterator() ); !iter.IsEnd(); iter.Increment() )
                        {
                            rw::TextureBase *texture = iter.Resolve();

                            // We can only serialize if we have a raster.
                            rw::Raster *texRaster = texture->GetRaster();

                            if ( texRaster )
                            {
								rw::uint32 curWidth = 0;
								rw::uint32 curHeight = 0;
								// CALCULATE RULE BASED HERE
								texRaster->getSize(curWidth, curHeight);
								MagicLineEdit* widthEdit;
								MagicLineEdit* heightEdit;
								//1:1 matching size ALL APPLY RULES
								if (curWidth == curHeight) {
									// only resize larger than 16x16
									if (curWidth > 16) {
										if (curWidth > 64) {
											//128++ only resize to 32
											widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(32)));
											heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(32)));
										}
										else {
											widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(16)));
											heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(16)));
										}
									}
									else {
										//leave 16x16 and lower alone
										widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curWidth)));
										heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curHeight)));
									}
								}
								else {
									// RATIO MISMATCHING case
									if (curWidth > 2 && curHeight > 2) {
										if (curWidth == 4 || curHeight == 4) {
											//4xY/Xx4 case, half
											widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curWidth / 2)));
											heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curHeight / 2)));
										}
										else {
											// Ratio is larger than 4xY, not expecting anything over 8x*, may need modification later
											widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curWidth / 4)));
											heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curHeight / 4)));
										}
									}
									else {
										// Do not resize any 1:X nor 2:X
										widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curWidth)));
										heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curHeight)));
									}

								}

								/// ENDDD

								// Fetch the sizes (with minimal validation).
								QString widthDimmString = widthEdit->text();
								QString heightDimmString = heightEdit->text();

								bool validWidth, validHeight;

								int widthDimm = widthDimmString.toInt(&validWidth);
								int heightDimm = heightDimmString.toInt(&validHeight);

								if (validWidth && validHeight)
								{
									// Resize!
									rw::uint32 rwWidth = (rw::uint32)widthDimm;
									rw::uint32 rwHeight = (rw::uint32)heightDimm;

									bool success = false;

									try
									{
										// Use default filters.
										texRaster->resize(rwWidth, rwHeight);
										shouldClose = true;
										success = true;
									}
									catch (rw::RwException& except)
									{
										// We should not close the dialog.
										shouldClose = false;
									}

								}
							}
							// We have changed the TXD.
							// END ALL TEXTURE RESIZE

                        }
                        


                

        if ( shouldClose )
        {
            this->close();
			this->mainWnd->NotifyChange();
			this->mainWnd->updateAllTextureMetaInfo();
			this->mainWnd->updateTextureView();

        }
    }

    void OnRequestCancel( bool checked )
    {
        this->close();
    }

private:
    MainWindow *mainWnd;
    rw::TexDictionary *texDict;

    QComboBox *formatSelBox;
};
