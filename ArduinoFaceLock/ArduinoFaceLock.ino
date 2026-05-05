// ============================================================
//  Passcode + Serial Button Controller
//  Buttons : 5x passcode | 1x enter | 1x send-to-USB
//  LEDs    : Green (unlocked/success) | Red (locked/error)
//
//  Wiring (all buttons use INPUT_PULLUP — connect to GND):
//    Passcode buttons    → D2, D3, D4, D5, D6
//    Enter button        → D7
//    Send button         → D8
//    Red   LED (+220Ω)   → D9  → GND
//    Green LED (+220Ω)   → D10 → GND
// ============================================================

// ── Pin Definitions ─────────────────────────────────────────
const int BTN_PASSCODE[5]   = {2, 3, 4, 5, 6};
const int BTN_ENTER         = 7;
const int BTN_SEND          = 8;
const int LED_RED           = 9;
const int LED_GREEN         = 10;
const int LED_BLUE          = 11

// ── Passcode Config ──────────────────────────────────────────
//  Edit PASSCODE_LENGTH and CORRECT_CODE to change your combo.
//  Each value is a button number (1–5).
//  Example below: press 1, 3, 2, 5 in order.
const int PASSCODE_LENGTH    = 4;
const int CORRECT_CODE[4]    = {1, 3, 2, 5};

// ── Timing ───────────────────────────────────────────────────
const unsigned long DEBOUNCE_MS   = 50;
const unsigned long LOCK_TIMEOUT  = 10000;  // ms of inactivity before auto-lock
const unsigned long LED_BLINK_MS  = 250;

// ── State ────────────────────────────────────────────────────
int  inputBuffer[PASSCODE_LENGTH];
int  inputIndex         = 0;
bool isUnlocked         = false;
bool waitingForSerial   = false;

unsigned long lastActivityTime = 0;

// Debounce tracking (indices 0-4 = passcode, 5 = enter, 6 = send)
unsigned long lastDebounceTime[7] = {0};
bool          lastButtonState[7];
bool          stableButtonState[7];

// ============================================================
void setup() {
  Serial.begin(9600);

  // Passcode buttons
  for (int i = 0; i < 5; i++) {
    pinMode(BTN_PASSCODE[i], INPUT_PULLUP);
    lastButtonState[i] = HIGH;
    stableButtonState[i] = HIGH;
  }
  // Enter & Send buttons
  pinMode(BTN_ENTER,  INPUT_PULLUP);
  pinMode(BTN_SEND,   INPUT_PULLUP);
  lastButtonState[5] = HIGH;  stableButtonState[5] = HIGH;
  lastButtonState[6] = HIGH;  stableButtonState[6] = HIGH;

  // LEDs
  pinMode(LED_GREEN,  OUTPUT);
  pinMode(LED_RED,    OUTPUT);
  pinMode(LED_BLUE,   OUTPUT)

  // Startup: both LEDs blink three times → board is alive
  blinkBoth(3);

  setLockedState();
  Serial.println("READY");
  Serial.println("---");
  Serial.println("Host commands (newline-terminated):");
  Serial.println("  UNLOCK | LOCK | RESET | OK");
  Serial.println("---");
}

// ============================================================
void loop() {
  unsigned long now = millis();

  // Auto-lock on inactivity
  if (isUnlocked && (now - lastActivityTime > LOCK_TIMEOUT)) {
    lockDevice("TIMEOUT — auto-locked");
  }

  // ── Passcode buttons 1–5 ──
  for (int i = 0; i < 5; i++) {
    if (buttonJustPressed(i, BTN_PASSCODE[i], now)) {
      lastActivityTime = now;
      if (!waitingForSerial) handlePasscodeInput(i + 1);
    }
  }

  // ── Enter button ──
  if (buttonJustPressed(5, BTN_ENTER, now)) {
    lastActivityTime = now;
    if (!waitingForSerial) handleEnter();
  }

  // ── Send button ──
  if (buttonJustPressed(6, BTN_SEND, now)) {
    lastActivityTime = now;
    if (!waitingForSerial) handleSend();
  }

  // ── Wait for host response ──
  if (waitingForSerial && Serial.available()) {
    String response = Serial.readStringUntil('\n');
    response.trim();
    handleSerialResponse(response);
  }
}

// ============================================================
//  Debounce — returns true on a clean falling edge (button press)
// ============================================================
bool buttonJustPressed(int idx, int pin, unsigned long now) {
  bool reading = digitalRead(pin);

  if (reading != lastButtonState[idx]) {
    lastDebounceTime[idx] = now;
  }
  lastButtonState[idx] = reading;

  bool pressed = false;
  if ((now - lastDebounceTime[idx]) > DEBOUNCE_MS) {
    if (reading != stableButtonState[idx]) {
      stableButtonState[idx] = reading;
      if (stableButtonState[idx] == LOW) pressed = true;  // LOW = pressed (pullup)
    }
  }
  return pressed;
}

