/******************************************************************************
 * Copyright 2013-2014 Espressif Systems (Wuxi)
 *
 * FileName: user_main.c
 *
 * Description: entry file of user application
 *
 * Modification history:
 *     2014/1/1, v1.0 create this file.
*******************************************************************************/
#include "ets_sys.h"
#include "osapi.h"

#include "user_interface.h"
#include "user_config.h"

#include "user_devicefind.h"
#include "user_webserver.h"
#include "c_types.h"
#include "espconn.h"
#include "../../ThirdParty/include/lwipopts.h"

#include "driver/uart.h"

#include <os_type.h>
#include <gpio.h>
#include "driver/i2c.h"
#include "driver/i2c_bmp180.h"
#include "driver/dht22.h"
#include "driver/gpio16.h"
#include "driver/ds18b20.h"

//#define DELAY 2000 /* milliseconds */
#define sleepms(x) os_delay_us(x*1000);

#if ESP_PLATFORM
#include "user_esp_platform.h"
#endif

#ifdef SERVER_SSL_ENABLE
#include "ssl/cert.h"
#include "ssl/private_key.h"
#else
#ifdef CLIENT_SSL_ENABLE
unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;
#endif
#endif
#define DELAY 1000 /* milliseconds */

LOCAL os_timer_t loop_timer;
extern int ets_uart_printf(const char *fmt, ...);
extern void ets_wdt_enable (void);
extern void ets_wdt_disable (void);
//extern void wdt_feed (void);


typedef enum {
	WIFI_CONNECTING,
	WIFI_CONNECTING_ERROR,
	WIFI_CONNECTED,
	TCP_DISCONNECTED,
	TCP_CONNECTING,
	TCP_CONNECTING_ERROR,
	TCP_CONNECTED,
	TCP_SENDING_DATA_ERROR,
	TCP_SENT_DATA
} tConnState;

struct espconn Conn;
esp_tcp ConnTcp;
extern int ets_uart_printf(const char *fmt, ...);
int (*console_printf)(const char *fmt, ...) = ets_uart_printf;
//mDNS
static char szH[40],szS[10],szN[30];
struct mdns_info thismdns;

static ETSTimer WiFiLinker;
static tConnState connState = WIFI_CONNECTING;
//static unsigned char tcpReconCount;

LOCAL os_timer_t loop_timer;
LOCAL nTcnt=0;
//Sensor Values & System Metrics
DATA_Sensors mySensors;
DATA_System sysParams;

DHT_Sensor DHsensor;

extern uint8 device_recon_count;
extern struct espconn user_conn;

int bmppresent = 0;

//static void platform_reconnect(struct espconn *);
static void wifi_check_ip(void *arg);
LOCAL void ICACHE_FLASH_ATTR loop_cb(void *arg);

const char *WiFiMode[] =
{
		"NULL",		// 0x00
		"STATION",	// 0x01
		"SOFTAP", 	// 0x02
		"STATIONAP"	// 0x03
};

#ifdef PLATFORM_DEBUG
// enum espconn state, see file /include/lwip/api/err.c
const char *sEspconnErr[] =
{
		"Ok",                    // ERR_OK          0
		"Out of memory error",   // ERR_MEM        -1
		"Buffer error",          // ERR_BUF        -2
		"Timeout",               // ERR_TIMEOUT    -3
		"Routing problem",       // ERR_RTE        -4
		"Operation in progress", // ERR_INPROGRESS -5
		"Illegal value",         // ERR_VAL        -6
		"Operation would block", // ERR_WOULDBLOCK -7
		"Connection aborted",    // ERR_ABRT       -8
		"Connection reset",      // ERR_RST        -9
		"Connection closed",     // ERR_CLSD       -10
		"Not connected",         // ERR_CONN       -11
		"Illegal argument",      // ERR_ARG        -12
		"Address i"
		"Error in   use",        // ERR_USE        -13
		"Low-level netif error", // ERR_IF         -14
		"Already connected"      // ERR_ISCONN     -15
};
#endif

#define DEGREES_C 0
#define DEGREES_F 1

