#include <Arduino.h>
#include "cmd.h"

volatile unsigned int cmd_vars[MAX_VAR];


COMMAND_STRUCTUR COMMAND_TABELLE[] = 
{
    
	{"res",command_reset}, 
	{"fss",command_factory_settings}, 
	{"apn",command_set_wifi_name},
	{"app",command_set_wifi_pass},
	{"mqa",command_set_mqtt_server_address},
	{"mqu",command_set_mqtt_server_username},
	{"mqp",command_set_mqtt_server_pass},
	{"sta",command_stat},
	{"??",command_help},
	{{00},NULL} 
};

const char helptext[] = {
		"res Restart\r\n"
		"apn Set ap name\r\n"
		"app Set ap pass\r\n"
		"fss Factory settings\r\n"
	    "mqa Mqtt addr\r\n"
	    "mqu Mqtt user\r\n"
	    "mqp Mqtt passwd\r\n"

//		"dd Enable debug output\r\n"
//		"pi Ping\r\n"
		"sta Dump status\r\n"
		"?? HELP\r\n"				
		"\r\n"
};

#define MAX_PARAM_LENGTH 16
static char cmd[2]; 
static char param[MAX_PARAM_LENGTH]; 
static int bufpos = 0;

int readline() {    
	char readch = Serial.read();
    int rpos;
    if (readch > 0) {
        switch (readch) {
            case ' ':
				Serial.print(' ');
				break; // ignore spaces
			case '\n': // ignore newline
                break;
            case '\r': // cr
                rpos = bufpos;
                bufpos = 0;  // Reset position index ready for next time
				Serial.print(readch);
				return rpos;
            default:
				if (bufpos < 2){ //first two chars are the command
					cmd[bufpos++] = readch;
					*param = 0; //set param empty until eventually one comes after the cmd
				}else{
					if (bufpos < MAX_PARAM_LENGTH + 2 - 1) {
						param[bufpos-2] = readch;
						param[bufpos-1] = 0;
						bufpos++;
					}
				}
				Serial.print(readch);
        }
    }
    return 0;
}

//------------------------------------------------------------------------------
//Decode command
unsigned char check_serial_cmd ()
{
	
	if (Serial.available() == 0) 
		return 0;
  	
	if (readline() > 0) {	
		char *string_pointer_tmp;
		unsigned char cmd_index = 0;
		//Serial.println(" ");
		//Kommando in Tabelle suchen
		while(1)
		{
			if (COMMAND_TABELLE[cmd_index].cmd[0] == cmd[0])
			{
				if (COMMAND_TABELLE[cmd_index].cmd[1] == cmd[1])
					break;
			}
			if (COMMAND_TABELLE[++cmd_index].fp == NULL) {
				Serial.println("?");
				return 0;
			}
		}
		
		//Evaluate possible incoming parameters
		if (*param != 0){//posible params comming
			char* pptr = param;
			for (unsigned char a = 0; a<MAX_VAR; a++)
			{ 
				string_pointer_tmp = strsep(&pptr ,"., ");
				cmd_vars[a] = strtol(string_pointer_tmp,NULL,0);
			}
		}else{ //empty param array
			for (unsigned char a = 0; a<MAX_VAR;a++)
			{ 
				cmd_vars[a] = 0;
			}		
		}
		//Exec command
		FLASH_LED(1);
		COMMAND_TABELLE[cmd_index].fp();
		return(1); 
	}
	return 0;
}