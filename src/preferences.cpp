// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#include "src/preferences.h"

#include <QCommandLineParser>
#include <QDir>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <utility>

#include "src/config.h"
#include "src/drawing/dragtool.h"
#include "src/drawing/drawtool.h"
#include "src/drawing/pointingtool.h"
#include "src/drawing/selectiontool.h"
#include "src/drawing/texttool.h"
#include "src/log.h"
#include "src/master.h"
#include "src/names.h"
#include "src/rendering/pdfdocument.h"

int Preferences::slideForPage(const int page) const noexcept
{
  return master->slideForPage(page);
}

int Preferences::pageForSlide(const int slide) const noexcept
{
  return master->pageForSlide(slide);
}

bool Preferences::pageExists(const int page) const noexcept
{
  return master->pageExits(page);
}

std::shared_ptr<Tool> createToolFromString(
    const QString &str, const Tool::InputDevices default_device)
{
  QJsonObject json;
  json.insert("tool", QJsonValue(str));
  return createTool(json, default_device);
}

std::shared_ptr<Tool> createTool(const QJsonObject &obj,
                                 const Tool::InputDevices default_device)
{
  const Tool::BasicTool base_tool = get_string_to_tool().value(
      obj.value("tool").toString(), Tool::InvalidTool);
  const QJsonValue dev_obj = obj.value("device");
  const auto &device_to_string = get_devices_to_string();
  Tool::InputDevices device;
  if (dev_obj.isString())
    device |=
        device_to_string.key(dev_obj.toString().toStdString(), Tool::NoDevice);
  else if (dev_obj.isArray())
    for (const auto &dev : static_cast<const QJsonArray>(dev_obj.toArray()))
      device |=
          device_to_string.key(dev.toString().toStdString(), Tool::NoDevice);
  debug_msg(DebugSettings, "device:" << device);
  if (device == Tool::NoDevice) device = default_device;
  Tool *tool;
  switch (base_tool) {
    case Tool::Pen:
    case Tool::FixedWidthPen:
    case Tool::Highlighter: {
      const QColor color(obj.value("color").toString(
          base_tool == DrawTool::Highlighter ? "yellow" : "black"));
      const float width = obj.value("width").toDouble(
          base_tool == DrawTool::Highlighter ? 20. : 2.);
      if (width <= 0.) return nullptr;
      const Qt::PenStyle pen_style = get_pen_style_codes().key(
          obj.value("style").toString().toStdString(), Qt::SolidLine);
      const QColor brush_color(obj.value("fill").toString());
      const Qt::BrushStyle brush_style = get_brush_style_codes().key(
          obj.value("brush").toString().toStdString(),
          brush_color.isValid() ? Qt::SolidPattern : Qt::NoBrush);
      const DrawTool::Shape shape = get_shape_codes().key(
          obj.value("shape").toString().toStdString(), DrawTool::Freehand);
      debug_msg(DebugSettings,
                "creating pen/highlighter" << base_tool << color << width);
      QPainter::CompositionMode composition =
          base_tool == DrawTool::Highlighter
              ? QPainter::CompositionMode_Darken
              : QPainter::CompositionMode_SourceOver;
      if (obj.contains("composition"))
        composition = get_composition_mode_codes().key(
            obj.value("composition").toString().toStdString(), composition);
      tool = new DrawTool(
          base_tool, device,
          QPen(color, width, pen_style, Qt::RoundCap, Qt::RoundJoin),
          QBrush(brush_color, brush_style), composition, shape);
      break;
    }
    case Tool::Eraser: {
      debug_msg(DebugSettings, "creating eraser");
      const QColor color(obj.value("color").toString("#c0808080"));
      const float linewidth = obj.value("linewidth").toDouble(0.5);
      tool = new PointingTool(Tool::Eraser, obj.value("size").toDouble(10.),
                              QBrush(color), device, linewidth);
      break;
    }
    case Tool::Pointer: {
      const QColor color(obj.value("color").toString("red"));
      const float size = obj.value("size").toDouble(5.);
      if (size <= 0.) return nullptr;
      debug_msg(DebugSettings, "creating pointer" << color << size);
      tool = new PointingTool(Tool::Pointer, size, color, device);
      static_cast<PointingTool *>(tool)->initPointerBrush();
      break;
    }
    case Tool::Torch: {
      const QColor color(obj.value("color").toString("#80000000"));
      const float size = obj.value("size").toDouble(80.);
      if (size <= 0.) return nullptr;
      debug_msg(DebugSettings, "creating torch" << color << size);
      tool = new PointingTool(Tool::Torch, size, color, device);
      break;
    }
    case Tool::Magnifier: {
      const QColor color(obj.value("color").toString("#80c0c0c0"));
      const float size = obj.value("size").toDouble(120.);
      const float scale = obj.value("scale").toDouble(2.);
      debug_msg(DebugSettings, "creating magnifier" << color << size << scale);
      tool = new PointingTool(Tool::Magnifier, size, color, device,
                              scale < 0.1   ? 0.1
                              : scale > 10. ? 5.
                                            : scale);
      break;
    }
    case Tool::TextInputTool: {
      QFont font(obj.value("font").toString("black"));
      if (obj.contains("font size"))
        font.setPointSizeF(obj.value("font size").toDouble(12.));
      const QColor color(obj.value("color").toString("black"));
      debug_msg(DebugSettings, "creating text tool" << color << font);
      tool = new TextTool(font, color, device);
      break;
    }
    case Tool::DragViewTool: {
      auto dtool = new DragTool(device);
      dtool->setFlags(obj);
      tool = dtool;
      break;
    }
    case Tool::BasicSelectionTool:
    case Tool::RectSelectionTool:
    case Tool::FreehandSelectionTool:
      tool = new SelectionTool(base_tool, device);
      break;
    case Tool::InvalidTool:
      debug_msg(DebugSettings,
                "tried to create invalid tool" << obj.value("tool"));
      return nullptr;
    default:
      debug_msg(DebugSettings, "creating default tool" << obj.value("tool"));
      /* These case of AnyDrawTool, AnyPointingTool, and AnySelectionTool
       * should never occur. But if they did occur, they would lead to
       * segmentation faults if they were not handled correctly. */
      if (base_tool & Tool::AnyDrawTool)
        tool = new DrawTool(base_tool, device, QPen());
      else if (base_tool & Tool::AnyPointingTool)
        tool = new PointingTool(base_tool, 10., Qt::black, device);
      else if (base_tool & Tool::AnySelectionTool)
        tool = new SelectionTool(base_tool, device);
      else
        tool = new Tool(base_tool, device);
  }
  return std::shared_ptr<Tool>(tool);
}

