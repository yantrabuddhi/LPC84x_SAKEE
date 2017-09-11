/*
 Pins used in this application:
 
 P0.19 [O] - CLKOUT: main/1
 P0.20 [I] - SCT_IN0: QEI phA
 P0.21 [I] - SCT_IN1: QEI phB
 P0.18 [O] - SCT_OUT0: direction indicator (0: CW/forward, 1: CCW/reverse)
  
 P0.28 [O] - GPO: test synchro pulse
 P1.20 [O] - GPO: SCT isr indicator

*/

#include "syscon.h"
#include "iocon.h"
#include "sct.h"

#include "qei.h"
#include "sct_generic_addon.h"

#if USE_SCT

//volatile uint32_t main_loop_counter, i, j;
//volatile uint32_t user_gate_pattern, user_gate_counter;
//volatile uint32_t dummy_32b;
volatile uint32_t qei_state;
//volatile uint32_t sct_isr_count;
volatile int32_t _qei_step; //, sw_qei_max;

#define	SCT_CONFIGURATION	0

enum sct_in			{sct_in_qei_A = 0, sct_in_qei_B};
enum sct_out		{sct_out_qei_direction = 0};
enum sct_mc			{sct_m_dummy = 0};
enum sct_state	{sct_st_qei00 = 0, sct_st_qei01, sct_st_qei10, sct_st_qei11};
enum sct_event	{sct_ev_qei00_A_re = 0, sct_ev_qei00_B_re,
								 sct_ev_qei01_A_fe, sct_ev_qei01_B_re,
								 sct_ev_qei10_A_re, sct_ev_qei10_B_fe,
								 sct_ev_qei11_A_fe, sct_ev_qei11_B_fe};

#define	SYNCHRO_PORT		0
#define	SYNCHRO_PIN			28
								 
#define	SCT0ISR_PORT		1
#define	SCT0ISR_PIN			20

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

uint32_t init_qei(uint32_t state);
uint32_t init_sct(uint32_t _setup);

void qei_init (void)
{
  _qei_step = 0;

	qei_state = 0;
	init_qei(qei_state);
	init_sct(0);
}


#if 0
uint32_t init_mcu(uint32_t _setup);
uint32_t qei_rotate(uint32_t * pnt_qei_state, uint32_t direction, uint32_t steps);

void sw_delay(uint32_t delay);

int main(void)
{
	main_loop_counter = 0;
	
	init_mcu(0);
	
	sw_qei_count = 0;
	sw_qei_max = 27;
	
	qei_state = 0;
	init_qei(qei_state);
	
	init_sct(0);
	
	while(1)
	{
		//generate synchro pulse...
		sw_delay(1000);
		LPC_GPIO_PORT->SET[SYNCHRO_PORT] = 1<<SYNCHRO_PIN;
		sw_delay(100);
		LPC_GPIO_PORT->CLR[SYNCHRO_PORT] = 1<<SYNCHRO_PIN;
		sw_delay(1000);
		
		//generate CW sequence
		qei_rotate((uint32_t *) &qei_state, QEI_CW, 15);
		
		//generate CCW sequence
		qei_rotate((uint32_t *) &qei_state, QEI_CCW, 8);
		
		main_loop_counter++;
	}
	
	return 0;
}

void sw_gate(void)
{
	user_gate_counter = 0;

	user_gate_pattern = 0xDEADC0DE;
	
	while(user_gate_pattern != 0x13)
	{
		user_gate_counter++;
	}

	return;
}

