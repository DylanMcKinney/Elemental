/*
   Copyright (c) 2009-2014, Jack Poulson
   All rights reserved.

   This file is part of Elemental and is under the BSD 2-Clause License, 
   which can be found in the LICENSE file in the root directory, or at 
   http://opensource.org/licenses/BSD-2-Clause
*/
#include "El.hpp"
#include "./util.hpp"

namespace El {
namespace qp {
namespace primal {

// The following solves a quadratic program in "primal" conic form:
//
//   min 1/2 x^T Q x + c^T x
//   s.t. A x = b, x >= 0,
//
// as opposed to the more general "dual" conic form:
//
//   min 1/2 x^T Q x + c^T x
//   s.t. A x = b, h - G x >= 0,
//
// using a simple Infeasible Path Following (IPF) scheme. This routine
// should only be used for academic purposes, as the Mehrotra alternative
// typically requires an order of magnitude fewer iterations.
template<typename Real>
void IPF
( const Matrix<Real>& Q, const Matrix<Real>& A, 
  const Matrix<Real>& b, const Matrix<Real>& c,
  Matrix<Real>& x, Matrix<Real>& y, Matrix<Real>& z,
  const IPFCtrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("qp::primal::IPF"))    

    const Int m = A.Height();
    const Int n = A.Width();
    Matrix<Real> J, rhs, rmu, rb, rc, dx, dy, dz;
#ifndef EL_RELEASE
    Matrix<Real> dxError, dyError, dzError;
#endif
    for( Int numIts=0; ; ++numIts )
    {
        // Check that no entries of x or z are non-positive
        // ================================================
        Int numNonPos_x = 0;
        for( Int i=0; i<x.Height(); ++i )
            if( x.Get(i,0) <= Real(0) )
                ++numNonPos_x;
        Int numNonPos_z = 0;
        for( Int i=0; i<z.Height(); ++i )
            if( z.Get(i,0) <= Real(0) )
                ++numNonPos_z;
        if( numNonPos_x > 0 || numNonPos_z > 0 )
            LogicError
            (numNonPos_x," entries of x were nonpositive and ",
             numNonPos_z," entries of z were nonpositive");

        // Check for convergence
        // =====================
        // |c^T x + x^T Q x - b^T y| / (1 + |c^T x + 1/2 x^T Q x|) <= tol ?
        // ----------------------------------------------------------------
        Zeros( rhs, n, 1 );
        Hemv( LOWER, Real(1), Q, x, Real(0), rhs );
        const Real xTQx = Dot(x,rhs);
        const Real primObj = Dot(c,x) + xTQx/2;
        const Real dualObj = Dot(b,y) - xTQx/2; 
        const Real objConv = Abs(primObj-dualObj) / (Real(1)+Abs(primObj));
        // || r_b ||_2 / (1 + || b ||_2) <= tol ?
        // --------------------------------------
        const Real bNrm2 = Nrm2( b );
        rb = b;
        Gemv( NORMAL, Real(1), A, x, Real(-1), rb );
        const Real rbNrm2 = Nrm2( rb );
        const Real rbConv = rbNrm2 / (Real(1)+bNrm2);
        // || r_c ||_2 / (1 + || c + Q x ||_2) <= tol ?
        // --------------------------------------------
        // NOTE: rhs currently contains Q x
        Axpy( Real(1), c, rhs );
        const Real objGradNrm2 = Nrm2( rhs );
        rc = rhs;
        Gemv( TRANSPOSE, Real(1), A, y, Real(-1), rc );
        Axpy( Real(1), z, rc );
        const Real rcNrm2 = Nrm2( rc );
        const Real rcConv = rcNrm2 / (Real(1)+objGradNrm2);
        // Now check the pieces
        // --------------------
        if( objConv <= ctrl.tol && rbConv <= ctrl.tol && rcConv <= ctrl.tol )
            break;
        else if( ctrl.print )
            std::cout << " iter " << numIts << ":\n"
                      << "  | primObj - dualObj | / (1 + |primObj|) = "
                      << objConv << "\n"
                      << "  || r_b ||_2 / (1 + || b ||_2)           = "
                      << rbConv << "\n"
                      << "  || r_c ||_2 / (1 + || c + Q x ||_2)     = "
                      << rcConv << std::endl;

        // Raise an exception after an unacceptable number of iterations
        // =============================================================
        if( numIts == ctrl.maxIts )
            RuntimeError
            ("Maximum number of iterations (",ctrl.maxIts,") exceeded");

        // Compute the duality measure and r_mu = X Z e - tau e
        // ====================================================
        const Real mu = Dot(x,z) / n;
        rmu.Resize( n, 1 );
        for( Int i=0; i<n; ++i )
            rmu.Set( i, 0, x.Get(i,0)*z.Get(i,0) - ctrl.centering*mu );

        if( ctrl.system == FULL_KKT )
        {
            // Construct the full KKT system
            // =============================
            KKT( Q, A, x, z, J );
            KKTRHS( rmu, rc, rb, rhs );

            // Compute the proposed step from the KKT system
            // =============================================
            GaussianElimination( J, rhs );
            ExpandKKTSolution( m, n, rhs, dx, dy, dz );
        }
        else // ctrl.system == AUGMENTED_KKT
        {
            // Construct the reduced KKT system
            // ================================
            AugmentedKKT( Q, A, x, z, J );
            AugmentedKKTRHS( x, rmu, rc, rb, rhs );

            // Compute the proposed step from the KKT system
            // =============================================
            SymmetricSolve( LOWER, NORMAL, J, rhs );
            ExpandAugmentedSolution( x, z, rmu, rhs, dx, dy, dz );
        }

#ifndef EL_RELEASE
        // Sanity checks
        // =============
        const Real rmuNrm2 = Nrm2( rmu );
        dzError = rmu;
        for( Int i=0; i<n; ++i )
        {
            const Real xi = x.Get(i,0);
            const Real zi = z.Get(i,0);
            const Real dxi = dx.Get(i,0);
            const Real dzi = dz.Get(i,0);
            dzError.Update( i, 0, xi*dzi + zi*dxi );
        }
        const Real dzErrorNrm2 = Nrm2( dzError );

        dyError = dz;
        Gemv( TRANSPOSE, Real(1), A, dy, Real(1), dyError );
        Hemv( LOWER, Real(-1), Q, dx, Real(1), dyError );
        Axpy( Real(1), rc, dyError );
        const Real dyErrorNrm2 = Nrm2( dyError );

        dxError = rb;
        Gemv( NORMAL, Real(1), A, dx, Real(1), dxError );
        const Real dxErrorNrm2 = Nrm2( dxError );

        if( ctrl.print )
            std::cout << "  || dxError ||_2 / (1 + || r_b ||_2)  = " 
                      << dxErrorNrm2/(1+rbNrm2) << "\n"
                      << "  || dyError ||_2 / (1 + || r_c ||_2)  = " 
                      << dyErrorNrm2/(1+rcNrm2) << "\n"
                      << "  || dzError ||_2 / (1 + || r_mu ||_2) = " 
                      << dzErrorNrm2/(1+rmuNrm2) << std::endl;
#endif

        // Decide on the step length
        // =========================
        const Real alpha =
          IPFLineSearch
          ( Q, A, b, c, x, y, z, dx, dy, dz, 
            ctrl.tol*(1+bNrm2), ctrl.tol*(1+objGradNrm2), ctrl.lineSearchCtrl );
        if( ctrl.print )
            std::cout << "  alpha = " << alpha << std::endl;

        // Update the state by stepping a distance of alpha
        // ================================================
        Axpy( alpha, dx, x );
        Axpy( alpha, dy, y );
        Axpy( alpha, dz, z );
    }
}

template<typename Real>
void IPF
( const AbstractDistMatrix<Real>& QPre, const AbstractDistMatrix<Real>& APre, 
  const AbstractDistMatrix<Real>& b,    const AbstractDistMatrix<Real>& c,
  AbstractDistMatrix<Real>& xPre, AbstractDistMatrix<Real>& y, 
  AbstractDistMatrix<Real>& zPre, const IPFCtrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("qp::primal::IPF"))    

