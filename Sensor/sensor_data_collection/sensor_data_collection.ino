#include <DHT.h>

// =========================
// Pin configuration
// =========================
#define LDR_PIN  12
#define PIR_PIN  13
#define DHT_PIN  14

#define DHT_TYPE DHT22

DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
    Serial.begin(115200);

    // LDR
    pinMode(LDR_PIN, INPUT);

    // PIR
    pinMode(PIR_PIN, INPUT);

    // DHT22
    dht.begin();

    Serial.println();
    Serial.println("================================");
    Serial.println("Smart Home Sensor Test");
    Serial.println("LDR + PIR + DHT22");
    Serial.println("================================");
}

void loop() {

    // =================================
    // 1. Read LDR
    // =================================
    int lightValue = analogRead(LDR_PIN);


    // =================================
    // 2. Read PIR
    // =================================
    int motionValue = digitalRead(PIR_PIN);


    // =================================
    // 3. Read DHT22
    // =================================
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();


    // =================================
    // Print results
    // =================================

    Serial.println("--------------------------------");

    // LDR
    Serial.print("Light ADC: ");
    Serial.println(lightValue);

    // PIR
    Serial.print("Motion: ");

    if (motionValue == HIGH) {
        Serial.println("DETECTED");
    } else {
        Serial.println("NO MOTION");
    }

    // DHT22
    if (isnan(temperature) || isnan(humidity)) {

        Serial.println("DHT22: ERROR");

    } else {

        Serial.print("Temperature: ");
        Serial.print(temperature);
        Serial.println(" °C");

        Serial.print("Humidity: ");
        Serial.print(humidity);
        Serial.println(" %");
    }

    delay(2000);
}