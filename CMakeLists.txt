# SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
# SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

project(beamerpresenter
    VERSION 0.2.6
    DESCRIPTION "Multi-screen PDF presentation viewer"
    HOMEPAGE_URL "https://github.com/stiglers-eponym/BeamerPresenter"
    LANGUAGES CXX)
set(version_suffix "")

# Configure compilation basics
set(CMAKE_CONFIGURATION_TYPES Release Debug)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define available build/config types
set(QT_VERSION_MAJOR 6 CACHE STRING "Qt major version")
# Minor Qt version is required for defining dependencies when packaging
set(QT_VERSION_MINOR 7 CACHE STRING "Qt minor version")

# Check if the required Qt libraries are available
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Gui REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Multimedia REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS MultimediaWidgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Xml REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Svg REQUIRED)


# Options concerning the installation
option(GENERATE_MANPAGES "Generate man pages and include them in installation" ON)
option(INSTALL_LICENSE "Include license file in installation" ON)
option(MARK_AS_SMALL "Mark package as small (set ON when using minimal MuPDF)" OFF)

# Options concerning the PDF engine
option(USE_QTPDF "Include QtPDF (requires Qt >= 6.3 or Qt >= 5.10)" OFF)
option(USE_MUPDF "Include MuPDF" ON)
option(USE_POPPLER "Include Poppler" OFF)
option(USE_EXTERNAL_RENDERER "Include option to use external renderer" OFF)
option(USE_WEBCAMS "Allow cameras as video source (Qt 6 only)" ON)
option(LINK_MUPDF_THIRD "Link to mupdf-third (only relevant when using MuPDF)" ON)
option(LINK_MUJS "Link to mujs (only relevant when using MuPDF)" OFF)
option(LINK_GUMBO "Link to gumbo-parser, should be on when using MuPDF >= 1.18" ON)
option(LINK_TESSERACT "Link to tesseract (only relevant when using MuPDF" OFF)
option(MUPDF_USE_SYSTEM_LIBS "MuPDF uses system libraries that need to be included. This is the default when statically linking to MuPDF." ON)
option(SUPPRESS_MUPDF_WARNINGS "Suppress warnings from MuPDF while loading the document pages" OFF)

option(CHECK_CLANG_TIDY "Run clang-tidy when compiling" OFF)
if (CHECK_CLANG_TIDY)
    set(CMAKE_CXX_CLANG_TIDY "clang-tidy;-checks=-*,clang-analyzer-*,-clang-analyzer-cplusplus*,cppcoreguidelines-*")
endif()

# Warnings and errors based on the selection of PDF engines
if ((NOT USE_MUPDF) AND (NOT USE_POPPLER))
    if (USE_QTPDF)
        message(WARNING "Only Qt PDF included as PDF engine, features will be very limited")
    else()
        message(FATAL_ERROR "No PDF engine provided")
    endif()
elseif (USE_MUPDF AND USE_POPPLER)
    message(WARNING "MuPDF and Poppler included, Poppler will (probably) not work for some documents")
endif()

if (USE_QTPDF)
    # This is probably not required in 5.14 > Qt >= 5.10.
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Pdf REQUIRED)
endif()


# Define some default paths
include(GNUInstallDirs)

# APPID_NAME is mainly relevant for flatpak where icon and desktop files
# should follow a certain naming convention.
set(APPID_NAME "beamerpresenter" CACHE STRING "file name without file type extension of app icon and .desktop file")

set(DEFAULT_ICON_PATH
    "${CMAKE_INSTALL_DATAROOTDIR}/beamerpresenter/icons"
    CACHE STRING
    "Directory for storing icons used by the programm")
GNUInstallDirs_get_absolute_install_dir(ABS_ICON_PATH DEFAULT_ICON_PATH DATAROOTDIR)

set(APPICON_PATH
    "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
    CACHE STRING
    "Directory for storing the app icon")
GNUInstallDirs_get_absolute_install_dir(ABS_APPICON_PATH APPICON_PATH DATAROOTDIR)
set(ABS_APPICON_PATH "${ABS_APPICON_PATH}/${APPID_NAME}.svg")

set(GUI_CONFIG_PATH
    "${CMAKE_INSTALL_SYSCONFDIR}/xdg/beamerpresenter"
    CACHE STRING
    "Configuration directory")
GNUInstallDirs_get_absolute_install_dir(ABS_GUI_CONFIG_PATH GUI_CONFIG_PATH SYSCONFDIR)
set(ABS_GUI_CONFIG_PATH "${ABS_GUI_CONFIG_PATH}/gui.json")

