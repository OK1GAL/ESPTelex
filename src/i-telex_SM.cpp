#include "header.hpp"

WiFiServer server(ITELEX_PORT);
WiFiClient client;

uint8_t iTelexState = ITELEX_IDLE_STATE;
bool virtual_terminal_active = false;

uint32_t dialed_number = 0xffffffff;

itelex_subsciber outgoing_subscriber;
uint8_t query_status = 0;

uint8_t CharSent = 0;
uint8_t totalBytesReceived;
uint8_t RecVersion = 0;
int tmp = 0;
uint8_t IamServer = 0;
uint64_t st_timestamp = 0;

#define TCPIP_DATA_BUFFER_LENGTH 256
uint8_t TCPIPDataBuffer[TCPIP_DATA_BUFFER_LENGTH];
#define BAUDOT_DATA_BUFFER_LENGTH 1024
uint8_t BaudotBuffer[BAUDOT_DATA_BUFFER_LENGTH];
uint8_t BDData;
uint16_t BDBufferRDPTR = 0;
uint16_t BDBufferWRPTR = 0;

uint64_t ack_timer = 0;

uint64_t last_rxed_ack = 0;
uint64_t online_start_time = 0;
uint64_t last_hangup_time = 0;
extern int line_init_SM;

uint64_t centralex_reconnect_time = 0;
uint64_t centralex_step_timer = 0;
uint64_t last_centralex_heartbeat_tx = 0;
uint64_t last_centralex_heartbeat_rx = 0;
uint8_t centralex_state = CENTRALEX_STATE_DISABLED;

String get_centralex_state_str()
{
    switch (centralex_state)
    {
    case CENTRALEX_STATE_DISABLED: return "DISABLED";
    case CENTRALEX_STATE_WAIT_RECONNECT: return "WAIT_RECONNECT";
    case CENTRALEX_STATE_CONNECTING: return "CONNECTING";
    case CENTRALEX_STATE_WAIT_CONFIRM: return "WAIT_CONFIRM";
    case CENTRALEX_STATE_STANDBY: return "STANDBY";
    case CENTRALEX_STATE_IN_CALL: return "IN_CALL";
    default: return "UNKNOWN (" + String(centralex_state) + ")";
    }
}

void centralex_trigger_reconnect(uint32_t delay_ms)
{
    if (centralex_cfg.enabled)
    {
        centralex_state = CENTRALEX_STATE_WAIT_RECONNECT;
        centralex_reconnect_time = esp_timer_get_time() + ((uint64_t)delay_ms * 1000);
    }
    else
    {
        centralex_state = CENTRALEX_STATE_DISABLED;
    }
}

