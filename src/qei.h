/*
===============================================================================
 Name        : quadrature.h
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description :
===============================================================================
*/

#ifndef QEI_H_
#define QEI_H_


#ifdef __cplusplus
 extern "C" {
#endif

#include <stdint.h>
#include "LPC8xx.h"
#include "config.h"

void    qei_init           (void);

int32_t qei_abs_step       (void);
int32_t qei_offset_step    (void);
void    qei_reset_step     (void);
void    qei_reset_step_val (int32_t value);

uint8_t qei_read_pin       (void);

#ifdef __cplusplus
 }
#endif

#endif /* QEI_H_ */
