#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ============================================================
// HARDWARE
// ============================================================

#define IR_TX_PIN 16

IRsend irsend(IR_TX_PIN);

// ============================================================
// ELECTRA_AC
// 104 BIT / 13 BYTE
// ============================================================

#define FRAME_BYTES 13
#define FRAME_BITS 104

#define IR_FREQUENCY 38

#define HEADER_MARK   9050
#define HEADER_SPACE  4484

#define BIT_MARK      580

#define ZERO_SPACE    550
#define ONE_SPACE     1670

#define TRAILING_MARK 580

// 2 header + 104 * 2 + 1 trailing
#define RAW_LENGTH 211


// ============================================================
// MODE
// ============================================================

enum Mode
{
    MODE_AUTO,
    MODE_COOL,
    MODE_DRY,
    MODE_HEAT
};


// ============================================================
// FAN
// ============================================================

enum Fan
{
    FAN_AUTO,
    FAN_LOW,
    FAN_MID,
    FAN_HIGH
};


// ============================================================
// AC STATE
// ============================================================

struct ACState
{
    bool power;

    uint8_t temperature;

    Mode mode;

    Fan fan;

    bool swing;

    bool timerEnabled;

    uint8_t timerHour;

    bool timerHalfHour;
};


// ============================================================
// CURRENT STATE
// ============================================================

ACState ac =
{
    true,           // POWER
    26,             // TEMPERATURE

    MODE_COOL,      // MODE
    FAN_AUTO,       // FAN

    false,          // SWING

    false,          // TIMER
    0,
    false
};


// ============================================================
// REVERSE BITS
// ============================================================

uint8_t reverseBits8(uint8_t x)
{
    x = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4);
    x = ((x & 0xCC) >> 2) | ((x & 0x33) << 2);
    x = ((x & 0xAA) >> 1) | ((x & 0x55) << 1);

    return x;
}


// ============================================================
// TEMPERATURE ENCODING
// ============================================================

uint8_t encodeTemperature(uint8_t temp)
{
    switch (temp)
    {
        case 20: return 0xE6;
        case 21: return 0xF6;
        case 22: return 0xEE;
        case 23: return 0xFE;
        case 24: return 0xE1;
        case 25: return 0xF1;
        case 26: return 0xE9;
        case 27: return 0xF9;
        case 28: return 0xE5;
        case 29: return 0xF5;
        case 30: return 0xED;
    }

    // fallback 26C
    return 0xE9;
}


// ============================================================
// FAN ENCODING
// ============================================================

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


// ============================================================
// MODE ENCODING
// ============================================================

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


// ============================================================
// CHECKSUM
// ============================================================

uint8_t calculateChecksum(uint8_t frame[FRAME_BYTES])
{
    uint16_t sum = 0;

    for (int i = 0; i < 12; i++)
    {
        sum += reverseBits8(frame[i]);
    }

    return reverseBits8(sum & 0xFF);
}


// ============================================================
// BASE FRAME
// ============================================================

void buildBaseFrame(uint8_t frame[FRAME_BYTES])
{
    frame[0] = 0xC3;

    frame[1] = encodeTemperature(ac.temperature);

    frame[2] = 0x07;

    frame[3] = 0x00;

    frame[4] = encodeFan(ac.fan);

    frame[5] = 0x00;

    frame[6] = encodeMode(ac.mode);

    frame[7] = 0x00;

    frame[8] = 0x00;

    frame[9] = ac.power ? 0x04 : 0x00;

    frame[10] = 0x00;

    frame[11] = 0x00;

    frame[12] = 0x00;
}


// ============================================================
// POWER
// ============================================================

