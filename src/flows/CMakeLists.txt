# Flashing pipelines: connect the device/workbench UI with the protocol backends
# (Sahara/Firehose, MTK BROM/DA, SPRD FDL, RockUSB, FEL) and the resource packs.
# Owner: protocols teammate (follow-up task).
# Scope: src/flows/** (new module), docs/FLASH_PIPELINES.md.
add_library(gf_flows STATIC
    FlowsRegister.cpp
    FlowOps.cpp FlowOps.h
    PartitionTable.cpp PartitionTable.h
    QcomFlow.cpp QcomFlow.h
    MtkFlow.cpp MtkFlow.h
    SprdFlow.cpp SprdFlow.h
    RkFlow.cpp RkFlow.h
    AwFlow.cpp AwFlow.h
    FlowTools.cpp FlowTools.h
    MiFlashFlow.cpp MiFlashFlow.h
)
target_include_directories(gf_flows PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(gf_flows PUBLIC
    gf_core gf_usb gf_packets
    gf_qcom gf_mtk gf_sprd gf_rk gf_aw gf_device
    Qt6::Core
)
