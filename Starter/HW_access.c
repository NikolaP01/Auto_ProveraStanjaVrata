#include "BlackBox.h"
#include "HW_access.h"

/*
 * LED BAR SECTION
 */

#define LED_BARS_N (10)

static commData qSock;

int init_LED_comm(void)
{
	if (HWSIM_openXtLink(&qSock, DEV_LED_BARS, LOCAL_ADDRESS) == comm_OK)
		return 0;
	else
		return -1;
}

int close_LED_comm(void)
{
	return HWSIM_closeLink(&qSock);
}

int set_LED_BAR(uint8_t b, uint8_t d)
{
	cmd_template cmd;
	cmd.command = CMD_LEDBAR_SET_LED_BAR;
	cmd.body.type_1.unit = b;
	cmd.body.type_1.data = d;
	if (HWSIM_execute(&qSock, sizeof(cmd), &cmd) == comm_OK)
		return 0;
	else
		return -1;
}

int get_LED_BAR(uint8_t b, uint8_t* d)
{
	cmd_template cmd;
	rply_template rply;
	static uint16_t seq = 0;

	cmd.command = CMD_LEDBAR_GET_LED_BAR;
	cmd.body.type_2.sequence_num = ++seq;
	cmd.body.type_2.unit = b;
	if (HWSIM_query(&qSock, sizeof(cmd), &cmd, sizeof(rply), &rply) == comm_OK)
	{
		if ((rply.body.type_1.sequence_num == seq) && (rply.command == RPLY_LEDBAR_GET_LED_BAR))
		{
			*d = rply.body.type_1.data;
			return 0;
		}
		else
		{
			return -2;
		}
	}
	else
	{
		return -1;
	}
}


/*
 * 7-SEG MUX SECTION
 */

static commData sSock;

int init_7seg_comm(void)
{
	if (HWSIM_openLink(&sSock, DEV_SEG7_MUX, LOCAL_ADDRESS) == comm_OK)
		return 0;
	else
		return -1;
}

int close_7seg_comm(void)
{
	return HWSIM_closeLink(&sSock);
}

int select_7seg_digit(uint8_t d)
{
	cmd_template cmd;
	cmd.command = CMD_7SEG_SELECT_DIGIT;
	cmd.body.type_3.data = d;
	if (HWSIM_execute(&sSock, sizeof(cmd), &cmd) == comm_OK)
		return 0;
	else
		return -1;
}

int set_7seg_digit(uint8_t d)
{
	cmd_template cmd;
	cmd.command = CMD_7SEG_SET_DIGIT;
	cmd.body.type_3.data = d;
	if (HWSIM_execute(&sSock, sizeof(cmd), &cmd) == comm_OK)
		return 0;
	else
		return -1;
}


/*
 * FULL SERIAL COMM SECTION
 */

static commData serialUpSock[10];
static commData serialDownSock[10];

int init_serial_uplink(uint8_t c)
{
	if (c > 9)
		return -1;
	if (HWSIM_openLink(serialUpSock+c, DEV_SRL_CATCHER+c, LOCAL_ADDRESS) == comm_OK)
		return 0;
	else
		return -1;
}

int close_serial_uplink(uint8_t c)
{
	if (c > 9)
		return -1;
	return HWSIM_closeLink(serialUpSock+c);
}

int init_serial_downlink(uint8_t c)
{
	if (c > 9)
		return -1;
	if (HWSIM_openXtLink(serialDownSock + c, DEV_SRL_SENDER + c, LOCAL_ADDRESS) == comm_OK)
		return 0;
	else
		return -1;
}

int close_serial_downlink(uint8_t c)
{
	if (c > 9)
		return -1;
	return HWSIM_closeLink(serialDownSock + c);
}

int send_serial_character(uint8_t c, uint8_t d)
{
	cmd_template cmd;

	if (c > 9)
		return -1;
	cmd.command = CMD_SEND_CHARACTER;
	cmd.body.type_3.data = d;
	if (HWSIM_execute(serialUpSock+c, sizeof(cmd), &cmd) == comm_OK)
		return 0;
	else
		return -1;
}

int get_serial_character(uint8_t c, uint8_t* d)
{
	cmd_template cmd;
	rply_template rply;
	static uint16_t sqn = 10;

	if (c > 9)
		return -1;
	cmd.command = CMD_GET_CHARACTER;
	cmd.body.type_2.sequence_num = sqn;
	if (HWSIM_query(serialDownSock + c, sizeof(cmd), &cmd, sizeof(rply), &rply) == comm_OK)
	{
		if ((rply.command == RPLY_GET_CHARACTER) && (rply.body.type_1.sequence_num == sqn))
		{
			*d = rply.body.type_1.data;
			sqn++;
			return 0;
		}
	}
	sqn++;
	return -1;
}