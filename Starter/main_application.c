// STANDARD INCLUDES
#include <stdio.h>
#include <conio.h>

// KERNEL INCLUDES
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"
#include "main.h"

// HARDWARE SIMULATOR UTILITY FUNCTIONS  
#include "HW_access.h"


// SERIAL SIMULATOR CHANNEL TO USE 
#define COM_CH0 (0)		//kanal za senzore
#define COM_CH1 (1)		//kanal za briznu vozila
#define COM_CH2 (2)		//kanal za komunikaciju sa PC-jem


// TASK PRIORITIES 
#define	TASK_SERIAL_SEND_PRI		( tskIDLE_PRIORITY + 2 )
#define TASK_SERIAl_REC_PRI			( tskIDLE_PRIORITY + 3 )
#define	SERVICE_TASK_PRI		( tskIDLE_PRIORITY + 1 )


//KONSTANTE
#define high_speed (0x09)


// TASKS: FORWARD DECLARATIONS 
void main_demo(void);									//main funckija
void LEDBar_Task(void* pvParameters);					//citanje stanja prekidaca na LED baru
void Display7seg_Task(void* pvParameters);				//upravljanje displejom
void SerialSend_Task(void* pvParameters);				//slanje podataka PC-ju
void SerialReceive_Task(void* pvParameters);			//slanje podataka PC-ju
void vApplicationIdleHook(void);						//idle task


// TRASNMISSION DATA - CONSTANT IN THIS APPLICATION 
const_word trigger[] = "0123456789abcdef";
const_word door_msg[] = "Proveri vrata!";
const_word alarm_msg[] = "gepek";
uvolatile_t t_point;

// RECEPTION DATA BUFFER 
#define R_BUF_SIZE (32)
uvolatile_t r_point;


//PROMENLJIVE
static char PC_msg[R_BUF_SIZE];
uint8_t vrata_serial[5];
uint8_t vrata_LED[5];
uint8_t brzina = 0;
uint8_t full = 0x00;
uint8_t alarm_LED = 0x00;
uint_t door_open = 0;
uint_t alarm_gepek = 1;


// 7-SEG NUMBER DATABASE - ALL HEX DIGITS [ 0 1 2 3 4 5 6 7 8 9 A B C D E F ]
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };


// GLOBAL OS-HANDLES 
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t PC_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore;
SemaphoreHandle_t seg7_BinarySemaphore;
TimerHandle_t per_TimerHandle;
uint64_t idleHookCounter;


// INTERRUPTS //
static uint32_t OnLED_ChangeInterrupt(void) {	// OPC - ON INPUT CHANGE - INTERRUPT HANDLER 
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(LED_INT_BinarySemaphore, &xHigherPTW);
	portYIELD_FROM_ISR(xHigherPTW);
}

static uint32_t prvProcessTBEInterrupt(void) {	// TBE - TRANSMISSION BUFFER EMPTY - INTERRUPT HANDLER 
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(TBE_BinarySemaphore, &xHigherPTW);
	portYIELD_FROM_ISR(xHigherPTW);
}

static uint32_t prvProcessRXCInterrupt(void) {	// RXC - RECEPTION COMPLETE - INTERRUPT HANDLER 
	BaseType_t xHigherPTW = pdFALSE;

	xSemaphoreGiveFromISR(RXC_BinarySemaphore, &xHigherPTW);
	portYIELD_FROM_ISR(xHigherPTW);
}


// PERIODIC TIMER CALLBACK 
static void TimerCallback(TimerHandle_t xTimer)
{
	if( (door_open == (uint_t)2) || ( (door_open == (uint_t)1) && (alarm_gepek == (uint_t)1) ) ){
		set_LED_BAR(1, full);	//naizmenicno se pale i gase diode
		full = ~full;
	}
	else {
		set_LED_BAR(1, alarm_LED);	//ukljuci prvu LED prema stanju alarma za gepek
	}
}


