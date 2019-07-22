/*****************************************************************************
*
*  PROJECT:     Eir SDK
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        eirrepo/sdk/avlsetmaputil.h
*  PURPOSE:     Shared code between Set and Map objects
*
*  Find the Eir SDK at: https://osdn.net/projects/eirrepo/
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _AVL_SET_AND_MAP_SHARED_HEADER_
#define _AVL_SET_AND_MAP_SHARED_HEADER_

#include "MacroUtils.h"
#include "AVLTree.h"

#define MAKE_SETMAP_ITERATOR( iteratorName, hostType, nodeType, nodeRedirNode, treeMembPath, avlTreeType ) \
    struct iteratorName \
    { \
        AINLINE iteratorName( hostType& host ) : real_iter( host.treeMembPath ) \
        { \
            return; \
        } \
        AINLINE iteratorName( hostType *host ) : real_iter( host->treeMembPath ) \
        { \
            return; \
        } \
        AINLINE iteratorName( nodeType *iter ) : real_iter( &iter->nodeRedirNode ) \
        { \
            return; \
        } \
        AINLINE iteratorName( iteratorName&& ) = default; \
        AINLINE iteratorName( const iteratorName& ) = default; \
        AINLINE ~iteratorName( void ) = default; \
        AINLINE bool IsEnd( void ) const \
        { \
            return real_iter.IsEnd(); \
        } \
        AINLINE void Increment( void ) \
        { \
            real_iter.Increment(); \
        } \
        AINLINE nodeType* Resolve( void ) \
        { \
            return AVL_GETITEM( nodeType, real_iter.Resolve(), nodeRedirNode ); \
        } \
    private: \
        typename avlTreeType::diff_node_iterator real_iter; \
    }

#endif //_AVL_SET_AND_MAP_SHARED_HEADER_