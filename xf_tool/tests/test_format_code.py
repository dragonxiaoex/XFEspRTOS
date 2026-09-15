"""验证格式化脚本的目录边界、路径传递、失败处理和实际 AStyle 输出。"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "format_code_linux.sh"
SOURCE = "int test( int * value )\n{\n\tif(*value>0)\n\t{\n\t\treturn *value+1;\n\t}\n\treturn 0;\n}\n"


class FormatCodeTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="xf-format-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "workspace with spaces"
        self.tool = self.root / "xf_tool"
        self.tool.mkdir(parents=True)
        shutil.copyfile(SCRIPT, self.tool / SCRIPT.name)
        self.files = {}
        for name in (
            "xf_common/main/entry.c", "xf_common/components/common/driver.c",
            "project/xc200/inc/header with spaces.h", "project/xc200/src/device.c",
            "project/xc100/src/device.c", "project/xc200/inc/lv_conf.h",
            "project/xc200/images/image.c", "project/xc200/build/generated.c",
            "project/xc200/managed_components/vendor.c", "esp-idf/vendor.c",
            "xf_common/components/lvgl/vendor.c", "ref/sonoff_wifi.c",
        ):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(SOURCE)
            self.files[name] = path
        self.cwd = Path(self.temp.name) / "other directory"
        self.cwd.mkdir()
        (self.cwd / "unrelated.h").write_text(SOURCE)
        (self.root / "project/xc200/src/external.c").symlink_to(self.cwd / "unrelated.h")

    def invoke(self, *args, env=None):
        return subprocess.run(
            ["/bin/bash", str(self.tool / SCRIPT.name), *args], cwd=self.cwd,
            env=env, capture_output=True, text=True, timeout=10,
        )

    def test_real_format_from_other_directory_and_scope(self):
        if not shutil.which("astyle"):
            self.skipTest("需要安装 astyle 以验证实际格式")
        result = self.invoke("xc200")
        self.assertEqual(result.returncode, 0, result.stderr)
        selected = {
            "xf_common/main/entry.c", "xf_common/components/common/driver.c",
            "project/xc200/inc/header with spaces.h", "project/xc200/src/device.c",
        }
        for name, path in self.files.items():
            with self.subTest(name=name):
                text = path.read_text()
                if name in selected:
                    self.assertIn("int test(int *value)\n{", text)
                    self.assertIn("    if (*value > 0) {", text)
                    self.assertIn("return *value + 1;", text)
                    self.assertNotIn("\t", text)
                else:
                    self.assertEqual(text, SOURCE)
        self.assertEqual((self.cwd / "unrelated.h").read_text(), SOURCE)
        self.assertFalse(list(self.root.rglob("*.orig")))
        before = {name: (path.read_bytes(), path.stat().st_mtime_ns) for name, path in self.files.items()}
        self.assertEqual(self.invoke("xc200").returncode, 0)
        self.assertEqual(before, {name: (path.read_bytes(), path.stat().st_mtime_ns)
                                  for name, path in self.files.items()})

    def test_no_model_formats_all_products_and_ignores_external_options(self):
        if not shutil.which("astyle"):
            self.skipTest("需要安装 astyle 以验证实际格式")
        options = self.cwd / "bad-options"
        options.write_text("invalid-option-must-not-be-read\n")
        env = dict(os.environ, ARTISTIC_STYLE_OPTIONS=str(options), ARTISTIC_STYLE_PROJECT_OPTIONS=str(options))
        result = self.invoke(env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        for model in ("xc100", "xc200"):
            self.assertIn("    if (*value > 0) {", self.files[f"project/{model}/src/device.c"].read_text())

    def test_missing_tool_and_formatter_failure_are_reported(self):
        tools = self.cwd / "tools"
        tools.mkdir()
        for command in ("dirname", "find"):
            (tools / command).symlink_to(shutil.which(command))
        env = dict(os.environ, PATH=str(tools))
        result = self.invoke("xc200", env=env)
        self.assertEqual(result.returncode, 127)
        self.assertIn("astyle", result.stderr)
        formatter = tools / "astyle"
        formatter.write_text("#!/bin/sh\nprintf 'formatter failed\\n' >&2\nexit 17\n")
        formatter.chmod(0o755)
        result = self.invoke("xc200", env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("formatter failed", result.stderr)

    def test_invalid_model_does_not_modify_sources(self):
        for args in (("../outside",), ("missing",), ("xc200", "xc100")):
            with self.subTest(args=args):
                self.assertNotEqual(self.invoke(*args).returncode, 0)
                self.assertTrue(all(path.read_text() == SOURCE for path in self.files.values()))

    def test_no_sources_does_not_start_formatter_or_wait_for_stdin(self):
        for path in self.files.values():
            path.unlink()
        result = self.invoke("xc200")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(result.stdout)


if __name__ == "__main__":
    unittest.main()
