/*
 * Pi2RMini.c
 *
 * Pi2R Mini device firmware for v2.x boards
 * Logan Microelectronics 2013-2014
 * http://awjlogan.wordpress.com
 * https://github.com/awjlogan/pi2rmini
 *
 * Written in AVR Studio 5 for AVR GCC
 * Target: Atmel ATTiny13
 * 4.8 MHz internal oscillator / 8 = 600 kHz
 * Pins:	1	!RST					PB5
 *			2	Green LED				PB3
 *			3	Red LED					PB4
 *			4	GND		
 *			5	Switch					PB0
 *			6	RPi communication line	PB1
 *			7	RPi MOSET				PB2
 *			8	+3V3
 *
 * Fuse calculated:		FF 69 (http://www.engbedded.com/fusecalc)
 * avrdude arguments:	-U lfuse:w:0x69:m -U hfuse:w:0xff:m
 * for: 4.8 MHz int osc, 14CK + 64 ms startup, CKDIV, no WDT, SPI enabled, !RST enabled, no BOD
 *
 * To change the startup, reset, and shutdown delays, alter lines with 'seconds = xx'. The TMR0 interrupt hits every
 * ~100 ms, so the seconds value should be 'delay in seconds x 10'. The variable 'flash' acts in the same way.
*/			

#define F_CPU		600000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

// Status register definitions
#define	PI_ERROR	1
#define PI_ON		8
#define PI_OFF		16
#define BUTTON		32

#define PI_COMM		PINB1

// Pin assignments
#define SW			0x01
#define PI_FET		0x04
#define LED_G		0x08
#define LED_R		0x10

// common cathode status LED on/off
#define RED_OFF		PORTB &= ~(LED_R)
#define RED_ON		PORTB |= LED_R
#define GRN_OFF		PORTB &= ~(LED_G)
#define GRN_ON		PORTB |= LED_G

// Function prototypes
static void setup(void);
static void button(void);
static void reset(void);

// Global variables
static volatile uint8_t state = 0;
static volatile uint8_t flash = 0;
static volatile uint16_t seconds = 0;

int main(void)
{
	setup();
	state |= PI_OFF;
	RED_ON;
	GRN_OFF;
	
	// check status registers and respond
	while(1)
    {	
		if (state & BUTTON) {
			// disable PCINT while button press is handled
			PCMSK &= ~(1<<PCINT0);
			state &= ~(BUTTON);
			_delay_ms(50);					// debounce
			if (!(PINB & PINB0)) {
				button();
			}
			PCMSK |= (1<<PCINT0);
		}
		
		// flash red LED if in error state
		if (state & PI_ERROR) {
			if (!flash) {
				PORTB ^= LED_R;
				flash = 2;
			}
		}
	}	
}

static void button(void) {
	if (state & PI_OFF) {
		state = PI_ON;
		PORTB |= PI_FET;
		GRN_ON;
		RED_ON;
		
		// wait 60 secs for RPi to start up
		seconds = 600;
		while(PINB & (1<<PI_COMM)) {
			if (!seconds) {
				state |= PI_ERROR;
				GRN_OFF;
				return;
			}
		}
		RED_OFF;
		return;
	}
	
	if (state & PI_ON) {
		// check for forced reset - 4 s press
		seconds = 40;
		RED_ON;
		GRN_OFF;
		while (!(PINB & (1<<PINB0))) {
			// flash red/green
			if (!flash) {
				PORTB ^= (LED_G + LED_R);
				flash = 2;
			}
			// time up, reset!
			if (!seconds) {
				cli();
				reset();
				while(!(PINB & (1<<PINB0)));
				sei();
				return;
			}
		}
		
		// don't turn off in error state
		if (state & PI_ERROR) {
			RED_ON;
			GRN_OFF;
			return;
		}		
		
		// send shutdown signal, wait 10 secs for response
		RED_ON;
		GRN_OFF;
		DDRB |= (1<<DDB1);
		PORTB |= (1<<PINB1);
		_delay_ms(1);
		PORTB &= ~(1<<PINB1);
		_delay_ms(100);
		DDRB &= ~(1<<DDB1);
		PORTB |= (1<<PINB1);
		seconds = 100;
		while(PINB & (1<<PI_COMM)) {
			if (!seconds) {
				RED_ON;
				GRN_OFF;
				state |= PI_ERROR;
				return;
			}
		}
		
		// wait 30 secs, then shutdown
		GRN_ON;
		seconds = 300;
		while(seconds) {}
		reset();
		return;
	}
}

static void reset(void) {
	state = PI_OFF;
	PORTB = LED_R;
	DDRB = ((1<<DDB2)|(1<<DDB3)|(1<<DDB4));	
	return;
}

static void setup(void) {
	cli();
	
	// oscillator setup
	CLKPR = 0x80;	
	CLKPR = 0x01;
	
	// interrupt setup
	PCMSK |= 1 << PCINT0;	
	GIMSK |= 1 << PCIE;
	TIMSK0 |= 1 << TOIE0;
	
	// TMR0 setup
	// TCNT0 = 21 will give 0.1s count up time
	TCCR0A = 0;		
	TCCR0B = 0x05;	
					
	// PORTB setup
	// MCUCR |= (1<<PUD); // for v2.x, pull ups are enabled
	DDRB = ((1<<DDB2)|(1<<DDB3)|(1<<DDB4));
	PORTB |= ((1<<PINB0)|(1<<PI_COMM));	

	sei();
}

ISR(PCINT0_vect) {
	state |= BUTTON;
}

ISR(TIM0_OVF_vect) {
	if(flash) flash--;
	if(seconds) seconds--;
	TCNT0 = 21;					// reset for ~100 ms interval
}
