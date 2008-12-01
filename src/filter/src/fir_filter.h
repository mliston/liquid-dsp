//
// Finite impulse response filter
//

#ifndef __FIR_FILTER_H__
#define __FIR_FILTER_H__

// FIR filter prototypes
typedef enum {
    FIR_NONE=0,
    FIR_RRCOS,
    FIR_RCOS,
    FIR_GAUSS
} fir_prototype;

//  filter type     : out   : coeff : in
//  ----------------+-------+-------+-----
//  fir_filter_rrr  : REAL  : REAL  : REAL
//  fir_filter_ccc  : CPLX  : CPLX  : CPLX
//  fir_filter_ccr  : CPLX  : CPLX  : REAL

typedef struct fir_filter_s * fir_filter;

fir_filter fir_filter_create(float * _h, unsigned int _n);

//fir_filter fir_filter_create_prototype(unsigned int _n);

void fir_filter_destroy(fir_filter _f);

void fir_filter_print(fir_filter _f);

float fir_filter_execute(fir_filter _f, float * _v);

// accessor functions
unsigned int fir_filter_get_length(fir_filter _f);

// Macro:
//  X   : name-mangling macro
//  O   : output type
//  C   : coefficients type
//  I   : input type
// DEFINE_FIR_FITER_API(X,O,C,I)

//
// internal
//
struct fir_filter_s {
    float * h;
    unsigned int h_len;

    fir_prototype p;
};

#endif // __FIR_FILTER_H__

