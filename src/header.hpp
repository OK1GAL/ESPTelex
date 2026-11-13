/*

This project is build with the help from I-Telex wiki and
https://github.com/markjuhrig/ArduTelex/blob/main/ArduTelex/ArduTelex.h


*/

#ifndef __HEADER__
#define __HEADER__

#include <iostream>
#include <stdlib.h>
#include <Arduino.h>
#include "BasicOTA.hpp"
#include "INA226.h"
#include "WiFi.h"
#include "time.h"
#include <Wire.h>
#include <SC16IS752.h>
#include <esp_task_wdt.h>
#include "TCA9555.h"
#include "SPI.h"
#include "SD.h"
#include "FS.h"
#include <ESP32Ping.h>
#include "ESPTelnet.h"

// use esp_timer_get_time() instead of micros()
#define WATCHDOG_TIMEOUT_s 30
extern char sprintf_buf[64];

extern uint8_t outgoing_payload[256];
extern uint8_t outgoing_length;
extern uint8_t incoming_payload[256];
extern uint8_t incoming_length;

struct itelex_subsciber
{
    uint32_t call_number;
    char name[40];
    uint16_t flags;
    uint8_t connection_type;
    char hostname[40];
    uint8_t IP_address[4];
    uint32_t IP_address32;
    uint16_t port;
    uint8_t extension;
    uint16_t pin;
    uint32_t timestamp;
    IPAddress IP;
};

// task timing
#define SDCARD_STATUS_CHECK_PERIOD_us 1000000

// debug
#define UART_LOG_MASK 0b1
#define SD_CARD_LOG_MASK 0b10
#define TELNET_LOG_MASK 0b100

extern uint8_t current_log_level;

void debug_log(const String &s, uint8_t _log_level, uint8_t _log_mask);
void parseDebugCommands(String &s);
void serial_handler();
String uint8ToBinStr(uint8_t _data);

// telnet
#define TELNET_PORT 23
extern ESPTelnet telnet;
void init_telnet();
void telnet_handler();

// IO definitions
#define STATUS_LED_PIN 13
#define DRIVER_ON_PIN 14
#define POL_SWITCH_PIN 27
#define CURRENT_ALERT_PIN 26
#define EXT_UART_NIRQ_PIN 16
#define EXT_UART_NRST_PIN 25
#define TTY_RX_PIN 4
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

#define SD_DETECT_PIN 17
#define SD_CS_PIN 5
#define SD_SCK_PIN 18
#define SD_SDO_PIN 19
#define SD_SDI_PIN 23

#define TCA_ADDRESS 0x20
#define TCA_IRQ 34
#define TCAPINMODE 0b1111000000000000
#define TCA_IDLE_LED 0
#define TCA_DIALING_LED 1
#define TCA_CONNECTED_LED 2
#define TCA_SDCARD_LED_G 3
#define TCA_SDCARD_LED_R 4
#define TCA_SDCARD_LED_B 5
#define TCA_NETWORK_LED_G 6
#define TCA_NETWORK_LED_R 7
#define TCA_NETWORK_LED_B 8
#define TCA_ITELEX_LED_G 9
#define TCA_ITELEX_LED_R 10
#define TCA_ITELEX_LED_B 11
#define TCA_TEST_BTN 14
#define TCA_DISCONNECT_BTN 15

#define SDLED_OFF 0
#define SDLED_OK 10
#define SDLED_NOK 20
#define SDLED_RW 30

#define DEBUG_UART_BAUDRATE 115200
#define DEBUG_UART Serial

extern uint8_t lastSDLEDstate;
extern uint64_t network_activity_until;
extern uint64_t itelex_activity_until;
void notify_network_activity(uint32_t duration_ms = 120);
void notify_itelex_activity(uint32_t duration_ms = 120);

// IO functions
void set_driver_power(int power);
void set_polarity(int polarity);
int get_polarity();
extern bool polarity_inverted;
int get_idle_polarity();
int get_active_polarity();
int get_CL_RX_STATE();
void initTCA();
uint8_t readTCABTN(uint input);
void setTCALED(uint8_t led, uint8_t state);
void setTCA_test_patern();
void handle_LEDs();
void handle_buttons();
void run_selftest(bool allow_tty_print = true);
void setSDCardLED(uint8_t _ledstate);
uint8_t getSDLEDstate();

