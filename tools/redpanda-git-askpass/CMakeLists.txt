if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(redpanda-git-askpass MACOSX_BUNDLE main.cpp)
else()
    add_executable(redpanda-git-askpass MACOSX_BUNDLE main.cpp)
endif()

target_ui_classes(redpanda-git-askpass dialog)

target_compile_definitions(redpanda-git-askpass PRIVATE ${GLOBAL_COMPILE_DEFINITIONS})

target_link_libraries(redpanda-git-askpass PRIVATE
    Qt::Core
    Qt::Gui
    Qt::Widgets)

install(TARGETS redpanda-git-askpass
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
