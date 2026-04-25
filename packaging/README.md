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
Use one of the scripts provided for the LTS releases (ubuntu2004.sh - ubuntu2604.sh).
It is highly recommended to first read the script and select a configuration.
The script will install the dependencies and build the package.

Afterwards you can install the package:
```sh
sudo apt install ./beamerpresenter-*-x86_64.deb
```

For more details, check the scripts and the comments in CMakeLists.txt.


## Flatpak
The flatpak package is built using github actions. The build can be reproduced locally using the file `io.github.beamerpresenter.BeamerPresenter.*.yml`.


## Windows
see [Windows.md](Windows.md)
