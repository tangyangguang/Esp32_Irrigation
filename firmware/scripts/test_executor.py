#!/usr/bin/env python3
"""Focused hosted concurrency check of production execution ownership."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="irrigation-executor-") as directory:
    binary = str(Path(directory) / "test")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-pthread", "-Wall", "-Wextra",
        "-I", str(root / "test/host_executor/stubs"), "-I", str(root / "src/irrigation"),
        str(root / "test/host_executor/main.cpp"),
        *(str(root / "src/irrigation" / name) for name in
          ("WateringExecutor.cpp", "WateringController.cpp", "FlowMonitor.cpp", "IrrigationConfig.cpp")),
        "-o", binary], check=True)
    subprocess.run([binary], check=True, timeout=10)
