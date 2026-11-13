#include "header.hpp"

TCA9555 TCA(TCA_ADDRESS);

void set_driver_power(int power)
{
    digitalWrite(DRIVER_ON_PIN, power);
}

int _current_polarity = 0;
bool polarity_inverted = false;

int get_idle_polarity()
{
    return polarity_inverted ? 0 : 1;
}

int get_active_polarity()
{
    return polarity_inverted ? 1 : 0;
}

// 0 = negative  1 = positive
void set_polarity(int polarity)
{
    digitalWrite(POL_SWITCH_PIN, polarity);
    _current_polarity = polarity;
}

int get_polarity()
{
    return _current_polarity;
}

int get_CL_RX_STATE()
{
    return digitalRead(TTY_RX_PIN);
}

void initTCA()
{
    pinMode(TCA_IRQ, INPUT);
    if (TCA.begin())
    {
        debug_log("IO expander found.", 2, UART_LOG_MASK);
    }
    else
    {
        debug_log("IO expander not found.", 2, UART_LOG_MASK);
    }

    TCA.pinMode16(TCAPINMODE);
    TCA.write16(0b0000111111111111);
    TCA.read16(); // Clear any pending interrupt on boot
}

static uint16_t cached_tca_inputs = 0xFFFF;
static uint64_t last_tca_poll_time = 0;

void updateTCAInputs()
{
    uint64_t now = esp_timer_get_time();
    // Read over I2C if TCA interrupt is asserted (active LOW) or fallback every 50ms
    if (digitalRead(TCA_IRQ) == LOW || (now - last_tca_poll_time > 50000))
    {
        cached_tca_inputs = TCA.read16();
        last_tca_poll_time = now;
    }
}

uint8_t readTCABTN(uint input)
{
    updateTCAInputs();
    return !((cached_tca_inputs >> input) & 1);
}

void setTCALED(uint8_t led, uint8_t state)
{
    TCA.write1(led, !state);
}

static void setRGB_SDCard(uint8_t r, uint8_t g, uint8_t b)
{
    setTCALED(TCA_SDCARD_LED_R, r);
    setTCALED(TCA_SDCARD_LED_G, g);
    setTCALED(TCA_SDCARD_LED_B, b);
}

static void setRGB_Network(uint8_t r, uint8_t g, uint8_t b)
{
    setTCALED(TCA_NETWORK_LED_R, r);
    setTCALED(TCA_NETWORK_LED_G, g);
    setTCALED(TCA_NETWORK_LED_B, b);
}

static void setRGB_ITelex(uint8_t r, uint8_t g, uint8_t b)
{
    setTCALED(TCA_ITELEX_LED_R, r);
    setTCALED(TCA_ITELEX_LED_G, g);
    setTCALED(TCA_ITELEX_LED_B, b);
}

uint64_t network_activity_until = 0;
uint64_t itelex_activity_until = 0;
static uint64_t sdcard_rw_until = 0;
static uint64_t selftest_display_until = 0;
static bool selftest_sd_ok = false;
static bool selftest_net_ok = false;
static bool selftest_itlx_ok = false;
static bool selftest_hw_ok = false;
static bool selftest_idle_line_ok = false;
static bool selftest_active_line_ok = false;

void notify_network_activity(uint32_t duration_ms)
{
    network_activity_until = esp_timer_get_time() + ((uint64_t)duration_ms * 1000);
}

void notify_itelex_activity(uint32_t duration_ms)
{
    itelex_activity_until = esp_timer_get_time() + ((uint64_t)duration_ms * 1000);
}

uint64_t testtimer = 0;
uint16_t currenttestpatern = 0;
void setTCA_test_patern()
{
    if (readTCABTN(TCA_DISCONNECT_BTN))
    {
        TCA.write16(0b0000011101110000);
    }
    else
    {
        if (esp_timer_get_time() > testtimer + 100000)
        {
            currenttestpatern++;
            if (currenttestpatern > 0b0000111111111111)
            {
                currenttestpatern = 0;
            }

            TCA.write16(currenttestpatern); // 0b0000011101110000
            testtimer = esp_timer_get_time();
        }
    }
}

