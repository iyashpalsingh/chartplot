QT += core gui widgets charts

CONFIG += c++17

TARGET   = BatteryPlotter
TEMPLATE = app

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    excelreader.cpp \
    chartmanager.cpp

HEADERS += \
    mainwindow.h \
    excelreader.h \
    chartmanager.h

FORMS += \
    mainwindow.ui
