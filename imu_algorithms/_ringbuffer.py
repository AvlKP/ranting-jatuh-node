"""Power-of-2 circular buffer for storing calibrated gyro magnitude.

Stores float32 values with cheap bitwise-modulo indexing.
2048 samples covers 39.4 seconds at 52 Hz ODR -- longer than
any known event (max ~28.5s).
"""

import numpy as np


class RingBuffer:
    """Power-of-2 circular buffer for calibrated gyro magnitude."""

    def __init__(self, size: int = 2048):
        if size & (size - 1) != 0:
            raise ValueError(f"RingBuffer size must be power of 2, got {size}")
        self.SIZE = size
        self.MASK = size - 1
        self._buf = np.zeros(size, dtype=np.float32)
        self.write_ptr: int = 0

    def write(self, value: float) -> None:
        """Write one sample and advance write pointer."""
        self._buf[self.write_ptr & self.MASK] = value
        self.write_ptr += 1

    def read_segment(self, start_idx: int, length: int) -> np.ndarray:
        """Copy a contiguous segment from the ring buffer.

        Handles wrap-around by copying in two parts if the segment
        crosses the buffer end.
        """
        if length <= 0:
            return np.array([], dtype=np.float32)

        start_mod = start_idx & self.MASK
        end_mod = (start_idx + length - 1) & self.MASK

        if start_mod <= end_mod:
            return self._buf[start_mod:end_mod + 1].copy()
        else:
            part1 = self._buf[start_mod:]
            part2 = self._buf[:end_mod + 1]
            return np.concatenate([part1, part2])

    def onset_age(self, onset_idx: int) -> int:
        """Samples between onset and current write pointer."""
        return self.write_ptr - onset_idx

    def is_truncated(self, onset_age: int) -> bool:
        """Check if onset predates the oldest buffered sample."""
        return onset_age > self.SIZE
