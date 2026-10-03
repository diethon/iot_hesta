#include <Arduino.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#include <IRremoteESP8266.h>
#include <IRsend.h>


// ============================================================
// WIFI
// ============================================================

#define WIFI_SSID       "Dung Home T3"
#define WIFI_PASSWORD   "123lyvanphuc"


// ============================================================
// MQTT
// ============================================================

// IP máy đang chạy Mosquitto
#define MQTT_SERVER     "192.168.88.104"
#define MQTT_PORT       1883

#define MQTT_USER       ""
#define MQTT_PASSWORD   ""


// ============================================================
// DEVICE
// ============================================================

#define NODE_ID         "32442d7f-dfcf-4b2f-a380-adba32876cec"
#define DEVICE_ID       "05d8a4fd-d7f1-4c62-a70e-a4213552b054"

#define DEVICE_TYPE     "AIR_CONDITIONER"


// ============================================================
// MQTT TOPICS
// ============================================================

#define MQTT_COMMAND_TOPIC \
    "hesta/nodes/32442d7f-dfcf-4b2f-a380-adba32876cec/devices/05d8a4fd-d7f1-4c62-a70e-a4213552b054/command"

#define MQTT_TELEMETRY_TOPIC \
    "hesta/nodes/32442d7f-dfcf-4b2f-a380-adba32876cec/devices/05d8a4fd-d7f1-4c62-a70e-a4213552b054/telemetry"

#define MQTT_STATUS_TOPIC \
    "hesta/nodes/32442d7f-dfcf-4b2f-a380-adba32876cec/devices/05d8a4fd-d7f1-4c62-a70e-a4213552b054/status"


// ============================================================
// HARDWARE
// ============================================================

#define IR_TX_PIN 13

IRsend irsend(IR_TX_PIN);


// ============================================================
// MQTT CLIENT
// ============================================================

WiFiClient espClient;

PubSubClient mqttClient(espClient);


// ============================================================
// ELECTRA/CASPER AC
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
    true,
    26,

    MODE_COOL,

    FAN_AUTO,

    false,

    false,
    0,
    false
};


// ============================================================
// MODE -> STRING
// ============================================================

const char* modeToString(Mode mode)
{
    switch (mode)
    {
        case MODE_AUTO:
            return "AUTO";

        case MODE_COOL:
            return "COOL";

        case MODE_DRY:
            return "DRY";

        case MODE_HEAT:
            return "HEAT";
    }

    return "COOL";
}


// ============================================================
// STRING -> MODE
// ============================================================

Mode stringToMode(String value)
{
    value.toUpperCase();

    if (value == "AUTO")
        return MODE_AUTO;

    if (value == "COOL")
        return MODE_COOL;

    if (value == "DRY")
        return MODE_DRY;

    if (value == "HEAT")
        return MODE_HEAT;

    return MODE_COOL;
}


// ============================================================
// FAN -> STRING
// ============================================================

const char* fanToString(Fan fan)
{
    switch (fan)
    {
        case FAN_AUTO:
            return "AUTO";

        case FAN_LOW:
            return "LOW";

        case FAN_MID:
            return "MID";

        case FAN_HIGH:
            return "HIGH";
    }

    return "AUTO";
}


// ============================================================
// STRING -> FAN
// ============================================================

Fan stringToFan(String value)
{
    value.toUpperCase();

    if (value == "AUTO")
        return FAN_AUTO;

    if (value == "LOW")
        return FAN_LOW;

    if (value == "MID")
        return FAN_MID;

    if (value == "HIGH")
        return FAN_HIGH;

    return FAN_AUTO;
}


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
// TEMP +
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
// TEMP -
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
// MODE NORMAL
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
// HEAT
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
// AUTO
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
            buildModeNormalFrame(frame, MODE_COOL);
            break;

        case MODE_DRY:
            buildModeNormalFrame(frame, MODE_DRY);
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

    rawData[index++] = HEADER_MARK;
    rawData[index++] = HEADER_SPACE;

    for (
        int byteIndex = 0;
        byteIndex < FRAME_BYTES;
        byteIndex++
    )
    {
        uint8_t data = frame[byteIndex];

        for (int bit = 0; bit < 8; bit++)
        {
            rawData[index++] = BIT_MARK;

            bool bitValue =
                data & (1 << bit);

            if (bitValue)
                rawData[index++] = ONE_SPACE;
            else
                rawData[index++] = ZERO_SPACE;
        }
    }

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

        Serial.print(frame[i], HEX);
        Serial.print(" ");
    }

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

    irsend.sendRaw(
        rawData,
        RAW_LENGTH,
        IR_FREQUENCY
    );

    Serial.println("IR SENT!");
    Serial.println("---------------------------------------");
}


