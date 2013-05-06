/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2013 Joseph Gaeddert
 *
 * This file is part of liquid.
 *
 * liquid is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * liquid is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with liquid.  If not, see <http://www.gnu.org/licenses/>.
 */

//
// iirfilt : Infinite impulse response filter
//
// References:
//  [Pintelon:1990] Rik Pintelon and Johan Schoukens, "Real-Time
//      Integration and Differentiation of Analog Signals by Means of
//      Digital Filtering," IEEE Transactions on Instrumentation and
//      Measurement, vol 39 no. 6, December 1990.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// defined:
//  IIRFILT()       name-mangling macro
//  TO              output type
//  TC              coefficients type
//  TI              input type
//  WINDOW()        window macro
//  DOTPROD()       dotprod macro
//  PRINTVAL()      print macro

// use structured dot product? 0:no, 1:yes
#define LIQUID_IIRFILT_USE_DOTPROD   (0)

struct IIRFILT(_s) {
    TC * b;             // numerator (feed-forward coefficients)
    TC * a;             // denominator (feed-back coefficients)
    TI * v;             // internal filter state (buffer)
    unsigned int n;     // filter length (order+1)

    unsigned int nb;    // numerator length
    unsigned int na;    // denominator length

    // filter structure type
    enum {
        IIRFILT_TYPE_NORM=0,
        IIRFILT_TYPE_SOS
    } type;

#if LIQUID_IIRFILT_USE_DOTPROD
    DOTPROD() dpb;      // numerator dot product
    DOTPROD() dpa;      // denominator dot product
#endif

    // second-order sections 
    IIRFILTSOS() * qsos;    // second-order sections filters
    unsigned int nsos;      // number of second-order sections
};

// create iirfilt (infinite impulse response filter) object
//  _b      :   numerator, feed-forward coefficients [size: _nb x 1]
//  _nb     :   length of numerator
//  _a      :   denominator, feed-back coefficients [size: _na x 1]
//  _na     :   length of denominator
IIRFILT() IIRFILT(_create)(TC * _b,
                           unsigned int _nb,
                           TC * _a,
                           unsigned int _na)
{
    // validate input
    if (_nb == 0) {
        fprintf(stderr,"error: iirfilt_%s_create(), numerator length cannot be zero\n", EXTENSION_FULL);
        exit(1);
    } else if (_na == 0) {
        fprintf(stderr,"error: iirfilt_%s_create(), denominator length cannot be zero\n", EXTENSION_FULL);
        exit(1);
    }
    
    unsigned int i;

    // create structure and initialize
    IIRFILT() q = (IIRFILT()) malloc(sizeof(struct IIRFILT(_s)));
    q->nb = _nb;
    q->na = _na;
    q->n = (q->na > q->nb) ? q->na : q->nb;
    q->type = IIRFILT_TYPE_NORM;

    // allocate memory for numerator, denominator
    q->b = (TC *) malloc((q->na)*sizeof(TC));
    q->a = (TC *) malloc((q->nb)*sizeof(TC));

    // normalize coefficients by _a[0]
    TC a0 = _a[0];
#if defined LIQUID_FIXED && TC_COMPLEX==0
    for (i=0; i<q->nb; i++) q->b[i] = Q(_div)(_b[i], a0);
    for (i=0; i<q->na; i++) q->b[i] = Q(_div)(_a[i], a0);
#elif defined LIQUID_FIXED && TC_COMPLEX==1
    for (i=0; i<q->nb; i++) q->b[i] = CQ(_div)(_b[i], a0);
    for (i=0; i<q->na; i++) q->b[i] = CQ(_div)(_a[i], a0);
#else
    for (i=0; i<q->nb; i++) q->b[i] = _b[i] / a0;
    for (i=0; i<q->na; i++) q->a[i] = _a[i] / a0;
#endif

    // create buffer and initialize
    q->v = (TI *) malloc((q->n)*sizeof(TI));

#if LIQUID_IIRFILT_USE_DOTPROD
    q->dpa = DOTPROD(_create)(q->a+1, q->na-1);
    q->dpb = DOTPROD(_create)(q->b,   q->nb);
#endif

    // reset internal state
    IIRFILT(_clear)(q);
    
    // return iirfilt object
    return q;
}

