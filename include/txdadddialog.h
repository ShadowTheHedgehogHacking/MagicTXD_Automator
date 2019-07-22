#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QCheckBox>
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>

#include <functional>

#include "rwimageimporter.h"

#include "qtutils.h"

class MainWindow;

class TexAddDialog : public QDialog
{
public:
    enum eCreationType
    {
        CREATE_IMGPATH,
        CREATE_RASTER
    };

    struct dialogCreateParams
    {
        inline dialogCreateParams(void)
        {
            this->overwriteTexName = NULL;
        }

        QString actionDesc;
        QString actionName;
        eCreationType type;
        struct
        {
            QString imgPath;
        } img_path;
        struct
        {
            rw::TextureBase *tex;
        } orig_raster;

        // Optional properties.
        const QString *overwriteTexName;
    };

    struct texAddOperation
    {
        enum eAdditionType
        {
            ADD_RASTER,
            ADD_TEXCHUNK
        };
        eAdditionType add_type;

        struct
        {
            // Selected texture properties.
            std::string texName;
            std::string maskName;

            rw::Raster *raster;
        } add_raster;
        struct
        {
            rw::TextureBase *texHandle;
        } add_texture;
    };

    typedef std::function <void(const texAddOperation&)> operationCallback_t;

    TexAddDialog(MainWindow *mainWnd, const dialogCreateParams& create_params, operationCallback_t func);
    ~TexAddDialog(void);

    void loadPlatformOriginal(void);
    void updatePreviewWidget(void);

    void createRasterForConfiguration(void);

    // Helpers.
    static QComboBox* createPlatformSelectComboBox(MainWindow *mainWnd);

    static void RwTextureAssignNewRaster( rw::TextureBase *texHandle, rw::Raster *newRaster, const std::string& texName, const std::string& maskName );

private:
    void UpdatePreview();
    void ClearPreview();

    void releaseConvRaster(void);

    inline rw::Raster* GetDisplayRaster(void)
    {
        if (rw::Raster *convRaster = this->convRaster)
        {
            return convRaster;
        }

        if (this->hasPlatformOriginal)
        {
            if (rw::Raster *origRaster = this->platformOrigRaster)
            {
                return origRaster;
            }
        }

        return NULL;
    }

    void UpdateAccessability(void);

    void SetCurrentPlatform( QString name );
    QString GetCurrentPlatform(void);

public slots:
    void OnTextureAddRequest(bool checked);
    void OnCloseRequest(bool checked);

    void OnPlatformSelect(const QString& newText);

    void OnPlatformFormatTypeToggle(bool checked);

    void OnTextureCompressionSeelct(const QString& newCompression);
    void OnTexturePaletteTypeSelect(const QString& newPaletteType);
    void OnTexturePixelFormatSelect(const QString& newPixelFormat);

    void OnPreviewBackgroundStateChanged(int state);
    void OnScalePreviewStateChanged(int state);
    void OnFillPreviewStateChanged(int state);

private:
    MainWindow *mainWnd;

    bool isConstructing;        // we allow late-initialization of properties.

    eCreationType dialog_type;
    eImportExpectation img_exp;

    rw::Raster *platformOrigRaster;
    rw::TextureBase *texHandle;     // if not NULL, then this texture will be used for import.
    rw::Raster *convRaster;
    bool hasPlatformOriginal;
    QPixmap pixelsToAdd;

    bool hasConfidentPlatform;
    bool wantsGoodPlatformSetting;

    MagicLineEdit *textureNameEdit;
    MagicLineEdit *textureMaskNameEdit;
    QWidget *platformSelectWidget;

    QFormLayout *platformPropForm;
    QLabel *platformHeaderLabel;
    QWidget *platformRawRasterProp;
    QComboBox *platformCompressionSelectProp;
    QComboBox *platformPaletteSelectProp;
    QComboBox *platformPixelFormatSelectProp;

    bool enableOriginal;
    bool enableRawRaster;
    bool enableCompressSelect;
    bool enablePaletteSelect;
    bool enablePixelFormatSelect;

    QRadioButton *platformOriginalToggle;
    QRadioButton *platformRawRasterToggle;
    QRadioButton *platformCompressionToggle;
    QRadioButton *platformPaletteToggle;

    // General properties.
    QCheckBox *propGenerateMipmaps;

    // Preview widget stuff.
    QLabel *previewLabel;
    QScrollArea *previewScrollArea;
    QCheckBox *scaledPreviewCheckBox;
    QCheckBox *fillPreviewCheckBox;
    QCheckBox *backgroundForPreviewCheckBox;
    QLabel *previewInfoLabel;

    // The buttons.
    QPushButton *cancelButton;
    QPushButton *applyButton;

    operationCallback_t cb;

    QString imgPath;

    // Special variant of image importing for our dialog.
    struct texAddImageImportMethods : public imageImportMethods
    {
        inline texAddImageImportMethods( TexAddDialog *texAdd )
        {
            this->dialog = texAdd;
        }

        void OnWarning( rw::rwStaticString <char>&& msg ) const override;
        void OnError( rw::rwStaticString <char>&& msg ) const override;

        rw::Raster* MakeRaster( void ) const override;

        TexAddDialog *dialog;
    };

    texAddImageImportMethods impMeth;

    void clearTextureOriginal( void );

    rw::Raster* MakeRaster( void );
};
