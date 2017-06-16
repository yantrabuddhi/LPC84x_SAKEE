/*
===============================================================================
 Name        : quadrature.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : SCT based quadrature decoder
===============================================================================
*/

/**************************************************************************/
/*!
    @file     ProTrinketVolumeKnobPlus.ino
    @author   Mike Barela for Adafruit Industries
    @license  MIT

    This is an example of using the Adafruit Pro Trinket with a rotary
    encoder as a USB HID Device.  Turning the knob controls the sound on
    a multimedia computer, pressing the knob mutes/unmutes the sound

    Adafruit invests time and resources providing this open source code,
    please support Adafruit and open-source hardware by purchasing
    products from Adafruit!

    @section  HISTORY

    v1.0  - First release 1/26/2015  Mike Barela based on code by Frank Zhou
*/
/**************************************************************************/

#include "sct.h"
#include "syscon.h"
#include "iocon.h"

#include "qei.h"
#include "sct_generic_addon.h"

/*
 Pins used in this application:

 P0.19 [O] - CLKOUT: main/1
 P0.20 [I] - SCT_IN0: QEI phA
 P0.21 [I] - SCT_IN1: QEI phB
 P0.22 [O] - SCT_OUT0: direction indicator (0: CW/forward, 1: CCW/reverse)

 P0.26 [O] - GPO: test synchro pulse
 P0.27 [O] - GPO: SCT isr indicator
 */

enum sct_in
{
  sct_in_qei_A = 0,
  sct_in_qei_B
};

enum sct_out
{
  sct_out_qei_direction = 0
};

enum sct_state
{
  sct_st_qei00 = 0,
  sct_st_qei01,
  sct_st_qei10,
  sct_st_qei11
};

enum sct_event
{
  sct_ev_qei00_A_re = 0,
  sct_ev_qei00_B_re,
  sct_ev_qei01_A_fe,
  sct_ev_qei01_B_re,
  sct_ev_qei10_A_re,
  sct_ev_qei10_B_fe,
  sct_ev_qei11_A_fe,
  sct_ev_qei11_B_fe
};

#define QEI_CW  0
#define QEI_CCW 1

#define QEI_A_RE (LPC_IOCON->PIO0_20 = 0<<11 | 0<<10 | 0<<6 | 1<<5 | 2<<3)
#define QEI_A_FE (LPC_IOCON->PIO0_20 = 0<<11 | 0<<10 | 0<<6 | 1<<5 | 1<<3)
#define QEI_B_RE (LPC_IOCON->PIO0_21 = 0<<11 | 0<<10 | 0<<6 | 1<<5 | 2<<3)
#define QEI_B_FE (LPC_IOCON->PIO0_21 = 0<<11 | 0<<10 | 0<<6 | 1<<5 | 1<<3)

#define QEI_FORWARD QEI_CW
#define QEI_REVERSE QEI_CCW

#define QEI_A_HIGH QEI_A_RE
#define QEI_A_LOW  QEI_A_FE
#define QEI_B_HIGH QEI_B_RE
#define QEI_B_LOW  QEI_B_FE

volatile int32_t _qei_step = 0;
volatile uint8_t enc_prev_pos = 0;
volatile uint8_t _state = 0; // pin A bit0, pin B bit1

volatile uint32_t _qei_isr_counter = 0;

#define PIN_A_PORT   (QEI_A_PIN / 32)
#define PIN_A_BIT    (QEI_A_PIN % 32)

#define PIN_B_PORT   (QEI_B_PIN / 32)
#define PIN_B_BIT    (QEI_B_PIN % 32)

int32_t qei_abs_step    (void)
{
  return _qei_step;
}

int32_t qei_offset_step (void)
{
  static int32_t last_value = 0;

  int32_t ret = _qei_step - last_value;

  last_value =  _qei_step;

  return ret;
}

void qei_reset_step  (void)
{
  _qei_step = 0;
}

