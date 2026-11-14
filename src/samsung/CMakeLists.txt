add_library(gf_samsung STATIC
    Pit.cpp Pit.h
    SamsungRegister.cpp
)
target_include_directories(gf_samsung PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_samsung PUBLIC gf_core Qt6::Core)