uint32_t init_mcu(uint32_t setup)
{
	uint32_t result;
	
	LPC_SYSCON->PRESETCTRL0 |= 1<<7;          //clear SWM reset
	LPC_SYSCON->SYSAHBCLKCTRL0 |= 1<<7;       //enable SWM clock
	
	//CLKOUT setup begin
	//CLKOUT = main/1 @ P0.19
	LPC_SYSCON->CLKOUTDIV = 1;								//prepare CLKOUT/1
	LPC_SYSCON->CLKOUTSEL = 1;								//CLKOUT =  main clock
	//CLKOUT setup end
	LPC_SWM->PINASSIGN11 =                    //CLKOUT @ P0.19...
	  (LPC_SWM->PINASSIGN11 & ~(0xFF<<8)) |   //...
	  ((0*32+19)<<8);                         //...
	
	LPC_SYSCON->PRESETCTRL0 |= 1<<6;          //clear GPIO0 reset
	LPC_SYSCON->SYSAHBCLKCTRL0 |= 1<<6;       //enable GPIO0 clock
	LPC_SYSCON->PRESETCTRL0 |= 1<<20;         //clear GPIO1 reset
	LPC_SYSCON->SYSAHBCLKCTRL0 |= 1<<20;      //enable GPIO1 clock
	
	//synchro pin direction: output, low
	LPC_GPIO_PORT->CLR[SYNCHRO_PORT] = 1<<SYNCHRO_PIN;	
	LPC_GPIO_PORT->DIRSET[SYNCHRO_PORT] = 1<<SYNCHRO_PIN;
	
  //sct0 isr pin indicator: output low
	LPC_GPIO_PORT->CLR[SCT0ISR_PORT] =  1<<SCT0ISR_PIN;
	LPC_GPIO_PORT->DIRSET[SCT0ISR_PORT] =  1<<SCT0ISR_PIN;
	
	result = 0;
	
	return result;
}
#endif

uint32_t init_sct(uint32_t setup)
{
	uint32_t result;
	
	LPC_SYSCON->SYSAHBCLKCTRL0 |= (GPIO0 | GPIO1 | SWM);				//enable access to SWM

#if 0
	LPC_SYSCON->PRESETCTRL0 |= 1<<18;         //clear IOCON reset
	LPC_SYSCON->SYSAHBCLKCTRL0 |= 1<<18;      //enable IOCON clock
	
	LPC_SYSCON->PRESETCTRL0 |= 1<<6;          //clear GPIO0 reset
	LPC_SYSCON->SYSAHBCLKCTRL0 |= 1<<6;       //enable GPIO0 clock
	
	LPC_SYSCON->PRESETCTRL0 |= 1<<20;         //clear GPIO1 reset
	LPC_SYSCON->SYSAHBCLKCTRL0 |= 1<<20;      //enable GPIO1 clock
#endif

	//SCT setup begin
	LPC_SYSCON->PRESETCTRL0 &= ~(1<<8);       //activate SCT reset
	LPC_SYSCON->PRESETCTRL0 |= 1<<8;          //clear SCT reset
	LPC_SYSCON->SYSAHBCLKCTRL0 |= 1<<8;       //enable SCT clock

	
	LPC_SWM->PINASSIGN6 = (LPC_SWM->PINASSIGN6 & ~(0xFF<<24)) | ((0*32+20)<<24);	//SCT0_GPIO_IN_A_I @ P0.20 (QEI A)
	LPC_INMUX_TRIGMUX->SCT0_INMUX0 = 0;																						//SCT0_PIN0 = SWM
	LPC_IOCON->PIO0_20 = (LPC_IOCON->PIO0_20 & ~(3<<3)) | 1<<5 | 0<<3;						//no PU/PD; enable hysteresis
	
	LPC_SWM->PINASSIGN7 = (LPC_SWM->PINASSIGN7 & ~(0xFF<< 0)) | ((0*32+21)<< 0);	//SCT0_GPIO_IN_B_I @ P0.21 (QEI B)
	LPC_INMUX_TRIGMUX->SCT0_INMUX1 = 1;																						//SCT0_PIN1 = SWM
	LPC_IOCON->PIO0_21 = (LPC_IOCON->PIO0_21 & ~(3<<3)) | 1<<5 | 0<<3;						//no PU/PD; enable hysteresis
	
	LPC_SWM->PINASSIGN7 = (LPC_SWM->PINASSIGN7 & ~(0xFF<<24)) | ((0*32+18)<<24);	//CTOUT_0 @ P0.18 (counting up indicator)
	LPC_IOCON->PIO0_18 = (LPC_IOCON->PIO0_18 & ~(3<<3)) | 1<<5 | 0<<3;						//no PU/PD; enable hysteresis

	LPC_SCT0->CONFIG =         0<<18 |	//no autolimit H
															 0<<17 |	//no autolimit L
									 (1<<sct_in_qei_A  |	//synchronize inputs...
								1<<sct_in_qei_B)<<9  |	//...
								 						    1<<8 |	//do not reload H match registers
																1<<7 |	//do not reload L match registers
																0<<3 |	//NA
																0<<1 |	//System Clock Mode
																1<<0;		//1x 32-bit counter
	LPC_SCT0->CTRL = (1-1)<<5  | 1<<4  | 0<<3  | 1<<2  | 0<<1  | 0<<0;			//U: no prescaler,bidirectional,NA,halt,NA,count up
	LPC_SCT0->EVEN = 0;								  //disable all interrupts
	
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

//  sct_isr_count = 0;
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
	
	result = 0;
	
	return result;
}

