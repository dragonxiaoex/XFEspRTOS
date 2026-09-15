"""验证公共组件自动收集文件及增量构建时的文件增删识别。"""

import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


CMAKE = os.environ.get("XF_TEST_CMAKE") or shutil.which("cmake")
SOURCE = Path(__file__).resolve().parents[2] / "xf_common/components/common/CMakeLists.txt"


class CommonSourcesTest(unittest.TestCase):
    def setUp(self):
        if not CMAKE:
            self.skipTest("需要 CMake 3.22.1 及以上")
        version = subprocess.check_output([CMAKE, "--version"], text=True)
        parts = tuple(map(int, re.search(r"(\d+)\.(\d+)\.(\d+)", version).groups()))
        if parts < (3, 22, 1):
            self.skipTest("请加载 ESP-IDF 环境，或用 XF_TEST_CMAKE 指定新版 CMake")
        self.temp = tempfile.TemporaryDirectory(prefix="xf-common-sources-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.common = self.root / "common"
        self.common.mkdir()
        shutil.copyfile(SOURCE, self.common / "CMakeLists.txt")
        self.build = self.root / "build"

    def write(self, name, content):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def run_command(self, *arguments):
        result = subprocess.run(arguments, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def build_and_check(self, expected_sources):
        self.run_command(CMAKE, "--build", str(self.build))
        self.run_command(str(self.build / "app"))
        commands = json.loads((self.build / "compile_commands.json").read_text())
        sources = {Path(command["file"]).relative_to(self.root).as_posix() for command in commands}
        self.assertEqual(sources, {"main.c", *expected_sources})

    def test_incremental_build_discovers_sources_and_header_directories(self):
        if not shutil.which("ninja") or not shutil.which("cc") or not shutil.which("c++"):
            self.skipTest("需要 Ninja 和主机 C/C++ 编译器")
        # 使用真实 common CMake，仅以主机静态库替代 ESP-IDF 组件注册。
        self.write("CMakeLists.txt", '''
cmake_minimum_required(VERSION 3.22.1)
project(common_sources C CXX ASM)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(XF_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/product")
macro(idf_component_register)
    cmake_parse_arguments(COMP "" "" "SRCS;INCLUDE_DIRS;REQUIRES" ${ARGN})
    add_library(common STATIC ${COMP_SRCS})
    target_include_directories(common PUBLIC ${COMP_INCLUDE_DIRS})
    set(COMPONENT_LIB common)
endmacro()
add_subdirectory(common)
add_executable(app main.c)
target_link_libraries(app PRIVATE common)
''')
        self.write("product/inc/xf_project_config.h", "#define TEST_VALUE 7\n")
        self.write("common/base/base.h", "int base_value(void);\n")
        self.write("common/base/base.c", '#include "xf_project_config.h"\nint base_value(void) { return TEST_VALUE; }\n')
        self.write("main.c", '#include "base.h"\nint main(void) { return base_value() != 7; }\n')
        self.run_command(CMAKE, "-S", str(self.root), "-B", str(self.build), "-G", "Ninja")
        self.build_and_check({"common/base/base.c"})

        # 新模块位于新建的多级目录；后续只执行 build，不手动重新配置。
        self.write("common/module/nested/extra.h", "int extra_value(void);\n")
        self.write("common/module/nested/extra.c", "int extra_value(void) { return 11; }\n")
        self.write("common/module/nested/cpp.cpp", 'extern "C" int cpp_value(void) { return 13; }\n')
        self.write("main.c", '''
#include "base.h"
#include "extra.h"
int cpp_value(void);
int main(void) { return base_value() + extra_value() + cpp_value() != 31; }
''')
        self.build_and_check({"common/base/base.c", "common/module/nested/extra.c", "common/module/nested/cpp.cpp"})

        # 只有头文件的新目录也需要更新包含路径。
        self.write("common/settings/values.h", "#define EXTRA_VALUE 19\n")
        self.write("common/module/nested/extra.c", '#include "values.h"\nint extra_value(void) { return EXTRA_VALUE; }\n')
        self.write("main.c", '#include "extra.h"\nint main(void) { return extra_value() != 19; }\n')
        self.build_and_check({"common/base/base.c", "common/module/nested/extra.c", "common/module/nested/cpp.cpp"})

        # 移动源文件、删除源文件和整个头文件目录，无须 clean。
        (self.common / "module/nested/extra.c").rename(self.common / "extra.c")
        (self.common / "module/nested/cpp.cpp").unlink()
        shutil.rmtree(self.common / "base")
        self.build_and_check({"common/extra.c"})

    def test_early_dependency_expansion_works_in_script_mode(self):
        self.write("common/nested/value.c", "int value(void) { return 0; }\n")
        self.write("common/nested/value.h", "int value(void);\n")
        self.write("early.cmake", '''
cmake_minimum_required(VERSION 3.22.1)
set(CMAKE_BUILD_EARLY_EXPANSION 1)
set(XF_PROJECT_DIR "${CMAKE_CURRENT_LIST_DIR}/product")
macro(idf_component_register)
    cmake_parse_arguments(COMP "" "" "SRCS;INCLUDE_DIRS;REQUIRES" ${ARGN})
    if(NOT "esp_driver_uart" IN_LIST COMP_REQUIRES)
        message(FATAL_ERROR "Missing UART dependency")
    endif()
    if(NOT "${CMAKE_CURRENT_LIST_DIR}/nested" IN_LIST COMP_INCLUDE_DIRS)
        message(FATAL_ERROR "Missing header directory during dependency expansion")
    endif()
endmacro()
include("${CMAKE_CURRENT_LIST_DIR}/common/CMakeLists.txt")
''')
        self.run_command(CMAKE, "-P", str(self.root / "early.cmake"))


if __name__ == "__main__":
    unittest.main()
