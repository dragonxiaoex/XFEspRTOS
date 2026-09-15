# ESP-IDF 提前扫描依赖时使用脚本模式，不能启用 CONFIGURE_DEPENDS。
# 正式配置时跟踪文件列表，新增、删除文件会在下次构建时自动重新配置。
set(common_glob_options)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    list(APPEND common_glob_options CONFIGURE_DEPENDS)
endif()

file(GLOB_RECURSE common_sources ${common_glob_options}
    "${CMAKE_CURRENT_LIST_DIR}/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/*.S"
)
file(GLOB_RECURSE common_headers ${common_glob_options}
    "${CMAKE_CURRENT_LIST_DIR}/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/*.hpp"
)

# 根目录支持带模块路径的 include，头文件所在目录兼容现有的直接 include。
set(common_include_dirs "${CMAKE_CURRENT_LIST_DIR}")
foreach(header IN LISTS common_headers)
    get_filename_component(header_dir "${header}" DIRECTORY)
    list(APPEND common_include_dirs "${header_dir}")
endforeach()
list(REMOVE_DUPLICATES common_include_dirs)

idf_component_register(
    SRCS ${common_sources}
    INCLUDE_DIRS ${common_include_dirs}
    REQUIRES esp_driver_uart
)

# 公共日志使用当前产品的配置头文件，并将该路径提供给依赖公共组件的代码。
# 公共 menuconfig 没有产品上下文，仅配置组件，不编译产品固件。
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND XF_PROJECT_DIR)
    target_include_directories(${COMPONENT_LIB} PUBLIC "${XF_PROJECT_DIR}/inc")
endif()
