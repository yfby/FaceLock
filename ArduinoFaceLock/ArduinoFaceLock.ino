// ============================================================
//  Passcode + Serial Button Controller
//  Buttons : 5x passcode | 1x enter | 1x send-to-USB
//  LEDs    : Green (unlocked/success) | Red (locked/error) | Blue (mode/USB activity)
//
//  Wiring (all buttons use INPUT_PULLUP — connect to GND):
//    Passcode buttons    → D2, D3, D4, D5, D6
//    Enter button        → D7
//    Send button         → D8
//    Red   LED (+220Ω)   → D9  → GND
//    Green LED (+220Ω)   → D10 → GND
//    Blue  LED (+220Ω)   → D11 → GND  (indicates recognition / USB activity)
// ============================================================

// ── Pin Definitions ─────────────────────────────────────────
const int BTN_PASSCODE[5]   = {2, 3, 4, 5, 6};
const int BTN_ENTER         = 7;
const int BTN_SEND          = 8;
const int LED_RED           = 9;
const int LED_GREEN         = 10;
const int LED_BLUE          = 11;

// ── Passcode Config ──────────────────────────────────────────
//  Edit PASSCODE_LENGTH and CORRECT_CODE to change your combo.
//  Each value is a button number (1–5).
//  Example below: press 1, 3, 2, 5 in order.
const int PASSCODE_LENGTH    = 4;
const int CORRECT_CODE[4]    = {1, 1, 3, 4};

// ── Timing ───────────────────────────────────────────────────
const unsigned long DEBOUNCE_MS   = 50;
const unsigned long LOCK_TIMEOUT  = 10000;  // ms of inactivity before auto-lock
const unsigned long LED_BLINK_MS  = 250;

// ── State ────────────────────────────────────────────────────
int  inputBuffer[PASSCODE_LENGTH];
int  inputIndex         = 0;
bool isUnlocked         = false;
bool isRecognitionMode  = false;

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
  pinMode(LED_BLUE,   OUTPUT);

  // Startup: both LEDs blink three times → board is alive
  blinkAll(3);
  setLockedState();
}

// ============================================================
void loop() {
  unsigned long now = millis();

  // Auto-lock on inactivity
  if (isUnlocked && (now - lastActivityTime > LOCK_TIMEOUT)) {
    lockDevice("TIMEOUT — auto-locked");
  }

  // Timeout face recognition if not timed out manually by host
  if (isRecognitionMode && (now - lastActivityTime > LOCK_TIMEOUT)) {
    exitRecognitionMode();
  }

  // ── Passcode buttons 1–5 ──
  for (int i = 0; i < 5; i++) {
    if (buttonJustPressed(i, BTN_PASSCODE[i], now)) {
      lastActivityTime = now;
      if (!isRecognitionMode) handlePasscodeInput(i + 1);
    }
  }

  // ── Enter button ──
  if (buttonJustPressed(5, BTN_ENTER, now)) {
    lastActivityTime = now;
    if (!isRecognitionMode) handleEnter();
  }

  // ── Face recognition mode button ──
  if (buttonJustPressed(6, BTN_SEND, now)) {
    lastActivityTime = now;
    if (!isRecognitionMode) enterRecognitionMode();
  }

  // ── Wait for host response ──
  if (isRecognitionMode && Serial.available()) {
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
    blinkGreen(1);  // quick feedback tap
    Serial.println(digit);
  } else {
    // Buffer is full -- indicate error and clear so the user may retry
    blinkRed(2);
    clearInput();
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
      lockDevice("No passcode entered");
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
    unlockDevice("Unlocked with passcode");
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
void enterRecognitionMode() {
  isRecognitionMode = true;

  // Pulse both LEDs once to show transmission
  blinkAll(1);

  // Enter face recogniton
  Serial.println("RECOGNITION_MODE");
  digitalWrite(LED_BLUE, HIGH);
}

void exitRecognitionMode() {
  isRecognitionMode = false;

  // Pulse both LEDs once to show transmission
  blinkAll(1);

  // Enter face recogniton
  Serial.println("EXIT_RECOGNITION_MODE");
  digitalWrite(LED_BLUE, LOW);
  lockDevice("RECOGNITION_MODE_TIMEDOUT");
}

// ============================================================
//  Handle host response over USB serial
// ============================================================
void handleSerialResponse(String cmd) {
  isRecognitionMode = false;
  digitalWrite(LED_BLUE, LOW);

  if (cmd == "FACE_RECOGNIZED") {
    unlockDevice("HOST:UNLOCKED_FACE_RECOGNIZED");

  } else if (cmd == "FACE_UNRECOGNIZED") {
    lockDevice("HOST:FACE_UNRECOGNIZED");

  } else if (cmd == "RESET") {
    lockDevice("HOST:RESET");

  } else {
    lockDevice("HOST:UNKNOWN_COMMAND");
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

void unlockDevice(const char* msg) {
  isUnlocked = true;
  lastActivityTime = millis();
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_RED,   LOW);
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
  bool redWas  = digitalRead(LED_RED);
  bool blueWas = digitalRead(LED_BLUE);
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_GREEN, HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(LED_GREEN, LOW);
    delay(LED_BLINK_MS);
  }
  digitalWrite(LED_RED,  redWas);
  digitalWrite(LED_BLUE, blueWas);
}

void blinkRed(int times) {
  bool greenWas = digitalRead(LED_GREEN);
  bool blueWas  = digitalRead(LED_BLUE);
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_RED, HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(LED_RED, LOW);
    delay(LED_BLINK_MS);
  }
  digitalWrite(LED_GREEN, greenWas);
  digitalWrite(LED_BLUE,  blueWas);
}

void blinkBlue(int times) {
  bool greenWas = digitalRead(LED_GREEN);
  bool redWas   = digitalRead(LED_RED);
  for (int i = 0; i < times; i++) {
    digitalWrite(LED_BLUE, HIGH);
    delay(LED_BLINK_MS);
    digitalWrite(LED_BLUE, LOW);
    delay(LED_BLINK_MS);
  }
  digitalWrite(LED_GREEN, greenWas);
  digitalWrite(LED_RED,   redWas);
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
