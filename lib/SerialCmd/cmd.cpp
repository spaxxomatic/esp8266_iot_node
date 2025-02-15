#include <Arduino.h>

#include "cmd.h"


COMMAND_STRUCTUR COMMAND_TABELLE[] = 
{
    
	{"re",command_reset}, 
	{"fs",command_factory_settings}, 
	{"sp",command_set_conn_params},
	{"st",command_stat},
	{"ec",command_enter_config},	
	{"ce",command_exit_config},
	{"mr",command_mqtt_report},
	{"fu",command_trigger_update},
	{"pi",command_ping},
#ifdef ESP32	
	{"bt",command_ble},
#endif	
	{"??",command_help},
	{{00},NULL} 
};

const char helptext[] = {
		"re Restart\r\n"
		"sp Set conn params (ssid, pwd, mqtt_addr, mqtt_user, mqtt_pass)\r\n"
#ifdef ESP32		
		"bt [on,off] enable/disable bluetooth\r\n"
#endif		
		"fs Factory settings\r\n"
		"ec Enter config\r\n"
		"ce Exit config\r\n"
		"mr Send report via mqtt\r\n"
		"st Dump status\r\n"
		"fu <fw_version> Attempt firmware update \r\n"
		"pi <host> Ping host\r\n"
		"?? HELP\r\n"				
		"\r\n"
};

#define MAX_PARAM_STR_LENGTH 128
  	
static char cmd[2];
static char param[MAX_PARAM_STR_LENGTH]; 
static int bufpos = 0;	

int readline() {    
	char readch = Serial.read();
    int rpos;
    if (readch > 0) {
        switch (readch) {
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
					if (bufpos < MAX_PARAM_STR_LENGTH + 2 - 1) {
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
				Serial.println("\n ??");
				return 0;
			}
		}
		
		//Exec command
		
		COMMAND_TABELLE[cmd_index].fp(param);
		return(1); 
	}

	return 0;
}
	
	 void command_help (char* params){	 
		Serial.println(helptext);
    };

	 void command_factory_settings (char* params){
		Serial.println("TBD");
	 };



	