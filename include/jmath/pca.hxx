/* $Id$ */

#include "jafarConfig.h"

#ifdef HAVE_BOOST_SANDBOX
#ifdef HAVE_LAPACK

#include <cmath>
#include "jmath/pca.hpp"
#include "jmath/jmathException.hpp"
#include <boost/numeric/bindings/lapack/driver/syev.hpp>
#include <boost/numeric/bindings/ublas/matrix.hpp>
#include <boost/numeric/bindings/ublas/vector.hpp>
#include <boost/numeric/bindings/ublas/symmetric.hpp>

namespace lapack = boost::numeric::bindings::lapack;
namespace ublas = boost::numeric::ublas;
using namespace std;
using namespace jafar::jmath;

template<typename T>
void PCA_T<T>::batchPCA(const ublas::matrix<T>& X_, int dim_) {
  int m = X_.size1();
  int n = X_.size2();
  JFR_PRECOND((0 <= dim_) && (dim_ <= min(m,n)),
	      "PCA::batchPCA: wrong dimension input. must be in [1," << min(m,n) << "]");
  if(dim_ == 0)
    dim_ = min(m,n);
  ublas::matrix<T> centeredX(X_);
  // compute mean and center X
  mean.resize(m);
  for (int i=0; i<m; i++) {
    mean(i) = 0.0;
    for (int j=0; j<n; j++)
      mean(i) += centeredX(i,j);
	  
    mean(i) /= n;
    for (int j=0; j<n; j++)
      centeredX(i,j) -= mean(i);
  }
  // compute svd
  if (m <= n) {
    ublas::matrix<T,ublas::column_major> A = ublas::prod(centeredX,ublas::trans(centeredX));
    A /= n;
    ublas::symmetric_adaptor<ublas::matrix<T, ublas::column_major>, ublas::upper > s_A(A);
    ublas::vector<T> alpha(m);
    int ierr = lapack::syev('V',s_A,alpha);	  
    if (ierr!=0)
      JFR_RUN_TIME("PCA::batchPCA: error in lapack::syev() function, ierr=" << ierr);
    eigenvectors.resize(m,m);
		eigenvalues.resize(m);
    for(int i=0;i<m;i++) {
      eigenvalues(i) = alpha(m-i-1);
      ublas::column(eigenvectors,i) = ublas::column(A,m-i-1);
    }
  } else {
    ublas::matrix<T,ublas::column_major> A = ublas::prod(ublas::trans(centeredX),centeredX);
    A /= n;
    ublas::symmetric_adaptor<ublas::matrix<T, ublas::column_major>, ublas::upper > s_A(A);
    ublas::vector<T> alpha(n);
    int ierr = lapack::syev('V',s_A,alpha);
    if (ierr!=0)
      JFR_RUN_TIME("PCA::batchPCA: error in lapack::syev() function, ierr=" << ierr);
    eigenvectors.resize(m,n);
		eigenvalues.resize(n);
    for(int i=0;i<n;i++) {
      eigenvalues(i) = alpha(n-i-1);
      ublas::column(eigenvectors,i) = ublas::column(A,n-i-1);
    }
    eigenvectors = ublas::prod(centeredX,eigenvectors);
    for(int j=0; j<n; j++) {
      ublas::matrix_column< ublas::matrix<T> > Uc(eigenvectors,j);
      Uc /= sqrt(n * eigenvalues(j));
    }
  }
  // reduce the subspace if needed
	JFR_DEBUG("reduce subspace if needed");
  if ((0 < dim_) && (dim_ < min(m,n))) {
    eigenvalues = ublas::project(eigenvalues,ublas::range(min(m,n)-dim_,min(m,n)));
    eigenvectors = ublas::project(eigenvectors,ublas::range(0,m),
				  ublas::range(min(m,n)-dim_,min(m,n)));
  }
  if (!basis_only)  
    coefficients = ublas::prod(ublas::trans(eigenvectors),centeredX); 
}

