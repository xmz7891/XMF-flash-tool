cmake_minimum_required(VERSION 3.22)

project(XMF VERSION 0.2 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/utf-8 /W3)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Quick QuickControls2 Network Concurrent Svg Sql Xml Test
)
qt_standard_project_setup(REQUIRES 6.5)

# ---------------------------------------------------------------------------
# Backend libraries (each lives in its own directory and is owned by one module)
# ---------------------------------------------------------------------------
add_subdirectory(src/core)        # logging, tasks, utils, tool registry, settings
add_subdirectory(src/packets)     # XMFPacket resource packages
add_subdirectory(src/image)       # firmware / image file-format toolbox
add_subdirectory(src/usb)         # USB transport abstraction (WinUSB)
add_subdirectory(src/device)      # port scanning, adb/fastboot, drivers
add_subdirectory(src/qcom)        # Qualcomm EDL (Sahara / Firehose)
add_subdirectory(src/mtk)         # MediaTek BROM / DA / scatter
add_subdirectory(src/sprd)        # Spreadtrum FDL / PAC
add_subdirectory(src/rk)          # Rockchip RockUSB
add_subdirectory(src/aw)          # Allwinner FEL
add_subdirectory(src/samsung)     # Samsung PIT / Odin
add_subdirectory(src/extras)      # root / bootloader / unlock / bypass tooling
add_subdirectory(src/firmware)    # firmware resource search + local library
add_subdirectory(src/flows)       # crossing-platform flashing pipelines
add_subdirectory(src/app)         # controllers + main


# ---------------------------------------------------------------------------
# Tests (optional, enabled by default)
# ---------------------------------------------------------------------------
option(GF_BUILD_TESTS "Build unit tests" ON)
if(GF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Deploy helper: copy runtime dependencies next to the executable
# ---------------------------------------------------------------------------
add_custom_target(deploy
    COMMAND ${CMAKE_COMMAND} -E echo "Deploying runtime..."
)


