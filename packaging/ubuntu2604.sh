#!/usr/bin/bash
# Build for Ubuntu 26.04 / resolute

set -e

# Adjust these parameters
: ${QT_VERSION_MAJOR:=6}  # Qt version
: ${BACKEND:=mupdf}  # mupdf. poppler, or qtpdf
: ${SOURCE_DIR:=BeamerPresenter}  # path to soure directory (git repository)
: ${BUILD_DIR:=build}  # path to build directory

if [[ "$QT_VERSION_MAJOR" == 5 ]]; then
    QT_VERSION_MINOR=15
    # Install general dependencies
    sudo apt install cmake g++ zlib1g-dev qtmultimedia5-dev libqt5svg5-dev qttools5-dev
elif [[ "$QT_VERSION_MAJOR" == 6 ]]; then
    # Install general dependencies
    sudo apt install cmake g++ zlib1g-dev qt6-multimedia-dev qt6-svg-dev qt6-tools-dev
    QT_VERSION_MINOR=10
else
    echo "Invalid Qt version: $QT_VERSION_MAJOR"
    exit 1
fi

if [[ "$BACKEND" == mupdf ]]; then
    cmake \
        sudo apt install libmupdf-dev
        -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE='Release' \
        -DUBUNTU_VERSION="26.04" \
        -DGIT_VERSION=OFF \
        -DUSE_POPPLER=OFF \
        -DUSE_MUPDF=ON \
        -DUSE_QTPDF=OFF \
        -DUSE_EXTERNAL_RENDERER=OFF \
        -DUSE_TRANSLATIONS=ON \
        -DLINK_MUPDF_THIRD=OFF \
        -DLINK_MUJS=OFF \
        -DLINK_GUMBO=OFF \
        -DQT_VERSION_MAJOR=$QT_VERSION_MAJOR \
        -DQT_VERSION_MINOR=$QT_VERSION_MINOR \
        -DINSTALL_LICENSE=OFF \
        -DCPACK_GENERATOR='DEB;' \
        -DCMAKE_INSTALL_PREFIX='/usr' \
        -DCMAKE_INSTALL_SYSCONFDIR='/etc'
elif [[ "$BACKEND" == poppler ]]; then
    sudo apt install "libpoppler-qt${QT_VERSION_MAJOR}-dev"
    cmake \
        -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE='Release' \
        -DUBUNTU_VERSION="26.04" \
        -DGIT_VERSION=OFF \
        -DUSE_POPPLER=ON \
        -DUSE_MUPDF=OFF \
        -DUSE_QTPDF=OFF \
        -DUSE_EXTERNAL_RENDERER=OFF \
        -DUSE_TRANSLATIONS=ON \
        -DQT_VERSION_MAJOR=$QT_VERSION_MAJOR \
        -DQT_VERSION_MINOR=$QT_VERSION_MINOR \
        -DINSTALL_LICENSE=OFF \
        -DCPACK_GENERATOR='DEB;' \
        -DCMAKE_INSTALL_PREFIX='/usr' \
        -DCMAKE_INSTALL_SYSCONFDIR='/etc'
elif [[ "$BACKEND" == qtpdf ]]; then
    if [[ "$QT_VERSION_MAJOR" == 6 ]]; then
        sudo apt install qt6-pdf-dev
    elif [[ "$QT_VERSION_MAJOR" == 5 ]]; then
        sudo apt install qtpdf5-dev
    fi
    cmake \
        -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE='Release' \
        -DUBUNTU_VERSION="26.04" \
        -DGIT_VERSION=OFF \
        -DUSE_POPPLER=OFF \
        -DUSE_MUPDF=OFF \
        -DUSE_QTPDF=ON \
        -DUSE_EXTERNAL_RENDERER=OFF \
        -DUSE_TRANSLATIONS=ON \
        -DQT_VERSION_MAJOR=$QT_VERSION_MAJOR \
        -DQT_VERSION_MINOR=$QT_VERSION_MINOR \
        -DINSTALL_LICENSE=OFF \
        -DCPACK_GENERATOR='DEB;' \
        -DCMAKE_INSTALL_PREFIX='/usr' \
        -DCMAKE_INSTALL_SYSCONFDIR='/etc'
else
    echo "Invalid build backend"
    exit 1
fi

cmake --build "$BUILD_DIR"
cpack --config "${BUILD_DIR}/CPackConfig.cmake"
