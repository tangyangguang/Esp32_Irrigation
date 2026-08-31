#!/usr/bin/env python3
"""Build the production IOT path with non-secret, realistically sized TLS data."""

from pathlib import Path
import subprocess
import sys


PROJECT_DIR = Path(__file__).resolve().parents[1]
PRIVATE_DIR = PROJECT_DIR / "local_private"
PRIVATE_HEADER = PRIVATE_DIR / "irrigation_iot_private.h"
PIO_WRAPPER = PROJECT_DIR.parent.parent / "Esp32Base" / "scripts" / "pio_arduino.py"
CA_BODY_BYTES = 2048


def _fixture_header() -> str:
    body = "A" * CA_BODY_BYTES
    lines = [body[index : index + 64] for index in range(0, len(body), 64)]
    pem = ['    "-----BEGIN CERTIFICATE-----\\n" \\']
    pem.extend(f'    "{line}\\n" \\' for line in lines)
    pem.append('    "-----END CERTIFICATE-----\\n"')
    return "\n".join(
        [
            "#pragma once",
            '#define IRRIGATION_IOT_MQTT_HOST "mqtt.release-fixture.invalid"',
            "#define IRRIGATION_IOT_MQTT_PORT 8883",
            '#define IRRIGATION_IOT_MQTT_USERNAME "release-fixture-user"',
            '#define IRRIGATION_IOT_MQTT_PASSWORD "release-fixture-password"',
            "#define IRRIGATION_IOT_MQTT_CA_PEM \\",
            *pem,
            "",
        ]
    )


def main() -> int:
    if PRIVATE_HEADER.exists():
        print(
            "ERROR: refusing to replace existing local_private/"
            "irrigation_iot_private.h",
            file=sys.stderr,
        )
        return 2
    PRIVATE_DIR.mkdir(parents=True, exist_ok=True)
    PRIVATE_HEADER.write_text(_fixture_header(), encoding="utf-8")
    try:
        clean = subprocess.call(
            [
                sys.executable,
                str(PIO_WRAPPER),
                "2",
                "run",
                "-e",
                "esp32_irrigation",
                "--target",
                "clean",
            ],
            cwd=PROJECT_DIR,
        )
        if clean != 0:
            return clean
        return subprocess.call(
            [sys.executable, str(PIO_WRAPPER), "2", "run", "-e", "esp32_irrigation"],
            cwd=PROJECT_DIR,
        )
    finally:
        PRIVATE_HEADER.unlink(missing_ok=True)
        try:
            PRIVATE_DIR.rmdir()
        except OSError:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