static void ICACHE_FLASH_ATTR InitializemDNS(struct ip_info ipConfig) {
	//Initialize mDNS Responder
	strcpy(szH,DNS_SVR);
	thismdns.host_name = &szH[0];
	strcpy(szS,DNS_SVR_NAME);
	thismdns.server_name = szS;
	thismdns.server_port =9703;
	strcpy(szN,DNS_TXTDATA);
	thismdns.txt_data[0] = szN;
	thismdns.ipAddr = ipConfig.ip.addr;
	espconn_mdns_init(&thismdns);
}

static void ICACHE_FLASH_ATTR wifi_check_ip(void *arg)
{
	struct ip_info ipConfig;
	os_timer_disarm(&WiFiLinker);
	switch(wifi_station_get_connect_status())
	{
		case STATION_GOT_IP:
			wifi_get_ip_info(STATION_IF, &ipConfig);
			if(ipConfig.ip.addr != 0) {
				connState = WIFI_CONNECTED;
				#ifdef PLATFORM_DEBUG
				ets_uart_printf("WiFi connected\r\n");
				ets_uart_printf("IP: %08X\r\n",ipConfig.ip.addr);
				//Initialize mDNS Responder
				InitializemDNS(ipConfig);

				#endif
				connState = TCP_CONNECTING;
				//startwebserver
				sleepms(1000);
				user_webserver_init(SERVER_PORT);
				os_timer_disarm(&loop_timer);
				os_timer_setfn(&loop_timer, (os_timer_func_t *)loop_cb, (void *)0);
				os_timer_arm(&loop_timer, DELAY, 10);

				////senddata();
				return;
			}
			break;
		case STATION_WRONG_PASSWORD:
			connState = WIFI_CONNECTING_ERROR;
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting error, wrong password\r\n");
			#endif
			break;
		case STATION_NO_AP_FOUND:
			connState = WIFI_CONNECTING_ERROR;
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting error, ap not found\r\n");
			#endif
			break;
		case STATION_CONNECT_FAIL:
			connState = WIFI_CONNECTING_ERROR;
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting fail\r\n");
			#endif
			break;
		default:
			connState = WIFI_CONNECTING;
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("WiFi connecting...\r\n");
			#endif
	}
	os_timer_setfn(&WiFiLinker, (os_timer_func_t *)wifi_check_ip, NULL);
	os_timer_arm(&WiFiLinker, 1000, 0);
}
///////////////////////////////////////////////////////////////////////
// Callback when External Station Connects or Disconnects
///////////////////////////////////////////////////////////////////////
void wifi_event_cb(System_Event_t *evt) {
	struct ip_info ipConfig;
	static int serverinit=0;
	switch (evt->event) {
		case EVENT_SOFTAPMODE_STACONNECTED:
			ets_uart_printf("station: " MACSTR " join, AID = %d\n",
			MAC2STR(evt->event_info.sta_connected.mac),
			evt->event_info.sta_connected.aid);
			//Start Web Server (Upon first connection)
			if(!serverinit) {
				user_webserver_init(SERVER_PORT);
				serverinit=1;
			}
			//Start periodic loop
			os_timer_disarm(&loop_timer);
			os_timer_setfn(&loop_timer, (os_timer_func_t *)loop_cb, (void *)0);
			os_timer_arm(&loop_timer, DELAY, 10);
			break;
		case EVENT_SOFTAPMODE_STADISCONNECTED:
			ets_uart_printf("station: " MACSTR " leave, AID = %d\n",
			MAC2STR(evt->event_info.sta_disconnected.mac),
			evt->event_info.sta_disconnected.aid);
			//Stop Web Server
			//???;
			//Stop periodic loop
			os_timer_disarm(&loop_timer);
			break;
		default:
			break;
	}
}