uint8_t qei_read_pin(void)
{
  uint8_t ret;

  // Encoder is active LOW
  ret  = bit_is_clear(LPC_GPIO_PORT->PIN[PIN_A_PORT], PIN_A_BIT) ? 0x01 : 0;
  ret |= bit_is_clear(LPC_GPIO_PORT->PIN[PIN_B_PORT], PIN_B_BIT) ? 0x02 : 0;

  return ret;
}

void qei_init(void)
{
  LPC_SYSCON->SYSAHBCLKCTRL0 |= (GPIO_INT);
  LPC_SYSCON->PRESETCTRL0 &= (GPIOINT_RST_N);
  LPC_SYSCON->PRESETCTRL0 |= ~(GPIOINT_RST_N);

  LPC_IOCON->PIO0_20 |= MODE_PULLUP;
  LPC_IOCON->PIO0_21 |= MODE_PULLUP;

  // Set pin direction to input
  LPC_GPIO_PORT->DIRCLR[PIN_A_PORT] = bit(PIN_A_BIT);
  LPC_GPIO_PORT->DIRCLR[PIN_B_PORT] = bit(PIN_B_BIT);

  // Configure Pin A & B as interrupt
  LPC_SYSCON->PINTSEL[0] = QEI_A_PIN;
  LPC_SYSCON->PINTSEL[1] = QEI_B_PIN;

  // Configure the Pin interrupt mode register (a.k.a ISEL) for edge-sensitive on PINTSEL1,0
  LPC_PIN_INT->ISEL = 0x00;

  // Configure the IENR (pin interrupt enable rising) for rising edges on PINTSEL0,1
  LPC_PIN_INT->SIENR = 0x3;

  // Configure the IENF (pin interrupt enable falling) for falling edges on PINTSEL0,1
  LPC_PIN_INT->SIENF = 0x3;

  // Clear any pending or left-over interrupt flags
  LPC_PIN_INT->IST = 0xFF;

  // Get initial state
  enc_prev_pos = qei_read_pin();

  // Enable pin interrupts 0 - 1 in the NVIC (see core_cm0plus.h)
  NVIC_EnableIRQ(PININT0_IRQn);
  NVIC_EnableIRQ(PININT1_IRQn);
}

void qei_isr(uint8_t pin)
{
  LPC_PIN_INT->IST = bit(pin);

  static uint8_t enc_flags = 0;

  uint8_t enc_cur_pos = qei_read_pin();

  // if any rotation at all
  if (enc_cur_pos != enc_prev_pos)
  {
    if (enc_prev_pos == 0x00)
    {
      // this is the first edge
      if (enc_cur_pos == 0x01) {
        enc_flags |= (1 << 0);
      }
      else if (enc_cur_pos == 0x02) {
        enc_flags |= (1 << 1);
      }
    }

    if (enc_cur_pos == 0x03)
    {
      // this is when the encoder is in the middle of a "step"
      enc_flags |= (1 << 4);
    }
    else if (enc_cur_pos == 0x00)
    {
      // this is the final edge
      if (enc_prev_pos == 0x02) {
        enc_flags |= (1 << 2);
      }
      else if (enc_prev_pos == 0x01) {
        enc_flags |= (1 << 3);
      }

      // check the first and last edge
      // or maybe one edge is missing, if missing then require the middle state
      // this will reject bounces and false movements
      if (bit_is_set(enc_flags, 0) && (bit_is_set(enc_flags, 2) || bit_is_set(enc_flags, 4))) {
        _qei_step++;
      }
      else if (bit_is_set(enc_flags, 2) && (bit_is_set(enc_flags, 0) || bit_is_set(enc_flags, 4))) {
        _qei_step++;
      }
      else if (bit_is_set(enc_flags, 1) && (bit_is_set(enc_flags, 3) || bit_is_set(enc_flags, 4))) {
        _qei_step--;
      }
      else if (bit_is_set(enc_flags, 3) && (bit_is_set(enc_flags, 1) || bit_is_set(enc_flags, 4))) {
        _qei_step--;
      }

      enc_flags = 0; // reset for next time
    }
  }

  enc_prev_pos = enc_cur_pos;
}

