# MediaTek: BROM protocol, DA download, preloader/scatter, partition operations.
# Owner: protocols teammate.
add_library(gf_mtk STATIC
    MtkRegister.cpp MtkTools.cpp MtkTools.h
    Brom.cpp Brom.h
    Da.cpp Da.h
    Preloader.cpp Preloader.h
    Scatter.cpp Scatter.h
)
target_include_directories(gf_mtk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_mtk PUBLIC gf_core gf_usb Qt6::Core)
