#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include "qtutils.h"
#include "languages.h"

struct TexResizeWindow : public QDialog, public magicTextLocalizationItem
{
    inline TexResizeWindow( MainWindow *mainWnd, TexInfoWidget *texInfo ) : QDialog( mainWnd )
    {
        this->setWindowFlags( this->windowFlags() & ~Qt::WindowContextHelpButtonHint );

        this->mainWnd = mainWnd;
        this->texInfo = texInfo;

        this->setWindowModality( Qt::WindowModal );
        this->setAttribute( Qt::WA_DeleteOnClose );

        // Get the current texture dimensions.
        rw::uint32 curWidth = 0;
        rw::uint32 curHeight = 0;

        rw::rasterSizeRules rasterRules;
        {
            if ( texInfo )
            {
                if ( rw::TextureBase *texHandle = texInfo->GetTextureHandle() )
                {
                    if ( rw::Raster *texRaster = texHandle->GetRaster() )
                    {
                        try
                        {
                            // Getting the size may fail if there is no native data.
                            texRaster->getSize( curWidth, curHeight );

                            // Also get the size rules.
                            texRaster->getSizeRules( rasterRules );
                        }
                        catch( rw::RwException& except )
                        {
                            // Got no dimms.
                            curWidth = 0;
                            curHeight = 0;
                        }
                    }
                }
            }
        }

        // We want a basic dialog with two dimension line edits and our typical two buttons.
        MagicLayout<QFormLayout> layout(this);

        // We only want to accept unsigned integers.
        QIntValidator *dimensionValidator = new QIntValidator( 1, ( rasterRules.maximum ? rasterRules.maxVal : 4096 ), this );

		//dream: create rules here
		//CASE: 8x128, 4x64, how to handle?
		//CASE: Matching 1:1 any size, how to handle?

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
				} else {
					widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(16)));
					heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(16)));
				}
			} else {
				//leave 16x16 and lower alone
				widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curWidth)));
				heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curHeight)));
			}
		} else {
			// RATIO MISMATCHING case
			if (curWidth > 2 && curHeight > 2) {
				if (curWidth == 4 || curHeight == 4) {
					//4xY/Xx4 case, half
					widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curWidth/2)));
					heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curHeight/2)));
				} else {
					// Ratio is larger than 4xY, not expecting anything over 8x*, may need modification later
					widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curWidth / 4)));
					heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curHeight / 4)));
				}
			} else {
				// Do not resize any 1:X nor 2:X
				widthEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curWidth)));
				heightEdit = new MagicLineEdit(ansi_to_qt(std::to_string(curHeight)));
			}

		}

		widthEdit->setValidator(dimensionValidator);
		heightEdit->setValidator(dimensionValidator);

        this->widthEdit = widthEdit;
        connect( widthEdit, &QLineEdit::textChanged, this, &TexResizeWindow::OnChangeDimensionProperty );

        this->heightEdit = heightEdit;
        connect( heightEdit, &QLineEdit::textChanged, this, &TexResizeWindow::OnChangeDimensionProperty );

        layout.top->addRow( CreateLabelL( "Main.Resize.Width" ), widthEdit );
        layout.top->addRow( CreateLabelL( "Main.Resize.Height" ), heightEdit );

        // Now the buttons, I guess.
        QPushButton *buttonSet = CreateButtonL( "Main.Resize.Set" );
        layout.bottom->addWidget( buttonSet );

        this->buttonSet = buttonSet;

        connect( buttonSet, &QPushButton::clicked, this, &TexResizeWindow::OnRequestSet );

        QPushButton *buttonCancel = CreateButtonL( "Main.Resize.Cancel" );
        layout.bottom->addWidget( buttonCancel );

        connect( buttonCancel, &QPushButton::clicked, this, &TexResizeWindow::OnRequestCancel );

        // Remember us as the only resize dialog.
        mainWnd->resizeDlg = this;

        // Initialize the dialog.
        this->UpdateAccessibility();

        RegisterTextLocalizationItem( this );
    }

    inline ~TexResizeWindow( void )
    {
        UnregisterTextLocalizationItem( this );

        // There can be only one.
        this->mainWnd->resizeDlg = NULL;
    }

    void updateContent( MainWindow *mainWnd ) override
    {
        this->setWindowTitle( getLanguageItemByKey( "Main.Resize.Desc" ) );
    }