uint32_t init_qei(uint32_t state)
{
  uint32_t result;
	
	switch (state)
	{
		case 0: QEI_A_LOW;
		        QEI_B_LOW;
						break;
		case 1: QEI_A_HIGH;
		        QEI_B_LOW;
						break;
		case 2: QEI_A_LOW;
		        QEI_B_HIGH;
						break;
		case 3:
	 default: QEI_A_HIGH;
		        QEI_B_HIGH;
						break;
	}
	
	result = 0;
	
	return result;
}

#if 0
uint32_t qei_rotate(uint32_t * pnt_qei_state, uint32_t direction, uint32_t steps)
{
  uint32_t result;
	uint32_t i_loc;
	uint32_t qei_state_loc = *pnt_qei_state;
	
	// direction: clock-wise/forward
	if (direction == QEI_CW)
	{
		for (i_loc = 0; i_loc != steps; i_loc++)
		{
			switch (qei_state_loc)
			{
				case 0: QEI_A_RE;
					      qei_state_loc = 1;
								break;
				case 1: QEI_B_RE;
					      qei_state_loc = 3;
								break;
				case 2: QEI_B_FE;
					      qei_state_loc = 0;
								break;
				case 3: QEI_A_FE;
					      qei_state_loc = 2;
								break;
			}
		}//qei cw/forward end
	}
	// direction: counter clock-wise/reverse
	else
	{
		for (i_loc = 0; i_loc != steps; i_loc++)
		{
			switch (qei_state_loc)
			{
				case 0: QEI_B_RE;
					      qei_state_loc = 2;
								break;
				case 1: QEI_A_FE;
					      qei_state_loc = 0;
								break;
				case 2: QEI_A_RE;
					      qei_state_loc = 3;
								break;
				case 3: QEI_B_FE;
					      qei_state_loc = 1;
								break;
			}
		}//qei ccw/reverse end
	}
	
	*pnt_qei_state = qei_state_loc;
	
	result = 0;
	
	return result;
}

void sw_delay(uint32_t delay)
{
	while(delay != 0)
	{
		delay--;
	}
	
	return;
}
#endif

void SCT_IRQHandler(void)
{
	LPC_GPIO_PORT->SET[SCT0ISR_PORT] = 1<<SCT0ISR_PIN;
	
//	sct_isr_count++;
	
	LPC_SCT0->EVFLAG = 0x000000FF;				        //clear all event flags
	
	//update sw_qei_count based on the direction
	if ((LPC_SCT0->OUTPUT & (1<<sct_out_qei_direction)) == 0)
	{ //CW direction
	  _qei_step++;
	}
	else
	{ //CCW direction
	  _qei_step--;
	}//sw qei count uptede end
	
	LPC_GPIO_PORT->CLR[SCT0ISR_PORT] =  1<<SCT0ISR_PIN;
	
	return;
}

#endif