void toolToJson(std::shared_ptr<const Tool> tool, QJsonObject &obj)
{
  if (!tool) return;
  obj.insert("tool", get_string_to_tool().key(tool->tool()));
  const auto &device_to_string = get_device_to_string();
  QJsonArray devices;
  for (auto device : device_to_string.keys())
    if (tool->device().testFlag(device))
      devices.append(QString::fromStdString(device_to_string[device]));
  obj.insert("device", devices);
  if (tool->tool() & Tool::AnyDrawTool) {
    const auto drawtool = std::static_pointer_cast<const DrawTool>(tool);
    obj.insert("width", drawtool->width());
    obj.insert("color", drawtool->color().name());
    if (drawtool->brush().style() != Qt::NoBrush) {
      obj.insert("fill", drawtool->brush().color().name());
      if (drawtool->brush().style() != Qt::SolidPattern)
        obj.insert(
            "brush",
            get_brush_style_codes().value(drawtool->brush().style()).c_str());
    }
    obj.insert("style",
               get_pen_style_codes().value(drawtool->pen().style()).c_str());
    obj.insert("shape",
               get_shape_codes().value(drawtool->shape(), "freehand").c_str());
    if (drawtool->compositionMode() != QPainter::CompositionMode_SourceOver)
      obj.insert("composition",
                 get_composition_mode_codes()
                     .value(drawtool->compositionMode(), "unknown")
                     .c_str());
  } else if (tool->tool() & Tool::AnyPointingTool) {
    obj.insert("size",
               std::static_pointer_cast<const PointingTool>(tool)->size());
    obj.insert("color", tool->color().name());
  } else if (tool->tool() == Tool::TextInputTool) {
    obj.insert("color", tool->color().name());
    obj.insert(
        "font",
        std::static_pointer_cast<const TextTool>(tool)->font().toString());
  }
}

Preferences::Preferences(QObject *parent)
    : QObject(parent),
      settings(QSettings::NativeFormat, QSettings::UserScope, "beamerpresenter",
               "beamerpresenter")
{
  settings.setFallbacksEnabled(false);
  settings.setDefaultFormat(QSettings::IniFormat);
  current_tools.insert(Tool::Eraser,
                       std::shared_ptr<Tool>(new PointingTool(
                           Tool::Eraser, 10., QColor(128, 128, 128, 192),
                           Tool::TabletEraser | Tool::MouseRightButton, 0.5)));
  // If settings is empty, copy system scope config to user space file.
  if (settings.allKeys().isEmpty() && settings.isWritable()) {
    QSettings globalsettings(QSettings::NativeFormat, QSettings::SystemScope,
                             "beamerpresenter", "beamerpresenter");
    for (const auto &key :
         static_cast<const QList<QString>>(globalsettings.allKeys()))
      settings.setValue(key, globalsettings.value(key));
  }
}