uint8_t lastSDLEDstate = SDLED_OFF;
uint8_t SDLEDstatus = SDLED_OFF;
void setSDCardLED(uint8_t _ledstate)
{
    SDLEDstatus = _ledstate;
    if (_ledstate == SDLED_RW)
    {
        sdcard_rw_until = esp_timer_get_time() + 150000; // Keep RW pulse visible for at least 150ms
    }
    switch (_ledstate)
    {
    case SDLED_OFF:
        setRGB_SDCard(0, 0, 0);
        break;
    case SDLED_OK:
        setRGB_SDCard(0, 1, 0);
        break;
    case SDLED_NOK:
        setRGB_SDCard(1, 0, 0);
        break;
    case SDLED_RW:
        setRGB_SDCard(0, 0, 1);
        break;
    default:
        setRGB_SDCard(0, 0, 0);
        break;
    }
}

uint8_t getSDLEDstate()
{
    return SDLEDstatus;
}

void run_selftest(bool allow_tty_print)
{
    debug_log("=== ESPTelex Self-Test & Line Test ===", 0, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);

    // 1. Brief LED lamp test sweep (Red -> Green -> Blue -> All) so user can verify all LEDs work
    setTCALED(TCA_IDLE_LED, 1);
    setTCALED(TCA_DIALING_LED, 0);
    setTCALED(TCA_CONNECTED_LED, 0);
    setRGB_SDCard(1, 0, 0);
    setRGB_Network(1, 0, 0);
    setRGB_ITelex(1, 0, 0);
    delay(180);

    setTCALED(TCA_IDLE_LED, 0);
    setTCALED(TCA_DIALING_LED, 1);
    setTCALED(TCA_CONNECTED_LED, 0);
    setRGB_SDCard(0, 1, 0);
    setRGB_Network(0, 1, 0);
    setRGB_ITelex(0, 1, 0);
    delay(180);

    setTCALED(TCA_IDLE_LED, 0);
    setTCALED(TCA_DIALING_LED, 0);
    setTCALED(TCA_CONNECTED_LED, 1);
    setRGB_SDCard(0, 0, 1);
    setRGB_Network(0, 0, 1);
    setRGB_ITelex(0, 0, 1);
    delay(180);

    // 2. Test I2C Peripherals (TCA9555, INA226, SC16IS752)
    bool tca_ok = TCA.isConnected();
    bool ina_ok = INA.isConnected();
    bool uart_ok = (TTY.ping() == 1);
    selftest_hw_ok = tca_ok && ina_ok && uart_ok;

    debug_log("I2C Peripherals: TCA9555=" + String(tca_ok ? "OK" : "FAIL") +
                  " | INA226=" + String(ina_ok ? "OK" : "FAIL") +
                  " | SC16IS752=" + String(uart_ok ? "OK" : "FAIL"),
              0, UART_LOG_MASK | TELNET_LOG_MASK);

    // 3. Test SD Card & Config Files
    bool sd_inserted = (!digitalRead(SD_DETECT_PIN)) && (card_inserted == 1);
    bool wifi_cfg_ok = sd_inserted && SD.exists("/configs/wifi.txt");
    bool cx_cfg_ok = sd_inserted && SD.exists("/configs/centralex.txt");
    selftest_sd_ok = sd_inserted;

    debug_log("SD Card: " + String(sd_inserted ? "OK (Mounted)" : "FAIL (Missing/Unmounted)") +
                  " | wifi.txt=" + String(wifi_cfg_ok ? "YES" : "NO") +
                  " | centralex.txt=" + String(cx_cfg_ok ? "YES" : "NO"),
              0, UART_LOG_MASK | TELNET_LOG_MASK);

    // 4. Test Network (WiFi + Gateway / Nameserver reachability)
    bool wifi_ok = (WiFi.status() == WL_CONNECTED);
    int rssi = wifi_ok ? WiFi.RSSI() : 0;
    IPAddress ns_ip;
    bool dns_ok = false;
    if (wifi_ok)
    {
        notify_network_activity(300);
        dns_ok = WiFi.hostByName(NAMESERVER_2_HOSTNAME, ns_ip);
    }
    selftest_net_ok = wifi_ok && dns_ok;

    debug_log("Network: WiFi=" + String(wifi_ok ? ("OK (" + WiFi.localIP().toString() + ", " + String(rssi) + "dBm)") : "FAIL") +
                  " | DNS/TlnServ=" + String(dns_ok ? ("OK (" + ns_ip.toString() + ")") : "FAIL"),
              0, UART_LOG_MASK | TELNET_LOG_MASK);

    // 5. Test i-Telex / Centralex status
    if (centralex_cfg.enabled)
    {
        selftest_itlx_ok = (centralex_state == CENTRALEX_STATE_STANDBY || centralex_state == CENTRALEX_STATE_IN_CALL);
        debug_log("i-Telex (Centralex #" + String(centralex_cfg.number) + "): " +
                      String(selftest_itlx_ok ? "OK" : "WARN") + " (" + get_centralex_state_str() + ")",
                  0, UART_LOG_MASK | TELNET_LOG_MASK);
    }
    else
    {
        selftest_itlx_ok = wifi_ok;
        debug_log("i-Telex (Direct IP port " + String(ITELEX_PORT) + "): " +
                      String(selftest_itlx_ok ? "OK" : "FAIL") + " (State: " + get_itelex_state_str() + ")",
                  0, UART_LOG_MASK | TELNET_LOG_MASK);
    }

    // 6. Test TW39 Line & Print to TTY if possible
    float idle_mA = 0.0f;
    float active_mA = 0.0f;
    int rx_idle = digitalRead(TTY_RX_PIN);
    int rx_active = rx_idle;

    if (iTelexState == ITELEX_IDLE_STATE)
    {
        // Measure idle loop current (Ruhestrom: expected ~2.0 to 8.0 mA)
        set_polarity(get_idle_polarity());
        set_driver_power(1);
        delay(50);
        idle_mA = get_line_current();
        rx_idle = digitalRead(TTY_RX_PIN);
        selftest_idle_line_ok = (idle_mA >= 1.5f && idle_mA <= 12.0f);

        // Switch to active polarity to test active loop current (Schreibstrom: expected ~25 to 60 mA)
        set_polarity(get_active_polarity());
        set_driver_power(1);
        reset_line_break_timer();

        // Wait 600ms for FSG relay / teletype motor spinup and loop closure
        for (int i = 0; i < 12; i++)
        {
            delay(50);
            esp_task_wdt_reset();
        }
        active_mA = get_line_current();
        rx_active = digitalRead(TTY_RX_PIN);
        selftest_active_line_ok = (active_mA >= 20.0f && active_mA <= 65.0f && rx_active == HIGH);

        debug_log("TW39 Line Test: Idle=" + String(idle_mA, 2) + " mA (" + String(selftest_idle_line_ok ? "OK" : "FAIL") +
                      ") | Active=" + String(active_mA, 2) + " mA, TTY_RX=" + String(rx_active) + " (" +
                      String(selftest_active_line_ok ? "OK" : "FAIL") + ")",
                  0, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);

        // If TTY is connected and responding on active loop, print the self-test report to the TTY!
        if (allow_tty_print && selftest_active_line_ok && uart_ok)
        {
            debug_log("Printing self-test report to physical TTY...", 0, UART_LOG_MASK | TELNET_LOG_MASK);

            // Give motor 1.0s more to reach full speed before printing characters
            for (int i = 0; i < 20; i++)
            {
                delay(50);
                esp_task_wdt_reset();
            }

            String report = "\r\n--- esptelex selftest ---\r\n";
            report += "hw: " + String(selftest_hw_ok ? "ok" : "err") + "  sd: " + String(selftest_sd_ok ? "ok" : "err") + "\r\n";
            report += "net: " + String(selftest_net_ok ? "ok" : "err") + " (" + WiFi.localIP().toString() + ")\r\n";
            if (centralex_cfg.enabled)
            {
                report += "itlx: cx " + String(centralex_cfg.number) + " " + String(selftest_itlx_ok ? "ok" : "err") + "\r\n";
            }
            else
            {
                report += "itlx: direct " + String(selftest_itlx_ok ? "ok" : "err") + "\r\n";
            }
            report += "line idle: " + String(idle_mA, 1) + " ma\r\n";
            report += "line actv: " + String(active_mA, 1) + " ma\r\n\r\n";

            // Transmit line-by-line so the 64-byte SC16IS752 TX FIFO never overflows
            int startIdx = 0;
            while (startIdx < (int)report.length())
            {
                int nlIdx = report.indexOf('\n', startIdx);
                if (nlIdx == -1)
                    nlIdx = report.length() - 1;
                String chunk = report.substring(startIdx, nlIdx + 1);
                tty_print_ascii(chunk.c_str());

                // Wait for chunk to finish transmitting at 50 baud (~150ms per char)
                uint64_t wait_start = esp_timer_get_time();
                uint64_t max_wait = (uint64_t)(chunk.length() + 4) * 165000ULL;
                while (!TTY_transmitter_empty() && (esp_timer_get_time() - wait_start < max_wait))
                {
                    delay(20);
                    esp_task_wdt_reset();
                }
                delay(150);
                startIdx = nlIdx + 1;
            }

            // Wait a moment after last character before powering down motor
            delay(400);
            tty_reset_fifos();
            tty_clear_echo_queue();
            tty_is_transmitting = false;
        }

        // Restore idle line state and prevent false dialing trigger from decaying current
        set_polarity(get_idle_polarity());
        set_driver_power(1);
        delay(100);
        fill_last_current(0);
        clear_PD_telex_number();
        reset_line_break_timer();
    }
    else
    {
        // Already in an active state (e.g. LOCAL or ONLINE)
        active_mA = get_line_current();
        rx_active = digitalRead(TTY_RX_PIN);
        selftest_idle_line_ok = true;
        selftest_active_line_ok = (active_mA >= 20.0f && active_mA <= 65.0f && rx_active == HIGH);

        debug_log("TW39 Active Line Check (state=" + get_itelex_state_str() + "): Current=" +
                      String(active_mA, 2) + " mA, TTY_RX=" + String(rx_active) + " (" +
                      String(selftest_active_line_ok ? "OK" : "FAIL") + ")",
                  0, UART_LOG_MASK | TELNET_LOG_MASK);

        if (allow_tty_print && iTelexState == ITELEX_LOCAL_STATE && selftest_active_line_ok && uart_ok)
        {
            String msg = "\r\nselftest: hw=" + String(selftest_hw_ok ? "ok" : "err") +
                         " sd=" + String(selftest_sd_ok ? "ok" : "err") +
                         " net=" + String(selftest_net_ok ? "ok" : "err") +
                         " i=" + String(active_mA, 1) + "ma\r\n";
            tty_print_ascii(msg.c_str());
        }
    }

    // Display self-test results on the LEDs for 4 seconds:
    // - SDCARD RGB: Green = SD OK, Red = SD Fail
    // - NETWORK RGB: Green = WiFi+DNS OK, Yellow = WiFi only, Red = Disconnected
    // - ITELEX RGB: Green = Centralex/Server + HW OK, Yellow = Connecting, Red = Fault
    // - IDLE LED: ON if Idle Line Current (Ruhestrom) OK
    // - CONNECTED LED: ON if Active Line Current (Schreibstrom) OK
    // - DIALING LED: ON if any line test failed (fault indicator)
    selftest_display_until = esp_timer_get_time() + 4000000ULL;
    debug_log("=== Self-Test Complete (displaying results on LEDs for 4s) ===", 0, UART_LOG_MASK | TELNET_LOG_MASK);
}

