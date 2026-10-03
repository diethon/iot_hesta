#include <Arduino.h>
#include <IRrecv.h>
#include <IRutils.h>

// ============================================================
// CASPER YKR-H/102E
// POWER BIT ANALYZER
// ============================================================
//
// VS1838B:
// VCC -> 3.3V
// GND -> GND
// OUT -> GPIO27
//
// Mục tiêu:
//   1. Capture Power ON
//   2. Capture Power OFF
//   3. Chuyển RAW -> 104 bit
//   4. So sánh ON/OFF
//   5. Tìm bit liên quan đến Power
//
// ============================================================

const uint16_t IR_RECEIVE_PIN = 13;

const uint16_t CAPTURE_BUFFER_SIZE = 1024;
const uint8_t TIMEOUT = 50;

const uint16_t NUM_BITS = 104;

IRrecv irrecv(
  IR_RECEIVE_PIN,
  CAPTURE_BUFFER_SIZE,
  TIMEOUT,
  true
);

decode_results results;


// ============================================================
// STORAGE
// ============================================================

uint8_t powerOnBits[NUM_BITS];
uint8_t powerOffBits[NUM_BITS];

uint32_t powerOnSpaces[NUM_BITS];
uint32_t powerOffSpaces[NUM_BITS];

bool hasPowerOn = false;
bool hasPowerOff = false;


// ============================================================
// FUNCTION: classify timing
// ============================================================
//
// ~530-600 us   = bit 0
// ~1650-1710 us = bit 1
//
// Có tolerance rộng để tránh ảnh hưởng AGC.
// ============================================================

int classifyBit(uint32_t space) {

  // Logical 0
  if (space >= 450 && space <= 900) {
    return 0;
  }

  // Logical 1
  if (space >= 1300 && space <= 1900) {
    return 1;
  }

  // Không xác định
  return -1;
}


// ============================================================
// CONVERT RAW -> 104 BIT
// ============================================================
//
// RAW structure:
//
// index 1 = header mark   ~9050
// index 2 = header space  ~4480
//
// bit 0:
// index 3 = mark
// index 4 = space
//
// bit 1:
// index 5 = mark
// index 6 = space
//
// ...
//
// bit 103:
// index 209 = mark
// index 210 = space
//
// index 211 = trailing mark
//
// ============================================================

bool decode104Bits(
  decode_results &r,
  uint8_t outputBits[],
  uint32_t outputSpaces[]
) {

  if (r.rawlen < 212) {

    Serial.println();
    Serial.println("ERROR: RAW frame too short!");
    Serial.print("RawLen = ");
    Serial.println(r.rawlen);

    return false;
  }

  for (uint16_t bit = 0; bit < NUM_BITS; bit++) {

    // Space index:
    // bit 0  -> 4
    // bit 1  -> 6
    // bit 2  -> 8
    //
    // formula:
    // 4 + bit * 2

    uint16_t spaceIndex = 4 + (bit * 2);

    uint32_t space =
      r.rawbuf[spaceIndex] * kRawTick;

    int decoded = classifyBit(space);

    if (decoded < 0) {

      Serial.println();
      Serial.println("ERROR: Cannot classify bit!");

      Serial.print("Bit       : ");
      Serial.println(bit);

      Serial.print("RAW index : ");
      Serial.println(spaceIndex);

      Serial.print("Timing    : ");
      Serial.print(space);
      Serial.println(" us");

      return false;
    }

    outputBits[bit] = decoded;
    outputSpaces[bit] = space;
  }

  return true;
}


// ============================================================
// PRINT BIT STREAM
// ============================================================

void printBitStream(
  const char *name,
  uint8_t bits[]
) {

  Serial.println();
  Serial.println("============================================================");
  Serial.print(name);
  Serial.println(" - 104 BIT STREAM");
  Serial.println("============================================================");

  for (uint16_t i = 0; i < NUM_BITS; i++) {

    Serial.print(bits[i]);

    if ((i + 1) % 8 == 0) {
      Serial.print(" ");
    }
  }

  Serial.println();
}


// ============================================================
// PRINT BIT TABLE
// ============================================================

void printBitTable() {

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" POWER ON vs POWER OFF");
  Serial.println("============================================================");

  Serial.println();
  Serial.println(
    "BIT   ON   OFF   DIFFER   ON_US   OFF_US"
  );

  Serial.println(
    "------------------------------------------------------------"
  );

  for (uint16_t i = 0; i < NUM_BITS; i++) {

    Serial.printf(
      "%03d    %d     %d      %s     %4lu     %4lu\n",
      i,
      powerOnBits[i],
      powerOffBits[i],
      (powerOnBits[i] != powerOffBits[i])
        ? "<---"
        : "-",
      powerOnSpaces[i],
      powerOffSpaces[i]
    );
  }
}