    ProxyCtrl proxCtrl;
    proxCtrl.colConstrain = true;
    proxCtrl.rowConstrain = true;
    proxCtrl.colAlign = 0;
    proxCtrl.rowAlign = 0;
    auto QPtr = ReadProxy<Real,MC,MR>(&QPre,proxCtrl);      auto& Q = *QPtr;
    auto APtr = ReadProxy<Real,MC,MR>(&APre,proxCtrl);      auto& A = *APtr;
    auto xPtr = ReadWriteProxy<Real,MC,MR>(&xPre,proxCtrl); auto& x = *xPtr;
    auto zPtr = ReadWriteProxy<Real,MC,MR>(&zPre,proxCtrl); auto& z = *zPtr;

    const Int m = A.Height();
    const Int n = A.Width();
    const Grid& grid = A.Grid();
    const Int commRank = A.Grid().Rank();

    DistMatrix<Real> J(grid), rhs(grid), 
                     rmu(grid), rb(grid), rc(grid), 
                     dx(grid),  dy(grid), dz(grid);
    dx.AlignWith( x );
    dz.AlignWith( x );
#ifndef EL_RELEASE
    DistMatrix<Real> dxError(grid), dyError(grid), dzError(grid);
    dzError.AlignWith( dz );
#endif
    for( Int numIts=0; ; ++numIts )
    {
        // Check that no entries of x or z are non-positive
        // ================================================
        Int numNonPos_x = 0;
        if( x.IsLocalCol(0) )
            for( Int iLoc=0; iLoc<x.LocalHeight(); ++iLoc )
                if( x.GetLocal(iLoc,0) <= Real(0) )
                    ++numNonPos_x;
        numNonPos_x = mpi::AllReduce( numNonPos_x, x.DistComm() );
        Int numNonPos_z = 0;
        if( z.IsLocalCol(0) )
            for( Int iLoc=0; iLoc<z.LocalHeight(); ++iLoc )
                if( z.GetLocal(iLoc,0) <= Real(0) )
                    ++numNonPos_z;
        numNonPos_z = mpi::AllReduce( numNonPos_z, z.DistComm() );
        if( numNonPos_x > 0 || numNonPos_z > 0 )
            LogicError
            (numNonPos_x," entries of x were nonpositive and ",
             numNonPos_z," entries of z were nonpositive");

        // Check for convergence
        // =====================
        // |c^T x + x^T Q x - b^T y| / (1 + |c^T x + 1/2 x^T Q x|) <= tol ?
        // ----------------------------------------------------------------
        Zeros( rhs, n, 1 );
        Hemv( LOWER, Real(1), Q, x, Real(0), rhs );
        const Real xTQx = Dot(x,rhs);
        const Real primObj = Dot(c,x) + xTQx/2;
        const Real dualObj = Dot(b,y) - xTQx/2;
        const Real objConv = Abs(primObj-dualObj) / (Real(1)+Abs(primObj));
        // || r_b ||_2 / (1 + || b ||_2) <= tol ?
        // --------------------------------------
        const Real bNrm2 = Nrm2( b );
        rb = b;
        Gemv( NORMAL, Real(1), A, x, Real(-1), rb );
        const Real rbNrm2 = Nrm2( rb );
        const Real rbConv = rbNrm2 / (Real(1)+bNrm2);
        // || r_c ||_2 / (1 + || c + Q x ||_2) <= tol ?
        // --------------------------------------------
        // NOTE: rhs currently contains Q x
        Axpy( Real(1), c, rhs );
        const Real objGradNrm2 = Nrm2( rhs );
        rc = rhs;
        Gemv( TRANSPOSE, Real(1), A, y, Real(-1), rc );
        Axpy( Real(1), z, rc );
        const Real rcNrm2 = Nrm2( rc );
        const Real rcConv = rcNrm2 / (Real(1)+objGradNrm2);
        // Now check the pieces
        // --------------------
        if( objConv <= ctrl.tol && rbConv <= ctrl.tol && rcConv <= ctrl.tol )
            break;
        else if( ctrl.print && commRank == 0 )
            std::cout << " iter " << numIts << ":\n"
                      << "  |primObj - dualObj| / (1 + |primObj|) = "
                      << objConv << "\n"
                      << "  || r_b ||_2 / (1 + || b ||_2)         = "
                      << rbConv << "\n"
                      << "  || r_c ||_2 / (1 + || c + Q x ||_2)   = "
                      << rcConv << std::endl;

        // Raise an exception after an unacceptable number of iterations
        // =============================================================
        if( numIts == ctrl.maxIts )
            RuntimeError
            ("Maximum number of iterations (",ctrl.maxIts,") exceeded");

        // Compute the duality measure and r_mu = X Z e - tau e
        // ====================================================
        const Real mu = Dot(x,z) / n;
        rmu.Resize( n, 1 );
        if( rmu.IsLocalCol(0) )
            for( Int iLoc=0; iLoc<rmu.LocalHeight(); ++iLoc )
                rmu.SetLocal
                ( iLoc, 0, 
                  x.GetLocal(iLoc,0)*z.GetLocal(iLoc,0) - ctrl.centering*mu );

        if( ctrl.system == FULL_KKT )
        {
            // Construct the full KKT system
            // =============================
            KKT( Q, A, x, z, J );
            KKTRHS( rmu, rc, rb, rhs );

            // Compute the proposed step from the KKT system
            // =============================================
            GaussianElimination( J, rhs );
            ExpandKKTSolution( m, n, rhs, dx, dy, dz );
        }
        else // ctrl.system == AUGMENTED_KKT
        {
            // Construct the reduced KKT system
            // ================================
            AugmentedKKT( Q, A, x, z, J );
            AugmentedKKTRHS( x, rmu, rc, rb, rhs );

            // Compute the proposed step from the KKT system
            // =============================================
            SymmetricSolve( LOWER, NORMAL, J, rhs );
            ExpandAugmentedSolution( x, z, rmu, rhs, dx, dy, dz );
        }

#ifndef EL_RELEASE
        // Sanity checks
        // =============
        const Real rmuNrm2 = Nrm2( rmu );
        // TODO: Find a more convenient syntax for expressing this operation
        dzError = rmu;
        if( dzError.IsLocalCol(0) )
        {
            for( Int iLoc=0; iLoc<dzError.LocalHeight(); ++iLoc )
            {
                const Real xi = x.GetLocal(iLoc,0);
                const Real zi = z.GetLocal(iLoc,0);
                const Real dxi = dx.GetLocal(iLoc,0);
                const Real dzi = dz.GetLocal(iLoc,0);
                dzError.UpdateLocal( iLoc, 0, xi*dzi + zi*dxi );
            }
        }
        const Real dzErrorNrm2 = Nrm2( dzError );

        dyError = dz;
        Gemv( TRANSPOSE, Real(1), A, dy, Real(1), dyError );
        Hemv( LOWER, Real(-1), Q, dx, Real(1), dyError );
        Axpy( Real(1), rc, dyError );
        const Real dyErrorNrm2 = Nrm2( dyError );

        dxError = rb;
        Gemv( NORMAL, Real(1), A, dx, Real(1), dxError );
        const Real dxErrorNrm2 = Nrm2( dxError );

        if( ctrl.print && commRank == 0 )
            std::cout << "  || dxError ||_2 / (1 + || r_b ||_2)  = " 
                      << dxErrorNrm2/(1+rbNrm2) << "\n"
                      << "  || dyError ||_2 / (1 + || r_c ||_2)  = " 
                      << dyErrorNrm2/(1+rcNrm2) << "\n"
                      << "  || dzError ||_2 / (1 + || r_mu ||_2) = " 
                      << dzErrorNrm2/(1+rmuNrm2) << std::endl;
#endif

        // Decide on the step length
        // =========================
        const Real alpha =
          IPFLineSearch
          ( Q, A, b, c, x, y, z, dx, dy, dz, 
            ctrl.tol*(1+bNrm2), ctrl.tol*(1+objGradNrm2), ctrl.lineSearchCtrl );
        if( ctrl.print && commRank == 0 )
            std::cout << "  alpha = " << alpha << std::endl;

        // Update the state by stepping a distance of alpha
        // ================================================
        Axpy( alpha, dx, x );
        Axpy( alpha, dy, y );
        Axpy( alpha, dz, z );
    }
}

template<typename Real>
void IPF
( const SparseMatrix<Real>& Q, const SparseMatrix<Real>& A, 
  const Matrix<Real>& b,       const Matrix<Real>& c,
  Matrix<Real>& x, Matrix<Real>& y, Matrix<Real>& z,
  const IPFCtrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("qp::primal::IPF"))    
    LogicError("Sequential sparse-direct solvers not yet supported");
}

