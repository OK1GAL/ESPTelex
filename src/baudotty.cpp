#include "header.hpp"

char ascii2baudot[130] = {0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B0110100, 0B1000000, 0B1000000, 0B0101011, 0B1000000, 0B1000100,
                          0B1000010, 0B1000000, 0B1000010, 0B1001000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000,
                          0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000,
                          0B1000000, 0B1000000, 0B1000100, 0B0101101, 0B0100101, 0B1000000, 0B0101001, 0B1000000, 0B0111010, 0B0100101,
                          0B0101111, 0B0110010, 0B1000000, 0B1000000, 0B0101100, 0B0100011, 0B0111100, 0B0111101, 0B0110110, 0B0110111,
                          0B0110011, 0B0100001, 0B0101010, 0B0110000, 0B0110101, 0B0100111, 0B0100110, 0B0111000, 0B0101110, 0B0111110,
                          0B1000000, 0B1000000, 0B1000000, 0B0111001, 0B1000000, 0B0000011, 0B0011001, 0B0001110, 0B0001001, 0B0000001,
                          0B0001101, 0B0011010, 0B0010100, 0B0000110, 0B0001011, 0B0001111, 0B0010010, 0B0011100, 0B0001100, 0B0011000,
                          0B0010110, 0B0010111, 0B0001010, 0B0000101, 0B0010000, 0B0000111, 0B0011110, 0B0010011, 0B0011101, 0B0010101,
                          0B0010001, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B0000011, 0B0011001, 0B0001110,
                          0B0001001, 0B0000001, 0B0001101, 0B0011010, 0B0010100, 0B0000110, 0B0001011, 0B0001111, 0B0010010, 0B0011100,
                          0B0001100, 0B0011000, 0B0010110, 0B0010111, 0B0001010, 0B0000101, 0B0010000, 0B0000111, 0B0011110, 0B0010011,
                          0B0011101, 0B0010101, 0B0010001, 0B1000000, 0B1000000, 0B1000000, 0B1000000, 0B1011111, 0B1011111, 0B1011011};

SC16IS752 TTY = SC16IS752(SC16IS750_PROTOCOL_I2C, SC16IS750_ADDRESS_BB);

uint8_t baudot_recieving_mode = BAUDOT_MODE_UNDEFINED;
uint8_t baudot_sending_mode = BAUDOT_MODE_UNDEFINED;

char baudotToAscii(uint8_t baudot)
{
    baudot_sending_mode = BAUDOT_MODE_UNDEFINED;

    if (baudot > 31)
    {
        return BAUDOT_CHAR_UNKNOWN;
    }

    if (baudot == BAUDOT_MODE_ZI)
    {
        baudot_recieving_mode = BAUDOT_MODE_ZI;
        // Serial.print("<ZI>");
    }
    else if (baudot == BAUDOT_MODE_BU)
    {
        baudot_recieving_mode = BAUDOT_MODE_BU;
        // Serial.print("<BU>");
    }

    if (baudot_recieving_mode == BAUDOT_MODE_BU)
    {
        return baudot_bu[baudot];
    }
    if (baudot_recieving_mode == BAUDOT_MODE_ZI)
    {
        return baudot_zi[baudot];
    }
    return BAUDOT_CHAR_UNKNOWN;
}

void init_tty()
{
    digitalWrite(EXT_UART_NRST_PIN, 1);
    TTY.begin(TTY_BAUDRATE_A, TTY_BAUDRATE_B); // baudrate setting
    if (TTY.ping() != 1)
    {
        DEBUG_UART.printf("SC16IS752 device not found\n\r");
    }
    else
    {
        DEBUG_UART.printf("SC16IS752 device found\n\r");
        TTY.SetLine(SC16IS752_CHANNEL_A, 5, 0, 2);
        TTY.EnableEnhancedFunctions(SC16IS752_CHANNEL_A);
        TTY.InterruptControl(SC16IS752_CHANNEL_A, SC16IS750_INT_RHR);
        TTY.InterruptControl(SC16IS752_CHANNEL_B, 0b00000000);
    }
}

bool tty_is_transmitting = false;
uint8_t lastTXLVL = 64;
uint8_t currentTXLVL = 64;
static uint64_t last_tx_start_time = 0;
static uint64_t temt_first_seen_time = 0;

