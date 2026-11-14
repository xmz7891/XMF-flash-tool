# Firmware / image file-format toolbox.
# Owner: image-core teammate.
# Scope: src/image/**, docs/FORMATS_IMAGE.md, tests under src/image/tests/**.
add_library(gf_image STATIC
    ImageRegister.cpp

    # --- shared helpers -----------------------------------------------------
    ImageCommon.cpp
    Proto.cpp

    # --- format backends ----------------------------------------------------
    Cpio.cpp
    Sparse.cpp
    Gpt.cpp
    Compression.cpp
    BootImage.cpp
    Payload.cpp
    SuperImage.cpp
    Vbmeta.cpp
    ElfFormat.cpp
    BootLogo.cpp BootLogo.h
    ZipPackage.cpp

    # --- tool registrations (ToolSpec + handlers) ---------------------------
    ImageToolsFirmware.cpp
    ImageToolsConvert.cpp
    MbnTools.cpp MbnTools.h
    OplusDigest.cpp OplusDigest.h
)
target_include_directories(gf_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_image PUBLIC gf_core Qt6::Core Qt6::Gui Qt6::Network)

# Unit tests live in src/image/tests/*.cpp and are picked up by tests/CMakeLists.txt.
