#!/usr/bin/env python3
"""Launch the packaged SDL runtime without touching a game or user profile."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def verify(app):
    app = app.resolve()
    frameworks = app / "Contents/Frameworks"
    compat = any(b"SDL2COMPAT_DEBUG_LOGGING" in p.read_bytes()
                 for p in frameworks.glob("libSDL2*.dylib"))
    if compat and not (frameworks / "libSDL3.dylib").is_file():
        raise RuntimeError("Packaged sdl2-compat is missing its runtime dependency libSDL3.dylib")
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("DYLD_", "SDL_", "SDL2COMPAT_", "SDL3_"))}
    env["PATH"] = "/usr/bin:/bin:/usr/sbin:/sbin"
    with tempfile.TemporaryDirectory(prefix="dunecity-runtime-") as work:
        result = subprocess.run([str(app / "Contents/MacOS/dunecity"), "--check-desktop-runtime"],
                                cwd=work, env=env, capture_output=True, text=True, timeout=30)
    if result.returncode or "Desktop runtime initialization and rendering passed" not in result.stdout:
        raise RuntimeError("Packaged runtime failed: " + result.stdout + result.stderr)
    libraries = [Path(line.split(": ", 1)[1]).resolve() for line in result.stdout.splitlines()
                 if line.startswith("SDL runtime library: ")]
    # vcpkg embeds SDL statically. A successful initialization/render check is
    # valid with no SDL dylibs; any dynamically loaded SDL must still be bundled.
    if any(app not in p.parents for p in libraries):
        raise RuntimeError("SDL runtime loaded libraries outside the app: " + repr(libraries))
    if compat and not any(p.name == "libSDL3.dylib" for p in libraries):
        raise RuntimeError("The bundled SDL3 runtime was not loaded")
    print(result.stdout, end="")
    print("Verified packaged SDL libraries, initialization and hidden-window rendering.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    verify(parser.parse_args().app)
