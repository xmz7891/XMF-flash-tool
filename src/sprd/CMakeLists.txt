# Spreadtrum (Unisoc) U2S/FDL protocol + PAC firmware container.
# Owner: protocols teammate.
add_library(gf_sprd STATIC
    SprdRegister.cpp SprdTools.cpp SprdTools.h
    U2s.cpp U2s.h
    Fdl.cpp Fdl.h
    Pac.cpp Pac.h
)
target_include_directories(gf_sprd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_sprd PUBLIC gf_core gf_usb Qt6::Core)
