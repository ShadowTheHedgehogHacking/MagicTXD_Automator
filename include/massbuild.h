#pragma once

#include <QtWidgets/QDialog>
#include "languages.h"

struct MassBuildWindow : public QDialog, public magicTextLocalizationItem
{
private:
    friend struct massbuildEnv;

public:
    MassBuildWindow( MainWindow *mainWnd );
    ~MassBuildWindow( void );

    void updateContent( MainWindow *mainWnd ) override;

protected slots:
    void OnRequestBuild( bool checked );
    void OnRequestCancel( bool checked );

    void OnSelectCompressed( int state )
    {
        bool isGroupEnabled = ( state == Qt::Checked );

        this->editCompressionQuality->setEnabled( isGroupEnabled );
    }

    void OnSelectPalettized( int state )
    {
        bool isGroupEnabled = ( state == Qt::Checked );

        this->selectPaletteType->setEnabled( isGroupEnabled );
    }

private:
    void serialize( void );

    MainWindow *mainWnd;

    MagicLineEdit *editGameRoot;
    MagicLineEdit *editOutputRoot;
    QComboBox *selPlatformBox;
    QComboBox *selGameBox;
    QCheckBox *propGenMipmaps;
    MagicLineEdit *propGenMipmapsMax;
    QCheckBox *propCompressTextures;
    MagicLineEdit *editCompressionQuality;
    QCheckBox *propPalettizeTextures;
    QComboBox *selectPaletteType;
    QCheckBox *propCloseAfterComplete;

    RwListEntry <MassBuildWindow> node;
};
