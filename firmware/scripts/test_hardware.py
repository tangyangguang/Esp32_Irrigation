#!/usr/bin/env python3
"""Exercise production BoardHardware with fake GPIO/LEDC; never access hardware."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='irrigation-hardware-') as directory:
    binary = str(Path(directory) / 'test')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
        '-I', str(root / 'test/host_hardware/stubs'), '-I', str(root / 'src/irrigation'),
        str(root / 'test/host_hardware/main.cpp'), str(root / 'src/irrigation/BoardHardware.cpp'), str(root / 'src/irrigation/StatusIndicator.cpp'),
        '-o', binary], check=True)
    subprocess.run([binary], check=True)
