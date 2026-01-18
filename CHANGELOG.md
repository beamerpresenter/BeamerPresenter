# Changelog
## 0.2.7 (upcoming)
* new: read speaker notes from pdfpc files
* new: flatpak builds with MuPDF

## 0.2.6
### new features
* flexible mapping of page numbers to slides allows adding empty slides and removing slides
* zoom into view using buttons, scroll wheel, or touchscreen
* new tool: drag view
* read overlays from JSON file created for pdfpc, intended for usage with Polylux
* allow manually setting view aspect ratio, effectively changing the default zoom
### improvements
* saving drawings: each page part gets own layer (only relevant for documents containing presentation and nodes side-by-side)
* loading drawings: improved handling of relative paths
### bug fixes
* laser pointers remaining visible
* thumbnail widget: focus current page
* wrong slide shown after fly transition
### internal
* replace integer values containing bit-wise flags by structs and QFlags

## 0.2.5
* embedded videos: play media files embedded in the PDF file (experimental)
* bug fixes (includes crash on multi-touch)
* page labels available in Qt PDF version
* define some fallback file paths relative to directory of the binary to allow for a portable installation
* thumbnail overview: highlight current page, enable touch screen scrolling
* added Italian translation (thanks to albanobattistella!)
* internal:
    * restructuring of handling media: The new setup is constructed for Qt 6. In the Qt 5 version only a limited set of features is available.
    * (hopefully) more robust parallelization and memory management
    * use clang-format

## 0.2.4
* pages listed in TOC always start a new slide and are not considered overlays
* page up and page down can be used to navigate pages also while showing TOC or thumbnails
* more navigation keyboard shortcuts in default configuration
* new widget: ToolWidget ("tools") showing tools currently connected to input devices
* improved search widget: now highlights search results
* saving/loading files: single gzipped XML file (.bpr/.xopp) can now contain information about multiple PDF files
* selection:
    * added delete button to selection rectangle
    * draw tools and hand tool (no tool) can be use to manipulate selection rectangle
* different installation path for translation files
* copy and paste to/from image formats:
    * paste only as picture (no erasing possible)
    * this adds the Qt SVG module as a dependency
* new feature: allow camera (webcam) output as input for videos
* internal: different drawing history, memory management, and stacking order mechanism
* added build instructions for fedora Linux (RPM) and MSYS2
* compatibility with Qt 6.4 - 6.6 (beta)
* compatibility with MuPDF 1.23
* various bug fixes

## 0.2.3
* select and manipulate drawn items
    * selection tools
    * clipboard (shared clipboard with other applications is experimental)
    * move, resize and rotate drawn objects
    * change tool for existing objects
* new widgets: analog clock and search
* new PDF engine: Qt PDF
* keyboard shortcuts work in all widgets by default
* compatibility with newer versions of MuPDF
* tablet input: handle more buttons of the stylus
* bug fixes

## 0.2.2
* shape selection for draw tools (including a shape recognizer)
* better layout and more icons for buttons
* new buttons to select properties like color, style, and shape of the currently used tool
* better handling of links in PDFs
* added German translation
* new build system: cmake instead of qmake
* various bug fixes

## 0.2.1
* use Qt 5 or Qt 6
* various bug fixes

## 0.2.0
* initial version of completely rewritten program