static bool last_test_btn_state = false;
static uint64_t last_test_btn_change = 0;
static bool last_disc_btn_state = false;
static uint64_t last_disc_btn_change = 0;

void handle_buttons()
{
    uint64_t now = esp_timer_get_time();

    // Debounce DISCONNECT button (50ms)
    bool disc_pressed = (readTCABTN(TCA_DISCONNECT_BTN) != 0);
    if (disc_pressed != last_disc_btn_state && (now - last_disc_btn_change > 50000))
    {
        last_disc_btn_state = disc_pressed;
        last_disc_btn_change = now;
        if (disc_pressed)
        {
            debug_log("DISCONNECT button pressed! Forcing station disconnect...", 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
            // Visual confirmation on LEDs (all status LEDs flash red/on briefly)
            setTCALED(TCA_IDLE_LED, 1);
            setTCALED(TCA_DIALING_LED, 1);
            setTCALED(TCA_CONNECTED_LED, 1);
            setRGB_ITelex(1, 0, 0);
            itelex_force_disconnect();
        }
    }

    // Debounce TEST button (50ms)
    bool test_pressed = (readTCABTN(TCA_TEST_BTN) != 0);
    if (test_pressed != last_test_btn_state && (now - last_test_btn_change > 50000))
    {
        last_test_btn_state = test_pressed;
        last_test_btn_change = now;
        if (test_pressed)
        {
            debug_log("TEST button pressed! Starting self-test...", 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
            run_selftest(true);
        }
    }
}

uint8_t lastblinkstate = 0;
uint64_t lastblink = 0;
uint8_t fastblinkstate = 0;
uint64_t lastfastblink = 0;
uint64_t ledshandlertimer = 0;

void handle_LEDs()
{
    uint64_t now = esp_timer_get_time();
    if (now <= ledshandlertimer + LEDs_HANDLER_INTERVAL)
    {
        return;
    }
    ledshandlertimer = now;

    // Update blink oscillators:
    // - Slow blink: 1000ms period (500ms toggles or 1000ms toggles for IDLE)
    // - Fast blink: 250ms period for activity / handshakes
    if (now > lastblink + 1000000)
    {
        lastblink = now;
        lastblinkstate = !lastblinkstate;
    }
    if (now > lastfastblink + 250000)
    {
        lastfastblink = now;
        fastblinkstate = !fastblinkstate;
    }

    // If self-test summary display is active, show self-test results on LEDs
    if (now < selftest_display_until)
    {
        setRGB_SDCard(selftest_sd_ok ? 0 : 1, selftest_sd_ok ? 1 : 0, 0);
        if (selftest_net_ok)
            setRGB_Network(0, 1, 0);
        else if (WiFi.status() == WL_CONNECTED)
            setRGB_Network(1, 1, 0);
        else
            setRGB_Network(1, 0, 0);

        if (selftest_itlx_ok && selftest_hw_ok)
            setRGB_ITelex(0, 1, 0);
        else if (selftest_hw_ok)
            setRGB_ITelex(1, 1, 0);
        else
            setRGB_ITelex(1, 0, 0);

        setTCALED(TCA_IDLE_LED, selftest_idle_line_ok ? 1 : 0);
        setTCALED(TCA_CONNECTED_LED, selftest_active_line_ok ? 1 : 0);
        setTCALED(TCA_DIALING_LED, (!selftest_idle_line_ok || !selftest_active_line_ok) ? fastblinkstate : 0);
        return;
    }

    // =========================================================================
    // 1. SDCARD RGB LED
    //    - Blue:   Read/Write activity in progress
    //    - Green:  Card inserted & mounted OK
    //    - Red:    Card missing or mount error
    // =========================================================================
    if (now < sdcard_rw_until || SDLEDstatus == SDLED_RW)
    {
        setRGB_SDCard(0, 0, 1); // Blue on R/W
    }
    else if (!digitalRead(SD_DETECT_PIN) && card_inserted)
    {
        setRGB_SDCard(0, 1, 0); // Green when mounted & ready
    }
    else
    {
        setRGB_SDCard(1, 0, 0); // Red when missing or error
    }

    // =========================================================================
    // 2. NETWORK RGB LED
    //    - Blue (or Cyan flash): Network packet / Telnet / DNS activity
    //    - Green (Solid):        WiFi connected (good signal >= -75 dBm)
    //    - Yellow (R+G):         WiFi connecting/disconnected OR weak signal
    //    - Red:                  WiFi connection failed / no network
    // =========================================================================
    wl_status_t wf = WiFi.status();
    if (wf == WL_CONNECTED)
    {
        if (now < network_activity_until)
        {
            setRGB_Network(0, 0, 1); // Blue flash on network activity
        }
        else if (telnet.isConnected())
        {
            // Subtle cyan/green or solid green when connected
            setRGB_Network(0, 1, 0);
        }
        else
        {
            setRGB_Network(0, 1, 0); // Solid Green
        }
    }
    else if (wf == WL_DISCONNECTED || wf == WL_IDLE_STATUS || wf == WL_SCAN_COMPLETED)
    {
        // Blinking Yellow while attempting to connect / searching
        setRGB_Network(fastblinkstate, fastblinkstate, 0);
    }
    else
    {
        // Solid Red on connection failure / no shield / no SSID
        setRGB_Network(1, 0, 0);
    }

    // =========================================================================
    // 3. ITELEX RGB LED
    //    - Purple/Magenta (R+B) or Cyan (G+B) pulse: Baudot TX/RX or Nameserver activity
    //    - Blue (Solid):         Active i-Telex call online (ITELEX_ONLINE_STATE)
    //    - Blue (Fast Blink):    i-Telex handshake / dialing peer (CONNECTING / CS_*)
    //    - Cyan (G+B):           Local mode active (ITELEX_LOCAL_STATE)
    //    - Green (Solid):        Ready for calls (Centralex STANDBY or Direct-IP listening)
    //    - Yellow (R+G Blink):   Centralex connecting / waiting confirmation / reconnect wait
    //    - Red (Solid/Blink):    Disconnected / Line Fault (0 mA in IDLE when TTY expected)
    // =========================================================================
    if (now < itelex_activity_until)
    {
        // Flash Cyan/White on Baudot telegram / Nameserver activity
        setRGB_ITelex(0, 1, 1);
    }
    else if (iTelexState == ITELEX_ONLINE_STATE || iTelexState == ITELEX_CONNECTED_STATE)
    {
        setRGB_ITelex(0, 0, 1); // Solid Blue during active online call
    }
    else if (iTelexState == ITELEX_LOCAL_STATE)
    {
        setRGB_ITelex(0, 1, 1); // Cyan in local mode
    }
    else if (iTelexState == ITELEX_DIALING_STATE ||
             iTelexState == ITELEX_CONNECTING_STATE ||
             iTelexState == CS_START ||
             iTelexState == CS_SEND_VER ||
             iTelexState == CS_REC_VER ||
             iTelexState == SERVER_WAIT_FOR_DD ||
             iTelexState == CS_CON_VER ||
             iTelexState == CS_SEND_DD ||
             iTelexState == CS_END)
    {
        // Fast blinking Blue during rotary dialing & i-Telex handshake negotiation
        setRGB_ITelex(0, 0, fastblinkstate);
    }
    else if (iTelexState == CS_ABORT || iTelexState == ITELEX_DISCONNECT_STATE)
    {
        setRGB_ITelex(1, 0, 0); // Red during abort/disconnect
    }
    else // ITELEX_IDLE_STATE
    {
        if (wf != WL_CONNECTED)
        {
            setRGB_ITelex(1, 0, 0); // Red if network is down
        }
        else if (centralex_cfg.enabled)
        {
            if (centralex_state == CENTRALEX_STATE_STANDBY)
            {
                setRGB_ITelex(0, 1, 0); // Solid Green: Centralex standby registered & ready!
            }
            else if (centralex_state == CENTRALEX_STATE_CONNECTING || centralex_state == CENTRALEX_STATE_WAIT_CONFIRM)
            {
                setRGB_ITelex(fastblinkstate, fastblinkstate, 0); // Fast blink Yellow while logging into Centralex
            }
            else if (centralex_state == CENTRALEX_STATE_WAIT_RECONNECT)
            {
                setRGB_ITelex(1, 1, 0); // Solid Yellow while waiting for reconnect timer
            }
            else
            {
                setRGB_ITelex(1, 0, 0); // Red if Centralex disabled/misconfigured
            }
        }
        else
        {
            // Direct IP mode: Green when listening on port 134 with WiFi connected
            setRGB_ITelex(0, 1, 0);
        }
    }

    // =========================================================================
    // 4. Single-color State LEDs (IDLE, DIALING, CONNECTED) - kept as before
    // =========================================================================
    switch (iTelexState)
    {
    case ITELEX_IDLE_STATE:
        setTCALED(TCA_IDLE_LED, lastblinkstate);
        setTCALED(TCA_DIALING_LED, 0);
        setTCALED(TCA_CONNECTED_LED, 0);
        break;
    case ITELEX_DIALING_STATE:
        setTCALED(TCA_IDLE_LED, 0);
        setTCALED(TCA_DIALING_LED, 1);
        setTCALED(TCA_CONNECTED_LED, 0);
        break;
    case ITELEX_CONNECTING_STATE:
    case CS_START:
    case CS_SEND_VER:
    case CS_REC_VER:
    case SERVER_WAIT_FOR_DD:
    case CS_CON_VER:
    case CS_SEND_DD:
    case CS_END:
        setTCALED(TCA_CONNECTED_LED, lastblinkstate);
        setTCALED(TCA_DIALING_LED, 0);
        setTCALED(TCA_IDLE_LED, 0);
        break;
    case ITELEX_CONNECTED_STATE:
    case ITELEX_ONLINE_STATE:
    case ITELEX_LOCAL_STATE:
        setTCALED(TCA_CONNECTED_LED, 1);
        setTCALED(TCA_DIALING_LED, 0);
        setTCALED(TCA_IDLE_LED, 0);
        break;
    case ITELEX_DISCONNECT_STATE:
    case CS_ABORT:
        setTCALED(TCA_CONNECTED_LED, 1);
        setTCALED(TCA_DIALING_LED, 1);
        setTCALED(TCA_IDLE_LED, 1);
        break;

    default:
        break;
    }
}