void centralex_disconnect_occupied()
{
    if (centralex_cfg.enabled && centralex_state == CENTRALEX_STATE_STANDBY)
    {
        debug_log("Centralex: Disconnecting standby with 'occ' (occupied)", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        uint8_t occ_pkt[5] = { ITLX_END_CMD, 0x03, 'o', 'c', 'c' };
        client.write(occ_pkt, 5);
        client.flush();
        client.stop();
        centralex_trigger_reconnect(5000);
    }
}

void centralex_reconnect()
{
    if (client.connected() && (centralex_state == CENTRALEX_STATE_STANDBY || centralex_state == CENTRALEX_STATE_WAIT_CONFIRM))
    {
        client.stop();
    }
    centralex_trigger_reconnect(100);
}

enum CONNECTION_STATE
{
    CON_NOT_ESTABLISHED = 0,
    CON_ESTABLISHED = 1,
    CON_ESTABLISHED_TIME_SENT = 2
};
uint8_t ConnectionState = CON_NOT_ESTABLISHED;

byte putbuffer(byte item)
{
    if (((BDBufferWRPTR + 1) % BAUDOT_DATA_BUFFER_LENGTH) == BDBufferRDPTR)
    {
        // buffer is full, avoid overflow
        return 0;
    }
    BaudotBuffer[BDBufferWRPTR] = item;
    BDBufferWRPTR = (BDBufferWRPTR + 1) % BAUDOT_DATA_BUFFER_LENGTH;
    return 1;
}

byte getbuffer(byte *value)
{
    if (BDBufferRDPTR == BDBufferWRPTR)
    {
        // buffer is empty
        return 0;
    }
    *value = BaudotBuffer[BDBufferRDPTR];
    BDBufferRDPTR = (BDBufferRDPTR + 1) % BAUDOT_DATA_BUFFER_LENGTH;
    return 1;
}

void ITelex_init()
{
    server.begin();
    reset_line_break_timer();
    if (centralex_cfg.enabled)
    {
        centralex_trigger_reconnect(1000);
    }
}

void sendHeartbeat(void)
{
    byte buf[3];
    buf[0] = ITLX_HEARTBEAT_CMD;
    buf[1] = 0; // lenght = 0
    client.write(buf, 2);
}

void sendVersion(void)
{
    byte buf[3];
    buf[0] = ITLX_VERSION_CMD;
    buf[1] = 1; // lenght = 1
    buf[2] = 1; // Version = 1
    client.write(buf, 3);
}

void sendACK(void)
{
    byte buf[3];
    buf[0] = ITLX_ACK_CMD;
    buf[1] = 1; // lenght = 1
    buf[2] = totalBytesReceived;
    client.write(buf, 3);
    debug_log("ACK: " + String(totalBytesReceived), 3, UART_LOG_MASK | TELNET_LOG_MASK);
}

void sendDD(byte DDNumber)
{
    byte buf[3];
    buf[0] = ITLX_DIRECTDIAL_CMD;
    buf[1] = 1; // lenght = 1
    buf[2] = DDNumber;
    client.write(buf, 3);
}

void sendBaudot(byte BDdata)
{
    byte buf[3];
    buf[0] = ITLX_BAUDOTDATA_CMD;
    buf[1] = 1; // lenght = 1
    buf[2] = BDdata;
    client.write(buf, 3);
    client.flush();
    notify_itelex_activity(150);
    notify_network_activity(100);
}

void sendEnd()
{
    if (client.connected())
    {
        byte buf[2];
        buf[0] = ITLX_END_CMD;
        buf[1] = 0; // length = 0
        client.write(buf, 2);
        client.flush();
    }
}

void iTelexCommandParser(bool IamServ)
{
    last_rxed_ack = esp_timer_get_time();

    switch (TCPIPDataBuffer[0])
    {
    case ITLX_HEARTBEAT_CMD:
        debug_log("Heartbeat cmd received", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        break;
    case ITLX_DIRECTDIAL_CMD:
        debug_log("Direct dial received.", 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
        if (IamServer)
        {
            if (iTelexState == CS_REC_VER)
            {
                sendVersion();
            }
            ConnectionState = CON_ESTABLISHED;
            if (iTelexState == SERVER_WAIT_FOR_DD || iTelexState == CS_REC_VER)
            {
                iTelexState = CS_END;
            }
        }
        break;
    case ITLX_BAUDOTDATA_CMD:
        // Per i-Telex spec section 2.2.2 & 3.1.2: if the called station didn't receive a
        // direct dial packet before the first baudot data packet, activate the default printer
        if (IamServer && (iTelexState == SERVER_WAIT_FOR_DD || iTelexState == CS_REC_VER))
        {
            if (iTelexState == CS_REC_VER)
            {
                sendVersion();
            }
            ConnectionState = CON_ESTABLISHED;
            iTelexState = CS_END;
        }
        // copy Baudot data into input buffer
        notify_itelex_activity(150);
        notify_network_activity(100);
        sprintf(sprintf_buf, "Received a baudot data with size %d", TCPIPDataBuffer[1]);
        debug_log(sprintf_buf, 1, UART_LOG_MASK | TELNET_LOG_MASK);
        for (int i = 0; i < TCPIPDataBuffer[1]; i++)
        { // TCPIPDataBuffer[1] = length
            putbuffer(TCPIPDataBuffer[2 + i]);
        }
        break;
    case ITLX_END_CMD:
        debug_log("End cmd received", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        itelex_hangup();
        break;
    case ITLX_REJECT_CMD:
        {
            String reason = "";
            for (int i = 0; i < TCPIPDataBuffer[1]; i++)
            {
                char c = (char)TCPIPDataBuffer[2 + i];
                if (c >= 32 && c <= 126)
                    reason += c;
            }
            debug_log("Reject cmd received" + (reason.length() > 0 ? (" (" + reason + ")") : ""), 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
            iTelexState = ITELEX_DISCONNECT_STATE;
        }
        break;
    case ITLX_ACK_CMD:
        debug_log("ACK cmd received", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        break;
    case ITLX_VERSION_CMD:
        RecVersion = TCPIPDataBuffer[2];
        debug_log("Version cmd received", 1, UART_LOG_MASK);
        if (IamServer)
        {
            sendVersion();
            if (iTelexState == CS_REC_VER)
            {
                iTelexState = SERVER_WAIT_FOR_DD;
                st_timestamp = esp_timer_get_time();
            }
        }
        break;
    case ITLX_LOOPBACK_CMD:
        debug_log("Loopback cmd received", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        // do nothing
        break;
    default:
        break;
    }
} // iTelexCommandParser

byte ReceiveTCPIPCommand(void)
{
    /* if (!client.connected())
        return ITLX_NOTCONNECTED_CMD; */

    // We need at least 2 bytes (Type and Length) to start parsing
    if (client.available() >= 2)
    {
        uint8_t type = client.peek(); // Peek the first byte (Type)

        // Since we can't peek the second byte easily, we read the header
        // but only if we know we won't get stuck.

        // Let's read the 2 bytes of header properly
        uint8_t header[2];
        client.readBytes(header, 2);

        type = header[0];
        uint8_t len = header[1];

        // Security check for buffer size
        if (len > (TCPIP_DATA_BUFFER_LENGTH - 2))
        {
            debug_log("Packet too long, flushing!", 1, UART_LOG_MASK);
            while (client.available())
                client.read(); // Flush junk
            return ITLX_NOCOMMAND_CMD;
        }

        // Now we wait for the rest of the packet (the payload)
        // We use a timeout-safe read for the payload
        if (len > 0)
        {
            // Wait until the rest of the packet is available, client disconnects, or timeout
            uint64_t startWait = esp_timer_get_time();
            while (client.available() < len)
            {
                if (!client.connected() || (esp_timer_get_time() - startWait > 500000))
                { // 500ms timeout or disconnect
                    debug_log("Packet payload timeout or client disconnected!", 1, UART_LOG_MASK);
                    while (client.available())
                        client.read(); // Flush junk to keep stream aligned
                    return ITLX_NOCOMMAND_CMD;
                }
                delay(1);
            }
            client.readBytes(&TCPIPDataBuffer[2], len);
            TCPIPDataBuffer[2 + len] = 0;
        }
        else
        {
            TCPIPDataBuffer[2] = 0;
        }

        // Reconstruct the full packet in the buffer for the parser
        TCPIPDataBuffer[0] = type;
        TCPIPDataBuffer[1] = len;

        return type;
    }
    return ITLX_NOCOMMAND_CMD;
} // end: ReceiveTCPIPCommand(void)

String get_itelex_state_str()
{
    switch (iTelexState)
    {
    case ITELEX_IDLE_STATE: return "IDLE";
    case ITELEX_DIALING_STATE: return "DIALING";
    case ITELEX_CONNECTED_STATE: return "CONNECTED";
    case ITELEX_CONNECTING_STATE: return "CONNECTING";
    case CS_START: return "HANDSHAKE_START";
    case CS_SEND_VER: return "SEND_VERSION";
    case CS_REC_VER: return "WAIT_VERSION";
    case SERVER_WAIT_FOR_DD: return "SERVER_WAIT_FOR_DD";
    case CS_CON_VER: return "CONFIRM_VERSION";
    case CS_SEND_DD: return "SEND_DIRECT_DIAL";
    case CS_ABORT: return "HANDSHAKE_ABORT";
    case CS_END: return "HANDSHAKE_END";
    case ITELEX_BUSY_STATE: return "BUSY";
    case ITELEX_ONLINE_STATE: return "ONLINE";
    case ITELEX_LOCAL_STATE: return "LOCAL";
    case ITELEX_DISCONNECT_STATE: return "DISCONNECTING";
    default: return "UNKNOWN (" + String(iTelexState) + ")";
    }
}

bool itelex_dial(uint32_t number, bool is_virtual)
{
    if (iTelexState != ITELEX_IDLE_STATE && iTelexState != ITELEX_DIALING_STATE)
    {
        debug_log("Cannot dial: connection currently busy (state=" + get_itelex_state_str() + ")", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        return false;
    }

    // Disconnect Centralex standby with 'occ' if active
    centralex_disconnect_occupied();

    virtual_terminal_active = is_virtual;
    outgoing_subscriber.call_number = 0;
    outgoing_subscriber.connection_type = 0;
    outgoing_subscriber.extension = 0;
    outgoing_subscriber.flags = 0;
    for (int i = 0; i < 40; i++)
    {
        outgoing_subscriber.hostname[i] = 0;
        outgoing_subscriber.name[i] = 0;
    }
    outgoing_subscriber.IP_address32 = 0;
    outgoing_subscriber.IP = IPAddress(0, 0, 0, 0);
    outgoing_subscriber.IP_address[0] = 0;
    outgoing_subscriber.IP_address[1] = 0;
    outgoing_subscriber.IP_address[2] = 0;
    outgoing_subscriber.IP_address[3] = 0;
    outgoing_subscriber.pin = 0;
    outgoing_subscriber.port = 0;
    outgoing_subscriber.timestamp = 0;

    query_status = 0;
    dialed_number = number;
    if (!is_virtual)
    {
        set_polarity(get_active_polarity());
        set_driver_power(1);
    }
    fill_last_current(1);
    reset_line_break_timer();
    debug_log("Dialing number: " + String(dialed_number) + (is_virtual ? " (Virtual Terminal)" : ""), 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);

    if (dialed_number == 0)
    {
        query_status = 1;
    }
    else if (dialed_number > 9999)
    {
        query_status = itelex_peer_query(dialed_number, outgoing_subscriber);
    }
    else
    {
        debug_log("Querying SD card: " + String(dialed_number), 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
        uint8_t sdret = SD_load_directory(dialed_number, outgoing_subscriber);
        if (sdret == 1)
        {
            debug_log("SD card number OK", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            query_status = 1;
        }
        else
        {
            debug_log("Subscriber file error: " + String(sdret), 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
        }
    }

    if (query_status == 1)
    {
        debug_log("Dialed number OK", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("Number: " + String(outgoing_subscriber.call_number), 1, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("Name: " + String(outgoing_subscriber.name), 1, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("Hostname: " + String(outgoing_subscriber.hostname), 1, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("IP: " + outgoing_subscriber.IP.toString(), 1, UART_LOG_MASK | TELNET_LOG_MASK);
        debug_log("Port: " + String(outgoing_subscriber.port), 1, UART_LOG_MASK | TELNET_LOG_MASK);
        fill_last_current(1);
        reset_line_break_timer();
        iTelexState = ITELEX_CONNECTING_STATE;
        clear_PD_telex_number();
        return true;
    }
    else
    {
        debug_log("Peer query failed for number: " + String(dialed_number), 1, UART_LOG_MASK | TELNET_LOG_MASK);
        itelex_hangup();
        return false;
    }
}

void itelex_hangup()
{
    if (iTelexState != ITELEX_IDLE_STATE)
    {
        debug_log("Disconnect requested, terminating session immediately.", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        sendEnd();
        delay(50);
        client.stop();
        set_polarity(get_idle_polarity());
        set_driver_power(1);
        fill_last_current(0);
        line_init_SM = 0;
        last_hangup_time = esp_timer_get_time();

        IamServer = false;
        virtual_terminal_active = false;
        ConnectionState = CON_NOT_ESTABLISHED;
        totalBytesReceived = 0;
        RecVersion = 0;
        online_start_time = 0;
        BDBufferRDPTR = 0;
        BDBufferWRPTR = 0;
        memset(TCPIPDataBuffer, 0, TCPIP_DATA_BUFFER_LENGTH);

        tty_reset_fifos();
        tty_clear_echo_queue();
        tty_is_transmitting = false;
        reset_line_break_timer();

        iTelexState = ITELEX_IDLE_STATE;
        debug_log("Session terminated, state reset to IDLE", 1, UART_LOG_MASK | TELNET_LOG_MASK);

        if (centralex_cfg.enabled)
        {
            debug_log("Centralex: Call ended, will reconnect standby in 5 seconds", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            centralex_trigger_reconnect(5000);
        }
    }
    else
    {
        debug_log("Already idle / disconnected.", 1, UART_LOG_MASK | TELNET_LOG_MASK);
    }
}

void itelex_force_disconnect()
{
    debug_log("EMERGENCY FORCE DISCONNECT triggered!", 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);

    if (client.connected())
    {
        if (iTelexState != ITELEX_IDLE_STATE)
        {
            sendEnd();
            delay(50);
        }
        else if (centralex_cfg.enabled && centralex_state == CENTRALEX_STATE_STANDBY)
        {
            uint8_t occ_pkt[5] = { ITLX_END_CMD, 0x03, 'o', 'c', 'c' };
            client.write(occ_pkt, 5);
            client.flush();
            delay(30);
        }
    }
    client.stop();

    // Cut loop power for 250ms to force any stuck FSG/TTY relay to drop out, then restore idle polarity
    set_driver_power(0);
    set_polarity(get_idle_polarity());
    delay(250);
    set_driver_power(1);

    fill_last_current(0);
    clear_PD_telex_number();
    line_init_SM = 0;
    last_hangup_time = esp_timer_get_time();

    IamServer = false;
    virtual_terminal_active = false;
    ConnectionState = CON_NOT_ESTABLISHED;
    totalBytesReceived = 0;
    RecVersion = 0;
    online_start_time = 0;
    BDBufferRDPTR = 0;
    BDBufferWRPTR = 0;
    memset(TCPIPDataBuffer, 0, TCPIP_DATA_BUFFER_LENGTH);

    tty_reset_fifos();
    tty_clear_echo_queue();
    tty_is_transmitting = false;
    reset_line_break_timer();

    iTelexState = ITELEX_IDLE_STATE;
    debug_log("Force disconnect completed, state reset to IDLE", 1, UART_LOG_MASK | TELNET_LOG_MASK);

    if (centralex_cfg.enabled)
    {
        centralex_trigger_reconnect(5000);
    }
}

void itelex_send_wru()
{
    if (!client.connected() || iTelexState != ITELEX_ONLINE_STATE)
    {
        debug_log("Cannot send WRU: Not connected to an online peer (state=" + get_itelex_state_str() + ")", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    sendBaudot(BAUDOT_MODE_ZI); // Shift to figures
    delay(5);
    sendBaudot(18);             // WRU (Who are you? / Figures-D)
    delay(5);
    sendBaudot(BAUDOT_MODE_BU); // Shift back to letters
    debug_log("[SENT]: <WRU> (Who are you?)", 1, UART_LOG_MASK | TELNET_LOG_MASK);
}

void itelex_send_crlf()
{
    if (!client.connected() || iTelexState != ITELEX_ONLINE_STATE)
    {
        debug_log("Cannot send CRLF: Not connected (state=" + get_itelex_state_str() + ")", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    sendBaudot(8); // CR
    delay(5);
    sendBaudot(2); // LF
    debug_log("[SENT]: <CR><LF>", 1, UART_LOG_MASK | TELNET_LOG_MASK);
}

void itelex_send_cr()
{
    if (!client.connected() || iTelexState != ITELEX_ONLINE_STATE)
    {
        debug_log("Cannot send CR: Not connected (state=" + get_itelex_state_str() + ")", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    sendBaudot(8);
    debug_log("[SENT]: <CR>", 1, UART_LOG_MASK | TELNET_LOG_MASK);
}

void itelex_send_lf()
{
    if (!client.connected() || iTelexState != ITELEX_ONLINE_STATE)
    {
        debug_log("Cannot send LF: Not connected (state=" + get_itelex_state_str() + ")", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    sendBaudot(2);
    debug_log("[SENT]: <LF>", 1, UART_LOG_MASK | TELNET_LOG_MASK);
}

void itelex_send_bell()
{
    if (!client.connected() || iTelexState != ITELEX_ONLINE_STATE)
    {
        debug_log("Cannot send Bell: Not connected (state=" + get_itelex_state_str() + ")", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }
    sendBaudot(BAUDOT_MODE_ZI); // Figures
    delay(5);
    sendBaudot(19);             // Bell (Figures-J)
    delay(5);
    sendBaudot(BAUDOT_MODE_BU); // Letters
    debug_log("[SENT]: <BELL> (Klingel)", 1, UART_LOG_MASK | TELNET_LOG_MASK);
}

void itelex_send_ascii(const String &text, bool append_crlf)
{
    if (!client.connected() || iTelexState != ITELEX_ONLINE_STATE)
    {
        debug_log("Cannot send message: Not connected to an online peer (state=" + get_itelex_state_str() + ")", 1, UART_LOG_MASK | TELNET_LOG_MASK);
        return;
    }

    static int current_shift = -1; // 0 = letters, 1 = figures

    for (size_t i = 0; i < text.length(); i++)
    {
        char ch = text[i];

        // Check for backslash escape sequences: \w (WRU), \r (CR), \n (LF), \b (Bell), \\ (Backslash)
        if (ch == '\\' && i + 1 < text.length())
        {
            char next = text[i + 1];
            if (next == 'w' || next == 'W')
            {
                sendBaudot(BAUDOT_MODE_ZI);
                delay(5);
                sendBaudot(18); // WRU
                delay(5);
                sendBaudot(BAUDOT_MODE_BU);
                current_shift = 0;
                i++;
                continue;
            }
            else if (next == 'r' || next == 'R')
            {
                sendBaudot(8); // CR
                i++;
                continue;
            }
            else if (next == 'n' || next == 'N')
            {
                sendBaudot(2); // LF
                i++;
                continue;
            }
            else if (next == 'a' || next == 'A' || next == 'b' || next == 'B')
            {
                sendBaudot(BAUDOT_MODE_ZI);
                delay(5);
                sendBaudot(19); // Bell
                delay(5);
                sendBaudot(BAUDOT_MODE_BU);
                current_shift = 0;
                i++;
                continue;
            }
            else if (next == '\\')
            {
                ch = '\\';
                i++;
            }
        }

        if (ch == '\r')
        {
            sendBaudot(8);
            continue;
        }
        if (ch == '\n')
        {
            sendBaudot(2);
            continue;
        }
        if (ch < 0 || ch > 127)
        {
            continue;
        }

        if (ch >= 'a' && ch <= 'z')
        {
            ch = ch - 'a' + 'A';
        }

        uint8_t raw = (uint8_t)ascii2baudot[(uint8_t)ch];
        int chartype = (raw >> 5) & 0x01;
        uint8_t code = raw & 0x1F;

        if (code == 0 && ch != ' ')
        {
            continue;
        }

        if (chartype != current_shift)
        {
            uint8_t shift_code = (chartype == 0) ? BAUDOT_MODE_BU : BAUDOT_MODE_ZI;
            sendBaudot(shift_code);
            current_shift = chartype;
            delay(5);
        }

        sendBaudot(code);
        delay(5);
    }

    if (append_crlf)
    {
        sendBaudot(8); // CR
        delay(5);
        sendBaudot(2); // LF
    }

    debug_log("[SENT]: " + text, 1, UART_LOG_MASK | TELNET_LOG_MASK);
}

void handle_centralex()
{
    if (!centralex_cfg.enabled)
    {
        centralex_state = CENTRALEX_STATE_DISABLED;
        return;
    }

    if (WiFi.status() != WL_CONNECTED)
    {
        return;
    }

    if (centralex_cfg.number == 0)
    {
        static uint64_t last_warn_time = 0;
        if (esp_timer_get_time() > last_warn_time + 30000000)
        {
            debug_log("Centralex: enabled but subscriber number is 0! Check /configs/centralex.txt", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            last_warn_time = esp_timer_get_time();
        }
        centralex_state = CENTRALEX_STATE_DISABLED;
        return;
    }

    if (iTelexState != ITELEX_IDLE_STATE)
    {
        return;
    }

    switch (centralex_state)
    {
    case CENTRALEX_STATE_DISABLED:
        centralex_trigger_reconnect(1000);
        break;

    case CENTRALEX_STATE_WAIT_RECONNECT:
        if (esp_timer_get_time() >= centralex_reconnect_time)
        {
            debug_log("Centralex: Connecting to " + centralex_cfg.server + ":" + String(centralex_cfg.port) + "...", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            centralex_state = CENTRALEX_STATE_CONNECTING;
            centralex_step_timer = esp_timer_get_time();
        }
        break;

    case CENTRALEX_STATE_CONNECTING:
    {
        IPAddress server_ip;
        bool resolved = false;

        // Check if string is already an IP address
        if (server_ip.fromString(centralex_cfg.server))
        {
            resolved = true;
        }
        else if (WiFi.hostByName(centralex_cfg.server.c_str(), server_ip))
        {
            resolved = true;
        }

        if (!resolved)
        {
            debug_log("Centralex: DNS lookup failed for host '" + centralex_cfg.server + "', retrying in 15s", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            centralex_trigger_reconnect(15000);
            break;
        }

        debug_log("Centralex: Connecting to " + server_ip.toString() + ":" + String(centralex_cfg.port) + "...", 1, UART_LOG_MASK | TELNET_LOG_MASK);

        if (client.connect(server_ip, centralex_cfg.port))
        {
            client.setNoDelay(true);
            debug_log("Centralex: Connected. Sending login (0x81) for subscriber " + String(centralex_cfg.number) + "...", 1, UART_LOG_MASK | TELNET_LOG_MASK);

            uint8_t login[8];
            login[0] = RITLX_CONNECTREMOTE_CMD; // 0x81
            login[1] = 6;                       // Length
            login[2] = (centralex_cfg.number >> 0) & 0xFF;
            login[3] = (centralex_cfg.number >> 8) & 0xFF;
            login[4] = (centralex_cfg.number >> 16) & 0xFF;
            login[5] = (centralex_cfg.number >> 24) & 0xFF;
            login[6] = (centralex_cfg.pin >> 0) & 0xFF;
            login[7] = (centralex_cfg.pin >> 8) & 0xFF;
            client.write(login, 8);
            client.flush();

            centralex_state = CENTRALEX_STATE_WAIT_CONFIRM;
            centralex_step_timer = esp_timer_get_time();
        }
        else
        {
            debug_log("Centralex: TCP connection failed to " + server_ip.toString() + ":" + String(centralex_cfg.port) + ", retrying in 15s", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            client.stop();
            centralex_trigger_reconnect(15000);
        }
        break;
    }

    case CENTRALEX_STATE_WAIT_CONFIRM:
        if (!client.connected())
        {
            debug_log("Centralex: Disconnected while waiting for login confirm", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            client.stop();
            centralex_trigger_reconnect(10000);
            break;
        }

        // Per spec: confirmation can take up to 30s, disconnect after 35s
        if (esp_timer_get_time() > centralex_step_timer + 35000000)
        {
            debug_log("Centralex: Login confirmation timeout (35s)", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            client.stop();
            centralex_trigger_reconnect(15000);
            break;
        }

        if (client.available() >= 2)
        {
            uint8_t cmd = ReceiveTCPIPCommand();
            if (cmd == RITLX_REMOTECONFIRM_CMD) // 0x82
            {
                debug_log("Centralex: Login confirmed (0x82). Standby active!", 1, UART_LOG_MASK | TELNET_LOG_MASK);
                centralex_state = CENTRALEX_STATE_STANDBY;
                last_centralex_heartbeat_tx = esp_timer_get_time();
                last_centralex_heartbeat_rx = esp_timer_get_time();
            }
            else if (cmd == ITLX_REJECT_CMD) // 0x04
            {
                String rej = "";
                for (int i = 0; i < TCPIPDataBuffer[1] && i < 32; i++)
                {
                    rej += (char)TCPIPDataBuffer[2 + i];
                }
                debug_log("Centralex: Login rejected (0x04)" + (rej.length() ? (": \"" + rej + "\"") : ""), 1, UART_LOG_MASK | TELNET_LOG_MASK);
                client.stop();
                centralex_trigger_reconnect(20000); // 20s backoff (handles server oop bug or bad credentials)
            }
            else if (cmd == 0xFF) // Error
            {
                String err = "";
                for (int i = 0; i < TCPIPDataBuffer[1] && i < 64; i++)
                {
                    err += (char)TCPIPDataBuffer[2 + i];
                }
                debug_log("Centralex: Server error (0xFF)" + (err.length() ? (": \"" + err + "\"") : ""), 1, UART_LOG_MASK | TELNET_LOG_MASK);
                client.stop();
                centralex_trigger_reconnect(20000);
            }
            else if (cmd == ITLX_HEARTBEAT_CMD) // 0x00
            {
                last_centralex_heartbeat_rx = esp_timer_get_time();
            }
        }
        break;

    case CENTRALEX_STATE_STANDBY:
        if (!client.connected())
        {
            debug_log("Centralex: Standby connection lost, reconnecting in 5s", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            client.stop();
            centralex_trigger_reconnect(5000);
            break;
        }

        // Send periodic heartbeat every 15s
        if (esp_timer_get_time() > last_centralex_heartbeat_tx + 15000000)
        {
            uint8_t hb[2] = { ITLX_HEARTBEAT_CMD, 0x00 };
            client.write(hb, 2);
            client.flush();
            last_centralex_heartbeat_tx = esp_timer_get_time();
        }

        // Inactivity timeout: 45s without any packet from server
        if (esp_timer_get_time() > last_centralex_heartbeat_rx + 45000000)
        {
            debug_log("Centralex: Standby heartbeat timeout, reconnecting...", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            client.stop();
            centralex_trigger_reconnect(5000);
            break;
        }

        // Process packets arriving in standby
        if (client.available() >= 2)
        {
            uint8_t cmd = ReceiveTCPIPCommand();
            if (cmd == ITLX_HEARTBEAT_CMD) // 0x00
            {
                last_centralex_heartbeat_rx = esp_timer_get_time();
            }
            else if (cmd == RITLX_REMOTECALL_CMD) // 0x83
            {
                last_centralex_heartbeat_rx = esp_timer_get_time();
                debug_log("Centralex: Incoming call signaled (0x83)!", 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);

                if (iTelexState == ITELEX_IDLE_STATE)
                {
                    debug_log("Centralex: Accepting call (0x84)...", 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
                    uint8_t acc[2] = { RITLX_REMOTEACCEPTCALL_CMD, 0x00 };
                    client.write(acc, 2);
                    client.flush();

                    // Reverse polarity for active TW39 loop (motor starts)
                    set_polarity(get_active_polarity());
                    set_driver_power(1);
                    fill_last_current(1);
                    reset_line_break_timer();
                    debug_log("Centralex: Active line polarity (" + String(get_active_polarity()) + ") applied. Current: " + String(get_line_current(), 2) + " mA (raw: " + String(get_raw_line_current(), 2) + " mA)", 1, UART_LOG_MASK | TELNET_LOG_MASK);

                    IamServer = true;
                    totalBytesReceived = 0;
                    RecVersion = 0;
                    iTelexState = CS_REC_VER;
                    st_timestamp = esp_timer_get_time();
                    centralex_state = CENTRALEX_STATE_IN_CALL;
                }
                else
                {
                    debug_log("Centralex: Line busy, sending reject 'occ'", 1, UART_LOG_MASK | TELNET_LOG_MASK);
                    uint8_t rej[5] = { ITLX_REJECT_CMD, 0x03, 'o', 'c', 'c' };
                    client.write(rej, 5);
                    client.flush();
                }
            }
            else if (cmd == ITLX_REJECT_CMD || cmd == 0xFF)
            {
                debug_log("Centralex: Disconnected by server reject/error in standby", 1, UART_LOG_MASK | TELNET_LOG_MASK);
                client.stop();
                centralex_trigger_reconnect(15000);
            }
        }
        break;

    case CENTRALEX_STATE_IN_CALL:
        break;
    }
}

uint64_t time_idle_to_dial = 0;
int line_init_SM = 0;
uint64_t last_line_init_timer = 0;
int line_refuse_SM = 0;
uint64_t last_line_refuse_timer = 0;
void handle_ITelex()
{
    switch (iTelexState)
    {
    case ITELEX_IDLE_STATE:
        if (server.hasClient())
        {
            if (centralex_cfg.enabled && centralex_state == CENTRALEX_STATE_STANDBY)
            {
                centralex_disconnect_occupied();
            }
            client = server.available();
            if (client)
            {
                client.setNoDelay(true);
                debug_log("Client " + client.remoteIP().toString() + " connecting (direct LAN).", 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);

                // Switch polarity for active TW39 loop (motor starts)
                set_polarity(get_active_polarity());
                set_driver_power(1);
                fill_last_current(1);
                reset_line_break_timer();
                debug_log("Direct LAN: Active line polarity (" + String(get_active_polarity()) + ") applied. Current: " + String(get_line_current(), 2) + " mA (raw: " + String(get_raw_line_current(), 2) + " mA)", 1, UART_LOG_MASK | TELNET_LOG_MASK);

                // Transition to a state that handles the incoming handshake
                IamServer = true;
                totalBytesReceived = 0;
                RecVersion = 0;
                iTelexState = CS_REC_VER;
                st_timestamp = esp_timer_get_time();
                centralex_state = CENTRALEX_STATE_IN_CALL;
            }
        }

        handle_centralex();

        // Allow 1.5s after hangup for FSG H-relay slow release so decaying call current
        // does not falsely trigger outgoing rotary dialing (line_init_SM)
        if (last_hangup_time != 0 && esp_timer_get_time() < last_hangup_time + 1500000)
        {
            fill_last_current(0);
            line_init_SM = 0;
        }
        else if ((get_polarity() == get_idle_polarity()) && get_last_1s_current())
        {
            clear_PD_telex_number();
            switch (line_init_SM)
            {
            case 0:
                last_line_init_timer = esp_timer_get_time();
                line_init_SM = 1;
                break;
            case 1:
                if (esp_timer_get_time() > last_line_init_timer + 1000000) // 1000ms to get ready
                {
                    set_driver_power(0);
                    last_line_init_timer = esp_timer_get_time();
                    line_init_SM = 2;
                }
                break;
            case 2:
                if (esp_timer_get_time() > last_line_init_timer + 25000) // 25ms
                {
                    set_driver_power(1);
                    last_line_init_timer = esp_timer_get_time();
                    line_init_SM = 0;
                    iTelexState = ITELEX_DIALING_STATE;
                    init_rotary_dial_handler();
                    time_idle_to_dial = esp_timer_get_time();
                    fill_last_current(1);
                    reset_line_break_timer();
                    centralex_disconnect_occupied();
                }
                break;

            default:
                line_init_SM = 0;
                break;
            }
        }

        break;

    case ITELEX_DIALING_STATE:
        if (esp_timer_get_time() > time_idle_to_dial + 100000)
        {
            rotary_dial_handler();
        }

        if (get_PD_telex_number() != 0xffffffff)
        {
            itelex_dial(get_PD_telex_number(), false);
        }
        if (!get_last_3s_current())
        {
            debug_log("Line current lost during dialing, returning to IDLE", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            set_polarity(get_idle_polarity());
            iTelexState = ITELEX_IDLE_STATE;
        }
        break;

    case ITELEX_CONNECTING_STATE:
        if (dialed_number == 0)
        {
            delay(1500);
            update_time();
            sprintf(sprintf_buf, "%02d:%02d:%02d  %02d.%02d. %04d\r\n\r\n", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
            tty_print_ascii(sprintf_buf);
            fill_last_current(1);
            reset_line_break_timer();
            iTelexState = ITELEX_LOCAL_STATE;
            break;
        }
        else if (query_status == 1)
        {
            bool connected = false;
            uint16_t port = outgoing_subscriber.port;
            if (port == 0)
            {
                port = ITELEX_PORT;
            }

            // 1. If connection type is hostname (1 or 3) and hostname is present, try hostname
            if ((outgoing_subscriber.connection_type == 1 || outgoing_subscriber.connection_type == 3 || outgoing_subscriber.IP == IPAddress(0, 0, 0, 0)) && strlen(outgoing_subscriber.hostname) > 0)
            {
                debug_log("Connecting to " + String(outgoing_subscriber.hostname) + ":" + String(port) + "...", 1, UART_LOG_MASK | TELNET_LOG_MASK);
                connected = client.connect(outgoing_subscriber.hostname, port);
            }

            // 2. Connect directly by IP if not connected yet and IP is valid (type 2, 4, 5, local LAN, or fallback)
            if (!connected && outgoing_subscriber.IP != IPAddress(0, 0, 0, 0))
            {
                debug_log("Connecting to IP " + outgoing_subscriber.IP.toString() + ":" + String(port) + "...", 1, UART_LOG_MASK | TELNET_LOG_MASK);
                connected = client.connect(outgoing_subscriber.IP, port);
            }

            if (connected)
            {
                client.setNoDelay(true);
                debug_log("Client connected, initiating handshakes.", 1, UART_LOG_MASK | TELNET_LOG_MASK);
                iTelexState = CS_START;
            }
            else
            {
                String target = (strlen(outgoing_subscriber.hostname) > 0) ? String(outgoing_subscriber.hostname) : outgoing_subscriber.IP.toString();
                debug_log("Client connection failed to " + target + ":" + String(port), 1, UART_LOG_MASK | TELNET_LOG_MASK);
                itelex_hangup();
            }
        }
        else
        {
            debug_log("Invalid number, canceling connection.", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            itelex_hangup();
        }
        if (!virtual_terminal_active && !get_last_3s_current())
        {
            debug_log("Line current lost during connecting, returning to IDLE", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            set_polarity(get_idle_polarity());
            iTelexState = ITELEX_IDLE_STATE;
        }
        break;

    case ITELEX_DISCONNECT_STATE:
        itelex_hangup();
        break;

    case ITELEX_LOCAL_STATE:
        if (TTYavailable())
        {
            int raw = TTYread();
            if (raw >= 0 && raw != 0xFF)
            {
                uint8_t c = (uint8_t)(raw & 0x1F);
                debug_log("TTYRX: raw=0x" + String(raw, HEX) + " flipped=0x" + String(flipBaudotOrder(c), HEX) + " ascii='" + String(baudotToAscii(flipBaudotOrder(c))) + "'", 2, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
            }
        }
        if (is_line_break_detected(400000))
        {
            debug_log("Line break detected in local state, ending connection", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            itelex_hangup();
        }
        break;

    case CS_START:
        debug_log("i-telex_SM state: CS_START", 3, UART_LOG_MASK | TELNET_LOG_MASK);
        iTelexState = CS_SEND_VER;
        tmp = 0;
        RecVersion = 0;
        break;
    case CS_SEND_VER:
        debug_log("i-telex_SM state: CS_SEND_VER", 3, UART_LOG_MASK | TELNET_LOG_MASK);
        if (tmp > 3)
        {
            iTelexState = CS_ABORT;
            debug_log("Version negotiation did not work!", 3, UART_LOG_MASK | TELNET_LOG_MASK);
            break;
        }
        sendVersion(); // send version = 1
        iTelexState = CS_REC_VER;
        st_timestamp = esp_timer_get_time();
        break;
    case CS_REC_VER:
        if (RecVersion > 0)
        {
            if (IamServer)
            {
                // As server, once we've received version (and sent ours via parser),
                // we wait for the Direct Dial command.
                if (RecVersion == 1)
                {
                    sendVersion();
                    iTelexState = SERVER_WAIT_FOR_DD; // Or a dedicated "Wait for DD" state
                    st_timestamp = esp_timer_get_time();
                }
                else
                {
                    RecVersion = 0;
                    tmp++;
                    iTelexState = CS_SEND_VER;
                }
            }
            else
            {
                // As client, proceed to send our DD
                if (RecVersion == 1)
                {
                    iTelexState = CS_SEND_DD;
                }
                else
                {
                    RecVersion = 0;
                    tmp++;
                    iTelexState = CS_SEND_VER;
                }
            }
        }
        else if (esp_timer_get_time() > (st_timestamp + TCPIP_TIMEOUT))
        {
            debug_log("Timeout on version rx", 3, UART_LOG_MASK);
            iTelexState = CS_ABORT;
        }
        break;

    case SERVER_WAIT_FOR_DD:
        if (esp_timer_get_time() > (st_timestamp + TCPIP_TIMEOUT))
        {
            debug_log("Timeout while waiting for DD.", 3, UART_LOG_MASK);
            iTelexState = CS_ABORT;
        }
        break;

    case CS_CON_VER:
        debug_log("i-telex_SM state: CS_CON_VER", 3, UART_LOG_MASK | TELNET_LOG_MASK);
        sendVersion();
        iTelexState = CS_SEND_DD;
        break;

    case CS_SEND_DD:
        debug_log("i-telex_SM state: CS_SEND_DD", 3, UART_LOG_MASK | TELNET_LOG_MASK);
        sendDD(outgoing_subscriber.extension); // send DirectDial No (Durchwahl)
        iTelexState = CS_END;

        break;
    case CS_ABORT:
        debug_log("i-telex_SM state: CS_ABORT", 3, UART_LOG_MASK | TELNET_LOG_MASK);
        iTelexState = ITELEX_DISCONNECT_STATE;
        break;
    case CS_END:
        debug_log("i-telex_SM state: CS_END", 3, UART_LOG_MASK | TELNET_LOG_MASK);
        iTelexState = ITELEX_ONLINE_STATE;
        TTYRXflush();
        CharSent = 0;
        if (!virtual_terminal_active)
        {
            set_polarity(get_active_polarity());
            set_driver_power(1);
        }
        fill_last_current(1);
        reset_line_break_timer();
        online_start_time = esp_timer_get_time();
        last_rxed_ack = esp_timer_get_time();
        ConnectionState = CON_ESTABLISHED;
        debug_log("Online line state: current=" + String(get_line_current(), 2) + " mA (raw: " + String(get_raw_line_current(), 2) + " mA), TTY_RX=" + String(digitalRead(TTY_RX_PIN)) + ", pol=" + String(get_polarity()), 1, UART_LOG_MASK | TELNET_LOG_MASK);
        break;

    case ITELEX_ONLINE_STATE:
        if (!client.connected() && client.available() < 2)
        {
            debug_log("Remote peer closed connection, disconnecting", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            itelex_hangup();
            break;
        }

        if (last_rxed_ack + 30000000 < esp_timer_get_time())
        {
            debug_log("Peer timeout, disconnecting", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            itelex_hangup();
            break;
        }

        // Give the teletype a 5.0-second grace period after going online
        // for motor spin-up and loop current stabilization before arming ST break detection
        if (esp_timer_get_time() > online_start_time + 5000000)
        {
            if (!virtual_terminal_active && is_line_break_detected(400000))
            {
                debug_log("ST break detected (>400ms): current=" + String(get_line_current(), 2) + " mA (raw: " + String(get_raw_line_current(), 2) + " mA), TTY_RX=" + String(digitalRead(TTY_RX_PIN)) + ", disconnecting", 1, UART_LOG_MASK | TELNET_LOG_MASK);
                itelex_hangup();
            }
        }
        else
        {
            reset_line_break_timer();
        }

        break;

    default:
        break;
    } // end of main I-Telex SM

    if ((iTelexState == ITELEX_ONLINE_STATE) && (esp_timer_get_time() > (ack_timer + 1000000)))
    {                                     // one sec timer
        ack_timer = esp_timer_get_time(); // reset one sec timer
        sendACK();                        // send acknowledge iTx-command
    }

    if (TTYavailable())
    { // new character received from 50 baud teletype receiver
        int raw = TTYread();
        if (raw >= 0 && raw != 0xFF)
        {
            char c = flipBaudotOrder((uint8_t)raw & 0x1F);
            if (c != 0) // Ignore idle/null
            {
                if (tty_is_echo((uint8_t)c))
                {
                    // Discard local loopback echo of what ESP32 transmitted to TTY
                }
                else
                {
                    char ascii_char = baudotToAscii(c);
                    if (ascii_char != BAUDOT_CHAR_UNKNOWN && ascii_char != '\016' && ascii_char != '\017' && ascii_char != BAUDOT_MODE_BU && ascii_char != BAUDOT_MODE_ZI && ascii_char != 0)
                    {
                        Serial.print(ascii_char);
                        if (telnet.isConnected())
                        {
                            telnet.print(ascii_char);
                        }
                    }
                    if (c != BAUDOT_CHAR_UNKNOWN)
                    {
                        if (client.connected())
                        {
                            debug_log("[TX TTY]: raw=0x" + String(raw, HEX) + " code=" + String((uint8_t)c) + " ('" + String(ascii_char) + "')", 1, UART_LOG_MASK | TELNET_LOG_MASK);
                            sendBaudot(c);
                        }
                    }
                }
            }
        }
    } // end new character received

    if (iTelexState != ITELEX_IDLE_STATE && iTelexState != ITELEX_LOCAL_STATE && iTelexState != ITELEX_DIALING_STATE && iTelexState != ITELEX_DISCONNECT_STATE)
    {
        uint8_t cmd = ReceiveTCPIPCommand();
        if (cmd != ITLX_NOCOMMAND_CMD && cmd != ITLX_NOTCONNECTED_CMD)
        {
            iTelexCommandParser(IamServer);
            if (ConnectionState == CON_ESTABLISHED)
            {
                // PrintBD_DateTime();
                ConnectionState = CON_ESTABLISHED_TIME_SENT;
                sendACK();
                ack_timer = esp_timer_get_time();
            }
        }
        else if (iTelexState != ITELEX_CONNECTING_STATE && iTelexState != ITELEX_IDLE_STATE && !client.connected() && client.available() < 2)
        {
            debug_log("Remote peer closed connection, disconnecting", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            itelex_hangup();
        }
    }

    if (iTelexState == ITELEX_ONLINE_STATE)
    {                               // Baudot Data in Buffer to be sent to TTY
        if (BDBufferRDPTR != BDBufferWRPTR)
        {
            if (virtual_terminal_active || (TXFIFO_available() > 0))
            {
                if (getbuffer(&BDData))
                {
                    if (!virtual_terminal_active)
                    {
                        tty_register_transmitted_echo(BDData);
                        tty_write_raw(flipBaudotOrder(BDData));
                    }
                    CharSent++;
                    totalBytesReceived++; // Count character as processed for ACK flow control
                    char c = baudotToAscii(BDData);
                    if (c != BAUDOT_CHAR_UNKNOWN && c != '\016' && c != '\017' && c != BAUDOT_MODE_BU && c != BAUDOT_MODE_ZI && c != 0)
                    {
                        Serial.print(c);
                        if (telnet.isConnected())
                        {
                            telnet.print(c);
                        }
                    }
                }
            }
        }
    } // End: Baudot Data in Buffer to be sent to TTY
}