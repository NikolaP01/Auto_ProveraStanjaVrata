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


// TASKS: FORWARD DECLARATIONS 
void LEDBar_Task(void* pvParameters);				//logicki izlazi
void Display7seg_Task(void* pvParameters);			//upravljanje displejom
void SerialSend_Task(void* pvParameters);			//slanje podataka PC-ju
void SerialReceiveDoor_Task(void* pvParameters);	//provera stanja vrata i brzine
static void checkIdleCountTimerFun(TimerHandle_t tH);


// TRASNMISSION DATA - CONSTANT IN THIS APPLICATION 
const char trigger[] = "0123456789abcdef";
unsigned volatile t_point;


// RECEPTION DATA BUFFER 
#define R_BUF_SIZE (32)
uint8_t vrata_serial[5];
uint8_t vrata_LED[5];
uint8_t brzina = 0;
unsigned volatile r_point;


// 7-SEG NUMBER DATABASE - ALL HEX DIGITS [ 0 1 2 3 4 5 6 7 8 9 A B C D E F ]
static const char hexnum[] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71 };


// GLOBAL OS-HANDLES 
SemaphoreHandle_t LED_INT_BinarySemaphore;
SemaphoreHandle_t TBE_BinarySemaphore;
SemaphoreHandle_t TBE_pomocni_BinarySemaphore;
SemaphoreHandle_t RXC_BinarySemaphore;
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
	static uint8_t bdt = 0;
	//set_LED_BAR(2, 0x00);//sve LEDovke iskljucene
	//set_LED_BAR(3, 0xF0);// gornje 4 LEDovke ukljucene

	//set_LED_BAR(0, bdt); // ukljucena LED-ovka se pomera od dole ka gore
	bdt <<= 1;
	if (bdt == 0)
		bdt = 1;
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
	select_7seg_digit(4);
	set_7seg_digit(0x40);
	set_LED_BAR(1, 0x00);


	// INTERRUPT HANDLERS
	vPortSetInterruptHandler(portINTERRUPT_SRL_OIC, OnLED_ChangeInterrupt);		// ON INPUT CHANGE INTERRUPT HANDLER 
	vPortSetInterruptHandler(portINTERRUPT_SRL_TBE, prvProcessTBEInterrupt);	// SERIAL TRANSMITT INTERRUPT HANDLER 
	vPortSetInterruptHandler(portINTERRUPT_SRL_RXC, prvProcessRXCInterrupt);	// SERIAL RECEPTION INTERRUPT HANDLER 


	// BINARY SEMAPHORES
	LED_INT_BinarySemaphore = xSemaphoreCreateBinary();	// CREATE LED INTERRUPT SEMAPHORE 
	TBE_BinarySemaphore = xSemaphoreCreateBinary();		// CREATE TBE SEMAPHORE - SERIAL TRANSMIT COMM 
	RXC_BinarySemaphore = xSemaphoreCreateBinary();		// CREATE RXC SEMAPHORE - SERIAL RECEIVE COMM
	TBE_pomocni_BinarySemaphore = xSemaphoreCreateBinary();


	// TIMERS
	per_TimerHandle = xTimerCreate("Timer", pdMS_TO_TICKS(500), pdTRUE, NULL, TimerCallback);
	xTimerStart(per_TimerHandle, 0);
	CPUUsage_TimerHandle = xTimerCreate("TimerCPU", pdMS_TO_TICKS(100), pdTRUE, NULL, checkIdleCountTimerFun);
	xTimerStart(CPUUsage_TimerHandle, 0);

	// TASKS 
	xTaskCreate(SerialSendPC_Task, "PCTx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAL_SEND_PRI, NULL);			// SERIAL TRANSMITTER TASK
	xTaskCreate(SerialReceivePC_Task, "PCRx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);		// SERIAL RECEIVER TASK 
	xTaskCreate(SerialReceiveDoor_Task, "DoorRx", configMINIMAL_STACK_SIZE, NULL, TASK_SERIAl_REC_PRI, NULL);	// SERIAL RECEIVER TASK 
	xTaskCreate(LEDBar_Task, "LED", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);					// CREATE LED BAR TASK
	//xTaskCreate(Display7seg_Task, "7Seg", configMINIMAL_STACK_SIZE, NULL, SERVICE_TASK_PRI, NULL);					// CREATE LED BAR TASK 
	r_point = 0;


	// START SCHEDULER
	vTaskStartScheduler();
	while (1);
}

// TASKS: IMPLEMENTATIONS
void LEDBar_Task(void* pvParameters) {
	unsigned i;
	uint8_t d;
	while (1) {
		xSemaphoreTake(LED_INT_BinarySemaphore, portMAX_DELAY);
		get_LED_BAR(0, &d);
		
		/*i = 3;
		do {
			i--;
			select_7seg_digit(i);
			set_7seg_digit(hexnum[d % 10]);
			d /= 10;
		} while (i > 0);*/

	}
}

void Display7seg_Task(void* pvParameters) {
	unsigned i;
	uint8_t d;
	while (1) {
		//komentar za tesitiranje
	}
}

void SerialSendPC_Task(void* pvParameters) {	//slanje poruka PC-ju
	while (1) {
		xSemaphoreTake(TBE_pomocni_BinarySemaphore, portMAX_DELAY);
		t_point = 0;
		while (t_point < sizeof(vrata_serial)) {
			send_serial_character(COM_CH2, trigger[vrata_serial[t_point++]]);
			xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
		}
		send_serial_character(COM_CH2, 13);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
		send_serial_character(COM_CH2, trigger[brzina]);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
		send_serial_character(COM_CH2, 13);
		xSemaphoreTake(TBE_BinarySemaphore, portMAX_DELAY);// kada se koristi predajni interapt
	}
}

void SerialReceivePC_Task(void* pvParameters) {	//provera stanja vrata
	uint8_t cc = 0;
	static uint8_t cnt_door = 0;	//brojac koliko puta je stigao interrupt za VRATA
	static uint8_t cnt_sens = 0;	//brojac koliko puta je stigao interrupt za SENZORE
	static int flag;
	while (1) {
		xSemaphoreTake(RXC_BinarySemaphore, portMAX_DELAY);	// ceka na serijski prijemni interapt
		get_serial_character(COM_CH0, &cc);	//ucitava primljeni karakter u promenjivu cc

		if (cc == 0xfe) {	//stizu podaci za stanje VRATA
			r_point = 0;
			cnt_door++;
			flag = 0;
			select_7seg_digit(6);						//obrisati
			set_7seg_digit(hexnum[cnt_door & 0x0F]);	//obrisati
		}
		else if (cc == 0xff) {	//stizu podaci za stanje SENZORA
			r_point = 0;
			cnt_sens++;
			flag = 1;
			select_7seg_digit(5);						//obrisati
			set_7seg_digit(hexnum[cnt_sens & 0x0F]);	//obrisati
		}
		else if (cc == 0xed) {	// za svaki KRAJ poruke
			xSemaphoreGive(TBE_pomocni_BinarySemaphore, portMAX_DELAY);
		}
		else if (r_point < sizeof(vrata_serial)) { // pamti karaktere
			if (flag == 0) {
				vrata_serial[r_point++] = cc;
			}
			else if (flag == 1) {
				brzina = cc;
				flag++;		//zastita u slucaju da se unese vise bajtova pre STOP bajta, pamti se vrednost samo prvog
			}
		}
	}
}

void SerialReceiveDoor_Task(void* pvParameters) {	//provera stanja vrata
	uint8_t cc = 0;
	static uint8_t cnt_door = 0;	//brojac koliko puta je stigao interrupt za VRATA
	static uint8_t cnt_sens = 0;	//brojac koliko puta je stigao interrupt za SENZORE
	static int flag;
	while (1) {
		xSemaphoreTake(RXC_BinarySemaphore, portMAX_DELAY);	// ceka na serijski prijemni interapt
		get_serial_character(COM_CH0, &cc);	//ucitava primljeni karakter u promenjivu cc

		if (cc == 0xfe) {	//stizu podaci za stanje VRATA
			r_point = 0;
			cnt_door++;
			flag = 0;
			select_7seg_digit(6);						//obrisati
			set_7seg_digit(hexnum[cnt_door & 0x0F]);	//obrisati
		}
		else if (cc == 0xff) {	//stizu podaci za stanje SENZORA
			r_point = 0;
			cnt_sens++;
			flag = 1;
			select_7seg_digit(5);						//obrisati
			set_7seg_digit(hexnum[cnt_sens & 0x0F]);	//obrisati
		}
		else if (cc == 0xed) {	// za svaki KRAJ poruke
			xSemaphoreGive(TBE_pomocni_BinarySemaphore, portMAX_DELAY);
		}
		else if (r_point < sizeof(vrata_serial)) { // pamti karaktere
			if (flag == 0) {
				vrata_serial[r_point++] = cc;
			}
			else if (flag == 1) {
				brzina = cc;
				flag++;		//zastita u slucaju da se unese vise bajtova pre STOP bajta, pamti se vrednost samo prvog
			}
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
		for (int i = 0; i < 5; i++) {
			printf("%c", trigger[vrata_serial[i]]);
		}
		printf("\n");
		cnt_sum = 0;
		avg_counter = 0;
	}
}
