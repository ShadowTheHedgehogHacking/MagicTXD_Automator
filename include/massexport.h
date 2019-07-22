#pragma once

#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QRadioButton>

#include "languages.h"

// The mass export window is a simple dialog that extracts the contents of multiple TXD files into a directory.
// It is a convenient way of dumping all image files, even from IMG containers.
struct MassExportWindow : public QDialog, public magicTextLocalizationItem
{
    friend struct massexportEnv;

    MassExportWindow( MainWindow *mainWnd );
    ~MassExportWindow( void );

    void updateContent( MainWindow *mainWnd ) override;

public slots:
    void OnRequestExport( bool checked );
    void OnRequestCancel( bool checked );

private:
    void serialize( void );

    MainWindow *mainWnd;

    MagicLineEdit *editGameRoot;
    MagicLineEdit *editOutputRoot;
    QComboBox *boxRecomImageFormat;
    QRadioButton *optionExportPlain;
    QRadioButton *optionExportTXDName;
    QRadioButton *optionExportFolders;

    RwListEntry <MassExportWindow> node;
};