Preferences::Preferences(const QString &file, QObject *parent)
    : QObject(parent), settings(file, QSettings::IniFormat)
{
  settings.setDefaultFormat(QSettings::IniFormat);
  current_tools.insert(Tool::Eraser,
                       std::shared_ptr<Tool>(new PointingTool(
                           Tool::Eraser, 10., QColor(128, 128, 128, 192),
                           Tool::TabletEraser | Tool::MouseRightButton, 0.5)));
}

Preferences::~Preferences()
{
  current_tools.clear();
  key_tools.clear();
}

void Preferences::loadSettings()
{
  debug_msg(DebugSettings, "Loading settings:" << settings.fileName());
  debug_msg(DebugSettings, settings.allKeys());
  bool ok;

  // GENERAL SETTINGS
  {
    // Paths to required files / directories
    QString fallback_root = QCoreApplication::applicationDirPath();
    debug_msg(DebugSettings, "fallback root:" << fallback_root);
    const QRegularExpression unix_like(UNIX_LIKE);
    if (fallback_root.contains(unix_like)) fallback_root.remove(unix_like);
    gui_config_file =
        settings.value("gui config", DEFAULT_GUI_CONFIG_PATH).toString();
    if (!QFileInfo::exists(gui_config_file)) {
      gui_config_file = DEFAULT_GUI_CONFIG_PATH;
      if (!QFileInfo::exists(gui_config_file)) {
        gui_config_file = fallback_root + DEFAULT_GUI_CONFIG_PATH;
        if (!QFileInfo::exists(gui_config_file))
          gui_config_file = fallback_root + "/gui.json";
      }
    }
    manual_file = settings.value("manual", DOC_PATH "/README.html").toString();
    if (!QFileInfo::exists(manual_file)) {
      manual_file = DOC_PATH "/README.html";
      if (!QFileInfo::exists(manual_file)) {
        manual_file = fallback_root + DOC_PATH "/README.html";
        if (!QFileInfo::exists(manual_file))
          manual_file = fallback_root + "/doc/README.html";
      }
    }
    icon_path = settings.value("icon path", DEFAULT_ICON_PATH).toString();
    if (!QFileInfo::exists(icon_path)) {
      icon_path = DEFAULT_ICON_PATH;
      if (!QFileInfo::exists(icon_path)) {
        icon_path = fallback_root + DEFAULT_ICON_PATH;
        if (!QFileInfo::exists(icon_path)) icon_path = fallback_root + "/icons";
      }
    }
    const QString icontheme = settings.value("icon theme").toString();
    if (!icontheme.isEmpty()) QIcon::setThemeName(icontheme);
    const QStringList iconthemepaths =
        settings.value("icon theme paths").toStringList();
    if (!iconthemepaths.isEmpty()) QIcon::setThemeSearchPaths(iconthemepaths);
  }
#ifdef QT_DEBUG
  // Only check settings for debug information if was not set on the command
  // line.
  if (debug_level == 0) {
    const QStringList debug_flags = settings.value("debug").toStringList();
    for (const auto &flag : debug_flags)
      debug_level |= string_to_debug_flag(flag);
  }
#endif
  if (settings.value("log", false).toBool())
    global_flags |= LogSlideChanges;
  else
    global_flags &= ~LogSlideChanges;
  if (settings.value("automatic slide changes", true).toBool())
    global_flags |= AutoSlideChanges;
  else
    global_flags &= ~AutoSlideChanges;
  if (settings.value("external links", false).toBool())
    global_flags |= OpenExternalLinks;
  else
    global_flags &= ~OpenExternalLinks;
  if (settings.value("autoload pdfpc", false).toBool())
    global_flags |= AutoloadPdfpc;

  qreal num;
  {
    const int frame_time = settings.value("frame time").toInt(&ok);
    if (ok && frame_time > 0) slide_duration_animation = frame_time;
    if (!settings.value("gestures", true).toBool()) gesture_actions.clear();
    num = settings.value("max zoom increment", 0.2).toReal(&ok);
    if (ok && num > 0) max_zoom_increment = 1.0 + num;
    const QColor color(settings.value("search highlight color").toString());
    if (color.isValid()) search_highlighting_color = color;
  }

  // DRAWING
  settings.beginGroup("drawing");
  int value = settings.value("history length visible").toUInt(&ok);
  if (ok) history_length_visible_slides = value;
  value = settings.value("history length hidden").toUInt(&ok);
  if (ok) history_length_hidden_slides = value;
  overlay_mode = get_string_to_overlay_mode().value(
      settings.value("mode").toString(), OverlayDrawingMode::Cumulative);
  num = settings.value("line sensitifity").toDouble(&ok);
  if (ok && 0 < num && num < 0.1) line_sensitivity = num;
  num = settings.value("snap angle").toDouble(&ok);
  if (ok && 0 < num && num < 0.5) snap_angle = num;
  num = settings.value("ellipse sensitivity").toDouble(&ok);
  if (ok && 0 < num && num < 0.5) ellipse_sensitivity = num;
  num = settings.value("circle threshold").toDouble(&ok);
  if (ok && 0 < num && num < 0.5) ellipse_to_circle_snapping = num;
  num = settings.value("rectangle angle tolerance").toDouble(&ok);
  if (ok && 0 < num && num < 3) rect_angle_tolerance = num;
  num = settings.value("rectangle closing tolerance").toDouble(&ok);
  if (ok && 0 < num && num < 2) rect_closing_tolerance = num;
  if (settings.value("finalize drawn paths", false).toBool())
    global_flags |= FinalizeDrawnPaths;
  else
    global_flags &= ~FinalizeDrawnPaths;
  num = settings.value("arrow tip scale").toDouble(&ok);
  if (ok && 0.01 < arrow_tip_scale && arrow_tip_scale < 100)
    arrow_tip_scale = num;
  num = settings.value("arrow tip ratio").toDouble(&ok);
  if (ok && 0.01 < arrow_tip_scale && arrow_tip_scale < 100)
    arrow_tip_ratio = num;
  settings.endGroup();

  // SELECTION
  settings.beginGroup("selection");
  {
    num = settings.value("handle size").toDouble(&ok);
    if (ok && 0.1 < num && num < 100) selection_rect_handle_size = num;
    num = settings.value("minimal path width").toDouble(&ok);
    if (ok && 0.1 < num && num < 100) path_min_selectable_width = num;
    QColor color = QColor(settings.value("selection color").toString());
    if (color.isValid()) selection_rect_brush = color;
    color = QColor(settings.value("selection outline color").toString());
    if (color.isValid()) selection_rect_pen.setColor(color);
    num = settings.value("selection outline width").toDouble(&ok);
    if (ok && 0 <= num && num < 50) selection_rect_pen.setWidthF(num);
  }
  settings.endGroup();

  // RENDERING
  settings.beginGroup("rendering");
  // page_part threshold
  const float threshold = settings.value("page part threshold").toReal(&ok);
  if (ok) page_part_threshold = threshold;
  // maximum image size
  const qreal maximgsize = settings.value("max image size").toReal(&ok);
  if (ok) max_image_size = maximgsize;
  {  // renderer
#ifdef USE_EXTERNAL_RENDERER
    rendering_command = settings.value("rendering command").toString();
    rendering_arguments = settings.value("rendering arguments").toStringList();
#endif
    const QString renderer_str =
        settings.value("renderer").toString().toLower();
    debug_msg(DebugSettings, renderer_str);
    if (!renderer_str.isEmpty()) {
      bool understood_renderer = false;
#ifdef USE_QTPDF
      if (renderer_str.count("qtpdf") > 0) {
        renderer = Renderer::QtPDF;
        pdf_engine = PdfEngine::QtPDF;
        understood_renderer = true;
      }
#endif
#ifdef USE_MUPDF
      if (renderer_str.count("mupdf") > 0) {
        renderer = Renderer::MuPDF;
        pdf_engine = PdfEngine::MuPdf;
        understood_renderer = true;
      }
#endif
#ifdef USE_POPPLER
      if (renderer_str.count("poppler") > 0) {
        renderer = Renderer::Poppler;
        pdf_engine = PdfEngine::Poppler;
        understood_renderer = true;
      }
#endif
#ifdef USE_EXTERNAL_RENDERER
      if (renderer_str.count("extern") > 0) {
        if (rendering_command.isEmpty() || rendering_arguments.isEmpty()) {
          qWarning() << "External renderer requested but no command "
                        "or no arguments given. Falling back to Poppler.";
          qInfo() << "Note that both \"rendering command\" and "
                     "\"rendering arguments\" are required.";
          understood_renderer = true;
        } else
          renderer = Renderer::ExternalRenderer;
      }
#endif
      if (!understood_renderer)
        qWarning() << "Invalid renderer argument in settings:" << renderer_str;
    }
  }
  settings.endGroup();

  // cache
  const qreal memory = settings.value("memory").toFloat(&ok);
  if (ok) max_memory = memory;
  const int npages = settings.value("cache pages").toInt(&ok);
  if (ok) max_cache_pages = npages;

  // INTERACTION
  // Default tools associated to devices
  settings.beginGroup("tools");
  QStringList allKeys = settings.allKeys();
  QList<Action> actions;
  if (!allKeys.isEmpty()) {
    current_tools.clear();
    const auto &device_to_string = get_devices_to_string();
    QList<std::shared_ptr<Tool>> tools;
    for (const auto &dev : std::as_const(allKeys))
      parseActionsTools(
          settings.value(dev), actions, tools,
          device_to_string.key(dev.toStdString(), Tool::AnyNormalDevice));
    actions.clear();
    for (const auto &tool : std::as_const(tools))
      current_tools.insert(tool->tool(), tool);
  }
  settings.endGroup();
  // Keyboard shortcuts
  settings.beginGroup("keys");
  allKeys = settings.allKeys();
  if (!allKeys.isEmpty()) {
    QList<std::shared_ptr<Tool>> tools;
    key_actions.clear();
    for (const auto &key : std::as_const(allKeys)) {
      const QKeySequence seq(key);
      if (seq.isEmpty())
        qWarning() << "Unknown key sequence in config:" << key;
      else {
        parseActionsTools(settings.value(key), actions, tools);
        for (const auto &tool : std::as_const(tools))
          key_tools.insert(seq, tool);
        for (const auto action : std::as_const(actions))
          key_actions.insert(seq, action);
        actions.clear();
        tools.clear();
      }
    }
  }
  settings.endGroup();
  // Gestures
  settings.beginGroup("gestures");
  allKeys = settings.allKeys();
  if (!allKeys.isEmpty()) {
    QList<std::shared_ptr<Tool>> tools;
    gesture_actions.clear();
    Gesture gesture;
    for (const auto &key : std::as_const(allKeys)) {
      gesture = string_to_gesture(key);
      if (gesture != Gesture::InvalidGesture)
        qWarning() << "Unknown gesture in config:" << key;
      else {
        parseActionsTools(settings.value(key), actions, tools);
        for (const auto action : std::as_const(actions))
          gesture_actions.insert(gesture, action);
        actions.clear();
        if (!tools.isEmpty()) {
          qWarning() << "Gestures cannot be used to select tools";
          tools.clear();
        }
      }
    }
  }
  settings.endGroup();
}

