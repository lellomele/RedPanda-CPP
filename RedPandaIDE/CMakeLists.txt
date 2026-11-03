
# RedPandaIDE #

add_library(redpanda_path_identity STATIC src/pathidentity.cpp)
target_link_libraries(redpanda_path_identity PUBLIC Qt::Core)
target_include_directories(redpanda_path_identity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(redpanda_analysis_input STATIC src/analysis/sourceinput.cpp)
target_link_libraries(redpanda_analysis_input PUBLIC Qt::Core redpanda_path_identity)
target_include_directories(redpanda_analysis_input PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(RedPandaIDE WIN32)

target_qt_plain_cpp(RedPandaIDE
    src/analysis/parserconfiguration
    src/analysis/parserconfigurationcapture
    src/analysis/parsertoolchainprobe
    src/analysis/semanticsnapshot
    src/autolinkmanager
    src/diagnostics
    src/snippetsstorage
    src/buildartifacts
    src/diagnosticcatalog
    src/sessionstore
    src/editorcloseplan
    src/documentfilecontroller
    test/closeplanchecks
    test/documentstorechecks
    test/documentfilechecks
    test/compilerrequestchecks
    test/recoverychecks
    test/projectoperationchecks
    test/interfacechecks
    test/editorperformancechecks
    test/compilerworkflowchecks
    src/colorscheme
    src/customfileiconprovider
    src/projectoptions
    src/settings
    src/syntaxermanager
    src/systemconsts
    src/utils
    src/visithistorymanager
    # compiler
    src/compiler/compilerinfo
    # debugger
    src/debugger/dapprotocol
    src/debugger/gdbmiresultparser
    # parser
    src/parser/cppparser
    src/parser/cpppreprocessor
    src/parser/cpptokenizer
    src/parser/parserutils
    src/parser/statementmodel
    # problems
    src/problems/competitivecompenionhandler
    src/problems/freeprojectsetformat
    src/problems/ojproblemset
    src/problems/problemcasevalidator
    # settings
    src/settings/basesettings
    src/settings/codecompletionsettings
    src/settings/codeformattersettings
    src/settings/compilersetsettings
    src/settings/compilesettings
    src/settings/debuggersettings
    src/settings/dirsettings
    src/settings/editorsettings
    src/settings/environmentsettings
    src/settings/executorsettings
    src/settings/languagesettings
    src/settings/uisettings
    # utils
    src/utils/escape
    src/utils/file
    src/utils/font
    src/utils/os
    src/utils/parsearg
    src/utils/parser
    src/utils/parsemacros
    src/utils/pe
    src/utils/terminal
    src/utils/ui
    src/utils/colorcontrast
    )

target_moc_classes(RedPandaIDE
    src/caretlist
    src/codesnippetsmanager
    src/cpprefacter
    src/editor
    src/editormanager
    src/iconsmanager
    src/main
    src/resilientapplication
    src/lifecycle/applicationlifecycle
    src/sessionrecovery
    src/settingsdialog/environmentrecoverywidget
    src/project
    src/projecttemplate
    src/shortcutmanager
    src/symbolusagemanager
    src/thememanager
    src/todoparser
    src/toolsmanager
    # compiler
    src/compiler/compiler
    src/compiler/compilermanager
    src/compiler/executablerunner
    src/compiler/filecompiler
    src/compiler/nasmfilecompiler
    src/compiler/ojproblemcasesrunner
    src/compiler/projectcompiler
    src/compiler/runner
    src/compiler/stdincompiler
    # debugger
    src/debugger/dapdebugger
    src/debugger/debugger
    src/debugger/debuggermodels
    src/debugger/gdbmidebugger
    # parser
    src/parser/cppparser
    src/parser/statementmodel
    # problems
    src/problems/competitivecompenionhandler
    # reformatter
    src/reformatter/basereformatter
    src/reformatter/astyleformatter
    # settings dialog
    src/settingsdialog/settingswidget
    # widgets
    src/widgets/bookmarkmodel
    src/widgets/classbrowser
    src/widgets/classbrowsercontroller
    src/widgets/codecompletionlistview
    src/widgets/codecompletionpopup
    src/widgets/coloredit
    src/widgets/compileargumentswidget
    src/widgets/customdisablediconengine
    src/widgets/customfilesystemmodel
    src/widgets/darkfusionstyle
    src/widgets/editorstabwidget
    src/widgets/filenameeditdelegate
    src/widgets/functiontooltipwidget
    src/widgets/headercompletionpopup
    src/widgets/issuestable
    src/widgets/labelwithmenu
    src/widgets/lightfusionstyle
    src/widgets/linenumbertexteditor
    src/widgets/macroinfomodel
    src/widgets/ojproblemsetmodel
    src/widgets/qconsole
    src/widgets/qpatchedcombobox
    src/widgets/searchresultview
    src/widgets/shortcutinputedit
    src/widgets/shrinkabletabwidget)

target_ui_classes(RedPandaIDE
    src/mainwindow
    # settings dialog
    src/settingsdialog/compilerautolinkwidget
    src/settingsdialog/compilergaswidget
    src/settingsdialog/compilernasmwidget
    src/settingsdialog/compilersetdirectorieswidget
    src/settingsdialog/compilersetoptionwidget
    src/settingsdialog/debuggeneralwidget
    src/settingsdialog/editorautosavewidget
    src/settingsdialog/editorclipboardwidget
    src/settingsdialog/editorcodecompletionwidget
    src/settingsdialog/editorcolorschemewidget
    src/settingsdialog/editorcustomctypekeywords
    src/settingsdialog/editorfontwidget
    src/settingsdialog/editorgeneralwidget
    src/settingsdialog/editormiscwidget
    src/settingsdialog/editorsnippetwidget
    src/settingsdialog/editorsymbolcompletionwidget
    src/settingsdialog/editorsyntaxcheckwidget
    src/settingsdialog/editortooltipswidget
    src/settingsdialog/environmentappearancewidget
    src/settingsdialog/environmentfolderswidget
    src/settingsdialog/environmentperformancewidget
    src/settingsdialog/environmentprogramswidget
    src/settingsdialog/environmentshortcutwidget
    src/settingsdialog/executorgeneralwidget
    src/settingsdialog/executorproblemsetwidget
    src/settingsdialog/formattergeneralwidget
    src/settingsdialog/formatterpathwidget
    src/settingsdialog/languageasmgenerationwidget
    src/settingsdialog/projectcompileparamaterswidget
    src/settingsdialog/projectcompilerwidget
    src/settingsdialog/projectdirectorieswidget
    src/settingsdialog/projectdllhostwidget
    src/settingsdialog/projectfileswidget
    src/settingsdialog/projectgeneralwidget
    src/settingsdialog/projectmakefilewidget
    src/settingsdialog/projectoutputwidget
    src/settingsdialog/projectprecompilewidget
    src/settingsdialog/settingsdialog
    src/settingsdialog/toolsgeneralwidget
    # widgets
    src/widgets/aboutdialog
    src/widgets/choosethemedialog
    src/widgets/cpudialog
    src/widgets/custommakefileinfodialog
    src/widgets/editorfontdialog
    src/widgets/filepropertiesdialog
    src/widgets/infomessagebox
    src/widgets/newclassdialog
    src/widgets/newheaderdialog
    src/widgets/newprojectdialog
    src/widgets/newprojectunitdialog
    src/widgets/newtemplatedialog
    src/widgets/ojproblempropertywidget
    src/widgets/projectalreadyopendialog
    src/widgets/searchdialog
    src/widgets/searchinfiledialog
    src/widgets/signalmessagedialog)

target_compile_definitions(RedPandaIDE PRIVATE
    ${GLOBAL_COMPILE_DEFINITIONS}
    APP_NAME=\"${APP_NAME}\"
    LIBEXECDIR=\"${CMAKE_INSTALL_LIBEXECDIR}\"
    REDPANDA_CPP_VERSION=\"${REDPANDA_VERSION}\"
    ARCH_X86_64=1
    ARCH_X86=1)

if(APP_VERSION_PRE_RELEASE)
    target_compile_definitions(RedPandaIDE PRIVATE
        APP_VERSION_SUFFIX=\"${APP_VERSION_PRE_RELEASE}\")
endif()

target_include_directories(RedPandaIDE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(RedPandaIDE PRIVATE
    Qt::Core
    Qt::Concurrent
    Qt::Gui
    Qt::Network
    Qt::PrintSupport
    Qt::Svg
    Qt::Widgets
    Qt::Xml
    qsynedit
    redpanda_qt_utils)

#target_link_options(RedPandaIDE PRIVATE
#    $<$<CONFIG:Release>:-s -static>
#    $<$<CONFIG:MinSizeRel>:-s -static>
#)

#####################
# test-editor       #
#####################

add_executable(test-editor test/test-editor-main.cpp)

target_qt_plain_cpp(test-editor
    src/analysis/semanticsnapshot
    #settings
    src/settings/basesettings
    src/settings/dirsettings
    src/settings/editorsettings
    src/settings/codecompletionsettings

    src/parser/cppparser
    src/parser/cpppreprocessor
    src/parser/cpptokenizer
    src/parser/parserutils
    src/parser/statementmodel

    # utils
    src/utils/escape
    src/utils/file
    src/utils/font
    src/utils/os
    src/utils/parsearg
    src/utils/parsemacros
    src/utils/ui
    src/utils/colorcontrast

    src/colorscheme
    src/snippetsstorage
    src/buildartifacts
    src/diagnosticcatalog
    src/sessionstore
    src/diagnostics
    src/syntaxermanager
    src/iconsmanager
    src/systemconsts
    src/compiler/compilerinfo
    src/reformatter/basereformatter
    )

target_moc_classes(test-editor
    src/editor
    src/settingsdialog/settingswidget
    src/resilientapplication
    src/widgets/coloredit

    src/widgets/bookmarkmodel
    src/widgets/codecompletionlistview
    src/widgets/codecompletionpopup
    src/widgets/customdisablediconengine
    src/widgets/functiontooltipwidget
    src/widgets/headercompletionpopup

    src/symbolusagemanager
    src/codesnippetsmanager
    #test
    test/test_editor_base
    test/test_editor_persistence
    test/test_editor_symbol_completion
    test/test_settingswidget
    test/test_ide_services
    test/test_service_failure_paths
    src/widgets/searchresultview
    test/test_search_models
)
target_sources(test-editor PRIVATE defaultconfigs.qrc)
target_include_directories(test-editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(test-editor PRIVATE
        Qt::Core
        Qt::Gui
        Qt::PrintSupport
        Qt::Svg
        Qt::Test
        Qt::Widgets
        Qt::Xml
        qsynedit
        redpanda_qt_utils)

if (QT_FEATURE_static)
    qt_import_plugins(test-editor
        INCLUDE Qt::QOffscreenIntegrationPlugin)
endif()

add_custom_target(copy-ide-test-resources
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/test/resources"
        "${CMAKE_CURRENT_BINARY_DIR}/resources"
    COMMENT "Copying shared IDE test resources"
    VERBATIM)
add_dependencies(test-editor copy-ide-test-resources)

target_compile_definitions(test-editor PRIVATE
    ${GLOBAL_COMPILE_DEFINITIONS}
    APP_NAME=\"test-editor\"
    LIBEXECDIR=\"${CMAKE_INSTALL_LIBEXECDIR}\"
    REDPANDA_CPP_VERSION=\"${REDPANDA_VERSION}\"
    ARCH_X86_64=1
    ARCH_X86=1)

foreach(suite IN ITEMS search-models ide-services failure-paths settings smart-editing persistence)
    add_test(NAME test-editor-${suite} COMMAND test-editor --suite ${suite})
    set_tests_properties(test-editor-${suite} PROPERTIES TIMEOUT 120 LABELS "ide;functional" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

add_dependencies(all-test-targets test-editor)

add_executable(test-project-file-operations test/test_project_file_operations.cpp)
target_include_directories(test-project-file-operations PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-project-file-operations PRIVATE Qt::Core Qt::Test)
add_test(NAME test-project-file-operations COMMAND test-project-file-operations)
set_tests_properties(test-project-file-operations PROPERTIES TIMEOUT 60 LABELS "project;persistence;functional")
add_dependencies(all-test-targets test-project-file-operations)

#####################
# test-cppparser    #
#####################

add_executable(test-cppparser test/test-cppparser-main.cpp)

target_qt_plain_cpp(test-cppparser
    src/analysis/semanticmemory
    src/analysis/semanticpublication
    src/analysis/semanticpublicationbuilder
    src/analysis/enginesemanticadapter
    src/analysis/semanticsnapshot
    src/parser/cppparser
    src/parser/cpppreprocessor
    src/parser/cpptokenizer
    src/parser/parserutils
    src/parser/statementmodel
    )

target_moc_classes(test-cppparser
    src/widgets/classbrowser
    src/widgets/classbrowsercontroller
    #test
    test/test_parser_configuration_queue
    test/test_semanticpublication
    test/test_semanticsnapshot
    test/test_cppparser
    test/test_cpppreprocessor
    test/test_cpptokenizer
)
target_include_directories(test-cppparser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(test-cppparser PRIVATE
        Qt::Core
        Qt::Test
        qsynedit
        redpanda_qt_utils)

if (QT_FEATURE_static)
    qt_import_plugins(test-cppparser
        INCLUDE Qt::QOffscreenIntegrationPlugin)
endif()

add_dependencies(test-cppparser copy-ide-test-resources)

target_compile_definitions(test-cppparser PRIVATE
    ${GLOBAL_COMPILE_DEFINITIONS}
    APP_NAME=\"test-cppparser\"
    LIBEXECDIR=\"${CMAKE_INSTALL_LIBEXECDIR}\"
    REDPANDA_CPP_VERSION=\"${REDPANDA_VERSION}\"
    ARCH_X86_64=1
    ARCH_X86=1)

foreach(suite IN ITEMS preprocessor tokenizer parser parser-configuration-queue semantic-snapshot semantic-publication)
    add_test(NAME test-cppparser-${suite} COMMAND test-cppparser --suite ${suite})
    set_tests_properties(test-cppparser-${suite} PROPERTIES TIMEOUT 120 LABELS "parser;functional" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

add_dependencies(all-test-targets test-cppparser)

foreach(parser_consumer IN ITEMS RedPandaIDE test-editor test-cppparser)
    target_sources(${parser_consumer} PRIVATE
        src/parser/cppparserconfiguration.cpp
        src/parser/cppparseroutline.cpp
        src/analysis/outlinejob.cpp)
endforeach()
foreach(outline_consumer IN ITEMS RedPandaIDE test-editor)
    target_sources(${outline_consumer} PRIVATE
        src/analysis/enginesemanticadapter.cpp
        src/analysis/semanticmemory.cpp
        src/analysis/semanticpublication.cpp
        src/analysis/semanticpublicationbuilder.cpp)
endforeach()
target_sources(RedPandaIDE PRIVATE src/analysis/semanticretirement.cpp)
foreach(parser_test IN ITEMS test-editor test-cppparser)
    target_sources(${parser_test} PRIVATE
        src/analysis/parserconfiguration.cpp
        src/analysis/parsertoolchainprobe.cpp)
endforeach()
target_sources(test-cppparser PRIVATE src/utils/parsearg.cpp)

#####################
# Platform-specific #
#####################

if(WIN32)
    target_ui_classes(RedPandaIDE
        src/settingsdialog/environmentfileassociationwidget
        src/settingsdialog/projectversioninfowidget)

    target_link_libraries(RedPandaIDE PRIVATE
        advapi32
        dbghelp
        psapi
        shlwapi
        user32)
endif()

if(MACOS)
    target_link_libraries(RedPandaIDE PRIVATE Qt::GuiPrivate)
endif()

if(${FILESYSTEM_LAYOUT} STREQUAL "bundle")
    set_target_properties(RedPandaIDE PROPERTIES
        MACOSX_BUNDLE ON)
    set(MACOSX_BUNDLE_ICON_FILE RedPandaIDE.icns)
    set_source_files_properties(
        ${CMAKE_SOURCE_DIR}/platform/macos/RedPandaIDE.icns
        PROPERTIES
            MACOSX_PACKAGE_LOCATION Resources)
    target_sources(RedPandaIDE PRIVATE
        ${CMAKE_SOURCE_DIR}/platform/macos/RedPandaIDE.icns)
endif()

if(LINUX AND QT_FEATURE_static)
    qt_import_plugins(RedPandaIDE
        INCLUDE
            Qt::QComposePlatformInputContextPlugin
            Qt::QFcitx5PlatformInputContextPlugin
            Qt::QIbusPlatformInputContextPlugin)
endif()

if(${FILESYSTEM_LAYOUT} STREQUAL "hierarchy")
    install(
        TARGETS RedPandaIDE
        DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(${FILESYSTEM_LAYOUT} STREQUAL "flat")
    install(
        TARGETS RedPandaIDE
        DESTINATION .)
elseif(${FILESYSTEM_LAYOUT} STREQUAL "bundle")
    install(
        TARGETS RedPandaIDE
        BUNDLE DESTINATION .)
else()
    message(FATAL_ERROR "Unknown FILESYSTEM_LAYOUT: ${FILESYSTEM_LAYOUT}")
endif()

#########
# Paths #
#########

set (MAIN_PROGRAM_PATH_DEFS
    FILESYSTEM_LAYOUT=FILESYSTEM_LAYOUT_${FILESYSTEM_LAYOUT}
    FILESYSTEM_LAYOUT_hierarchy=1
    FILESYSTEM_LAYOUT_flat=2
    FILESYSTEM_LAYOUT_bundle=3
    MAKE_INTERFACE=MAKE_INTERFACE_${MAKE_INTERFACE}
    MAKE_INTERFACE_unix=1
    MAKE_INTERFACE_mingw32=2
    PORTABLE_CONFIG=PORTABLE_CONFIG_${PORTABLE_CONFIG}
    PORTABLE_CONFIG_oui=1
    PORTABLE_CONFIG_non=2
    PORTABLE_CONFIG_registry=3)

target_compile_definitions(RedPandaIDE PRIVATE ${MAIN_PROGRAM_PATH_DEFS})
target_compile_definitions(test-editor PRIVATE ${MAIN_PROGRAM_PATH_DEFS})

###################
# Feature options #
###################

if(LUA_ADDON)
    target_qt_plain_cpp(RedPandaIDE
        src/addon/luaapi
        src/addon/luaexecutor
        src/addon/luaruntime)

    target_compile_definitions(RedPandaIDE PRIVATE ENABLE_LUA_ADDON)

    target_link_libraries(RedPandaIDE PRIVATE lua)
endif()

if(SDCC)
    target_moc_classes(RedPandaIDE
        src/compiler/sdccfilecompiler
        src/compiler/sdccprojectcompiler)

    target_compile_definitions(RedPandaIDE PRIVATE ENABLE_SDCC)
endif()

if(VCS)
    target_qt_plain_cpp(RedPandaIDE
        src/settings/vcssettings)
    target_moc_classes(RedPandaIDE
        src/vcs/gitmanager
        src/vcs/gitrepository
        src/vcs/gitutils)

    target_ui_classes(RedPandaIDE
        src/settingsdialog/toolsgitwidget
        src/vcs/gitbranchdialog
        src/vcs/gitfetchdialog
        src/vcs/gitlogdialog
        src/vcs/gitmergedialog
        src/vcs/gitpulldialog
        src/vcs/gitpushdialog
        src/vcs/gitremotedialog
        src/vcs/gitresetdialog
        src/vcs/gituserconfigdialog)

    target_compile_definitions(RedPandaIDE PRIVATE ENABLE_VCS)
endif()

if(WINDOWS_PREFER_OPENCONSOLE)
    target_compile_definitions(RedPandaIDE PRIVATE WINDOWS_PREFER_OPENCONSOLE)
endif()

#############
# Resources #
#############

target_sources(RedPandaIDE PRIVATE
    fonts.qrc
    codes.qrc
    defaultconfigs.qrc
    icons.qrc
    projecttemplates.qrc)

# translation

target_embed_translations(RedPandaIDE
    TS_FILES
        translations/RedPandaIDE_it_IT.ts
        translations/RedPandaIDE_pt_BR.ts
        translations/RedPandaIDE_ru_RU.ts
        translations/RedPandaIDE_zh_CN.ts
        translations/RedPandaIDE_zh_TW.ts)

if(QT_FEATURE_static)
    set(qtbase_translation_filenames
        qtbase_it.qm
        qtbase_pt_BR.qm
        qtbase_ru_RU.qm
        qtbase_zh_CN.qm
        qtbase_zh_TW.qm)

    set(qtbase_translation_files)

    foreach(filename IN LISTS qtbase_translation_filenames)
        if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
            set(absolute_filename ${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/${filename})
        else()
            # there is no `QT5_INSTALL_TRANSLATIONS`. hard-code default path
            set(absolute_filename ${QT5_INSTALL_PREFIX}/translations/${filename})
        endif()
        if(EXISTS ${absolute_filename})
            list(APPEND qtbase_translation_files ${absolute_filename})
        endif()
    endforeach()

    if(qtbase_translation_files)
        message(STATUS "Found Qt base translation files: ${qtbase_translation_files}")
        target_embed_qtbase_translations(RedPandaIDE
            QM_FILES ${qtbase_translation_files})
    else()
        message(WARNING "Qt base translation files not found")
    endif()
endif()

# appereance

file(GLOB_RECURSE resource_iconset_files
    "resources/iconsets/*.json"
    "resources/iconsets/*.svg")
target_embed_resources(RedPandaIDE
    RESOURCE_NAME iconsets
    FILES ${resource_iconset_files})

file(GLOB resource_theme_files
    "resources/themes/*.json"
    "resources/themes/*.lua"
    "resources/themes/*.png")
target_embed_resources(RedPandaIDE
    RESOURCE_NAME theme
    FILES ${resource_theme_files})

file(GLOB resource_colorscheme_files
    "resources/colorschemes/*.scheme")
target_embed_resources(RedPandaIDE
    RESOURCE_NAME colorschemes
    FILES ${resource_colorscheme_files})

# app icon

if(WIN32)
    target_sources(RedPandaIDE PRIVATE
        resources/windows_icon.rc)
elseif(MACOS)
    set(app_icon ../platform/macos/RedPandaIDE.icns)
    set_source_files_properties(${app_icon}
        PROPERTIES
            MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(RedPandaIDE PRIVATE
        ${app_icon})
endif()

# Include Italian Qt standard dialogs even with a dynamically linked Qt build.
if(QT_VERSION_MAJOR GREATER_EQUAL 6 AND NOT QT_FEATURE_static)
    set(ITALIAN_QTBASE_TRANSLATION "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}/qtbase_it.qm")
    if(EXISTS "${ITALIAN_QTBASE_TRANSLATION}")
        target_embed_qtbase_translations(RedPandaIDE QM_FILES "${ITALIAN_QTBASE_TRANSLATION}")
    endif()
endif()

foreach(selector_case IN ITEMS missing empty unknown duplicate list-conflict)
    add_test(NAME test-runner-${selector_case}
        COMMAND ${CMAKE_COMMAND} -DCASE=${selector_case} -DTEST_PROGRAM=$<TARGET_FILE:test-editor>
            -P ${CMAKE_SOURCE_DIR}/tests/check_qtest_selector.cmake)
    set_tests_properties(test-runner-${selector_case} PROPERTIES TIMEOUT 15 LABELS "infrastructure" ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()

# MI argument encoding and an opt-in real GCC/GDB watch workflow.
add_executable(test-gdbmi-arguments test/test_gdbmi_arguments.cpp)
target_include_directories(test-gdbmi-arguments PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-gdbmi-arguments PRIVATE Qt::Core Qt::Test)
add_test(NAME test-gdbmi-arguments COMMAND test-gdbmi-arguments)
set_tests_properties(test-gdbmi-arguments PROPERTIES TIMEOUT 90 LABELS "debugger;functional")
add_dependencies(all-test-targets test-gdbmi-arguments)

# Isolated QtCore shutdown protocol tests; no production adapter dependency.
add_executable(test-application-lifecycle test/test-application-lifecycle-main.cpp)
target_moc_classes(test-application-lifecycle
    src/lifecycle/applicationlifecycle
    test/test_application_lifecycle)
target_include_directories(test-application-lifecycle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-application-lifecycle PRIVATE Qt::Core Qt::Test)
add_test(NAME test-application-lifecycle COMMAND test-application-lifecycle)
set_tests_properties(test-application-lifecycle PROPERTIES
    TIMEOUT 30 LABELS "lifecycle;unit")
add_dependencies(all-test-targets test-application-lifecycle)

add_executable(test-compile-configuration test/test_compile_configuration.cpp)
target_include_directories(test-compile-configuration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-compile-configuration PRIVATE Qt::Core Qt::Test redpanda_path_identity)
add_test(NAME test-compile-configuration COMMAND test-compile-configuration)
set_tests_properties(test-compile-configuration PROPERTIES TIMEOUT 30 LABELS "compiler;unit")
add_dependencies(all-test-targets test-compile-configuration)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    add_executable(test-analysis-source-input test/test_analysis_source_input.cpp)
    target_qt_plain_cpp(test-analysis-source-input src/analysis/sourceinput)
    target_include_directories(test-analysis-source-input PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(test-analysis-source-input PRIVATE Qt::Core Qt::Test)
    add_test(NAME test-analysis-source-input COMMAND test-analysis-source-input)
    set_tests_properties(test-analysis-source-input PROPERTIES TIMEOUT 30 LABELS "parser;unit")
    add_dependencies(all-test-targets test-analysis-source-input)
endif()

add_executable(test-path-identity test/test_path_identity.cpp)
target_include_directories(test-path-identity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-path-identity PRIVATE Qt::Core Qt::Test redpanda_path_identity)
add_test(NAME test-path-identity COMMAND test-path-identity)
set_tests_properties(test-path-identity PROPERTIES TIMEOUT 30 LABELS "persistence;unit")
add_dependencies(all-test-targets test-path-identity)

# Isolated reclamation contract; runtime publication adapters are migrated separately.
add_executable(test-semanticretirement test/test-semanticretirement-main.cpp)
target_qt_plain_cpp(test-semanticretirement
    src/analysis/semanticmemory src/analysis/semanticretirement
    src/analysis/semanticpublication src/analysis/semanticpublicationbuilder)
target_moc_classes(test-semanticretirement test/test_semanticretirement)
target_include_directories(test-semanticretirement PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-semanticretirement PRIVATE Qt::Core Qt::Widgets Qt::Test)
add_test(NAME test-semanticretirement COMMAND test-semanticretirement)
set_tests_properties(test-semanticretirement PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "analysis;memory;lifecycle;unit")
add_dependencies(all-test-targets test-semanticretirement)

add_executable(test-frozen-compilation test/test_frozen_compilation.cpp
    test/test_containedprocess.cpp src/utils/parsearg.cpp)
target_include_directories(test-frozen-compilation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/libs/redpanda_qt_utils)
target_link_libraries(test-frozen-compilation PRIVATE Qt::Core Qt::Test redpanda_path_identity)
add_test(NAME test-frozen-compilation COMMAND test-frozen-compilation)
set_tests_properties(test-frozen-compilation PROPERTIES TIMEOUT 30 LABELS "compiler;lifecycle;unit")
add_dependencies(all-test-targets test-frozen-compilation)

add_executable(test-containedprocess test/test-containedprocess-main.cpp test/test_containedprocess.cpp)
target_include_directories(test-containedprocess PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-containedprocess PRIVATE Qt::Core Qt::Test)
add_test(NAME test-containedprocess COMMAND test-containedprocess)
set_tests_properties(test-containedprocess PROPERTIES TIMEOUT 30 LABELS "compiler;lifecycle;unit")
add_dependencies(all-test-targets test-containedprocess)

add_executable(test-autolink-native-budget test/test_autolink_native_budget.cpp)
target_include_directories(test-autolink-native-budget PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test-autolink-native-budget PRIVATE Qt::Core Qt::Test redpanda_path_identity)
add_test(NAME test-autolink-native-budget COMMAND test-autolink-native-budget)
set_tests_properties(test-autolink-native-budget PROPERTIES TIMEOUT 30 LABELS "compiler;unit")
add_dependencies(all-test-targets test-autolink-native-budget)

foreach(consumer IN ITEMS RedPandaIDE test-editor test-cppparser test-project-file-operations test-analysis-source-input)
    if(TARGET ${consumer})
        target_link_libraries(${consumer} PRIVATE redpanda_path_identity)
    endif()
endforeach()
foreach(consumer IN ITEMS RedPandaIDE test-editor test-cppparser)
    target_link_libraries(${consumer} PRIVATE redpanda_analysis_input)
endforeach()
