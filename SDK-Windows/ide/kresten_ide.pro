QT += widgets

CONFIG += c++17

TARGET = kresten_ide

SOURCES += \
    src/main.cpp \
    src/mainwindow.cpp \
    src/projecttree.cpp \
    src/codeeditor.cpp \
    src/buildoutput.cpp \
    src/projectmanager.cpp \
    src/settingsdialog.cpp \
    src/newprojectwizard.cpp

HEADERS += \
    src/mainwindow.h \
    src/projecttree.h \
    src/codeeditor.h \
    src/buildoutput.h \
    src/projectmanager.h \
    src/settingsdialog.h \
    src/newprojectwizard.h

FORMS += \
    ui/mainwindow.ui \
    ui/settingsdialog.ui \
    ui/newprojectwizard.ui

RESOURCES += \
    resources/icons.qrc

# Installation
target.path = $$[QT_INSTALL_EXAMPLES]/kresten_ide
INSTALLS += target