
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "diag/Trace.h"

#include "stm32f10x.h"
//Import USART to communicate with CPU
#include "stm32f10x_usart.h"

#include "PPM.h"
#include "I2C_Master.h"
#include "MPU9250.h"
#include "delay.h"
#include "usart.h"
#include <misc.h>

// Sample pragmas to cope with warnings. Please note the related line at
// the end of this function, used to pop the compiler diagnostics status.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-declarations"
#pragma GCC diagnostic ignored "-Wreturn-type"

#define buf_size 256
#define max_msg_size 24
#define min_msg_size 6
#define delta 450
#define CPU_CORE_FREQUENCY_HZ 72000000 /* CPU core frequency in Hz */

//Global Variables
volatile uint16_t head, tail;//Head and tail of Ring buffer
uint8_t buffer[buf_size]; //Ring buffer size 256

int throttle1, throttle2, throttle3; //Keep track of throttle level
int newthrottle1, newthrottle2, newthrottle3;

int USART1_IRQHandler(void);
void Ring_Buf_Get(void);
void interpret(void);
void moveMotors(void);

//Init functions sets all the Pins to the various mode
int init(void){

	//Initializing the clocks
	//USART, I2C, TIM3, GPIOA, GPIOB and AFIO clocks enable
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA |RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3 | RCC_APB1Periph_I2C1, ENABLE);


	PWM_Pin_Configuration();//Configuration of PPM GPIO pins
	Tim_Config();//Configuration PPM for 72Mhz Clock
	initUsart();
	DelayInit();

	//Software reset of I2C line, will be re-enabled in i2c init
	DelayMs(100);
	I2C_SoftwareResetCmd(I2C1, ENABLE);
	I2C_SoftwareResetCmd(I2C1, DISABLE);
	DelayMs(100);

	I2C_Master_Init();


	return 0;
}

// ----- main() ---------------------------------------------------------------
int main(int argc, char* argv[]){

	//Low level initialization of pins
	init();
	//Enable Sensors

	calibrate_sensor();
	init_sensor();


	//Keep track of throttle level
	//throttle1 = throttle2 = throttle3 = 0;
	newthrottle1 = newthrottle2 = newthrottle3 = 4500;
	//mov(throttle1,newthrottle1,throttle2,newthrottle2,throttle3,newthrottle3);
	throttle1 = throttle2 = throttle3 = 4500;
	TIM3->CCR1=throttle1;
	TIM3->CCR2=throttle2;
	TIM3->CCR3=throttle3;


	//Head and tail of ring buffer
	head=0;
	tail=0;
	memset(buffer,0,sizeof(buffer));
	magMaxX = magMaxY = magMaxZ = magMinX = magMinY = magMinZ = 0.0;
	while (1){

		Ring_Buf_Get();
		moveMotors();
		read_acc();
		read_mag();
		read_gyro();
		AHRS_Send();

		//run_mag_calibration();

	}

}

/*
 * @Brief: Puts data into ring buffer
 * Casting of tail and head is important as overflow is part of the design of how the ring buffer works.
 * Example: If tail == 256 (overflowed), casting it will now make it 0 point to start of buffer.
 *
 * @param: None
 * @return: 0 if no error & -1 if error
 */


int USART1_IRQHandler(void){
	//If buffer is not full
	if( !(head - tail == buf_size)){
		//Important to keep uint8_t as it truncates head to give value between 0 - 255
		buffer[(uint8_t)head] = (uint8_t)USART_ReceiveData(USART1);
		head++;
		return 0;
	}else{
		//TODO: Throw error when return -1
		return -1;
	}

}

/*
 * @Brief: Gets data from ring buffer if there is data present
 * Casting of tail and head is important as overflow is part of the design of how the ring buffer works.
 * Example: If tail == 256 (overflowed), casting it will now make it 0 point to start of buffer.
 *
 * @param: None
 * @return: None
 */

void Ring_Buf_Get(void){

	//offset here is only uint8_t as buffer size is cap at 256
	uint8_t offset;

	//This gives you the absolute offset between head and tail
	offset = head - tail;

	//If buffer is not empty
	if((uint8_t)offset >= min_msg_size){
		//Check if start condition is met
		if(buffer[(uint8_t)tail] == 255 && buffer[(uint8_t)(tail+1)] == 255){//(uint8_t)offset > max_msg_size &&
			//interpret instruction
			//uint8_t test[18];
			interpret();
			tail = tail+2+buffer[(uint8_t)(tail+2)];

		}else{
			//increase pointer until start condition is met and wait for more data
			tail++;
		}
	}

	return ;

}

/*
 * @Brief: interpret checks what is the instruction type provided and interpret the data accordingly
 * as of now, only instruction to move motor is available
 * e.g. if it is 1, it is move motors. if it is 2, it could be put MCU to sleep
 * @param: None
 * @return: None
 */

void interpret(void){

	//Use switch if there is more if condition
	if(buffer[(uint8_t)(tail+3)] == 1){

		newthrottle1 = buffer[(uint8_t)(tail+4)]<<8 | buffer[(uint8_t)(tail+5)];
		newthrottle2 = buffer[(uint8_t)(tail+6)]<<8 | buffer[(uint8_t)(tail+7)];
		newthrottle3 = buffer[(uint8_t)(tail+8)]<<8 | buffer[(uint8_t)(tail+9)];
		if (newthrottle1 > 6000){
					newthrottle1 = 6000;
				}
				if (newthrottle2 > 6000){
					newthrottle2 = 6000;
				}
				if (newthrottle3 > 6000){
					newthrottle3 = 6000;
				}
				if (newthrottle1 < 3000){
					newthrottle1 = 3000;
				}
				if (newthrottle2 < 3000){
					newthrottle2 = 3000;
				}
				if (newthrottle3 < 3000){
					newthrottle3 = 3000;
				}
//
//		mov(throttle1,newthrottle1,throttle2,newthrottle2,throttle3,newthrottle3);
//		throttle1 = newthrottle1;
//		throttle2 = newthrottle2;
//		throttle3 = newthrottle3;

	}
	return;
}

/*
 * @Brief: moveMotors tries to mimic an analog movement of a control stick. This is required
 * to prevent LARGE discrete jumps of PPM signal that will cause motor controller to lose sync
 * with controller(MCU).
 * This is done by increasing PPM values with each time step.
 *
 * @param: None
 * @return: None
 */

void moveMotors(void){

	if(floor(abs(throttle1 - newthrottle1)/delta)){
		if(throttle1 < newthrottle1){
			throttle1 = throttle1 + delta;
		}else{
			throttle1 = throttle1 - delta;
		}
	}else{
		throttle1 = newthrottle1;
	}

	if(floor(abs(throttle2 - newthrottle2)/delta)){
			if(throttle2 < newthrottle2){
				throttle2 = throttle2 + delta;
			}else{
				throttle2 = throttle2 - delta;
			}
		}else{
			throttle2 = newthrottle2;
	}
	if(floor(abs(throttle3 - newthrottle3)/delta)){
			if(throttle3 < newthrottle3){
				throttle3 = throttle3 + delta;
			}else{
				throttle3 = throttle3 - delta;
			}
		}else{
			throttle3 = newthrottle3;
	}
	TIM3->CCR1=throttle1;
	TIM3->CCR2=throttle2;
	TIM3->CCR3=throttle3;

}
#pragma GCC diagnostic pop

// ----------------------------------------------------------------------------

