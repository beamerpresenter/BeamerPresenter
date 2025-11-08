// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QKeySequence>
#include <QMultiMap>
#include <QObject>
#include <QPen>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <memory>

#include "src/config.h"
#include "src/drawing/tool.h"
#include "src/enumerates.h"

class PdfDocument;
class QCommandLineParser;
class QJsonObject;
class Master;

/// Class storing various preferences.
/// It should have only one instance, which is available globally through
/// the functions writable_preferences() and preferences().
/// Values get initialized by calling loadSettings().
class Preferences : public QObject
{
  Q_OBJECT

  /// Interface for configuration file. Only local configuration file is used.
  /// The system-wide configuration file is copied to a user space file, if
  /// that one does not exist.
  QSettings settings;
  /// Master object
  Master *master;

  friend Master *master() noexcept;
  friend int main(int argc, char *argv[]);

 public:
  enum GlobalFlag {
    /// Automatically change slides if the pdf defines a duration for the slide.
    AutoSlideChanges = 1 << 0,
    /// Log slide changes to standard output.
    LogSlideChanges = 1 << 1,
    /// Mute all multimedia content by default.
    MuteApplication = 1 << 2,
    /// Allow opening external links.
    OpenExternalLinks = 1 << 3,
    /// Finalize drawing paths
    FinalizeDrawnPaths = 1 << 4,
    /// Automatically load pdfpc JSON file.
    AutoloadPdfpc = 1 << 5,
  };
  Q_DECLARE_FLAGS(GlobalFlags, GlobalFlag);
  Q_FLAG(GlobalFlags);

  /* ********************** */
  /*  GLOBAL CONFIGURATION  */
  /* ********************** */

  // SETTINGS
  /// Path to GUI configuration file.
  QString gui_config_file;

  /// Path to manual (html formatted), required for help tab in settings.
  QString manual_file;

  /// Directory for icons
  QString icon_path;

#ifdef QT_DEBUG
  /// Flags for log and debug level.
  quint16 debug_level = NoLog;
#endif

  /// Global flags.
  GlobalFlags global_flags = AutoSlideChanges;

  /// Color for filling rectangles highlighting search results.
  QBrush search_highlighting_color{QColor(40, 100, 60, 100)};

  /// Duration of a slide in an animation, in ms.
  int slide_duration_animation = 50;

  // DRAWING
  /// Maximum number of steps in drawing history of currently visible slide.
  int history_length_visible_slides = 50;
  /// Maximum number of steps in drawing history of hidden slide.
  int history_length_hidden_slides = 20;
  /// Define how should drawings be assigned to overlays.
  OverlayDrawingMode overlay_mode = OverlayDrawingMode::Cumulative;

  // SHAPE RECOGNITION
  /// Parameter for sensitivity of line detectoin.
  /// Larger numbers mean that more strokes are recognized as lines.
  qreal line_sensitivity = 0.005;
  /// Sensitivity for detecting ellipsis.
  qreal ellipse_sensitivity = 0.05;
  /// Ellipses with relative deviation of the radii smaller than this number
  /// will be considered as circles.
  qreal ellipse_to_circle_snapping = 0.025;
  /// Slope (angle in radiants with small angle approximation) for snapping
  /// to horizontal or vertical direction for recognized shapes.
  qreal snap_angle = 0.05;
  /// Tolerance in angles of different sides of a rectangle (radiants).
  qreal rect_angle_tolerance = 0.3;
  /// Allowed distance between first and last point in rectangle relative to
  /// variance of point coordinates.
  qreal rect_closing_tolerance = 0.1;
  /// Scale for arrow tip size.
  qreal arrow_tip_scale = 1.;
  /// length of arrow tip relative to half width.
  qreal arrow_tip_ratio = 1.2;

  // SELECTION
  /// Minimal pen width for defining path shape. For thinner paths
  /// the shape is defined with this width.
  qreal path_min_selectable_width = 3.;
  /// Pen for drawing rectangle around selection.
  QPen selection_rect_pen{QPen(QColor(128, 128, 144, 128), 0.5, Qt::DotLine)};
  /// Pen for filling rectangle around selection.
  QBrush selection_rect_brush{QBrush(QColor(128, 128, 144, 32))};
  /// Size (in pt) of selection rectangle handles.
  qreal selection_rect_handle_size = 7;