// MAIN - SYSTEM STARTUP POINT 
void main_demo(void) {
	// INITIALIZATION OF THE PERIPHERALS
	init_7seg_comm();
	init_LED_comm();
	init_serial_downlink(COM_CH0);	// inicijalizacija serijske RX na kanalu 0
	init_serial_downlink(COM_CH1);	// inicijalizacija serijske RX na kanalu 1
	init_serial_uplink(COM_CH2);	// inicijalizacija serijske TX na kanalu 2
	init_serial_downlink(COM_CH2);	// inicijalizacija serijske RX na kanalu 2


	// INTERRUPT HANDLERS
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);		// ON INPUT CHANGE INTERRUPT HANDLER 
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);	// SERIAL TRANSMITT INTERRUPT HANDLER 
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);	// SERIAL RECEPTION INTERRUPT HANDLER 


	// BINARY SEMAPHORES
	LED_INT_BinarySemaphore = xSemaphoreCreateBinary();	// CREATE LED INTERRUPT SEMAPHORE 
	TBE_BinarySemaphore = xSemaphoreCreateBinary();		// CREATE TBE SEMAPHORE - SERIAL TRANSMIT COMM 
	RXC_BinarySemaphore = xSemaphoreCreateBinary();		// CREATE RXC SEMAPHORE - SERIAL RECEIVE COMM
	PC_BinarySemaphore = xSemaphoreCreateBinary();
	seg7_BinarySemaphore = xSemaphoreCreateBinary();


	// TIMERS
	per_TimerHandle = xTimerCreate("Timer", pdMS_TO_TICKS(500), pdTRUE, NULL, TimerCallback);
	xTimerStart(per_TimerHandle, 0);


	// TASKS 
	xTaskCreate(SerialSend_Task, "Tx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);			// SERIAL TRANSMITTER TASK
	xTaskCreate(SerialReceive_Task, "Rx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);		// SERIAL RECEIVER TASK 
	xTaskCreate(LEDBar_Task, "LED", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);					// CREATE LED BAR TASK
	xTaskCreate(Display7seg_Task, "7Seg", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);				// CREATE LED BAR TASK 
	r_point = 0;


	// START SCHEDULER
	vTaskStartScheduler();
	for (;;);
}

// TASKS: IMPLEMENTATIONS
void LEDBar_Task(void* pvParameters) {
	uint8_t d;
	for (;;) {
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		get_LED_BAR(0, &d);
		for (uint_t i = 0; i < sizeof(vrata_LED); i++) {
			vrata_LED[i] = d % (uint8_t)2;
			d >>= 1;
		}
	}
}

void Display7seg_Task(void* pvParameters) {
	uint_t i;
	for (;;) {
		xSemaphoreTake(seg7_BinarySemaphore, portMAX_DELAY);
		i = 0;
		door_open = 0;
		while (i < sizeof(vrata_serial)) {
			select_7seg_digit((uint8_t)i);
			if ((vrata_serial[i] != (uint8_t)0x00) && (brzina > (uint8_t)high_speed)) {
				set_7seg_digit(hexnum[1]);
				if ( (i == (sizeof(vrata_serial) - (uint_t)1)) && (door_open == (uint_t)0) ) {	//ako je otvoren samo gepek		//PROVERI MISRU
					door_open = (uint_t)1;	//dodatno proveriti prekidac
				}
				else {
					door_open = (uint_t)2;	//ako je otvoreno bilo sta da nije gepek upaliti diode bez provere prekidaca

				}
			}
			else {
				set_7seg_digit(hexnum[0]);
			}
			i++;
		}
	}
}

void SerialSend_Task(void* pvParameters) {	//slanje poruka PC-ju
	uint_t not_equal;	//flag da 2 niza nisu jednaka
	for (;;) {
		xSemaphoreTake(PC_BinarySemaphore, portMAX_DELAY);
		t_point = 0;
		while (t_point < sizeof(vrata_serial)) {
			send_serial_character(COM_CH2, (uint8_t)trigger[vrata_serial[t_point]]);
			t_point++;
			xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);
		}
		send_serial_character(COM_CH2, 13);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);

		t_point = 0;
		uint_t brzina_int = (uint_t)brzina;
		while (++t_point < sizeof(brzina_int)) {
			send_serial_character(COM_CH2, (uint8_t)(brzina_int/(uint_t)100) + (uint8_t)'0' );
			brzina_int = (brzina_int % (uint_t)100) * (uint_t)10;
			xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);
		}
		send_serial_character(COM_CH2, 13);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);

		vTaskDelay(pdMS_TO_TICKS(2000));
		not_equal = 0;
		for (uint_t i = 0; i < sizeof(vrata_serial); i++) {
			if (vrata_serial[i] != vrata_LED[i]) {
				not_equal = (uint_t)1;
			}
		}
		if (not_equal == (uint_t)1) {
			t_point = 0;
			while (t_point < sizeof(door_msg)) {
				send_serial_character(COM_CH2, door_msg[t_point++]);
				xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);
			}
			send_serial_character(COM_CH2, 13);
			xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);
		}
	}
}

