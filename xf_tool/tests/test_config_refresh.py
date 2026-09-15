"""验证实际顶层 CMake 的配置刷新与本地菜单状态保留行为。"""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


CMAKE = os.environ.get("XF_TEST_CMAKE") or shutil.which("cmake")
SOURCE = Path(__file__).resolve().parents[1] / "CMakeLists.txt"


class ConfigRefreshTest(unittest.TestCase):
    def setUp(self):
        if not CMAKE:
            self.skipTest("需要 CMake 3.22.1 及以上")
        version = subprocess.check_output([CMAKE, "--version"], text=True)
        parts = tuple(map(int, re.search(r"(\d+)\.(\d+)\.(\d+)", version).groups()))
        if parts < (3, 22, 1):
            self.skipTest("请加载 ESP-IDF 环境，或用 XF_TEST_CMAKE 指定新版 CMake")
        self.temp = tempfile.TemporaryDirectory(prefix="xf-config-refresh-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tool = self.root / "xf_tool"
        (self.tool / "config").mkdir(parents=True)
        shutil.copyfile(SOURCE, self.tool / "CMakeLists.txt")
        shutil.copytree(SOURCE.parent / "cmake", self.tool / "cmake")
        project = self.root / "project/test"
        (project / "inc").mkdir(parents=True)
        (project / "CMakeLists.txt").touch()
        (project / "inc/xf_project_config.h").write_text(
            '#define XF_USE_CHIP_ID "esp32s3"\n#define XF_SOFTWARE_VERSION_STRING "1.0"\n'
        )
        self.common = self.tool / "config/esp32s3.conf"
        self.common.write_text("CONFIG_NUMBER=100\n")
        self.private = project / "sdkconfig.conf"
        self.private.write_text("CONFIG_FEATURE=y\n")
        sdk = self.root / "sdk"
        (sdk / "tools/cmake").mkdir(parents=True)
        # 只替换 SDK 配置生成端，配置输入摘要和刷新逻辑使用仓库内的真实 CMake。
        (sdk / "tools/cmake/project.cmake").write_text('''
if(NOT EXISTS "${SDKCONFIG}")
    file(WRITE "${SDKCONFIG}" "")
    foreach(input IN LISTS SDKCONFIG_DEFAULTS)
        file(READ "${input}" input_text)
        file(APPEND "${SDKCONFIG}" "${input_text}")
    endforeach()
endif()
macro(project)
endmacro()
''')
        self.env = dict(os.environ, IDF_PATH=str(sdk), IDF_TARGET="esp32s3")
        self.build = self.root / "build"

    def configure(self, common=False):
        arguments = ["-DXF_COMMON_CONFIG_ONLY=ON", "-DIDF_TARGET=esp32s3"] if common else ["-DMODEL=test"]
        result = subprocess.run([CMAKE, "-S", str(self.tool), "-B", str(self.build), *arguments],
                                env=self.env, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_conf_changes_refresh_but_unchanged_inputs_keep_menu_values(self):
        self.configure()
        generated = self.build / "sdkconfig"
        self.assertEqual(generated.read_text(), self.common.read_text() + self.private.read_text())
        generated.write_text("saved menu values\n")
        self.configure()
        self.assertEqual(generated.read_text(), "saved menu values\n")
        self.common.write_text("CONFIG_NUMBER=200\n")
        self.configure()
        self.assertEqual((self.build / "sdkconfig.before-conf-change").read_text(), "saved menu values\n")
        self.assertEqual(generated.read_text(), self.common.read_text() + self.private.read_text())
        self.private.write_text("CONFIG_FEATURE=n\n")
        self.configure()
        self.assertEqual(generated.read_text(), self.common.read_text() + self.private.read_text())

    def test_first_adoption_keeps_existing_saved_config(self):
        self.build.mkdir()
        (self.build / "sdkconfig").write_text("existing saved menu values\n")
        self.configure()
        self.assertEqual((self.build / "sdkconfig").read_text(), "existing saved menu values\n")

    def test_common_context_has_no_product_and_refreshes_only_on_common_changes(self):
        self.configure(common=True)
        generated = self.build / "sdkconfig"
        self.assertEqual(generated.read_text(), self.common.read_text())
        generated.write_text("saved common menu values\n")
        self.private.write_text("CONFIG_FEATURE=n\n")
        self.configure(common=True)
        self.assertEqual(generated.read_text(), "saved common menu values\n")
        self.common.write_text("CONFIG_NUMBER=200\n")
        self.configure(common=True)
        self.assertEqual(generated.read_text(), self.common.read_text())
        self.assertEqual((self.build / "sdkconfig.before-conf-change").read_text(), "saved common menu values\n")
        shutil.rmtree(self.root / "project")
        self.configure(common=True)

    def test_product_build_directory_cannot_be_used_for_common_menu(self):
        self.configure()
        result = subprocess.run([CMAKE, "-S", str(self.tool), "-B", str(self.build),
                                 "-DXF_COMMON_CONFIG_ONLY=ON", "-DIDF_TARGET=esp32s3"],
                                env=self.env, capture_output=True, text=True, timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("own build directory", result.stderr)


if __name__ == "__main__":
    unittest.main()