void setup_wifi_ap_mode(void)
{
	char ssid[33];
	char password[33];

	wifi_set_opmode(SOFTAP_MODE);
	wifi_softap_dhcps_stop();

	struct softap_config apconfig;
	if(wifi_softap_get_config(&apconfig))
	{
		os_memset(apconfig.ssid, 0, sizeof(apconfig.ssid));
		os_memset(apconfig.password, 0, sizeof(apconfig.password));
		os_sprintf((char *) ssid, "%s", WIFI_AP_NAME);
		os_memcpy((char *) apconfig.ssid, ssid, os_strlen(ssid));
		os_sprintf(password, "%s", WIFI_AP_PASSWORD);
		os_memcpy(apconfig.password, password, os_strlen(password));
		apconfig.authmode = AUTH_OPEN;
		apconfig.ssid_hidden = 0;
		apconfig.max_connection = 4;
		//apconfig.channel=7;
		if(!wifi_softap_set_config(&apconfig))
		{
			#ifdef PLATFORM_DEBUG
			ets_uart_printf("ESP8266 not set ap config!\r\n");
			#endif
		}
	}
    //Set WiFi event callback
	wifi_set_event_handler_cb(wifi_event_cb);

    LOCAL struct ip_info info;
    IP4_ADDR(&info.ip, 192, 168, 22, 1);
    IP4_ADDR(&info.gw, 192, 168, 22, 1);
    IP4_ADDR(&info.netmask, 255, 255, 255, 0);
    wifi_set_ip_info(SOFTAP_IF, &info);

    struct dhcps_lease dhcp_lease;
    IP4_ADDR(&dhcp_lease.start_ip, 192, 168, 22, 2);
    IP4_ADDR(&dhcp_lease.end_ip, 192, 168, 22, 5);
    wifi_softap_set_dhcps_lease(&dhcp_lease);

    wifi_softap_dhcps_start();

    //Startup Complete Status
    ets_uart_printf("SOFTAP Status:%d\r\n",wifi_softap_dhcps_status());
    ets_uart_printf("Size of ESP8266N4: %d\r\n",sizeof(apconfig.ssid));
    ets_uart_printf("Leng of ESP8266N4: %d\r\n",os_strlen(apconfig.ssid));

}


extern void get_temp_ds18b20(int sensor, int units, char * temp)
{
	int r, i;
	uint8_t addr[9], data[12];
	ds_init();
	switch(sensor) {
		case 1:
			//Sensor 1 (Replace with unique 8-byte ds18b20 Address)
			addr[0]=0x10;
			addr[1]=0x2E;
			addr[2]=0x4B;
			addr[3]=0x2F;
			addr[4]=0x00;
			addr[5]=0x08;
			addr[6]=0x00;
			addr[7]=0x5B;
			break;
		case 2:
			//Sensor 2 (Replace with unique 8-byte ds18b20 Address)
			addr[0]=0x10;
			addr[1]=0x13;
			addr[2]=0x45;
			addr[3]=0x2F;
			addr[4]=0x00;
			addr[5]=0x08;
			addr[6]=0x00;
			addr[7]=0x5E;
			break;
		default:
		    //No Sensor Address set
			break;
	}
	// perform the conversion
	ds_reset();
	select(addr);

	ds_write(DS1820_CONVERT_T, 1); // perform temperature conversion

	sleepms(1000); // sleep 1s

	ds_reset();
	select(addr);
	ds_write(DS1820_READ_SCRATCHPAD, 0); // read scratchpad

	for(i = 0; i < 9; i++)
	{
		data[i] = ds_read();
	}
	ds_reset();

	int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract, WholeF, FractF;
	LowByte = data[0];
	HighByte = data[1];
	TReading = (HighByte << 8) + LowByte;
	SignBit = TReading & 0x8000;  // test most sig bit
	if (SignBit) // negative
		TReading = (TReading ^ 0xffff) + 1; // 2's comp

	//Celsius
	Whole = (( ( (int)((data[1]<<8) | data[0]) * 0.5) + (data[6]/16) )*100) -25;
	Fract = Whole%100;
	Whole = Whole/100;

	//Fahrenheit
	WholeF = ((SignBit==0) ? 1 : -1) * ((Whole * 9)/5) + 32;
	if(WholeF>0) {
		SignBit=0;
	}
	FractF = ((Fract * 9)/5);
	if(units==DEGREES_C) {
		os_sprintf(temp,"%c%d.%d", SignBit ? '-' : '+', Whole, Fract < 10 ? 0 : Fract < 100 ? Fract/10 : Fract/100);
	}
	else {
		os_sprintf(temp,"%c%d.%d", SignBit ? '-' : '+', WholeF, FractF < 10 ? 0 : FractF < 100 ? FractF/10 : FractF/100);
	}
}

