#!/usr/bin/env python3
"""Run irrigation store readers against the actual Base and its host FS fixture."""
from pathlib import Path
import os
import subprocess
import tempfile

firmware = Path(__file__).resolve().parents[1]
root = firmware.parents[2]
base = root / 'foundation/Esp32Base'
sdk = root / 'platform/iot-device/sdk/src'
arduino_json = firmware / '.pio/libdeps/native/ArduinoJson/src'
unity = base / '.pio/libdeps/arduino2/native_record_store_harness/Unity/src'
harness = base / 'test/test_native_record_store_harness'
with tempfile.TemporaryDirectory(prefix='irrigation-store-') as directory:
    work = Path(directory)
    subprocess.run([os.environ.get('CC', 'cc'), '-c', str(unity / 'unity.c'), '-o', str(work / 'unity.o')], check=True)
    command = [os.environ.get('CXX', 'c++'), '-std=c++17', '-ftrivial-auto-var-init=pattern',
        '-DESP32BASE_PROFILE=ESP32BASE_PROFILE_MINIMAL', '-DESP32BASE_LOG_LEVEL=ESP32BASE_LOG_NONE',
        '-DESP32BASE_EB_FILELOG_DEFAULT_MODE=0', '-DESP32BASE_ENABLE_FS=1', '-DESP32BASE_ENABLE_FILELOG=0', '-DESP32BASE_ENABLE_RECORD_STORE=1',
        '-DESP32BASE_ENABLE_CONDITIONS=1', '-DESP32BASE_ENABLE_TIME=1',
        '-DIRRIGATION_BASE_HARNESS="' + str(harness / 'test_main.cpp') + '"']
    for include in [firmware / 'test/host_storage/stubs', firmware / 'src/irrigation', sdk, arduino_json, base / 'src', harness / 'stubs', unity]:
        command += ['-I', str(include)]
    command += [str(firmware / 'test/host_storage/main.cpp')]
    command += [str(firmware / 'src/irrigation' / source) for source in ['WateringRecordStore.cpp', 'WateringRecordCodec.cpp', 'IrrigationAuditStore.cpp', 'IrrigationAuditPayload.cpp', 'IrrigationRecords.cpp', 'IrrigationEvents.cpp', 'WateringHistory.cpp']]
    command += [str(sdk / 'RecordStream.cpp')]
    command += [str(work / 'unity.o'), '-o', str(work / 'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(work / 'test')], check=True)