void Preferences::parseActionsTools(const QVariant &input,
                                    QList<Action> &actions,
                                    QList<std::shared_ptr<Tool>> &tools,
                                    const Tool::InputDevices default_device)
{
  // First try to interpret sequence as json object.
  // Here the way how Qt changes the string is not really optimal.
  // In the input quotation marks must be escaped. For
  // convenience it is allowed to use single quotation marks
  // instead.
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(
      input.toStringList().join(",").replace("'", "\"").toUtf8(), &error);
  QJsonArray array;
  if (error.error == QJsonParseError::NoError) {
    if (doc.isArray())
      array = doc.array();
    else if (doc.isObject())
      array.append(doc.object());
  }
  if (array.isEmpty()) {
    for (const auto &action_str :
         static_cast<const QStringList>(input.toStringList())) {
      const Action action = get_string_to_action().value(action_str.toLower(),
                                                         Action::InvalidAction);
      if (action == InvalidAction) {
        auto tool = createToolFromString(action_str.toLower(), default_device);
        if (tool)
          tools.append(tool);
        else
          qWarning() << "Unknown action/tool in config:" << action_str
                     << "as part of input" << input;
      } else
        actions.append(action);
    }
    return;
  }
  for (const auto &value : std::as_const(array)) {
    if (value.isString()) {
      const Action action = get_string_to_action().value(
          value.toString().toLower(), Action::InvalidAction);
      if (action == InvalidAction) {
        auto tool =
            createToolFromString(value.toString().toLower(), default_device);
        if (tool)
          tools.append(tool);
        else
          qWarning() << "Unknown action/tool in config:" << value
                     << "as part of input" << input;
      } else
        actions.append(action);
    } else if (value.isObject()) {
      const QJsonObject object = value.toObject();
      std::shared_ptr<Tool> tool = createTool(object, default_device);
      if (tool) {
        debug_msg(
            DebugSettings | DebugDrawing,
            "Adding tool" << tool.get() << tool->tool() << tool->device());
        tools.append(tool);
      }
    }
  }
}

