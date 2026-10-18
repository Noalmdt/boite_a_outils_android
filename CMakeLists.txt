cmake_minimum_required(VERSION 3.16)

# La version est transmise au code (APP_VERSION) : c'est le SEUL endroit à modifier
# pour publier une nouvelle version.
project(boite_a_outils_android VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 6 uniquement (QProcess::finished, setTransferTimeout, QtConcurrent::run...)
find_package(QT NAMES Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network Concurrent)

add_subdirectory(QSimpleUpdater)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        webhookmanager.h
        webhookmanager.cpp
)

qt_add_executable(boite_a_outils_android
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_compile_definitions(boite_a_outils_android PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(boite_a_outils_android PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Concurrent
    QSimpleUpdater
)

set_target_properties(boite_a_outils_android PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS boite_a_outils_android
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(boite_a_outils_android)
