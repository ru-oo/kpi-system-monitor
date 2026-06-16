cmake_minimum_required(VERSION 3.16)

project(KpiProject VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SerialBus / SerialPort do not exist on iOS. The iPad runs in 'client' mode and
# gets all data over UDP (Network), so those modules — and the CAN/serial source
# files that need them — are excluded from the iOS build.
if(IOS)
    find_package(Qt6 REQUIRED COMPONENTS Quick Network)
else()
    find_package(Qt6 REQUIRED COMPONENTS Quick Network SerialBus SerialPort)
endif()

qt_standard_project_setup(REQUIRES 6.5)

# Auto-generate qmldir for the qml/ and qml/kpi/ subdirectories (silences QTP0004).
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

# Sources that depend on SerialBus/SerialPort (desktop bridge / standalone only).
if(IOS)
    set(KPI_CAN_SOURCES "")
else()
    set(KPI_CAN_SOURCES
        src/CanBridge.h
        src/CanBridge.cpp
        src/SerialGpsBridge.h
        src/SerialGpsBridge.cpp
        src/I2cImuBridge.h
        src/I2cImuBridge.cpp
    )
endif()

qt_add_executable(KpiProject
    src/main.cpp
    src/KpiData.h
    src/KpiData.cpp
    src/WireState.h
    src/StateLink.h
    src/StateLink.cpp
    ${KPI_CAN_SOURCES}
    src/RunRecorder.h
    src/RunRecorder.cpp
    src/MapModel.h
    src/MapModel.cpp
    src/LaneMapModel.h
    src/LaneMapModel.cpp
    src/RawFrameModel.h
    src/RawFrameModel.cpp
    src/DebugLink.h
    src/DebugLink.cpp
    src/Config.h
    src/Config.cpp
)

qt_add_qml_module(KpiProject
    URI KpiProject
    VERSION 1.0
    QML_FILES
        qml/main.qml
        qml/kpi/KpiTheme.qml
        qml/kpi/KpiDashboardApp.qml
        qml/kpi/PagePerformance.qml
        qml/kpi/PageVehicle.qml
        qml/kpi/PageKpiValidation.qml
        qml/kpi/PageSystem.qml
        qml/kpi/PageTactical.qml
        qml/kpi/PageRuns.qml
        qml/kpi/TacticalMap.qml
        qml/kpi/KpiPanel.qml
        qml/kpi/MetricTile.qml
        qml/kpi/MiniStat.qml
        qml/kpi/KvCell.qml
        qml/kpi/SparklineChart.qml
        qml/kpi/ResourceGraph.qml
        qml/kpi/PageDebug.qml
        qml/kpi/RadarChart.qml
        qml/kpi/MemoryTile.qml
        qml/kpi/AccuracyTracker.qml
        qml/kpi/VelocityArc.qml
        qml/kpi/SteeringDial.qml
        qml/kpi/CanStatus.qml
        qml/kpi/Pi5Status.qml
        qml/kpi/LogPanel.qml
        qml/kpi/StateDots.qml
        qml/kpi/AxisDot.qml
        qml/kpi/BigStat.qml
)

set_target_properties(KpiProject PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.kdt.kpiproject"
    MACOSX_BUNDLE_BUNDLE_NAME "KpiProject"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2" # 1: iPhone, 2: iPad
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/ios/Info.plist.in"
    OUTPUT_NAME "KpiProjectApp"
)

target_link_libraries(KpiProject PRIVATE
    Qt6::Quick
    Qt6::Network
)
if(NOT IOS)
    target_link_libraries(KpiProject PRIVATE Qt6::SerialBus Qt6::SerialPort)
endif()

install(TARGETS KpiProject
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Keep runtime data (config + SLAM maps) next to the executable so the app
# loads the current versions from applicationDirPath.
add_custom_command(TARGET KpiProject POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.json" "$<TARGET_FILE_DIR:KpiProject>/config.json"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/maps" "$<TARGET_FILE_DIR:KpiProject>/maps"
)