set(DOC_PATH
    "${CMAKE_INSTALL_DOCDIR}"
    CACHE STRING
    "Directory for storing the documentation and example configurations")
GNUInstallDirs_get_absolute_install_dir(ABS_DOC_PATH DOC_PATH DOCDIR)

set(TRANSLATION_PATH
    "${CMAKE_INSTALL_DATAROOTDIR}/locale"
    CACHE STRING
    "Directory for storing internationalization/translation files")
GNUInstallDirs_get_absolute_install_dir(ABS_TRANSLATION_PATH TRANSLATION_PATH DATAROOTDIR)
set(ABS_TRANSLATION_PATH "${ABS_TRANSLATION_PATH}/")

# Metainfo file is required by flathub
option(INSTALL_METAINFO "install %{id}.metainfo.xml in .../share/metainfo as required by flathub" OFF)
if (INSTALL_METAINFO)
    install(FILES
        "packaging/io.github.beamerpresenter.beamerpresenter.metainfo.xml"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo"
        RENAME "${APPID_NAME}.metainfo.xml"
        OPTIONAL)
endif()


# Set app version string
set(APP_VERSION "${beamerpresenter_VERSION}${version_suffix}")
option(GIT_VERSION "include git commit count in version" ON)
if (GIT_VERSION AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND git -C ${PROJECT_SOURCE_DIR} rev-list --count HEAD
        COMMAND head -c-1
        TIMEOUT 1
        OUTPUT_VARIABLE APP_GIT_COMMITS)
    execute_process(
        COMMAND git -C ${PROJECT_SOURCE_DIR} rev-parse --short HEAD
        COMMAND head -c-1
        TIMEOUT 1
        OUTPUT_VARIABLE APP_GIT_HASH)
    message(NOTICE "Set version: ${APP_VERSION}-${APP_GIT_COMMITS}.${APP_GIT_HASH}")
    if (APP_GIT_COMMITS AND APP_GIT_HASH)
        set(APP_VERSION "${APP_VERSION}-${APP_GIT_COMMITS}.${APP_GIT_HASH}")
    endif()
endif()

# Linker flags for debugging
set(LFLAGS "${LFLAGS} $<$<CONFIG:Debug>:-rdynamic>>")

# Add subdirectory containing C++ sources. This defines target beamerpresenter.
add_subdirectory(src)

# Compiler definitions
target_compile_definitions(beamerpresenter PUBLIC
        $<$<CONFIG:Debug>:QT_DEBUG>
        $<$<CONFIG:Release>:QT_NO_DEBUG_OUTPUT>
        $<$<CONFIG:Release>:QT_NO_DEBUG>
        $<$<CONFIG:Debug>:QT_DEPRECATED_WARNINGS>
        $<$<CONFIG:Debug>:FITZ_DEBUG_LOCKING>
    )

# Translations
option(USE_TRANSLATIONS "Enable translations" ON)
if (USE_TRANSLATIONS)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS LinguistTools REQUIRED)
    file(GLOB ts_files translations/*.ts)
    if (${QT_VERSION_MAJOR} EQUAL 5)
        qt5_add_translation(qm_files ${ts_files})
        add_custom_target(translations ALL DEPENDS ${qm_files})
    else()
        qt6_add_lupdate(beamerpresenter TS_FILE ${ts_files})
        qt6_add_lrelease(beamerpresenter TS_FILES ${ts_files} QM_FILES_OUTPUT_VARIABLE qm_files)
    endif()
    foreach (filename IN LISTS qm_files)
        string(REGEX REPLACE "^.*/([^/]*)\\.qm$" "\\1" language ${filename})
        install(FILES ${filename} DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/locale/${language}/LC_MESSAGES/" RENAME "beamerpresenter.qm")
    endforeach()
endif()

# License
if (INSTALL_LICENSE)
    install(FILES LICENSE LICENSES/GPL-3.0-or-later.txt LICENSES/AGPL-3.0-or-later.txt DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/beamerpresenter/")
endif()

# Add other subdirectories.
add_subdirectory(config)
add_subdirectory(share)
if (GENERATE_MANPAGES)
    add_subdirectory(man)
endif()


# Packaging
set(PACKAGE_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_CONTACT "Valentin Bruch <software@vbruch.eu>")
set(CPACK_PACKAGE_VERSION "${APP_VERSION}-qt${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}")
if (USE_MUPDF AND USE_POPPLER)
    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}-full")
    set(CONFLICTING_PACKAGES "${PROJECT_NAME}-mupdf, ${PROJECT_NAME}-mupdf-small, ${PROJECT_NAME}-poppler, ${PROJECT_NAME}-qtpdf")
elseif (USE_MUPDF)
    if (MARK_AS_SMALL)
        set(CPACK_PACKAGE_NAME "${PROJECT_NAME}-mupdf-small")
        set(CONFLICTING_PACKAGES "${PROJECT_NAME}-full, ${PROJECT_NAME}-mupdf, ${PROJECT_NAME}-poppler, ${PROJECT_NAME}-qtpdf")
    else()
        set(CPACK_PACKAGE_NAME "${PROJECT_NAME}-mupdf")
        set(CONFLICTING_PACKAGES "${PROJECT_NAME}-full, ${PROJECT_NAME}-mupdf-small, ${PROJECT_NAME}-poppler, ${PROJECT_NAME}-qtpdf")
    endif()
elseif(USE_POPPLER)
    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}-poppler")
    set(CONFLICTING_PACKAGES "${PROJECT_NAME}-full, ${PROJECT_NAME}-mupdf-small, ${PROJECT_NAME}-mupdf, ${PROJECT_NAME}-qtpdf")
elseif(USE_QTPDF)
    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}-qtpdf")
    set(CONFLICTING_PACKAGES "${PROJECT_NAME}-full, ${PROJECT_NAME}-mupdf-small, ${PROJECT_NAME}-mupdf, ${PROJECT_NAME}-poppler")