// create iirfilt (infinite impulse response filter) object based
// on second-order sections form
//  _B      :   numerator, feed-forward coefficients [size: _nsos x 3]
//  _A      :   denominator, feed-back coefficients [size: _nsos x 3]
//  _nsos   :   number of second-order sections
//
// NOTE: The number of second-order sections can be computed from the
// filter's order, n, as such:
//   r = n % 2
//   L = (n-r)/2
//   nsos = L+r
IIRFILT() IIRFILT(_create_sos)(TC * _B,
                               TC * _A,
                               unsigned int _nsos)
{
    // validate input
    if (_nsos == 0) {
        fprintf(stderr,"error: iirfilt_%s_create_sos(), filter must have at least one 2nd-order section\n", EXTENSION_FULL);
        exit(1);
    }

    // create structure and initialize
    IIRFILT() q = (IIRFILT()) malloc(sizeof(struct IIRFILT(_s)));
    q->type = IIRFILT_TYPE_SOS;
    q->nsos = _nsos;
    q->qsos = (IIRFILTSOS()*) malloc( (q->nsos)*sizeof(IIRFILTSOS()) );
    q->n = _nsos * 2;

    // create coefficients array and copy over
    q->b = (TC *) malloc(3*(q->nsos)*sizeof(TC));
    q->a = (TC *) malloc(3*(q->nsos)*sizeof(TC));
    memmove(q->b, _B, 3*(q->nsos)*sizeof(TC));
    memmove(q->a, _A, 3*(q->nsos)*sizeof(TC));

    TC at[3];
    TC bt[3];
    unsigned int i,k;
    for (i=0; i<q->nsos; i++) {
        for (k=0; k<3; k++) {
            at[k] = q->a[3*i+k];
            bt[k] = q->b[3*i+k];
        }
        q->qsos[i] = IIRFILTSOS(_create)(bt,at);
        //q->qsos[i] = IIRFILT(_create)(q->b+3*i,3,q->a+3*i,3);
    }
    return q;
}

// create iirfilt (infinite impulse response filter) object based
// on prototype
//  _ftype      :   filter type (e.g. LIQUID_IIRDES_BUTTER)
//  _btype      :   band type (e.g. LIQUID_IIRDES_BANDPASS)
//  _format     :   coefficients format (e.g. LIQUID_IIRDES_SOS)
//  _order      :   filter order
//  _fc         :   low-pass prototype cut-off frequency
//  _f0         :   center frequency (band-pass, band-stop)
//  _Ap         :   pass-band ripple in dB
//  _As         :   stop-band ripple in dB
IIRFILT() IIRFILT(_create_prototype)(liquid_iirdes_filtertype _ftype,
                                     liquid_iirdes_bandtype   _btype,
                                     liquid_iirdes_format     _format,
                                     unsigned int _order,
                                     float _fc,
                                     float _f0,
                                     float _Ap,
                                     float _As)
{
    // derived values : compute filter length
    unsigned int N = _order; // effective order
    // filter order effectively doubles for band-pass, band-stop
    // filters due to doubling the number of poles and zeros as
    // a result of filter transformation
    if (_btype == LIQUID_IIRDES_BANDPASS ||
        _btype == LIQUID_IIRDES_BANDSTOP)
    {
        N *= 2;
    }
    unsigned int r = N%2;       // odd/even order
    unsigned int L = (N-r)/2;   // filter semi-length

    // allocate memory for filter coefficients
    unsigned int h_len = (_format == LIQUID_IIRDES_SOS) ? 3*(L+r) : N+1;
    float B[h_len];
    float A[h_len];

    // design filter (compute coefficients)
    liquid_iirdes(_ftype, _btype, _format, _order, _fc, _f0, _Ap, _As, B, A);

    // move coefficients to type-specific arrays (e.g. float complex)
    TC Bc[h_len];
    TC Ac[h_len];
    unsigned int i;
    for (i=0; i<h_len; i++) {
#if defined LIQUID_FIXED && TC_COMPLEX==0
        Bc[i] = Q(_float_to_fixed)(B[i]);
        Ac[i] = Q(_float_to_fixed)(A[i]);
#elif defined LIQUID_FIXED && TC_COMPLEX==1
        Bc[i].real = Q(_float_to_fixed)(B[i]);
        Bc[i].imag = 0;
        Ac[i].real = Q(_float_to_fixed)(A[i]);
        Ac[i].imag = 0;
#else
        Bc[i] = B[i];
        Ac[i] = A[i];
#endif
    }

    // create filter object
    IIRFILT() q = NULL;
    if (_format == LIQUID_IIRDES_SOS)
        q = IIRFILT(_create_sos)(Bc, Ac, L+r);
    else
        q = IIRFILT(_create)(Bc, h_len, Ac, h_len);

    // return filter object
    return q;
}

