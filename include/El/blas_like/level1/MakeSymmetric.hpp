/*
   Copyright (c) 2009-2016, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"

namespace El {

template<typename T>
void MakeSymmetric( UpperOrLower uplo, Matrix<T>& A, bool conjugate )
{
    DEBUG_ONLY(CSE cse("MakeSymmetric"))
    const Int n = A.Width();
    if( A.Height() != n )
        LogicError("Cannot make non-square matrix symmetric");

    if( conjugate )
        MakeDiagonalReal(A);

    T* ABuf = A.Buffer();
    const Int ldim = A.LDim();
    if( uplo == LOWER )
    {
        for( Int j=0; j<n; ++j )
        {
            for( Int i=j+1; i<n; ++i )
            {
                if( conjugate )
                    ABuf[j+i*ldim] = Conj(ABuf[i+j*ldim]); 
                else
                    ABuf[j+i*ldim] = ABuf[i+j*ldim];
            }    
        }
    }
    else
    {
        for( Int j=0; j<n; ++j )
        {
            for( Int i=0; i<j; ++i )
            {
                if( conjugate )
                    ABuf[j+i*ldim] = Conj(ABuf[i+j*ldim]);
                else
                    ABuf[j+i*ldim] = ABuf[i+j*ldim];
            }
        }
    }
}

template<typename T>
void MakeSymmetric
( UpperOrLower uplo, ElementalMatrix<T>& A, bool conjugate )
{
    DEBUG_ONLY(CSE cse("MakeSymmetric"))
    if( A.Height() != A.Width() )
        LogicError("Cannot make non-square matrix symmetric");

    MakeTrapezoidal( uplo, A );
    if( conjugate )
        MakeDiagonalReal(A);

    unique_ptr<ElementalMatrix<T>> ATrans( A.Construct(A.Grid(),A.Root()) );
    Transpose( A, *ATrans, conjugate );
    if( uplo == LOWER )
        AxpyTrapezoid( UPPER, T(1), *ATrans, A, 1 );
    else
        AxpyTrapezoid( LOWER, T(1), *ATrans, A, -1 );
}

template<typename T>
void MakeSymmetric( UpperOrLower uplo, SparseMatrix<T>& A, bool conjugate )
{
    DEBUG_ONLY(CSE cse("MakeSymmetric"))
    if( A.Height() != A.Width() )
        LogicError("Cannot make non-square matrix symmetric");

    MakeTrapezoidal( uplo, A );

    const Int m = A.Height();
    const Int numEntries = A.NumEntries();
    const Int* sBuf = A.LockedSourceBuffer();
    const Int* tBuf = A.LockedTargetBuffer();
    T* vBuf = A.ValueBuffer();

    // Iterate over the diagonal entries
    Int numDiagonal = 0;
    for( Int i=0; i<m; ++i )
    {
        const Int e = A.Offset( i, i );
        if( e < numEntries && sBuf[e] == i && tBuf[e] == i )
        {
            ++numDiagonal;
            if( conjugate && IsComplex<T>::value )
                vBuf[e] = RealPart(vBuf[e]);
        }
    }

    A.Reserve( numEntries-numDiagonal );
    sBuf = A.LockedSourceBuffer();
    tBuf = A.LockedTargetBuffer();
    vBuf = A.ValueBuffer();

    if( uplo == LOWER )
    {
        // Transpose the strictly lower triangle onto the upper triangle
        for( Int k=0; k<numEntries; ++k ) 
        {
            if( sBuf[k] > tBuf[k] )
            {
                if( conjugate )
                    A.QueueUpdate( tBuf[k], sBuf[k], Conj(vBuf[k]) );
                else
                    A.QueueUpdate( tBuf[k], sBuf[k], vBuf[k] );
            }
        }
    }
    else
    {
        // Transpose the strictly upper triangle onto the lower triangle
        for( Int k=0; k<numEntries; ++k ) 
        {
            if( sBuf[k] < tBuf[k] )
            {
                if( conjugate )
                    A.QueueUpdate( tBuf[k], sBuf[k], Conj(vBuf[k]) );
                else
                    A.QueueUpdate( tBuf[k], sBuf[k], vBuf[k] );
            }
        }
    }
    A.ProcessQueues();
}

template<typename T>
void MakeSymmetric( UpperOrLower uplo, DistSparseMatrix<T>& A, bool conjugate )
{
    DEBUG_ONLY(CSE cse("MakeSymmetric"))
    if( A.Height() != A.Width() )
        LogicError("Cannot make non-square matrix symmetric");

    MakeTrapezoidal( uplo, A );
    const Int numLocalEntries = A.NumLocalEntries();
    T* vBuf = A.ValueBuffer();
    const Int* sBuf = A.LockedSourceBuffer();
    const Int* tBuf = A.LockedTargetBuffer();
    if( conjugate && IsComplex<T>::value )
    {
        for( Int k=0; k<numLocalEntries; ++k )
            if( sBuf[k] == tBuf[k] )
                vBuf[k] = RealPart(vBuf[k]);
    }

    // Compute the number of entries to send
    // =====================================
    Int numSend = 0;
    for( Int k=0; k<numLocalEntries; ++k )
    {
        const Int i = sBuf[k];
        const Int j = tBuf[k];
        if( (uplo == LOWER && i > j) || (uplo == UPPER && i < j) )
            ++numSend;
    }

    // Apply the updates
    // =================
    A.Reserve( numSend, numSend );
    for( Int k=0; k<numLocalEntries; ++k )
    {
        const Int i = sBuf[k];
        const Int j = tBuf[k];
        if( (uplo == LOWER && i > j) || (uplo == UPPER && i < j) )
            A.QueueUpdate( j, i, ( conjugate ? Conj(vBuf[k]) : vBuf[k] ) );
    }
    A.ProcessQueues();
}

#define PROTO(T) \
  template void MakeSymmetric \
  ( UpperOrLower uplo, Matrix<T>& A, bool conjugate ); \
  template void MakeSymmetric \
  ( UpperOrLower uplo, ElementalMatrix<T>& A, bool conjugate ); \
  template void MakeSymmetric \
  ( UpperOrLower uplo, SparseMatrix<T>& A, bool conjugate ); \
  template void MakeSymmetric \
  ( UpperOrLower uplo, DistSparseMatrix<T>& A, bool conjugate );

#define EL_ENABLE_DOUBLEDOUBLE
#define EL_ENABLE_QUADDOUBLE
#define EL_ENABLE_QUAD
#define EL_ENABLE_BIGINT
#define EL_ENABLE_BIGFLOAT
#include "El/macros/Instantiate.h"

} // namespace El