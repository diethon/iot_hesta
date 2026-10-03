#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>

// ============================================================
// HARDWARE
// ============================================================

// IR LED -> 2N2222 -> GPIO13
#define IR_TX_PIN 13

// VS1838B OUT -> GPIO14
#define IR_RX_PIN 14

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
// VS1838B CAPTURE
// ============================================================

#define RX_BUFFER_SIZE 300

volatile uint32_t rxRaw[RX_BUFFER_SIZE];

volatile uint16_t rxCount = 0;

volatile uint32_t rxLastEdge = 0;

volatile bool rxStarted = false;

volatile bool rxDone = false;


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
        frame[4] = 0x04;
        frame[6] = 0x03;
    }
    else
    {
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
// ============================================================

void buildTimerFrame(
    uint8_t frame[FRAME_BYTES],
    uint8_t hour,
    bool halfHour
)
{
    buildBaseFrame(frame);

    frame[11] = 0xB0;

    frame[9] = 0x06;

    uint8_t value = 0xA0 + hour;

    frame[4] = reverseBits8(value);

    if (halfHour)
        frame[5] = 0x78;
    else
        frame[5] = 0x00;

    frame[12] = calculateChecksum(frame);
}


// ============================================================
// TIMER CANCEL
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


    // 104 BITS

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
// VS1838B INTERRUPT
// ============================================================
//
// VS1838B:
// IDLE       = HIGH
// IR detected = LOW
//
// Mỗi lần OUT đổi trạng thái,
// lưu khoảng thời gian giữa 2 cạnh.
// ============================================================

void IRAM_ATTR vs1838ISR()
{
    uint32_t now = micros();

    // Lần đầu tiên có edge
    if (!rxStarted)
    {
        rxStarted = true;
        rxLastEdge = now;
        return;
    }

    uint32_t duration =
        now - rxLastEdge;

    rxLastEdge = now;


    // Gap lớn => kết thúc frame
    //
    // Header chỉ ~9ms,
    // nên 15ms là ngưỡng an toàn.
    if (duration > 15000)
    {
        if (rxCount > 0)
        {
            rxDone = true;
        }

        return;
    }


    // Bỏ các xung quá nhỏ do nhiễu
    if (duration < 80)
        return;


    if (!rxDone &&
        rxCount < RX_BUFFER_SIZE)
    {
        rxRaw[rxCount++] = duration;
    }
}


// ============================================================
// RESET VS1838 CAPTURE
// ============================================================

void resetRXCapture()
{
    noInterrupts();

    rxCount = 0;

    rxLastEdge = 0;

    rxStarted = false;

    rxDone = false;

    interrupts();
}


// ============================================================
// PRINT VS1838 RAW
// ============================================================

void printRXRaw()
{
    Serial.println();
    Serial.println("============== VS1838B RAW ==============");

    Serial.print("Captured length: ");
    Serial.println(rxCount);

    for (int i = 0; i < rxCount; i++)
    {
        Serial.printf(
            "[%03d] %lu us",
            i + 1,
            rxRaw[i]
        );

        if (i % 2 == 0)
            Serial.print("  <-- MARK");
        else
            Serial.print("  <-- SPACE");

        Serial.println();
    }

    Serial.println(
        "=========================================="
    );
}


// ============================================================
// DECODE VS1838 RAW -> 13 BYTES
// ============================================================

bool decodeVS1838(
    uint8_t decoded[FRAME_BYTES]
)
{
    memset(
        decoded,
        0,
        FRAME_BYTES
    );


    // Cần ít nhất:
    //
    // 2 header
    // 104 bit * 2
    // 1 trailing
    //
    // = 211

    if (rxCount < 211)
    {
        Serial.println();
        Serial.println(
            "VS1838: KHONG DU 211 RAW ELEMENTS!"
        );

        return false;
    }


    // --------------------------------------------------------
    // Header
    // --------------------------------------------------------

    Serial.println();

    Serial.println(
        "---------- VS1838 HEADER ----------"
    );

    Serial.print("Header MARK : ");
    Serial.print(rxRaw[0]);
    Serial.println(" us");

    Serial.print("Header SPACE: ");
    Serial.print(rxRaw[1]);
    Serial.println(" us");


    // --------------------------------------------------------
    // Check header
    // --------------------------------------------------------

    bool headerOK = true;


    if (abs(
            (long)rxRaw[0]
            - HEADER_MARK
        ) > 2000)
    {
        headerOK = false;
    }


    if (abs(
            (long)rxRaw[1]
            - HEADER_SPACE
        ) > 1500)
    {
        headerOK = false;
    }


    if (headerOK)
    {
        Serial.println(
            "Header check: OK"
        );
    }
    else
    {
        Serial.println(
            "Header check: KHONG KHOP"
        );
    }


    // --------------------------------------------------------
    // Decode 104 bits
    // --------------------------------------------------------

    for (int bitIndex = 0;
         bitIndex < FRAME_BITS;
         bitIndex++)
    {
        // raw:
        //
        // index 0 = header mark
        // index 1 = header space
        //
        // bit 0:
        // index 2 = mark
        // index 3 = space
        //
        // bit 1:
        // index 4 = mark
        // index 5 = space

        int spaceIndex =
            3 + (bitIndex * 2);


        uint32_t space =
            rxRaw[spaceIndex];


        bool bitValue =
            space > 1000;


        int byteIndex =
            bitIndex / 8;

        int bitInByte =
            bitIndex % 8;


        if (bitValue)
        {
            decoded[byteIndex] |=
                (1 << bitInByte);
        }
    }


    return true;
}


// ============================================================
// PRINT VS1838 DECODED FRAME
// ============================================================

void printDecodedFrame(
    uint8_t frame[FRAME_BYTES]
)
{
    Serial.println();
    Serial.println(
        "========== VS1838 DECODED =========="
    );

    Serial.print("HEX: ");

    for (int i = 0;
         i < FRAME_BYTES;
         i++)
    {
        if (frame[i] < 0x10)
            Serial.print("0");

        Serial.printf(
            "%02X ",
            frame[i]
        );
    }

    Serial.println();

    Serial.println(
        "====================================="
    );
}


// ============================================================
// COMPARE FRAME
// ============================================================

bool compareFrames(
    uint8_t expected[FRAME_BYTES],
    uint8_t received[FRAME_BYTES]
)
{
    bool same = true;


    Serial.println();
    Serial.println(
        "========== BYTE COMPARISON =========="
    );


    for (int i = 0;
         i < FRAME_BYTES;
         i++)
    {
        Serial.printf(
            "B%-2d  TX=%02X   VS1838=%02X",
            i,
            expected[i],
            received[i]
        );


        if (expected[i] ==
            received[i])
        {
            Serial.println(
                "   OK"
            );
        }
        else
        {
            Serial.println(
                "   !!! KHAC !!!"
            );

            same = false;
        }
    }


    Serial.println(
        "====================================="
    );


    return same;
}


// ============================================================
// CHECK AGAINST REMOTE CAPTURE VALUES
// ============================================================
//
// Đây là các frame bạn đã capture trước đó.
// ============================================================

struct KnownRemoteFrame
{
    const char* name;

    uint8_t data[FRAME_BYTES];
};


KnownRemoteFrame knownRemoteFrames[] =
{
    {
        "COOL 24C STATE",
        {
            0xC3, 0xE1, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x00,
            0x50
        }
    },


    {
        "COOL 25C STATE",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x00,
            0x48
        }
    },


    {
        "COOL 26C STATE",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x00,
            0x58
        }
    },


    {
        "POWER ON 26C",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0xA0,
            0xF8
        }
    },


    {
        "POWER OFF 26C",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x00, 0x00, 0xA0,
            0xFF
        }
    },


    {
        "FAN LOW 25C",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x06, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x20,
            0x6B
        }
    },


    {
        "FAN MID 25C",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x02, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x20,
            0x6D
        }
    },


    {
        "FAN HIGH 25C",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x04, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x20,
            0x69
        }
    },


    {
        "FAN AUTO 25C",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x20,
            0x68
        }
    },


    {
        "SWING ON 26C",
        {
            0xC3, 0x09, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x40,
            0xA8
        }
    },


    {
        "SWING OFF 26C",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x40,
            0x38
        }
    },


    {
        "DRY 26C",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x05, 0x00, 0x02, 0x00,
            0x00, 0x04, 0x00, 0x60,
            0x02
        }
    },


    {
        "HEAT",
        {
            0xC3, 0xE5, 0x07, 0x00,
            0x05, 0x00, 0x01, 0x00,
            0x00, 0x0C, 0x00, 0x60,
            0x05
        }
    },


    {
        "AUTO FAN AUTO",
        {
            0xC3, 0xE0, 0x07, 0x00,
            0x05, 0x00, 0x00, 0x00,
            0x00, 0x04, 0x00, 0x60,
            0x0E
        }
    },


    {
        "AUTO FAN HIGH",
        {
            0xC3, 0xE0, 0x07, 0x00,
            0x04, 0x00, 0x03, 0x00,
            0x00, 0x04, 0x00, 0x60,
            0x0D
        }
    },


    {
        "COOL MODE 25C",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x60,
            0x18
        }
    },


    {
        "TEMP MINUS 26->25",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x80,
            0xC8
        }
    },


    {
        "TEMP PLUS 25->26",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0x00,
            0x58
        }
    },


    {
        "TIMER 0.5H",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x05, 0x78, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0xBE
        }
    },


    {
        "TIMER 1H",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x85, 0x00, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0x06
        }
    },


    {
        "TIMER 1.5H",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x85, 0x78, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0x7E
        }
    },


    {
        "TIMER 2H",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x45, 0x00, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0x86
        }
    },


    {
        "TIMER 2.5H",
        {
            0xC3, 0xF1, 0x07, 0x00,
            0x45, 0x78, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0xFE
        }
    },


    {
        "TIMER 3H",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0xC5, 0x00, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0x56
        }
    },


    {
        "TIMER 3.5H",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0xC5, 0x78, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0x11
        }
    },


    {
        "TIMER 4H",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x25, 0x00, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0xD6
        }
    },


    {
        "TIMER 4.5H",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x25, 0x78, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0x91
        }
    },


    {
        "TIMER 5H",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0xA5, 0x00, 0x04, 0x00,
            0x00, 0x06, 0x00, 0xB0,
            0x36
        }
    },


    {
        "TIMER CANCEL",
        {
            0xC3, 0xE9, 0x07, 0x00,
            0x05, 0x00, 0x04, 0x00,
            0x00, 0x04, 0x00, 0xB0,
            0xE4
        }
    }
};