template<typename Real>
void IPF
( const DistSparseMatrix<Real>& Q, const DistSparseMatrix<Real>& A, 
  const DistMultiVec<Real>& b, const DistMultiVec<Real>& c,
  DistMultiVec<Real>& x, DistMultiVec<Real>& y, DistMultiVec<Real>& z,
  const IPFCtrl<Real>& ctrl )
{
    DEBUG_ONLY(CallStackEntry cse("qp::primal::IPF"))    

    const Int m = A.Height();
    const Int n = A.Width();
    mpi::Comm comm = A.Comm();
    const Int commRank = mpi::Rank(comm);
    const Real epsilon = lapack::MachineEpsilon<Real>();

    DistSymmInfo info;
    DistSeparatorTree sepTree;
    DistMap map, invMap;
    DistSparseMatrix<Real> J(comm);
    DistSymmFrontTree<Real> JFrontTree;

    DistMultiVec<Real> rhs(comm),
                       rmu(comm), rb(comm), rc(comm), 
                       dx(comm),  dy(comm), dz(comm);
    DistNodalMultiVec<Real> rhsNodal;

    DistMultiVec<Real> regCand(comm), reg(comm);
    DistNodalMultiVec<Real> regCandNodal, regNodal;

#ifndef EL_RELEASE
    DistMultiVec<Real> dxError(comm), dyError(comm), dzError(comm);
#endif
    for( Int numIts=0; ; ++numIts )
    {
        // Check that no entries of x or z are non-positive
        // ================================================
        Int numNonPos_x = 0;
        for( Int iLoc=0; iLoc<x.LocalHeight(); ++iLoc )
            if( x.GetLocal(iLoc,0) <= Real(0) )
                ++numNonPos_x;
        numNonPos_x = mpi::AllReduce( numNonPos_x, comm );
        Int numNonPos_z = 0;
        for( Int iLoc=0; iLoc<z.LocalHeight(); ++iLoc )
            if( z.GetLocal(iLoc,0) <= Real(0) )
                ++numNonPos_z;
        numNonPos_z = mpi::AllReduce( numNonPos_z, comm );
        if( numNonPos_x > 0 || numNonPos_z > 0 )
            LogicError
            (numNonPos_x," entries of x were nonpositive and ",
             numNonPos_z," entries of z were nonpositive");

        // Check for convergence
        // =====================
        // |c^T x + x^T Q x - b^T y| / (1 + |c^T x + 1/2 x^T Q x|) <= tol ?
        // ----------------------------------------------------------------
        Zeros( rhs, n, 1 );
        // NOTE: This assumes that Q is explicitly Hermitian
        Multiply( NORMAL, Real(1), Q, x, Real(0), rhs );
        const Real xTQx = Dot(x,rhs);
        const Real primObj = Dot(c,x) + xTQx/2;
        const Real dualObj = Dot(b,y) - xTQx/2;
        const Real objConv = Abs(primObj-dualObj) / (Real(1)+Abs(primObj));
        // || r_b ||_2 / (1 + || b ||_2) <= tol ?
        // --------------------------------------
        const Real bNrm2 = Nrm2( b );
        rb = b;
        Multiply( NORMAL, Real(1), A, x, Real(-1), rb );
        const Real rbNrm2 = Nrm2( rb );
        const Real rbConv = rbNrm2 / (Real(1)+bNrm2);
        // || r_c ||_2 / (1 + || c + Q x ||_2) <= tol ?
        // --------------------------------------------
        // NOTE: rhs currently contains Q x
        Axpy( Real(1), c, rhs );
        const Real objGradNrm2 = Nrm2( rhs );
        rc = rhs;
        Multiply( TRANSPOSE, Real(1), A, y, Real(-1), rc );
        Axpy( Real(1), z, rc );
        const Real rcNrm2 = Nrm2( rc );
        const Real rcConv = rcNrm2 / (Real(1)+objGradNrm2);
        // Now check the pieces
        // --------------------
        if( objConv <= ctrl.tol && rbConv <= ctrl.tol && rcConv <= ctrl.tol )
            break;
        else if( ctrl.print && commRank == 0 )
            std::cout << " iter " << numIts << ":\n"
                      << "  |primObj - dualObj| / (1 + |primObj|) = "
                      << objConv << "\n"
                      << "  || r_b ||_2 / (1 + || b ||_2)         = "
                      << rbConv << "\n"
                      << "  || r_c ||_2 / (1 + || c + Q x ||_2)   = "
                      << rcConv << std::endl;

        // Raise an exception after an unacceptable number of iterations
        // =============================================================
        if( numIts == ctrl.maxIts )
            RuntimeError
            ("Maximum number of iterations (",ctrl.maxIts,") exceeded");

        // Compute the duality measure and r_mu = X Z e - tau e
        // ====================================================
        const Real mu = Dot(x,z) / n;
        rmu.Resize( n, 1 );
        for( Int iLoc=0; iLoc<rmu.LocalHeight(); ++iLoc )
        {
            const Real xi = x.GetLocal(iLoc,0);
            const Real zi = z.GetLocal(iLoc,0);
            rmu.SetLocal( iLoc, 0, xi*zi - ctrl.centering*mu );
        }

        // Compute the search direction
        // ============================
        const Real minReductionFactor = 2;
        const Int maxRefineIts = 10;
        // ctrl.system == AUGMENTED_KKT
        {
            // Construct the "normal" KKT system
            // ---------------------------------
            // TODO: Add default regularization
            AugmentedKKT( Q, A, x, z, J, false );
            AugmentedKKTRHS( x, rmu, rc, rb, rhs );
            const Real pivTol = MaxNorm(J)*epsilon;
            const Real regMagPrimal = Pow(epsilon,Real(0.75));
            const Real regMagLagrange = Pow(epsilon,Real(0.5));
            regCand.Resize( n+m, 1 );
            for( Int iLoc=0; iLoc<regCand.LocalHeight(); ++iLoc )
            {
                const Int i = regCand.FirstLocalRow() + iLoc;
                if( i < n )
                    regCand.SetLocal( iLoc, 0, -regMagPrimal );
                else
                    regCand.SetLocal( iLoc, 0, regMagLagrange );
            }
            // Do not use any a priori regularization
            Zeros( reg, m+n, 1 );

            // Compute the proposed step from the KKT system
            // ---------------------------------------------
            if( numIts == 0 )
            {
                NestedDissection( J.LockedDistGraph(), map, sepTree, info );
                map.FormInverse( invMap );
            }
            JFrontTree.Initialize( J, map, sepTree, info );
            regCandNodal.Pull( invMap, info, regCand );
            regNodal.Pull( invMap, info, reg );
            RegularizedLDL
            ( info, JFrontTree, pivTol, regCandNodal, regNodal, LDL_1D );
            regNodal.Push( invMap, info, reg );
            // TODO: Iterative refinement
            /*
            SolveWithIterativeRefinement
            ( J, invMap, info, JFrontTree, rhs, 
              minReductionFactor, maxRefineIts );
            */
            rhsNodal.Pull( invMap, info, rhs );
            Solve( info, JFrontTree, rhsNodal );
            rhsNodal.Push( invMap, info, rhs );
            ExpandAugmentedSolution( x, z, rmu, rhs, dx, dy, dz );
        }

#ifndef EL_RELEASE
        // Sanity checks
        // =============
        const Real rmuNrm2 = Nrm2( rmu );
        dzError = rmu;
        for( Int iLoc=0; iLoc<dzError.LocalHeight(); ++iLoc )
        {
            const Real xi = x.GetLocal(iLoc,0);
            const Real zi = z.GetLocal(iLoc,0);
            const Real dxi = dx.GetLocal(iLoc,0);
            const Real dzi = dz.GetLocal(iLoc,0);
            dzError.UpdateLocal( iLoc, 0, xi*dzi + zi*dxi );
        }
        const Real dzErrorNrm2 = Nrm2( dzError );

        dyError = dz;
        Multiply( TRANSPOSE, Real(1), A, dy, Real(1), dyError );
        Multiply( NORMAL, Real(-1), Q, dx, Real(1), dyError );
        Axpy( Real(1), rc, dyError );
        const Real dyErrorNrm2 = Nrm2( dyError );

        dxError = rb;
        Multiply( NORMAL, Real(1), A, dx, Real(1), dxError );
        const Real dxErrorNrm2 = Nrm2( dxError );

        if( ctrl.print && commRank == 0 )
            std::cout << "  || dxError ||_2 / (1 + || r_b ||_2)  = " 
                      << dxErrorNrm2/(1+rbNrm2) << "\n"
                      << "  || dyError ||_2 / (1 + || r_c ||_2)  = " 
                      << dyErrorNrm2/(1+rcNrm2) << "\n"
                      << "  || dzError ||_2 / (1 + || r_mu ||_2) = " 
                      << dzErrorNrm2/(1+rmuNrm2) << std::endl;
#endif

        // Decide on the step length
        // =========================
        const Real alpha = 
          IPFLineSearch
          ( Q, A, b, c, x, y, z, dx, dy, dz, 
            ctrl.tol*(1+bNrm2), ctrl.tol*(1+objGradNrm2), ctrl.lineSearchCtrl );
        if( ctrl.print && commRank == 0 )
            std::cout << "  alpha = " << alpha << std::endl;

        // Update the state by stepping a distance of alpha
        // ================================================
        Axpy( alpha, dx, x );
        Axpy( alpha, dy, y );
        Axpy( alpha, dz, z );
    }
}