// ==========================================
// Software Loopback Echo Cancellation Queue
// ==========================================
#define ECHO_QUEUE_SIZE 32
static uint8_t echo_queue[ECHO_QUEUE_SIZE];
static uint64_t echo_timestamp[ECHO_QUEUE_SIZE];
static uint8_t echo_head = 0;
static uint8_t echo_tail = 0;

void tty_register_transmitted_echo(uint8_t code)
{
    uint8_t next = (echo_head + 1) % ECHO_QUEUE_SIZE;
    if (next != echo_tail)
    {
        echo_queue[echo_head] = code & 0x1F;
        echo_timestamp[echo_head] = esp_timer_get_time();
        echo_head = next;
    }
}

bool tty_is_echo(uint8_t code)
{
    uint64_t now = esp_timer_get_time();

    // Drop stale echoes older than 1.5 seconds
    while (echo_tail != echo_head && (now - echo_timestamp[echo_tail] > 1500000))
    {
        echo_tail = (echo_tail + 1) % ECHO_QUEUE_SIZE;
    }

    if (echo_tail == echo_head)
    {
        // If transmitter is still actively pulsing line, received char is an echo
        return tty_is_transmitting;
    }

    // Direct match at head of echo queue
    if (echo_queue[echo_tail] == (code & 0x1F))
    {
        echo_tail = (echo_tail + 1) % ECHO_QUEUE_SIZE;
        return true;
    }

    // Check next entry in case of a dropped shift code
    uint8_t next = (echo_tail + 1) % ECHO_QUEUE_SIZE;
    if (next != echo_head && echo_queue[next] == (code & 0x1F))
    {
        echo_tail = (next + 1) % ECHO_QUEUE_SIZE;
        return true;
    }

    // If still transmitting physically, treat as echo
    return tty_is_transmitting;
}

void tty_clear_echo_queue()
{
    echo_head = 0;
    echo_tail = 0;
}

uint8_t TTY_transmitter_empty()
{
    // Read Line Status Register (LSR)
    // Bit 6 = TEMT (Transmitter Empty: both THR FIFO and TSR Shift Register empty)
    uint8_t lsr = TTY.ReadRegister(SC16IS752_CHANNEL_A, SC16IS750_REG_LSR);
    return (lsr & 0x40) ? 1 : 0;
}

int lastchar = 0;
void TTYTXletter(uint8_t letter, uint8_t forcechange)
{
    if (letter > 127)
    {
        return;
    }
    if (!tty_is_transmitting)
    {
        TTY.EnableReceive(SC16IS752_CHANNEL_A, 0);
        tty_is_transmitting = true;
    }
    last_tx_start_time = esp_timer_get_time();
    temt_first_seen_time = 0;

    int chartype = ((ascii2baudot[letter] >> 5) & 0x01); // check if current char is letter or figure
    if ((chartype != lastchar) || forcechange)
    {
        uint8_t shift_code = ascii2baudot[128 + chartype] & 0b11111;
        tty_register_transmitted_echo(shift_code);
        TTY.write(SC16IS752_CHANNEL_A, shift_code | 0b100000);
        lastchar = chartype;
    }
    uint8_t char_code = ascii2baudot[letter] & 0b11111;
    tty_register_transmitted_echo(char_code);
    TTY.write(SC16IS752_CHANNEL_A, char_code | 0b100000);
    lastTXLVL = 63;
}

void tty_print_ascii(const char *_text)
{
    if (_text == nullptr)
    {
        return;
    }
    int textsize = strlen(_text);
    if (textsize == 0)
    {
        return;
    }
    TTYTXletter(_text[0], 1);
    for (int i = 1; i < textsize; i++)
    {
        TTYTXletter(_text[i], 0);
    }
}

void tty_write_raw(uint8_t _data)
{
    if (!tty_is_transmitting)
    {
        TTY.EnableReceive(SC16IS752_CHANNEL_A, 0);
        tty_is_transmitting = true;
    }
    last_tx_start_time = esp_timer_get_time();
    temt_first_seen_time = 0;
    TTY.write(SC16IS752_CHANNEL_A, _data);
    lastTXLVL = 63;
}

