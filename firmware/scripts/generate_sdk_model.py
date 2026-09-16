"""Generate the irrigation device SDK model contract at build time.

The authoritative definition lives in platform/iot-device; the generated header
is written into the build directory, so a contract change always rebuilds
against the current definition instead of a stale checked-in copy.
"""
Import("env")
from pathlib import Path
import subprocess
import sys

project = Path(env.subst("$PROJECT_DIR")).resolve()
workspace = project.parents[2]
sdk = workspace / "platform/iot-device/sdk"
definition = (
    workspace
    / "platform/iot-device/contracts/device-types/irrigation-controller/definition.json"
)
generated = Path(env.subst("$BUILD_DIR")) / "generated"
generated.mkdir(parents=True, exist_ok=True)
output = generated / "IrrigationSdkModel.generated.h"
subprocess.run(
    [
        sys.executable,
        str(sdk / "scripts/generate_model.py"),
        str(definition),
        "--model",
        "irrigation-controller-6-zone",
        "--output",
        str(output),
    ],
    check=True,
)
env.AppendUnique(CPPPATH=[str(generated)])
