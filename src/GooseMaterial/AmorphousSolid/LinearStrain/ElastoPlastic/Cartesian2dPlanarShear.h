/* ========================================== DESCRIPTION ==========================================

(c - GPLv3) T.W.J. de Geus (Tom) | tom@geus.me | www.geus.me | github.com/tdegeus/GooseMaterial

Description
-----------

Elastic plastic material for amorphous solids.

Suggested references
--------------------

*   The code + comments below.
*   docs/AmorphousSolid/LinearStrain/ElastoPlastic/readme.pdf

================================================================================================= */

#define _USE_MATH_DEFINES // to use "M_PI" from "math.h"

#include <tuple>
#include <math.h>
#include <cppmat/tensor2.h>

#warning "GooseMaterial/AmorphousSolid/LinearStrain/ElastoPlastic/Cartesian2dPlanarShear.h : first usage, careful check then remove this message"

namespace GooseMaterial {
namespace AmorphousSolid {
namespace LinearStrain {
namespace ElastoPlastic {
namespace Cartesian2dPlanarShear {

using V   = cppmat::vector2   <double>;
using T2s = cppmat::tensor2_2s<double>;
using T2d = cppmat::tensor2_2d<double>;

// ============================================ OVERVIEW ===========================================

class Material
{
private:

  double              m_K;    // bulk  modulus
  double              m_G;    // shear modulus
  std::vector<double> m_epsy; // yield strains
  V                   m_n;    // normal of the weak layer

public:

  // constructor / destructor
 ~Material(){};
  Material(){};
  Material(double K, double G, const V &n, const std::vector<double> &epsy={}, bool init_elastic=true);

  // compute stress at "Eps"
  T2s stress(const T2s &Eps);

