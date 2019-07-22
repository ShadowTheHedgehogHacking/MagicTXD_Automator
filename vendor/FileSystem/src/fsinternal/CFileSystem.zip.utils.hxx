/*****************************************************************************
*
*  PROJECT:     Eir FileSystem
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        FileSystem/src/CFileSystem.zip.utils.hxx
*  PURPOSE:     ZIP archive filesystem internal .cpp header
*
*  Get Eir FileSystem from https://osdn.net/projects/eirfs/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _ZIP_INTERNAL_CPP_HEADER_
#define _ZIP_INTERNAL_CPP_HEADER_

// Structures that are used by the .zip extension code.
struct zip_mapped_rdircheck
{
    inline bool Perform( const char map[4], size_t count, long& off ) const
    {
        if ( count < 4 )
        {
            return false;
        }

        const endian::little_endian <fsUInt_t>& sig_dword = *(const endian::little_endian <fsUInt_t>*)&map[0];

        if ( sig_dword != ZIP_SIGNATURE )
        {
            return false;
        }

        off = 4;
        return true;
    }
};

#pragma pack(1)

struct _endDir
{
    endian::little_endian <fsUShort_t>      diskID;
    endian::little_endian <fsUShort_t>      diskAlign;
    endian::little_endian <fsUShort_t>      entries;
    endian::little_endian <fsUShort_t>      totalEntries;
    endian::little_endian <fsUInt_t>        centralDirectorySize;
    endian::little_endian <fsUInt_t>        centralDirectoryOffset;

    endian::little_endian <fsUShort_t>      commentLen;
};

#pragma pack()

#endif //_ZIP_INTERNAL_CPP_HEADER_