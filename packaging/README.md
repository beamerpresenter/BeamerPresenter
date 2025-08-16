# Building and packaging
This file describes how the packages included in the release are built.


## Arch or Manjaro
The Arch packages can be built with the following command:
Download the files `PKGBUILD_poppler` and `PKGBUILD_mupdf` from this directory.
The version with poppler as PDF engine and Qt 6 can be build using:
```sh
_qt_version_major=6 makepkg -p PKGBUILD_poppler
```
The packages for Qt 5 and with MuPDF can be built analogously.
You can install the newly created package using (for version 0.2.6):
```sh
sudo pacman -U beamerpresenter-poppler-qt6-0.2.6-1-x86_64.pkg.tar.zst
```
The file `PKGBUILD_git` can be used to test new and experimental features by selecting a git branch.


## Ubuntu
Install the build dependencies. Make sure you select all of the lines below which match your setup:
```sh
# All systems:
sudo apt install --no-install-recommends cmake zlib1g-dev libmupdf-dev libfreetype-dev libharfbuzz-dev libjpeg-dev libopenjp2-7-dev libjbig2dec0-dev
# When using Qt 5:
sudo apt install --no-install-recommends qtmultimedia5-dev qttools5-dev libpoppler-qt5-dev
# Ubuntu == 21.10 (only relevant for MuPDF):
sudo apt install --no-install-recommends libmujs-dev
# Ubuntu >= 22.04 (only relevant for MuPDF):
sudo apt install --no-install-recommends libmujs-dev libgumbo-dev
# When using Qt 6 (only Ubuntu >= 22.04):
sudo apt install --no-install-recommends qt6-multimedia-dev libqt6opengl6-dev libgl1-mesa-dev qt6-tools-dev qt6-tools-dev-tools qt6-l10n-tools
```

For version 0.2.6, the source can be [downloaded here](https://github.com/beamerpresenter/BeamerPresenter/archive/refs/tags/v0.2.6.tar.gz).
Check and unpack the download:
```sh
sha256sum -c - <<< "ed6b397ea4ec0afdf7a9bf8f3db272ae9999fab1d485a29b9f9b82615472b1fd v0.2.6.tar.gz"
tar -xvf v0.2.6.tar.gz
cd BeamerPresenter-0.2.6
```

Now configure the package using CMake. This requires the configuration of the Qt version (major and minor version), and the PDF engine (Poppler or MuPDF). The Qt minor version is only needed for version checking of dependencies.
For building BeamerPresenter with poppler in Ubuntu 24.04 with Qt6.4 use:
```sh
mkdir -p build_dir
cmake \
    -B build_dir \
    -DUBUNTU_VERSION='24.04' \
    -DCMAKE_BUILD_TYPE='Release' \
    -DGIT_VERSION=OFF \
    -DUSE_POPPLER=ON \
    -DUSE_MUPDF=OFF \
    -DUSE_QTPDF=OFF \
    -DUSE_EXTERNAL_RENDERER=OFF \
    -DLINK_MUPDF_THIRD=ON \
    -DLINK_MUJS=ON \
    -DLINK_GUMBO=ON \
    -DUSE_TRANSLATIONS=ON \
    -DQT_VERSION_MAJOR=6 \
    -DQT_VERSION_MINOR=4 \
    -DINSTALL_LICENSE=OFF \
    -DCPACK_GENERATOR='DEB;' \
    -DCMAKE_INSTALL_PREFIX='/usr' \
    -DCMAKE_INSTALL_SYSCONFDIR='/etc'
```
* to use MuPDF instead of Poppler: set `-DUSE_POPPLER=OFF` and `-DUSE_MUPDF=ON`
* in Ubuntu 21.10: set `-DQT_VERSION_MAJOR=5`, `-DQT_VERSION_MINOR=15`, and `-DLINK_MUPDF_THIRD=OFF`
* in Ubuntu 22.04:
    * when using Qt 5: set `-DQT_VERSION_MINOR=15`
    * when using Qt 6: set `-DQT_VERSION_MAJOR=6` and `-DQT_VERSION_MINOR=2`

Here the build directory was set to `build_dir`, which you can replace by any other empty directory.
We disable `INSTALL_LICENSE` because cpack uses a separate function for installing the license following the conventions of debian packages.

Now build and create the package:
```sh
cmake --build build_dir
cpack --config build_dir/CPackConfig.cmake
```

Now you can install the package:
```sh
sudo apt install ./beamerpresenter-poppler-0.2.6-qt6.4-x86_64.deb
```


## Flatpak
The flatpak package is built using github actions. The build can be reproduced locally using the file `io.github.beamerpresenter.yml`.


## Windows
see [Windows.md](Windows.md)
