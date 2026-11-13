#include "header.hpp"

INA226 INA(INA226_ADDRESS);

void init_current()
{
    if (!INA.begin())
    {
        DEBUG_UART.printf("INA226 was not found at address 0x%X, please check your address and connections.\n\r", INA226_ADDRESS);
    }
    INA.setMaxCurrentShunt(0.15, INA226_SHUNT_RESISTOR);
    DEBUG_UART.printf("Current: %fmA\n\r", INA.getCurrent_mA());
}

// returns 0 if the current is <10mA, otherwise it returns 1
int get_CL_amplitude()
{
    float current = fabs(INA.getCurrent_mA());
    if (current < 10.0f)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

float get_line_current()
{
    return fabs(INA.getCurrent_mA());
}

float get_raw_line_current()
{
    return INA.getCurrent_mA();
}

uint8_t historic_line_states[NUM_OF_CURRENT_STATE_SAMPLES];
uint64_t last_current_state_time = 0;
void handle_current_sampling()
{
    if (esp_timer_get_time() > (last_current_state_time + CURRENT_STATE_SAMPLE_RATE_us))
    {
        for (int i = NUM_OF_CURRENT_STATE_SAMPLES - 1; i > 0; i--)
        {
            historic_line_states[i] = historic_line_states[i - 1];
        }
        historic_line_states[0] = get_CL_amplitude();
        last_current_state_time = esp_timer_get_time();
    }
}

int get_last_1s_current()
{
    int _num_of_samples = (1000000 / CURRENT_STATE_SAMPLE_RATE_us);
    int _statesum = 0;
    for (int i = 0; i < _num_of_samples; i++)
    {
        _statesum = _statesum + historic_line_states[i];
    }
    if (_statesum < _num_of_samples / 5)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

int get_last_3s_current()
{
    int _num_of_samples = (3000000 / CURRENT_STATE_SAMPLE_RATE_us);
    int _statesum = 0;
    for (int i = 0; i < _num_of_samples; i++)
    {
        _statesum = _statesum + historic_line_states[i];
    }
    if (_statesum < _num_of_samples / 5)
    {
        return 0;
    }
    else
    {
        return 1;
    }
}

int fill_last_current(uint8_t value)
{
    for (int i = 0; i < 256; i++)
    {
        historic_line_states[i] = value;
    }
    return 1;
}

static uint64_t last_line_mark_time = 0;

void reset_line_break_timer()
{
    last_line_mark_time = esp_timer_get_time();
}

bool is_line_break_detected(uint32_t break_threshold_us)
{
    // Fast hardware pin check on TTY_RX_PIN (GPIO 4)
    // HIGH means current is flowing (Mark / closed loop)
    // LOW means loop is broken or in Space state (0 mA)
    if (digitalRead(TTY_RX_PIN) == HIGH)
    {
        last_line_mark_time = esp_timer_get_time();
        return false;
    }

    uint64_t now = esp_timer_get_time();
    if (last_line_mark_time == 0)
    {
        last_line_mark_time = now;
        return false;
    }

    // Check if the pin has been continuously LOW for >= break_threshold_us
    if ((now - last_line_mark_time) >= break_threshold_us)
    {
        // Double check with INA226 current measurement to confirm loop is open (< 10mA)
        if (get_line_current() < 10.0f)
        {
            return true;
        }
        else
        {
            // Current is actually flowing, reset timer
            last_line_mark_time = now;
            return false;
        }
    }
    return false;
}