void SerialReceive_Task(void* pvParameters) {
	uint8_t cc0 = 0;
	uint8_t cc1 = 0;
	uint8_t cc2 = 0;
	uint_t msg_src = 0;			//0 - vrata		1 - brzina		2 - PC komanda
	uint_t byte_flag = 0;		//flag za "sredinu" poruke
	uint_t equal = 0;			//flag za jednakost stringova
	for (;;) {
		xSemaphoreTake(RXC_BinarySemaphore, portMAX_DELAY);
		get_serial_character(COM_CH0, &cc0);
		get_serial_character(COM_CH1, &cc1);
		get_serial_character(COM_CH2, &cc2);

		if ( (cc0 == (uint8_t)0xfe) && (byte_flag == (uint_t)0) ) {	//stizu podaci za VRATA sa kanala 0
			r_point = 0;
			msg_src = 0;
			byte_flag = 1;
		}
		else if ( (cc1 == (uint8_t)0xff) && (byte_flag == (uint_t)0) ) {	//stizu podaci za BRZINU sa kanala 1
			r_point = 0;
			msg_src = 1;
			byte_flag = 1;
		}
		else if ( (cc2 == (uint8_t)alarm_msg[0]) && (byte_flag == (uint_t)0) ) {	//stizu podaci za ALARM sa kanala 2
			r_point = 0;
			msg_src = 2;
			byte_flag = 1;
			PC_msg[r_point] = (char)cc2;
			r_point++;
		}

		else if ( (byte_flag == (uint_t)1) && (msg_src == (uint_t)0) ) {
			if (r_point < sizeof(vrata_serial)) {
				vrata_serial[r_point] = cc0;
				r_point++;
				if (r_point == sizeof(vrata_serial)) {
					byte_flag = 0;
				}
			}
		}
		else if ( (byte_flag == (uint_t)1) && (msg_src == (uint_t)1) ) {
			brzina = cc1;
			byte_flag = 0;
		}
		else if ( (byte_flag == (uint_t)1) && (msg_src == (uint_t)2) ) {
			if (r_point < sizeof(PC_msg)) {
				PC_msg[r_point] = (char)cc2;
				r_point++;
				if ( (cc2 == (uint8_t)10) || (cc2 == (uint8_t)13) ) {	//13 - CR	10 - NOVI RED
					byte_flag = 0;
					equal = 1;
					for (uint_t i = 0; i < ((uint_t)r_point - (uint_t)1); i++) {
						if (PC_msg[i] != alarm_msg[i]) {
							equal = 0;
						}
					}
					if (equal == (uint_t)1) {
						if (alarm_gepek == (uint_t)0) {
							alarm_gepek = 1;
							alarm_LED = 0x01;
						}
						else {
							alarm_gepek = 0;
							alarm_LED = 0x00;
						}
					}
				}
			}
		}

		else if ( (cc0 == (uint8_t)0xed) && (byte_flag == (uint_t)0) && (msg_src == (uint_t)0) ) {	// za svaki KRAJ poruke sa kanala 0
			xSemaphoreGive(PC_BinarySemaphore, portMAX_DELAY);
			xSemaphoreGive(seg7_BinarySemaphore, portMAX_DELAY);
		}
		else if ( (cc1 == (uint8_t)0xed) && (byte_flag == (uint_t)0) && (msg_src == (uint_t)1) ) {	// za svaki KRAJ poruke sa kanala 1
			xSemaphoreGive(PC_BinarySemaphore, portMAX_DELAY);
			xSemaphoreGive(seg7_BinarySemaphore, portMAX_DELAY);
		}
		else {
			cc0 = 0;
			cc1 = 0;
			cc2 = 0;
		}
	}
}

void vApplicationIdleHook(void) {
	idleHookCounter++;
}