#define LEDs_HANDLER_INTERVAL 100000

// SDCard
extern uint8_t card_inserted;
void initSDCard();
void handleSDCardStatusCheck();
void SD_load_WiFi();
void SD_load_centralex();
uint8_t SD_load_directory(uint32_t _call_number, itelex_subsciber &_subscriber);

// FSG signaling
/* #define FSG_IDLE 0
#define FSG_DIALING 1
#define FSG_CONNECTED 2
#define FSG_DISCONNECTING 3

extern int current_FSG_state;

void handle_FSG_signaling_SM();
void FSG_idle_handler();
void FSG_dialing_handler();
void FSG_connected_handler();
void FSG_disconnecting_handler(); */

// Pulsed dialing
#define PD_IDLE 0
#define PD_DIALING_DIGIT 1
#define PD_WAITING_FOR_DIGIT 2
#define PD_SAVING 3

#define PD_SAMPLE_DELAY 100000    // us
#define PD_SAMPLE_OFFSET 45000    // us
#define PD_NUMBER_TIMEOUT 3000000 // us
void rotary_dial_handler();
uint32_t get_PD_telex_number();
void clear_PD_telex_number();
void init_rotary_dial_handler();

// Current measurement
#define INA226_ADDRESS 0x40
#define INA226_SHUNT_RESISTOR 0.5 // in ohms

#define CURRENT_STATE_SAMPLE_RATE_us 20000 // 50ms per sample
#define NUM_OF_CURRENT_STATE_SAMPLES 256

extern INA226 INA;
void init_current();
int get_CL_amplitude();
float get_line_current();
float get_raw_line_current();
void handle_current_sampling();
int get_last_1s_current();
int get_last_3s_current();
int fill_last_current(uint8_t value);
bool is_line_break_detected(uint32_t break_threshold_us = 400000);
void reset_line_break_timer();
// WIFI
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET_SEC 3600
#define DAYLIGHT_OFFSET_SEC 3600

extern struct tm timeinfo;

extern String wifiSSID;
extern String wifiPASSWORD;

void init_wifi();
void init_time();
uint8_t wifi_attempt_connection(uint8_t _attempts);
void update_time();

// TTY
#define TTY_BAUDRATE_A 50
#define TTY_BAUDRATE_B 50

#define BAUDOT_MODE_UNDEFINED 0
#define BAUDOT_MODE_BU 31
#define BAUDOT_MODE_ZI 27
const char baudot_bu[] = "#t\ro hnm\nlrgipcvezdbsyfxawj\017uqk\016";
const char baudot_zi[] = "#5\r9 #,.\n)4#80:=3+@?'6#/-2%\01771(\016";
extern char ascii2baudot[130];


// extern SC16IS752 TTY;
extern SC16IS752 TTY;
extern bool tty_is_transmitting;
void init_tty();
void deinit_tty();
void tty_print_ascii(const char *_text);
void tty_wait_until_THR_empty();
void handle_tty_phy();
uint8_t TXFIFO_empty();
uint8_t TXFIFO_available();
uint8_t TTYavailable();
int TTYread();
void tty_write_raw(uint8_t _data);
char baudotToAscii(uint8_t baudot);
void TTYRXflush();
uint8_t flipBaudotOrder(uint8_t _data);
uint8_t RXFIFO_available();
uint8_t TTY_transmitter_empty();
void tty_reset_fifos();
void tty_register_transmitted_echo(uint8_t code);
bool tty_is_echo(uint8_t code);
void tty_clear_echo_queue();

// I-TELEX
#define NAMESERVER_1_HOSTNAME "tlnserv.teleprinter.net"
#define NAMESERVER_2_HOSTNAME "tlnserv2.teleprinter.net"
#define NAMESERVER_3_HOSTNAME "tlnserv3.teleprinter.net"
#define NAMESERVER_PORT 11811
#define NAMESERVER_TIMEOUT_us 1000000

#define ITELEX_PORT 134

#define TCPIP_TIMEOUT 10000000 // us

