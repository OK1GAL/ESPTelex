#include "header.hpp"

uint8_t current_log_level = 3;

// log mask bits
// 0 UART
// 1 SD card
// 2 Telnet
// log levels:
// 0 no log
// 1 basic log
// 2 detailed log
// 3 log everything
void debug_log(const String &s, uint8_t _log_level, uint8_t _log_mask)
{
    if (_log_level > current_log_level)
    {
        return;
    }

    if ((_log_mask & UART_LOG_MASK) != 0)
    {
        DEBUG_UART.println(s);
    }
    if (((_log_mask & SD_CARD_LOG_MASK) != 0) && card_inserted)
    {
        File logfile = SD.open("/logs/debuglog.txt", FILE_APPEND);
        lastSDLEDstate = getSDLEDstate();
        setSDCardLED(SDLED_RW);
        if (logfile)
        {
            logfile.println(String(timeinfo.tm_hour) + ":" + String(timeinfo.tm_min) + ":" + String(timeinfo.tm_sec) + "  " + s);
            logfile.close();
        }
        else
        {
            debug_log("Log file not found, log not writen.", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        }
        setSDCardLED(lastSDLEDstate);
    }
    if (((_log_mask & TELNET_LOG_MASK) != 0) && telnet.isConnected())
    {
        telnet.println(s);
    }
}

// ==========================================
// Command Dispatch System
// ==========================================

typedef void (*CommandHandler)(const String &args);

struct DebugCommand
{
    const char *name;
    CommandHandler handler;
    const char *help;
};

// Forward declaration of handlers
void cmd_help(const String &args);
void cmd_dial(const String &args);
void cmd_call(const String &args);
void cmd_msg(const String &args);
void cmd_raw(const String &args);
void cmd_wru(const String &args);
void cmd_crlf(const String &args);
void cmd_cr(const String &args);
void cmd_lf(const String &args);
void cmd_bell(const String &args);
void cmd_hangup(const String &args);
void cmd_status(const String &args);
void cmd_query(const String &args);
void cmd_tx(const String &args);
void cmd_verbosity(const String &args);
void cmd_time(const String &args);
void cmd_reboot(const String &args);
void cmd_centralex(const String &args);
void cmd_polarity(const String &args);
void cmd_invert_polarity(const String &args);
void cmd_driver(const String &args);
void cmd_line(const String &args);
void cmd_selftest(const String &args);
void cmd_forcedisc(const String &args);

// Easily expandable command registry
const DebugCommand commandTable[] = {
    {"help", cmd_help, "Displays this help menu"},
    {"call", cmd_call, "Dial peer using physical TTY: call <number>"},
    {"dial", cmd_dial, "Dial peer in virtual terminal mode: dial <number>"},
    {"msg", cmd_msg, "Send ASCII text + CRLF (escapes: \\w=WRU, \\r, \\n, \\b=Bell): msg <text>"},
    {"raw", cmd_raw, "Send raw text without auto-CRLF (supports \\w, \\r, \\n): raw <text>"},
    {"wru", cmd_wru, "Send WRU (Who are you? / Figures-D / Wer da?)"},
    {"crlf", cmd_crlf, "Send Carriage Return + Line Feed (CR LF)"},
    {"cr", cmd_cr, "Send Carriage Return (CR)"},
    {"lf", cmd_lf, "Send Line Feed (LF)"},
    {"bell", cmd_bell, "Send Bell signal (Figures-J / Klingel)"},
    {"hangup", cmd_hangup, "Disconnect active i-Telex session"},
    {"disc", cmd_hangup, "Alias for hangup"},
    {"forcedisc", cmd_forcedisc, "Emergency force disconnect & loop power reset"},
    {"selftest", cmd_selftest, "Run hardware/network/line self-test (selftest [notty])"},
    {"test", cmd_selftest, "Alias for selftest"},
    {"pol", cmd_polarity, "Set/toggle polarity: pol [0|1]"},
    {"polarity", cmd_polarity, "Alias for pol"},
    {"invert-polarity", cmd_invert_polarity, "Toggle/set polarity inversion: invert-polarity [0|1]"},
    {"pwr", cmd_driver, "Toggle/set line driver power: pwr [0|1]"},
    {"line", cmd_line, "Show real-time TW39 loop current and pin status"},
    {"loop", cmd_line, "Alias for line"},
    {"status", cmd_status, "Show system, connection and line status"},
    {"centralex", cmd_centralex, "Show Centralex status or control: centralex [reconnect|disconnect]"},
    {"query", cmd_query, "Lookup subscriber info: query <number>"},
    {"tty-dial", cmd_query, "Alias for query <number>"},
    {"tx", cmd_tx, "Print text to physical TTY: tx <text>"},
    {"tty-txtext", cmd_tx, "Alias for tx <text>"},
    {"set-verbosity", cmd_verbosity, "Set log verbosity: set-verbosity <0-3>"},
    {"set-verboxity", cmd_verbosity, "Alias for set-verbosity"},
    {"time", cmd_time, "Display current RTC / NTP time"},
    {"reboot", cmd_reboot, "Reboot the ESP32"}};

void cmd_help(const String &args)
{
    debug_log("=== ESPTelex Command Menu ===", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    for (size_t i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); i++)
    {
        String nameStr = commandTable[i].name;
        while (nameStr.length() < 16)
        {
            nameStr += " ";
        }
        debug_log(nameStr + "- " + commandTable[i].help, 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    debug_log("Escape sequences in msg/raw: \\w = WRU, \\r = CR, \\n = LF, \\b = Bell, \\\\ = \\", 0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void cmd_dial(const String &args)
{
    if (args.length() == 0)
    {
        debug_log("Usage: dial <number> (e.g. dial 11111 or dial 601)", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    uint32_t num = args.toInt();
    itelex_dial(num, true);
}

void cmd_call(const String &args)
{
    if (args.length() == 0)
    {
        debug_log("Usage: call <number> (e.g. call 600 or call 68)", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    uint32_t num = args.toInt();
    debug_log("Placing call to " + String(num) + " using physical TTY...", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    itelex_dial(num, false);
}

void cmd_msg(const String &args)
{
    if (args.length() == 0)
    {
        debug_log("Usage: msg <text to send>", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    itelex_send_ascii(args, true);
}

void cmd_raw(const String &args)
{
    if (args.length() == 0)
    {
        debug_log("Usage: raw <text to send without trailing CRLF>", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    itelex_send_ascii(args, false);
}

void cmd_wru(const String &args)
{
    itelex_send_wru();
}

void cmd_crlf(const String &args)
{
    itelex_send_crlf();
}

void cmd_cr(const String &args)
{
    itelex_send_cr();
}

void cmd_lf(const String &args)
{
    itelex_send_lf();
}

void cmd_bell(const String &args)
{
    itelex_send_bell();
}

void cmd_hangup(const String &args)
{
    itelex_hangup();
}

void cmd_forcedisc(const String &args)
{
    itelex_force_disconnect();
}

void cmd_selftest(const String &args)
{
    bool allow_tty = !(args.equalsIgnoreCase("notty") || args.equalsIgnoreCase("silent") || args == "0");
    run_selftest(allow_tty);
}

void cmd_status(const String &args)
{
    debug_log("=== ESPTelex System Status ===", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("State: " + get_itelex_state_str(), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("Virtual Terminal: " + String(virtual_terminal_active ? "ACTIVE" : "INACTIVE"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("Line Current: " + String(get_line_current(), 2) + " mA (raw signed: " + String(get_raw_line_current(), 2) + " mA)", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("Polarity: " + String(get_polarity() ? "NORMAL (1)" : "REVERSED (0)") +
                  (get_polarity() == get_idle_polarity() ? " [IDLE]" : " [ACTIVE]") +
                  (polarity_inverted ? " (Inverted Mode)" : ""),
              0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("TTY_RX Pin (GPIO 4): " + String(digitalRead(TTY_RX_PIN) ? "HIGH (Mark/Closed loop)" : "LOW (Space/Open loop)"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("Driver Pin (GPIO 14): " + String(digitalRead(DRIVER_ON_PIN) ? "ON (1)" : "OFF (0)"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    if (WiFi.status() == WL_CONNECTED)
    {
        debug_log("WiFi: Connected (" + WiFi.SSID() + ") IP: " + WiFi.localIP().toString() + " RSSI: " + String(WiFi.RSSI()) + "dBm", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    else
    {
        debug_log("WiFi: Disconnected", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    debug_log("SD Card: " + String(card_inserted ? "INSERTED / READY" : "NOT INSERTED"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    if (centralex_cfg.enabled)
    {
        debug_log("Centralex: ENABLED (" + get_centralex_state_str() + ") Server: " + centralex_cfg.server + ":" + String(centralex_cfg.port) + " No: " + String(centralex_cfg.number), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    else
    {
        debug_log("Centralex: DISABLED (Direct IP mode)", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    sprintf(sprintf_buf, "Time: %02d:%02d:%02d %02d.%02d.%04d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    debug_log(String(sprintf_buf), 0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void cmd_polarity(const String &args)
{
    if (args.length() > 0)
    {
        int p = args.toInt();
        set_polarity(p ? 1 : 0);
    }
    else
    {
        set_polarity(!get_polarity());
    }
    delay(50);
    debug_log("Polarity set to: " + String(get_polarity() ? "NORMAL (1)" : "REVERSED (0)") +
                  " (Idle=" + String(get_idle_polarity()) + ", Active=" + String(get_active_polarity()) + ")" +
                  " | Current: " + String(get_line_current(), 2) + " mA (raw signed: " + String(get_raw_line_current(), 2) + " mA)" +
                  " | TTY_RX pin: " + String(digitalRead(TTY_RX_PIN)),
              0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void cmd_invert_polarity(const String &args)
{
    if (args.length() > 0)
    {
        polarity_inverted = (args == "1" || args.equalsIgnoreCase("true") || args.equalsIgnoreCase("yes"));
    }
    else
    {
        polarity_inverted = !polarity_inverted;
    }
    if (iTelexState == ITELEX_IDLE_STATE)
    {
        set_polarity(get_idle_polarity());
    }
    debug_log("Polarity inversion: " + String(polarity_inverted ? "ENABLED" : "DISABLED") +
                  " (Idle pol=" + String(get_idle_polarity()) + ", Active pol=" + String(get_active_polarity()) + ")",
              0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void cmd_driver(const String &args)
{
    if (args.length() > 0)
    {
        int p = args.toInt();
        set_driver_power(p ? 1 : 0);
    }
    else
    {
        set_driver_power(!digitalRead(DRIVER_ON_PIN));
    }
    delay(20);
    debug_log("Driver power set to: " + String(digitalRead(DRIVER_ON_PIN) ? "ON (1)" : "OFF (0)") +
                  " | Current: " + String(get_line_current(), 2) + " mA (raw signed: " + String(get_raw_line_current(), 2) + " mA)",
              0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void cmd_line(const String &args)
{
    debug_log("=== TW39 Current Loop Status ===", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("  Current: " + String(get_line_current(), 2) + " mA (raw signed: " + String(get_raw_line_current(), 2) + " mA)", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("  Polarity: " + String(get_polarity() ? "NORMAL (1)" : "REVERSED (0)") +
                  (get_polarity() == get_idle_polarity() ? " [IDLE]" : " [ACTIVE]"),
              0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("  Polarity Inverted Mode: " + String(polarity_inverted ? "YES" : "NO"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("  TTY_RX pin (GPIO 4): " + String(digitalRead(TTY_RX_PIN) ? "HIGH (Mark / Closed loop)" : "LOW (Space / Open loop)"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("  Driver Pin (GPIO 14): " + String(digitalRead(DRIVER_ON_PIN) ? "ON (1)" : "OFF (0)"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    debug_log("  State: " + get_itelex_state_str(), 0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void cmd_centralex(const String &args)
{
    if (args.equalsIgnoreCase("reconnect") || args.equalsIgnoreCase("restart"))
    {
        debug_log("Reloading Centralex config and reconnecting...", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        SD_load_centralex();
        centralex_reconnect();
    }
    else if (args.equalsIgnoreCase("disconnect") || args.equalsIgnoreCase("stop"))
    {
        debug_log("Disconnecting Centralex standby...", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        centralex_disconnect_occupied();
    }
    else
    {
        debug_log("Centralex Config & Status:", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  Enabled: " + String(centralex_cfg.enabled ? "YES" : "NO"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  State: " + get_centralex_state_str(), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  Server: " + centralex_cfg.server + ":" + String(centralex_cfg.port), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  Number: " + String(centralex_cfg.number), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  PIN: " + String(centralex_cfg.pin ? "**** (configured)" : "0 (not set)"), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("Commands: centralex reconnect | centralex disconnect", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
}

itelex_subsciber test_subscriber;
void cmd_query(const String &args)
{
    if (args.length() == 0)
    {
        debug_log("Usage: query <number>", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    uint32_t num = args.toInt();
    debug_log("Querying subscriber number: " + String(num), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    uint8_t res = 0;
    if (num > 9999)
    {
        res = itelex_peer_query(num, test_subscriber);
    }
    else
    {
        res = SD_load_directory(num, test_subscriber);
    }

    if (res == 1)
    {
        debug_log("Subscriber found:", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  Number: " + String(test_subscriber.call_number), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  Name: " + String(test_subscriber.name), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  Host: " + String(test_subscriber.hostname), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  IP: " + test_subscriber.IP.toString(), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  Port: " + String(test_subscriber.port), 0, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("  Ext: " + String(test_subscriber.extension), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    else
    {
        debug_log("Subscriber not found (res=" + String(res) + ")", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
}

void cmd_tx(const String &args)
{
    if (args.length() == 0)
    {
        debug_log("Usage: tx <text to print on local TTY>", 0, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    tty_print_ascii(args.c_str());
    debug_log("Sent to physical TTY: \"" + args + "\"", 0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void cmd_verbosity(const String &args)
{
    int val = args.toInt();
    if (args.length() > 0 && val >= 0 && val <= 3)
    {
        current_log_level = val;
        debug_log("Log verbosity set to " + String(current_log_level), 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    else
    {
        debug_log("Usage: set-verbosity <0-3>", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
}

void cmd_time(const String &args)
{
    update_time();
    sprintf(sprintf_buf, "%02d:%02d:%02d %02d.%02d.%04d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
    debug_log("Current time: " + String(sprintf_buf), 0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void cmd_reboot(const String &args)
{
    debug_log("Rebooting ESP32...", 0, UART_LOG_MASK | TELNET_LOG_MASK);
    delay(200);
    ESP.restart();
}

void parseDebugCommands(String &s)
{
    s.trim();
    if (s.length() == 0)
    {
        return;
    }

    int spaceIdx = s.indexOf(' ');
    String cmdName = (spaceIdx == -1) ? s : s.substring(0, spaceIdx);
    String args = (spaceIdx == -1) ? "" : s.substring(spaceIdx + 1);
    cmdName.toLowerCase();
    args.trim();

    for (size_t i = 0; i < sizeof(commandTable) / sizeof(commandTable[0]); i++)
    {
        if (cmdName == commandTable[i].name)
        {
            commandTable[i].handler(args);
            return;
        }
    }

    debug_log("Unknown command: '" + cmdName + "'. Type 'help' for available commands.", 0, UART_LOG_MASK | TELNET_LOG_MASK);
}

void serial_handler()
{
    static String serialBuffer = "";
    static bool lastWasCR = false;
    while (DEBUG_UART.available())
    {
        char c = (char)DEBUG_UART.read();
        if (c == '\r')
        {
            lastWasCR = true;
            DEBUG_UART.println();
            serialBuffer.trim();
            if (serialBuffer.length() > 0)
            {
                parseDebugCommands(serialBuffer);
                serialBuffer = "";
            }
            DEBUG_UART.print("> ");
        }
        else if (c == '\n')
        {
            if (lastWasCR)
            {
                lastWasCR = false;
            }
            else
            {
                DEBUG_UART.println();
                serialBuffer.trim();
                if (serialBuffer.length() > 0)
                {
                    parseDebugCommands(serialBuffer);
                    serialBuffer = "";
                }
                DEBUG_UART.print("> ");
            }
        }
        else if (c == '\b' || c == 127) // Backspace / Delete
        {
            lastWasCR = false;
            if (serialBuffer.length() > 0)
            {
                serialBuffer.remove(serialBuffer.length() - 1);
                DEBUG_UART.print("\b \b"); // Erase character on terminal screen
            }
        }
        else if (c == 3) // Ctrl+C to cancel line
        {
            lastWasCR = false;
            serialBuffer = "";
            DEBUG_UART.println("\r\n> ");
        }
        else if (c >= 32 && c < 127) // Printable character
        {
            lastWasCR = false;
            if (serialBuffer.length() < 256)
            {
                serialBuffer += c;
                DEBUG_UART.print(c); // Smart local echo
            }
        }
    }
}

String uint8ToBinStr(uint8_t _data)
{
    String ret = "";
    for (int i = 0; i < 8; i++)
    {
        ret = ret + String((_data >> (7 - i)) & 1);
    }

    return ret;
}