// ============================================================
// PRINT DIFFERENT BITS ONLY
// ============================================================

void printDifferentBits() {

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" DIFFERENT BITS");
  Serial.println("============================================================");

  bool found = false;

  for (uint16_t i = 0; i < NUM_BITS; i++) {

    if (powerOnBits[i] != powerOffBits[i]) {

      found = true;

      Serial.print("Bit ");
      Serial.print(i);

      Serial.print(" : ON=");
      Serial.print(powerOnBits[i]);

      Serial.print(" OFF=");
      Serial.print(powerOffBits[i]);

      Serial.print(" | ON=");
      Serial.print(powerOnSpaces[i]);

      Serial.print("us OFF=");
      Serial.print(powerOffSpaces[i]);

      Serial.println("us");
    }
  }

  if (!found) {

    Serial.println(
      "NO DIFFERENT BITS FOUND."
    );
  }
}


// ============================================================
// BUILD BYTE - MSB FIRST
// ============================================================

void buildMSBBytes(
  uint8_t bits[],
  uint8_t bytes[]
) {

  memset(bytes, 0, 13);

  for (uint16_t i = 0; i < NUM_BITS; i++) {

    bytes[i / 8] <<= 1;

    bytes[i / 8] |= bits[i];
  }
}


// ============================================================
// BUILD BYTE - LSB FIRST
// ============================================================

void buildLSBBytes(
  uint8_t bits[],
  uint8_t bytes[]
) {

  memset(bytes, 0, 13);

  for (uint16_t i = 0; i < NUM_BITS; i++) {

    bytes[i / 8] |=
      bits[i] << (i % 8);
  }
}


// ============================================================
// PRINT HEX
// ============================================================

void printHex(
  uint8_t bytes[],
  uint8_t count
) {

  for (uint8_t i = 0; i < count; i++) {

    if (bytes[i] < 0x10) {
      Serial.print("0");
    }

    Serial.print(
      bytes[i],
      HEX
    );

    if (i < count - 1) {
      Serial.print(" ");
    }
  }

  Serial.println();
}


// ============================================================
// PRINT HEX REPRESENTATION
// ============================================================

void printHexRepresentation(
  const char *name,
  uint8_t bits[]
) {

  uint8_t msbBytes[13];
  uint8_t lsbBytes[13];

  buildMSBBytes(
    bits,
    msbBytes
  );

  buildLSBBytes(
    bits,
    lsbBytes
  );

  Serial.println();
  Serial.println("------------------------------------------------------------");

  Serial.print(name);
  Serial.println(" - MSB FIRST:");

  printHex(
    msbBytes,
    13
  );

  Serial.print(name);
  Serial.println(" - LSB FIRST:");

  printHex(
    lsbBytes,
    13
  );
}


// ============================================================
// PRINT SUMMARY
// ============================================================

void printSummary() {

  Serial.println();
  Serial.println();
  Serial.println("############################################################");
  Serial.println("#                  ANALYSIS RESULT                         #");
  Serial.println("############################################################");

  Serial.println();

  if (!hasPowerOn) {

    Serial.println("Power ON : NOT CAPTURED");
  }
  else {

    Serial.println("Power ON : CAPTURED");
  }

  if (!hasPowerOff) {

    Serial.println("Power OFF: NOT CAPTURED");
  }
  else {

    Serial.println("Power OFF: CAPTURED");
  }

  if (!(hasPowerOn && hasPowerOff)) {

    Serial.println();
    Serial.println(
      "Need BOTH Power ON and Power OFF."
    );

    return;
  }

  printBitStream(
    "POWER ON",
    powerOnBits
  );

  printBitStream(
    "POWER OFF",
    powerOffBits
  );

  printHexRepresentation(
    "POWER ON",
    powerOnBits
  );

  printHexRepresentation(
    "POWER OFF",
    powerOffBits
  );

  printBitTable();

  printDifferentBits();

  Serial.println();
  Serial.println("############################################################");
  Serial.println("#                     END RESULT                           #");
  Serial.println("############################################################");

  Serial.println();
}


// ============================================================
// RESET CAPTURE
// ============================================================

void resetAnalyzer() {

  hasPowerOn = false;
  hasPowerOff = false;

  memset(
    powerOnBits,
    0,
    sizeof(powerOnBits)
  );

  memset(
    powerOffBits,
    0,
    sizeof(powerOffBits)
  );

  memset(
    powerOnSpaces,
    0,
    sizeof(powerOnSpaces)
  );

  memset(
    powerOffSpaces,
    0,
    sizeof(powerOffSpaces)
  );

  Serial.println();
  Serial.println(
    "Analyzer reset."
  );

  Serial.println();
  Serial.println(
    "1. Turn AC ON."
  );

  Serial.println(
    "2. Wait 2-3 seconds."
  );

  Serial.println(
    "3. Capture Power ON."
  );

  Serial.println(
    "4. Then turn AC OFF."
  );

  Serial.println(
    "5. Capture Power OFF."
  );

  Serial.println();
}


