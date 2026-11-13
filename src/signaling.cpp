/* #include "header.hpp"

// current state for the signaling state machine
// 0 = IDLE
int current_FSG_state = FSG_IDLE;

uint32_t dialed_number = 0xffffffff;

itelex_subsciber outgoing_subscriber;
uint8_t query_status = 0;


void handle_FSG_signaling_SM()
{
    switch (current_FSG_state)
    {
    case FSG_IDLE:
        FSG_idle_handler();
        break;
    case FSG_DIALING:
        FSG_dialing_handler();
        break;
    case FSG_CONNECTED:
        FSG_connected_handler();
        break;
    case FSG_DISCONNECTING:
        FSG_disconnecting_handler();
        break;
    default:
        current_FSG_state = FSG_IDLE;
        break;
    }
}

uint64_t time_idle_to_dial = 0;
int line_init_SM = 0;
uint64_t last_line_init_timer = 0;
void FSG_idle_handler()
{
    if (get_polarity() && get_last_1s_current())
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
                current_FSG_state = FSG_DIALING;
                init_rotary_dial_handler();
                time_idle_to_dial = esp_timer_get_time();
            }
            break;

        default:
            line_init_SM = 0;
            break;
        }
    }
}

void FSG_dialing_handler()
{
    if (esp_timer_get_time() > time_idle_to_dial + 100000)
    {
        rotary_dial_handler();
    }

    if (get_PD_telex_number() != 0xffffffff)
    {
        set_polarity(0);
        dialed_number = get_PD_telex_number();
        DEBUG_UART.printf("Dialed number: %lu\n\r", dialed_number);
        if (dialed_number > 9999)
        {
            query_status = itelex_peer_query(dialed_number, outgoing_subscriber);
            DEBUG_UART.printf("Nameserver query status: %d\n\r", query_status);
        }
        else
        {
            DEBUG_UART.printf("Number too short for i-telex: %lu\n\r", dialed_number);
        }
        current_FSG_state = FSG_CONNECTED;
        clear_PD_telex_number();
        // TTY.flush(SC16IS752_CHANNEL_A);
    }
    if (!get_last_1s_current())
    {
        set_polarity(1);
        current_FSG_state = FSG_IDLE;
    }
}

int line_refuse_SM = 0;
uint64_t last_line_refuse_timer = 0;
void FSG_connected_handler()
{
    if (!get_last_1s_current())
    {
        set_polarity(1);
        current_FSG_state = FSG_IDLE;
    }
    if (dialed_number == 0)
    {
        delay(2000);
        // print time and end connection
        update_time();
        sprintf(sprintf_buf, "%d:%d:%d  %d.%d. %d\n\r", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);
        tty_print_ascii(sprintf_buf);
        tty_print_ascii("    ");
        // tty_wait_until_THR_empty();
        // delay(1000);
        set_polarity(1);
        current_FSG_state = FSG_IDLE;
    }
    else if (dialed_number <= 9999)
    {
        uint8_t sdret = SD_load_directory(dialed_number, outgoing_subscriber);
        if (sdret == 1)
        {
            debug_log("Dialed number OK", 1, UART_LOG_MASK | TELNET_LOG_MASK);
            debug_log("Number: " + String(outgoing_subscriber.call_number), 1, UART_LOG_MASK | TELNET_LOG_MASK);
            debug_log("Name: " + String(outgoing_subscriber.name), 1, UART_LOG_MASK | TELNET_LOG_MASK);
            debug_log("Hostname: " + String(outgoing_subscriber.hostname), 1, UART_LOG_MASK | TELNET_LOG_MASK);
            debug_log("IP: " + String(outgoing_subscriber.IP_address[0]) + ":" + String(outgoing_subscriber.IP_address[1]) + ":" + String(outgoing_subscriber.IP_address[2]) + ":" + String(outgoing_subscriber.IP_address[3]), 1, UART_LOG_MASK | TELNET_LOG_MASK);
            debug_log("Port: " + String(outgoing_subscriber.port), 1, UART_LOG_MASK | TELNET_LOG_MASK);
            debug_log("Timestamp: " + String(outgoing_subscriber.timestamp), 1, UART_LOG_MASK | TELNET_LOG_MASK);
            debug_log("IP: " + outgoing_subscriber.IP.toString(), 1, UART_LOG_MASK | TELNET_LOG_MASK);
        }
        else
        {
            debug_log("Subscriber file error: " + String(sdret), 1, UART_LOG_MASK | TELNET_LOG_MASK | SD_CARD_LOG_MASK);
        }
        set_polarity(1);
        current_FSG_state = FSG_IDLE;
    }
    else
    {
        if ((query_status == 1) && (dialed_number > 9999))
        {
            switch (line_refuse_SM)
            {
            case 0:
                set_polarity(0);
                last_line_refuse_timer = esp_timer_get_time();
                line_refuse_SM = 1;
                break;
            case 1:
                if (esp_timer_get_time() > last_line_refuse_timer + 1000000) // 1000ms to get ready
                {
                    set_polarity(1);
                    current_FSG_state = FSG_IDLE;
                    last_line_refuse_timer = esp_timer_get_time();
                    line_refuse_SM = 0;
                    }
                break;

            default:
                line_refuse_SM = 0;
                break;
            }
        }
        else
        {
            switch (line_refuse_SM)
            {
            case 0:
                set_polarity(0);
                last_line_refuse_timer = esp_timer_get_time();
                line_refuse_SM = 1;
                break;
            case 1:
                if (esp_timer_get_time() > last_line_refuse_timer + 1000000) // 1000ms to get ready
                {
                    set_polarity(1);
                    current_FSG_state = FSG_IDLE;
                    last_line_refuse_timer = esp_timer_get_time();
                    line_refuse_SM = 0;
                }
                break;

            default:
                line_refuse_SM = 0;
                break;
            }
        }
    }
}

void FSG_disconnecting_handler()
{
}
 */