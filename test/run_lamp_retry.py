"""Compile and execute the real controller queue methods with simulated HTTP/time.

Usage: python test\\run_lamp_retry.py <compiler> [compiler arguments...]
Examples: ... g++  /  ... C:\\tools\\zig.exe c++
"""
import pathlib
import subprocess
import sys
import tempfile


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    root = pathlib.Path(__file__).resolve().parents[1]
    header = (root / "src" / "lighting_controller.h").read_text()
    source = (root / "src" / "lighting_controller.cpp").read_text()
    structs = "\n".join(block(header, "struct " + name) + ";" for name in ("LampState", "LampCommand"))
    methods = "\n".join(block(source, signature) for signature in (
        "bool LightingController::isBeforeMillis(",
        "void LightingController::resetLampCommands(",
        "void LightingController::queueLampCommand(",
        "void LightingController::processLampCommand(",
    ))
    harness = """
class LightingController {
public:
  AppConfig &cfg;
  explicit LightingController(AppConfig &config) : cfg(config) {}
  STRUCTS
  std::mutex lampMutex;
  std::vector<LampState> lampStates;
  std::deque<LampCommand> lampCommands;
  uint32_t lampGeneration = 0;
  bool lampCommandInFlight = false;
  bool hasShelly102Command = false;
  unsigned long lastShelly102CommandMs = 0;
  bool isBeforeMillis(unsigned long now, unsigned long target) const;
  void resetLampCommands();
  void queueLampCommand(LampState &, bool, bool = false);
  void processLampCommand();
};
METHODS
""".replace("STRUCTS", structs).replace("METHODS", methods)
    with tempfile.TemporaryDirectory(prefix="lamp-retry-") as directory:
        temporary = pathlib.Path(directory)
        (temporary / "controller_test.inc").write_text(harness)
        binary = temporary / "lamp_retry.exe"
        subprocess.run(sys.argv[1:] + ["-std=c++11", "-Wall", "-Wextra", "-Werror",
                       "-I", str(temporary), str(root / "test" / "lamp_retry.test.cpp"),
                       "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