// ============================================================
// PROCESS CAPTURE
// ============================================================

void processCapture() {

  Serial.println();
  Serial.println("============================================================");
  Serial.println("NEW IR SIGNAL");
  Serial.println("============================================================");

  Serial.print("Protocol : ");
  Serial.println(
    typeToString(results.decode_type)
  );

  Serial.print("Bits     : ");
  Serial.println(results.bits);

  Serial.print("Value    : 0x");

  serialPrintUint64(
    results.value,
    HEX
  );

  Serial.println();

  Serial.print("RawLen   : ");
  Serial.println(results.rawlen);


  // ==========================================================
  // Decode 104 bits
  // ==========================================================

  uint8_t tempBits[NUM_BITS];
  uint32_t tempSpaces[NUM_BITS];

  bool success =
    decode104Bits(
      results,
      tempBits,
      tempSpaces
    );

  if (!success) {

    Serial.println();
    Serial.println(
      ">>> 104-bit decode FAILED."
    );

    Serial.println();

    return;
  }


  // ==========================================================
  // Determine what capture this is
  // ==========================================================

  if (!hasPowerOn) {

    memcpy(
      powerOnBits,
      tempBits,
      sizeof(tempBits)
    );

    memcpy(
      powerOnSpaces,
      tempSpaces,
      sizeof(tempSpaces)
    );

    hasPowerOn = true;

    Serial.println();
    Serial.println(
      ">>> SAVED AS POWER ON"
    );
  }

  else if (!hasPowerOff) {

    memcpy(
      powerOffBits,
      tempBits,
      sizeof(tempBits)
    );

    memcpy(
      powerOffSpaces,
      tempSpaces,
      sizeof(tempSpaces)
    );

    hasPowerOff = true;

    Serial.println();
    Serial.println(
      ">>> SAVED AS POWER OFF"
    );
  }

  else {

    Serial.println();
    Serial.println(
      "Both captures already exist."
    );

    Serial.println(
      "Press RESET to start again."
    );

    return;
  }


  // ==========================================================
  // Print current capture
  // ==========================================================

  if (hasPowerOn && !hasPowerOff) {

    printBitStream(
      "CAPTURED POWER ON",
      powerOnBits
    );

    printHexRepresentation(
      "POWER ON",
      powerOnBits
    );

    Serial.println();
    Serial.println(
      ">>> Now capture POWER OFF."
    );
  }


  // ==========================================================
  // If both available -> compare
  // ==========================================================

  if (hasPowerOn && hasPowerOff) {

    printSummary();
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  irrecv.enableIRIn();

  Serial.println();
  Serial.println();
  Serial.println("############################################################");
  Serial.println("#                                                          #");
  Serial.println("#       CASPER YKR-H/102E POWER BIT ANALYZER             #");
  Serial.println("#                                                          #");
  Serial.println("############################################################");

  Serial.println();

  Serial.print(
    "IR Receiver GPIO: "
  );

  Serial.println(
    IR_RECEIVE_PIN
  );

  Serial.println();

  Serial.println(
    "EXPECTED FRAME:"
  );

  Serial.println(
    "Header = ~9050 + ~4480 us"
  );

  Serial.println(
    "Bit 0  = index 3 + 4"
  );

  Serial.println(
    "Bit 103 = index 209 + 210"
  );

  Serial.println(
    "Total = 104 bits"
  );

  Serial.println();

  Serial.println(
    "============================================================"
  );

  Serial.println(
    "TEST PROCEDURE"
  );

  Serial.println(
    "============================================================");

  Serial.println();

  Serial.println(
    "1. Set AC to COOL / 26C / FAN AUTO."
  );

  Serial.println(
    "2. Make sure SWING OFF and TIMER OFF."
  );

  Serial.println(
    "3. Turn AC ON."
  );

  Serial.println(
    "4. Press POWER ON once."
  );

  Serial.println(
    "5. Wait for capture."
  );

  Serial.println(
    "6. Then press POWER OFF."
  );

  Serial.println(
    "7. Wait for second capture."
  );

  Serial.println();

  Serial.println(
    "Ready..."
  );

  Serial.println();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  if (irrecv.decode(&results)) {

    processCapture();

    irrecv.resume();
  }

  delay(10);
}