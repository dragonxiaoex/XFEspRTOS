"""用真实 Kconfig 引擎验证菜单配置回写及冷启动重建的一致性。"""

import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import config_sync

try:
    import esp_kconfiglib.core as kconfiglib
    from kconfgen.core import get_json_values
except ImportError:
    kconfiglib = None


KCONFIG = '''mainmenu "Config sync test"
config IDF_TARGET
    string
    default "esp32s3"
config COMMON_ON
    bool "Shared switch"
    default n
config NUMBER
    int "Number"
    default 10
choice
    prompt "Mode"
    default MODE_A
config MODE_A
    bool "Mode A"
config MODE_B
    bool "Mode B"
endchoice
config FEATURE
    bool "Feature"
    default n
config TIMEOUT
    int "Timeout"
    default 800 if FEATURE
    default 300
config CHILD
    bool "Child"
    depends on FEATURE
    default y
config DERIVED
    int
    default 42 if FEATURE
    default 1
config TEXT
    string "Text"
    default "base"
'''


@unittest.skipIf(kconfiglib is None, "使用 ESP-IDF Python 环境运行 Kconfig 测试")
class ConfigSyncTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="xf-config-sync-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.build.mkdir()
        self.common = self.root / "common.conf"
        self.private = self.root / "private.conf"
        self.common.write_text("CONFIG_COMMON_ON=y\nCONFIG_MODE_B=y\nCONFIG_NUMBER=20\n")
        self.private.write_text("# 保留产品注释。\nCONFIG_FEATURE=y\n")
        (self.root / "Kconfig").write_text(KCONFIG)
        (self.root / "sdkconfig.rename").touch()
        (self.build / "config.env").write_text(json.dumps({
            "IDF_PATH": str(self.root), "IDF_VERSION": "6.1.0", "IDF_TARGET": "esp32s3",
            "COMPONENT_SDKCONFIG_RENAMES": "",
        }))
        environment = patch.dict(os.environ, {"KCONFIG_PARSER_VERSION": "1"})
        environment.start()
        self.addCleanup(environment.stop)

    def resolve(self, override):
        config = kconfiglib.Kconfig(str(self.root / "Kconfig"), print_report=False)
        config.load_config(str(self.common), print_report=False)
        config.load_config(str(override), replace=False, print_report=False)
        return config

    def save_menu(self, settings):
        menu = self.root / "menu.conf"
        menu.write_text(settings)
        config = self.resolve(menu)
        config.write_config(str(self.build / "sdkconfig"))
        return get_json_values(config)

    def assert_roundtrip(self, expected):
        original_common = self.common.read_bytes()
        config_sync.sync_config(self.build, self.common, self.private)
        self.assertEqual(get_json_values(self.resolve(self.private)), expected)
        self.assertEqual(self.common.read_bytes(), original_common)
        self.assertIn("保留产品注释", self.private.read_text())

    def test_disable_shared_boolean_and_return_choice_to_sdk_default(self):
        expected = self.save_menu("CONFIG_COMMON_ON=n\nCONFIG_MODE_A=y\nCONFIG_NUMBER=10\n")
        self.assert_roundtrip(expected)
        text = self.private.read_text()
        self.assertIn("CONFIG_COMMON_ON=n", text)
        self.assertIn("CONFIG_MODE_A=y", text)
        self.assertIn("CONFIG_NUMBER=10", text)
        self.assertNotIn("CONFIG_FEATURE=", text)

    def test_conditional_default_equal_to_public_baseline_still_needs_override(self):
        expected = self.save_menu("CONFIG_FEATURE=y\nCONFIG_TIMEOUT=300\n")
        self.assert_roundtrip(expected)
        text = self.private.read_text()
        self.assertIn("CONFIG_FEATURE=y", text)
        self.assertIn("CONFIG_TIMEOUT=300", text)
        self.assertNotIn("CONFIG_CHILD=", text)
        self.assertNotIn("CONFIG_DERIVED=", text)
        self.assertNotIn("CONFIG_IDF_TARGET=", text)

    def test_default_markers_do_not_leak_between_reconstruction_attempts(self):
        self.save_menu("CONFIG_FEATURE=y\n")
        saved = self.build / "sdkconfig"
        saved.write_text(saved.read_text().replace("CONFIG_FEATURE=y", "# default:\nCONFIG_FEATURE=y"))
        expected = get_json_values(self.resolve(saved))
        self.assert_roundtrip(expected)
        self.assertIn("CONFIG_FEATURE=y", self.private.read_text())
        self.assertNotIn("# default:", self.private.read_text())

    def test_common_disabled_default_is_written_as_plain_assignment(self):
        self.save_menu("CONFIG_COMMON_ON=n\n")
        saved = self.build / "sdkconfig"
        saved.write_text(saved.read_text().replace("# CONFIG_COMMON_ON is not set",
                                                   "# default:\n# CONFIG_COMMON_ON is not set"))
        config_sync.sync_config(self.build, self.common)
        self.assertIn("CONFIG_COMMON_ON=n", self.common.read_text())
        self.assertNotIn("# default:", self.common.read_text())

    def test_strings_and_empty_strings_roundtrip(self):
        for setting in ('CONFIG_TEXT=""\n', 'CONFIG_TEXT="path with spaces \\"quoted\\""\n'):
            with self.subTest(setting=setting):
                self.assert_roundtrip(self.save_menu(setting))

    def test_repeated_sync_does_not_rewrite_files(self):
        expected = self.save_menu("CONFIG_FEATURE=y\n")
        self.assert_roundtrip(expected)
        first = self.private.read_bytes(), self.private.stat().st_mtime_ns
        config_sync.sync_config(self.build, self.common, self.private)
        self.assertEqual(first, (self.private.read_bytes(), self.private.stat().st_mtime_ns))

    def test_failed_validation_preserves_private_and_saved_config(self):
        self.save_menu("CONFIG_FEATURE=y\n")
        original = self.private.read_bytes()
        saved = (self.build / "sdkconfig").read_bytes()
        with patch.object(config_sync, "render_config", return_value="CONFIG_FEATURE=n\n"):
            with self.assertRaises(config_sync.ConfigSyncError):
                config_sync.sync_config(self.build, self.common, self.private)
        self.assertEqual(self.private.read_bytes(), original)
        self.assertEqual((self.build / "sdkconfig").read_bytes(), saved)
        self.assertFalse((self.build / "config_sync/sdkconfig.conf.before-sync").exists())

    def test_common_menu_changes_common_without_importing_or_modifying_private(self):
        self.common.write_text("# 公共约定。\nCONFIG_NUMBER=20\nCONFIG_COMMON_ON=y\n")
        private_before = self.private.read_bytes()
        expected = self.save_menu("CONFIG_NUMBER=30\nCONFIG_COMMON_ON=n\n")
        config_sync.sync_config(self.build, self.common)
        text = self.common.read_text()
        self.assertIn("公共约定", text)
        self.assertIn("CONFIG_NUMBER=30", text)
        self.assertIn("CONFIG_COMMON_ON=n", text)
        self.assertNotIn("CONFIG_FEATURE=", text)
        self.assertEqual(self.private.read_bytes(), private_before)
        config = kconfiglib.Kconfig(str(self.root / "Kconfig"), print_report=False)
        config.load_config(str(self.common), print_report=False)
        self.assertEqual(get_json_values(config), expected)
        self.assertEqual((self.build / "config_sync/common.conf.before-sync").read_text(),
                         "# 公共约定。\nCONFIG_NUMBER=20\nCONFIG_COMMON_ON=y\n")

    def test_common_defaults_and_unavailable_options_are_preserved_on_noop(self):
        self.common.write_text("# 公共约定。\nCONFIG_NUMBER=10\nCONFIG_FUTURE_COMPONENT=y\n")
        self.save_menu("")
        before = self.common.read_bytes(), self.common.stat().st_mtime_ns
        config_sync.sync_config(self.build, self.common)
        self.assertEqual(before, (self.common.read_bytes(), self.common.stat().st_mtime_ns))

    def test_common_conditional_defaults_roundtrip_and_private_still_overrides(self):
        expected = self.save_menu("CONFIG_FEATURE=y\nCONFIG_TIMEOUT=300\nCONFIG_NUMBER=30\n")
        self.private.write_text("CONFIG_NUMBER=99\n")
        config_sync.sync_config(self.build, self.common)
        self.assertIn("CONFIG_TIMEOUT=300", self.common.read_text())
        actual = get_json_values(self.resolve(self.private))
        self.assertEqual(actual, dict(expected, NUMBER=99))
        self.private.write_text("")
        self.assertEqual(get_json_values(self.resolve(self.private)), expected)

    def test_common_failed_validation_preserves_all_inputs(self):
        self.save_menu("CONFIG_NUMBER=30\n")
        before = [path.read_bytes() for path in (self.common, self.private, self.build / "sdkconfig")]
        with patch.object(config_sync, "render_config", return_value="CONFIG_NUMBER=99\n"):
            with self.assertRaises(config_sync.ConfigSyncError):
                config_sync.sync_config(self.build, self.common)
        self.assertEqual(before, [path.read_bytes() for path in (self.common, self.private, self.build / "sdkconfig")])
        self.assertFalse((self.build / "config_sync/common.conf.before-sync").exists())


if __name__ == "__main__":
    unittest.main()
