# Rockchip RockUSB protocol (RKDevTool compatible).
# Owner: protocols teammate.
add_library(gf_rk STATIC
    RkRegister.cpp RkTools.cpp RkTools.h
    RkFirmware.cpp RkFirmware.h
    RkFirmwareTools.cpp
    RockUsb.cpp RockUsb.h
)
target_include_directories(gf_rk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_rk PUBLIC gf_core gf_usb Qt6::Core)