// ============================================================
//  Passcode digit input
// ============================================================
void handlePasscodeInput(int digit) {
  if (inputIndex < PASSCODE_LENGTH) {
    inputBuffer[inputIndex++] = digit;

    // Print masked input progress
    Serial.print("Input [");
    for (int i = 0; i < inputIndex; i++)   Serial.print("*");
    for (int i = inputIndex; i < PASSCODE_LENGTH; i++) Serial.print("_");
    Serial.println("]");

    blinkGreen(1);  // quick feedback tap
  } else {
    // Buffer already full
    Serial.println("Buffer full — press ENTER to submit or ENTER with nothing to clear.");
    blinkRed(2);
  }
}

// ============================================================
//  Enter — validate passcode
// ============================================================
void handleEnter() {
  // Pressing Enter with nothing clears any leftover state
  if (inputIndex == 0) {
    if (isUnlocked) {
      lockDevice("Manually locked");
    } else {
      Serial.println("(No input — enter passcode digits first)");
    }
    return;
  }

  bool match = (inputIndex == PASSCODE_LENGTH);
  if (match) {
    for (int i = 0; i < PASSCODE_LENGTH; i++) {
      if (inputBuffer[i] != CORRECT_CODE[i]) { match = false; break; }
    }
  }

  clearInput();

  if (match) {
    isUnlocked = true;
    lastActivityTime = millis();
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED,   LOW);
    Serial.println("UNLOCKED ✓");
  } else {
    isUnlocked = false;
    Serial.println("WRONG PASSCODE ✗");
    blinkRed(4);       // angry blink
    setLockedState();
  }
}

// ============================================================
//  Send — transmit status over USB and wait for a host command
// ============================================================
void handleSend() {
  waitingForSerial = true;

  // Pulse both LEDs once to show transmission
  blinkBoth(1);

  // Send a simple JSON payload — the host can parse and respond
  Serial.print("{\"event\":\"BUTTON_SEND\",\"unlocked\":");
  Serial.print(isUnlocked ? "true" : "false");
  Serial.print(",\"inputPending\":");
  Serial.print(inputIndex > 0 ? "true" : "false");
  Serial.println("}");

  // Slow-blink red to show we're waiting
  blinkRed(1);
  Serial.println("(waiting for host response…)");
}

// ============================================================
//  Handle host response over USB serial
//
//  Supported commands (case-sensitive, newline-terminated):
//    UNLOCK  → force unlock regardless of passcode
//    LOCK    → force lock
//    RESET   → clear input buffer and lock
//    OK      → acknowledge, restore LEDs, no state change
//  Anything else is echoed back as UNKNOWN.
// ============================================================
void handleSerialResponse(String cmd) {
  waitingForSerial = false;

  if (cmd == "UNLOCK") {
    isUnlocked = true;
    lastActivityTime = millis();
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED,   LOW);
    Serial.println("ACK:UNLOCK");

  } else if (cmd == "LOCK") {
    lockDevice("ACK:LOCK");

  } else if (cmd == "RESET") {
    clearInput();
    lockDevice("ACK:RESET");

  } else if (cmd == "OK") {
    Serial.println("ACK:OK");
    // Restore LED state to match current lock status
    if (isUnlocked) {
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_RED,   LOW);
    } else {
      setLockedState();
    }

  } else {
    Serial.print("UNKNOWN: \"");
    Serial.print(cmd);
    Serial.println("\"");
    blinkRed(2);
    if (isUnlocked) {
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_RED,   LOW);
    } else {
      setLockedState();
    }
  }
}

// ============================================================
//  Helpers
// ============================================================

void lockDevice(const char* msg) {
  isUnlocked = false;
  clearInput();
  setLockedState();
  Serial.println(msg);
}

void clearInput() {
  inputIndex = 0;
  memset(inputBuffer, 0, sizeof(inputBuffer));
}

// Locked & ready: Red steady on, Green off
void setLockedState() {
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED,   HIGH);
}

void blinkGreen(int times) {
  bool redWas = digitalRead(LED_RED);
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_GREEN, HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(LED_GREEN, LOW);
    delay(LED_BLINK_MS);
  }
  // Restore red to what it was
  digitalWrite(LED_RED, redWas);
}

void blinkRed(int times) {
  bool greenWas = digitalRead(LED_GREEN);
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_RED, HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(LED_RED, LOW);
    delay(LED_BLINK_MS);
  }
  digitalWrite(LED_GREEN, greenWas);
}

void blinkAll(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED,   HIGH);
    digitalWrite(LED_BLUE,  HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED,   LOW);
    digitalWrite(LED_BLUE,  LOW);
    delay(LED_BLINK_MS);
  }
}

