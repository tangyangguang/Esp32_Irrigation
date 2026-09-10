Import("env")

import subprocess
import sys
from pathlib import Path

project_dir = Path(env.subst("$PROJECT_DIR"))
command = [sys.executable, str(project_dir / "scripts" / "generate_web_assets.py"), "--check"]
result = subprocess.run(command, cwd=project_dir)
if result.returncode != 0:
    print("Run: python3 scripts/generate_web_assets.py")
    env.Exit(result.returncode)

workspace = project_dir.parents[2]
command = [sys.executable, str(workspace / "platform/iot-device/sdk/scripts/generate_model.py"),
           str(workspace / "platform/iot-device/contracts/device-types/irrigation-controller/definition.json"),
           "--model", "irrigation-controller-6-zone", "--output",
           str(project_dir / "src/irrigation/generated/IrrigationModel.h"), "--check"]
result = subprocess.run(command, cwd=project_dir)
if result.returncode != 0:
    print("Regenerate IrrigationModel.h using the SDK generator and current lab definition")
    env.Exit(result.returncode)
