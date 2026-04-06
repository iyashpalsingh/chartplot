cmake_minimum_required(VERSION 3.19)
project(plotChart LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Charts)

qt_standard_project_setup()

qt_add_executable(plotChart
    WIN32 MACOSX_BUNDLE
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    excelreader.cpp
    excelreader.h
    chartmanager.cpp
    chartmanager.h
    xlsxreader.cpp
    xlsxreader.h
)

target_link_libraries(plotChart
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Charts
        z          # zlib — for XLSX/ZIP decompression
)

install(TARGETS plotChart
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET plotChart
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
