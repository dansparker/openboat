# Qt tests of the app: model classes (tst_app) and the real UI driven by
# mouse clicks (tst_ui.qml). Run headless: QT_QPA_PLATFORM=offscreen.
find_package(Qt6 6.5 REQUIRED COMPONENTS Test QuickTest)

qt_add_executable(boat_app_tests tst_app.cpp)
target_link_libraries(boat_app_tests PRIVATE boat_ui Qt6::Test)
add_test(NAME boat_app_tests COMMAND boat_app_tests)

qt_add_executable(boat_ui_tests tst_ui.cpp)
target_link_libraries(boat_ui_tests PRIVATE boat_ui boat_uiplugin Qt6::QuickTest)
target_compile_definitions(boat_ui_tests PRIVATE QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME boat_ui_tests COMMAND boat_ui_tests)