#ifdef QT_DEBUG
void Preferences::loadDebugFromParser(const QCommandLineParser &parser)
{
  // set debug flags
  if (parser.isSet("debug")) {
    debug_level = 0;
    for (const auto &flag :
         static_cast<const QStringList>(parser.value("debug").split(",")))
      debug_level |= string_to_debug_flag("debug " + flag);
  }
}
#endif

void Preferences::loadFromParser(const QCommandLineParser &parser)
{
  // presentation file from positional arguments
  const QStringList arguments = parser.positionalArguments();
  if (!arguments.isEmpty()) {
    file_alias["presentation"] = arguments.first();
    if (!file_alias.contains("notes")) {
      if (arguments.length() > 1)
        file_alias["notes"] = arguments[1];
      else
        file_alias["notes"] = arguments.first();
    }
  }

  // timer total time
  if (parser.isSet("t")) msecs_total = 60000 * parser.value("t").toDouble();

  // log slide changes
  if (parser.isSet("log")) global_flags |= LogSlideChanges;

  // renderer and pdf engine
  if (parser.isSet("renderer")) {
    QString const &renderer_str = parser.value("renderer");
    bool understood_renderer = false;
    debug_msg(DebugSettings, "renderer" << renderer_str);
#ifdef USE_QTPDF
    if (renderer_str.count("qtpdf", Qt::CaseInsensitive) > 0) {
      renderer = Renderer::QtPDF;
      pdf_engine = PdfEngine::QtPDF;
      understood_renderer = true;
    }
#endif
#ifdef USE_MUPDF
    if (renderer_str.count("mupdf", Qt::CaseInsensitive) > 0) {
      renderer = Renderer::MuPDF;
      pdf_engine = PdfEngine::MuPdf;
      understood_renderer = true;
    }
#endif
#ifdef USE_POPPLER
    if (renderer_str.count("poppler", Qt::CaseInsensitive) > 0) {
      renderer = Renderer::Poppler;
      pdf_engine = PdfEngine::Poppler;
      understood_renderer = true;
    }
#endif
#ifdef USE_EXTERNAL_RENDERER
    if (renderer_str.count("extern", Qt::CaseInsensitive) > 0) {
      if (rendering_command.isEmpty() || rendering_arguments.isEmpty()) {
        qWarning() << "External renderer requested but no command "
                      "or no arguments given. Falling back to Poppler.";
        qInfo() << "Note that both \"rendering command\" and "
                   "\"rendering arguments\" are required.";
        understood_renderer = true;
      } else
        renderer = Renderer::ExternalRenderer;
    }
#endif
    if (!understood_renderer)
      qWarning() << "Invalid renderer argument on command line:"
                 << renderer_str;
  }

  // disable cache
  if (parser.isSet("nocache")) max_cache_pages = 0;

#ifdef QT_DEBUG
  // (Re)load debug info from command line.
  if (settings.contains("debug")) loadDebugFromParser(parser);
#endif
}