#define PROTO(Real) \
  template void IPF \
  ( const Matrix<Real>& Q, const Matrix<Real>& A, \
    const Matrix<Real>& b, const Matrix<Real>& c, \
    Matrix<Real>& x, Matrix<Real>& y, Matrix<Real>& z, \
    const IPFCtrl<Real>& ctrl ); \
  template void IPF \
  ( const AbstractDistMatrix<Real>& Q, const AbstractDistMatrix<Real>& A, \
    const AbstractDistMatrix<Real>& b, const AbstractDistMatrix<Real>& c, \
    AbstractDistMatrix<Real>& x, AbstractDistMatrix<Real>& y, \
    AbstractDistMatrix<Real>& z, \
    const IPFCtrl<Real>& ctrl ); \
  template void IPF \
  ( const SparseMatrix<Real>& Q, const SparseMatrix<Real>& A, \
    const Matrix<Real>& b,       const Matrix<Real>& c, \
    Matrix<Real>& x, Matrix<Real>& y, Matrix<Real>& z, \
    const IPFCtrl<Real>& ctrl ); \
  template void IPF \
  ( const DistSparseMatrix<Real>& Q, const DistSparseMatrix<Real>& A, \
    const DistMultiVec<Real>& b,     const DistMultiVec<Real>& c, \
    DistMultiVec<Real>& x, DistMultiVec<Real>& y, DistMultiVec<Real>& z, \
    const IPFCtrl<Real>& ctrl );

#define EL_NO_INT_PROTO
#define EL_NO_COMPLEX_PROTO
#include "El/macros/Instantiate.h"

} // namespace primal
} // namespace qp
} // namespace El