#include <Arduino.h>

// =====================================================
// Casper YKR-H/102E - Frame Builder
// =====================================================

class CasperAC
{
public:

    // -------------------------------------------------
    // ENUM
    // -------------------------------------------------

    enum Mode
    {
        MODE_AUTO,
        MODE_COOL,
        MODE_DRY,
        MODE_HEAT
    };

    enum Fan
    {
        FAN_AUTO,
        FAN_LOW,
        FAN_MID,
        FAN_HIGH
    };

    // -------------------------------------------------
    // STATE
    // -------------------------------------------------

    struct State
    {
        bool power = true;

        uint8_t temperature = 26;

        Mode mode = MODE_COOL;

        Fan fan = FAN_AUTO;

        bool swing = false;

        bool timerEnabled = false;
        uint8_t timerHour = 0;
        bool timerHalfHour = false;
    };

private:

    State state;

    // =================================================
    // BIT REVERSE
    // =================================================

    uint8_t reverseBits8(uint8_t x)
    {
        x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);
        x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
        x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1);

        return x;
    }

    // =================================================
    // TEMPERATURE
    // =================================================

    uint8_t encodeTemperature(uint8_t temperature)
    {
        if (temperature < 20)
            temperature = 20;

        if (temperature > 30)
            temperature = 30;

        uint8_t value =
            0x67 + (temperature - 20) * 8;

        return reverseBits8(value);
    }

    // =================================================
    // FAN
    // =================================================

    uint8_t encodeFan(Fan fan)
    {
        switch (fan)
        {
            case FAN_AUTO:
                return 0x05;

            case FAN_LOW:
                return 0x06;

            case FAN_MID:
                return 0x02;

            case FAN_HIGH:
                return 0x04;
        }

        return 0x05;
    }

    // =================================================
    // MODE
    // =================================================

    uint8_t encodeMode(Mode mode)
    {
        switch (mode)
        {
            case MODE_COOL:
                return 0x04;

            case MODE_DRY:
                return 0x02;

            case MODE_HEAT:
                return 0x01;

            case MODE_AUTO:
                return 0x00;
        }

        return 0x04;
    }

    // =================================================
    // CHECKSUM
    // =================================================

    uint8_t calculateChecksum(uint8_t frame[13])
    {
        uint16_t sum = 0;

        for (int i = 0; i < 12; i++)
        {
            sum += reverseBits8(frame[i]);
        }

        return reverseBits8(sum & 0xFF);
    }

public:

    // =================================================
    // GET STATE
    // =================================================

    State& getState()
    {
        return state;
    }

    // =================================================
    // BUILD NORMAL FRAME
    // =================================================

    void buildFrame(uint8_t frame[13])
    {
        memset(frame, 0, 13);

        // ---------------------------------------------
        // CONSTANT
        // ---------------------------------------------

        frame[0] = 0xC3;

        frame[2] = 0x07;

        frame[3] = 0x00;

        frame[7] = 0x00;

        frame[8] = 0x00;

        frame[10] = 0x00;

        // ---------------------------------------------
        // TEMPERATURE
        // ---------------------------------------------

        frame[1] =
            encodeTemperature(state.temperature);

        // ---------------------------------------------
        // FAN
        // ---------------------------------------------

        frame[4] =
            encodeFan(state.fan);

        // ---------------------------------------------
        // TIMER
        // ---------------------------------------------

        frame[5] = 0x00;

        // ---------------------------------------------
        // MODE
        // ---------------------------------------------

        frame[6] =
            encodeMode(state.mode);

        // ---------------------------------------------
        // POWER
        // ---------------------------------------------

        frame[9] =
            state.power ? 0x04 : 0x00;

        // ---------------------------------------------
        // DEFAULT COMMAND
        // ---------------------------------------------

        frame[11] = 0xA0;

        // ---------------------------------------------
        // CHECKSUM
        // ---------------------------------------------

        frame[12] =
            calculateChecksum(frame);
    }

    // =================================================
    // PRINT FRAME
    // =================================================

    void printFrame(uint8_t frame[13])
    {
        Serial.println();

        Serial.println("========== CASPER FRAME ==========");

        for (int i = 0; i < 13; i++)
        {
            if (frame[i] < 0x10)
                Serial.print("0");

            Serial.print(frame[i], HEX);

            if (i < 12)
                Serial.print(" ");
        }

        Serial.println();

        Serial.println("==================================");
    }
};


// =====================================================
// GLOBAL
// =====================================================

CasperAC ac;


// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);

    uint8_t frame[13];

    ac.buildFrame(frame);

    ac.printFrame(frame);
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
}