void Preferences::addKeyAction(const QKeySequence sequence, const Action action)
{
  if (!key_actions.contains(sequence) ||
      !key_actions.values(sequence).contains(action))
    key_actions.insert(sequence, action);
  const QString keycode = sequence.toString();
  if (!keycode.isEmpty()) {
    settings.beginGroup("keys");
    QStringList list = settings.value(keycode).toStringList();
    const QString &string = get_string_to_action().key(action);
    if (!list.contains(string)) {
      list.append(string);
      settings.setValue(keycode, list);
    }
    settings.endGroup();
  }
}

void Preferences::removeKeyAction(const QKeySequence sequence,
                                  const Action action)
{
  key_actions.remove(sequence, action);
  settings.beginGroup("keys");
  const QString keycode = sequence.toString();
  if (!keycode.isEmpty() && settings.contains(keycode)) {
    QStringList list = settings.value(keycode).toStringList();
    list.removeAll(get_string_to_action().key(action));
    if (list.isEmpty())
      settings.remove(keycode);
    else
      settings.setValue(keycode, list);
  }
  settings.endGroup();
}

void Preferences::setMemory(double new_memory)
{
  max_memory = 1048596 * new_memory;
  settings.setValue("memory", QString::number(max_memory));
  emit distributeMemory();
}

void Preferences::setCacheSize(const int new_size)
{
  max_cache_pages = new_size;
  settings.setValue("cache pages", QString::number(max_cache_pages));
  emit distributeMemory();
}

