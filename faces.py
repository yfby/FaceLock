"""Face management helpers.

This module provides a small FaceManager class responsible for storing
face encodings on disk, capturing an encoding from a webcam, and
matching an encoding against the enrolled faces.

The implementation stores encodings as plain Python lists inside a
JSON file but converts them to numpy arrays for recognition operations
to match the expectations of the underlying face_recognition library.
"""

import json
from typing import Optional, Dict, Any, List
from pathlib import Path

import numpy as np
import cv2
import face_recognition


class FaceManager:
    """Manage enrollment and recognition of faces.

    Args:
        storage_path: Path to JSON file used to persist enrolled faces.
    """

    def __init__(self, storage_path: str = "faces.json"):
        self.storage_path = Path(storage_path)
        # In-memory mapping: name -> list(serializable) encoding
        self.faces: Dict[str, List[float]] = self._load_faces()

    def _load_faces(self) -> Dict[str, Any]:
        """Load face encodings from storage.

        If the file does not exist or is invalid, returns an empty dict.
        """
        if not self.storage_path.exists():
            return {}

        try:
            with open(self.storage_path, "r") as f:
                data = json.load(f)
                if isinstance(data, dict):
                    return data
        except (json.JSONDecodeError, OSError):
            # Corrupt file or IO error => start fresh
            return {}

        return {}

    def _save_faces(self) -> None:
        """Persist the in-memory faces mapping to disk."""
        # Ensure parent directory exists
        try:
            self.storage_path.parent.mkdir(parents=True, exist_ok=True)
            with open(self.storage_path, "w") as f:
                json.dump(self.faces, f, indent=2)
        except OSError:
            # If saving fails, there's not much we can do here; caller
            # should handle persistence failures if needed.
            pass

    def enroll(self, name: str, encoding) -> bool:
        """Enroll a face under ``name``.

        ``encoding`` may be a numpy.ndarray or a list-like object. The
        stored format is a plain list so it can be written to JSON.

        Returns True on success, False if the name already exists.
        """
        if name in self.faces:
            return False

        arr = np.asarray(encoding)
        # ensure 1D numeric list
        self.faces[name] = arr.flatten().tolist()
        self._save_faces()
        return True

    def recognize(self, encoding) -> str:
        """Compare ``encoding`` against enrolled faces.

        Returns the matched name or the string "Unknown". A simple
        distance threshold (0.6) is used; this mirrors the default
        used in many face-recognition examples.
        """
        if not self.faces:
            return "Unknown"

        # Convert stored encodings back to numpy arrays
        known_names = []
        known_arrays = []
        for name, data in self.faces.items():
            try:
                arr = np.asarray(data, dtype=float)
            except Exception:
                continue
            known_names.append(name)
            known_arrays.append(arr)

        if not known_arrays:
            return "Unknown"

        target = np.asarray(encoding)
        # face_recognition.face_distance expects a 2D array for known
        distances = face_recognition.face_distance(known_arrays, target)

        # Choose the closest match and apply a threshold
        if distances.size == 0:
            return "Unknown"

        closest_idx = int(np.argmin(distances))
        min_distance = float(distances[closest_idx])
        if min_distance < 0.6:
            return known_names[closest_idx]

        return "Unknown"

    def remove(self, name: str) -> bool:
        """Remove an enrolled face by name.

        Returns True if removed, False if the name was not found.
        """
        if name in self.faces:
            del self.faces[name]
            self._save_faces()
            return True
        return False

    def list(self) -> list:
        """Return enrolled face names."""
        return list(self.faces.keys())

    def get_encoding(self, name: str) -> Optional[np.ndarray]:
        """Return the stored encoding as a numpy array, or None if missing."""
        if name in self.faces:
            return np.asarray(self.faces[name], dtype=float)
        return None

    def capture_face_from_webcam(self, max_frames: int = 5) -> Optional[np.ndarray]:
        """Check the webcam for faces and return the first encoding found.

        This function reads at most ``max_frames`` frames from the default
        camera and returns the encoding for the closest detected face. If no
        face is found in the inspected frames, or the camera cannot be
        opened, ``None`` is returned. Bounding box area is used to pick the
        closest face when multiple are present.
        """
        cap = cv2.VideoCapture(0)

        if not cap.isOpened():
            return None

        encoding = None
        try:
            frames_checked = 0
            while frames_checked < int(max_frames):
                ret, frame = cap.read()
                if not ret:
                    break

                frames_checked += 1
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                locations = face_recognition.face_locations(rgb)
                encodings = face_recognition.face_encodings(rgb, locations)

                if encodings:
                    closest_idx = self._get_closest_face_index(locations)
                    encoding = encodings[closest_idx]
                    break
        finally:
            cap.release()

        return encoding

    def _get_closest_face_index(self, locations):
        """Return index of the face with the largest bounding-box area.

        A larger bounding box is assumed to indicate a closer subject.
        """
        if not locations:
            return 0

        sizes = []
        for top, right, bottom, left in locations:
            size = (right - left) * (bottom - top)
            sizes.append(size)

        # index of maximum size
        return int(np.argmax(sizes))