void PININT0_IRQHandler(void)
{
  qei_isr(0);
}

void PININT1_IRQHandler(void)
{
  qei_isr(1);
}

#if 0
void qei_init(void)
{
  _qei_step = 0; // Init variable

	//CLKOUT setup begin
	//CLKOUT = main/1 @ P0.19
	LPC_SYSCON->SYSAHBCLKCTRL0 |= (GPIO0 | GPIO1 | SWM);				//enable access to SWM
	LPC_SYSCON->CLKOUTDIV = 1;						//prepare CLKOUT/1
	LPC_SYSCON->CLKOUTSEL = 3;						//CLKOUT =  main clock

#if 0
	LPC_SYSCON->CLKOUTUEN = 0;						//update CLKOUT...
	LPC_SYSCON->CLKOUTUEN = 1;						//... selection
#endif

	//CLKOUT setup end
	LPC_SWM->PINASSIGN11 = (LPC_SWM->PINASSIGN11 & ~(0xFF<<16)) | ((0*32+19)<<16);	//CLKOUT @ P0.19

	// GPIO state
	QEI_A_LOW;
	QEI_B_LOW;

	/*------------- Config SCT -------------*/
	LPC_SYSCON->SYSAHBCLKCTRL0 |= IOCON;				//enable access to GPIOs

	//SCT setup begin
	LPC_SYSCON->SYSAHBCLKCTRL0 |= SCT;				//enable SCT clock...
	LPC_SYSCON->PRESETCTRL0 &= ~(SCT);				//... reset...
	LPC_SYSCON->PRESETCTRL0 |= SCT;					//... release SCT

	LPC_SWM->PINASSIGN6 = (LPC_SWM->PINASSIGN6 & ~(0xFF<<24)) | ((0*32+20)<<24);	//CTIN_0 @ P0.20 (QEI A)
	LPC_INMUX_TRIGMUX->SCT0_INMUX0 = 0;													//SCT0_PIN0 = SWM
	//LPC_IOCON->PIO0_20 = (LPC_IOCON->PIO0_20 & ~(3<<3)) | 1<<5 | 0<<3;				//no PU/PD; enable hysteresis
	LPC_IOCON->PIO0_20 = (LPC_IOCON->PIO0_20 & ~(3<<3)) | 1<<5 | 2<<3;				//PU; enable hysteresis

	LPC_SWM->PINASSIGN7 = (LPC_SWM->PINASSIGN7 & ~(0xFF<< 0)) | ((0*32+21)<< 0);	//CTIN_1 @ P0.21 (QEI B)
	LPC_INMUX_TRIGMUX->SCT0_INMUX1 = 1;													//SCT0_PIN1 = SWM
	//LPC_IOCON->PIO0_21 = (LPC_IOCON->PIO0_21 & ~(3<<3)) | 1<<5 | 0<<3;				//no PU/PD; enable hysteresis
	LPC_IOCON->PIO0_21 = (LPC_IOCON->PIO0_21 & ~(3<<3)) | 1<<5 | 2<<3;				//PU; enable hysteresis

	LPC_SWM->PINASSIGN7 = (LPC_SWM->PINASSIGN7 & ~(0xFF<<24)) | ((0*32+22)<<24);	//CTOUT_0 @ P0.22 (counting up indicator)
	LPC_IOCON->PIO0_22 = (LPC_IOCON->PIO0_22 & ~(3<<3)) | 1<<5 | 0<<3;				//no PU/PD; enable hysteresis

	LPC_SCT0->CONFIG =  0<<18               |					//no autolimit H
                      0<<17               |					//no autolimit L
                      (1<<sct_in_qei_A    |		//synchronize inputs...
                      1<<sct_in_qei_B)<<9 |	//...
                      1<<8                |					//do not reload H match registers
                      1<<7                |					//do not reload L match registers
                      0<<3                |					//NA
                      0<<1                |					//System Clock Mode
                      1<<0;					//1x 32-bit counter

	LPC_SCT0->CTRL = (1-1)<<5  | 1<<4  | 0<<3  | 1<<2  | 0<<1  | 0<<0;	//U: no prescaler,bidirectional,NA,halt,NA,count up
	LPC_SCT0->EVEN = 0;	//disable all interrupts

	//match register setup
	//====================

	//events setup
	//============

	//    setup for event: sct_ev_qei00_A_re
	//   enabled in state: sct_st_qei_00
	//       generated by: RE(A)
	//      state machine: go to state sct_st_qei_01
	//     output control: drive sct_out_qei_direction=0
	LPC_SCT0->EVENT[sct_ev_qei00_A_re].STATE = 1<<sct_st_qei00;
	LPC_SCT0->EVENT[sct_ev_qei00_A_re].CTRL = EVCTR_SCT_U | EVCTR_IN_RISE(sct_in_qei_A) | EVCTR_STATE_SET(sct_st_qei01);
	LPC_SCT0->OUT[sct_out_qei_direction].CLR |= 1<<sct_ev_qei00_A_re;

	//    setup for event: sct_ev_qei00_B_re
	//   enabled in state: sct_st_qei_00
	//       generated by: RE(B)
	//      state machine: go to state sct_st_qei_10
	//     output control: drive sct_out_qei_direction=1
	LPC_SCT0->EVENT[sct_ev_qei00_B_re].STATE = 1<<sct_st_qei00;
	LPC_SCT0->EVENT[sct_ev_qei00_B_re].CTRL = EVCTR_SCT_U | EVCTR_IN_RISE(sct_in_qei_B) | EVCTR_STATE_SET(sct_st_qei10);
	LPC_SCT0->OUT[sct_out_qei_direction].SET |= 1<<sct_ev_qei00_B_re;

	//    setup for event: sct_ev_qei01_A_fe
	//   enabled in state: sct_st_qei_01
	//       generated by: FE(A)
	//      state machine: go to state sct_st_qei_00
	//     output control: drive sct_out_qei_direction=1
	LPC_SCT0->EVENT[sct_ev_qei01_A_fe].STATE = 1<<sct_st_qei01;
	LPC_SCT0->EVENT[sct_ev_qei01_A_fe].CTRL = EVCTR_SCT_U | EVCTR_IN_FALL(sct_in_qei_A) | EVCTR_STATE_SET(sct_st_qei00);
	LPC_SCT0->OUT[sct_out_qei_direction].SET |= 1<<sct_ev_qei01_A_fe;

	//    setup for event: sct_ev_qei01_B_re
	//   enabled in state: sct_st_qei_01
	//       generated by: RE(B)
	//      state machine: go to state sct_st_qei_11
	//     output control: drive sct_out_qei_direction=0
	LPC_SCT0->EVENT[sct_ev_qei01_B_re].STATE = 1<<sct_st_qei01;
	LPC_SCT0->EVENT[sct_ev_qei01_B_re].CTRL = EVCTR_SCT_U | EVCTR_IN_RISE(sct_in_qei_B) | EVCTR_STATE_SET(sct_st_qei11);
	LPC_SCT0->OUT[sct_out_qei_direction].CLR |= 1<<sct_ev_qei01_B_re;

	//    setup for event: sct_ev_qei10_A_re
	//   enabled in state: sct_st_qei_10
	//       generated by: RE(A)
	//      state machine: go to state sct_st_qei_11
	//     output control: drive sct_out_qei_direction=1
	LPC_SCT0->EVENT[sct_ev_qei10_A_re].STATE = 1<<sct_st_qei10;
	LPC_SCT0->EVENT[sct_ev_qei10_A_re].CTRL = EVCTR_SCT_U | EVCTR_IN_RISE(sct_in_qei_A) | EVCTR_STATE_SET(sct_st_qei11);
	LPC_SCT0->OUT[sct_out_qei_direction].SET |= 1<<sct_ev_qei10_A_re;

	//    setup for event: sct_ev_qei10_B_fe
	//   enabled in state: sct_st_qei_10
	//       generated by: FE(B)
	//      state machine: go to state sct_st_qei_00
	//     output control: drive sct_out_qei_direction=0
	LPC_SCT0->EVENT[sct_ev_qei10_B_fe].STATE = 1<<sct_st_qei10;
	LPC_SCT0->EVENT[sct_ev_qei10_B_fe].CTRL = EVCTR_SCT_U | EVCTR_IN_FALL(sct_in_qei_B) | EVCTR_STATE_SET(sct_st_qei00);
	LPC_SCT0->OUT[sct_out_qei_direction].CLR |= 1<<sct_ev_qei10_B_fe;

	//    setup for event: sct_ev_qei11_A_fe
	//   enabled in state: sct_st_qei_11
	//       generated by: FE(A)
	//      state machine: go to state sct_st_qei_10
	//     output control: drive sct_out_qei_direction=0
	LPC_SCT0->EVENT[sct_ev_qei11_A_fe].STATE = 1<<sct_st_qei11;
	LPC_SCT0->EVENT[sct_ev_qei11_A_fe].CTRL = EVCTR_SCT_U | EVCTR_IN_FALL(sct_in_qei_A) | EVCTR_STATE_SET(sct_st_qei10);
	LPC_SCT0->OUT[sct_out_qei_direction].CLR |= 1<<sct_ev_qei11_A_fe;

	//    setup for event: sct_ev_qei11_B_fe
	//   enabled in state: sct_st_qei_11
	//       generated by: FE(B)
	//      state machine: go to state sct_st_qei_01
	//     output control: drive sct_out_qei_direction=1
	LPC_SCT0->EVENT[sct_ev_qei11_B_fe].STATE = 1<<sct_st_qei11;
	LPC_SCT0->EVENT[sct_ev_qei11_B_fe].CTRL = EVCTR_SCT_U | EVCTR_IN_FALL(sct_in_qei_B) | EVCTR_STATE_SET(sct_st_qei01);
	LPC_SCT0->OUT[sct_out_qei_direction].SET |= 1<<sct_ev_qei11_B_fe;

	//set initial state based on the QEI_A/B inputs
	switch(LPC_SCT0->INPUT & 0x03)
	{
		case 0: LPC_SCT0->STATE = sct_st_qei00;
			      break;
		case 1: LPC_SCT0->STATE = sct_st_qei01;
			      break;
		case 2: LPC_SCT0->STATE = sct_st_qei10;
			      break;
		case 3: LPC_SCT0->STATE = sct_st_qei11;
			      break;
	}
	LPC_SCT0->OUTPUT = 0<<sct_out_qei_direction;  //initial output: assume to be CW/forward

	LPC_SCT0->EVFLAG = 0x000000FF;				        //clear all event flags

	NVIC_SetPriority(SCT_IRQn, 1);
	NVIC_EnableIRQ(SCT_IRQn);

	//each of the events generates an interrupt request
	LPC_SCT0->EVEN =
	  1<<sct_ev_qei00_A_re | 1<<sct_ev_qei00_B_re |
	  1<<sct_ev_qei01_A_fe | 1<<sct_ev_qei01_B_re |
	  1<<sct_ev_qei10_A_re | 1<<sct_ev_qei10_B_fe |
	  1<<sct_ev_qei11_A_fe | 1<<sct_ev_qei11_B_fe;

	LPC_SCT0->COUNT = 0xDEADC0DE;			 						//prime the counter

	LPC_SCT0->CTRL = (LPC_SCT0->CTRL & ~(1<<2)) | //HALT->STOP
	                 (1<<1);

	//SCT setup end
}

void SCT_IRQHandler(void)
{
	_qei_isr_counter++;

	LPC_SCT0->EVFLAG = 0x000000FF;	//clear all event flags

	//update sw_qei_count based on the direction
	if ((LPC_SCT0->OUTPUT & (1<<sct_out_qei_direction)) == 0)
	{
	  //CW direction
	  _qei_step++;
	}
	else
	{
	  //CCW direction
	  _qei_step--;
	}

	return;
}
#endif
