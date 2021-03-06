#include <stddef.h>
#include <unistd.h>
#include <err.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include <fftw3.h>

#include "metadata.h"
#include "sdr_packet.h"
#include "constants.h"
#include "util.h"

int fftsize;
float* fftw_window;
float* fftw_avg;

fftwf_complex *fftw_in, *fftw_out;
fftwf_plan p;

/* Return i-th coefficient of Hamming window of a given length */
float hamming(int i, int length) {
  double a, b, w, N1;
  a = 25.0/46.0;
  b = 21.0/46.0;
  N1 = (double)(length-1);
  w = a - b*cos(2*i*M_PI/N1);
  return w;
}

/* allocate FFTW buffers and window and init plan */
void fftw_init(int N) {

  if(fftw_in != NULL) {
    fftwf_free(fftw_in);
  }
  if(fftw_out != NULL) {
    fftwf_free(fftw_out);
  }

  fftw_in = (fftwf_complex*) fftwf_safe_malloc(sizeof(fftwf_complex) * N);
  fftw_out = (fftwf_complex*) fftwf_safe_malloc(sizeof(fftwf_complex) * N);
  p = fftwf_plan_dft_1d(N, fftw_in, fftw_out, FFTW_FORWARD, FFTW_ESTIMATE);

  fftw_window = realloc(fftw_window, sizeof(float) * N);
  fftw_avg = realloc(fftw_avg, sizeof(float) * N);

  for(int i = 0; i<N; i++) {
    fftw_window[i] = hamming(i, N);
  }

  fftsize = N;
}

/* Calculate spectrum of packet *pkt and write it to pkt->spectrum
 * spp - how many waterfall lines to compute
 * fftskip - distance between beginning of transforms (we don't usually compute
 *  side-by-side or even overlapping FFT, but rather sample the signal once a while)
 * 
 * What is exacly calculated? 10log10((Re^2 + Im^2)/N) for the unnormalized FFT with Hamming window.
 */
void calc_spectrum(sdr_packet * pkt, int spp, int fftskip) {
  if(fftw_window == NULL) {
    err(EXIT_FAILURE, "Internal consistency, calling calc_spectrum before fftw_init");
  }

  size_t packet_len = spp * fftsize * sizeof(float);
  if(packet_len != pkt->spectrumsize) {
    pkt->spectrum = realloc(pkt->spectrum, packet_len);
    pkt->spectrumsize = packet_len;
  }

  int base = 0;

  for(int i = 0; i<spp; i++) {

    int iters = 0;

    memset(fftw_avg, 0, fftsize * sizeof(float));

    for(int j = 0; j<SDRPACKETSIZE/spp-fftskip-fftsize; j += fftskip) {

      /* Copy data to fftw */
      for(int k = 0; k<fftsize; k++) {
        ((float*)fftw_in)[COMPLEX*k] = fftw_window[k] * pkt->data[(base+k)*COMPLEX];
        ((float*)fftw_in)[COMPLEX*k+1] = fftw_window[k] * pkt->data[(base+k)*COMPLEX+1];
      }

      /* Transform */
      fftwf_execute(p);

      /* Read output */
      float * fout = (float *) fftw_out;
      for(int k = 0; k<fftsize; k++) {
        float v1 = fout[COMPLEX*k];
        float v2 = fout[COMPLEX*k+1];

        /* We are computing sum(log(something))).
         * We can compute log(prod(something)) and save computationally intesive logarithms.
         * However, this seems to be numerically unstable (you multiply small numbers until
         *  you end up with 0).  Probably can be implemented by splitting the float with
         * frexp(3) and computing exponent separately...

        if(j == 0) {
          fftw_avg[k] = (v1*v1 + v2*v2);
        } else {
          fftw_avg[k] *= (v1*v1 + v2*v2);
        }*/

        fftw_avg[k] += log10f((v1*v1 + v2*v2)/fftsize);

      }

      base += fftskip;
      iters++;
    }

    for(int k = 0; k<fftsize; k++) {
      fftw_avg[k] = fftw_avg[k] / (iters/10);
    }

    /* Per FFTW documentation:
     *  the positive frequencies are stored in the first half of the output
     *  and the negative frequencies are stored in backwards order in the
     *  second half of the output */
    memcpy(pkt->spectrum+i*fftsize, fftw_avg+fftsize/2, (fftsize/2) * sizeof(float));
    memcpy(pkt->spectrum+i*fftsize+fftsize/2, fftw_avg, (fftsize/2) * sizeof(float));

  }

}

/* Calculate histogram of first npoints samples
 * write it to pkt->histo
 * npoints <= UINT16_MAX as histo is currently array of 16-bits
 */
void calc_histogram(sdr_packet * pkt, int npoints) {
  memset(pkt->histo, 0, HISTOGRAM_RES * sizeof(uint16_t));
  for(int i = 0; i<npoints; i++) {
    int sample = fabs(pkt->data[i]) * HISTOGRAM_RES;
    sample = CLAMP(sample, 0, HISTOGRAM_RES-1);
    //printf("sample %f %i\n", fabs(pkt->data[i]), sample);
    pkt->histo[sample]++;
  }
}
