"""Run real configuration serialization, NVS backup and LittleFS restore code with fake storage.

Usage: python test\\run_config_backup.py <compiler> [compiler arguments...]
Requires Windows C++11 and the existing PlatformIO ArduinoJson dependency.
"""
import pathlib
import re
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    root = pathlib.Path(__file__).resolve().parents[1]
    header = (root / "src" / "app_config.h").read_text()
    source = (root / "src" / "app_config.cpp").read_text()
    code = re.sub(r"^#(?:include|pragma).*$", "", header + "\n" + source, flags=re.MULTILINE)
    with tempfile.TemporaryDirectory(prefix="config-backup-") as directory:
        temporary = pathlib.Path(directory)
        (temporary / "config_test.inc").write_text(code)
        binary = temporary / "config_backup.exe"
        subprocess.run(sys.argv[1:] + ["-std=c++11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(temporary), "-I", str(root / ".pio" / "libdeps" / "esp32dev" / "ArduinoJson" / "src"),
                       str(root / "test" / "config_backup.test.cpp"), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
