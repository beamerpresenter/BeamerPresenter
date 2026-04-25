#!/usr/bin/bash
# Build for Ubuntu 20.04 / focal

set -e

# Adjust these parameters
: ${BACKEND:=mupdf}  # mupdf or poppler
: ${SOURCE_DIR:=BeamerPresenter}  # path to soure directory (git repository)
: ${BUILD_DIR:=build}  # path to build directory

# Install general dependencies
sudo apt install cmake g++-10 zlib1g-dev qtmultimedia5-dev libqt5svg5-dev qttools5-dev

if [[ "$BACKEND" == mupdf ]]; then
    sudo apt install libmupdf-dev libfreetype-dev libharfbuzz-dev freeglut3-dev libjpeg-dev libopenjp2-7-dev libjbig2dec0-dev libgumbo-dev libbrotli-dev
    cmake \
        -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE='Release' \
        -DGIT_VERSION=OFF \
        -DUSE_POPPLER=OFF \
        -DUSE_MUPDF=ON \
        -DUSE_QTPDF=OFF \
        -DUSE_EXTERNAL_RENDERER=OFF \
        -DUSE_TRANSLATIONS=ON \
        -DLINK_MUPDF_THIRD=ON \
        -DLINK_MUJS=OFF \
        -DLINK_GUMBO=OFF \
        -DQT_VERSION_MAJOR=5 \
        -DQT_VERSION_MINOR=12 \
        -DUBUNTU_VERSION=20.04 \
        -DCPACK_GENERATOR="DEB;" \
        -DCMAKE_INSTALL_PREFIX='/usr' \
        -DCMAKE_INSTALL_SYSCONFDIR='/etc'
elif [[ "$BACKEND" == poppler ]]; then
    sudo apt install libpoppler-qt5-dev
    cmake \
        -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE='Release' \
        -DGIT_VERSION=OFF \
        -DUSE_POPPLER=ON \
        -DUSE_MUPDF=OFF \
        -DUSE_QTPDF=OFF \
        -DUSE_EXTERNAL_RENDERER=OFF \
        -DUSE_TRANSLATIONS=ON \
        -DQT_VERSION_MAJOR=5 \
        -DQT_VERSION_MINOR=12 \
        -DUBUNTU_VERSION=20.04 \
        -DCPACK_GENERATOR="DEB;" \
        -DCMAKE_INSTALL_PREFIX='/usr' \
        -DCMAKE_INSTALL_SYSCONFDIR='/etc'
else
    echo "Invalid build backend"
    exit 1
fi

cmake --build "$BUILD_DIR"
cpack --config "${BUILD_DIR}/CPackConfig.cmake"