// create 8th-order integrating filter
IIRFILT() IIRFILT(_create_integrator)()
{
    // 
    // integrator digital zeros/poles/gain, [Pintelon:1990] Table II
    //
    // zeros, digital, integrator
    float complex zdi[8] = {
        1.175839 * -1.0f,
        3.371020 * cexpf(_Complex_I * M_PI / 180.0f * -125.1125f),
        3.371020 * cexpf(_Complex_I * M_PI / 180.0f *  125.1125f),
        4.549710 * cexpf(_Complex_I * M_PI / 180.0f *  -80.96404f),
        4.549710 * cexpf(_Complex_I * M_PI / 180.0f *   80.96404f),
        5.223966 * cexpf(_Complex_I * M_PI / 180.0f *  -40.09347f),
        5.223966 * cexpf(_Complex_I * M_PI / 180.0f *   40.09347f),
        5.443743,};
    // poles, digital, integrator
    float complex pdi[8] = {
        0.5805235f * -1.0f,
        0.2332021f * cexpf(_Complex_I * M_PI / 180.0f * -114.0968f),
        0.2332021f * cexpf(_Complex_I * M_PI / 180.0f *  114.0968f),
        0.1814755f * cexpf(_Complex_I * M_PI / 180.0f *  -66.33969f),
        0.1814755f * cexpf(_Complex_I * M_PI / 180.0f *   66.33969f),
        0.1641457f * cexpf(_Complex_I * M_PI / 180.0f *  -21.89539f),
        0.1641457f * cexpf(_Complex_I * M_PI / 180.0f *   21.89539f),
        1.0f,};
    // gain, digital, integrator
    float complex kdi = -1.89213380759321e-05f;

    // second-order sections
    // allocate 12 values for 4 second-order sections each with
    // 2 roots (order 8), e.g. (1 + r0 z^-1)(1 + r1 z^-1)
    float Bi[12];
    float Ai[12];
    iirdes_dzpk2sosf(zdi, pdi, 8, kdi, Bi, Ai);

    // copy to type-specific array
    TC B[12];
    TC A[12];
    unsigned int i;
    for (i=0; i<12; i++) {
#if defined LIQUID_FIXED && TC_COMPLEX==0
        B[i] = Q(_float_to_fixed)(Bi[i]);
        A[i] = Q(_float_to_fixed)(Ai[i]);
#elif defined LIQUID_FIXED && TC_COMPLEX==1
        B[i].real = Q(_float_to_fixed)(Bi[i]);
        B[i].imag = 0;
        A[i].real = Q(_float_to_fixed)(Ai[i]);
        A[i].imag = 0;
#else
        B[i] = Bi[i];
        A[i] = Ai[i];
#endif
    }

    // create and return filter object
    return IIRFILT(_create_sos)(B,A,4);
}

