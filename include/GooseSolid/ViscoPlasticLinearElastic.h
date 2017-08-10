/* ========================================== DESCRIPTION ==========================================

(c - GPLv3) T.W.J. de Geus (Tom) | tom@geus.me | www.geus.me | github.com/tdegeus/GooseMesh

Overview
--------

class ViscoPlasticLinearElastic
|- stress
|- tangent
|- increment

Description
-----------

Elasto-visco-plastic material model, whereby the elasticity is based on linear elasticity (i.e. a
linear relationship between the linear strain and the Cauchy stress).

Suggested references
--------------------

*   The code + comments below.
*   docs/ViscoPlasticLinearElastic/ViscoPlasticLinearElastic.pdf
*   Former internal code: GooseFEM / mat3202

================================================================================================= */

#include <tuple>
#include <cppmat/tensor.h>

using T2  = cppmat::tensor2<double>;
using T2s = cppmat::tensor2s<double>;
using T2d = cppmat::tensor2d<double>;
using T4  = cppmat::tensor4<double>;

namespace GooseSolid {

// ============================================ OVERVIEW ===========================================

class ViscoPlasticLinearElastic
{
private:
  double m_K      ; // material parameter : bulk  modulus
  double m_G      ; // material parameter : shear modulus
  double m_sig0   ; // material parameter : 'yield' stress
  double m_gamma0 ; // material parameter : 'yield' strain
  double m_m      ; // material parameter : non-linearity
  T2s    m_eps    ; // history  parameter : strain tensor
  T2s    m_eps_n  ; // history  parameter : strain tensor at last increment
  T2s    m_epse   ; // history  parameter : elastic strain tensor
  T2s    m_epse_n ; // history  parameter : elastic strain tensor at last increment
  double m_ep     ; // history  parameter : accumulated plastic strain
  double m_ep_n   ; // history  parameter : accumulated plastic strain at last increment

public:
  // constructor / destructor
 ~ViscoPlasticLinearElastic(){};
  ViscoPlasticLinearElastic(){};
  ViscoPlasticLinearElastic(double K, double G, double sig0, double gamma0, double m=1.);

  // compute stress(+tangent) at "eps", depending on the history stored in this class
  T2s                stress (const T2s &eps, const double dt);
  std::tuple<T4,T2s> tangent(const T2s &eps, const double dt);

  // update history
  void increment();

  // perform actual computations
  std::tuple<T4,T2s> f_compute(const T2s &eps, const double dt, bool stress_only=false);
};

// ========================================= IMPLEMENTATION ========================================

ViscoPlasticLinearElastic::ViscoPlasticLinearElastic(
  double K, double G, double sig0 , double gamma0   , double m ) :
  m_K(K)  , m_G(G)  , m_sig0(sig0), m_gamma0(gamma0), m_m(m)
{
  // resize history tensors
  m_eps   .resize(3);
  m_eps_n .resize(3);
  m_epse  .resize(3);
  m_epse_n.resize(3);

  // initialize stress/strain free state
  m_eps_n .zeros();
  m_epse_n.zeros();
  m_ep_n = 0.0;
};

// -------------------------------------------------------------------------------------------------

void ViscoPlasticLinearElastic::increment()
{
  m_eps_n  = m_eps ;
  m_epse_n = m_epse;
  m_ep_n   = m_ep  ;
}

// -------------------------------------------------------------------------------------------------

T2s ViscoPlasticLinearElastic::stress(const T2s &eps, const double dt)
{
  T2s sig;
  T4  K4;

  std::tie(K4,sig) = this->f_compute(eps,dt,true);

  return sig;
};

// -------------------------------------------------------------------------------------------------

std::tuple<T4,T2s> ViscoPlasticLinearElastic::tangent(const T2s &eps, const double dt)
{
  return this->f_compute(eps,dt,false);
}


// -------------------------------------------------------------------------------------------------

std::tuple<T4,T2s> ViscoPlasticLinearElastic::f_compute(const T2s &eps, const double dt,
  bool stress_only)
{
  double dgamma = 0.0;
  double epse_m,sig_m,sig_eq;
  T2s epse_d,sig_d,sig,N;
  T2d I;

  // stress response
  // ---------------

  // second order identity tensor
  I      = cppmat::identity2(3);

  // total strain, trial elastic strain, and accumulated plastic strain
  m_eps  = eps;
  m_epse = m_epse_n + ( eps - m_eps_n );
  m_ep   = m_ep_n;

  // decompose trial elastic strain: hydrostatic part, deviatoric part
  epse_m = m_epse.trace()/3.;
  epse_d = m_epse - epse_m * I;

  // calculate the trial stress: hydrostatic and deviatoric part, and equivalent stress
  sig_m  = 3. * m_K * epse_m;
  sig_d  = 2. * m_G * epse_d;
  sig_eq = std::pow( 1.5 * sig_d.ddot(sig_d) , 0.5 );

  // return-map
  if ( sig_eq > 0.0 )
  {
    // - compute slip rate: linear stress sensitivity ( m == 1 )
    if ( std::abs(m_m-1.) < 1.e-6 )
    {
      dgamma = sig_eq / ( 3.*m_G + m_sig0/(m_gamma0*dt) );
    }
    // - compute slip rate: non-linear stress sensitivity ( m != 1 )
    else
    {
      int    i = 0;
      double R,dR;
      // -- loop until residual vanishes: do { ... } while ( residual > norm )
      do
      {
        // --- residual
        R  = dgamma - m_gamma0 * dt * std::pow( (sig_eq-3.*m_G*dgamma)/m_sig0 , 1./m_m);
        // --- derivative of the residual
        dR = 1. + 3.*m_G*m_gamma0*dt/m_m * std::pow( (sig_eq-3.*m_G*dgamma)/m_sig0 , 1./m_m-1.);
        // --- update the plastic multiplier
        dgamma -= R/dR;
        // --- limit maximum number of iterations
        if ( i>20 ) throw std::runtime_error("Return-map not succeeded");
        ++i;
      }
      while ( std::abs(R/m_gamma0) > 1.e-6 );
    }

    // - yield surface normal (for tangent)
    N      = 1.5 * sig_d/sig_eq;
    // - correct trial state
    sig_d  = ( 1.  -  3.*m_G*dgamma/sig_eq ) * sig_d;
    epse_d = sig_d / (2.*m_G);
    m_ep  += dgamma;
  }

  // combine volumetric and deviatoric stress/strain
  sig    = sig_m  * I + sig_d ;
  m_epse = epse_m * I + epse_d;

  // tangent
  // -------

  if ( stress_only ) {
    T4 K4(0);
    return std::make_tuple(K4,sig);
  }

  // fourth order deviator unit tensor (i.e. A_d = I4d : A)
  T4 I4d = cppmat::identity4d (3);
  T4 II  = cppmat::identity4II(3);

  // initialize tangent as the elasticity tensor
  T4 K4  = m_K * II + 2. * m_G * I4d;

  // plastic part: only when yielding
  if ( sig_eq > 0.0 )
  {
    // - update the tangent (1/2)
    K4 -= 6. * std::pow(m_G,2.) * dgamma/sig_eq * I4d;
    // - update the tangent (2/2)
    K4 += 4. * std::pow(m_G,2.) * (
      dgamma/sig_eq - 1./(3.*m_G + m_m*m_sig0/(m_gamma0*dt) * std::pow(dgamma/(m_gamma0*dt),m_m-1.))
    ) * N.dyadic(N);
  }

  return std::make_tuple(K4,sig);
}

// -------------------------------------------------------------------------------------------------

}
