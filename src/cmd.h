#ifndef _CMD_H_
	#define _CMD_H_	
		
	
	#include <string.h>
	#include <stdlib.h>
	
	typedef struct
	{
		char cmd[4]; 				        
		void(*fp)(void);  	// Zeiger auf Funktion
		char* descr;
	} COMMAND_STRUCTUR;	
	
	#define MAX_VAR	2
	#define HELPTEXT	1
	
	unsigned char check_serial_cmd ();
	extern void write_eeprom_ip (unsigned int);
	
	//reset the unit
	extern void command_reset		(void);
	
	extern void command_help  			(void);
	extern void command_factory_settings (void);
	extern void command_set_wifi_name (void);
	extern void command_set_wifi_pass (void);
	extern void command_set_mqtt_server_address (void);
	extern void command_set_mqtt_server_username  (void);
	extern void command_set_mqtt_server_pass   (void);
	extern void command_stat (void);
	
	
	
#endif //_CMD_H_


