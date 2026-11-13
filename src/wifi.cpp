#include "header.hpp"

struct tm timeinfo;

String wifiSSID = WIFI_SSID;
String wifiPASSWORD = WIFI_PASSWORD;

// returns 1 if successfull, otherwise 0
// blocking, each attempt takes 500ms
uint8_t wifi_attempt_connection(uint8_t _attempts)
{
    WiFi.begin(wifiSSID, wifiPASSWORD);
    debug_log("Attempting to connect to wifi.", 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
    for (int i = 0; i < _attempts; i++)
    {
        if (WiFi.status() == WL_CONNECTED)
        {
            debug_log("Attempt " + String(i) + "/" + String(_attempts) + " successfull.", 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
            debug_log("IPaddress: " + String(WiFi.localIP().toString()), 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
            return 1;
        }
        else
        {
            debug_log("Attempt " + String(i) + "/" + String(_attempts) + " unsuccessfull.", 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
        }
        delay(500);
    }
    return 0;
}

void init_wifi()
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname("ESPI-Telex");
}

void init_time()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        if (!getLocalTime(&timeinfo, 3000))
        {
            debug_log("Failed to obtain time.", 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
            return;
        }
        sprintf(sprintf_buf, "-- %d:%d:%d  %d.%d. %d --", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        debug_log(sprintf_buf, 1, UART_LOG_MASK | SD_CARD_LOG_MASK);
    }
}

void update_time()
{
    time_t now;
    time(&now);
    localtime_r(&now, &timeinfo);
}