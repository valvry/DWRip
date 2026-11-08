# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Didik DW and contributors.
cmake_minimum_required(VERSION 3.16)
project(dwrip_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Qt is provisioned locally via aqtinstall into gui/.qt (see README.md).
# Pass -DCMAKE_PREFIX_PATH=<repo>/gui/.qt/6.8.3/gcc_64 when configuring,
# or rely on the auto-detection below.
if(NOT CMAKE_PREFIX_PATH)
    file(GLOB _qt_candidates "${CMAKE_CURRENT_SOURCE_DIR}/.qt/*/gcc_64")
    if(_qt_candidates)
        list(GET _qt_candidates 0 _qt_dir)
        set(CMAKE_PREFIX_PATH "${_qt_dir}")
        message(STATUS "Auto-detected Qt at ${_qt_dir}")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg)

# libm.so shim: some glibc packagings (e.g. Ubuntu after a 2.39 update) ship
# libm.so.6 but drop the linker dev symlink /usr/lib/.../libm.so, so `-lm`
# falls back to the static libm.a and the link fails (undefined
# _dl_x86_cpu_features). If no system libm.so exists, create a local shared
# shim and put it first on the link path. No-op on healthy systems.
if(NOT EXISTS "/usr/lib/x86_64-linux-gnu/libm.so"
        AND NOT EXISTS "/usr/lib64/libm.so" AND NOT EXISTS "/usr/lib/libm.so")
    execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libm.so.6
                    OUTPUT_VARIABLE _libm_real OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(_libm_real AND EXISTS "${_libm_real}")
        set(_libm_shim "${CMAKE_BINARY_DIR}/libm-shim")
        file(MAKE_DIRECTORY "${_libm_shim}")
        if(NOT EXISTS "${_libm_shim}/libm.so")
            file(CREATE_LINK "${_libm_real}" "${_libm_shim}/libm.so" SYMBOLIC)
        endif()
        link_directories(BEFORE "${_libm_shim}")
        message(STATUS "libm.so shim: ${_libm_shim} -> ${_libm_real}")
    endif()
endif()

add_executable(dwrip-gui
    src/main.cpp
    src/Theme.cpp
    src/MainWindow.cpp
    src/InfoPanel.cpp
    src/PropertiesPanel.cpp
    src/SettingsDialog.cpp
    src/InkSettingsDialog.cpp
    src/CurveEditor.cpp
    src/ChannelProfileDialog.cpp
    src/PrinterProfile.cpp
    src/ExportDialog.cpp
    src/InkCost.cpp
    src/CupsSpooler.cpp
    src/PrintQueueDialog.cpp
    src/Compositor.cpp
    src/SpotMask.cpp
    src/SpotMaskDialog.cpp
    src/ThumbnailStrip.cpp
    src/FlowLayout.cpp
    src/dwrip/DwripPanel.cpp
    src/canvas/PrintBedScene.cpp
    src/canvas/PrintBedView.cpp
    src/canvas/ImageItem.cpp
    src/dwrip/DwripRunner.cpp
    src/dwrip/DwripRun.cpp
    resources/icons.qrc
)

target_include_directories(dwrip-gui PRIVATE src)
target_link_libraries(dwrip-gui PRIVATE Qt6::Widgets Qt6::Svg)
