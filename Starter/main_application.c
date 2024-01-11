// STANDARD INCLUDES
#include <stdio.h>
#include <conio.h>

// KERNEL INCLUDES
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"

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
void LEDBar_Task(void* pvParameters);					//logicki izlazi
void Display7seg_Task(void* pvParameters);				//upravljanje displejom
void SerialSend_Task(void* pvParameters);				//slanje podataka PC-ju
void SerialReceive_Task(void* pvParameters);			//slanje podataka PC-ju
static void checkIdleCountTimerFun(TimerHandle_t tH);


// TRASNMISSION DATA - CONSTANT IN THIS APPLICATION 
const char trigger[] = "0123456789abcdef";
const char door_msg[] = "Proveri vrata!";
const char alarm_msg[] = "gepek";
unsigned volatile t_point;


// RECEPTION DATA BUFFER 
#define R_BUF_SIZE (32)
unsigned volatile r_point;


//PROMENLJIVE
static uint8_t vrata_serial[5];
static uint8_t vrata_LED[5];
static uint8_t brzina = 0;
uint8_t full = 0x00;
static int door_open = 0;
static int alarm_gepek = 1;
static char PC_msg[R_BUF_SIZE];


// 7-SEG NUMBER DATABASE - ALL HEX DIGITS [ 0 1 2 3 4 5 6 7 8 9 A B C D E F ]
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };


// GLOBAL OS-HANDLES 
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t PC_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore;
SemaphoreHandle_t seg7_BinarySemaphore;
TimerHandle_t per_TimerHandle;
TimerHandle_t CPUUsage_TimerHandle;
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
	if(door_open == 2 || (door_open == 1 && alarm_gepek == 1)){
		set_LED_BAR(1, full);//sve LEDovke ukljucene
		full = ~full;
	}
	else set_LED_BAR(1, 0x00);//sve LEDovke iskljucene
	
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
	set_LED_BAR(1, 0x00);


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
	CPUUsage_TimerHandle = xTimerCreate("TimerCPU", pdMS_TO_TICKS(100), pdTRUE, NULL, checkIdleCountTimerFun);
	xTimerStart(CPUUsage_TimerHandle, 0);

	// TASKS 
	xTaskCreate(SerialSend_Task, "PCTx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);			// SERIAL TRANSMITTER TASK
	xTaskCreate(SerialReceive_Task, "DoorRx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);		// SERIAL RECEIVER TASK 
	xTaskCreate(LEDBar_Task, "LED", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);					// CREATE LED BAR TASK
	xTaskCreate(Display7seg_Task, "7Seg", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);				// CREATE LED BAR TASK 
	r_point = 0;


	// START SCHEDULER
	vTaskStartScheduler();
	while (1);
}

// TASKS: IMPLEMENTATIONS
void LEDBar_Task(void* pvParameters) {
	uint8_t d;
	while (1) {
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		get_LED_BAR(0, &d);

		for (unsigned i = 0; i < sizeof(vrata_LED); i++) {
			vrata_LED[i] = d % 2;
			d >>= 1;
		}
		for (int i = 0; i < sizeof(vrata_LED); i++) {
			printf("%d\n", vrata_LED[i]);
		}
		printf("\n");
	}
}

void Display7seg_Task(void* pvParameters) {
	unsigned i;
	uint8_t d;
	while (1) {
		xSemaphoreTake(seg7_BinarySemaphore, portMAX_DELAY);
		i = 0;
		door_open = 0;
		while (i < sizeof(vrata_serial)) {
			select_7seg_digit(i);
			if (vrata_serial[i] != 0x00 && brzina > high_speed) {
				set_7seg_digit(hexnum[1]);
				if (i == (sizeof(vrata_serial) - 1) && door_open == 0)	//ako je otvoren samo gepek
					door_open = 1;										//dodatno proveriti prekidac
				else door_open = 2;										//upaliti diode bez provere prekidaca
			}
			else set_7seg_digit(hexnum[0]);
			i++;
		}
	}
}

void SerialSend_Task(void* pvParameters) {	//slanje poruka PC-ju
	static int brzina_int;
	static unsigned not_equal;	//flag da 2 niza nisu jednaka
	while (1) {
		xSemaphoreTake(PC_BinarySemaphore, portMAX_DELAY);
		t_point = 0;
		while (t_point < sizeof(vrata_serial)) {
			send_serial_character(COM_CH2, trigger[vrata_serial[t_point++]]);
			xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
		}
		send_serial_character(COM_CH2, 13);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt

		t_point = 0;
		brzina_int = (int)brzina;
		while (++t_point < sizeof(brzina_int)) {
			send_serial_character(COM_CH2, (char)(brzina_int/100) + '0');
			brzina_int = (brzina_int % 100) * 10;
			xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
		}
		send_serial_character(COM_CH2, 13);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt

		vTaskDelay(pdMS_TO_TICKS(1000));
		not_equal = 0;
		for (int i = 0; i < sizeof(vrata_serial); i++) {
			if (vrata_serial[i] != vrata_LED[i]) not_equal = 1;
		}
		if (not_equal == 1) {
			t_point = 0;
			while (t_point < sizeof(door_msg)) {
				send_serial_character(COM_CH2, door_msg[t_point++]);
				xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
			}
			send_serial_character(COM_CH2, 13);
			xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
		}
	}
}

void SerialReceive_Task(void* pvParameters) {
	uint8_t cc0 = 0;
	uint8_t cc1 = 0;
	uint8_t cc2 = 0;
	static int msg_src = 0;			//0 - vrata		1 - brzina		2 - PC komanda
	static int byte_flag = 0;		//oznaka za "sredinu" poruke
	static int equal = 0;
	while (1) {
		xSemaphoreTake(RXC_BinarySemaphore, portMAX_DELAY);	// ceka na serijski prijemni interapt
		get_serial_character(COM_CH0, &cc0);	//ucitava primljeni karakter u promenjivu cc
		get_serial_character(COM_CH1, &cc1);	//ucitava primljeni karakter u promenjivu cc
		get_serial_character(COM_CH2, &cc2);	//ucitava primljeni karakter u promenjivu cc

		if (cc0 == 0xfe && byte_flag == 0) {	//stizu podaci za VRATA sa kanala 0
			r_point = 0;
			msg_src = 0;
			byte_flag = 1;
		}
		else if (cc1 == 0xff && byte_flag == 0) {	//stizu podaci za BRZINU sa kanala 1
			r_point = 0;
			msg_src = 1;
			byte_flag = 1;
		}
		else if (cc2 == alarm_msg[0] && byte_flag == 0) {	//stizu podaci za ALARM sa kanala 2
			r_point = 0;
			msg_src = 2;
			byte_flag = 1;
			PC_msg[r_point++] = cc2;
		}

		else if (byte_flag == 1 && msg_src == 0) {
			if (r_point < sizeof(vrata_serial)) {
				vrata_serial[r_point++] = cc0;
				if (r_point == sizeof(vrata_serial)) byte_flag = 0;
			}
		}
		else if (byte_flag == 1 && msg_src == 1) {
			brzina = cc1;
			byte_flag = 0;
		}
		else if (byte_flag == 1 && msg_src == 2) {
			if (r_point < sizeof(PC_msg)) {
				PC_msg[r_point++] = cc2;
				if(cc2 == 13) byte_flag = 0;
				if (r_point == sizeof(PC_msg)) byte_flag = 0;
				printf("%c\n",cc2);
			}
		}

		else if (cc0 == 0xed && byte_flag == 0) {	// za svaki KRAJ poruke
			xSemaphoreGive(PC_BinarySemaphore, portMAX_DELAY);
			xSemaphoreGive(seg7_BinarySemaphore, portMAX_DELAY);
			printf("kanal 0 - kraj\n");
		}
		else if (cc1 == 0xed && byte_flag == 0) {	// za svaki KRAJ poruke
			xSemaphoreGive(PC_BinarySemaphore, portMAX_DELAY);
			xSemaphoreGive(seg7_BinarySemaphore, portMAX_DELAY);
			printf("kanal 1 - kraj\n");
		}
		else if (byte_flag == 0) {	// za svaki KRAJ poruke
			equal = 1;
			for (int i = 0; i < sizeof(alarm_msg) - 1; i++) {
				if (PC_msg[i] != alarm_msg[i]) equal = 0;
			}
			if (equal == 1) {
				if (alarm_gepek == 0) {
					alarm_gepek = 1;
					set_LED_BAR(1, 0x01);
				}
				else {
					alarm_gepek = 0;
					set_LED_BAR(1, 0x00);
				}
			}
			printf("isti string\n");
		}
	}
}

void vApplicationIdleHook(void) {
	idleHookCounter++;
}

static void checkIdleCountTimerFun(TimerHandle_t tH) {
	static uint8_t avg_counter = 0;
	static uint64_t cnt_sum = 0;
	cnt_sum += idleHookCounter;
	
	idleHookCounter = 0;
	if (avg_counter++ == 9) {
		//printf("Prosecni IdleHook counter je: %lld\n", cnt_sum / 10);
		// Idle Hook bez dodatnih taskova je: ~2500000 - Kalibrisati spram racunara
		uint64_t average = cnt_sum / 10;
		float odnos = (float)average / 3800000;//2500000;
		if (odnos > 1) {
			odnos = 1;
		}
		int procenat = (int)((float)odnos * 100);

		//printf("Zauzetost procesora u je: %d\n", 100 - procenat);
		/*for (int i = 0; i < 5; i++) {
			printf("%d", vrata_serial[i]);
		}
		printf("\n");*/
		for (int i = 0; i < sizeof(PC_msg); i++) {
			printf("%c", PC_msg[i]);
		}
		cnt_sum = 0;
		avg_counter = 0;
	}
}
