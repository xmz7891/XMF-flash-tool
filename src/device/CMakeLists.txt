# Device layer: port scanning, ADB/Fastboot wrappers, driver status, batch jobs.
# Owner: device teammate.
add_library(gf_device STATIC
    DeviceEnumerator.cpp DeviceEnumerator.h
    Ports.cpp Ports.h
    PortsModel.cpp PortsModel.h
    PlatformTools.cpp PlatformTools.h
    AdbClient.cpp AdbClient.h
    FastbootClient.cpp FastbootClient.h
    DriverManager.cpp DriverManager.h
    BatchJobManager.cpp BatchJobManager.h
    DeviceRegister.cpp
)
target_include_directories(gf_device PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_device PUBLIC gf_core gf_usb Qt6::Core Qt6::Network)
find_package(Threads REQUIRED)
target_link_libraries(gf_device PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(gf_device PRIVATE setupapi cfgmgr32)
endif()