void Preferences::setRenderer(const QString &string)
{
  const QString &new_renderer = string.toLower();
#ifdef USE_QTPDF
  if (new_renderer == "qtpdf") {
    settings.beginGroup("rendering");
    settings.setValue("renderer", "qtpdf");
    settings.endGroup();
    return;
  }
#endif
#ifdef USE_MUPDF
  if (new_renderer == "mupdf") {
    settings.beginGroup("rendering");
    settings.setValue("renderer", "mupdf");
    settings.endGroup();
    return;
  }
#endif
#ifdef USE_POPPLER
  if (new_renderer == "poppler") {
    settings.beginGroup("rendering");
    settings.setValue("renderer", "poppler");
    settings.endGroup();
    return;
  }
#endif
#ifdef USE_EXTERNAL_RENDERER
#ifdef USE_QTPDF
  if (new_renderer == "qtpdf + external") {
    settings.beginGroup("rendering");
    settings.setValue("renderer", "qtpdf external");
    settings.endGroup();
    return;
  }
#endif
#ifdef USE_MUPDF
  if (new_renderer == "mupdf + external") {
    settings.beginGroup("rendering");
    settings.setValue("renderer", "mupdf external");
    settings.endGroup();
    return;
  }
#endif
#ifdef USE_POPPLER
  if (new_renderer == "poppler + external") {
    settings.beginGroup("rendering");
    settings.setValue("renderer", "poppler external");
    settings.endGroup();
    return;
  }
#endif
#endif  // USE_EXTERNAL_RENDERER
}

QUrl Preferences::resolvePath(const QString &identifier) const noexcept
{
  QDir basedir;
  if (document) {
    basedir = QDir(document->getPath());
    basedir.cdUp();
  }
  QUrl url(identifier);

  // 1. If no scheme is specified: Check if URL refers to a local file
  if (url.isRelative()) {
    if (basedir.exists(url.path())) {
      url.setScheme("file");
      url.setPath(basedir.absoluteFilePath(url.path()));
      return url;
    }
    if (QFileInfo::exists(url.path())) {
      url.setScheme("file");
      if (QDir::isRelativePath(url.path()))
        url.setPath(QDir().absoluteFilePath(url.path()));
      return url;
    }
    // Fall back to heuristics (might be unsafe!)
    url = QUrl::fromUserInput(identifier, basedir.absolutePath());
  }

  // 2. If scheme to local file is explicitly specified
  // Check basedir and relative to local path.
  static const QStringList local_schemes{"v4l2", "v4l", "cam", "file"};
  if (local_schemes.contains(url.scheme())) {
    if (basedir.exists(url.path()))
      url.setPath(basedir.absoluteFilePath(url.path()));
    if (QFileInfo::exists(url.path()) && QDir::isRelativePath(url.path()))
      url.setPath(QDir().absoluteFilePath(url.path()));
    return url;
  }

  // 3. If a scheme other than file is specified
  if (global_flags & OpenExternalLinks)
    return url;
  else
    return QUrl();
}

std::shared_ptr<Tool> Preferences::currentTool(
    const Tool::InputDevices device) const noexcept
{
  const auto end = current_tools.cend();
  for (auto tool = current_tools.cbegin(); tool != end; ++tool)
    if (*tool && ((*tool)->device() & device)) return *tool;
  return nullptr;
}

void Preferences::removeKeyTool(std::shared_ptr<const Tool> tool,
                                const bool remove_from_settings)
{
  if (remove_from_settings) settings.beginGroup("keys");
  for (auto it = key_tools.begin(); it != key_tools.end();) {
    if (*it == tool) {
      emit stopDrawing();
      if (remove_from_settings) {
        const QString keycode = QKeySequence(it.key()).toString();
        if (!keycode.isEmpty()) settings.remove(keycode);
      }
      it = key_tools.erase(it);
    } else
      ++it;
  }
  if (remove_from_settings) settings.endGroup();
}

void Preferences::replaceKeyToolShortcut(const QKeySequence oldkeys,
                                         const QKeySequence newkeys,
                                         std::shared_ptr<Tool> tool)
{
  // TODO: check if this really removes the tools!
  key_tools.remove(oldkeys, tool);
  settings.beginGroup("keys");
  const QString oldcode = QKeySequence(oldkeys).toString();
  if (!oldcode.isEmpty()) settings.remove(oldcode);
  if (!newkeys.isEmpty() && tool) {
    key_tools.insert(newkeys, tool);
    QJsonObject obj;
    toolToJson(tool, obj);
    /* Convert JSON object to string for config file. This makes
     * it human-readable and circumvents a bug in old Qt versions. */
    settings.setValue(
        QKeySequence(newkeys).toString(),
        QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact)));
  }
  settings.endGroup();
}

