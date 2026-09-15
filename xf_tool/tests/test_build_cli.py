"""通过独立进程验证命令接口，模拟 SDK 与串口后端，不操作真实设备。"""

import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build as wrapper


SCRIPT = Path(__file__).resolve().parents[1] / "build.py"
FAKE_BACKEND = """import json, os, sys
from pathlib import Path
entry = {
    'kind': KIND,
    'args': sys.argv[1:],
    'cwd': os.getcwd(),
    'target': os.environ.get('IDF_TARGET'),
    'sdk': os.environ.get('IDF_PATH'),
}
with Path(os.environ['XF_TEST_CALLS']).open('a') as output:
    output.write(json.dumps(entry) + '\\n')
if KIND == 'report':
    print('Partitions Table / APP Memory Info', flush=True)
    sys.exit(int(os.environ.get('XF_TEST_REPORT_EXIT', '0')))
if KIND == 'sync':
    sys.exit(int(os.environ.get('XF_TEST_SYNC_EXIT', '0')))
if KIND == 'format':
    sys.exit(int(os.environ.get('XF_TEST_FORMAT_EXIT', '0')))
if 'flash' in sys.argv:
    sys.exit(int(os.environ.get('XF_TEST_FLASH_EXIT', '0')))
sys.exit(int(os.environ.get('XF_TEST_BACKEND_EXIT', '0')))
"""


class BuildCliTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="xf-build-cli-")
        self.addCleanup(self.temp.cleanup)
        # 路径带空格，用于检查参数传递和 SDK 环境初始化。
        self.root = Path(self.temp.name) / "workspace with spaces"
        self.tool = self.root / "xf_tool"
        self.tool.mkdir(parents=True)
        shutil.copyfile(SCRIPT, self.tool / "build.py")
        (self.tool / "build_report.py").write_text("KIND = 'report'\n" + FAKE_BACKEND)
        (self.tool / "config_sync.py").write_text("KIND = 'sync'\n" + FAKE_BACKEND)
        (self.tool / "format_backend.py").write_text("KIND = 'format'\n" + FAKE_BACKEND)
        (self.tool / "format_code_linux.sh").write_text(
            f'exec {shlex.quote(sys.executable)} {shlex.quote(str(self.tool / "format_backend.py"))} "$@"\n'
        )
        self.sdk = self.root / "esp-idf"
        (self.sdk / "tools").mkdir(parents=True)
        (self.sdk / "tools" / "idf.py").write_text("KIND = 'idf'\n" + FAKE_BACKEND)
        runtime = self.root / "runtime"
        (runtime / "bin").mkdir(parents=True)
        (runtime / "bin" / "python").symlink_to(sys.executable)
        (self.sdk / "export.sh").write_text(
            f"export IDF_PATH={shlex.quote(str(self.sdk))}\n"
            f"export IDF_PYTHON_ENV_PATH={shlex.quote(str(runtime))}\n"
        )
        modules = self.root / "fake_modules"
        serial_tools = modules / "serial" / "tools"
        serial_tools.mkdir(parents=True)
        (modules / "serial" / "__init__.py").touch()
        (serial_tools / "__init__.py").touch()
        (serial_tools / "miniterm.py").write_text("KIND = 'serial'\n" + FAKE_BACKEND)
        self.calls_file = self.root / "calls.jsonl"
        self.env = os.environ.copy()
        self.env.update({
            "PYTHONPATH": str(modules),
            "XF_TEST_CALLS": str(self.calls_file),
            "IDF_PATH": "/a/different/sdk",
            "IDF_TARGET": "esp32c3",
        })
        self.add_project("xc200", "esp32s3")

    def add_project(self, model, target):
        project = self.root / "project" / model
        (project / "inc").mkdir(parents=True)
        (project / "CMakeLists.txt").write_text("idf_component_register()\n")
        (project / "inc" / "xf_project_config.h").write_text(
            f'#define XF_USE_CHIP_ID "{target}"\n'
        )
        (project / "sdkconfig.conf").write_text("CONFIG_FREERTOS_HZ=100\n")
        (self.tool / "config").mkdir(exist_ok=True)
        (self.tool / "config" / f"{target}.conf").write_text("# 芯片公共配置\n")

    def invoke(self, *args, env=None, cwd=None):
        return subprocess.run(
            [sys.executable, str(self.tool / "build.py"), *args],
            cwd=cwd or self.root,
            env=env or self.env,
            capture_output=True,
            text=True,
            timeout=15,
        )

    def calls(self):
        if not self.calls_file.exists():
            return []
        return [json.loads(line) for line in self.calls_file.read_text().splitlines()]

    def test_documented_model_commands(self):
        cases = [
            (("MODEL=xc200",), ["build"], None),
            (("MODEL=xc200", "PORT=/dev/ttyUSB0"), ["build", "flash"], "/dev/ttyUSB0"),
            (("MODEL=xc200", "LOG=/dev/ttyUSB0"), ["build", "flash", "monitor"], "/dev/ttyUSB0"),
            (("MODEL=xc200", "menuconfig"), ["menuconfig"], None),
        ]
        for cli, actions, port in cases:
            with self.subTest(cli=cli):
                start = len(self.calls())
                result = self.invoke(*cli)
                self.assertEqual(result.returncode, 0, result.stderr)
                calls = self.calls()[start:]
                idf_calls = [call for call in calls if call["kind"] == "idf"]
                action_names = {"build", "flash", "monitor", "menuconfig"}
                self.assertEqual([arg for call in idf_calls for arg in call["args"] if arg in action_names], actions)
                expected_kinds = ["idf", "sync"] if actions == ["menuconfig"] else ["format", "idf", "report"] + (["idf"] if port else [])
                self.assertEqual([call["kind"] for call in calls], expected_kinds)
                for call in idf_calls:
                    self.assertIn("MODEL=xc200", call["args"])
                    self.assertIn("IDF_TARGET=esp32s3", call["args"])
                    self.assertEqual(call["args"][call["args"].index("-B") + 1], str(self.tool / "build/xc200/esp32s3"))
                    self.assertEqual(call["target"], "esp32s3")
                    self.assertEqual(call["sdk"], str(self.sdk))
                    self.assertEqual(call["cwd"], str(self.tool))
                    if port:
                        self.assertEqual(call["args"][call["args"].index("-p") + 1], port)
                    else:
                        self.assertNotIn("-p", call["args"])
                if actions != ["menuconfig"]:
                    self.assertEqual(calls[0]["args"], ["xc200"])
                    self.assertEqual(calls[2]["args"], [str(self.tool / "build/xc200/esp32s3")])
                    self.assertIn("Partitions Table", result.stdout)
                else:
                    self.assertIn(str(self.root / "project/xc200/sdkconfig.conf"), calls[1]["args"])
                    self.assertIn(str(self.tool / "config/esp32s3.conf"), calls[1]["args"])

    def test_menu_failure_does_not_write_back(self):
        result = self.invoke("MODEL=xc200", "menuconfig", env=dict(self.env, XF_TEST_BACKEND_EXIT="130"))
        self.assertEqual(result.returncode, 130)
        self.assertEqual([call["kind"] for call in self.calls()], ["idf"])

    def test_common_menu_selects_only_chip_without_loading_a_product(self):
        shutil.rmtree(self.root / "project")
        result = self.invoke("menuconfig")
        self.assertEqual(result.returncode, 0, result.stderr)
        menu, sync = self.calls()
        self.assertEqual([menu["kind"], sync["kind"]], ["idf", "sync"])
        self.assertIn("XF_COMMON_CONFIG_ONLY=ON", menu["args"])
        self.assertIn(str(self.tool / "build/.common/esp32s3"), menu["args"])
        self.assertFalse(any(arg.startswith("MODEL=") for arg in menu["args"]))
        self.assertNotIn("--private", sync["args"])
        self.assertIn(str(self.tool / "config/esp32s3.conf"), sync["args"])
        self.assertEqual(menu["target"], "esp32s3")

    def test_common_menu_with_multiple_chips_requires_a_choice(self):
        (self.tool / "config/esp32c3.conf").write_text("# C3\n")
        result = self.invoke("menuconfig")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CHIP=", result.stderr)
        self.assertFalse(self.calls())
        result = self.invoke("CHIP=esp32c3", "menuconfig")
        self.assertEqual(result.returncode, 0, result.stderr)
        menu, sync = self.calls()
        self.assertEqual(menu["target"], "esp32c3")
        self.assertIn(str(self.tool / "config/esp32c3.conf"), sync["args"])

    def test_interactive_chip_selection_retries_and_accepts_a_number(self):
        (self.tool / "config/esp32c3.conf").touch()
        with patch.object(wrapper, "TOOL_DIR", self.tool), patch("sys.stdin.isatty", return_value=True), \
                patch("builtins.input", side_effect=["invalid", "2"]):
            self.assertEqual(wrapper.common_chip(""), "esp32s3")

    def test_common_menu_failure_does_not_write_back(self):
        result = self.invoke("menuconfig", env=dict(self.env, XF_TEST_BACKEND_EXIT="130"))
        self.assertEqual(result.returncode, 130)
        self.assertEqual([call["kind"] for call in self.calls()], ["idf"])

    def test_sync_failure_is_reported_to_caller(self):
        result = self.invoke("MODEL=xc200", "menuconfig", env=dict(self.env, XF_TEST_SYNC_EXIT="1"))
        self.assertEqual(result.returncode, 1)
        self.assertEqual([call["kind"] for call in self.calls()], ["idf", "sync"])

    def test_log_only_needs_no_project_or_build(self):
        shutil.rmtree(self.root / "project")
        shutil.rmtree(self.tool / "config")
        result = self.invoke("LOG=/dev/ttyUSB0")
        self.assertEqual(result.returncode, 0, result.stderr)
        call, = self.calls()
        self.assertEqual(call["kind"], "serial")
        self.assertEqual(call["args"][-2:], ["/dev/ttyUSB0", "115200"])
        self.assertIsNone(call["target"])
        self.assertFalse((self.tool / "build").exists())

    def test_header_selects_chip_and_build_directory(self):
        self.add_project("sensor", "esp32c3")
        result = self.invoke("MODEL=sensor")
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(call for call in self.calls() if call["kind"] == "idf")
        self.assertEqual(call["target"], "esp32c3")
        self.assertIn(str(self.tool / "build/sensor/esp32c3"), call["args"])

    def test_argument_order_and_shell_characters(self):
        port = "/dev/serial with spaces;touch SHOULD_NOT_EXIST"
        result = self.invoke(f"PORT={port}", "MODEL=xc200", cwd=self.tool)
        self.assertEqual(result.returncode, 0, result.stderr)
        call = next(call for call in self.calls() if call["kind"] == "idf")
        self.assertEqual(call["args"][call["args"].index("-p") + 1], port)
        self.assertFalse((self.tool / "SHOULD_NOT_EXIST").exists())

    def test_invalid_commands_do_not_start_backend(self):
        cases = [
            (), ("PORT=/dev/ttyUSB0",), ("MODEL=",),
            ("MODEL=../outside",), ("MODEL=xc200", "MODEL=other"),
            ("MODEL=xc200", "PORT=/dev/ttyUSB0", "LOG=/dev/ttyUSB0"),
            ("MODEL=xc200", "LOG=/dev/ttyUSB0", "menuconfig"),
            ("MODEL=xc200", "clean"), ("clean", "menuconfig"),
            ("MODEL=xc200", "UNKNOWN=value"), ("MODEL=missing",),
            ("CHIP=esp32s3",), ("MODEL=xc200", "CHIP=esp32s3", "menuconfig"),
            ("CHIP=../outside", "menuconfig"), ("CHIP=esp32c3", "menuconfig"),
            ("menuconfig", "PORT=/dev/ttyUSB0"), ("CHIP=esp32s3", "clean"),
        ]
        for cli in cases:
            with self.subTest(cli=cli):
                result = self.invoke(*cli)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.calls())

    def test_bad_chip_configuration_is_rejected(self):
        header = self.root / "project/xc200/inc/xf_project_config.h"
        for value in ("", "#define XF_USE_CHIP_ID 3\n", '#define XF_USE_CHIP_ID "esp32p4"\n',
                      '#define XF_USE_CHIP_ID "esp32s3"\n#define XF_USE_CHIP_ID "esp32c3"\n'):
            with self.subTest(value=value):
                header.write_text(value)
                result = self.invoke("MODEL=xc200")
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse(self.calls())

    def test_conf_cannot_redefine_chip(self):
        config = self.root / "project/xc200/sdkconfig.conf"
        config.write_text('CONFIG_IDF_TARGET="esp32s3"\n')
        result = self.invoke("MODEL=xc200")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("XF_USE_CHIP_ID", result.stderr)
        self.assertFalse(self.calls())

    def test_backend_failure_is_propagated(self):
        env = dict(self.env, XF_TEST_BACKEND_EXIT="37")
        result = self.invoke("MODEL=xc200", "LOG=/dev/ttyUSB0", env=env)
        self.assertEqual(result.returncode, 37)
        self.assertEqual([call["kind"] for call in self.calls()], ["format", "idf"])
        self.assertNotIn("Partitions Table", result.stdout)

    def test_format_failure_stops_before_build_report_and_flash(self):
        result = self.invoke("MODEL=xc200", "LOG=/dev/ttyUSB0", env=dict(self.env, XF_TEST_FORMAT_EXIT="23"))
        self.assertEqual(result.returncode, 23)
        self.assertEqual([call["kind"] for call in self.calls()], ["format"])
        self.assertIn("格式化失败", result.stderr)

    def test_missing_formatter_stops_build(self):
        (self.tool / "format_code_linux.sh").unlink()
        result = self.invoke("MODEL=xc200")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.calls())

    def test_report_precedes_flash_and_flash_failure_is_propagated(self):
        env = dict(self.env, XF_TEST_FLASH_EXIT="38")
        result = self.invoke("MODEL=xc200", "LOG=/dev/ttyUSB0", env=env)
        self.assertEqual(result.returncode, 38)
        self.assertEqual([call["kind"] for call in self.calls()], ["format", "idf", "report", "idf"])
        self.assertLess(result.stdout.index("APP Memory Info"), result.stdout.rindex("flash monitor"))

    def test_report_failure_does_not_change_build_result(self):
        env = dict(self.env, XF_TEST_REPORT_EXIT="1")
        result = self.invoke("MODEL=xc200", "PORT=/dev/ttyUSB0", env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("资源统计未完成", result.stderr)
        self.assertEqual([call["kind"] for call in self.calls()], ["format", "idf", "report", "idf"])

    def test_sdk_activation_failure_stops_before_backend(self):
        (self.sdk / "export.sh").write_text("return 7\n")
        result = self.invoke("MODEL=xc200", "PORT=/dev/ttyUSB0")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("环境初始化失败", result.stderr)
        self.assertFalse(self.calls())

    def test_clean_needs_no_sdk_and_preserves_sources(self):
        shutil.rmtree(self.sdk)
        build_dir = self.tool / "build"
        (build_dir / "xc200/esp32s3").mkdir(parents=True)
        (build_dir / "xc200/esp32s3/firmware.bin").write_bytes(b"firmware")
        (build_dir / "sdkconfig").write_text("CONFIG_FREERTOS_HZ=100\n")
        outside = self.root / "outside"
        outside.mkdir()
        (outside / "keep.txt").write_text("keep")
        (build_dir / "external-link").symlink_to(outside, target_is_directory=True)
        result = self.invoke("clean")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(build_dir.exists())
        self.assertTrue((outside / "keep.txt").exists())
        self.assertTrue((self.root / "project/xc200/sdkconfig.conf").exists())
        self.assertTrue((self.tool / "config/esp32s3.conf").exists())
        self.assertEqual(self.invoke("clean").returncode, 0)
        self.assertFalse(self.calls())

    def test_clean_refuses_root_symlink(self):
        outside = self.root / "outside"
        outside.mkdir()
        keep = outside / "keep.txt"
        keep.write_text("keep")
        (self.tool / "build").symlink_to(outside, target_is_directory=True)
        result = self.invoke("clean")
        self.assertNotEqual(result.returncode, 0)
        self.assertTrue(keep.exists())

    def test_help_needs_no_sdk(self):
        shutil.rmtree(self.sdk)
        result = self.invoke("--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("MODEL=xc200", result.stdout)
        self.assertFalse(self.calls())


if __name__ == "__main__":
    unittest.main()