  // post-process functions
  // - find the index of the current yield strain (below "eps_d")
  size_t find(double     epsd);
  size_t find(const T2s &Eps );
  // - return yield strain "i"
  double eps_y(size_t i);
  // - return hydrostatic/deviatoric equivalent stress/strain
  double eps_m(const T2s &Eps);
  double eps_d(const T2s &Eps);
  double eps_s(const T2s &Eps);
  double eps_n(const T2s &Eps);
  double sig_m(const T2s &Sig);
  double sig_d(const T2s &Sig);
  // - return the strain energy (or its hydrostatic or deviatoric component)
  double energy  (double     epsm, double epss, double epsn);
  double energy_m(double     epsm                          );
  double energy_s(double     epss                          );
  double energy_n(double     epsn                          );
  double energy  (const T2s &Eps                           );
  double energy_m(const T2s &Eps                           );
  double energy_s(const T2s &Eps                           );
  double energy_n(const T2s &Eps                           );

};

// ===================================== IMPLEMENTATION : CORE =====================================

Material::Material(double K, double G, const V &n, const std::vector<double> &epsy, bool init_elastic)
{
  // check input - normal of the weak layer
  if ( n.norm() <= 0. )
    throw std::runtime_error("Normal vector cannot be null vector");

  // copy input - normal of the weak layer
  m_n  = n;
  m_n.setUnitLength();

  // copy input - elastic moduli
  m_K = K;
  m_G = G;

  // copy input - yield strains
  std::vector<double> vec = epsy;
  // initialize: no yield strain -> only elastic
  if ( vec.size() == 0 ) vec = { 1000000. };
  // sort input
  std::sort( vec.begin() , vec.end() );

  // check to add item to have an initial elastic response
  if ( init_elastic )
    if ( vec[0] == -vec[1] )
      init_elastic = false;

  // copy input
  // - counters
  size_t N = vec.size();
  size_t i = 0;
  // - add yield strain to have an initial elastic response
  if ( init_elastic ) ++N;
  // - allocate
  m_epsy.resize(N);
  // - add yield strain to have an initial elastic response
  if ( init_elastic ) { m_epsy[i] = -vec[0]; ++i; }
  // - copy the rest
  for ( auto &j : vec ) {
    m_epsy[i] = j; ++i;
  }

  // check the number of yield strains
  if ( m_epsy.size() < 2 )
    throw std::runtime_error("Specify at least two yield strains 'eps_y'");
}

// -------------------------------------------------------------------------------------------------

size_t Material::find(double epsd)
{
  // check extremes
  if ( epsd < m_epsy.front() or epsd >= m_epsy.back() )
    throw std::runtime_error("Insufficient 'eps_y'");

  // set initial search bounds and index
  size_t n = m_epsy.size()-1;  // upper-bound
  size_t z = 1;                // lower-bound
  size_t l = 0;                // left-bound
  size_t r = n;                // right-bound
  size_t i = r / 2;            // average

  // loop until found
  while ( true )
  {
    // check if found, unroll once to speed-up
    if ( epsd >= m_epsy[i-1] and epsd < m_epsy[i  ] ) return i-1;
    if ( epsd >= m_epsy[i  ] and epsd < m_epsy[i+1] ) return i;
    if ( epsd >= m_epsy[i+1] and epsd < m_epsy[i+2] ) return i+1;

    // correct the left- and right-bound
    if ( epsd >= m_epsy[i] ) l = i;
    else                      r = i;

    // set new search index
    i = ( r + l ) / 2;
    i = std::max(i,z);
  }
}

// -------------------------------------------------------------------------------------------------

T2s Material::stress(const T2s &Eps)
{
  // decompose strain: hydrostatic part, deviatoric part
  T2d    I     = cppmat::identity2_2();
  double epsm  = Eps.trace()/2.;
  T2s    Epsd  = Eps - epsm*I;

  // constitutive response - hydrostatic part
  double sigm  = m_K * epsm;

  // get strain vector, and its equivalent project on the plane
  V sn = Epsd.dot(m_n);               sn.setUnitLength();
  V s  = sn - ( sn.dot(m_n) ) * m_n;  s .setUnitLength();

  // decompose deviatoric strain in a planar part and a non-planar part
  double epss  = cppmat::dot(s.dot(Epsd),m_n);
  T2s    Epss  = epss * ( s.dyadic(m_n) + m_n.dyadic(s) ).astensor2s();
  T2s    Epsn  = Epsd - Epss;

  // planar equivalent strain zero -> only non-planar elastic deviatoric stress
  if ( epss <= 0. ) return sigm * I + m_G * Epsn;

  // read current yield strains
  size_t i       = find(epss);
  double eps_min = ( m_epsy[i+1] + m_epsy[i] ) / 2.;
  double deps_y  = ( m_epsy[i+1] - m_epsy[i] ) / 2.;

  // constitutive response - deviatoric part
  T2s Sigs = ( ( m_G/epss ) * ( deps_y/M_PI ) * sin ( M_PI/deps_y * (epss-eps_min) ) ) * Epss;

  // return full strain tensor
  return sigm*I + m_G * Epsn + Sigs;
}

// ================================= IMPLEMENTATION : POST-PROCESS =================================

size_t Material::find(const T2s &Eps)
{
  return find(eps_d(Eps));
}

// -------------------------------------------------------------------------------------------------

double Material::eps_y(size_t i)
{
  return m_epsy[i];
}

// -------------------------------------------------------------------------------------------------

double Material::eps_m(const T2s &Eps)
{
  return Eps.trace()/2.;
}

// -------------------------------------------------------------------------------------------------

double Material::eps_d(const T2s &Eps)
{
  T2d    I    = cppmat::identity2_2();
  double epsm = Eps.trace()/2.;
  T2s    Epsd = Eps - epsm*I;

  return std::pow( .5*Epsd.ddot(Epsd) , 0.5 );
}

// -------------------------------------------------------------------------------------------------

double Material::eps_s(const T2s &Eps)
{
  T2d    I    = cppmat::identity2_2();
  double epsm = Eps.trace()/2.;
  T2s    Epsd = Eps - epsm*I;

  // get strain vector, and its equivalent project on the plane
  V sn = Epsd.dot(m_n);               sn.setUnitLength();
  V s  = sn - ( sn.dot(m_n) ) * m_n;  s .setUnitLength();

  return cppmat::dot(s.dot(Epsd),m_n);
}

// -------------------------------------------------------------------------------------------------

double Material::eps_n(const T2s &Eps)
{
  T2d    I    = cppmat::identity2_2();
  double epsm = Eps.trace()/2.;
  T2s    Epsd = Eps - epsm*I;

  // get strain vector, and its equivalent project on the plane
  V sn = Epsd.dot(m_n);               sn.setUnitLength();
  V s  = sn - ( sn.dot(m_n) ) * m_n;  s .setUnitLength();

  // decompose deviatoric strain in a planar part and a non-planar part
  double epss  = cppmat::dot(s.dot(Epsd),m_n);
  T2s    Epss  = epss * ( s.dyadic(m_n) + m_n.dyadic(s) ).astensor2s();
  T2s    Epsn  = Epsd - Epss;

  return std::pow( .5*Epsn.ddot(Epsn) , 0.5 );
}

// -------------------------------------------------------------------------------------------------

double Material::sig_m(const T2s &Sig)
{
  return Sig.trace()/2.;
}

// -------------------------------------------------------------------------------------------------

double Material::sig_d(const T2s &Sig)
{
  T2d    I    = cppmat::identity2_2();
  double sigm = Sig.trace()/2.;
  T2s    Sigd = Sig - sigm*I;

  return std::pow( .5*Sigd.ddot(Sigd) , 0.5 );
}

// -------------------------------------------------------------------------------------------------

double Material::energy_m(double epsm)
{
  return m_K * std::pow( epsm , 2. );
}

// -------------------------------------------------------------------------------------------------

double Material::energy_m(const T2s &Eps)
{
  return energy_m(eps_m(Eps));
}

// -------------------------------------------------------------------------------------------------

double Material::energy_s(double epss)
{
  size_t i       = find(epss);
  double eps_min = ( m_epsy[i+1] + m_epsy[i] ) / 2.;
  double deps_y  = ( m_epsy[i+1] - m_epsy[i] ) / 2.;

  return -2. * m_G * std::pow( deps_y/M_PI , 2. ) * ( 1. + cos( M_PI/deps_y * (epss-eps_min) ) );
}

// -------------------------------------------------------------------------------------------------

double Material::energy_s(const T2s &Eps)
{
  return energy_s(eps_s(Eps));
}

// -------------------------------------------------------------------------------------------------

double Material::energy_n(double epsn)
{
  return m_G * std::pow( epsn , 2. );
}

// -------------------------------------------------------------------------------------------------

double Material::energy_n(const T2s &Eps)
{
  return energy_n(eps_n(Eps));
}

// -------------------------------------------------------------------------------------------------

double Material::energy(double epsm, double epss, double epsn)
{
  return energy_m(epsm) + energy_s(epss) + energy_n(epsn);
}

// -------------------------------------------------------------------------------------------------

double Material::energy(const T2s &Eps)
{
  return energy_m(eps_m(Eps)) + energy_s(eps_s(Eps)) + energy_n(eps_n(Eps));
}

// =================================================================================================

} // namespace ...
} // namespace ...
} // namespace ...
} // namespace ...
} // namespace ...