  // RENDERING
  /// Threshold of page aspect ratio for splitting pages in notes and
  /// presentation.
  float page_part_threshold = 2.5;
  /// PagePart which represents the presentation.
  PagePart default_page_part = FullPage;
  /// Maximum image size in pixels.
  qreal max_image_size = 3e7;

#ifdef USE_MUPDF
  /// PDF engine (should be same as renderer except if renderer is external)
  PdfEngine pdf_engine = PdfEngine::MuPdf;
  /// Renderer used to convert PDF page to image.
  Renderer renderer = Renderer::MuPDF;
#elif defined(USE_POPPLER)
  /// PDF engine (should be same as renderer except if renderer is external)
  PdfEngine pdf_engine = PdfEngine::Poppler;
  /// Renderer used to convert PDF page to image.
  Renderer renderer = Renderer::Poppler;
#elif defined(USE_QTPDF)
  /// PDF engine (should be same as renderer except if renderer is external)
  PdfEngine pdf_engine = PdfEngine::QtPDF;
  /// Renderer used to convert PDF page to image.
  Renderer renderer = Renderer::QtPDF;
#endif
#ifdef USE_EXTERNAL_RENDERER
  /// Rendering command for external renderer.
  QString rendering_command;
  /// Arguments to rendering_command.
  QStringList rendering_arguments;
#endif

  /// Maximally allowed memory size in bytes.
  /// Negative numbers are interpreted as infinity.
  float max_memory = -1.;
  /// Maximally allowed number of pages in cache.
  /// Negative numbers are interpreted as infinity.
  int max_cache_pages = -1;

  // INTERACTION
  /// Touch screen gestures
  qreal max_zoom_increment = 1.2;
  /// Map key combinations to actions for global keyboard shortcuts.
  QMultiMap<QKeySequence, Action> key_actions{
      {Qt::Key_PageDown, Action::NextPage},
      {Qt::Key_PageUp, Action::PreviousPage},
      {Qt::Key_Right, Action::NextPage},
      {Qt::Key_Left, Action::PreviousPage},
      {Qt::Key_Space, Action::Update},
      {Qt::Key_Home, Action::FirstPage},
      {Qt::Key_End, Action::LastPage},
      {Qt::CTRL | Qt::Key_C, Action::CopyClipboard},
      {Qt::CTRL | Qt::Key_X, Action::CutClipboard},
      {Qt::CTRL | Qt::Key_V, Action::PasteClipboard},
      {Qt::CTRL | Qt::Key_Z, Action::UndoDrawing},
      {Qt::CTRL | Qt::Key_Y, Action::RedoDrawing},
      {Qt::Key_Delete, Action::RemoveSelectedItems},
      {Qt::CTRL | Qt::Key_S, Action::SaveDrawings},
      {Qt::CTRL | Qt::SHIFT | Qt::Key_S, Action::SaveDrawingsAs},
      {Qt::CTRL | Qt::Key_O, Action::LoadDrawings},
  };
  /** Map key combinations to tools. These tools are not owned by this.
   * Only when the program ends and preferences() is the last object to
   * be deleted, the remaining key_tools will be deleted in the destructor
   * of preferences().
   * Objects owning a tool which is listed here should always call
   * writable_preferences().removeKeyTool before deleting this tool.
   * */
  QMultiMap<QKeySequence, std::shared_ptr<Tool>> key_tools;

  /// Map gestures to actions
  QMultiMap<Gesture, Action> gesture_actions{
      {SwipeLeft, Action::NextPage},
      {SwipeRight, Action::PreviousPage},
      {SwipeUp, Action::NextPage},
      {SwipeDown, Action::PreviousPage},
  };

  /* ************************** */
  /*  DEFINED PER PRESENTATION  */
  /* ************************** */

  /// Map "presentation", "notes", ... to file names.
  /// This is needed to interpret GUI config.
  QMap<QString, QString> file_alias;
  /// Main document
  std::shared_ptr<const PdfDocument> document;
  /// Number of pages in main document
  int number_of_pages = 0;

  /* ******************************* */
  /*  VARIABLES WITHOUT FIXED VALUE  */
  /* ******************************* */