endif()
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-qt${QT_VERSION_MAJOR}-${PACKAGE_ARCH}")
set(CPACK_PACKAGE_DESCRIPTION_FILE "${PROJECT_SOURCE_DIR}/packaging/package_description")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)

math(EXPR QT_VERSION_MINOR_PLUS "${QT_VERSION_MINOR} + 1")

if (CPACK_GENERATOR MATCHES "DEB")
    set(UBUNTU_VERSION "24.04" CACHE STRING "Ubuntu version, used for dependencies")
    # configure debian package (tested in Ubuntu 20.04, 22.04, and 24.04)
    #
    # Required packages for building:
    # * cmake
    # * git (only when building mainline version)
    # * zlib1g-dev
    # * qtmultimedia5-dev or qt6-multimedia-dev
    # * libqt5svg5-dev or libqt6svg6-dev (Ubuntu ≤24.04) or qt6-svg-dev (Ubuntu ≥24.10)
    # * qttools5-dev or (qt6-tools-dev and qt6-tools-dev-tools)
    # * libqt6opengl6-dev (Qt 6 only)
    # * libgl1-mesa-dev (Qt 6 only)
    # For poppler:
    # * libpoppler-qt5-dev or libpoppler-qt6-dev
    # For MuPDF:
    # * libmupdf-dev
    # * libfreetype-dev (Ubuntu <25.04)
    # * libharfbuzz-dev (Ubuntu <25.04)
    # * libjpeg-dev (Ubuntu <25.04)
    # * libopenjp2-7-dev (Ubuntu <25.04)
    # * libjbig2dec0-dev (Ubuntu <25.04)
    # * libgumbo-dev (Ubuntu <25.04, for MuPDF >=1.18, probably not required for version <=1.17)
    # * libmujs-dev (only Ubuntu 21.10 to 24.04)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "zlib1g (>= 1:1.2.11)")
    set(QT_MEDIA_PACKAGE "libqt${QT_VERSION_MAJOR}multimediawidgets${QT_VERSION_MAJOR}")
    set(QT_SVG_PACKAGE "libqt${QT_VERSION_MAJOR}svg${QT_VERSION_MAJOR}")
    set(QT_VERSION_LOWER "(>= ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR})")
    set(QT_VERSION_UPPER "(<< ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR_PLUS})")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, ${QT_MEDIA_PACKAGE} ${QT_VERSION_LOWER}, ${QT_MEDIA_PACKAGE} ${QT_VERSION_UPPER}, ${QT_SVG_PACKAGE} ${QT_VERSION_LOWER}, ${QT_SVG_PACKAGE} ${QT_VERSION_UPPER}")
    if (USE_MUPDF)
        # Distinguish Ubuntu ≥25.04 (dynamically linked MuPDF) and
        # earlier versions (statically linked MuPDF)
        if ("${UBUNTU_VERSION}" GREATER_EQUAL "25.04")
            set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libmupdf25.1")
        else()
            set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libfreetype6, libharfbuzz0b, libjpeg8, libopenjp2-7, libjbig2dec0")
            if (LINK_GUMBO)
                if ("${UBUNTU_VERSION}" GREATER_EQUAL "24.04")
                    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libgumbo2")
                else()
                    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libgumbo1")
                endif()
            endif()
            if (LINK_MUJS)
                if ("${UBUNTU_VERSION}" GREATER_EQUAL "23.10")
                    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libmujs3")
                else()
                    set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libmujs1")
                endif()
            endif()
        endif()
    endif()
    if (USE_POPPLER)
        if ("${UBUNTU_VERSION}" GREATER_EQUAL "24.04")
            if ("${QT_VERSION_MAJOR}" EQUAL "5")
                set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libpoppler-qt5-1t64")
            elseif ("${QT_VERSION_MAJOR}" EQUAL "6")
                set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libpoppler-qt6-3t64")
            endif()
        else()
            set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, libpoppler-qt${QT_VERSION_MAJOR}-1")
        endif()
    endif()
    if (USE_QTPDF)
        set(QT_PDF_PACKAGE "libqt${QT_VERSION_MAJOR}pdf${QT_VERSION_MAJOR}")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS "${CPACK_DEBIAN_PACKAGE_DEPENDS}, ${QT_PDF_PACKAGE} ${QT_VERSION_LOWER}, ${QT_PDF_PACKAGE} ${QT_VERSION_UPPER}")
    endif()
    if ("${PACKAGE_ARCH}" STREQUAL "x86_64")
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    else()
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${PACKAGE_ARCH}")
    endif()
    set(CPACK_DEBIAN_PACKAGE_SECTION "science")
    set(CPACK_DEBIAN_PACKAGE_CONFLICTS "${CONFLICTING_PACKAGES}")
    if (("${QT_VERSION_MAJOR}" EQUAL "5") AND (USE_MUPDF OR USE_POPPLER))
        set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libqt5multimedia5-plugins")
    endif()
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSE" DESTINATION "${CMAKE_INSTALL_DOCDIR}" RENAME "copyright")
    install(FILES "${PROJECT_SOURCE_DIR}/LICENSES/AGPL-3.0-or-later.txt" "${PROJECT_SOURCE_DIR}/LICENSES/GPL-3.0-or-later.txt" DESTINATION "${CMAKE_INSTALL_DOCDIR}")