const int knownRemoteCount =
    sizeof(knownRemoteFrames) /
    sizeof(knownRemoteFrames[0]);


// ============================================================
// COMPARE AGAINST KNOWN REMOTE
// ============================================================

void compareWithKnownRemote(
    uint8_t received[FRAME_BYTES]
)
{
    Serial.println();
    Serial.println(
        "====== SO SANH VOI REMOTE DA CAPTURE ======"
    );


    bool found = false;


    for (int n = 0;
         n < knownRemoteCount;
         n++)
    {
        bool same = true;


        for (int i = 0;
             i < FRAME_BYTES;
             i++)
        {
            if (
                received[i] !=
                knownRemoteFrames[n].data[i]
            )
            {
                same = false;
                break;
            }
        }


        if (same)
        {
            Serial.println();
            Serial.print(
                ">>> TRUNG KHOP REMOTE: "
            );

            Serial.println(
                knownRemoteFrames[n].name
            );

            Serial.println(
                ">>> FRAME DA DUOC XAC NHAN."
            );

            found = true;

            break;
        }
    }


    if (!found)
    {
        Serial.println();
        Serial.println(
            ">>> KHONG TRUNG VOI CAC FRAME"
        );

        Serial.println(
            ">>> REMOTE DA LUU TRU."
        );

        Serial.println(
            ">>> Dieu nay co the la do timing"
        );

        Serial.println(
            ">>> VS1838 bi thieu/mat xung,"
        );

        Serial.println(
            ">>> hoac day la frame chua luu."
        );
    }


    Serial.println(
        "============================================"
    );
}


