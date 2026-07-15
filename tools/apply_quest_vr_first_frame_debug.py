#!/usr/bin/env python3
"""Apply a CI-only Quest OpenXR first-frame diagnostic patch.

This deliberately removes controller/action-set setup and emulator-texture sampling,
then clears the left eye red and the right eye blue. It also adds loud logs around
the first 120 frame attempts. The production quest-vr-prototype branch remains
untouched.
"""

from pathlib import Path

SOURCE = Path("quest-vr/src/main/cpp/QuestVrBridge.cpp")