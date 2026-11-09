# Firmware resource tools: vendor download search, official links, local firmware
# library and hash verification.
# Owner: image-core teammate (follow-up task).
# Scope: src/firmware/** (new module), docs/FIRMWARE_SOURCES.md, data/firmware/**.
add_library(gf_firmware STATIC
    FirmwareRegister.cpp
    FirmwareCommon.cpp
    FirmwareData.cpp
    FirmwareNet.cpp
    FirmwareFiles.cpp
    FirmwareTools.cpp
)
target_include_directories(gf_firmware PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_firmware PUBLIC
    gf_core gf_image gf_sprd
    Qt6::Core Qt6::Network
)
