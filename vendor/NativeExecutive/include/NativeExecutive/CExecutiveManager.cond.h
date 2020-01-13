/*****************************************************************************
*
*  PROJECT:     Native Executive
*  LICENSE:     See LICENSE in the top level directory
*  FILE:        NativeExecutive/CExecutiveManager.cond.h
*  PURPOSE:     Hazard-safe conditional variable implementation
*
*  Multi Theft Auto is available from http://www.multitheftauto.com/
*
*****************************************************************************/

#ifndef _EXECUTIVE_MANAGER_CONDITIONAL_
#define _EXECUTIVE_MANAGER_CONDITIONAL_

BEGIN_NATIVE_EXECUTIVE

// Flood-gate style conditional variable.
// It comes with hazard-safety: if thread is asked to terminate then conditional variable will not wait.
struct CCondVar abstract
{
    void Wait( CReadWriteWriteContextSafe <>& ctxLock );
    void Signal( void );

    CExecutiveManager* GetManager( void );
};

END_NATIVE_EXECUTIVE

#endif //_EXECUTIVE_MANAGER_CONDITIONAL_