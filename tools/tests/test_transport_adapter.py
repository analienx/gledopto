from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware" / "wireless-dump-stager"


class TransportAdapterNativeTests(unittest.TestCase):
    def test_native_transport_adapter(self):
        gcc = shutil.which("gcc")
        if not gcc:
            self.skipTest("gcc not available")
        with tempfile.TemporaryDirectory() as td:
            exe = Path(td) / "transport_adapter_test"
            subprocess.run(
                [
                    gcc,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(FW),
                    str(FW / "glsd_stager_core.c"),
                    str(FW / "glsd_stager_dispatch.c"),
                    str(FW / "glsd_transport_adapter.c"),
                    str(FW / "tests" / "transport_adapter_test.c"),
                    "-o",
                    str(exe),
                ],
                check=True,
                capture_output=True,
                text=True,
            )
            cp = subprocess.run(
                [str(exe)], check=True, capture_output=True, text=True
            )
            self.assertIn("transport_adapter_test: PASS", cp.stdout)


if __name__ == "__main__":
    unittest.main()