LOCAL void ICACHE_FLASH_ATTR loop_cb(void *arg)
{
	char szT[32];

	DHT_Sensor_Data data;

    int32_t temperature;
    int32_t pressure;

    //Read Sensors & copy values here
    // Change nTcnt%2 to nTcnt%3 for 3 sensors...
	switch(nTcnt%2) {
		case 0:
			if(DHTRead(&DHsensor, &data)) {
			    DHTFloat2String(mySensors.tDht11, ((9/5) * data.temperature)+32);
			    DHTFloat2String(mySensors.hDht11, data.humidity);
			}
			else {
				os_sprintf(mySensors.tDht11,"%s","78.3");
				os_sprintf(mySensors.hDht11,"%s","39");
			}
			break;
		case 1:
			if(bmppresent) {
				temperature = BMP180_GetTemperature();
				pressure = BMP180_GetPressure(OSS_0);
				os_sprintf(mySensors.pBmp085,"%ld.%02d", pressure/3386,((pressure%3386)*100)/3386);
				os_sprintf(mySensors.tBmp085,"%ld.%01d", ((temperature*18)/100) + 32,(temperature*18)%100);
			}
			else {
				os_sprintf(mySensors.pBmp085,"%s","29.7");
				os_sprintf(mySensors.tBmp085,"%s","71.1");
			}
			os_sprintf(mySensors.aBmp085,"%s","555.0");
			break;
		case 2: //Placeholder for a ds18b20 temperature sensor #1 (Not called in this example)
			get_temp_ds18b20(1,DEGREES_F,mySensors.t1Ds18b20);
			break;
		case 3: //Placeholder for a ds18b20 temperature sensor #2 (Not called in this example)
			get_temp_ds18b20(2,DEGREES_F,mySensors.t2Ds18b20);
			break;
		default:
			break;
	}


	//Get System Parameters
	os_sprintf(sysParams.systime,"%d",system_get_time()/1000000);
	os_sprintf(sysParams.freeheap,"%d",system_get_free_heap_size());
	os_sprintf(sysParams.loopcnt,"%d",nTcnt);
	os_sprintf(sysParams.wifistatus,"%d",wifi_station_get_connect_status());
	os_sprintf(sysParams.wifireconnects,"%d",device_recon_count);
	os_sprintf(sysParams.wifimode,"%d",wifi_get_opmode());

	LOCAL remot_info *pcon_info;
	espconn_get_connection_info(&user_conn, &pcon_info,0);

	ets_uart_printf("Iteration: %d k.k.(5): %s Heap: %s WIFI:%s recon:%d\n",nTcnt++,
			sysParams.wifistatus,
			sysParams.freeheap,
			sysParams.wifimode,device_recon_count);
}

void user_rf_pre_init(void)
{
	system_phy_set_rfoption(2);
}

/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
*******************************************************************************/
void user_init(void)
{
	os_delay_us(2000000); //wait 2 sec
	ets_wdt_enable();
	ets_wdt_disable();

	user_rf_pre_init();

	// Configure the UART
	uart_init(BIT_RATE_115200, BIT_RATE_115200);

	gpio16_output_conf();
	gpio16_output_set(0);

	//Initialize Barometric/Temperature Sensor
	bmppresent = BMP180_Init();

	//Initialize Humidity/Temperature Sensor
	DHsensor.pin = 1;  //GPIO5
	DHsensor.type = DHT22;
	DHTInit(&DHsensor);

	//Initialize Wifi in AP Mode (Waiting for connection)
	setup_wifi_ap_mode();


#if ESP_PLATFORM
    //user_esp_platform_init();
#endif

    //user_devicefind_init();
#ifdef SERVER_SSL_ENABLE
    user_webserver_init(SERVER_SSL_PORT);
#else
    //user_webserver_init(SERVER_PORT);
#endif


}

