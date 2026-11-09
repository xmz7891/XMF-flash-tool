# GeekFlashPacket resource package support (Config.xml + embedded resources).
# Owner: lead
add_library(gf_packets STATIC
    PacketRegister.cpp
    PacketStore.cpp PacketStore.h
    ZipArchive.cpp ZipArchive.h
    third_party/miniz.c
    third_party/miniz_tdef.c
    third_party/miniz_tinfl.c
    third_party/miniz_zip.c
)
target_include_directories(gf_packets PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_link_libraries(gf_packets PUBLIC gf_core Qt6::Core Qt6::Xml Qt6::Gui)
