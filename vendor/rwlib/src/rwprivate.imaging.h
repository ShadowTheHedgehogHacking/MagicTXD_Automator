// Framework-private global include header file about imaging extennsions.

#ifndef _RENDERWARE_PRIVATE_IMAGING_
#define _RENDERWARE_PRIVATE_IMAGING_

namespace rw
{

// Special mipmap pushing algorithms.
// This was once a public export, but it seems to dangerous to do that.
bool DeserializeMipmapLayer( Stream *inputStream, rawMipmapLayer& rawLayer );
bool SerializeMipmapLayer( Stream *outputStream, const char *formatDescriptor, const rawMipmapLayer& rawLayer );

// Native imaging internal functions with special requirements.
// Read them up before using them!
void NativeImagePutToRasterNoLock( NativeImage *nativeImg, Raster *raster );
void NativeImageFetchFromRasterNoLock( NativeImage *nativeImg, Raster *raster, const char *nativeTexName, bool& needsRefOut );

} // namespace rw

#endif //_RENDERWARE_PRIVATE_IMAGING_