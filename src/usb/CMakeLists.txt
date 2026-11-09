# USB transport abstraction: WinUSB bulk/control endpoints used by the
# Qualcomm/MTK/SPRD/RK/Allwinner protocol implementations.
# Owner: protocols teammate.
#
# Contract: every protocol backend must only talk to an IUsbTransport, never to
# the OS directly. Implementations: WinUsbTransport (real hardware),
# MockTransport (unit tests).
add_library(gf_usb STATIC
    UsbTransport.cpp UsbTransport.h
    UsbIo.cpp UsbIo.h
    MockTransport.cpp MockTransport.h
    ProtocolSupport.cpp ProtocolSupport.h
    TaskReporter.cpp TaskReporter.h
    ToolOptions.cpp ToolOptions.h
    WinUsbTransport.cpp WinUsbTransport.h
)
target_include_directories(gf_usb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_usb PUBLIC gf_core Qt6::Core)
if(WIN32)
    target_link_libraries(gf_usb PRIVATE setupapi winusb)
endif()
