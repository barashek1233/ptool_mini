cmake_minimum_required(VERSION 3.18)

project(ptool_mini VERSION 1.0.0)

set(PTOOL_MINI_VERSION_SUFFIX "rc10")
set(PTOOL_MINI_VERSION_FULL "${PROJECT_VERSION}-${PTOOL_MINI_VERSION_SUFFIX}")

include(FetchContent)

# Keep external deps minimal for ptool_mini build.
set(BUILD_TOOLS OFF CACHE BOOL "Disable tools in dependencies" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Disable tests in dependencies" FORCE)
set(BUILD_PACKAGES OFF CACHE BOOL "Disable packages in dependencies" FORCE)

FetchContent_Declare(cube_aw_flash
    GIT_REPOSITORY https://gitlab.aqsi.ru/aqc_embedded/tools/liballwinner-flashing-helper
    GIT_TAG dev
    GIT_SUBMODULES "")
FetchContent_Declare(sam-ba
    GIT_REPOSITORY https://gitlab.aqsi.ru/aqc_embedded/tools/sam-ba
    GIT_TAG master)
FetchContent_MakeAvailable(cube_aw_flash sam-ba)
FetchContent_GetProperties(sam-ba)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 COMPONENTS Core Network REQUIRED)

add_executable(ptool_mini
    main.cpp
    src/ConsoleAtmelFlasher.cpp
    src/CubeFlasher.cpp
    src/CubeFlasherManager.cpp
    src/CubeImageInfo.cpp
    src/DevNotifyQtStyle.cpp
    src/HardwareDetection.cpp
    src/LogMessageHandlers.cpp
    src/loggingCategories.cpp
    src/Mapping.cpp
    src/bootconfigparser.cpp
    src/ConfigManager.cpp
    src/allwinner/AllwinnerDevicesManager.cpp
    src/allwinner/ConsoleAllwinnerFlasher.cpp)

target_include_directories(ptool_mini PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/allwinner")

target_link_libraries(ptool_mini PRIVATE
    Qt5::Core
    Qt5::Network
    samba_objects
    cube_aw_flash)

add_dependencies(ptool_mini samba_core samba_conn_serial)

add_custom_command(TARGET ptool_mini
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -D CMAKE_INSTALL_PREFIX=. -P ${sam-ba_BINARY_DIR}/cmake_install.cmake)

target_compile_definitions(ptool_mini PRIVATE
    PTOOL_MINI_VERSION="${PTOOL_MINI_VERSION_FULL}")

target_compile_options(ptool_mini PRIVATE
    -Werror -Wno-error=cpp -Wno-unused-parameter)

install(TARGETS ptool_mini RUNTIME DESTINATION bin)

# udev-правила для доступа к USB/block устройствам без root
install(FILES packaging/99-ptool-mini.rules
    DESTINATION lib/udev/rules.d)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "ptool-mini")
set(CPACK_PACKAGE_VERSION "${PTOOL_MINI_VERSION_FULL}")
set(CPACK_PACKAGE_CONTACT "support@aqsi.ru")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ptool_mini — утилита прошивки устройств")
# libcap2-bin нужен для setcap в postinst; adduser — для addgroup.
# qml-module-qtquick2 — не зависимость libqt5quick5 (пакеты разделены в Debian/Ubuntu),
# а сам QML-плагин QtQuick, загружаемый QQmlEngine в рантайме. dpkg-shlibdeps видит
# только ELF-зависимости через линковщик и не может обнаружить эту зависимость сам —
# без неё sam-ba падает на любой команде с "Type ... unavailable" / "Unknown port 'serial'".
# Остальные зависимости (libqt5qml5, libfdt1, libconfig9 и т.п.) вычисляются
# автоматически dpkg-shlibdeps по реальным библиотекам, на которые слинкован бинарь.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcap2-bin, adduser (>= 3.11), qml-module-qtquick2")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/postrm")

include(CPack)
