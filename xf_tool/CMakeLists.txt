cmake_minimum_required(VERSION 3.22.1)

option(XF_COMMON_CONFIG_ONLY "Configure shared chip settings without a product" OFF)
if(XF_COMMON_CONFIG_ONLY)
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/common_config.cmake")
    return()
endif()

set(MODEL "xc200" CACHE STRING "Select project like: xc100, xc200...")

if(NOT MODEL)
    message(FATAL_ERROR "MODEL must name a directory under project/.")
endif()

if(NOT MODEL MATCHES "^[A-Za-z0-9][A-Za-z0-9_-]*$")
    message(FATAL_ERROR "Invalid MODEL: ${MODEL}")
endif()

get_filename_component(XF_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(COMMON_DIR "${XF_ROOT_DIR}/xf_common")
set(XF_PROJECT_DIR "${XF_ROOT_DIR}/project/${MODEL}")
set(XF_PROJECT_CONFIG "${XF_PROJECT_DIR}/sdkconfig.conf")
set(PROJECT_CONFIG "${XF_PROJECT_DIR}/inc/xf_project_config.h")

if(NOT EXISTS "${XF_PROJECT_DIR}/CMakeLists.txt" OR NOT EXISTS "${XF_PROJECT_CONFIG}")
    message(FATAL_ERROR "Project ${MODEL} requires CMakeLists.txt and sdkconfig.conf under ${XF_PROJECT_DIR}.")
endif()

if(NOT EXISTS "${PROJECT_CONFIG}")
    message(FATAL_ERROR "Missing project header: ${PROJECT_CONFIG}")
endif()

# 芯片由产品头文件中的字符串宏选择，保持与 build.py 相同的格式约定。
file(STRINGS "${PROJECT_CONFIG}" XF_TARGET_LINES
    REGEX "^[ \t]*#[ \t]*define[ \t]+XF_USE_CHIP_ID[ \t]+\"[a-z0-9]+\"[ \t]*(/\\*.*\\*/|//.*)?$")
list(LENGTH XF_TARGET_LINES XF_TARGET_COUNT)
if(NOT XF_TARGET_COUNT EQUAL 1)
    message(FATAL_ERROR "${PROJECT_CONFIG} must contain exactly one #define XF_USE_CHIP_ID \"...\" line.")
endif()
string(REGEX MATCH "XF_USE_CHIP_ID[ \t]+\"([a-z0-9]+)\"" XF_TARGET_MATCH "${XF_TARGET_LINES}")
set(XF_IDF_TARGET "${CMAKE_MATCH_1}")

foreach(XF_SELECTED_TARGET "${IDF_TARGET}" "$ENV{IDF_TARGET}")
    if(XF_SELECTED_TARGET AND NOT XF_SELECTED_TARGET STREQUAL XF_IDF_TARGET)
        message(FATAL_ERROR "Project ${MODEL} uses ${XF_IDF_TARGET}, but IDF_TARGET is ${XF_SELECTED_TARGET}. Use a separate build directory and a matching target.")
    endif()
endforeach()
set(IDF_TARGET "${XF_IDF_TARGET}" CACHE STRING "ESP-IDF target")

set(XF_COMMON_CONFIG "${CMAKE_CURRENT_LIST_DIR}/config/${IDF_TARGET}.conf")
if(NOT EXISTS "${XF_COMMON_CONFIG}")
    message(FATAL_ERROR "Missing common chip configuration: ${XF_COMMON_CONFIG}")
endif()
foreach(XF_CONFIG_FILE "${XF_COMMON_CONFIG}" "${XF_PROJECT_CONFIG}")
    file(STRINGS "${XF_CONFIG_FILE}" XF_CONFIG_TARGET REGEX "^[ \t]*CONFIG_IDF_TARGET[ \t]*=")
    if(XF_CONFIG_TARGET)
        message(FATAL_ERROR "Remove CONFIG_IDF_TARGET from ${XF_CONFIG_FILE}; select the chip with XF_USE_CHIP_ID.")
    endif()
endforeach()

# 不允许不同产品复用同一套 CMake 缓存和生成配置。
if(DEFINED XF_CONFIG_MODEL AND NOT XF_CONFIG_MODEL STREQUAL MODEL)
    message(FATAL_ERROR "This build directory belongs to ${XF_CONFIG_MODEL}. Use -B build/${MODEL}/${IDF_TARGET} for ${MODEL}.")
endif()
set(XF_CONFIG_MODEL "${MODEL}" CACHE INTERNAL "Product owning this build directory")

# 文件后缀可自定义；先加载芯片公共配置，再加载产品差异配置。
set(SDKCONFIG_DEFAULTS "${XF_COMMON_CONFIG};${XF_PROJECT_CONFIG}")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
# 维护文件变化后重新生成完整配置，避免旧 sdkconfig 覆盖新的公共/私有设置。
file(SHA256 "${XF_COMMON_CONFIG}" XF_COMMON_CONFIG_HASH)
file(SHA256 "${XF_PROJECT_CONFIG}" XF_PROJECT_CONFIG_HASH)
string(SHA256 XF_CONFIG_INPUTS_HASH "${XF_COMMON_CONFIG_HASH}:${XF_PROJECT_CONFIG_HASH}")
set(XF_CONFIG_INPUTS_STAMP "${CMAKE_BINARY_DIR}/.xf_config_inputs.sha256")
if(EXISTS "${XF_CONFIG_INPUTS_STAMP}" AND EXISTS "${SDKCONFIG}")
    file(READ "${XF_CONFIG_INPUTS_STAMP}" XF_PREVIOUS_CONFIG_HASH)
    if(NOT XF_PREVIOUS_CONFIG_HASH STREQUAL XF_CONFIG_INPUTS_HASH)
        message(STATUS "Public/private configuration changed; regenerating sdkconfig.")
        configure_file("${SDKCONFIG}" "${SDKCONFIG}.before-conf-change" COPYONLY)
        file(REMOVE "${SDKCONFIG}")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${XF_COMMON_CONFIG}" "${XF_PROJECT_CONFIG}" "${PROJECT_CONFIG}")

# 引入 ESP-IDF 的工具，使用上面选定的芯片与配置路径。
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 设置组件路径
set(EXTRA_COMPONENT_DIRS 
    ${COMMON_DIR}/components
    ${COMMON_DIR}/
    ${XF_PROJECT_DIR}/
)

set(PROJECT_VERSION "unknown")

if(EXISTS ${PROJECT_CONFIG})
    file(READ ${PROJECT_CONFIG} VERSION_CONTENT)
    
    string(REGEX MATCH "#define[ \t]+XF_SOFTWARE_VERSION_STRING[ \t]+\"([^\"]+)\"" _ ${VERSION_CONTENT})
    set(PROJECT_VERSION ${CMAKE_MATCH_1})
    
    message(STATUS "Detected version for ${MODEL}: ${PROJECT_VERSION}")
else()
    message(WARNING "Version header not found: ${PROJECT_CONFIG}")
endif()

# 项目名称（最终固件名称）
project("${MODEL}-v${PROJECT_VERSION}")

# 配置成功后才记录输入；首次接入保留已有 menuconfig 保存结果。
file(WRITE "${XF_CONFIG_INPUTS_STAMP}" "${XF_CONFIG_INPUTS_HASH}")
