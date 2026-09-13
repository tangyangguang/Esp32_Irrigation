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
