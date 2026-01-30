/**
 * E32-TTL-100 Transceiver Interface - RECEIVER
 * Receives JSON telemetry and prints to Serial for Python Bridge
 */
#include "E32-TTL-100.h"
#include <SoftwareSerial.h>

// Enable this for RECEIVER mode
#define Device_A

#define M0_PIN 7
#define M1_PIN 8
#define AUX_PIN A0
#define SOFT_RX 10
#define SOFT_TX 11

SoftwareSerial softSerial(SOFT_RX, SOFT_TX);

//=== AUX ===========================================+
bool AUX_HL;
bool ReadAUX() {
  int val = analogRead(AUX_PIN);
  if (val < 50) {
    AUX_HL = LOW;
  } else {
    AUX_HL = HIGH;
  }
  return AUX_HL;
}

RET_STATUS WaitAUX_H() {
  RET_STATUS STATUS = RET_SUCCESS;
  uint8_t cnt = 0;

  while ((ReadAUX() == LOW) && (cnt++ < TIME_OUT_CNT)) {
    Serial.print(".");
    delay(100);
  }

  if (cnt >= TIME_OUT_CNT) {
    STATUS = RET_TIMEOUT;
    Serial.println(" TimeOut");
  }

  return STATUS;
}
//=== AUX ===========================================-

//=== Mode Select ===================================+
bool chkModeSame(MODE_TYPE mode) {
  static MODE_TYPE pre_mode = MODE_INIT;

  if (pre_mode == mode) {
    return true;
  } else {
    Serial.print("SwitchMode: from ");
    Serial.print(pre_mode, HEX);
    Serial.print(" to ");
    Serial.println(mode, HEX);
    pre_mode = mode;
    return false;
  }
}
void SwitchMode(MODE_TYPE mode) {
  if (!chkModeSame(mode)) {
    WaitAUX_H();

    if (mode == MODE_0_NORMAL) {
      digitalWrite(M0_PIN, LOW);
      digitalWrite(M1_PIN, LOW);
    } else if (mode == MODE_3_SLEEP) {
      digitalWrite(M0_PIN, HIGH);
      digitalWrite(M1_PIN, HIGH);
    }

    delay(50);
    WaitAUX_H();
  }
}
//=== Mode Select ===================================-

//=== Basic cmd =====================================+
void cleanUARTBuf() {
  while (softSerial.available()) {
    softSerial.read();
  }
}

void triple_cmd(SLEEP_MODE_CMD_TYPE Tcmd) {
  uint8_t CMD[3] = {Tcmd, Tcmd, Tcmd};
  softSerial.write(CMD, 3);
  delay(50);
}

RET_STATUS Module_info(uint8_t *pReadbuf, uint8_t buf_len) {
  RET_STATUS STATUS = RET_SUCCESS;
  uint8_t Readcnt, idx;

  Readcnt = softSerial.available();
  if (Readcnt == buf_len) {
    for (idx = 0; idx < buf_len; idx++) {
      *(pReadbuf + idx) = softSerial.read();
    }
  } else {
    STATUS = RET_DATA_SIZE_NOT_MATCH;
    cleanUARTBuf();
  }

  return STATUS;
}
//=== Basic cmd =====================================-

//=== Sleep mode cmd ================================+
RET_STATUS Write_CFG_PDS(struct CFGstruct *pCFG) {
  softSerial.write((uint8_t *)pCFG, 6);
  WaitAUX_H();
  delay(1200);
  return RET_SUCCESS;
}

RET_STATUS Read_CFG(struct CFGstruct *pCFG) {
  RET_STATUS STATUS = RET_SUCCESS;

  cleanUARTBuf();
  triple_cmd(R_CFG);

  STATUS = Module_info((uint8_t *)pCFG, sizeof(CFGstruct));
  if (STATUS == RET_SUCCESS) {
    Serial.print("  HEAD:     ");
    Serial.println(pCFG->HEAD, HEX);
    Serial.print("  ADDH:     ");
    Serial.println(pCFG->ADDH, HEX);
    Serial.print("  ADDL:     ");
    Serial.println(pCFG->ADDL, HEX);
    Serial.print("  CHAN:     ");
    Serial.println(pCFG->CHAN, HEX);
  }

  return STATUS;
}

RET_STATUS Read_module_version(struct MVerstruct *MVer) {
  RET_STATUS STATUS = RET_SUCCESS;

  cleanUARTBuf();
  triple_cmd(R_MODULE_VERSION);

  STATUS = Module_info((uint8_t *)MVer, sizeof(MVerstruct));
  if (STATUS == RET_SUCCESS) {
    Serial.print("  HEAD:     0x");
    Serial.println(MVer->HEAD, HEX);
    Serial.print("  Model:    0x");
    Serial.println(MVer->Model, HEX);
    Serial.print("  Version:  0x");
    Serial.println(MVer->Version, HEX);
    Serial.print("  features: 0x");
    Serial.println(MVer->features, HEX);
  }

  return RET_SUCCESS;
}

void Reset_module() {
  triple_cmd(W_RESET_MODULE);
  WaitAUX_H();
  delay(1000);
}

