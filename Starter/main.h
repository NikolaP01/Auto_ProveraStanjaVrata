#pragma once
// STANDARD INCLUDES
#include <stdio.h>
#include <conio.h>

// KERNEL INCLUDES
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "timers.h"
#include "extint.h"


//Definisanje tipova podataka
typedef const char const_word;
typedef unsigned volatile uvolatile_t;
typedef unsigned int uint_t;

extern const_word trigger[16], door_msg[14], alarm_msg[5];
extern uvolatile_t t_point;
extern uvolatile_t r_point;

extern uint8_t vrata_serial[5];
extern uint8_t vrata_LED[5];
extern uint8_t brzina;
extern uint8_t full;
extern uint8_t alarm_LED;
extern uint_t door_open;
extern uint_t alarm_gepek;

extern SemaphoreHandle_t LED_INT_BinarySemaphore;
extern SemaphoreHandle_t TBE_BinarySemaphore;
extern SemaphoreHandle_t PC_BinarySemaphore;
extern SemaphoreHandle_t RXC_BinarySemaphore;
extern SemaphoreHandle_t seg7_BinarySemaphore;
extern TimerHandle_t per_TimerHandle;
extern uint64_t idleHookCounter;