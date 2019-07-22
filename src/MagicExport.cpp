#include "MagicExport.h"

#include "texformathelper.hxx"

bool MagicFormatPluginExports::PutTexelRGBA(
    void *texelSource, unsigned int texelIndex,
	MAGIC_RASTER_FORMAT rasterFormat, unsigned int depth, MAGIC_COLOR_ORDERING colorOrder,
	unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha
) const
{
    rw::eRasterFormat internal_rasterFormat;
    rw::eColorOrdering internal_colorOrder;

    MagicMapToInternalRasterFormat( rasterFormat, internal_rasterFormat );
    MagicMapToInternalColorOrdering( colorOrder, internal_colorOrder );

	return rw::PutTexelRGBA(texelSource, texelIndex, internal_rasterFormat, depth, internal_colorOrder, red, green, blue, alpha);
}

bool MagicFormatPluginExports::BrowseTexelRGBA(
    const void *texelSource, unsigned int texelIndex, MAGIC_RASTER_FORMAT rasterFormat, unsigned int depth,
	MAGIC_COLOR_ORDERING colorOrder, MAGIC_PALETTE_TYPE paletteType, const void *paletteData, unsigned int paletteSize,
	unsigned char& redOut, unsigned char& greenOut, unsigned char& blueOut, unsigned char& alphaOut
) const
{
    rw::eRasterFormat internal_rasterFormat;
    rw::eColorOrdering internal_colorOrder;
    rw::ePaletteType internal_paletteType;

    MagicMapToInternalRasterFormat( rasterFormat, internal_rasterFormat );
    MagicMapToInternalColorOrdering( colorOrder, internal_colorOrder );
    MagicMapToInternalPaletteType( paletteType, internal_paletteType );

	return rw::BrowseTexelRGBA(
        texelSource, texelIndex, internal_rasterFormat, depth, internal_colorOrder,
		internal_paletteType, paletteData, paletteSize, redOut, greenOut, blueOut, alphaOut
    );
}