template<typename T>
void PCA_T<T>::updatePCA(const ublas::vector<T>& I_, UFlag f_, T thd_) {
  JFR_PRECOND(mean.size() != 0,
	      "PCA::project: no eigenspace available");
  JFR_PRECOND(I_.size() == eigenvectors.size1(),
	      "PCA::project: wrong size of the input vector. should be " << eigenvectors.size1());
  int n = eigenvectors.size2();
  ublas::vector<T> meanp = (n*mean+I_)/static_cast<T>(n+1);
  ublas::vector<T> a = ublas::prod(ublas::trans(eigenvectors),(I_-mean));
  ublas::vector<T> y = ublas::prod(eigenvectors,a)+mean;
  ublas::vector<T> h = y-I_;
  T normh = ublas::norm_2(h);
  if (normh > 0) 
    h /= normh;
  else
    h.clear();
  T gamma = ublas::inner_prod(ublas::trans(h),(I_-mean));
  ublas::matrix<T,ublas::column_major> D(a.size()+1,a.size()+1);
  D.clear();
  ublas::project(D,ublas::range(0,a.size()),
		 ublas::range(0,a.size())) = ublas::outer_prod(a,a);
  D /= static_cast<T>(n)/static_cast<T>((n+1)*(n+1));
  for(std::size_t i=0; i < a.size(); i++) {
    D(i,i) += static_cast<T>(n)/static_cast<T>(n+1)*eigenvalues(i);
    D(D.size1()-1,i) = static_cast<T>(n)/static_cast<T>((n+1)*(n+1))*gamma*a(i);
    D(i,D.size2()-1) = static_cast<T>(n)/static_cast<T>((n+1)*(n+1))*gamma*a(i);
    D(D.size1()-1,D.size2()-1) = static_cast<T>(n)/static_cast<T>((n+1)*(n+1))*gamma*gamma;
  }
  ublas::symmetric_adaptor<ublas::matrix<T, ublas::column_major>, ublas::upper > s_D(D);
  ublas::vector<T> alphap(D.size1());
  int ierr = lapack::syev('V',s_D,alphap);	  
  if (ierr!=0)
    JFR_RUN_TIME("PCA::updatePCA: error in lapack::syev() function, ierr=" << ierr);
  ublas::matrix<T> R(D.size1(),D.size2());
  eigenvalues.resize(eigenvalues.size()+1);
  for(std::size_t i=0;i<eigenvalues.size();i++) {
    eigenvalues(i) = alphap(eigenvalues.size()-i-1);
    ublas::column(R,i) = ublas::column(D,D.size2()-i-1);
  }
  ublas::matrix<T> Up(eigenvectors.size1(),eigenvectors.size2()+1);
  Up.clear();
  ublas::project(Up,ublas::range(0,eigenvectors.size1()),
		 ublas::range(0,eigenvectors.size2())).assign(eigenvectors);
  ublas::column(Up,Up.size2()-1) = h;
  eigenvectors = ublas::prod(Up,R);
  if (!basis_only) {
    ublas::vector<T> etha = ublas::prod(ublas::trans(Up),(mean-meanp));
    coefficients.resize(coefficients.size1()+1,coefficients.size2()+1);
    for(std::size_t i=0; i<coefficients.size2()-1; i++) {
      coefficients(coefficients.size1()-1,i) = 0;
      ublas::column(coefficients,i) = ublas::prod(ublas::trans(R),
						  ublas::column(coefficients,i))+etha;
    }
    a.resize(a.size()+1);
    a(a.size()-1) = 0;
    ublas::column(coefficients,coefficients.size2()-1) = ublas::prod(ublas::trans(R),a)+etha;
  }
  mean = meanp;
  switch (f_) {   
  case ore: 
  case increase:
    if (eigenvectors.size1() >= eigenvectors.size2())
      break;
  case preserve:
    if (!basis_only)
      coefficients = ublas::project(coefficients,
				    ublas::range(0,coefficients.size1()-1),
				    ublas::range(0,coefficients.size2()));
    eigenvectors = ublas::project(eigenvectors,
				  ublas::range(0,eigenvectors.size1()),
				  ublas::range(0,eigenvectors.size2()-1));
    eigenvalues.resize(eigenvalues.size()-1);
    break;
  default:
    JFR_RUN_TIME("PCA::UpdatePCA: unknown UFlag");
  }
}

template<typename T>
ublas::vector<T> PCA_T<T>::project(const ublas::vector<T>& I_) const {
  JFR_PRECOND(mean.size() != 0,
	      "PCA::project: no eigenspace available");
  JFR_PRECOND(I_.size() == eigenvectors.size1(),
	      "PCA::project: wrong size of the input vector. should be " << eigenvectors.size1());
  return ublas::prod(ublas::trans(eigenvectors),(I_-mean));
}
template<typename T>
ublas::vector<T> PCA_T<T>::reconstruct(const ublas::vector<T>& P_) const {
  JFR_PRECOND(mean.size() != 0,
	      "PCA::project: no eigenspace available");
  JFR_PRECOND(P_.size() <= eigenvectors.size2(),
	      "PCA::project: wrong size of the input vector. should be in [1," << eigenvectors.size2() << "]");
  return (ublas::prod(eigenvectors,P_) + mean);
}

#endif
#endif