void tty_wait_until_THR_empty()
{
    TTY.InterruptControl(SC16IS752_CHANNEL_A, SC16IS750_INT_THR | SC16IS750_INT_RHR);
    uint64_t wait_start = esp_timer_get_time();
    while (digitalRead(EXT_UART_NIRQ_PIN) && (esp_timer_get_time() - wait_start < 500000))
    {
        delay(1);
    }
    TTY.InterruptControl(SC16IS752_CHANNEL_A, SC16IS750_INT_RHR);
}

char c = 0;
uint8_t lastSMState = 0xff;
void handle_tty_phy()
{
    uint64_t now = esp_timer_get_time();

    if (tty_is_transmitting)
    {
        // At 50 baud, a 7.5-bit character takes 150ms.
        // Wait at least 140ms before polling transmitter empty status.
        if (now - last_tx_start_time > 140000)
        {
            if (TTY_transmitter_empty())
            {
                if (temt_first_seen_time == 0)
                {
                    temt_first_seen_time = now;
                }
                // Hold off for 60ms after TEMT is true to allow line receiver to finish stop bit
                else if (now - temt_first_seen_time > 60000)
                {
                    TTY.FIFOReset(SC16IS752_CHANNEL_A, 1); // Reset hardware RX FIFO
                    TTYRXflush();
                    TTY.EnableReceive(SC16IS752_CHANNEL_A, 1);
                    tty_is_transmitting = false;
                    temt_first_seen_time = 0;
                    lastTXLVL = 64;
                }
            }
            else
            {
                temt_first_seen_time = 0;
            }
        }
    }

    if (lastSMState != iTelexState)
    {
        switch (iTelexState)
        {
        case ITELEX_IDLE_STATE:
        case ITELEX_DIALING_STATE:
        case ITELEX_CONNECTING_STATE:
        case ITELEX_DISCONNECT_STATE:
            TTY.EnableReceive(SC16IS752_CHANNEL_A, 0);
            tty_is_transmitting = false;
            break;

        case ITELEX_CONNECTED_STATE:
        case ITELEX_ONLINE_STATE:
        case ITELEX_LOCAL_STATE:
            if (!tty_is_transmitting)
            {
                TTY.EnableReceive(SC16IS752_CHANNEL_A, 1);
            }
            break;

        default:
            break;
        }
        lastSMState = iTelexState;
    }
}

uint8_t TXFIFO_empty()
{
    return TTY_transmitter_empty();
}

void TTYRXflush()
{
    while (TTY.available(SC16IS752_CHANNEL_A))
    {
        TTY.read(SC16IS752_CHANNEL_A);
    }
}

void tty_reset_fifos()
{
    TTY.FIFOReset(SC16IS752_CHANNEL_A, 0); // Reset TX FIFO
    TTY.FIFOReset(SC16IS752_CHANNEL_A, 1); // Reset RX FIFO
    TTYRXflush();
}

// returns free space in tx fifo
uint8_t TXFIFO_available()
{
    return TTY.FIFOAvailableSpace(SC16IS752_CHANNEL_A);
}

uint8_t RXFIFO_available()
{
    return TTY.FIFOAvailableData(SC16IS752_CHANNEL_A);
}

uint8_t TTYavailable()
{
    if (tty_is_transmitting)
    {
        return 0;
    }
    // If IRQ pin is asserted (active LOW), data is ready
    if (digitalRead(EXT_UART_NIRQ_PIN) == LOW)
    {
        return TTY.available(SC16IS752_CHANNEL_A);
    }
    // Also poll every 15ms so single characters (below 8-byte FIFO trigger)
    // are picked up immediately without waiting for a 600ms timeout
    static uint64_t last_poll_time = 0;
    uint64_t now = esp_timer_get_time();
    if (now - last_poll_time >= 15000)
    {
        last_poll_time = now;
        return TTY.available(SC16IS752_CHANNEL_A);
    }
    return 0;
}

int TTYread()
{
    return TTY.read(SC16IS752_CHANNEL_A);
}

uint8_t flipBaudotOrder(uint8_t _data)
{
    uint8_t ret = 0;
    ret = ret | ((_data >> 4) & 1);
    ret = ret | ((_data >> 3) & 1) << 1;
    ret = ret | ((_data >> 2) & 1) << 2;
    ret = ret | ((_data >> 1) & 1) << 3;
    ret = ret | ((_data) & 1) << 4;
    return ret;
}