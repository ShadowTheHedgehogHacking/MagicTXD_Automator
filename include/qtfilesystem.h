#ifndef _QT_FILESYSTEM_EMBEDDING_HEADER_
#define _QT_FILESYSTEM_EMBEDDING_HEADER_

#include <CFileSystem.h>

void register_file_translator( CFileTranslator *source );
void unregister_file_translator( CFileTranslator *source );

#endif //_QT_FILESYSTEM_EMBEDDING_HEADER_