// ============================================================
// PRINT CAPTURE RESULT
// ============================================================

void analyzeVS1838(
    uint8_t txFrame[FRAME_BYTES]
)
{
    Serial.println();
    Serial.println();
    Serial.println(
        "################################################"
    );

    Serial.println(
        "#             VS1838B ANALYSIS                #"
    );

    Serial.println(
        "################################################"
    );


    Serial.print(
        "VS1838 captured elements: "
    );

    Serial.println(rxCount);


    // --------------------------------------------------------
    // Raw
    // --------------------------------------------------------

    printRXRaw();


    // --------------------------------------------------------
    // Decode
    // --------------------------------------------------------

    uint8_t receivedFrame[FRAME_BYTES];


    bool decodeOK =
        decodeVS1838(
            receivedFrame
        );


    if (!decodeOK)
    {
        Serial.println();
        Serial.println(
            "VS1838 DECODE: FAILED"
        );

        return;
    }


    printDecodedFrame(
        receivedFrame
    );


    // --------------------------------------------------------
    // Compare TX vs RX
    // --------------------------------------------------------

    bool same =
        compareFrames(
            txFrame,
            receivedFrame
        );


    Serial.println();


    if (same)
    {
        Serial.println(
            "******** RESULT ********"
        );

        Serial.println(
            "TX == VS1838 RX"
        );

        Serial.println(
            "=> VS1838 DA THU DUNG FRAME."
        );

        Serial.println(
            "*************************"
        );
    }
    else
    {
        Serial.println(
            "******** RESULT ********"
        );

        Serial.println(
            "TX != VS1838 RX"
        );

        Serial.println(
            "=> FRAME BI KHAC."
        );

        Serial.println(
            "*************************"
        );
    }


    // --------------------------------------------------------
    // Compare known remote
    // --------------------------------------------------------

    compareWithKnownRemote(
        receivedFrame
    );


    Serial.println();
    Serial.println(
        "################################################"
    );

    Serial.println(
        "#             END VS1838 TEST                 #"
    );

    Serial.println(
        "################################################"
    );
}


