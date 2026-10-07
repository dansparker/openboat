find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick Sql)
qt_standard_project_setup(REQUIRES 6.5)

set(BOAT_QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/ChartView.qml
    qml/DataBar.qml
    qml/DataField.qml
    qml/AlarmBanner.qml
    qml/NavStrip.qml
    qml/NavPage.qml
    qml/SettingsPage.qml
    qml/TouchButton.qml)

set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

# Place every QML file at the module root (next to the generated qmldir),
# otherwise files in qml/ cannot see each other (Qt policy QTP0004).
foreach(qml_file IN LISTS BOAT_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_library(boat_ui STATIC)
qt_add_qml_module(boat_ui
    URI OpenBoat
    VERSION 1.0
    SOURCES
        src/alarm_sound.hpp
        src/alarm_sound.cpp
        src/boat_model.hpp
        src/boat_model.cpp
        src/settings.hpp
        src/settings.cpp
        src/route_store.hpp
        src/route_store.cpp
        src/mbtiles_provider.hpp
        src/mbtiles_provider.cpp
    QML_FILES
        ${BOAT_QML_FILES})

target_include_directories(boat_ui PUBLIC src)
target_link_libraries(boat_ui PUBLIC boat::core boat::nav boat::track Qt6::Quick Qt6::Sql)
boat_set_warnings(boat_ui)

# Alarm tone on the loudspeaker (optional: Qt Multimedia add-on module)
find_package(Qt6 6.5 QUIET COMPONENTS Multimedia)
if(TARGET Qt6::Multimedia)
    target_link_libraries(boat_ui PUBLIC Qt6::Multimedia)
    target_compile_definitions(boat_ui PRIVATE BOAT_HAVE_MULTIMEDIA)
    message(STATUS "OpenBoat: alarm sound on the loudspeaker enabled (Qt Multimedia)")
else()
    message(STATUS "OpenBoat: Qt Multimedia not found - alarm sound only via GPIO buzzer")
endif()

qt_add_executable(openboat src/main.cpp)
target_compile_definitions(openboat PRIVATE OPENBOAT_VERSION="${PROJECT_VERSION}")
target_link_libraries(openboat PRIVATE
    boat_ui
    boat_uiplugin
    boat::nmea0183
    boat::nmea2000
    boat::sim
    boat::buzzer)
boat_set_warnings(openboat)

set_target_properties(openboat PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON)

include(GNUInstallDirs)
install(TARGETS openboat
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
