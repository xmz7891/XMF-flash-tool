# Unit tests. Any module can drop test_*.cpp files under src/<module>/tests/ and
# they are picked up automatically. Use the GF_TEST_CLASS macro from TestRegistry.h.
file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*/tests/*.cpp)

if(NOT TEST_SOURCES)
    add_custom_target(xmf_tests
        COMMAND ${CMAKE_COMMAND} -E echo "No tests yet - add src/<module>/tests/test_*.cpp")
    return()
endif()

qt_add_executable(xmf_tests
    main.cpp
    TestRegistry.h
    ${TEST_SOURCES}
)
target_include_directories(xmf_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tests
)
target_link_libraries(xmf_tests PRIVATE
    Qt6::Test Qt6::Core Qt6::Gui
    gf_core gf_image gf_packets gf_usb gf_device
    gf_qcom gf_mtk gf_sprd gf_rk gf_aw
    gf_extras gf_firmware gf_flows
)
add_test(NAME xmf_tests COMMAND xmf_tests)
