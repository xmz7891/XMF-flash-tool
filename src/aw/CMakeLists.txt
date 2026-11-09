# Allwinner FEL protocol (PhoenixSuit compatible).
# Owner: protocols teammate.
add_library(gf_aw STATIC
    AwRegister.cpp AwTools.cpp AwTools.h
    AwFirmware.cpp AwFirmware.h
    AwFirmwareTools.cpp
    Fel.cpp Fel.h
)
target_include_directories(gf_aw PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_aw PUBLIC gf_core gf_usb Qt6::Core)