void Preferences::setPagePartThreshold(const double threshold)
{
  page_part_threshold = threshold;
  settings.beginGroup("rendering");
  settings.setValue("page part threshold", page_part_threshold);
  settings.endGroup();
}

void Preferences::setHistoryVisibleSlide(const int length)
{
  if (length >= 0) history_length_visible_slides = length;
  settings.beginGroup("drawing");
  settings.setValue("history length visible", history_length_visible_slides);
  settings.endGroup();
}

void Preferences::setHistoryHiddenSlide(const int length)
{
  if (length >= 0) history_length_hidden_slides = length;
  settings.beginGroup("drawing");
  settings.setValue("history length hidden", history_length_hidden_slides);
  settings.endGroup();
}

void Preferences::setLogSlideChanges(const bool log)
{
  if (log) {
    global_flags |= LogSlideChanges;
    settings.setValue("log", true);
  } else {
    global_flags &= ~LogSlideChanges;
    settings.remove("log");
  }
}

#ifdef USE_EXTERNAL_RENDERER
void Preferences::setRenderingCommand(const QString &string)
{
  rendering_command = string;
  settings.beginGroup("rendering");
  settings.setValue("rendering command", rendering_command);
  settings.endGroup();
}

void Preferences::setRenderingArguments(const QString &string)
{
  rendering_arguments = string.split(",");
  settings.beginGroup("rendering");
  settings.setValue("rendering arguments", rendering_arguments);
  settings.endGroup();
}
#endif

void Preferences::setOverlayMode(const QString &string)
{
  const OverlayDrawingMode mode = get_string_to_overlay_mode().value(
      string, OverlayDrawingMode::InvalidOverlayMode);
  if (mode == OverlayDrawingMode::InvalidOverlayMode) return;
  overlay_mode = mode;
  settings.beginGroup("drawing");
  settings.setValue("mode", string);
  settings.endGroup();
}

void Preferences::setAutoSlideChanges(const bool show)
{
  if (show)
    global_flags |= AutoSlideChanges;
  else
    global_flags &= ~AutoSlideChanges;
  settings.setValue("automatic slide changes", show);
}

void Preferences::setFinalizePaths(const bool finalize)
{
  if (finalize)
    global_flags |= FinalizeDrawnPaths;
  else
    global_flags &= ~FinalizeDrawnPaths;
  settings.setValue("finalize drawn paths", finalize);
}

void Preferences::setExternalLinks(const bool enable)
{
  if (enable)
    global_flags |= OpenExternalLinks;
  else
    global_flags &= ~OpenExternalLinks;
  settings.setValue("external links", enable);
}

void Preferences::showErrorMessage(const QString &title,
                                   const QString &text) const
{
  qCritical() << text;
  master->showErrorMessage(title, text);
}

bool Preferences::setGuiConfigFile(const QString &file)
{
  if (file == gui_config_file) return false;
  if (QFileInfo(file).isFile()) {
    settings.setValue("gui config", file);
    gui_config_file = file;
    return true;
  }
  showErrorMessage(
      tr("Invalid file"),
      tr("GUI config file not set because it is not a valid file: ") + file);
  return false;
}

void Preferences::removeCurrentTool(const Tool::InputDevices device,
                                    const bool no_mouse_hover) noexcept
{
  Tool::InputDevices newdevice;
  for (auto tool_it = current_tools.begin(); tool_it != current_tools.end();) {
    if ((*tool_it)->device() & device) {
      newdevice = (*tool_it)->device() & ~device;
      if (newdevice)
        (*tool_it++)->setDevice(newdevice);
      else
        tool_it = current_tools.erase(tool_it);
    } else if (no_mouse_hover &&
               ((*tool_it)->device() == Tool::MouseNoButton)) {
      tool_it = current_tools.erase(tool_it);
    } else {
      ++tool_it;
    }
  }
}

void Preferences::setCurrentTool(std::shared_ptr<Tool> tool) noexcept
{
  Tool::InputDevices device = tool->device();
  // Delete mouse no button devices if MouseLeftButton is overwritten.
  if (tool->device() & Tool::MouseLeftButton) device |= Tool::MouseNoButton;
  // Delete tablet no pressure device if any tablet device is overwritten.
  if (tool->device() &
      (Tool::TabletCursor | Tool::TabletPen | Tool::TabletEraser))
    device |= Tool::TabletHover;
  removeCurrentTool(device, tool->device() & Tool::MouseLeftButton);
  current_tools.insert(tool->tool(), tool);
}