void buildPowerFrame(
    uint8_t frame[FRAME_BYTES],
    bool power
)
{
    buildBaseFrame(frame);

    frame[9] = power ? 0x04 : 0x00;

    frame[11] = 0xA0;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// TEMPERATURE +
// ============================================================

void buildTempPlusFrame(
    uint8_t frame[FRAME_BYTES],
    uint8_t newTemperature
)
{
    buildBaseFrame(frame);

    frame[1] = encodeTemperature(newTemperature);

    frame[11] = 0x00;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// TEMPERATURE -
// ============================================================

void buildTempMinusFrame(
    uint8_t frame[FRAME_BYTES],
    uint8_t newTemperature
)
{
    buildBaseFrame(frame);

    frame[1] = encodeTemperature(newTemperature);

    frame[11] = 0x80;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// FAN
// ============================================================

void buildFanFrame(
    uint8_t frame[FRAME_BYTES],
    Fan fan
)
{
    buildBaseFrame(frame);

    frame[4] = encodeFan(fan);

    frame[11] = 0x20;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// MODE COOL / DRY
// ============================================================

void buildModeNormalFrame(
    uint8_t frame[FRAME_BYTES],
    Mode mode
)
{
    buildBaseFrame(frame);

    frame[6] = encodeMode(mode);

    frame[11] = 0x60;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// MODE HEAT
//
// Capture thực tế:
//
// C3 E5 07 00 05 00 01 00 00 0C 00 60 05
//
// Đây là frame bạn đã capture.
// Ta giữ nguyên pattern này.
// ============================================================

void buildHeatFrame(
    uint8_t frame[FRAME_BYTES]
)
{
    buildBaseFrame(frame);

    frame[1] = encodeTemperature(ac.temperature);

    frame[4] = encodeFan(ac.fan);

    frame[6] = 0x01;

    frame[9] = 0x0C;

    frame[11] = 0x60;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// MODE AUTO
//
// Capture:
//
// AUTO + FAN AUTO:
//
// C3 E0 07 00 05 00 00 00 00 04 00 60 0E
//
// AUTO + FAN HIGH:
//
// C3 E0 07 00 04 00 03 00 00 04 00 60 0D
//
// Ta hỗ trợ cả 2 pattern.
// ============================================================

void buildAutoFrame(
    uint8_t frame[FRAME_BYTES]
)
{
    buildBaseFrame(frame);

    frame[1] = 0xE0;

    frame[9] = 0x04;

    frame[11] = 0x60;

    if (ac.fan == FAN_HIGH)
    {
        // Capture AUTO + HIGH
        frame[4] = 0x04;
        frame[6] = 0x03;
    }
    else
    {
        // Capture AUTO + AUTO
        frame[4] = 0x05;
        frame[6] = 0x00;
    }

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// MODE GENERAL
// ============================================================

void buildModeFrame(
    uint8_t frame[FRAME_BYTES],
    Mode mode
)
{
    switch (mode)
    {
        case MODE_COOL:

            buildModeNormalFrame(
                frame,
                MODE_COOL
            );

            break;


        case MODE_DRY:

            buildModeNormalFrame(
                frame,
                MODE_DRY
            );

            break;


        case MODE_HEAT:

            buildHeatFrame(frame);

            break;


        case MODE_AUTO:

            buildAutoFrame(frame);

            break;
    }
}


// ============================================================
// SWING
//
// OFF:
// C3 E9 07 00 05 00 04 00 00 04 00 40 38
//
// ON:
// C3 09 07 00 05 00 04 00 00 04 00 40 A8
// ============================================================

void buildSwingFrame(
    uint8_t frame[FRAME_BYTES],
    bool swing
)
{
    buildBaseFrame(frame);

    frame[11] = 0x40;

    if (swing)
    {
        frame[1] = 0x09;
    }

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// TIMER
//
// TIMER mapping:
//
// 0.5h -> A0 + 0 + half
// 1h   -> A1
// 1.5h -> A1 + half
// 2h   -> A2
// 2.5h -> A2 + half
// 3h   -> A3
// 3.5h -> A3 + half
// 4h   -> A4
// 4.5h -> A4 + half
// 5h   -> A5
// ============================================================

void buildTimerFrame(
    uint8_t frame[FRAME_BYTES],
    uint8_t hour,
    bool halfHour
)
{
    buildBaseFrame(frame);

    // TIMER command
    frame[11] = 0xB0;

    // TIMER ACTIVE
    frame[9] = 0x06;

    // Integer hour
    uint8_t value = 0xA0 + hour;

    frame[4] = reverseBits8(value);

    // 0.5h
    if (halfHour)
        frame[5] = 0x78;
    else
        frame[5] = 0x00;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// TIMER CANCEL
//
// C3 E9 07 00 05 00 04 00 00 04 00 B0 E4
// ============================================================

void buildTimerCancelFrame(
    uint8_t frame[FRAME_BYTES]
)
{
    buildBaseFrame(frame);

    frame[4] = 0x05;

    frame[5] = 0x00;

    frame[9] = 0x04;

    frame[11] = 0xB0;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// FRAME -> RAW
// ============================================================

void frameToRaw(
    uint8_t frame[FRAME_BYTES],
    uint16_t rawData[RAW_LENGTH]
)
{
    int index = 0;

    // HEADER
    rawData[index++] = HEADER_MARK;
    rawData[index++] = HEADER_SPACE;


    // ========================================================
    // 104 BITS
    // ========================================================

    for (int byteIndex = 0;
         byteIndex < FRAME_BYTES;
         byteIndex++)
    {
        uint8_t data = frame[byteIndex];

        // LSB FIRST
        for (int bit = 0; bit < 8; bit++)
        {
            rawData[index++] = BIT_MARK;

            bool bitValue =
                data & (1 << bit);

            if (bitValue)
            {
                rawData[index++] = ONE_SPACE;
            }
            else
            {
                rawData[index++] = ZERO_SPACE;
            }
        }
    }

    // TRAILING
    rawData[index++] = TRAILING_MARK;
}


// ============================================================
// PRINT FRAME
// ============================================================

void printFrame(
    uint8_t frame[FRAME_BYTES]
)
{
    Serial.print("HEX: ");

    for (int i = 0; i < FRAME_BYTES; i++)
    {
        if (frame[i] < 0x10)
            Serial.print("0");

        Serial.print(
            frame[i],
            HEX
        );

        Serial.print(" ");
    }

    Serial.println();


    Serial.print("B0=");
    Serial.printf("%02X ", frame[0]);

    Serial.print("B1=");
    Serial.printf("%02X ", frame[1]);

    Serial.print("B2=");
    Serial.printf("%02X ", frame[2]);

    Serial.print("B3=");
    Serial.printf("%02X ", frame[3]);

    Serial.print("B4=");
    Serial.printf("%02X ", frame[4]);

    Serial.print("B5=");
    Serial.printf("%02X ", frame[5]);

    Serial.print("B6=");
    Serial.printf("%02X ", frame[6]);

    Serial.print("B7=");
    Serial.printf("%02X ", frame[7]);

    Serial.print("B8=");
    Serial.printf("%02X ", frame[8]);

    Serial.print("B9=");
    Serial.printf("%02X ", frame[9]);

    Serial.print("B10=");
    Serial.printf("%02X ", frame[10]);

    Serial.print("B11=");
    Serial.printf("%02X ", frame[11]);

    Serial.print("B12=");
    Serial.printf("%02X", frame[12]);

    Serial.println();
}


// ============================================================
// SEND IR
// ============================================================

void sendFrame(
    uint8_t frame[FRAME_BYTES]
)
{
    uint16_t rawData[RAW_LENGTH];

    frameToRaw(
        frame,
        rawData
    );


    Serial.println();
    Serial.println("--------------- IR SEND ---------------");

    printFrame(frame);

    Serial.print("RAW length: ");
    Serial.println(RAW_LENGTH);

    Serial.print("Frequency: ");
    Serial.print(IR_FREQUENCY);
    Serial.println(" kHz");


    irsend.sendRaw(
        rawData,
        RAW_LENGTH,
        IR_FREQUENCY
    );


    Serial.println("IR SENT!");
    Serial.println("---------------------------------------");
}


// ============================================================
// POWER ON
// ============================================================

void powerOn()
{
    uint8_t frame[FRAME_BYTES];

    ac.power = true;

    buildPowerFrame(
        frame,
        true
    );

    sendFrame(frame);
}


// ============================================================
// POWER OFF
// ============================================================

void powerOff()
{
    uint8_t frame[FRAME_BYTES];

    ac.power = false;

    buildPowerFrame(
        frame,
        false
    );

    sendFrame(frame);
}


// ============================================================
// TEMP +
// ============================================================

void temperaturePlus()
{
    if (ac.temperature >= 30)
    {
        Serial.println(
            "Temperature already 30C"
        );

        return;
    }


    uint8_t newTemperature =
        ac.temperature + 1;


    uint8_t frame[FRAME_BYTES];


    buildTempPlusFrame(
        frame,
        newTemperature
    );


    ac.temperature =
        newTemperature;


    sendFrame(frame);
}


// ============================================================
// TEMP -
// ============================================================

void temperatureMinus()
{
    if (ac.temperature <= 20)
    {
        Serial.println(
            "Temperature already 20C"
        );

        return;
    }


    uint8_t newTemperature =
        ac.temperature - 1;


    uint8_t frame[FRAME_BYTES];


    buildTempMinusFrame(
        frame,
        newTemperature
    );


    ac.temperature =
        newTemperature;


    sendFrame(frame);
}


// ============================================================
// FAN
// ============================================================

void setFan(
    Fan fan
)
{
    uint8_t frame[FRAME_BYTES];

    ac.fan = fan;

    buildFanFrame(
        frame,
        fan
    );

    sendFrame(frame);
}


// ============================================================
// MODE
// ============================================================

void setMode(
    Mode mode
)
{
    uint8_t frame[FRAME_BYTES];

    ac.mode = mode;

    buildModeFrame(
        frame,
        mode
    );

    sendFrame(frame);
}


// ============================================================
// SWING
// ============================================================

void setSwing(
    bool enabled
)
{
    uint8_t frame[FRAME_BYTES];

    ac.swing = enabled;

    buildSwingFrame(
        frame,
        enabled
    );

    sendFrame(frame);
}


// ============================================================
// TIMER
// ============================================================

void setTimer(
    uint8_t hour,
    bool halfHour
)
{
    uint8_t frame[FRAME_BYTES];

    ac.timerEnabled = true;

    ac.timerHour =
        hour;

    ac.timerHalfHour =
        halfHour;


    buildTimerFrame(
        frame,
        hour,
        halfHour
    );


    sendFrame(frame);
}


// ============================================================
// TIMER CANCEL
// ============================================================

void cancelTimer()
{
    uint8_t frame[FRAME_BYTES];

    ac.timerEnabled = false;

    ac.timerHour = 0;

    ac.timerHalfHour = false;


    buildTimerCancelFrame(
        frame
    );


    sendFrame(frame);
}


// ============================================================
// PRINT STATE
// ============================================================

void printState()
{
    Serial.println();
    Serial.println("========================================");

    Serial.print("POWER       : ");
    Serial.println(
        ac.power
        ? "ON"
        : "OFF"
    );


    Serial.print("TEMPERATURE : ");
    Serial.print(ac.temperature);
    Serial.println(" C");


    Serial.print("MODE        : ");

    switch (ac.mode)
    {
        case MODE_AUTO:
            Serial.println("AUTO");
            break;

        case MODE_COOL:
            Serial.println("COOL");
            break;

        case MODE_DRY:
            Serial.println("DRY");
            break;

        case MODE_HEAT:
            Serial.println("HEAT");
            break;
    }


    Serial.print("FAN         : ");

    switch (ac.fan)
    {
        case FAN_AUTO:
            Serial.println("AUTO");
            break;

        case FAN_LOW:
            Serial.println("LOW");
            break;

        case FAN_MID:
            Serial.println("MID");
            break;

        case FAN_HIGH:
            Serial.println("HIGH");
            break;
    }


    Serial.print("SWING       : ");

    Serial.println(
        ac.swing
        ? "ON"
        : "OFF"
    );


    Serial.print("TIMER       : ");

    if (!ac.timerEnabled)
    {
        Serial.println("OFF");
    }
    else
    {
        Serial.print(
            ac.timerHour
        );

        if (ac.timerHalfHour)
            Serial.print(".5");

        Serial.println(" HOUR");
    }


    Serial.println(
        "========================================"
    );
}


// ============================================================
// MENU
// ============================================================

void printMenu()
{
    Serial.println();
    Serial.println("################################################");
    Serial.println("#          CASPER AC IR CONTROLLER             #");
    Serial.println("################################################");

    Serial.println();

    Serial.println("POWER");
    Serial.println(" 1  - POWER ON");
    Serial.println(" 2  - POWER OFF");

    Serial.println();

    Serial.println("TEMPERATURE");
    Serial.println(" 3  - TEMP +");
    Serial.println(" 4  - TEMP -");

    Serial.println();

    Serial.println("FAN");
    Serial.println(" 5  - FAN AUTO");
    Serial.println(" 6  - FAN LOW");
    Serial.println(" 7  - FAN MID");
    Serial.println(" 8  - FAN HIGH");

    Serial.println();

    Serial.println("MODE");
    Serial.println(" 9  - MODE COOL");
    Serial.println("10  - MODE DRY");
    Serial.println("11  - MODE HEAT");
    Serial.println("12  - MODE AUTO");

    Serial.println();

    Serial.println("SWING");
    Serial.println("13  - SWING ON");
    Serial.println("14  - SWING OFF");

    Serial.println();

    Serial.println("TIMER");
    Serial.println("15  - TIMER 0.5 HOUR");
    Serial.println("16  - TIMER 1 HOUR");
    Serial.println("17  - TIMER 1.5 HOUR");
    Serial.println("18  - TIMER 2 HOUR");
    Serial.println("19  - TIMER 2.5 HOUR");
    Serial.println("20  - TIMER 3 HOUR");
    Serial.println("21  - TIMER 3.5 HOUR");
    Serial.println("22  - TIMER 4 HOUR");
    Serial.println("23  - TIMER 4.5 HOUR");
    Serial.println("24  - TIMER 5 HOUR");
    Serial.println("25  - TIMER CANCEL");

    Serial.println();

    Serial.println("OTHER");
    Serial.println("26  - PRINT STATE");

    Serial.println();

    Serial.println("################################################");
}


// ============================================================
// PROCESS COMMAND
// ============================================================

void processCommand(
    String command
)
{
    command.trim();

    int cmd =
        command.toInt();


    switch (cmd)
    {
        // ====================================================
        // POWER
        // ====================================================

        case 1:
            powerOn();
            break;

        case 2:
            powerOff();
            break;


        // ====================================================
        // TEMPERATURE
        // ====================================================

        case 3:
            temperaturePlus();
            break;

        case 4:
            temperatureMinus();
            break;


        // ====================================================
        // FAN
        // ====================================================

        case 5:
            setFan(FAN_AUTO);
            break;

        case 6:
            setFan(FAN_LOW);
            break;

        case 7:
            setFan(FAN_MID);
            break;

        case 8:
            setFan(FAN_HIGH);
            break;


        // ====================================================
        // MODE
        // ====================================================

        case 9:
            setMode(MODE_COOL);
            break;

        case 10:
            setMode(MODE_DRY);
            break;

        case 11:
            setMode(MODE_HEAT);
            break;

        case 12:
            setMode(MODE_AUTO);
            break;


        // ====================================================
        // SWING
        // ====================================================

        case 13:
            setSwing(true);
            break;

        case 14:
            setSwing(false);
            break;


        // ====================================================
        // TIMER
        // ====================================================

        case 15:
            setTimer(0, true);
            break;

        case 16:
            setTimer(1, false);
            break;

        case 17:
            setTimer(1, true);
            break;

        case 18:
            setTimer(2, false);
            break;

        case 19:
            setTimer(2, true);
            break;

        case 20:
            setTimer(3, false);
            break;

        case 21:
            setTimer(3, true);
            break;

        case 22:
            setTimer(4, false);
            break;

        case 23:
            setTimer(4, true);
            break;

        case 24:
            setTimer(5, false);
            break;

        case 25:
            cancelTimer();
            break;


        // ====================================================
        // STATE
        // ====================================================

        case 26:
            printState();
            break;


        default:

            Serial.println();
            Serial.println(
                "Unknown command!"
            );

            break;
    }


    delay(100);

    printState();

    printMenu();
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);


    irsend.begin();


    Serial.println();
    Serial.println(
        "################################################"
    );

    Serial.println(
        "#          CASPER AC IR CONTROLLER             #"
    );

    Serial.println(
        "################################################"
    );


    Serial.println();

    Serial.print(
        "IR TX GPIO : "
    );

    Serial.println(
        IR_TX_PIN
    );


    Serial.print(
        "Frequency  : "
    );

    Serial.print(
        IR_FREQUENCY
    );

    Serial.println(
        " kHz"
    );


    Serial.print(
        "Frame      : "
    );

    Serial.print(
        FRAME_BYTES
    );

    Serial.println(
        " bytes / 104 bits"
    );


    Serial.print(
        "RAW length : "
    );

    Serial.println(
        RAW_LENGTH
    );


    printState();

    printMenu();
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    if (Serial.available())
    {
        String command =
            Serial.readStringUntil('\n');

        processCommand(command);
    }
}