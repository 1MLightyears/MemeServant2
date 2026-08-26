cmake_minimum_required(VERSION 3.21)

project(MemeServant2 VERSION 0.1.0 LANGUAGES CXX)

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

find_package(Qt6 6.11 REQUIRED COMPONENTS Concurrent Network Sql Widgets)

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
    src/thumbnail/thumbnaillistener.cpp
    src/app/appcontroller.cpp
    src/ui/settingswindow.cpp
    src/ui/capturetoast.cpp
    src/ui/quickbar.cpp
    src/ui/nicknameeditdialog.cpp
    src/ui/previewpopup.cpp
)

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
        COMMENT "正在部署Qt 6.11.2运行库到构建目录"
        VERBATIM
    )
endif()

target_include_directories(MemeServant2 PRIVATE src)
target_link_libraries(MemeServant2 PRIVATE
    Qt6::Concurrent
    Qt6::Network
    Qt6::Sql
    Qt6::Widgets
)

if(WIN32)
    target_link_libraries(MemeServant2 PRIVATE advapi32 dwmapi gdi32 user32)
endif()
