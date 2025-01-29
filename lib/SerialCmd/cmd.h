#ifndef _CMD_H_
	#define _CMD_H_	
	
	#include <string.h>
	#include <stdlib.h>
	
	typedef struct
	{
		char cmd[3]; 				        
		void(*fp)(char*);  	// Zeiger auf Funktion
		char* descr;
	} COMMAND_STRUCTUR;	

	#define HELPTEXT	1
	
	extern unsigned char check_serial_cmd ();
	
	//reset the unit
	extern void command_reset		(char*);	
	extern void command_help  			(char*);
	extern void command_factory_settings (char*);
	extern void command_set_conn_params (char*);
	extern void command_stat (char*);
	extern void command_enter_config (char*);
	extern void command_trigger_update(char*);
	#ifdef ESP32
	extern void command_ble(char*);
	#endif
	
#endif //_CMD_H_


