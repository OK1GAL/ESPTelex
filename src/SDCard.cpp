#include "header.hpp"

uint8_t card_inserted = 0;

void initSDCard()
{
    if (!digitalRead(SD_DETECT_PIN)) // SDCard connected
    {
        if (!SD.begin(SD_CS_PIN))
        {
            debug_log("SD card mount failed", 1, UART_LOG_MASK);
            card_inserted = 0;
            setSDCardLED(SDLED_NOK);
            return;
        }
        card_inserted = 1;
        setSDCardLED(SDLED_OK);
    }
    else
    {
        debug_log("SD card not connected", 1, UART_LOG_MASK);
        card_inserted = 0;
        setSDCardLED(SDLED_NOK);
        return;
    }
}

void SD_load_WiFi()
{
    lastSDLEDstate = getSDLEDstate();
    setSDCardLED(SDLED_RW);
    if (SD.exists("/configs/wifi.txt"))
    {
        debug_log("/configs/wifi.txt found", 2, UART_LOG_MASK);
        File wificonfigfile = SD.open("/configs/wifi.txt", FILE_READ);
        while (wificonfigfile.available())
        {
            wifiSSID = wificonfigfile.readStringUntil('\n');
            wifiPASSWORD = wificonfigfile.readStringUntil('\n');
            wifiSSID.trim();
            wifiPASSWORD.trim();
            if (wifiSSID.length() == 0)
            {
                continue;
            }
            debug_log("Attempting to connect to WiFi SSID: " + wifiSSID, 2, UART_LOG_MASK | SD_CARD_LOG_MASK);
            if (wifi_attempt_connection(20))
            {
                break;
            }
        }
        wificonfigfile.close();
    }
    else
    {
        debug_log("/configs/wifi.txt not found", 2, UART_LOG_MASK);
        wifi_attempt_connection(20);
    }
    setSDCardLED(lastSDLEDstate);
}

CentralexConfig centralex_cfg = {
    false,                     // enabled
    "tlnserv2.teleprinter.net",// server (78.47.222.94)
    49491,                     // port
    0,                         // number
    0                          // pin
};