#define ITELEX_IDLE_STATE 0
#define ITELEX_DIALING_STATE 10
#define ITELEX_CONNECTED_STATE 20
#define ITELEX_CONNECTING_STATE 50
#define CS_START 60    // Start of Version
#define CS_SEND_VER 70 // Send Version 07 01 01
#define CS_REC_VER 80  // Wait for Version from client, Client version should be 07 01 01, goto Send version if not
#define SERVER_WAIT_FOR_DD 85
#define CS_CON_VER 90  // confirm version
#define CS_SEND_DD 100 // send Direct Dial = Durchwahl
#define CS_ABORT 110   // if Version Negitiation was not successful
#define CS_END 120     // finished, set state = STATE_ONLINE
#define ITELEX_BUSY_STATE 130
#define ITELEX_ONLINE_STATE 140
#define ITELEX_LOCAL_STATE 150
#define ITELEX_DISCONNECT_STATE 200

// iTelex Commands, see https://wiki.telexforum.de/index.php?title=Communication_Specification_(i-Telex)#Packet_Types
#define ITLX_HEARTBEAT_CMD 0x00  // heartbeat, length = 0, Ex: 00 00
#define ITLX_DIRECTDIAL_CMD 0x01 // Durchwahl, length = 1, Ex: 01 01 00 = no DW
#define ITLX_BAUDOTDATA_CMD 0x02 // Baudot data, Ex: 02 07 1F 01 10 14 01 02 08 = "test" + CR + LF
#define ITLX_END_CMD 0x03        // End
#define ITLX_REJECT_CMD 0x04     // Reject, Ex: 04 02 6E 61 (na = not allowed)
#define ITLX_ACK_CMD 0x06        // Number of received and processed characters, Ex: 06 01 05
#define ITLX_VERSION_CMD 0x07    // Verion, Ex: 07 05 01 35 32 34 00 (version 1, + "524" = SVN-Rev)
#define ITLX_LOOPBACK_CMD 0x08   // Loopback Test, Ex: 08 02 45 6B
#define ITLX_ASCIIDATA_CMD 0x0B  // ASCII data, Ex: 00B 07 48 65 6C 6C 70 20 21 = "Hello !"  NOT YET SUPPORTED
#define ITLX_MAXCMD_CMD 0x85     // no more command from 0x85 and higher

// remote server commands
#define RITLX_CONNECTREMOTE_CMD 0x81    // TLX>RemSrv 81 06 Own Number (long) PIN (word)
#define RITLX_REMOTECONFIRM_CMD 0x82    // RemSrv>TLX 82 00
#define RITLX_REMOTECALL_CMD 0x83       // RemSrv>TLX 83 00
#define RITLX_REMOTEACCEPTCALL_CMD 0x84 // TLX>RemSrv 84 00

// Centralex configuration and state
struct CentralexConfig
{
    bool enabled;
    String server;
    uint16_t port;
    uint32_t number;
    uint16_t pin;
};

extern CentralexConfig centralex_cfg;

enum CENTRALEX_STATE
{
    CENTRALEX_STATE_DISABLED = 0,
    CENTRALEX_STATE_WAIT_RECONNECT = 1,
    CENTRALEX_STATE_CONNECTING = 2,
    CENTRALEX_STATE_WAIT_CONFIRM = 3,
    CENTRALEX_STATE_STANDBY = 4,
    CENTRALEX_STATE_IN_CALL = 5
};

extern uint8_t centralex_state;
void handle_centralex();
void centralex_disconnect_occupied();
void centralex_trigger_reconnect(uint32_t delay_ms = 5000);
void centralex_reconnect();
String get_centralex_state_str();

extern WiFiClient client;

#define ITLX_NOTCONNECTED_CMD 0xFE
#define ITLX_NOCOMMAND_CMD 0xFF

#define BAUDOT_CHAR_UNKNOWN 0

extern uint8_t iTelexState;
extern bool virtual_terminal_active;

void ITelex_init();
void handle_ITelex();
uint8_t itelex_peer_query(uint32_t _call_number, itelex_subsciber &_subscriber);
bool itelex_dial(uint32_t number, bool is_virtual = false);
void itelex_hangup();
void itelex_force_disconnect();
void itelex_send_ascii(const String &text, bool append_crlf = true);
void itelex_send_wru();
void itelex_send_crlf();
void itelex_send_cr();
void itelex_send_lf();
void itelex_send_bell();
String get_itelex_state_str();

#endif