endif()

if (CPACK_GENERATOR MATCHES "RPM")
    # configure RPM package (hardly tested)
    # Building with MuPDF in Fedora requires the option LINK_TESSERACT=ON.
    set(CPACK_RPM_PACKAGE_ARCHITECTURE "${PACKAGE_ARCH}")
    set(CPACK_RPM_PACKAGE_LICENSE "AGPLv3+")

    # Required packages for building in fedora:
    # * cmake
    # * git (only when building mainline version)
    # * zlib-devel
    # * qt5-qtmultimedia-devel (qt6-qtmultimedia-devel for Qt 6)
    # * qt5-qtsvg-devel (qt6-qtsvg-devel for Qt 6)
    # * qt5-qttools-devel (qt6-qttools-devel for Qt 6)
    # * fedora-packager
    # For poppler:
    # * poppler-qt5-devel (poppler-qt6-devel for Qt 6)
    # For MuPDF:
    # * mupdf-devel
    # * freetype-devel
    # * harfbuzz-devel
    # * libjpeg-turbo-devel
    # * openjpeg2-devel
    # * jbig2dec-devel
    # * gumbo-parser-devel
    # * tesseract-devel

    # Dependencies on libraries are automatically detected.
    # Uncomment the following lines to manually specify dependencies:

    #set(QT_VERSION_LOWER ">= ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR}.0")
    #set(QT_VERSION_UPPER "< ${QT_VERSION_MAJOR}.${QT_VERSION_MINOR_PLUS}.0")
    #set(CPACK_RPM_PACKAGE_REQUIRES "zlib, qt${QT_VERSION_MAJOR}-qtmultimedia ${QT_VERSION_LOWER}, qt${QT_VERSION_MAJOR}-qtmultimedia ${QT_VERSION_UPPER}")
    #if (USE_MUPDF)
    #    set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, freetype, harfbuzz, libjpeg-turbo, openjpeg2, tesseract")
    #    if (LINK_GUMBO)
    #        set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, gumbo-parser")
    #    endif()
    #    # for building mupdf: mupdf-devel
    #endif()
    #if (USE_POPPLER)
    #    set(CPACK_RPM_PACKAGE_REQUIRES "${CPACK_RPM_PACKAGE_REQUIRES}, poppler-qt${QT_VERSION_MAJOR}")
    #endif()
    set(CPACK_RPM_PACKAGE_CONFLICTS "${CONFLICTING_PACKAGES}")
endif()

include(CPack)
