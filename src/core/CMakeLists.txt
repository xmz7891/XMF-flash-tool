# Core: logging, task engine, tool registry, settings, utilities.
# Owned by: lead
add_library(gf_core STATIC
    Log.cpp Log.h
    TaskManager.cpp TaskManager.h
    ToolRegistry.cpp ToolRegistry.h
    Util.cpp Util.h
    Settings.cpp Settings.h
)
target_include_directories(gf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_core PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Concurrent)
