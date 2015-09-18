/*
   Copyright (c) 2009-2015, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"

#define COLDIST CIRC
#define ROWDIST CIRC

#include "./setup.hpp"

namespace El {

// Public section
// ##############

// Operator overloading
// ====================

// Make a copy
// -----------
template<typename T>
DM& DM::operator=( const ElementalMatrix<T>& A )
{
    DEBUG_ONLY(CSE cse("[CIRC,CIRC] = EM"));
    copy::Gather( A, *this );
    return *this;
}

// Basic queries
// =============
template<typename T>
mpi::Comm DM::DistComm() const EL_NO_EXCEPT
{ return ( this->Participating() ? mpi::COMM_SELF : mpi::COMM_NULL ); }

template<typename T>
mpi::Comm DM::RedundantComm() const EL_NO_EXCEPT
{ return ( this->Participating() ? mpi::COMM_SELF : mpi::COMM_NULL ); }

template<typename T>
mpi::Comm DM::CrossComm() const EL_NO_EXCEPT
{ return this->grid_->VCComm(); }

template<typename T>
mpi::Comm DM::ColComm() const EL_NO_EXCEPT
{ return ( this->Participating() ? mpi::COMM_SELF : mpi::COMM_NULL ); }

template<typename T>
mpi::Comm DM::RowComm() const EL_NO_EXCEPT
{ return ( this->Participating() ? mpi::COMM_SELF : mpi::COMM_NULL ); }

template<typename T>
int DM::ColStride() const EL_NO_EXCEPT { return 1; }
template<typename T>
int DM::RowStride() const EL_NO_EXCEPT { return 1; }
template<typename T>
int DM::DistSize() const EL_NO_EXCEPT { return 1; }
template<typename T>
int DM::CrossSize() const EL_NO_EXCEPT { return this->grid_->VCSize(); }
template<typename T>
int DM::RedundantSize() const EL_NO_EXCEPT { return 1; }

// Instantiate {Int,Real,Complex<Real>} for each Real in {float,double}
// ####################################################################

#define SELF(T,U,V) \
  template DistMatrix<T,COLDIST,ROWDIST>::DistMatrix \
  ( const DistMatrix<T,U,V>& A );
#define OTHER(T,U,V) \
  template DistMatrix<T,COLDIST,ROWDIST>::DistMatrix \
  ( const DistMatrix<T,U,V,BLOCK>& A ); \
  template DistMatrix<T,COLDIST,ROWDIST>& \
           DistMatrix<T,COLDIST,ROWDIST>::operator= \
           ( const DistMatrix<T,U,V,BLOCK>& A )
#define BOTH(T,U,V) \
  SELF(T,U,V); \
  OTHER(T,U,V)
#define PROTO(T) \
  template class DistMatrix<T,COLDIST,ROWDIST>; \
  OTHER(T,CIRC,CIRC); \
  BOTH( T,MC,  MR  ); \
  BOTH( T,MC,  STAR); \
  BOTH( T,MD,  STAR); \
  BOTH( T,MR,  MC  ); \
  BOTH( T,MR,  STAR); \
  BOTH( T,STAR,MC  ); \
  BOTH( T,STAR,MD  ); \
  BOTH( T,STAR,MR  ); \
  BOTH( T,STAR,STAR); \
  BOTH( T,STAR,VC  ); \
  BOTH( T,STAR,VR  ); \
  BOTH( T,VC,  STAR); \
  BOTH( T,VR,  STAR);

#define EL_ENABLE_QUAD
#include "El/macros/Instantiate.h"

} // namespace El
