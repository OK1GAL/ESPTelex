#include "header.hpp"

int current_PD_state = PD_IDLE;

uint64_t last_PD_sample_us = 0;
uint8_t current_number = 0;

uint64_t last_PD_digit_end = 0;

uint8_t outgoing_telex_number_array[16];
int current_telex_number_place = 0;

void init_rotary_dial_handler()
{
    current_PD_state = PD_IDLE;
    last_PD_sample_us = 0;
    current_number = 0;
    last_PD_digit_end = 0;
    clear_PD_telex_number();
    current_telex_number_place = 0;
}

void rotary_dial_handler()
{
    if (current_PD_state == PD_IDLE)
    {
        if (get_CL_RX_STATE() == 0)
        {
            last_PD_sample_us = esp_timer_get_time() + PD_SAMPLE_OFFSET;
            for (int i = 0; i < 16; i++)
            {
                outgoing_telex_number_array[i] = 0xff;
            }
            current_telex_number_place = 0;
            current_number = 1;
            current_PD_state = PD_DIALING_DIGIT;
            // DEBUG_UART.printf("Going to PD_DIALING_DIGIT\n\r");
        }
    }
    else if (current_PD_state == PD_DIALING_DIGIT)
    {
        if (esp_timer_get_time() > last_PD_sample_us + PD_SAMPLE_DELAY)
        {
            last_PD_sample_us = esp_timer_get_time();
            if (get_CL_RX_STATE() == 0)
            {
                current_number++;
                if (current_number > 10)
                {
                    current_PD_state = PD_IDLE;
                }
            }
            else
            {
                if (current_telex_number_place < 16)
                {
                    outgoing_telex_number_array[current_telex_number_place] = current_number;
                    current_telex_number_place++;
                }
                current_PD_state = PD_WAITING_FOR_DIGIT;
                DEBUG_UART.printf("Last digit: %d\n\r", current_number);
                // DEBUG_UART.printf("Going to PD_WAITING_FOR_DIGIT\n\r");
                last_PD_digit_end = esp_timer_get_time();
            }
        }
    }
    else if (current_PD_state == PD_WAITING_FOR_DIGIT)
    {
        if (get_CL_RX_STATE() == 0)
        {
            last_PD_sample_us = esp_timer_get_time() + PD_SAMPLE_OFFSET;
            current_number = 1;
            current_PD_state = PD_DIALING_DIGIT;
            // DEBUG_UART.printf("Going to PD_DIALING_DIGIT\n\r");
        }

        if (esp_timer_get_time() > last_PD_digit_end + PD_NUMBER_TIMEOUT ||
            current_telex_number_place >= 16)
        {
            // for (int i = 0; outgoing_telex_number_array[i] != 0xff; i++)
            //{
            //     DEBUG_UART.printf("%d,", outgoing_telex_number_array[i]);
            // }
            // DEBUG_UART.printf("\n\r");
            current_telex_number_place = 0;
            current_number = 0;
            current_PD_state = PD_IDLE;
            // DEBUG_UART.printf("Going to PD_IDLE\n\r");
        }
    }
    else
    {
        DEBUG_UART.printf("PD SM exception. Going to PD_IDLE\n\r");
        current_PD_state = PD_IDLE;
    }
}

uint32_t get_PD_telex_number()
{
    if (current_PD_state == PD_IDLE)
    {
        if (outgoing_telex_number_array[0] == 0xff)
        {
            return 0xffffffff;
        }
        uint32_t _number = 0;
        uint8_t numofdigits = 0;
        for (int i = 0; i < 16 && outgoing_telex_number_array[i] != 0xff; i++)
        {
            numofdigits++;
        }
        if (numofdigits == 0)
        {
            return 0xffffffff;
        }
        numofdigits--;
        for (int i = 0; i < 16 && outgoing_telex_number_array[i] != 0xff; i++)
        {
            if (outgoing_telex_number_array[i] <= 10)
            {
                if (outgoing_telex_number_array[i] == 10)
                {
                    _number = _number + (0 * pow(10, numofdigits - i));
                }
                else
                {
                    _number = _number + (outgoing_telex_number_array[i] * pow(10, numofdigits - i));
                }
            }
            else
            {
                return 0xffffffff;
            }
        }
        // DEBUG_UART.printf("Returned telex number: %d",_number);
        return _number;
    }
    else
    {
        return 0xffffffff;
    }
}

void clear_PD_telex_number()
{
    for (int i = 0; i < 16; i++)
    {
        outgoing_telex_number_array[i] = 0xff;
    }
}