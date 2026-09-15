"""验证分区容量、APP 分区选择和静态内存统计口径。"""

import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build_report


PARTITIONS = """# ESP-IDF Partition Table
# Name, Type, SubType, Offset, Size, Flags
nvs,data,nvs,0x9000,24K,
phy_init,data,phy,0xf000,4K,
ota_0,app,ota_0,0x10000,1M,
ota_1,app,ota_1,0x210000,2M,
"""


class BuildReportTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="xf-build-report-")
        self.addCleanup(self.temp.cleanup)
        self.build = Path(self.temp.name) / "build with spaces"
        (self.build / "config").mkdir(parents=True)
        self.project = {"project_name": "sensor-v2", "target": "esp32s3", "app_elf": "sensor-v2.elf"}
        self.flasher = {
            "bootloader": {"offset": "0x1000", "file": "bootloader/bootloader.bin"},
            "partition-table": {"offset": "0x8000", "file": "partition_table/partition-table.bin"},
            "app": {"offset": "0x210000", "file": "sensor-v2.bin"},
        }
        self.config = {"ESPTOOLPY_FLASHSIZE": "16MB", "SPIRAM": True}
        self.size_info = {"layout": [
            {"name": "Flash Code", "total": 0x800000, "used": 0x100000},
            {"name": "Flash Data", "total": 0x800000, "used": 0x40000},
            {"name": "IRAM", "total": 0x4000, "used": 0x1000},
            {"name": "DIRAM", "total": 0x40000, "used": 0x10000},
            {"name": "DRAM", "total": 0, "used": 0},
            {"name": "External RAM", "total": 0x2000000, "used": 0},
            {"name": "RTC FAST", "total": 0x2000, "used": 24},
        ]}
        for name, data in (("project_description.json", self.project), ("flasher_args.json", self.flasher),
                           ("config/sdkconfig.json", self.config)):
            (self.build / name).write_text(json.dumps(data))
        with (self.build / "sensor-v2.bin").open("wb") as output:
            output.truncate(0x180000)

    def sdk_output(self, command):
        if command[1].endswith("gen_esp32part.py"):
            self.assertEqual(command[-1], str(self.build / "partition_table/partition-table.bin"))
            self.assertEqual(command[command.index("--primary-bootloader-offset") + 1], "0x1000")
            return PARTITIONS
        self.assertTrue(command[1].endswith("idf_size.py"))
        self.assertIn("json2", command)
        self.assertEqual(command[-1], str(self.build / "sensor-v2.map"))
        return json.dumps(self.size_info)

    def test_report_uses_flashed_app_partition_and_actual_bin_size(self):
        with patch.object(build_report, "tool_output", side_effect=self.sdk_output):
            report = build_report.build_report(self.build, Path("/sdk"))
        self.assertIn("sensor-v2 / esp32s3", report)
        self.assertRegex(report, r"bootloader\s+0x00001000\s+28K")
        self.assertRegex(report, r"partition_table\s+0x00008000\s+4K")
        self.assertRegex(report, r"ota_1\s+0x00210000\s+2048K")
        self.assertRegex(report, r"FLASH \(ota_1\)\s+0x00200000\s+1572864 B\s+75.00%")
        self.assertRegex(report, r"DIRAM\s+0x00040000\s+65536 B\s+25.00%")
        self.assertNotIn("Flash Code", report)
        self.assertNotIn("Flash Data", report)
        self.assertRegex(report, r"PSRAM\s+N/A\s+0 B\s+N/A")
        self.assertIn("动态缓冲区不在本表统计范围", report)

    def test_custom_table_offset_and_explicit_reserved_partitions(self):
        flasher = dict(self.flasher, **{"partition-table": {"offset": "0x10000", "file": "custom.bin"}})
        csv = """boot_custom,bootloader,primary,0x1000,60K,
table_custom,partition_table,primary,0x10000,4K,
factory,app,factory,0x20000,1M,
"""
        with patch.object(build_report, "tool_output", return_value=csv) as tool:
            partitions = build_report.read_partitions(self.build, flasher, Path("/sdk"))
        self.assertEqual(len(partitions), 3)
        self.assertEqual(partitions[0]["size"], 60 * 1024)
        self.assertIn("0x10000", tool.call_args.args[0])

    def test_unknown_app_offset_is_not_guessed(self):
        with self.assertRaisesRegex(build_report.ReportError, "APP 分区"):
            build_report.memory_rows([], self.flasher, 512, self.size_info, self.config)

    def test_psram_static_usage_has_no_physical_capacity_percentage(self):
        self.size_info["layout"][5]["used"] = 32768
        partitions = [{"name": "ota_1", "type": "app", "offset": 0x210000, "size": 0x200000}]
        rows = build_report.memory_rows(partitions, self.flasher, 512, self.size_info, self.config)
        self.assertIn(("PSRAM", None, 32768), rows)

    def test_other_chip_uses_its_own_memory_types(self):
        info = {"layout": [{"name": "DRAM", "total": 200000, "used": 20000}]}
        partitions = [{"name": "factory", "type": "app", "offset": 0x210000, "size": 0x100000}]
        rows = build_report.memory_rows(partitions, self.flasher, 512, info, {})
        self.assertEqual(rows, [("FLASH (factory)", 0x100000, 512), ("DRAM", 200000, 20000)])

    def test_missing_psram_statistics_are_not_reported_as_zero(self):
        partitions = [{"name": "factory", "type": "app", "offset": 0x210000, "size": 0x100000}]
        rows = build_report.memory_rows(partitions, self.flasher, 512, {"layout": []}, self.config)
        self.assertIn(("PSRAM", None, None), rows)

    def test_invalid_partition_output_is_rejected(self):
        for csv in ("", "# header only\n", "broken,app,factory\n"):
            with self.subTest(csv=csv), patch.object(build_report, "tool_output", return_value=csv):
                with self.assertRaises(build_report.ReportError):
                    build_report.read_partitions(self.build, self.flasher, Path("/sdk"))


if __name__ == "__main__":
    unittest.main()
