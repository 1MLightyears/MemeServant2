# Single editing source for product/build-time metadata.
# CMakeLists.txt includes this file before project(), so these values configure
# both CMake targets and the generated appmetadata.h / project_metadata.json.
# Keep machine-specific tool paths out of this file.

set(MEMESERVANT2_DISPLAY_NAME "MemeServant2")
set(MEMESERVANT2_VERSION "1.0.1")
set(MEMESERVANT2_QT_MINIMUM "6.11")
set(MEMESERVANT2_PACKAGE_PLATFORM "windows-x64")
set(MEMESERVANT2_DB_SCHEMA_VERSION 1)
set(MEMESERVANT2_SINGLE_INSTANCE_MUTEX "Local\\MemeServant2-SingleInstance")

# configure_file performs no C string escaping, so pre-escape the mutex name for
# a C++ wide-string literal (one logical "\" becomes "\\" in appmetadata.h).
string(REPLACE "\\" "\\\\"
    MEMESERVANT2_SINGLE_INSTANCE_MUTEX_CPP
    "${MEMESERVANT2_SINGLE_INSTANCE_MUTEX}")
