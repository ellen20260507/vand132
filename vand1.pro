QT += core gui
QT += serialport widgets
QT += core gui widgets serialport sql charts
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
QMAKE_CXXFLAGS += -std=c++11 -D_GLIBCXX_USE_CXX11_ABI=0 -include new
QMAKE_CC = D:\QT5.14.2\Tools\mingw730_64\bin\gcc.exe
QMAKE_CXX = D:\QT5.14.2\Tools\mingw730_64\bin\g++.exe
QMAKE_LINK = D:\QT5.14.2\Tools\mingw730_64\bin\g++.exe
QMAKE_AR = D:\QT5.14.2\Tools\mingw730_64\bin\ar.exe

# OpenCV 3.4.8 配置（使用项目内 opencv 目录）
INCLUDEPATH += $$PWD/opencv/include
INCLUDEPATH += $$PWD/opencv/include/opencv
INCLUDEPATH += $$PWD/opencv/include/opencv2

LIBS += -L$$PWD/opencv/x64/mingw/lib
LIBS += -lopencv_core348 -lopencv_imgproc348 -lopencv_highgui348 \
        -lopencv_imgcodecs348 -lopencv_videoio348 -lopencv_objdetect348
# 原有配置
INCLUDEPATH += $$PWD/httpserver
include($$PWD/httpserver/httpserver.pri)
SOURCES += \
    configmanager.cpp \
    dbmanager.cpp \
    main.cpp \
    mainwindow.cpp \
    newdialog.cpp \
    myrequesthandler.cpp \
    pollconfig.cpp \
    serialworker.cpp \
    uistyle.cpp
HEADERS += \
    configmanager.h \
    dbmanager.h \
    mainwindow.h \
    newdialog.h \
    myrequesthandler.h \
    pollconfig.h \
    serialworker.h \
    uistyle.h \
    placement_new.h \
    placement_new_global.h
FORMS += \
    mainwindow.ui \
    newdialog.ui \
    myrequesthandler.ui

# 设置应用程序图标
RESOURCES += appicon.qrc

# 部署规则
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

# 部署到deploy_v2目录
win32: deploy_v2.path = deploy_v2/bin
win32:deploy_v2.files = $$OUT_PWD/release/$${TARGET}.exe
win32:deploy_v2_config.path = deploy_v2/config
win32:deploy_v2_config.files = config/*
win32:deploy_v2_static.path = deploy_v2/static
win32:deploy_v2_static.files = static/*
win32:deploy_v2_symbol.path = deploy_v2/symbol
win32:deploy_v2_symbol.files = symbol/*
win32:deploy_v2_scripts.path = deploy_v2
win32:deploy_v2_scripts.files = start.bat stop.bat readme.txt
INSTALLS += deploy_v2 deploy_v2_config deploy_v2_static deploy_v2_symbol deploy_v2_scripts