// create 8th-order differentiation filter
IIRFILT() IIRFILT(_create_differentiator)()
{
    // 
    // differentiator digital zeros/poles/gain, [Pintelon:1990] Table IV
    //
    // zeros, digital, differentiator
    float complex zdd[8] = {
        1.702575f * -1.0f,
        5.877385f * cexpf(_Complex_I * M_PI / 180.0f * -221.4063f),
        5.877385f * cexpf(_Complex_I * M_PI / 180.0f *  221.4063f),
        4.197421f * cexpf(_Complex_I * M_PI / 180.0f * -144.5972f),
        4.197421f * cexpf(_Complex_I * M_PI / 180.0f *  144.5972f),
        5.350284f * cexpf(_Complex_I * M_PI / 180.0f *  -66.88802f),
        5.350284f * cexpf(_Complex_I * M_PI / 180.0f *   66.88802f),
        1.0f,};
    // poles, digital, differentiator
    float complex pdd[8] = {
        0.8476936f * -1.0f,
        0.2990781f * cexpf(_Complex_I * M_PI / 180.0f * -125.5188f),
        0.2990781f * cexpf(_Complex_I * M_PI / 180.0f *  125.5188f),
        0.2232427f * cexpf(_Complex_I * M_PI / 180.0f *  -81.52326f),
        0.2232427f * cexpf(_Complex_I * M_PI / 180.0f *   81.52326f),
        0.1958670f * cexpf(_Complex_I * M_PI / 180.0f *  -40.51510f),
        0.1958670f * cexpf(_Complex_I * M_PI / 180.0f *   40.51510f),
        0.1886088f,};
    // gain, digital, differentiator
    float complex kdd = 2.09049284907492e-05f;

    // second-order sections
    // allocate 12 values for 4 second-order sections each with
    // 2 roots (order 8), e.g. (1 + r0 z^-1)(1 + r1 z^-1)
    float Bd[12];
    float Ad[12];
    iirdes_dzpk2sosf(zdd, pdd, 8, kdd, Bd, Ad);

    // copy to type-specific array
    TC B[12];
    TC A[12];
    unsigned int i;
    for (i=0; i<12; i++) {
#if defined LIQUID_FIXED && TC_COMPLEX==0
        B[i] = Q(_float_to_fixed)(Bd[i]);
        A[i] = Q(_float_to_fixed)(Ad[i]);
#elif defined LIQUID_FIXED && TC_COMPLEX==1
        B[i].real = Q(_float_to_fixed)(Bd[i]);
        B[i].imag = 0;
        A[i].real = Q(_float_to_fixed)(Ad[i]);
        A[i].imag = 0;
#else
        B[i] = Bd[i];
        A[i] = Ad[i];
#endif
    }

    // create and return filter object
    return IIRFILT(_create_sos)(B,A,4);
}

// create DC-blocking filter
//
//          1 -          z^-1
//  H(z) = ------------------
//          1 - (1-alpha)z^-1
IIRFILT() IIRFILT(_create_dc_blocker)(float _alpha)
{
    // compute DC-blocking filter coefficients
    float a1 = -1.0f + _alpha;

    // convert to type-specific array
#if defined LIQUID_FIXED && TC_COMPLEX==0
    TC b[2] = { Q(_one), -Q(_one) };
    TC a[2] = { Q(_one),  Q(_float_to_fixed)(a1) };
#elif defined LIQUID_FIXED && TC_COMPLEX==1
    TC b[2] = { {Q(_one),0}, {-Q(_one),0} };
    TC a[2] = { {Q(_one),0}, CQ(_float_to_fixed)(a1) };
#else
    TC b[2] = { 1.0f, -1.0f };
    TC a[2] = { 1.0f,  a1   };
#endif
    return IIRFILT(_create)(b,2,a,2);
}

// create phase-locked loop iirfilt object
//  _w      :   filter bandwidth
//  _zeta   :   damping factor (1/sqrt(2) suggested)
//  _K      :   loop gain (1000 suggested)
IIRFILT() IIRFILT(_create_pll)(float _w,
                               float _zeta,
                               float _K)
{
    // validate input
    if (_w <= 0.0f || _w >= 1.0f) {
        fprintf(stderr,"error: iirfilt_%s_create_pll(), bandwidth must be in (0,1)\n", EXTENSION_FULL);
        exit(1);
    } else if (_zeta <= 0.0f || _zeta >= 1.0f) {
        fprintf(stderr,"error: iirfilt_%s_create_pll(), damping factor must be in (0,1)\n", EXTENSION_FULL);
        exit(1);
    } else if (_K <= 0.0f) {
        fprintf(stderr,"error: iirfilt_%s_create_pll(), loop gain must be greater than zero\n", EXTENSION_FULL);
        exit(1);
    }

    // compute loop filter coefficients
    float bf[3];
    float af[3];
    iirdes_pll_active_lag(_w, _zeta, _K, bf, af);

    // copy to type-specific array
    TC b[3];
    TC a[3];
#if defined LIQUID_FIXED && TC_COMPLEX==0
    b[0] = Q(_float_to_fixed)(bf[0]);
    b[1] = Q(_float_to_fixed)(bf[1]);
    b[2] = Q(_float_to_fixed)(bf[2]);

    a[0] = Q(_float_to_fixed)(af[0]);
    a[1] = Q(_float_to_fixed)(af[1]);
    a[2] = Q(_float_to_fixed)(af[2]);
#elif defined LIQUID_FIXED && TC_COMPLEX==1
    b[0].real = Q(_float_to_fixed)(bf[0]);  b[0].imag = 0;
    b[1].real = Q(_float_to_fixed)(bf[1]);  b[1].imag = 0;
    b[2].real = Q(_float_to_fixed)(bf[2]);  b[2].imag = 0;

    a[0].real = Q(_float_to_fixed)(af[0]);  a[0].imag = 0;
    a[1].real = Q(_float_to_fixed)(af[1]);  a[1].imag = 0;
    a[2].real = Q(_float_to_fixed)(af[2]);  a[2].imag = 0;
#else
    b[0] = bf[0];   b[1] = bf[1];   b[2] = bf[2];
    a[0] = af[0];   a[1] = af[1];   a[2] = af[2];
#endif

    // create and return filter object
    return IIRFILT(_create_sos)(b,a,1);
}