// ============================================================
// MQTT TELEMETRY
// ============================================================

void publishACState()
{
    if (!mqttClient.connected())
        return;


    JsonDocument doc;

    doc["nodeId"] = NODE_ID;

    doc["deviceId"] = DEVICE_ID;

    doc["deviceType"] = DEVICE_TYPE;

    doc["power"] = ac.power;

    doc["temperature"] = ac.temperature;

    doc["mode"] = modeToString(ac.mode);

    doc["fan"] = fanToString(ac.fan);

    doc["swing"] = ac.swing;

    doc["timerEnabled"] = ac.timerEnabled;

    doc["timerHour"] = ac.timerHour;

    doc["timerHalfHour"] = ac.timerHalfHour;


    char buffer[512];

    serializeJson(doc, buffer);

    mqttClient.publish(
        MQTT_TELEMETRY_TOPIC,
        buffer,
        true
    );


    Serial.println();
    Serial.println("MQTT TELEMETRY:");

    Serial.println(buffer);
}


// ============================================================
// MQTT STATUS
// ============================================================

void publishStatus(
    const char* status
)
{
    mqttClient.publish(
        MQTT_STATUS_TOPIC,
        status,
        true
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

    publishACState();
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

    publishACState();
}


// ============================================================
// TEMP +
// ============================================================

void temperaturePlus()
{
    if (ac.temperature >= 30)
        return;


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

    publishACState();
}


// ============================================================
// TEMP -
// ============================================================

void temperatureMinus()
{
    if (ac.temperature <= 20)
        return;


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

    publishACState();
}


// ============================================================
// SET TEMPERATURE
// ============================================================

void setTemperature(
    uint8_t temperature
)
{
    if (
        temperature < 20 ||
        temperature > 30
    )
    {
        Serial.println(
            "Invalid temperature"
        );

        return;
    }


    if (temperature == ac.temperature)
    {
        publishACState();
        return;
    }


    if (temperature > ac.temperature)
    {
        while (ac.temperature < temperature)
        {
            temperaturePlus();
            delay(300);
        }
    }
    else
    {
        while (ac.temperature > temperature)
        {
            temperatureMinus();
            delay(300);
        }
    }
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

    publishACState();
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

    publishACState();
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

    publishACState();
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

    ac.timerHour = hour;

    ac.timerHalfHour = halfHour;


    buildTimerFrame(
        frame,
        hour,
        halfHour
    );


    sendFrame(frame);

    publishACState();
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

    publishACState();
}


// ============================================================
// PROCESS MQTT COMMAND
// ============================================================

void processMQTTCommand(
    char* payload
)
{
    Serial.println();
    Serial.println("========================================");
    Serial.println("MQTT COMMAND RECEIVED:");
    Serial.println(payload);
    Serial.println("========================================");


    JsonDocument doc;

    DeserializationError error =
        deserializeJson(
            doc,
            payload
        );


    if (error)
    {
        Serial.println(
            "Invalid JSON"
        );

        return;
    }


    const char* action =
        doc["action"];


    if (!action)
    {
        Serial.println(
            "Missing action"
        );

        return;
    }


    // ========================================================
    // POWER
    // ========================================================

    if (
        strcmp(
            action,
            "SET_POWER"
        ) == 0
    )
    {
        bool power =
            doc["power"];

        if (power)
            powerOn();
        else
            powerOff();

        return;
    }


    // ========================================================
    // TEMPERATURE
    // ========================================================

    if (
        strcmp(
            action,
            "SET_TEMPERATURE"
        ) == 0
    )
    {
        int temperature =
            doc["temperature"];

        setTemperature(
            temperature
        );

        return;
    }


    // ========================================================
    // TEMP PLUS
    // ========================================================

    if (
        strcmp(
            action,
            "TEMPERATURE_PLUS"
        ) == 0
    )
    {
        temperaturePlus();

        return;
    }


    // ========================================================
    // TEMP MINUS
    // ========================================================

    if (
        strcmp(
            action,
            "TEMPERATURE_MINUS"
        ) == 0
    )
    {
        temperatureMinus();

        return;
    }


    // ========================================================
    // FAN
    // ========================================================

    if (
        strcmp(
            action,
            "SET_FAN"
        ) == 0
    )
    {
        const char* value =
            doc["fan"];

        setFan(
            stringToFan(
                String(value)
            )
        );

        return;
    }


    // ========================================================
    // MODE
    // ========================================================

    if (
        strcmp(
            action,
            "SET_MODE"
        ) == 0
    )
    {
        const char* value =
            doc["mode"];

        setMode(
            stringToMode(
                String(value)
            )
        );

        return;
    }


    // ========================================================
    // SWING
    // ========================================================

    if (
        strcmp(
            action,
            "SET_SWING"
        ) == 0
    )
    {
        bool enabled =
            doc["swing"];

        setSwing(enabled);

        return;
    }


    // ========================================================
    // TIMER
    // ========================================================

    if (
        strcmp(
            action,
            "SET_TIMER"
        ) == 0
    )
    {
        int hour =
            doc["hour"];

        bool half =
            doc["halfHour"];

        setTimer(
            hour,
            half
        );

        return;
    }


    // ========================================================
    // TIMER CANCEL
    // ========================================================

    if (
        strcmp(
            action,
            "CANCEL_TIMER"
        ) == 0
    )
    {
        cancelTimer();

        return;
    }


    // ========================================================
    // GET STATE
    // ========================================================

    if (
        strcmp(
            action,
            "GET_STATE"
        ) == 0
    )
    {
        publishACState();

        return;
    }


    Serial.println(
        "Unknown MQTT action"
    );
}


// ============================================================
// MQTT CALLBACK
// ============================================================

void mqttCallback(
    char* topic,
    byte* payload,
    unsigned int length
)
{
    char message[1024];

    if (length >= sizeof(message))
        length = sizeof(message) - 1;


    memcpy(
        message,
        payload,
        length
    );

    message[length] = '\0';


    processMQTTCommand(
        message
    );
}


// ============================================================
// CONNECT MQTT
// ============================================================

void connectMQTT()
{
    while (!mqttClient.connected())
    {
        Serial.println();
        Serial.println(
            "Connecting MQTT..."
        );


        String clientId =
            String(NODE_ID) +
            "-" +
            String((uint32_t)ESP.getEfuseMac(), HEX);


        bool connected;


        if (
            strlen(MQTT_USER) > 0
        )
        {
            connected =
                mqttClient.connect(
                    clientId.c_str(),
                    MQTT_USER,
                    MQTT_PASSWORD
                );
        }
        else
        {
            connected =
                mqttClient.connect(
                    clientId.c_str()
                );
        }


        if (connected)
        {
            Serial.println(
                "MQTT connected!"
            );


            mqttClient.subscribe(
                MQTT_COMMAND_TOPIC
            );


            publishStatus(
                "ONLINE"
            );


            publishACState();
        }
        else
        {
            Serial.print(
                "MQTT failed, rc="
            );

            Serial.println(
                mqttClient.state()
            );

            delay(3000);
        }
    }
}


// ============================================================
// WIFI
// ============================================================

void connectWiFi()
{
    Serial.println();

    Serial.print(
        "Connecting WiFi: "
    );

    Serial.println(
        WIFI_SSID
    );


    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );


    while (
        WiFi.status() != WL_CONNECTED
    )
    {
        delay(500);

        Serial.print(".");
    }


    Serial.println();

    Serial.println(
        "WiFi connected!"
    );


    Serial.print(
        "ESP32 IP: "
    );

    Serial.println(
        WiFi.localIP()
    );


    Serial.print(
        "RSSI: "
    );

    Serial.println(
        WiFi.RSSI()
    );
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);

    delay(1000);


    // --------------------------------------------------------
    // IR
    // --------------------------------------------------------

    irsend.begin();


    // --------------------------------------------------------
    // WIFI
    // --------------------------------------------------------

    connectWiFi();


    // --------------------------------------------------------
    // MQTT
    // --------------------------------------------------------

    mqttClient.setServer(
        MQTT_SERVER,
        MQTT_PORT
    );


    mqttClient.setCallback(
        mqttCallback
    );


    mqttClient.setBufferSize(
        1024
    );


    connectMQTT();


    Serial.println();

    Serial.println(
        "========================================"
    );

    Serial.println(
        "CASPER AC MQTT + IR CONTROLLER"
    );

    Serial.println(
        "========================================"
    );


    Serial.print(
        "Node ID: "
    );

    Serial.println(
        NODE_ID
    );


    Serial.print(
        "Device ID: "
    );

    Serial.println(
        DEVICE_ID
    );


    Serial.print(
        "MQTT Command: "
    );

    Serial.println(
        MQTT_COMMAND_TOPIC
    );


    Serial.print(
        "MQTT Telemetry: "
    );

    Serial.println(
        MQTT_TELEMETRY_TOPIC
    );
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    // --------------------------------------------------------
    // WIFI
    // --------------------------------------------------------

    if (
        WiFi.status() != WL_CONNECTED
    )
    {
        connectWiFi();
    }


    // --------------------------------------------------------
    // MQTT
    // --------------------------------------------------------

    if (!mqttClient.connected())
    {
        connectMQTT();
    }


    mqttClient.loop();


    delay(10);
}