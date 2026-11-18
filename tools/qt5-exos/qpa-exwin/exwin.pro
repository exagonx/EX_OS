# The platform plugin of EX-OS: the windows of ExWin (tools/qt5-exos/qpa-exwin,
# copied into src/plugins/platforms/exwin by tools/qt5-exos/applica.py)
TARGET = qexwin

QT += \
    core-private gui-private \
    eventdispatcher_support-private fontdatabase_support-private

DEFINES += QT_NO_FOREACH

SOURCES = main.cpp qexwin.cpp
HEADERS = qexwin.h
OTHER_FILES += exwin.json

qtConfig(freetype): QMAKE_USE_PRIVATE += freetype

PLUGIN_TYPE = platforms
PLUGIN_CLASS_NAME = QExwinIntegrationPlugin
!equals(TARGET, $$QT_DEFAULT_QPA_PLUGIN): PLUGIN_EXTENDS = -
load(qt_plugin)