RET_STATUS SleepModeCmd(uint8_t CMD, void *pBuff) {
  RET_STATUS STATUS = RET_SUCCESS;

  WaitAUX_H();
  SwitchMode(MODE_3_SLEEP);

  switch (CMD) {
  case W_CFG_PWR_DWN_SAVE:
    STATUS = Write_CFG_PDS((struct CFGstruct *)pBuff);
    break;
  case R_CFG:
    STATUS = Read_CFG((struct CFGstruct *)pBuff);
    break;
  case W_CFG_PWR_DWN_LOSE:
    break;
  case R_MODULE_VERSION:
    Read_module_version((struct MVerstruct *)pBuff);
    break;
  case W_RESET_MODULE:
    Reset_module();
    break;
  default:
    return RET_INVALID_PARAM;
  }

  WaitAUX_H();
  return STATUS;
}
//=== Sleep mode cmd ================================-

RET_STATUS SettingModule(struct CFGstruct *pCFG) {
  RET_STATUS STATUS = RET_SUCCESS;

#ifdef Device_A
  pCFG->ADDH = DEVICE_A_ADDR_H;
  pCFG->ADDL = DEVICE_A_ADDR_L;
#else
  pCFG->ADDH = DEVICE_B_ADDR_H;
  pCFG->ADDL = DEVICE_B_ADDR_L;
#endif

  pCFG->OPTION_bits.trsm_mode = TRSM_FP_MODE;
  pCFG->OPTION_bits.tsmt_pwr = TSMT_PWR_10DB;

  STATUS = SleepModeCmd(W_CFG_PWR_DWN_SAVE, (void *)pCFG);
  SleepModeCmd(W_RESET_MODULE, NULL);
  STATUS = SleepModeCmd(R_CFG, (void *)pCFG);

  return STATUS;
}

RET_STATUS ReceiveMsg(uint8_t *pdatabuf, uint8_t *data_len) {
  SwitchMode(MODE_0_NORMAL);

  // 1. Check if there's at least one byte starting to arrive
  if (softSerial.available() > 0) {
    // 2. WAIT for the module to finish receiving the full packet
    // While AUX is LOW, the module is still busy receiving/processing
    uint32_t startTime = millis();
    while (ReadAUX() == LOW && (millis() - startTime < 1000)) {
      // Wait for AUX to go HIGH (with a 1s safety timeout)
    }

    // 3. Small "settle" delay.
    // At 9600 baud, 1 byte ~= 1ms. 60 bytes ~= 60ms.
    // If AUX goes high "after" transmission, we might still want a buffer.
    // Increasing delay to allow full buffer fill if AUX is slightly early or
    // jittery.
    delay(50);

    // 4. Now read everything available
    uint8_t idx = 0;
    while (softSerial.available() && idx < 100) {
      pdatabuf[idx++] = softSerial.read();
      // Safety: allow next byte to arrive if buffer is emptying fast
      if (!softSerial.available())
        delay(2);
    }
    *data_len = idx;

    Serial.print("[DEBUG] Received Bytes: ");
    Serial.println(*data_len);

    // --- IMPORTANT: PRINT RAW MESSAGE FOR PYTHON BRIDGE ---
    // The Python bridge looks for the JSON string
    // Filter to only print the JSON part (starts with '{' and ends with '}')

    char *msg = (char *)pdatabuf;

    // Simple filter: print characters if they are printable
    // Or better, just print the string assuming null termination if possible?
    // We'll manual print to be safe.

    bool jsonStarted = false;
    for (uint8_t i = 0; i < *data_len; i++) {
      if (pdatabuf[i] == '[')
        jsonStarted = true;

      if (jsonStarted) {
        Serial.write(pdatabuf[i]);
        if (pdatabuf[i] == ']') {
          jsonStarted = false;
          Serial.println(); // Newline at end of JSON
        }
      }
    }
    // -----------------------------------------------------

    return RET_SUCCESS;
  }

  return RET_NOT_IMPLEMENT;
}

void setup() {
  RET_STATUS STATUS = RET_SUCCESS;
  struct CFGstruct CFG;
  struct MVerstruct MVer;

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  softSerial.begin(9600);
  Serial.begin(9600);
  Serial.println("[RECEIVER - Device A]");

  STATUS = SleepModeCmd(R_CFG, (void *)&CFG);
  STATUS = SettingModule(&CFG);
  STATUS = SleepModeCmd(R_MODULE_VERSION, (void *)&MVer);

  SwitchMode(MODE_0_NORMAL);
  WaitAUX_H();
  delay(10);

  if (STATUS == RET_SUCCESS)
    Serial.println("Setup init OK!! Waiting for data...");
}

void blinkLED() {
  static bool LedStatus = LOW;
  digitalWrite(LED_BUILTIN, LedStatus);
  LedStatus = !LedStatus;
}

void loop() {
  uint8_t data_buf[100], data_len;

  // RECEIVER mode
  if (ReceiveMsg(data_buf, &data_len) == RET_SUCCESS) {
    blinkLED();
  }
}
