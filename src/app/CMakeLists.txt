# Application shell: controllers, native window, entry point, system tools.
# Owned by: lead
set(APP_SOURCES
    main.cpp
    AppController.cpp NativeWindow.cpp FirmwareWorkspace.cpp FirmwareBrowser.cpp OfficialTools.cpp EmbeddedApp.cpp
    FreeNotice.cpp FreeNoticePrivate.cpp
    SystemTools.cpp
)

qt_add_executable(XMF ${APP_SOURCES})
set_target_properties(XMF PROPERTIES
    WIN32_EXECUTABLE $<IF:$<CONFIG:Debug>,FALSE,TRUE>
    OUTPUT_NAME "XMF"
)

target_include_directories(XMF PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(XMF PRIVATE
    Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::Network Qt6::Sql
    gf_core gf_image gf_packets gf_usb gf_device
    gf_qcom gf_mtk gf_sprd gf_rk gf_aw
    gf_extras gf_firmware gf_flows gf_samsung
)
target_compile_definitions(XMF PRIVATE XMF_VERSION="${PROJECT_VERSION}")
if(WIN32)
    target_link_libraries(XMF PRIVATE dwmapi user32 shell32 shlwapi)
    # Executable / taskbar icon (explorer.exe reads this from the PE resources).
    target_sources(XMF PRIVATE ${PROJECT_SOURCE_DIR}/resources/xmf.rc)
    target_include_directories(XMF PRIVATE ${PROJECT_SOURCE_DIR}/resources)
endif()

# ---- QML module ------------------------------------------------------------
# All .qml files under <root>/qml are compiled into the executable as the "XMF"
# QML module. New files are picked up automatically (CONFIGURE_DEPENDS).
file(GLOB_RECURSE QML_FILES CONFIGURE_DEPENDS
    RELATIVE ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/qml/*.qml
)
set(QML_ABS_FILES "")
foreach(qml_file ${QML_FILES})
    list(APPEND QML_ABS_FILES ${PROJECT_SOURCE_DIR}/${qml_file})
    file(RELATIVE_PATH qml_alias ${PROJECT_SOURCE_DIR}/qml ${PROJECT_SOURCE_DIR}/${qml_file})
    set_source_files_properties(${PROJECT_SOURCE_DIR}/${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_alias})
endforeach()
set_source_files_properties(${PROJECT_SOURCE_DIR}/qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(XMF
    URI XMF
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${QML_ABS_FILES}
)

# ---- embedded assets -------------------------------------------------------
file(GLOB_RECURSE ICON_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/icons/*.svg)
if(ICON_FILES)
    qt_add_resources(XMF "icons" PREFIX "/icons" BASE ${PROJECT_SOURCE_DIR}/resources/icons FILES ${ICON_FILES})
endif()
file(GLOB FONT_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/fonts/*.ttf)
if(FONT_FILES)
    qt_add_resources(XMF "fonts" PREFIX "/fonts" BASE ${PROJECT_SOURCE_DIR}/resources/fonts FILES ${FONT_FILES})
endif()
file(GLOB IMAGE_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/resources/images/*)
if(IMAGE_FILES)
    qt_add_resources(XMF "images" PREFIX "/images" BASE ${PROJECT_SOURCE_DIR}/resources/images FILES ${IMAGE_FILES})
endif()

# ---- runtime data next to the executable ----------------------------------
if(EXISTS ${PROJECT_SOURCE_DIR}/data)
    add_custom_command(TARGET XMF POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                ${PROJECT_SOURCE_DIR}/data $<TARGET_FILE_DIR:XMF>/data
        COMMENT "Copying data directory")
endif()

