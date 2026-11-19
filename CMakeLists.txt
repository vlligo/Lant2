cmake_minimum_required(VERSION 3.16)

project(Lant VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 QUIET COMPONENTS Core Gui Widgets Concurrent)
if(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets Concurrent)
    set(QT_VERSION_MAJOR 5)
else()
    set(QT_VERSION_MAJOR 6)
endif()

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    antfieldwidget.h
    diagonalcolordialog.cpp
    highwayanalysis.cpp
    antfieldwidget.cpp
        QInt64SpinBox.cpp
        QInt64SpinBox.h
        QPoint64.h
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Lant
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(Lant SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(Lant
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(Lant PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Concurrent
)

set_target_properties(Lant PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(Lant)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
    add_executable(highwayanalysis_test tests/highwayanalysis_test.cpp highwayanalysis.cpp)
    set_target_properties(highwayanalysis_test PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
    target_include_directories(highwayanalysis_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME highwayanalysis COMMAND highwayanalysis_test)

    add_executable(highwaywindow_test tests/highwaywindow_test.cpp mainwindow.cpp QInt64SpinBox.cpp
                   diagonalcolordialog.cpp antfieldwidget.cpp highwayanalysis.cpp)
    target_include_directories(highwaywindow_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(highwaywindow_test PRIVATE Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Concurrent Qt${QT_VERSION_MAJOR}::Test)
    add_test(NAME pinch_zoom COMMAND highwaywindow_test -platform offscreen pinch-zoom)
    add_test(NAME audit_regressions COMMAND highwaywindow_test -platform offscreen audit-fixes)
    add_test(NAME runs_and_rankings COMMAND highwaywindow_test -platform offscreen runs-and-rankings)
    foreach(rule LR RL LLR RRL LLLR LRLR L LLRR)
        add_test(NAME highwaywindow_${rule} COMMAND highwaywindow_test -platform offscreen ${rule})
    endforeach()
endif()
