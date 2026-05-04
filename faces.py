import json
from pathlib import Path
import cv2
import face_recognition


class FaceManager:
    """
    Modular face recognition system for enrollment and recognition.
    """

    def __init__(self, storage_path: str = "faces.json"):
        self.storage_path = Path(storage_path)
        self.faces = self._load_faces()

    def _load_faces(self) -> dict:
        """Load face encodings from storage."""
        if self.storage_path.exists():
            with open(self.storage_path, "r") as f:
                return json.load(f)
        return {}

    def _save_faces(self):
        """Save face encodings to storage."""
        with open(self.storage_path, "w") as f:
            json.dump(self.faces, f, indent=2)

    def enroll(self, name: str, encoding) -> bool:
        """
        Enroll a face with the given name and encoding.
        Returns True if successful, False if name already exists.
        """
        if name in self.faces:
            return False
        self.faces[name] = encoding.tolist()
        self._save_faces()
        return True

    def recognize(self, encoding) -> str:
        """
        Compare encoding against enrolled faces.
        Returns the matched name or 'Unknown'.
        Uses closest match if multiple matches found.
        """
        known_encodings = [
            (name, self._ensure_list(face_data))
            for name, face_data in self.faces.items()
        ]

        if not known_encodings:
            return "Unknown"

        distances = face_recognition.face_distance(
            [enc for _, enc in known_encodings], encoding
        )

        # Find closest match with tolerance
        min_distance = min(distances) if distances.size > 0 else float('inf')
        
        if min_distance < 0.6:
            closest_idx = distances.argmin()
            return known_encodings[closest_idx][0]

        return "Unknown"

    def remove(self, name: str) -> bool:
        """
        Remove a face by name.
        Returns True if successful, False if not found.
        """
        if name in self.faces:
            del self.faces[name]
            self._save_faces()
            return True
        return False

    def list(self) -> list:
        """Returns list of enrolled face names."""
        return list(self.faces.keys())

    def get_encoding(self, name: str):
        """Returns the encoding for a given face name, or None if not found."""
        if name in self.faces:
            return self.faces[name]
        return None

    def capture_face_from_webcam(self):
        """
        Capture the closest face from webcam and return its encoding.
        Returns the encoding or None if no face detected.
        """
        cap = cv2.VideoCapture(0)

        if not cap.isOpened():
            return None

        encoding = None
        while True:
            ret, frame = cap.read()
            if not ret:
                break

            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            locations = face_recognition.face_locations(rgb)
            encodings = face_recognition.face_encodings(rgb, locations)

            if encodings:
                # Find the closest face (largest bounding box)
                closest_idx = self._get_closest_face_index(locations)
                encoding = encodings[closest_idx]
                break

        cap.release()
        return encoding

    def _get_closest_face_index(self, locations):
        """
        Find index of closest face based on bounding box size.
        Larger bounding box = closer face.
        """
        if not locations:
            return 0

        sizes = []
        for top, right, bottom, left in locations:
            size = (right - left) * (bottom - top)
            sizes.append(size)

        return sizes.index(max(sizes))

    def _ensure_list(self, data):
        """Ensure data is a list."""
        if isinstance(data, list):
            return data
        return list(data)
