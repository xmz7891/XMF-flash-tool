# Qualcomm EDL: Sahara handshake + Firehose programmer protocol.
# Owner: protocols teammate.
add_library(gf_qcom STATIC
    QcomRegister.cpp QcomTools.cpp QcomTools.h
    Elf.cpp Elf.h
    Sahara.cpp Sahara.h
    Firehose.cpp Firehose.h
)
target_include_directories(gf_qcom PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_qcom PUBLIC gf_core gf_usb Qt6::Core)
