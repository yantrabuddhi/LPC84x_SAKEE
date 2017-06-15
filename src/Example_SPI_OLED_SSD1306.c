/*
===============================================================================
 Name        : Example_SPI_OLED_SSD1306.c
 Author      : $(author)
 Version     :
 Copyright   : $(copyright)
 Description : Program entry point
===============================================================================
*/

#include <cr_section_macros.h>
#include <stdio.h>
#include <string.h>

#include "LPC8xx.h"
#include "core_cm0plus.h"
#include "syscon.h"
#include "gpio.h"
#include "swm.h"

#include "ssd1306.h"
#include "delay.h"
#include "gfx.h"
#include "testers/gfx_tester.h"

#include "adc_dma.h"
#include "qei.h"

#define LED_PIN		  (P0_0)
#define BUTTON_PIN  P1_21

#define bit(_x)    ( 1 << (_x) )

/*
 Pins used in this application:

 OLED Display
 LPC    ARD   Description
 -----  ---   -----------
 P0.6	D13	  OLED SCK
 P1.19	D11	  OLED Data/MOSI
 P1.18	D10	  OLED CS
 P0.16	D8	  OLED Reset
 P0.1	D7	  OLED Data/Command Select

 SCT QEI and ADC
 LPC    ARD   Description
 -----  ---   -----------
 P1.21  D4	  QEI0 switch/button
 P0.20  D3    SCT_IN0: QEI0 phA (internal pullup enabled)
 P0.21  D2    SCT_IN1: QEI0 phB (internal pullup enabled)

 DMA ADC
 LPC    ARD   Description
 -----  ---   -----------
 P0.14  A0    Analog Input (ADC2)

 LEDS
 LPC    ARD   Description
 -----  ---   -----------
 P0.0  	--	  Debug LED (GREEN)
*/

uint32_t button_pressed(void);

void led_on(void)
{
  LPC_GPIO_PORT->CLR0 = (1 << LED_PIN);
}

void led_off(void)
{
  LPC_GPIO_PORT->SET0 = (1 << LED_PIN);
}

static inline uint32_t button_read(void)
{
  // button is active LOW
  return ((~(LPC_GPIO_PORT->PIN[BUTTON_PIN/32])) & bit(BUTTON_PIN%32) );
}

