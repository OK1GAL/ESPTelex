#include "header.hpp"

BasicOTA OTA;

char sprintf_buf[64];

uint8_t outgoing_payload[256];
uint8_t outgoing_length = 0;
uint8_t incoming_payload[256];
uint8_t incoming_length = 0;

void setup()
{
  DEBUG_UART.begin(DEBUG_UART_BAUDRATE);
  Wire.begin();
  Wire.setTimeOut(20);

  debug_log("TW39 TELEX interface", 1, UART_LOG_MASK);
  debug_log("Initializing IO", 3, UART_LOG_MASK);

  pinMode(TTY_RX_PIN, INPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(DRIVER_ON_PIN, OUTPUT);
  set_driver_power(0);
  pinMode(POL_SWITCH_PIN, OUTPUT);
  pinMode(CURRENT_ALERT_PIN, INPUT);
  pinMode(EXT_UART_NIRQ_PIN, INPUT_PULLUP);
  pinMode(TCA_IRQ, INPUT);
  pinMode(EXT_UART_NRST_PIN, OUTPUT);
  pinMode(SD_DETECT_PIN, INPUT);

  init_current();
  set_polarity(get_idle_polarity());
  delay(100);
  set_driver_power(1);

  initTCA();
  initSDCard();
  init_wifi();
  SD_load_WiFi();
  SD_load_centralex();
  set_polarity(get_idle_polarity());
  init_time();

  init_telnet();

  init_tty();

  OTA.begin();
  esp_task_wdt_init(WATCHDOG_TIMEOUT_s, 1);
  esp_task_wdt_add(NULL);
  ITelex_init();
  sprintf(sprintf_buf, "Boot completed at %d:%d:%d  %d.%d. %d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
  debug_log(sprintf_buf, 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
  setSDCardLED(card_inserted ? SDLED_OK : SDLED_NOK);
  DEBUG_UART.println("Type 'help' for available commands.");
  DEBUG_UART.print("> ");
}

uint64_t last_ping_time = 0;
void loop()
{
  //handle_FSG_signaling_SM();
  handle_ITelex();
  handle_current_sampling();
  handleSDCardStatusCheck();
  handle_tty_phy();
  handle_buttons();
  handle_LEDs();

  if (esp_timer_get_time() > last_ping_time + 1000000)
  {
    update_time();
    esp_task_wdt_reset();
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
    // DEBUG_UART.printf("X-ping\n\r");
    // DEBUG_UART.printf("X-Current telex number: %lu\n\r", get_PD_telex_number());
    // DEBUG_UART.printf("X-Current: %fmA\n\r", get_line_current());
    // DEBUG_UART.printf("X-Current FSG state: %d\n\r", current_FSG_state);
    //sprintf(sprintf_buf, "Timestamp %d:%d:%d  %d.%d. %d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    //debug_log(sprintf_buf, 1, TELNET_LOG_MASK);
    last_ping_time = esp_timer_get_time();
  }
  OTA.handle();
  telnet_handler();
  serial_handler();
}