void SD_load_centralex()
{
    lastSDLEDstate = getSDLEDstate();
    setSDCardLED(SDLED_RW);
    if (SD.exists("/configs/centralex.txt"))
    {
        debug_log("/configs/centralex.txt found, loading Centralex config", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        File cfgFile = SD.open("/configs/centralex.txt", FILE_READ);
        while (cfgFile.available())
        {
            String line = cfgFile.readStringUntil('\n');
            line.trim();
            // Ignore empty lines and comments
            if (line.length() == 0 || line.startsWith("#") || line.startsWith(";"))
            {
                continue;
            }
            int eqIdx = line.indexOf('=');
            if (eqIdx > 0)
            {
                String key = line.substring(0, eqIdx);
                String val = line.substring(eqIdx + 1);
                key.trim();
                key.toLowerCase();
                val.trim();

                if (key == "enabled")
                {
                    centralex_cfg.enabled = (val == "1" || val.equalsIgnoreCase("true") || val.equalsIgnoreCase("yes"));
                }
                else if (key == "server" || key == "host" || key == "hostname")
                {
                    if (val.length() > 0)
                    {
                        centralex_cfg.server = val;
                    }
                }
                else if (key == "port")
                {
                    uint32_t p = val.toInt();
                    if (p > 0 && p <= 65535)
                    {
                        centralex_cfg.port = (uint16_t)p;
                    }
                }
                else if (key == "number" || key == "subscriber" || key == "telexnumber")
                {
                    centralex_cfg.number = (uint32_t)strtoul(val.c_str(), NULL, 10);
                }
                else if (key == "pin")
                {
                    centralex_cfg.pin = (uint16_t)val.toInt();
                }
                else if (key == "invert_polarity" || key == "polarity_inverted" || key == "reverse_polarity")
                {
                    polarity_inverted = (val == "1" || val.equalsIgnoreCase("true") || val.equalsIgnoreCase("yes"));
                    debug_log("Config: Polarity inversion set to " + String(polarity_inverted ? "YES" : "NO"), 1, UART_LOG_MASK | TELNET_LOG_MASK);
                }
            }
        }
        cfgFile.close();
        debug_log("Centralex: enabled=" + String(centralex_cfg.enabled ? "YES" : "NO") +
                      " server=" + centralex_cfg.server + ":" + String(centralex_cfg.port) +
                      " number=" + String(centralex_cfg.number) + " pin=" + String(centralex_cfg.pin ? "****" : "0 (unset)"),
                  1, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    else
    {
        debug_log("/configs/centralex.txt not found, Centralex disabled.", 2, UART_LOG_MASK | TELNET_LOG_MASK);
        centralex_cfg.enabled = false;
    }

    // Also check for optional /configs/hardware.txt or /configs/telex.txt for hardware options
    if (SD.exists("/configs/hardware.txt"))
    {
        File hwFile = SD.open("/configs/hardware.txt", FILE_READ);
        while (hwFile.available())
        {
            String line = hwFile.readStringUntil('\n');
            line.trim();
            if (line.length() == 0 || line.startsWith("#") || line.startsWith(";")) continue;
            int eqIdx = line.indexOf('=');
            if (eqIdx > 0)
            {
                String key = line.substring(0, eqIdx);
                String val = line.substring(eqIdx + 1);
                key.trim(); key.toLowerCase(); val.trim();
                if (key == "invert_polarity" || key == "polarity_inverted" || key == "reverse_polarity")
                {
                    polarity_inverted = (val == "1" || val.equalsIgnoreCase("true") || val.equalsIgnoreCase("yes"));
                    debug_log("Hardware config: Polarity inversion set to " + String(polarity_inverted ? "YES" : "NO"), 1, UART_LOG_MASK | TELNET_LOG_MASK);
                }
            }
        }
        hwFile.close();
    }
    setSDCardLED(lastSDLEDstate);
}

uint64_t lastSdcardChecktime = 0;
void handleSDCardStatusCheck()
{
    if (esp_timer_get_time() > lastSdcardChecktime + SDCARD_STATUS_CHECK_PERIOD_us)
    {
        if (!digitalRead(SD_DETECT_PIN))
        {
            if (card_inserted == 0)
            {
                if (!SD.begin(SD_CS_PIN))
                {
                    debug_log("SD card not connected", 1, UART_LOG_MASK);
                    setSDCardLED(SDLED_NOK);
                    card_inserted = 0;
                }
                else
                {
                    setSDCardLED(SDLED_OK);
                    card_inserted = 1;
                    SD_load_centralex();
                }
            }
        }
        if (digitalRead(SD_DETECT_PIN))
        {
            if (card_inserted == 1)
            {
                SD.end();
            }
            setSDCardLED(SDLED_NOK);
            card_inserted = 0;
        }

        lastSdcardChecktime = esp_timer_get_time();
    }
}

// Loads subscriber info from SD card
// Returns:
// 0x01 loaded successfully
// 0x10 if SD is disconnected
// 0x20 if subscriber file not found
//
uint8_t SD_load_directory(uint32_t _call_number, itelex_subsciber &_subscriber)
{
    lastSDLEDstate = getSDLEDstate();
    setSDCardLED(SDLED_RW);
    String path = "/telexdirectory/" + String(_call_number) + ".txt";
    debug_log(path, 2, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
    if (!card_inserted)
    {
        setSDCardLED(lastSDLEDstate);
        return 0x10; // SD card not inserted
    }
    else if (!SD.exists(path))
    {
        setSDCardLED(lastSDLEDstate);
        return 0x20;
    }
    else
    {
        memset(&_subscriber, 0, sizeof(itelex_subsciber));
        File subscriberFile = SD.open(path, FILE_READ);
        if (subscriberFile.available())
        {
            _subscriber.call_number = subscriberFile.readStringUntil('\n').toInt();
            int nameLen = subscriberFile.readBytesUntil('\n', _subscriber.name, 39);
            while (nameLen > 0 && (_subscriber.name[nameLen - 1] == '\r' || _subscriber.name[nameLen - 1] == ' ' || _subscriber.name[nameLen - 1] == '\t'))
            {
                nameLen--;
            }
            _subscriber.name[nameLen] = 0;

            _subscriber.flags = subscriberFile.readStringUntil('\n').toInt();
            _subscriber.connection_type = subscriberFile.readStringUntil('\n').toInt();

            int hostLen = subscriberFile.readBytesUntil('\n', _subscriber.hostname, 39);
            while (hostLen > 0 && (_subscriber.hostname[hostLen - 1] == '\r' || _subscriber.hostname[hostLen - 1] == ' ' || _subscriber.hostname[hostLen - 1] == '\t'))
            {
                hostLen--;
            }
            _subscriber.hostname[hostLen] = 0;
            _subscriber.IP_address[0] = subscriberFile.readStringUntil('.').toInt();
            _subscriber.IP_address[1] = subscriberFile.readStringUntil('.').toInt();
            _subscriber.IP_address[2] = subscriberFile.readStringUntil('.').toInt();
            _subscriber.IP_address[3] = subscriberFile.readStringUntil('\n').toInt();
            _subscriber.port = subscriberFile.readStringUntil('\n').toInt();
            _subscriber.extension = subscriberFile.readStringUntil('\n').toInt();
            _subscriber.pin = subscriberFile.readStringUntil('\n').toInt();
            _subscriber.timestamp = subscriberFile.readStringUntil('\n').toInt();
        }
        _subscriber.IP_address32 = uint32_t(_subscriber.IP_address[3]) | (uint32_t(_subscriber.IP_address[2]) << 8) | (uint32_t(_subscriber.IP_address[1]) << 16) | (uint32_t(_subscriber.IP_address[0]) << 24);
        _subscriber.IP = IPAddress(_subscriber.IP_address[0], _subscriber.IP_address[1], _subscriber.IP_address[2], _subscriber.IP_address[3]);
        subscriberFile.close();
    }
    setSDCardLED(lastSDLEDstate);
    return 1;
}