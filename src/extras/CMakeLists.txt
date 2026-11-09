# Root / KSU / APatch / Magisk + bootloader / unlock / bypass tools.
# Owner: extras teammate.
# Scope: src/extras/** (new module), docs/EXTRAS.md.
add_library(gf_extras STATIC
    ExtrasRegister.cpp

    # shared helpers
    ExtrasUtils.cpp ExtrasUtils.h
    Lz4Codec.cpp Lz4Codec.h

    # root / magisk / kernelsu
    RootImage.cpp RootImage.h
    RootSupport.cpp RootSupport.h
    RootTools.cpp

    # bootloader
    BootloaderSupport.cpp BootloaderSupport.h
    BootloaderTools.cpp

    # unlock / wipe
    UnlockTools.cpp

    # engineering / bypass info
    BypassSupport.cpp BypassSupport.h
    BypassTools.cpp
)
target_include_directories(gf_extras PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_extras PUBLIC
    gf_core gf_image gf_device gf_usb
    Qt6::Core Qt6::Gui
)