int main(void)
{
	SystemCoreClockUpdate();

	// Initialize the delay timer (systick) with 1ms intervals
	// use systick to trigger ADC, enable later when using other hw timer
	delay_init(12000000 / 1000);

	// Reset and enable the GPIO module (peripherals_lib)
	GPIOInit();

	// Initialize the status LED
	GPIOSetDir(LED_PIN/32, LED_PIN%32, 1);
	led_off();

	// Initialize Button
	GPIOSetDir(BUTTON_PIN/32, BUTTON_PIN%32, 0);

	// Initialize the SCT based quadrature decoder
	qei_init();

	// Initialize the DMA and systick based ADC sampler
	adc_dma_init();
	adc_dma_set_rate(100); // Set the ADC sample rate in microseconds

	// Initialize the SSD1306 display
	ssd1306_init();
	ssd1306_refresh();

#if 0
	// GFX Tester
	while (1) gfx_tester_run();
#endif

#if 1
	// test QEI
	while(1)
	{
	  static int32_t last_offset = 0;
	  static int32_t btn_count = 0;

	  ssd1306_clear();

	  ssd1306_set_text(8 , 0, 1, "ABS", 2);
	  ssd1306_set_text(60, 0, 1, "OFFSET", 2);

	  // ABS
	  int32_t abs = qei_abs_step();
	  gfx_printdec(8, 20, abs, 2, 1);

	  // Display offset if non-zero
	  int32_t cur_offset = qei_offset_step();
	  if ( cur_offset )
	  {
	    last_offset = cur_offset;
	  }
	  gfx_printdec(60, 20, last_offset, 2, 1);

	  // Select button counter
	  btn_count += (button_pressed() ? 1 : 0);
	  ssd1306_set_text(8 , 40, 1, "btn", 2);
	  gfx_printdec(60, 40, btn_count, 2, 1);

	  ssd1306_refresh();

	  delay_ms(1);
	}
#endif

	ssd1306_set_text(8, 0, 1, "WAITING FOR", 2);
	ssd1306_set_text(8, 16, 1, "ADC TRIGGER", 2);
	ssd1306_set_text(8, 36, 1, "RANGE:", 1);
	gfx_printdec(8, 48, 0x40, 2, 1);
	ssd1306_set_text(30, 54, 1, "..", 1);
	gfx_printdec(44, 48, 0xCF, 2, 1);
	ssd1306_refresh();

	while(1)
	{
		if ( !adc_dma_busy() )
		{
		  // Start sampling, After buffers are full
		  // sampling will stop --> adc_dma_busy() return false
//		  adc_dma_start();

		  // Start with threshold (low, high, mode)
		  // interrupt mode: 0 = disabled, 1 = outside threshold, 2 = crossing threshold
		  adc_dma_start_with_threshold(0x40, 0xCF, 2);
		  //adc_dma_start_with_threshold(0x3FF, 0xFFF, 2);
		  int16_t sample = adc_dma_get_threshold_sample();
		  if (sample < 0)
		  {
		  }
		  else
		  {
        gfx_graticule_cfg_t grcfg =
        {
            .w = 64,			// 64 pixels wide
            .h = 32,			// 32 pixels high
            .lines = GFX_GRATICULE_LINES_HOR | GFX_GRATICULE_LINES_VER,
            .line_spacing = 2,	// Divider lines are 1 dot every 2 pixels
            .block_spacing = 8	// Each block is 8x8 pixels
        };

        ssd1306_clear();

        // Render the title bars
        ssd1306_set_text(0, 0, 1, "NXP SAKEE", 1);
        ssd1306_set_text(127 - 48, 0, 1, "WAVEFORM", 1);	// 48 pixels wide

        // Render the graticule and waveform
        gfx_graticule(0, 16, &grcfg, 1);
        // Make sure we have at least 32 samples before the trigger, or start at 0 if less
        uint16_t start = sample >= 32 ? sample - 32 : 0;
        gfx_waveform_64_32(0, 16, 1, adc_dma_get_buffer(), start, 1024, 4);

        // Refresh the display
        ssd1306_refresh();
		  }
		}

		// Stop sampling
		// adc_dma_stop();
	}

	return 0;
}

/**
 * Check if button A,B,C state are pressed, include some software
 * debouncing.
 *
 * Note: Only set bit when Button is state change from
 * idle -> pressed. Press and hold only report 1 time, release
 * won't report as well
 *
 * @return Bitmask of pressed buttons e.g If BUTTON_A is pressed
 * bit 31 will be set.
 */
uint32_t button_pressed(void)
{
  // must be exponent of 2
  enum { MAX_CHECKS = 2, SAMPLE_TIME = 5 };

  /* Array that maintains bounce status, which is sampled
   * 10 ms each. Debounced state is valid if all values
   * on a switch maintain the same state (bit set or clear)
   */
  static uint32_t lastReadTime = 0;
  static uint32_t states[MAX_CHECKS] = { 0 };
  static uint32_t index = 0;

  // Last debounce state, used to detect changes
  static uint32_t lastDebounced = 0;

  // Too soon, nothing to do
  if (millis() - lastReadTime < SAMPLE_TIME ) return 0;

  lastReadTime = millis();

  // Take current read and mask with BUTTONs
  // Note: Bitwise inverted since buttons are active (pressed) LOW
  uint32_t debounced = button_read();

  // Copy current state into array
  states[ (index & (MAX_CHECKS-1)) ] = debounced;
  index++;

  // Bitwise And all the state in the array together to get the result
  // This means pin must stay at least MAX_CHECKS time to be realized as changed
  for(int i=0; i<MAX_CHECKS; i++)
  {
    debounced &= states[i];
  }

  // result is button changed and current debounce is set
  // Mean button is pressed (idle previously)
  uint32_t result = (debounced ^ lastDebounced) & debounced;

  lastDebounced = debounced;

  return result;
}