public slots:
    void OnChangeDimensionProperty( const QString& newText )
    {
        this->UpdateAccessibility();
    }

    void OnRequestSet( bool checked )
    {
        // Do the resize.
        bool shouldClose = true;

		// dream: BEGIN ALL TEXTURE RESIZE
		rw::TexDictionary* texDict = this->texDict;
		for (rw::TexDictionary::texIter_t iter(texDict->GetTextureIterator()); !iter.IsEnd(); iter.Increment())
		{
			rw::TextureBase* texture = iter.Resolve();
			// We can only serialize if we have a raster.
			rw::Raster* texRaster = texture->GetRaster();

			// Fetch the sizes (with minimal validation).
			QString widthDimmString = this->widthEdit->text();
			QString heightDimmString = this->heightEdit->text();

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

					success = true;
				}
				catch (rw::RwException& except)
				{
					this->mainWnd->txdLog->showError(QString("failed to resize raster: ") + ansi_to_qt(except.message));

					// We should not close the dialog.
					shouldClose = false;
				}

			}
		}
		// We have changed the TXD.
		this->mainWnd->NotifyChange();

		// Since we succeeded, we should update the view and things.
		this->mainWnd->updateTextureView();

		texInfo->updateInfo();
		// END ALL TEXTURE RESIZE

        if ( shouldClose )
        {
            this->close();
        }
    }

    void OnRequestCancel( bool checked )
    {
        this->close();
    }

private:
    void UpdateAccessibility( void )
    {
        // Only allow setting if we have a width and height, whose values are different from the original.
        bool allowSet = true;

        if ( TexInfoWidget *texInfo = this->texInfo )
        {
            if ( rw::TextureBase *texHandle = texInfo->GetTextureHandle() )
            {
                if ( rw::Raster *texRaster = texHandle->GetRaster() )
                {
                    rw::uint32 curWidth, curHeight;

                    try
                    {
                        texRaster->getSize( curWidth, curHeight );
                    }
                    catch( rw::RwException& )
                    {
                        curWidth = 0;
                        curHeight = 0;
                    }

                    // Check we have dimensions currenctly.
                    bool gotValidDimms = false;

                    QString widthDimmString = this->widthEdit->text();
                    QString heightDimmString = this->heightEdit->text();

                    if ( widthDimmString.isEmpty() == false && heightDimmString.isEmpty() == false )
                    {
                        bool validWidth, validHeight;

                        int widthDimm = widthDimmString.toInt( &validWidth );
                        int heightDimm = heightDimmString.toInt( &validHeight );

                        if ( validWidth && validHeight )
                        {
                            if ( widthDimm > 0 && heightDimm > 0 )
                            {
                                // Also verify whether the native texture can even accept the dimensions.
                                // Otherwise we get a nice red present.
                                rw::rasterSizeRules rasterRules;

                                texRaster->getSizeRules( rasterRules );

                                if ( rasterRules.verifyDimensions( widthDimm, heightDimm ) )
                                {
                                    // Now lets verify that those are different.
                                    rw::uint32 selWidth = (rw::uint32)widthDimm;
                                    rw::uint32 selHeight = (rw::uint32)heightDimm;

                                    if ( selWidth != curWidth || selHeight != curHeight )
                                    {
                                        gotValidDimms = true;
                                    }
                                }
                            }
                        }
                    }

                    if ( !gotValidDimms )
                    {
                        allowSet = false;
                    }
                }
            }
        }

        this->buttonSet->setDisabled( !allowSet );
    }

    MainWindow *mainWnd;
	rw::TexDictionary* texDict;

    TexInfoWidget *texInfo;

    QPushButton *buttonSet;
    MagicLineEdit *widthEdit;
    MagicLineEdit *heightEdit;
};
