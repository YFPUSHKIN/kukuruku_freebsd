#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <volk/volk.h>
#include <math.h>
#include <assert.h>

#include <unistd.h>

#define COMPLEX 2

/* Translate, filter and decimate signal from _buf, write it to file descriptor fd.
 *  _buf, buflen - input signal
 *  _carry, carrylen - buffer for filter history
 *  _taps, tapslen - filter coefficients
 *  decim - decimation
 *  rotator - vector denoting phase difference of successive samples
 *  _rotpos, rotposlen - vector denoting current rotator phase
 *  _firpos, firposlen - (integer), starting position in filter
 *  fd - output descriptor
 */
int xdump(char * _buf, size_t buflen, char * _carry, size_t carrylen, char * _taps, size_t tapslen, int decim, float rotator, char * _rotpos, size_t rotposlen, char * _firpos, size_t firposlen, int fd) {

  assert(rotposlen == sizeof(lv_32fc_t));
  assert(firposlen == sizeof(int32_t));

  int * firpos = (int*) _firpos;

  float * taps = (float*) _taps;
  int nsamples = buflen / (sizeof(float)*COMPLEX);

  float * alldata = malloc(sizeof(float) * COMPLEX * (buflen + carrylen));
  lv_32fc_t phase_inc = lv_cmake(cos(rotator), sin(rotator));
  lv_32fc_t* phase = (lv_32fc_t*) _rotpos;

  volk_32fc_s32fc_x2_rotator_32fc((lv_32fc_t*)alldata, // dst
                                  (lv_32fc_t*)_carry, // src
                                   phase_inc, phase, carrylen / (sizeof(float)*COMPLEX)); // params

  volk_32fc_s32fc_x2_rotator_32fc((lv_32fc_t*)(alldata + carrylen / sizeof(float)), // dst
                                  (lv_32fc_t*)_buf, // src
                                   phase_inc, phase, nsamples); // params

  int32_t i;
  int outsample = 0;

  FILE * of = fdopen(fd, "w");
  if(of == NULL) {
    perror("fdopen");
    fprintf(stderr, "Cannot open fd %i for writing\n", fd);
    return 0;
  }

  for(i = *firpos; i<nsamples; i+=decim) {
    lv_32fc_t prod;

    volk_32fc_32f_dot_prod_32fc(&prod, (lv_32fc_t*) (alldata+i*COMPLEX), taps, tapslen/sizeof(float));

    fwrite(&prod, sizeof(lv_32fc_t), 1, of);

    outsample++;
  }

  *firpos = i - nsamples;

  free(alldata);

  //fclose(of);
  return 0;

}

