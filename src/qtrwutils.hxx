// Utilities for interfacing between Qt and rwlib.
// Should not be included into the global headers, this is an on-demand component.

#include <algorithm>

inline QImage convertRWBitmapToQImage( const rw::Bitmap& rasterBitmap )
{
	rw::uint32 width, height;
	rasterBitmap.getSize(width, height);

	QImage texImage(width, height, QImage::Format::Format_ARGB32);

	// Copy scanline by scanline.
	for (rw::uint32 y = 0; y < height; y++)
	{
		uchar *scanLineContent = texImage.scanLine(y);

		QRgb *colorItems = (QRgb*)scanLineContent;

		for (rw::uint32 x = 0; x < width; x++)
		{
			QRgb *colorItem = (colorItems + x);

			unsigned char r, g, b, a;

			rasterBitmap.browsecolor(x, y, r, g, b, a);

			*colorItem = qRgba(r, g, b, a);
		}
	}

    return texImage;
}

inline QPixmap convertRWBitmapToQPixmap( const rw::Bitmap& rasterBitmap )
{
	return QPixmap::fromImage(
        convertRWBitmapToQImage( rasterBitmap )
    );
}

// Returns a sorted list of TXD platform names by importance.
template <typename stringListType>
inline rw::rwStaticVector <rw::rwStaticString <char>> PlatformImportanceSort( MainWindow *mainWnd, const stringListType& platformNames )
{
    // Set up a weighted container of platform strings.
    struct weightedNode
    {
        double weight;
        rw::rwStaticString <char> platName;

        inline bool operator < ( const weightedNode& right ) const
        {
            if ( this->weight > right.weight )
            {
                return true;
            }

            if ( FixedStringCompare( this->platName.GetConstString(), this->platName.GetLength(), right.platName.GetConstString(), right.platName.GetLength(), true ) == eir::eCompResult::LEFT_GREATER )
            {
                return true;
            }

            return false;
        }
    };

    size_t platCount = platformNames.GetCount();

    // Cache some things we are going to need.
    const char *rwRecTXDPlatform = nullptr;
    QString rwActualPlatform;
    rw::LibraryVersion txdVersion;
    {
        if ( rw::TexDictionary *currentTXD = mainWnd->getCurrentTXD() )
        {
            rwRecTXDPlatform = currentTXD->GetRecommendedDriverPlatform();
            rwActualPlatform = mainWnd->GetCurrentPlatform();

            txdVersion = currentTXD->GetEngineVersion();
        }
    }

    // The result container.
    rw::rwStaticSet <weightedNode> nodeContainer;

    // Process all platforms and store their rating.
    for ( size_t n = 0; n < platCount; n++ )
    {
        const rw::rwStaticString <char>& name = platformNames[ n ];

        weightedNode platNode;
        platNode.platName = name;
        platNode.weight = 0;

        // If the platform is recommended by the internal RW toolchain, we want to put it up front.
        if ( rwRecTXDPlatform && name == rwRecTXDPlatform )
        {
            platNode.weight += 0.9;
        }

        // If the platform makes sense in the TXD's version configuration, it is kinda important.
        RwVersionSets::eDataType curDataType = RwVersionSets::dataIdFromEnginePlatformName( ansi_to_qt( name ) );

        if ( curDataType != RwVersionSets::RWVS_DT_NOT_DEFINED )
        {
            // Check whether this version makes sense in this platform.
            int setIndex, platIndex, dataTypeIndex;

            bool makesSense = mainWnd->versionSets.matchSet( txdVersion, curDataType, setIndex, platIndex, dataTypeIndex );

            if ( makesSense )
            {
                // Honor it.
                platNode.weight += 0.7;
            }
        }

        // If we match the current platform of the TXD, we are uber important!
        if ( rwActualPlatform.isEmpty() == false && rwActualPlatform == name.GetConstString() )
        {
            platNode.weight += 1.0;
        }

        nodeContainer.Insert( std::move( platNode ) );
    }

    // Make the sorted thing.
    rw::rwStaticVector <rw::rwStaticString <char>> sortedResult;
    {
        size_t n = 0;

        for ( typename decltype(nodeContainer)::iterator iter( nodeContainer ); !iter.IsEnd(); iter.Increment(), n++ )
        {
            const weightedNode& curItem = iter.Resolve()->GetValue();

            sortedResult.AddToBack( std::move( curItem.platName ) );
        }
    }

    return sortedResult;
}
