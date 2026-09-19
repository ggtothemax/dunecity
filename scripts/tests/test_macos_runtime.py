import importlib.util
import io
from contextlib import redirect_stdout
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("macos_runtime", Path(__file__).parents[1] / "verify-macos-runtime.py")
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class MacRuntimePackagingTests(unittest.TestCase):
    def test_successful_static_sdl_needs_no_dylibs(self):
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.CompletedProcess([], 0, "Desktop runtime initialization and rendering passed\n", "")
            with patch.object(runtime.subprocess, "run", return_value=result), redirect_stdout(io.StringIO()):
                runtime.verify(Path(folder) / "dunecity.app")

    def test_external_sdl_is_rejected_despite_successful_render(self):
        with tempfile.TemporaryDirectory() as folder:
            result = subprocess.CompletedProcess([], 0,
                "SDL runtime library: /opt/homebrew/lib/libSDL2.dylib\nDesktop runtime initialization and rendering passed\n", "")
            with patch.object(runtime.subprocess, "run", return_value=result):
                with self.assertRaisesRegex(RuntimeError, "outside the app"):
                    runtime.verify(Path(folder) / "dunecity.app")

    def test_compat_without_sdl3_fails_before_launch(self):
        with tempfile.TemporaryDirectory() as folder:
            app = Path(folder) / "dunecity.app"
            libraries = app / "Contents/Frameworks"
            libraries.mkdir(parents=True)
            (libraries / "libSDL2.dylib").write_bytes(b"SDL2COMPAT_DEBUG_LOGGING")
            with patch.object(runtime.subprocess, "run") as launch:
                with self.assertRaisesRegex(RuntimeError, "missing its runtime dependency"):
                    runtime.verify(app)
                launch.assert_not_called()


if __name__ == "__main__":
    unittest.main()
