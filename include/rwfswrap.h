#ifndef _RENDERWARE_FILESYSTEM_STREAM_WRAP_
#define _RENDERWARE_FILESYSTEM_STREAM_WRAP_

CFile* RawOpenGlobalFile( CFileSystem *fileSys, const filePath& path, const filePath& mode );

rw::Stream* RwStreamCreateTranslated( rw::Interface *rwEngine, CFile *stream );

#endif //_RENDERWARE_FILESYSTEM_STREAM_WRAP_