// ============================================================
// SEND IR + CAPTURE VS1838
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

    Serial.println(
        "--------------- IR SEND ---------------"
    );


    printFrame(frame);


    Serial.print("RAW length: ");
    Serial.println(RAW_LENGTH);


    Serial.print("Frequency: ");
    Serial.print(IR_FREQUENCY);
    Serial.println(" kHz");


    Serial.print("TX GPIO: ");
    Serial.println(IR_TX_PIN);


    Serial.print("RX GPIO: ");
    Serial.println(IR_RX_PIN);


    // ========================================================
    // RESET RX
    // ========================================================

    resetRXCapture();


    // Cho VS1838 ổn định ở trạng thái idle
    delay(100);


    Serial.println();

    Serial.println(
        "VS1838 capture: ARMED"
    );


    // ========================================================
    // ENABLE INTERRUPT
    // ========================================================

    attachInterrupt(
        digitalPinToInterrupt(IR_RX_PIN),
        vs1838ISR,
        CHANGE
    );


    delay(10);


    // ========================================================
    // SEND IR
    // ========================================================

    Serial.println(
        "IR TRANSMISSION START..."
    );


    irsend.sendRaw(
        rawData,
        RAW_LENGTH,
        IR_FREQUENCY
    );


    Serial.println(
        "IR TRANSMISSION END."
    );


    // Chờ cạnh cuối từ VS1838
    delay(30);


    // ========================================================
    // STOP CAPTURE
    // ========================================================

    detachInterrupt(
        digitalPinToInterrupt(IR_RX_PIN)
    );


    Serial.println(
        "VS1838 capture: STOPPED"
    );


    // ========================================================
    // ANALYZE
    // ========================================================

    analyzeVS1838(frame);


    Serial.println(
        "IR SENT!"
    );

    Serial.println(
        "---------------------------------------"
    );
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

    Serial.println(
        "========================================"
    );


    Serial.print("POWER       : ");

    Serial.println(
        ac.power
        ? "ON"
        : "OFF"
    );


    Serial.print("TEMPERATURE : ");

    Serial.print(
        ac.temperature
    );

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

    Serial.println("POWER");

    Serial.println(
        " 1  - POWER ON"
    );

    Serial.println(
        " 2  - POWER OFF"
    );


    Serial.println();

    Serial.println("TEMPERATURE");

    Serial.println(
        " 3  - TEMP +"
    );

    Serial.println(
        " 4  - TEMP -"
    );


    Serial.println();

    Serial.println("FAN");

    Serial.println(
        " 5  - FAN AUTO"
    );

    Serial.println(
        " 6  - FAN LOW"
    );

    Serial.println(
        " 7  - FAN MID"
    );

    Serial.println(
        " 8  - FAN HIGH"
    );


    Serial.println();

    Serial.println("MODE");

    Serial.println(
        " 9  - MODE COOL"
    );

    Serial.println(
        "10  - MODE DRY"
    );

    Serial.println(
        "11  - MODE HEAT"
    );

    Serial.println(
        "12  - MODE AUTO"
    );


    Serial.println();

    Serial.println("SWING");

    Serial.println(
        "13  - SWING ON"
    );

    Serial.println(
        "14  - SWING OFF"
    );


    Serial.println();

    Serial.println("TIMER");

    Serial.println(
        "15  - TIMER 0.5 HOUR"
    );

    Serial.println(
        "16  - TIMER 1 HOUR"
    );

    Serial.println(
        "17  - TIMER 1.5 HOUR"
    );

    Serial.println(
        "18  - TIMER 2 HOUR"
    );

    Serial.println(
        "19  - TIMER 2.5 HOUR"
    );

    Serial.println(
        "20  - TIMER 3 HOUR"
    );

    Serial.println(
        "21  - TIMER 3.5 HOUR"
    );

    Serial.println(
        "22  - TIMER 4 HOUR"
    );

    Serial.println(
        "23  - TIMER 4.5 HOUR"
    );

    Serial.println(
        "24  - TIMER 5 HOUR"
    );

    Serial.println(
        "25  - TIMER CANCEL"
    );


    Serial.println();

    Serial.println("OTHER");

    Serial.println(
        "26  - PRINT STATE"
    );


    Serial.println();

    Serial.println(
        "################################################"
    );
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


    // ========================================================
    // IR TX
    // ========================================================

    irsend.begin();


    // ========================================================
    // VS1838B RX
    // ========================================================

    pinMode(
        IR_RX_PIN,
        INPUT
    );


    // VS1838B output idle HIGH.
    // Không bật pull-up vì module thường đã có output logic.
    // Nếu module của bạn cần pull-up, đổi thành INPUT_PULLUP.


    Serial.println();

    Serial.println(
        "################################################"
    );

    Serial.println(
        "#       CASPER IR TX + VS1838B RX TEST        #"
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
        "VS1838 GPIO: "
    );

    Serial.println(
        IR_RX_PIN
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


    Serial.println();

    Serial.println(
        "VS1838B expected:"
    );

    Serial.println(
        "IDLE = HIGH"
    );

    Serial.println(
        "IR detected = LOW"
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