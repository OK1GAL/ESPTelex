#include "header.hpp"

WiFiClient localClient;


void outbuf_clear()
{
    for (int i = 0; i < 256; i++)
    {
        outgoing_payload[i] = 0;
    }
    outgoing_length = 0;
}

void outbuf_append_byte(uint8_t _byte)
{
    outgoing_payload[outgoing_length] = _byte;
    outgoing_length++;
}

uint64_t server_timeout_timer = 0;
// query subscriber info from the nameserver
// returns:
// 0x00: client cot connected
// 0x01: query successfull
// 0x04: peer not found (subscriber doesn't exist)
// 0xfe: server timeout
// 0xff: unknown response
uint8_t itelex_peer_query(uint32_t _call_number, itelex_subsciber &_subscriber)
{
    printf("\n\rQuerying subscriber number: %lu\n\r", _call_number);
    notify_network_activity(300);
    notify_itelex_activity(300);
    if (localClient.connect(NAMESERVER_3_HOSTNAME, NAMESERVER_PORT))
    {
        setTCALED(TCA_ITELEX_LED_B, 1);
        DEBUG_UART.printf("Nameserver client connected\n\r");
    }
    else
    {
        localClient.stop();
        DEBUG_UART.printf("Nameserver client not connected\n\r");
        setTCALED(TCA_ITELEX_LED_B, 0);
        return 0;
    }
    outbuf_clear();
    outbuf_append_byte(0x03);                       // packet type peer_query
    outbuf_append_byte(0x05);                       // length
    outbuf_append_byte((_call_number >> 0) & 0xff); // call number LSB
    outbuf_append_byte((_call_number >> 8) & 0xff);
    outbuf_append_byte((_call_number >> 16) & 0xff);
    outbuf_append_byte((_call_number >> 24) & 0xff); // call number MSB
    outbuf_append_byte(0x01);                        // version
    outgoing_payload[1] = outgoing_length - 2;       // modify the length byte and substract the first two bytes

    localClient.write(outgoing_payload, outgoing_length);
    /* DEBUG_UART.printf("Outgoing data:\n\r");
    for (int i = 0; i < 256; i++)
    {
        DEBUG_UART.printf("%.2X ", outgoing_payload[i]);
        if (((i + 1) % 20 == 0))
        {
            DEBUG_UART.printf("\n\r");
        }
    }
    DEBUG_UART.printf("\n\r"); */

    server_timeout_timer = esp_timer_get_time();
    while ((!localClient.available()) && (esp_timer_get_time() < server_timeout_timer + NAMESERVER_TIMEOUT_us))
    {
        delay(1);
    }
    if ((esp_timer_get_time() > server_timeout_timer + NAMESERVER_TIMEOUT_us) || (localClient.available() < 2))
    {
        DEBUG_UART.printf("Nameserver timeout\n\r");
        localClient.stop();
        setTCALED(TCA_ITELEX_LED_B, 0);
        return 0xfe;
    }

    incoming_payload[0] = localClient.read();
    incoming_payload[1] = localClient.read();

    if (incoming_payload[0] == 0x04) // peer not found
    {
        localClient.stop();
        setTCALED(TCA_ITELEX_LED_B, 0);
        return 4;
    }
    else if (incoming_payload[0] == 0x05) // peer reply V1
    {                                     /*do nothing, continue*/
    }
    else if (incoming_payload[0] == 0xff) // peer reply V1
    {                                     /*do nothing, continue*/
        DEBUG_UART.printf("NAMESERVER ERROR!\n\r");
        uint64_t err_timeout = esp_timer_get_time() + 500000;
        for (int i = 0; i < incoming_payload[1] && i < 250; i++)
        {
            while (!localClient.available() && esp_timer_get_time() < err_timeout)
            {
                delay(1);
            }
            if (localClient.available())
            {
                DEBUG_UART.printf("%c", (char)localClient.read());
            }
        }
        DEBUG_UART.printf("\n\r");
        localClient.stop();
        setTCALED(TCA_ITELEX_LED_B, 0);
        return 0xff;
    }
    else
    {
        localClient.stop();
        setTCALED(TCA_ITELEX_LED_B, 0);
        return 0xff;
    }

    incoming_length = 0;
    uint8_t expected_len = incoming_payload[1];
    if (expected_len > 250)
    {
        expected_len = 250;
    }
    uint64_t payload_timeout = esp_timer_get_time() + 1000000;
    for (int i = 0; i < expected_len; i++)
    {
        while (!localClient.available() && esp_timer_get_time() < payload_timeout)
        {
            delay(1);
        }
        if (!localClient.available())
        {
            break;
        }
        incoming_payload[i + 2] = localClient.read();
        incoming_length++;
    }
    localClient.stop();
    /* DEBUG_UART.printf("Incomming data:\n\r");
    for (int i = 0; i < 256; i++)
    {
        DEBUG_UART.printf("%.2X ", incoming_payload[i]);
        if (((i + 1) % 20 == 0))
        {
            DEBUG_UART.printf("\n\r");
        }
    } */
    /* DEBUG_UART.printf("\n\r");
    DEBUG_UART.printf("Returned length %d  Actual length %d\n\r", incoming_payload[1], incoming_length);*/
    _subscriber.call_number = uint32_t(incoming_payload[2]) | (uint32_t(incoming_payload[3]) << 8) | (uint32_t(incoming_payload[4]) << 16) | (uint32_t(incoming_payload[5]) << 24);
    int _i = 0;
    while ((_i < 39) && (incoming_payload[_i + 6] > 31))
    {
        _subscriber.name[_i] = char(incoming_payload[_i + 6]);
        _i++;
    }
    while (_i > 0 && (_subscriber.name[_i - 1] == ' ' || _subscriber.name[_i - 1] == '\r'))
    {
        _i--;
    }
    _subscriber.name[_i] = 0;
    _subscriber.flags = uint16_t(incoming_payload[46]) | (uint16_t(incoming_payload[47]) << 8);
    _subscriber.connection_type = incoming_payload[48];
    _i = 0;
    while ((_i < 39) && (incoming_payload[_i + 49] > 31))
    {
        _subscriber.hostname[_i] = char(incoming_payload[_i + 49]);
        _i++;
    }
    while (_i > 0 && (_subscriber.hostname[_i - 1] == ' ' || _subscriber.hostname[_i - 1] == '\r'))
    {
        _i--;
    }
    _subscriber.hostname[_i] = 0;
    for (int i = 0; i < 4; i++)
    {
        _subscriber.IP_address[i] = incoming_payload[89 + i];
    }
    _subscriber.IP_address32 = uint32_t(incoming_payload[89]) | (uint32_t(incoming_payload[90]) << 8) | (uint32_t(incoming_payload[91]) << 16) | (uint32_t(incoming_payload[92]) << 24);
    _subscriber.IP = IPAddress(_subscriber.IP_address[0], _subscriber.IP_address[1], _subscriber.IP_address[2], _subscriber.IP_address[3]);
    _subscriber.port = uint16_t(incoming_payload[93]) | (uint16_t(incoming_payload[94]) << 8);
    _subscriber.extension = incoming_payload[95];
    _subscriber.pin = uint16_t(incoming_payload[96]) | (uint16_t(incoming_payload[97]) << 8);
    _subscriber.timestamp = uint32_t(incoming_payload[98]) | (uint32_t(incoming_payload[99]) << 8) | (uint32_t(incoming_payload[100]) << 16) | (uint32_t(incoming_payload[101]) << 24);
    setTCALED(TCA_ITELEX_LED_B, 0);
    return 1;
}