// destroy iirfilt object
void IIRFILT(_destroy)(IIRFILT() _q)
{
#if LIQUID_IIRFILT_USE_DOTPROD
    DOTPROD(_destroy)(_q->dpa);
    DOTPROD(_destroy)(_q->dpb);
#endif
    free(_q->b);
    free(_q->a);
    // if filter is comprised of cascaded second-order sections,
    // delete sub-filters separately
    if (_q->type == IIRFILT_TYPE_SOS) {
        unsigned int i;
        for (i=0; i<_q->nsos; i++)
            IIRFILTSOS(_destroy)(_q->qsos[i]);
        free(_q->qsos);
    } else {
        free(_q->v);
    }

    free(_q);
}

// print iirfilt object internals
void IIRFILT(_print)(IIRFILT() _q)
{
    printf("iir filter [%s]:\n", _q->type == IIRFILT_TYPE_NORM ? "normal" : "sos");
    unsigned int i;

    if (_q->type == IIRFILT_TYPE_SOS) {
        for (i=0; i<_q->nsos; i++)
            IIRFILTSOS(_print)(_q->qsos[i]);
    } else {

        printf("  b :");
        for (i=0; i<_q->nb; i++)
            PRINTVAL_TC(_q->b[i],%12.8f);
        printf("\n");

        printf("  a :");
        for (i=0; i<_q->na; i++)
            PRINTVAL_TC(_q->a[i],%12.8f);
        printf("\n");

#if 0
        printf("  v :");
        for (i=0; i<_q->n; i++)
            PRINTVAL(_q->v[i]);
        printf("\n");
#endif
    }
}

// clear
void IIRFILT(_clear)(IIRFILT() _q)
{
    unsigned int i;

    if (_q->type == IIRFILT_TYPE_SOS) {
        // clear second-order sections
        for (i=0; i<_q->nsos; i++) {
            IIRFILTSOS(_clear)(_q->qsos[i]);
        }
    } else {
        // set internal buffer to zero
#ifdef LIQUID_FIXED
        memset(_q->v, 0x00, sizeof(_q->v));
#else
        for (i=0; i<_q->n; i++)
            _q->v[i] = 0;
#endif
    }
}

