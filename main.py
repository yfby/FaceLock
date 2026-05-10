import argparse
import uuid
import time
import sys
import serial
from faces import FaceManager


def do_recognition(face_mgr: FaceManager, timeout_s: int = 5):
    """Attempt to capture and recognize a face within timeout_s seconds.

    Returns a tuple (recognized: bool, name_or_reason: str). If no face is
    captured before the timeout, returns (False, "Timeout"). If a face is
    captured but not matched, returns (False, "Unknown").
    """
    deadline = time.time() + float(timeout_s)
    # Keep a small sleep to avoid a tight busy loop if the webcam isn't
    # returning frames immediately.
    while time.time() < deadline:
        encoding = face_mgr.capture_face_from_webcam()
        if encoding is None:
            # No face captured this iteration; try again until timeout
            time.sleep(0.1)
            continue

        name = face_mgr.recognize(encoding)
        if name != "Unknown":
            return True, name
        return False, "Unknown"

    return False, "Timeout"


def enroll_face(face_mgr: FaceManager, timeout_s: int = 5):
    """Attempt to capture and recognize a face within timeout_s seconds.
    
    Capture webcam and enrolls face if not existint in loaded data.
    returns True if successfull and False if failed enrollment.
    """
    deadline = time.time() + float(timeout_s)
    # Keep a small sleep to avoid a tight busy loop if the webcam isn't
    # returning frames immediately.
    while time.time() < deadline:
        encoding = face_mgr.capture_face_from_webcam()
        if encoding is None:
            # No face captured this iteration; try again until timeout
            time.sleep(0.1)
            continue

        if face_mgr.recognize(encoding) != "Unknown":
            return False

        return face_mgr.enroll(str(uuid.uuid1()), encoding)

    return False, "Timeout"


def main():
    parser = argparse.ArgumentParser(description="Host bridge for Arduino FaceLock")
    parser.add_argument("--port", default="/dev/ttyUSB0", help="Serial port connected to Arduino")
    parser.add_argument("--baud", type=int, default=9600, help="Serial baud rate")
    parser.add_argument("--timeout", type=int, default=10, help="Face recognition timeout (s)")
    args = parser.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Failed to open serial port {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    face_handle = FaceManager()

    print(f"Listening on {args.port} @ {args.baud} baud. Waiting for Arduino events...")

    try:
        while True:
            try:
                if ser.in_waiting > 0:
                    line = ser.readline().decode("utf-8", errors="ignore").strip()
                    if not line:
                        continue

                    print(f"Arduino: {line}")

                    if line == "RECOGNITION_MODE":
                        print("running face recognition...")
                        recognized, info = do_recognition(face_handle)
                        if recognized:
                            print(f"Face recognized: {info}")
                            ser.write(b"FACE_RECOGNIZED\n")
                        else:
                            print(f"Face not recognized: {info}")
                            ser.write(b"FACE_UNRECOGNIZED\n")
                    elif line == "ADD_FACE":
                        print("enrolling face...")
                        enroll = enroll_face(face_handle)
                        print(enroll)
                        if enroll:
                            print("successfull enrollment")
                            ser.write(b"FACE_ENROLLED\n")
                        else:
                            print("failed enrollment")
                            ser.write(b"FAILED_ENROLLMENT\n")

            except serial.SerialException:
                # transient serial error; report and continue
                print("Serial connection error; retrying...", file=sys.stderr)
                time.sleep(1)
    except KeyboardInterrupt:
        print("Stopped by user.")
    finally:
        try:
            ser.close()
        except Exception:
            pass


if __name__ == "__main__":
    main()
