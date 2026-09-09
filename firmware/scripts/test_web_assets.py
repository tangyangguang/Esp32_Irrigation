#!/usr/bin/env python3
"""Check the actual generated C++ assets reconstruct every original Web fragment."""
import gzip
import re
import subprocess
import tempfile
from pathlib import Path

from generate_web_assets import ASSETS

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="irrigation-assets-") as directory:
    work = Path(directory)
    (work / "Esp32Base.h").write_text(r'''
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstddef>
#include <fstream>
#include <string>
#include <vector>
struct Esp32BaseWeb {
    inline static std::vector<std::string> paths;
    static bool addStaticAsset(const char* path, const char* type, const uint8_t* data,
                              size_t size, uint32_t cache, bool auth, bool gzip) {
        if (!auth || !gzip || cache != 86400 || std::string(type).empty()) return false;
        const std::string name = std::string(path).substr(std::string(path).find_last_of('/') + 1);
        paths.emplace_back(path);
        std::ofstream file(name + ".gz", std::ios::binary);
        file.write(reinterpret_cast<const char*>(data), size);
        return file.good();
    }
    static void sendChunk(const char* text) { std::fputs(text, stdout); }
};
''')
    (work / "main.cpp").write_text('''#include "IrrigationWebAssets.h"
#include <cstdlib>
int main(int argc, char** argv) {
    if (!IrrigationWebAssets::registerAssets()) return 2;
    return IrrigationWebAssets::send(static_cast<IrrigationWebAssets::Asset>(std::atoi(argv[1]))) ? 0 : 3;
}
''')
    generated = ROOT / "src/irrigation/generated"
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Wno-unused-parameter",
                    "-I", str(work), "-I", str(generated), str(work / "main.cpp"),
                    str(generated / "IrrigationWebAssets.cpp"), "-o", str(work / "check")], check=True)
    for index, (_, filename) in enumerate(ASSETS):
        emitted = subprocess.check_output([str(work / "check"), str(index)], cwd=work).decode()
        def restore(match):
            tag = "script" if match.group(1) else "style"
            url = match.group(1) or match.group(2)
            path = url.split("?", 1)[0].rsplit("/", 1)[1]
            body = gzip.decompress((work / (path + ".gz")).read_bytes()).decode()
            return f"<{tag}>{body}</{tag}>"
        reconstructed = re.sub(r'<script src="([^"]+)"></script>|<link rel="stylesheet" href="([^"]+)">', restore, emitted)
        assert reconstructed == (ROOT / "web-src" / filename).read_text(), filename
    assert subprocess.run([str(work / "check"), "255"], cwd=work).returncode == 3
    assert len(list(work.glob("*.gz"))) == 10
print("PASS: all 10 generated fragments preserve exact source content; gzip, registration and invalid asset checked")