  /// Current page number in reference presentation view.
  int page = 0;
  /// Previous page, required during slide changes.
  int previous_page = 0;
  /// Current slide number in reference presentation view.
  int slide = 0;

  /// Target time of presentation. Set to zero if timer is not running.
  QDateTime target_time;
  /// Passed time. When timer is running, this is set to UINT_LEAST32_MAX.
  quint32 msecs_passed = 0;
  /// Total time for presentation.
  quint32 msecs_total = 0;

  /** Tools used for different input devices, owned by this.
   * This is only a QMultiMap to keep it sorted. The sorting is
   * important for the order in which pointing tools are drawn in
   * SlideView.
   * */
  QMultiMap<Tool::BasicTool, std::shared_ptr<Tool>> current_tools;

  /* ***************** */
  /*     FUNCTIONS     */
  /* ***************** */

  /// Load settings from default config file location (defined by Qt).
  Preferences(QObject *parent = nullptr);

  /// Load settings from given file.
  Preferences(const QString &file, QObject *parent = nullptr);

  /// Destructor. Deletes current_tools and key_tools.
  ~Preferences();

  /// Load settings from QSettings.
  void loadSettings();

  /// Load settings from command line parser.
  /// Usually called after loadSettings(), since it should overwrite
  /// settings from the configuration file.
  void loadFromParser(const QCommandLineParser &parser);
#ifdef QT_DEBUG
  /// Set debug flags depending on command line option.
  void loadDebugFromParser(const QCommandLineParser &parser);
#endif

  /// Convert given file identifier to URL. If a relative path is provided,
  /// first check relative to the directory of the presentation PDF.
  /// Remote URLs are only returned if external links are enabled.
  /// The returned URL may be empty or invalid.
  QUrl resolvePath(const QString &identifier) const noexcept;

  /// Get the current tool for a given input device or nullptr if there
  /// is no tool for this device. The tool remains owned by preferences().
  std::shared_ptr<Tool> currentTool(
      const Tool::InputDevices device) const noexcept;
  /// Remove (but don't delete) all occurences of tool in key_tools.
  /// This should be called by an object owning tool before it deletes tool.
  void removeKeyTool(std::shared_ptr<const Tool> tool,
                     const bool remove_from_settings);
  /// Change key sequence associated with tool from oldkeys to newkeys.
  /// oldkeys or newkeys may be left empty to add a new tool or remove
  /// an existing tool.
  void replaceKeyToolShortcut(const QKeySequence oldkeys,
                              const QKeySequence newkeys,
                              std::shared_ptr<Tool> tool);

  /// Connect an action to a key code. This does not remove existing
  /// actions connected to the same key code.
  void addKeyAction(const QKeySequence sequence, const Action action);
  /// Remove an action from a key sequence.
  void removeKeyAction(const QKeySequence sequence, const Action action);

  /// Interpret input as definition of tools and actions.
  /// Append these tools and actions to the given lists.
  /// This function is used for reading tools/actions for
  /// key shortcuts.
  static void parseActionsTools(
      const QVariant &input, QList<Action> &actions,
      QList<std::shared_ptr<Tool>> &tools,
      const Tool::InputDevices default_device = Tool::NoDevice);

  /// Show error message in dialog in front of main window.
  void showErrorMessage(const QString &title, const QString &text) const;

  /// Set new GUI configuration file.
  bool setGuiConfigFile(const QString &file);

  /// Disconnect tools from the given device.
  /// Deletes tools if necessary.
  /// @param no_mouse_hover delete tools if only device MouseNoButton remains.
  void removeCurrentTool(const Tool::InputDevices device,
                         const bool no_mouse_hover = false) noexcept;
  /// Append tool to currently used tools. This takes ownership of tool.
  void setCurrentTool(std::shared_ptr<Tool> tool) noexcept;

  int slideForPage(const int page) const noexcept;
  int pageForSlide(const int slide) const noexcept;
  bool pageExists(const int page) const noexcept;

