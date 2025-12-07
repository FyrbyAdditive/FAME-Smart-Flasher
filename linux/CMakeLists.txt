cmake_minimum_required(VERSION 3.16)
project(fame-smart-flasher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

# Find libudev for serial port enumeration
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED libudev)

# Source files
set(SOURCES
    src/main.cpp
    src/protocol/SLIPCodec.cpp
    src/protocol/ESP32Protocol.cpp
    src/serial/SerialConnection.cpp
    src/serial/SerialPortManager.cpp
    src/services/FlashingService.cpp
    src/models/FirmwareFile.cpp
    src/ui/MainWindow.cpp
    src/ui/FlasherWidget.cpp
    src/ui/SerialMonitorWidget.cpp
    src/ui/AboutDialog.cpp
)

set(HEADERS
    src/protocol/SLIPCodec.h
    src/protocol/ESP32Protocol.h
    src/serial/SerialConnection.h
    src/serial/SerialPortManager.h
    src/services/FlashingService.h
    src/models/SerialPort.h
    src/models/FirmwareFile.h
    src/models/FlashingState.h
    src/ui/MainWindow.h
    src/ui/FlasherWidget.h
    src/ui/SerialMonitorWidget.h
    src/ui/AboutDialog.h
)

# Resources
set(RESOURCES
    resources/resources.qrc
)

# Create executable
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${UDEV_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Widgets
    ${UDEV_LIBRARIES}
)

# Install
install(TARGETS ${PROJECT_NAME}
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Install desktop file and icons for Linux
install(FILES flatpak/com.fyrbyadditive.fame-smart-flasher.desktop
    DESTINATION share/applications
)
install(FILES resources/icons/app-icon.svg
    DESTINATION share/icons/hicolor/scalable/apps
    RENAME com.fyrbyadditive.fame-smart-flasher.svg
)
