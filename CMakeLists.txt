cmake_minimum_required(VERSION 3.21)

# ProjectConfig.cmake is the only editing source for product metadata; the two
# configure_file() calls below convert it into formats used by C++ and PowerShell.
include(cmake/ProjectConfig.cmake)

project(MemeServant2 VERSION ${MEMESERVANT2_VERSION} LANGUAGES CXX)

# appmetadata.h supplies compile-time constants to C++ targets.
configure_file(cmake/appmetadata.h.in "${CMAKE_BINARY_DIR}/generated/appmetadata.h" @ONLY)
# project_metadata.json is regenerated during configure and consumed by package.ps1.
configure_file(cmake/project_metadata.json.in "${CMAKE_BINARY_DIR}/project_metadata.json" @ONLY)

if(MSVC)
    # CMake 4.0 misdecodes the localized MSVC /showIncludes prefix on this toolchain,
    # which makes Ninja miss header dependencies and can link ABI-incompatible objects.
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 ${MEMESERVANT2_QT_MINIMUM} REQUIRED COMPONENTS Concurrent Network Sql Svg Widgets)

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE
        NAMES windeployqt
        HINTS "${Qt6_DIR}/../../../bin"
        REQUIRED)
endif()

qt_add_executable(MemeServant2 WIN32
    src/app/main.cpp
    src/app/application.cpp
    src/core/models.cpp
    src/core/nicknameutils.cpp
    src/core/appstrings.cpp
    src/core/searchengine.cpp
    src/storage/appconfig.cpp
    src/storage/databaseservice.cpp
    src/storage/importservice.cpp
    src/storage/logservice.cpp
    src/platforms/win/winsingleinstance.cpp
    src/platforms/win/winclipboard.cpp
    src/platforms/win/winglobalhotkey.cpp
    src/platforms/win/wincaretlocator.cpp
    src/platforms/win/winpaste.cpp
    src/platforms/win/wincredentialstore.cpp
    src/platforms/win/winarchive.cpp
    src/platforms/win/winautostart.cpp
    src/platforms/win/winwindoweffects.cpp
    src/ai/openaicompatibleprovider.cpp
    src/prompts/prompts.qrc
    src/resources/app.qrc
    src/thumbnail/thumbnaillistener.cpp
    src/app/appcontroller.cpp
    src/ui/settingswindow.cpp
    src/ui/appstyle.cpp
    src/ui/capturetoast.cpp
    src/ui/quickbar.cpp
    src/ui/nicknameeditdialog.cpp
    src/ui/previewpopup.cpp
)

if(WIN32)
    # Windows 资源编译器把 icon.ico 嵌入 exe；Qt 端从 qrc 读取同一个 icon.ico。
    enable_language(RC)
    set(MEMESERVANT2_ICON_ICO "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/icon.ico")
    configure_file(cmake/app.rc.in "${CMAKE_BINARY_DIR}/generated/app.rc" @ONLY)
    # rc.exe 的依赖不被 Ninja 追踪；显式声明 .ico 依赖，替换图标后强制重编资源。
    set_source_files_properties("${CMAKE_BINARY_DIR}/generated/app.rc" PROPERTIES
        OBJECT_DEPENDS "${MEMESERVANT2_ICON_ICO}")
    target_sources(MemeServant2 PRIVATE "${CMAKE_BINARY_DIR}/generated/app.rc")
endif()

if(WIN32)
    # 构建目录必须自带匹配版本的Qt运行库，避免从Conda等路径加载旧DLL。
    add_custom_command(TARGET MemeServant2 POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                $<IF:$<CONFIG:Debug>,--debug,--release>
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --no-quick-import
                "$<TARGET_FILE:MemeServant2>"
        COMMENT "正在部署Qt运行库到构建目录"
        VERBATIM
    )
endif()

if(MSVC)
    # 只有 Qt6Svg 延迟加载：它仅在设置窗口重绘图标时使用，代码里没有引用它的数据符号。
    # Qt6Network 不能延迟加载：openaicompatibleprovider.cpp 的 qobject_cast<QNetworkReply*>
    # 会引用 QNetworkReply::staticMetaObject 这个数据导入，MSVC 无法延迟加载数据符号（LNK1194），
    # 因此保持普通导入，启动时就映射。
    # 用 TARGET_FILE_NAME 而不是写死文件名，Debug 构建的 Qt6Svgd.dll 同样生效。
    target_link_options(MemeServant2 PRIVATE
        "/DELAYLOAD:$<TARGET_FILE_NAME:Qt6::Svg>"
    )
    target_link_libraries(MemeServant2 PRIVATE delayimp)
    # 延迟加载的模块不在普通导入表里，windeployqt 扫描不到，显式复制保证运行期能找到。
    add_custom_command(TARGET MemeServant2 POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:Qt6::Svg>"
                "$<TARGET_FILE_DIR:MemeServant2>"
        COMMENT "正在复制延迟加载的Qt模块"
        VERBATIM
    )
endif()

target_include_directories(MemeServant2 PRIVATE src)
target_include_directories(MemeServant2 PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(MemeServant2 PRIVATE
    Qt6::Concurrent
    Qt6::Network
    Qt6::Sql
    Qt6::Svg
    Qt6::Widgets
)

if(WIN32)
    target_link_libraries(MemeServant2 PRIVATE advapi32 dwmapi gdi32 user32)
endif()