 public slots:
  /// Set maximum memory for cache. This function uses double instead of
  /// qreal because it is connected to a QDoubleSpinBox.
  void setMemory(const double new_memory);
  /// Set maximal number of slides in cache.
  void setCacheSize(const int new_size);
  /// Set renderer. Allowed values are "poppler", "mupdf",
  /// "poppler + external" and "mupdf + external".
  void setRenderer(const QString &string);
  /// Set page part threshold. This function uses double instead of qreal
  /// because it is connected to a QDoubleSpinBox.
  void setPagePartThreshold(const double threshold);
  /// Set number of drawing history steps for visible slides.
  void setHistoryVisibleSlide(const int length);
  /// Set number of drawing history steps for hidden slides.
  void setHistoryHiddenSlide(const int length);
  /// Enable/disable logging of slide changes.
  void setLogSlideChanges(const bool log);
#ifdef USE_EXTERNAL_RENDERER
  /// Set rendering command. It is not checked whether this command is valid.
  void setRenderingCommand(const QString &string);
  /// Set rendering arguments. It is not checked whether these are valid.
  void setRenderingArguments(const QString &string);
#endif
  /// Set overlay mode. Allowed values are defined in string_to_overlay_mode:
  /// "per page", "per label" and "cumulative"
  void setOverlayMode(const QString &string);
  /// Enable or disable automatic slide changes.
  void setAutoSlideChanges(const bool show);
  /// Enable or disable external links.
  void setExternalLinks(const bool enable);
  /// Enable or disable finalizing drawn paths.
  void setFinalizePaths(const bool finalize);

 signals:
  /// Interrupt drawing to avoid problems when changing or deleting tools.
  void stopDrawing();
  /// Tell master to redistribute cache memory.
  void distributeMemory();
};

Q_DECLARE_OPERATORS_FOR_FLAGS(Preferences::GlobalFlags);

/**
 * @brief Class with only static methods providing access to global Preferences
 * instance
 *
 * This class provides access to a global Preferences instance and makes
 * sure that only main() can initialize this global variable.
 * The global variable can be accessed using GlobalPreferences::readonly().
 * Writable preferences are accessible using
 * WritableGlobalPreferences::writable().
 *
 * Objects of this class are not allowed, it provides only static methods.
 */
class GlobalPreferences
{
  friend int main(int argc, char *argv[]);

 private:
  inline static Preferences *_instance = nullptr;
  inline static void initialize(QObject *parent = nullptr)
  {
    _instance = new Preferences(parent);
  }
  inline static void initialize(const QString &file, QObject *parent = nullptr)
  {
    _instance = new Preferences(file, parent);
  }

 protected:
  inline static Preferences *writable() noexcept { return _instance; }

 public:
  GlobalPreferences() = delete;
  inline static const Preferences *readonly() noexcept { return _instance; }
};

/**
 * @brief Variant of GlobalPreferences providing non-const access to global
 * Preferences instance.
 *
 * Friend classes of this class have access to the global Preferences
 * instance without the const restriction. Instances of friend classes
 * should in the same thread.
 *
 * Objects of this class are not allowed, it provides only static methods.
 */
class WritableGlobalPreferences : public GlobalPreferences
{
  friend class SettingsWidget;
  friend class KeyInputLabel;
  friend class TimerWidget;
  friend class Master;

 public:
  WritableGlobalPreferences() = delete;
};

/// Alias for GlobalPreferences::readonly
constexpr auto preferences = &GlobalPreferences::readonly;

/// Get global master object.
/// Only a single master object should be used. (Using multiple master
/// objects is not strictly forbitten, but does not make much sense.)
inline Master *master() noexcept
{
  return GlobalPreferences::readonly()->master;
}

/// Create tool from JSON formatted input.
std::shared_ptr<Tool> createTool(
    const QJsonObject &obj,
    const Tool::InputDevices default_device = Tool::NoDevice);

/// Create default tool from string.
std::shared_ptr<Tool> createToolFromString(
    const QString &str,
    const Tool::InputDevices default_device = Tool::NoDevice);

/// Write tool properties to JSON object.
void toolToJson(std::shared_ptr<const Tool> tool, QJsonObject &obj);

/// Static string to be used in regular expressions required to adapt paths.
constexpr char UNIX_LIKE[] =
    R"(\/(usr|ucrt64|mingw32|mingw64|clang32|clang64|clangamd64)\/bin$)";

#endif  // PREFERENCES_H