// execute normal iir filter using traditional numerator/denominator
// form (not second-order sections form)
//  _q      :   iirfilt object
//  _x      :   input sample
//  _y      :   output sample
void IIRFILT(_execute_norm)(IIRFILT() _q,
                            TI _x,
                            TO *_y)
{
    unsigned int i;

    // advance buffer
    for (i=_q->n-1; i>0; i--)
        _q->v[i] = _q->v[i-1];

#if LIQUID_IIRFILT_USE_DOTPROD
    // compute new v
    TI v0;
    DOTPROD(_execute)(_q->dpa, _q->v+1, & v0);
    v0 = _x - v0;
    _q->v[0] = v0;

    // compute new y
    DOTPROD(_execute)(_q->dpb, _q->v, _y);
#else

#  ifdef LIQUID_FIXED
    // compute new v
    TI v0 = _x;
    TI mul;
    for (i=1; i<_q->na; i++) {
        // multiply input by coefficient
        mul = MUL_TI_TC(_q->v[i], _q->a[i]);

        // accumulate result (subtract from)
        //v0 -= _q->a[i] * _q->v[i];
        SUB_TO_TO(v0, mul);
    }
    // assign result
    //_q->v[0] = v0;
    _q->v[0] = v0;

    // compute new y
    TO y0 = TO_ZERO;
    for (i=0; i<_q->nb; i++) {
        //y0 += _q->b[i] * _q->v[i];
        // multiply by coefficient
        mul = MUL_TI_TC(_q->v[i], _q->b[i]);

        // accumulate result
        y0 = ADD_TO_TO(y0, mul);
    }

    // set return value
    *_y = y0;
#  else // LIQUID_FIXED
    // compute new v
    TI v0 = _x;
    for (i=1; i<_q->na; i++)
        v0 -= _q->a[i] * _q->v[i];
    _q->v[0] = v0;

    // compute new y
    TO y0 = 0;
    for (i=0; i<_q->nb; i++)
        y0 += _q->b[i] * _q->v[i];

    // set return value
    *_y = y0;
#  endif // LIQUID_FIXED
#endif
}

// execute iir filter using second-order sections form
//  _q      :   iirfilt object
//  _x      :   input sample
//  _y      :   output sample
void IIRFILT(_execute_sos)(IIRFILT() _q,
                           TI _x,
                           TO *_y)
{
    TI t0 = _x;         // intermediate input
    TO t1 = TO_ZERO;    // intermediate output
    unsigned int i;
    for (i=0; i<_q->nsos; i++) {
        // run each filter separately
        IIRFILTSOS(_execute)(_q->qsos[i], t0, &t1);

        // output for filter n becomes input to filter n+1
        t0 = t1;
    }
    *_y = t1;
}

// execute iir filter, switching to type-specific function
//  _q      :   iirfilt object
//  _x      :   input sample
//  _y      :   output sample
void IIRFILT(_execute)(IIRFILT() _q,
                       TI _x,
                       TO *_y)
{
    if (_q->type == IIRFILT_TYPE_NORM)
        IIRFILT(_execute_norm)(_q,_x,_y);
    else
        IIRFILT(_execute_sos)(_q,_x,_y);
}

// get filter length (order + 1)
unsigned int IIRFILT(_get_length)(IIRFILT() _q)
{
    return _q->n;
}

// compute complex frequency response
//  _q      :   filter object
//  _fc     :   frequency
//  _H      :   output frequency response
void IIRFILT(_freqresponse)(IIRFILT()       _q,
                            float           _fc,
                            float complex * _H)
{
    switch (_q->type) {
    case IIRFILT_TYPE_NORM:
        // normal transfer function form
        IIRFILT(_freqresponse_tf)(_q, _fc, _H);
        break;
    case IIRFILT_TYPE_SOS:
        // second-order sections form
        IIRFILT(_freqresponse_sos)(_q, _fc, _H);
        break;
    default:
        fprintf(stderr,"error: iirfilt_%s_freqresponse(), invalid form\n", EXTENSION_FULL);
        exit(1);
    }
}

// compute complex frequency response (transfer function form)
//  _q      :   filter object
//  _fc     :   frequency
//  _H      :   output frequency response
void IIRFILT(_freqresponse_tf)(IIRFILT()       _q,
                               float           _fc,
                               float complex * _H)
{
    unsigned int i;
    // 
    float complex Ha = 0.0f;
    float complex Hb = 0.0f;
#if defined LIQUID_FIXED && TC_COMPLEX==0
    float complex b[_q->nb];
    float complex a[_q->na];
    for (i=0; i<_q->nb; i++) b[i] = Q(_fixed_to_float)(_q->b[i]);
    for (i=0; i<_q->na; i++) a[i] = Q(_fixed_to_float)(_q->a[i]);
#elif defined LIQUID_FIXED && TC_COMPLEX==1
    float complex b[_q->nb];
    float complex a[_q->na];
    for (i=0; i<_q->nb; i++) b[i] = CQ(_fixed_to_float)(_q->b[i]);
    for (i=0; i<_q->na; i++) a[i] = CQ(_fixed_to_float)(_q->a[i]);
#else
    TC * b = _q->b;
    TC * a = _q->a;
#endif

    for (i=0; i<_q->nb; i++)
        Hb += b[i] * cexpf(_Complex_I*2*M_PI*_fc*i);

    for (i=0; i<_q->na; i++)
        Ha += a[i] * cexpf(_Complex_I*2*M_PI*_fc*i);

    // set return value
    *_H = Hb / Ha;
}

