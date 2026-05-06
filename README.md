Dual-Factor Smart Lock System Using Arduino Passcode and Raspberry Pi Facial Recognition

This repo implements a dual-factor smart lock: an Arduino handles a local
passcode keypad and LEDs; a Raspberry Pi (or any Linux host) performs facial
recognition with a camera and acts as the second factor. The Arduino and Pi
talk over USB serial: the Arduino requests recognition, the Pi runs the
recognition and replies with an unlock/deny message.

The README below is intentionally concise and focused on what you need to
build and run the system on a Raspberry Pi.

## Quick summary

- Arduino: reads 5 passcode buttons, Enter button, and a Send/Recognition
  button. Controls Red/Green/Blue LEDs. Sketch: `ArduinoFaceLock/ArduinoFaceLock.ino`.
- Raspberry Pi: runs the Python host `main.py`, captures camera frames and
  performs recognition using `face_recognition`/dlib. Replies to Arduino with
  `FACE_RECOGNIZED` or `FACE_UNRECOGNIZED`.
- Face data: persisted to `faces.json` (name -> encoding list).

## Circuit (ASCII map)

Below is a simple ASCII diagram showing how to wire components to an Arduino
(Uno/Nano). All buttons use INPUT_PULLUP, so the other side of each button
connects to GND. LEDs use series resistors (about 220Ω) to GND.

```
                 +5V
                  |
                 [ ]  (not used directly; buttons use INPUT_PULLUP)

Arduino UNO / Nano
-------------------
            _______
           |       |
  D2 ----o-| BTN1  |
  D3 ----o-| BTN2  |
  D4 ----o-| BTN3  |
  D5 ----o-| BTN4  |
  D6 ----o-| BTN5  |
           |       |
  D7 ----o-| ENTER |
  D8 ----o-| SEND  |---(USB)----> Raspberry Pi (serial) ---(USB)----> CAMERA
           |       |
  D9 ----[220Ω]----|> RED LED  -> GND
 D10 ----[220Ω]----|> GREEN LED-> GND
 D11 ----[220Ω]----|> BLUE LED -> GND
           |_______|

Buttons: other side -> GND
LEDs: cathode -> GND, anode -> resistor -> digital pin

USB: connect the Arduino to the Raspberry Pi's USB port. The Pi will see a
serial device (commonly /dev/ttyACM0 or /dev/ttyUSB0).
```

## Raspberry Pi host: quick setup

1. Hardware: connect a Pi camera or USB webcam and the Arduino over USB.
2. OS: Raspberry Pi OS (or similar). Make sure the camera works (e.g.
   with libcamera or OpenCV).
3. Python: create a virtualenv and install dependencies:

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install face_recognition numpy opencv-python pyserial
```

4. Run the host program (adjust port if necessary):

```sh
python3 main.py --port /dev/ttyACM0 --baud 9600 --timeout 10
```

On the Pi, the serial device is commonly `/dev/ttyACM0` (Uno/Leonardo) or
`/dev/ttyUSB0` (some USB-serial adapters). If you get a permission error,
add your user to the `dialout` group: `sudo usermod -aG dialout $USER` and
re-login.

## Enrolling faces (simplified)

Use this short Python snippet on the Raspberry Pi to add a face from an image
file. The repo contains `faces.FaceManager` which handles persistence.

```py
from faces import FaceManager
import face_recognition

fm = FaceManager('faces.json')
img = face_recognition.load_image_file('alice.jpg')
enc = face_recognition.face_encodings(img)
if enc:
    fm.enroll('alice', enc[0])
    print('enrolled alice')
else:
    print('no face found')
```

After this, the Pi will recognize "alice" when the camera sees her face and
the Arduino has requested recognition.

## How it works (runtime flow)

1. User either enters a correct passcode locally (Arduino only) or presses
   the SEND/Recognition button to request facial recognition.
2. If SEND is pressed, Arduino prints `RECOGNITION_MODE` on serial and turns
   the Blue LED on.
3. Raspberry Pi receives the request, captures camera frames and tries to
   match against enrolled faces using stored encodings. If a match is found
   within the timeout, Pi sends `FACE_RECOGNIZED`, otherwise `FACE_UNRECOGNIZED`.
4. Arduino receives the response and either unlocks (Green LED) or shows an
   error (Red LED).