// compute complex frequency response (second-order sections form)
//  _q      :   filter object
//  _fc     :   frequency
//  _H      :   output frequency response
void IIRFILT(_freqresponse_sos)(IIRFILT()       _q,
                                float           _fc,
                                float complex * _H)
{
    unsigned int i;
    float complex H = 1.0f;

    // compute 3-point DFT for each second-order section
    for (i=0; i<_q->nsos; i++) {
#if defined LIQUID_FIXED && TC_COMPLEX==0
        float complex b[3] = { Q(_fixed_to_float)(_q->b[3*i+0]),
                               Q(_fixed_to_float)(_q->b[3*i+1]),
                               Q(_fixed_to_float)(_q->b[3*i+2])};
        float complex a[3] = { Q(_fixed_to_float)(_q->a[3*i+0]),
                               Q(_fixed_to_float)(_q->a[3*i+1]),
                               Q(_fixed_to_float)(_q->a[3*i+2])};
#elif defined LIQUID_FIXED && TC_COMPLEX==1
        float complex b[3] = { CQ(_fixed_to_float)(_q->b[3*i+0]),
                               CQ(_fixed_to_float)(_q->b[3*i+1]),
                               CQ(_fixed_to_float)(_q->b[3*i+2])};
        float complex a[3] = { CQ(_fixed_to_float)(_q->a[3*i+0]),
                               CQ(_fixed_to_float)(_q->a[3*i+1]),
                               CQ(_fixed_to_float)(_q->a[3*i+2])};
#else
        TC * b = &_q->b[3*i];
        TC * a = &_q->b[3*i];
#endif
        float complex Hb = b[0] * cexpf(_Complex_I*2*M_PI*_fc*0) +
                           b[1] * cexpf(_Complex_I*2*M_PI*_fc*1) +
                           b[2] * cexpf(_Complex_I*2*M_PI*_fc*2);

        float complex Ha = a[0] * cexpf(_Complex_I*2*M_PI*_fc*0) +
                           a[1] * cexpf(_Complex_I*2*M_PI*_fc*1) +
                           a[2] * cexpf(_Complex_I*2*M_PI*_fc*2);

        // TODO : check to see if we need to take conjugate
        H *= Hb / Ha;
    }

    // set return value
    *_H = H;
}

// compute group delay in samples
//  _q      :   filter object
//  _fc     :   frequency
float IIRFILT(_groupdelay)(IIRFILT() _q,
                           float _fc)
{
    float groupdelay = 0;
    unsigned int i;

    if (_q->type == IIRFILT_TYPE_NORM) {
        // compute group delay from regular transfer function form

        // copy coefficients
        float b[_q->nb];
        float a[_q->na];
        for (i=0; i<_q->nb; i++) {
#if defined LIQUID_FIXED && TC_COMPLEX==0
            b[i] = Q(_fixed_to_float)(_q->b[i]);
#elif defined LIQUID_FIXED && TC_COMPLEX==1
            b[i] = Q(_fixed_to_float)(_q->b[i].real);
#else
            b[i] = crealf(_q->b[i]);
#endif
        }
        for (i=0; i<_q->na; i++) {
#if defined LIQUID_FIXED && TC_COMPLEX==0
            a[i] = Q(_fixed_to_float)(_q->a[i]);
#elif defined LIQUID_FIXED && TC_COMPLEX==1
            a[i] = Q(_fixed_to_float)(_q->a[i].real);
#else
            a[i] = crealf(_q->a[i]);
#endif
        }
        groupdelay = iir_group_delay(b, _q->nb, a, _q->na, _fc);
    } else {
        // accumulate group delay from second-order sections
        for (i=0; i<_q->nsos; i++)
            groupdelay += IIRFILTSOS(_groupdelay)(_q->qsos[i], _fc) - 2;
    }

    return groupdelay;
}

