// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#include "src/master.h"

#include <zlib.h>

#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMessageBox>
#include <QMimeDatabase>
#include <QSizeF>
#include <QString>
#include <QThread>
#include <QTimerEvent>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QtConfig>
#include <algorithm>
#include <utility>

#include "src/config.h"
#include "src/drawing/tool.h"
#include "src/gui/analogclockwidget.h"
#include "src/gui/clockwidget.h"
#include "src/gui/containerwidget.h"
#include "src/gui/noteswidget.h"
#include "src/gui/searchwidget.h"
#include "src/gui/settingswidget.h"
#include "src/gui/slidelabelwidget.h"
#include "src/gui/slidenumberwidget.h"
#include "src/gui/stackedwidget.h"
#include "src/gui/tabwidget.h"
#include "src/gui/thumbnailwidget.h"
#include "src/gui/timerwidget.h"
#include "src/gui/tocwidget.h"
#include "src/gui/toolselectorwidget.h"
#include "src/gui/toolwidget.h"
#include "src/log.h"
#include "src/names.h"
#include "src/pdfmaster.h"
#include "src/preferences.h"
#include "src/rendering/pixcache.h"
#include "src/slidescene.h"
#include "src/slideview.h"

Master::~Master()
{
  emit clearCache();
  for (const auto cache : std::as_const(caches)) cache->thread()->quit();
  for (const auto cache : std::as_const(caches)) {
    cache->thread()->wait(thread_wait_time_ms);
    delete cache;
  }
  for (const auto &doc : std::as_const(documents)) {
    QList<SlideScene *> &scenes = doc->getScenes();
    while (!scenes.isEmpty()) delete scenes.takeLast();
  }
  while (!windows.isEmpty()) delete windows.takeLast();
  documents.clear();
}

Master::Status Master::readGuiConfig(const QString &filename)
{
  // Read file into JSON document
  QFile file(filename);
  if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qCritical() << tr("Could not read GUI config:") << filename;
    return ReadConfigFailed;
  }
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || doc.isNull() ||
      doc.isEmpty() || !doc.isArray()) {
    qCritical() << tr("GUI config file is empty or parsing failed:")
                << error.errorString();
    qInfo()
        << "Note that the GUI config file must represent a single JSON array.";
    return ParseConfigFailed;
  }

  /// Map "presentation", "notes", ... to PdfDocument pointers.
  /// This is needed to interpret GUI config and avoids asking for files
  /// multiple times.
  QMap<QString, std::shared_ptr<PdfMaster>> known_files;

  const QJsonArray array = doc.array();
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
  for (auto it = array.cbegin(); it != array.cend(); ++it)
#else
  for (auto it = array.begin(); it != array.end(); ++it)
#endif
  {
    if (it->type() != QJsonValue::Object) {
      qCritical() << tr("Ignoring invalid entry in GUI config.") << *it;
      continue;
    }
    const QJsonObject obj = it->toObject();
    // Start recursive creation of widgets.
    QWidget *const widget = createWidget(obj, nullptr, known_files);
    if (!widget) continue;
    // Root widgets should get their own QMainWindow.
    QMainWindow *const window = new QMainWindow();
    windows.append(window);
    window->setCentralWidget(widget);  // window takes ownership of widget
    window->resize(obj.value("width").toInt(640),
                   obj.value("height").toInt(400));
    if (obj.contains("title"))
      window->setWindowTitle(obj.value("title").toString());
    else
      window->setWindowTitle("BeamerPresenter");
    // Check whether this window should alawys be shown on top. This is the
    // default if the window only contains a tool selector.
    if (obj.value("always show").toBool(obj.value("type") == "tool selector"))
      // This window should always be on top.
      window->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint);
  }

  if (documents.isEmpty()) return NoPDFLoaded;
  if (windows.isEmpty()) return NoWindowsCreated;

  initializePageIndex();
  WritableGlobalPreferences::writable()->document =
      documents.first()->getDocument();
  const auto &scenes = documents.first()->getScenes();
  if (scenes.empty()) return NoScenesCreated;
  connect(scenes.first(), &SlideScene::finishTransition, this,
          &Master::postNavigation);
  // Focus a scene view by default. This makes keyboard shortcuts available.
  scenes.first()->views().first()->setFocus();

  debug_msg(DebugDrawing,
            "Initialized documents:" << known_files.size()
                                     << preferences()->file_alias);
  // Load drawings
  // TODO: avoid loading drawings multiple times
  QSet<QString> loaded_paths;
  if (!master_file.isEmpty()) loaded_paths.insert(master_file);
  for (const auto &doc : std::as_const(documents))
    if (!doc->drawingsPath().isEmpty())
      loaded_paths.insert(doc->drawingsPath());
  for (const auto &path : loaded_paths) loadBprDrawings(path, true);
  debug_msg(DebugDrawing, "Loaded drawings:" << known_files.size()
                                             << preferences()->file_alias);
  return Success;
}

void Master::initializePageIndex()
{
  int max_pages = 0;
  for (const auto &doc : std::as_const(documents))
    max_pages = std::max(doc->numberOfPages(), max_pages);
  if (page_idx.empty()) {
    for (int i = 0; i < max_pages; ++i) {
      page_idx.append(i);
      page_to_slide[i] = i;
    }
  } else if (page_to_slide.lastKey() < max_pages - 1) {
    for (int i = page_idx.size(), j = page_to_slide.lastKey() + 1;
         j < max_pages; ++i, ++j) {
      page_idx.append(j);
      page_to_slide[j] = i;
    }
  }
}

void Master::removeSlide(const int slide)
{
  if (slide < 0 || slide >= page_idx.size() || page_idx.size() == 1) return;
  const int page = page_idx[slide];
  if (page_to_slide[page] == slide) page_to_slide.remove(page_idx[slide]);
  page_idx.removeAt(slide);
  auto it = page_idx.crbegin();
  int i = page_idx.size();
  while (--i >= slide && it != page_idx.crend()) page_to_slide[*it++] = i;
  debug_msg(DebugPageChange, "removed slide" << slide << ", page" << page);
  debug_msg(DebugPageChange, "new page index:" << page_idx);
}

void Master::insertSlideAt(const int slide, const int page)
{
  if (slide < 0 || slide > page_idx.size()) return;
  page_idx.insert(slide, page);
  auto it = page_idx.crbegin();
  int i = page_idx.size();
  while (--i >= slide && it != page_idx.crend()) page_to_slide[*it++] = i;
  debug_msg(DebugPageChange, "inserted slide" << slide << ", page" << page);
  debug_msg(DebugPageChange, "new page index:" << page_idx);
}

QWidget *Master::createWidget(
    const QJsonObject &object, QWidget *parent,
    QMap<QString, std::shared_ptr<PdfMaster>> &known_files)
{
  QWidget *widget = nullptr;
  const GuiWidget type = string_to_widget_type(object.value("type").toString());
  switch (type) {
    case VBoxWidgetType:
    case HBoxWidgetType: {
      auto cwidget =
          new ContainerWidget(type == VBoxWidgetType ? QBoxLayout::TopToBottom
                                                     : QBoxLayout::LeftToRight,
                              parent);
      fillContainerWidget(cwidget, object, known_files);
      widget = cwidget;
      break;
    }
    case StackedWidgetType: {
      auto stackwidget = new StackedWidget(parent);
      fillContainerWidget(stackwidget, object, known_files);
      widget = stackwidget;
      break;
    }
    case TabbedWidgetType: {
      auto tabwidget = new TabWidget(parent);
      widget = tabwidget;
      const QString orientation =
          object.value("orientation").toString().toLower();
      if (orientation == "south")
        tabwidget->setTabPosition(QTabWidget::South);
      else if (orientation == "west")
        tabwidget->setTabPosition(QTabWidget::West);
      else if (orientation == "east")
        tabwidget->setTabPosition(QTabWidget::East);
      else
        tabwidget->setTabPosition(QTabWidget::North);
      fillContainerWidget(tabwidget, object, known_files);
      break;
    }
    case SlideType: {
      auto pdf = openFile(object.value("file").toString(), known_files);
      if (pdf) widget = createSlide(object, pdf, parent);
      break;
    }
    case OverviewType: {
      std::shared_ptr<const PdfDocument> document;
      const QString file = object.value("file").toString();
      if (!file.isEmpty()) {
        auto pdf = openFile(file, known_files);
        if (pdf) document = pdf->getDocument();
      }
      auto twidget = new ThumbnailWidget(document, parent);
      twidget->widget()->installEventFilter(this);
      widget = twidget;
      if (object.contains("columns"))
        twidget->setColumns(object.value("columns").toInt(4));
      if (object.value("overlays").toString() == "skip")
        twidget->flags() |= ThumbnailWidget::SkipOverlays;
      connect(this, &Master::sendAction, twidget,
              &ThumbnailWidget::handleAction, Qt::QueuedConnection);
      connect(this, &Master::navigationSignal, twidget,
              &ThumbnailWidget::receivePage, Qt::QueuedConnection);
      break;
    }
    case TOCType: {
      std::shared_ptr<const PdfDocument> document;
      const QString file = object.value("file").toString();
      if (!file.isEmpty()) {
        auto pdf = openFile(file, known_files);
        if (pdf) document = pdf->getDocument();
      }
      widget = new TOCwidget(document, parent);
      break;
    }
    case NotesType: {
      const bool label_by_number =
          object.value("identifier").toString() == "number";
      const QString id = object.value("id").toString();
      auto nwidget = new NotesWidget(label_by_number, id, parent);
      widget = nwidget;
      connect(this, &Master::navigationSignal, nwidget,
              &NotesWidget::pageChanged, Qt::QueuedConnection);
      connect(this, &Master::writeNotes, nwidget, &NotesWidget::writeNotes,
              Qt::DirectConnection);
      connect(this, &Master::readNotes, nwidget, &NotesWidget::readNotes,
              Qt::DirectConnection);
      connect(this, &Master::sendPageNote, nwidget, &NotesWidget::setPageNote,
              Qt::DirectConnection);
      connect(nwidget, &NotesWidget::saveDrawings, this, &Master::saveDrawings);
      // TODO: reload initiated from notes widget
      connect(nwidget, &NotesWidget::loadDrawings, this,
              [&](const QString name) { loadBprDrawings(name, true); });
      connect(nwidget, &NotesWidget::newUnsavedChanges, this, [&](void) {
        documents.first()->flags() |= PdfMaster::UnsavedNotes;
      });
      nwidget->zoomIn(object.value("zoom").toInt(notes_widget_default_zoom));
      // TODO: maybe find better implementation for this:
      if (object.contains("file"))
        nwidget->loadNotes(object.value("file").toString());
      break;
    }
    case ToolSelectorType: {
      auto toolwidget = new ToolSelectorWidget(parent);
      connect(this, &Master::sendActionStatus, toolwidget,
              &ToolSelectorWidget::sendStatus);
      toolwidget->addButtons(object.value("buttons").toArray());
      connect(toolwidget, &ToolSelectorWidget::sendTool, this, &Master::setTool,
              Qt::QueuedConnection);
      connect(toolwidget, &ToolSelectorWidget::sendToolProperties, this,
              &Master::sendToolProperties, Qt::DirectConnection);
      connect(toolwidget, &ToolSelectorWidget::updatedTool, this,
              &Master::sendNewToolSoft);
      widget = toolwidget;
      break;
    }
    case ToolWidgetType: {
      const QBoxLayout::Direction direction =
          object.value("orientation").toString("horizontal") == "horizontal"
              ? QBoxLayout::LeftToRight
              : QBoxLayout::TopToBottom;
      ToolWidget *toolwidget = new ToolWidget(parent, direction);
      widget = toolwidget;
      QList<Tool::InputDevice> devices;
      const auto &device_to_string = get_device_to_string();
      auto collect_devices = [&](const QString &name) -> void {
        if (!object.contains(name)) return;
        devices.clear();
        const QJsonArray arr = object.value(name).toArray();
        Tool::InputDevice dev;
        for (const auto &dev_obj : arr) {
          dev = device_to_string.key(dev_obj.toString().toStdString(),
                                     Tool::NoDevice);
          if (dev != Tool::NoDevice &&
              (dev & Tool::AnyNormalDevice) != Tool::AnyNormalDevice)
            devices.append(dev);
        }
      };
      collect_devices("mouse devices");
      if (!devices.isEmpty()) toolwidget->setMouseDevices(devices);
      collect_devices("tablet devices");
      if (!devices.isEmpty()) toolwidget->setTabletDevices(devices);
      toolwidget->initialize();
      break;
    }
    case SettingsType:
      widget = new SettingsWidget(parent);
      break;
    case SearchType: {
      widget = new SearchWidget(parent);
      connect(static_cast<SearchWidget *>(widget), &SearchWidget::searchPdf,
              this,
              [&](const QString &text, const int page, const bool forward) {
                documents.first()->search(text, page, forward);
              });
      break;
    }
    case ClockType:
      widget =
          new ClockWidget(object.value("touch input").toBool(true), parent);
      // This signal could also be connected directly to Master::sendAction,
      // but maybe the clock should be able to send different actions. Since
      // the clock rarely sends actions, this little overhead is unproblematic.
      connect(static_cast<ClockWidget *>(widget), &ClockWidget::sendAction,
              this, &Master::handleAction, Qt::QueuedConnection);
      break;
    case AnalogClockType: {
      widget = new AnalogClockWidget(object, parent);
      // This signal could also be connected directly to Master::sendAction,
      // but maybe the clock should be able to send different actions. Since
      // the clock rarely sends actions, this little overhead is unproblematic.
      connect(static_cast<AnalogClockWidget *>(widget),
              &AnalogClockWidget::sendAction, this, &Master::handleAction,
              Qt::QueuedConnection);
      break;
    }
    case TimerType: {
      TimerWidget *twidget = new TimerWidget(parent);
      widget = twidget;
      connect(twidget, &TimerWidget::updateStatus, this,
              &Master::sendActionStatus);
      connect(this, &Master::sendAction, twidget, &TimerWidget::handleAction,
              Qt::QueuedConnection);
      connect(twidget, &TimerWidget::setTimeForPage, this,
              &Master::setTimeForPage);
      connect(twidget, &TimerWidget::getTimeForPage, this,
              &Master::getTimeForPage);
      connect(this, &Master::setTotalTime, twidget, &TimerWidget::setTotalTime);
      connect(this, &Master::navigationSignal, twidget,
              &TimerWidget::updatePage, Qt::QueuedConnection);
      if (object.contains("colormap")) {
        const QJsonObject cmap_obj = object.value("colormap").toObject();
        QMap<qint32, QRgb> colormap;
        qint32 time;
        QColor color;
        bool ok;
        for (auto it = cmap_obj.constBegin(); it != cmap_obj.constEnd(); ++it) {
          time = it.key().toInt(&ok);
          color = QColor(it->toString());
          if (ok && color.isValid()) colormap[1000 * time] = color.rgb();
        }
        if (!colormap.isEmpty()) twidget->setColorMap(colormap);
      }
      if (!object.value("require confirmation").toBool(true))
        twidget->flags() |= TimerWidget::SetTimeWithoutConfirmation;
      if (object.value("confirmation default").toBool(false))
        twidget->flags() |= TimerWidget::SetTimerConfirmationDefault;
      break;
    }
    case SlideNumberType:
      widget = new SlideNumberWidget(parent);
      connect(static_cast<SlideNumberWidget *>(widget),
              &SlideNumberWidget::sendPage, this, &Master::navigateToPage);
      connect(this, &Master::navigationSignal,
              static_cast<SlideNumberWidget *>(widget),
              &SlideNumberWidget::receivePage, Qt::QueuedConnection);
      break;
    case SlideLabelType:
      widget = new SlideLabelWidget(parent);
      connect(static_cast<SlideLabelWidget *>(widget),
              &SlideLabelWidget::sendPage, this, &Master::navigateToPage);
      connect(this, &Master::navigationSignal,
              static_cast<SlideLabelWidget *>(widget),
              &SlideLabelWidget::receivePage, Qt::QueuedConnection);
      break;
    case GuiWidget::InvalidType:
      showErrorMessage(tr("Error while reading GUI config"),
                       tr("Ignoring entry in GUI config with invalid type ") +
                           object.value("type").toString());
      qCritical() << tr("Ignoring entry in GUI config with invalid type ") +
                         object.value("type").toString();
      break;
  }
  if (!widget) {
    qCritical() << tr("An error occurred while trying to create a widget with "
                      "JSON object")
                << object;
    return nullptr;
  }
  widget->installEventFilter(this);
  // Add keyboard shortcut.
  if (object.contains("keys")) {
    const QKeySequence seq(object.value("keys").toString());
    if (seq.isEmpty())
      qWarning() << "Unknown key sequence in config:" << object.value("keys");
    else
      shortcuts[seq] = widget;
  }
  // Read base color from config.
  const QColor bg_color = QColor(object.value("color").toString());
  if (bg_color.isValid()) {
    if (type == SlideType)
      static_cast<SlideView *>(widget)->setBackgroundBrush(bg_color);
    else {
      QPalette palette = widget->palette();
      palette.setColor(QPalette::All, QPalette::Base, bg_color);
      widget->setPalette(palette);
    }
  }
  return widget;
}

SlideView *Master::createSlide(const QJsonObject &object,
                               std::shared_ptr<PdfMaster> pdf, QWidget *parent)
{
  if (!pdf) return nullptr;

  const bool is_master = object.value("master").toBool();
  if (is_master && documents.first() != pdf) {
    documents.removeAll(pdf);
    documents.prepend(pdf);
  }

  // Calculate the shift for scene.
  PageShift shift = {object.value("shift").toInt(), ShiftOverlays::NoOverlay};
  const QString overlays = object.value("overlays").toString().toLower();
  if (overlays == "first")
    shift.overlay = ShiftOverlays::FirstOverlay;
  else if (overlays == "last")
    shift.overlay = ShiftOverlays::LastOverlay;

  // Get the page part.
  const QString page_part_str = object.value("page part").toString().toLower();
  PagePart page_part = FullPage;
  if (page_part_str == "left")
    page_part = LeftHalf;
  else if (page_part_str == "right")
    page_part = RightHalf;
  if (preferences()->page_part_threshold > 0.) {
    const QSizeF reference = pdf->getPageSize(0);
    if (reference.width() <
        preferences()->page_part_threshold * reference.height())
      page_part = FullPage;
  }
  if (is_master)
    WritableGlobalPreferences::writable()->default_page_part = page_part;
  if (page_part == FullPage)
    pdf->flags() |= PdfMaster::FullPageUsed;
  else if (page_part == LeftHalf)
    pdf->flags() |= PdfMaster::LeftHalfUsed;
  else if (page_part == RightHalf)
    pdf->flags() |= PdfMaster::RightHalfUsed;

  SlideScene *scene{nullptr};
  // Check whether we need a new SlideScene.
  for (auto &sceneit : pdf->getScenes())
    if (sceneit->getShift() == shift && sceneit->pagePart() == page_part) {
      scene = sceneit;
      break;
    }

  // Create new slide scene if necessary.
  if (scene == nullptr) {
    scene = new SlideScene(pdf, page_part, parent);
    scene->setPageShift(shift);
    if (shift == PageShift({0, ShiftOverlays::NoOverlay}) || is_master) {
      connect(scene, &SlideScene::finishTransition, this,
              &Master::postNavigation);
      pdf->getScenes().prepend(scene);
    } else
      pdf->getScenes().append(scene);
    connect(scene, &SlideScene::newUnsavedDrawings, pdf.get(),
            &PdfMaster::newUnsavedDrawings);
    connect(scene, &SlideScene::navigationSignal, this, &Master::navigateToPage,
            Qt::QueuedConnection);
    connect(scene, &SlideScene::sendAction, this, &Master::handleAction,
            Qt::QueuedConnection);
    connect(this, &Master::sendAction, scene, &SlideScene::receiveAction);
    connect(this, &Master::sendNewToolScene, scene, &SlideScene::toolChanged);
    connect(this, &Master::sendToolProperties, scene,
            &SlideScene::toolPropertiesChanged, Qt::DirectConnection);
    connect(this, &Master::postRendering, scene, &SlideScene::postRendering,
            Qt::QueuedConnection);
    connect(this, &Master::prepareNavigationSignal, scene,
            &SlideScene::prepareNavigationEvent);
    connect(pdf.get(), &PdfMaster::updateSearch, scene,
            &SlideScene::updateSearchResults);
    connect(preferences(), &Preferences::stopDrawing, scene,
            &SlideScene::stopDrawing);
  } else if (is_master) {
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
    pdf->getScenes().swapItemsAt(pdf->getScenes().indexOf(scene), 0);
#else
    QList<SlideScene *> &list = pdf->getScenes();
    SlideScene *const tmp_scene = list.first();
    const int idx = list.indexOf(scene);
    list[0] = list[idx];
    list[idx] = tmp_scene;
#endif
  }

  PixCache::CacheMode cacheMode = PixCache::FitPage;
  const double scroll_mode_aspect =
      object.value("scroll mode aspect").toDouble(-1.0);
  if (scroll_mode_aspect > 0) {
    scene->setFixedAspect(scroll_mode_aspect);
    cacheMode = PixCache::FitWidth;
  }

  // Get or create cache object.
  const PixCache *pixcache = getPixcache(
      pdf->getDocument(), page_part, object.value("cache hash").toInt(-1),
      object.value("threads").toInt(1), cacheMode);

  // Create slide view.
  SlideView *slide = new SlideView(scene, pixcache, parent);
  // flags for slide view
  if (object.value("media controls").toBool(false))
    slide->flags() |= SlideView::MediaControls;
  if (!object.value("show tools").toBool(true))
    slide->flags() &= ~SlideView::ShowPointingTools;
  // flags for slide scene
  if (!object.value("autoplay").toBool(true))
    scene->flags() &= ~(SlideScene::AutoplayVideo | SlideScene::AutoplaySounds);
  if (!object.value("media").toBool(true))
    scene->flags() &= ~(SlideScene::LoadMedia | SlideScene::CacheVideos |
                        SlideScene::AutoplayVideo | SlideScene::AutoplaySounds);
  if (!object.value("transitions").toBool(true))
    scene->flags() &= ~SlideScene::ShowTransitions;
  if (!object.value("cache videos").toBool(true))
    scene->flags() &= ~SlideScene::CacheVideos;
  if (!object.value("draw").toBool(true))
    scene->flags() &= ~SlideScene::ShowDrawings;
  // Mute slides by default, except if they are marked as master.
  if (!object.value("mute").toBool(!object.value("master").toBool(false)))
    scene->flags() &= ~SlideScene::MuteSlide;
  connect(slide, &SlideView::sendAction, this, &Master::handleAction,
          Qt::QueuedConnection);
  connect(scene, &SlideScene::navigationToViews, slide, &SlideView::pageChanged,
          Qt::DirectConnection);

  return slide;
}

std::shared_ptr<PdfMaster> Master::openFile(
    QString name, QMap<QString, std::shared_ptr<PdfMaster>> &known_files)
{
  // Check if name is known.
  if (known_files.contains(name)) return known_files.value(name);
  // Check if name is an alias.
  if (name.isEmpty()) name = "presentation";
  QString file = preferences()->file_alias.value(name);
  if (file.isEmpty()) file = name;
  // Check if the alias is known.
  else if (known_files.contains(file))
    return known_files.value(file);
  // Check if file is a valid path.
  QFileInfo fileinfo(file);
  if (!fileinfo.isFile())
    // Ask the user to open a file.
    fileinfo = QFileInfo(QFileDialog::getOpenFileName(
        nullptr, tr("Open file") + " \"" + name + "\"", "",
        tr("Documents (*.pdf);;BeamerPresenter/Xournal++ files "
           "(*.bpr *.xoj *.xopp *.xml);;All files (*)")));
  if (!fileinfo.isFile()) {
    // File does not exist, mark given aliases as invalid.
    qCritical() << tr("No valid file given");
    known_files[name] = nullptr;
    if (file != name) known_files[file] = nullptr;
    known_files[fileinfo.absoluteFilePath()] = nullptr;
    return nullptr;
  }
  // File exists, create a new PdfMaster object.
  const QString abs_path = fileinfo.absoluteFilePath();
  const QMimeType type = QMimeDatabase().mimeTypeForFile(abs_path);
  if (type.inherits("application/gzip") || type.inherits("text/xml") ||
      type.inherits("application/x-xopp") ||
      type.inherits("application/x-bpr")) {
    debug_msg(DebugDrawing, "Loading drawing file:" << name << abs_path
                                                    << known_files.size());
    loadBprInit(abs_path);
    for (const auto &doc : std::as_const(documents))
      known_files[doc->getFilename()] = doc;
    for (auto it = preferences()->file_alias.cbegin();
         it != preferences()->file_alias.cend(); ++it) {
      const QString abs_alias_path = QFileInfo(*it).absoluteFilePath();
      for (const auto &doc : std::as_const(documents)) {
        debug_msg(DebugDrawing, "Comparing:" << name << abs_alias_path
                                             << doc->getFilename()
                                             << doc->drawingsPath());
        if (doc->getFilename() == abs_alias_path || doc->getFilename() == *it ||
            doc->drawingsPath() == *it ||
            doc->drawingsPath() == abs_alias_path) {
          known_files[it.key()] = doc;
          known_files[*it] = doc;
          known_files[abs_alias_path] = doc;
          WritableGlobalPreferences::writable()->file_alias[it.key()] =
              doc->getFilename();
          break;
        }
      }
    }
    if (known_files.contains(name)) return known_files.value(name);
    debug_msg(DebugDrawing, "Alias not found:" << name << known_files.size()
                                               << preferences()->file_alias);
    if (!documents.empty()) return documents.first();
    return nullptr;
  }
  if (!type.inherits("application/pdf") &&
      !type.inherits("application/x-pdf")) {
    qCritical() << "Invalid file type given:" << type << abs_path;
    return nullptr;
  }
  auto pdf = createPdfMaster(abs_path);

  known_files[name] = pdf;
  known_files[abs_path] = pdf;
  if (file != name) known_files[file] = pdf;
  if (pdf) known_files[pdf->getFilename()] = pdf;
  return pdf;
}

std::shared_ptr<PdfMaster> Master::createPdfMaster(QString abs_path)
{
  debug_msg(DebugDrawing, "Opening new document" << abs_path);
  // First create an empty PdfMaster. Don't do anything with it,
  // it's not initialized yet. Only connect it to signals, some of
  // which may already be required in initialization.
  std::shared_ptr<PdfMaster> pdf(new PdfMaster);

  connect(this, &Master::sendAction, pdf.get(), &PdfMaster::receiveAction);
  connect(this, &Master::navigationSignal, pdf.get(),
          &PdfMaster::distributeNavigationEvents, Qt::QueuedConnection);
  connect(this, &Master::setTimeForPage, pdf.get(), &PdfMaster::setTimeForPage);
  connect(this, &Master::getTimeForPage, pdf.get(), &PdfMaster::getTimeForPage);
  connect(pdf.get(), &PdfMaster::writeNotes, this, &Master::writeNotes,
          Qt::DirectConnection);
  connect(pdf.get(), &PdfMaster::readNotes, this, &Master::readNotes,
          Qt::DirectConnection);
  connect(pdf.get(), &PdfMaster::setTotalTime, this, &Master::setTotalTime);
  connect(pdf.get(), &PdfMaster::sendPage, this, &Master::navigateToPage,
          Qt::DirectConnection);

  // Initialize document, try to laod PDF
  // TODO: should this be done at this point?
  pdf->loadDocument(abs_path);
  if (pdf->getDocument() == nullptr) {
    qCritical() << tr(
        "Failed to load PDF document. This will result in errors!");
    return nullptr;
  }

  // Adjust number of pages in preferences
  if (preferences()->number_of_pages &&
      preferences()->number_of_pages != pdf->numberOfPages()) {
    showErrorMessage(tr("Error while loading PDF file"),
                     tr("Loaded PDF files with different numbers of pages. "
                        "You should expect errors."));
    qCritical() << tr("Loaded PDF files with different numbers of pages.");
    WritableGlobalPreferences::writable()->number_of_pages =
        std::max(preferences()->number_of_pages, pdf->numberOfPages());
  } else
    WritableGlobalPreferences::writable()->number_of_pages =
        pdf->numberOfPages();

  documents.append(pdf);
  return pdf;
}

const PixCache *Master::getPixcache(const std::shared_ptr<PdfDocument> &doc,
                                    const PagePart page_part, int cache_hash,
                                    const int threads,
                                    const PixCache::CacheMode cacheMode)
{
  if (cache_hash == -1)
    // -1 is the "default hash" and indicates that a new object has to
    // be created.
    // Set hash to -2 or smaller.
    cache_hash = (caches.isEmpty() || caches.firstKey() >= 0)
                     ? -2
                     : caches.firstKey() - 1;
  else {
    // Check if a PixCache object with the given hash already exists.
    const PixCache *pixcache = caches.value(cache_hash, nullptr);
    if (pixcache) return pixcache;
  }

  // Create a new PixCache object an store it in caches.
  // Create the PixCache object.
  PixCache *pixcache = new PixCache(doc, threads, page_part, cacheMode);
  // Keep the new pixcache in caches.
  caches[cache_hash] = pixcache;
  // Set maximum number of pages in cache from settings.
  pixcache->setMaxNumber(preferences()->max_cache_pages);
  // Move the PixCache object to an own thread.
  pixcache->moveToThread(new QThread(pixcache));
  // Make sure that pixcache is initialized when the thread is started.
  connect(pixcache->thread(), &QThread::started, pixcache, &PixCache::init,
          Qt::QueuedConnection);
  connect(this, &Master::navigationSignal, pixcache,
          &PixCache::pageNumberChanged, Qt::QueuedConnection);
  connect(this, &Master::sendScaledMemory, pixcache, &PixCache::setScaledMemory,
          Qt::QueuedConnection);
  connect(this, &Master::clearCache, pixcache, &PixCache::clear,
          Qt::QueuedConnection);
  // Start the thread.
  pixcache->thread()->start();
  return pixcache;
}

void Master::fillContainerWidget(
    ContainerBaseClass *parent, const QJsonObject &parent_obj,
    QMap<QString, std::shared_ptr<PdfMaster>> &known_files)
{
  const QJsonArray array = parent_obj.value("children").toArray();
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
  for (auto it = array.cbegin(); it != array.cend(); ++it)
#else
  for (auto it = array.begin(); it != array.end(); ++it)
#endif
  {
    if (it->type() != QJsonValue::Type::Object) {
      qCritical() << tr("Ignoring invalid entry in GUI config.") << *it;
      continue;
    }
    const QJsonObject obj = it->toObject();
    // Create child widgets recursively
    QWidget *const newwidget =
        createWidget(obj, parent->asWidget(), known_files);
    if (!newwidget) continue;
    QString title = obj.value("title").toString();
    if (title.isEmpty()) title = obj.value("type").toString();
    parent->addWidgetCommon(newwidget, title);
  }
}

void Master::showAll() const
{
  for (const auto window : std::as_const(windows)) window->show();
}

bool Master::eventFilter(QObject *obj, QEvent *event)
{
  if (event->type() != QEvent::KeyPress)
    return QObject::eventFilter(obj, event);
  event->ignore();
  obj->event(event);
  if (event->isAccepted()) return true;
  const auto *kevent = static_cast<QKeyEvent *>(event);
#if (QT_VERSION_MAJOR >= 6)
  const QKeySequence key_code(kevent->keyCombination());
#else
  const quint32 key_code =
      kevent->key() | (kevent->modifiers() & ~Qt::KeypadModifier);
#endif
  // Search shortcuts for given key sequence.
  {
    QWidget *widget = shortcuts.value(key_code);
    debug_msg(DebugKeyInput,
              "Key action:" << widget << kevent
                            << (kevent->key() |
                                (kevent->modifiers() & ~Qt::KeypadModifier)));
    if (widget) {
      widget->show();
      widget->setFocus();
      QStackedWidget *stackwidget =
          dynamic_cast<QStackedWidget *>(widget->parentWidget());
      debug_msg(DebugKeyInput, widget << stackwidget << widget->parentWidget());
      if (stackwidget) {
        QTabWidget *tabwidget =
            dynamic_cast<QTabWidget *>(stackwidget->parentWidget());
        if (tabwidget)
          tabwidget->setCurrentWidget(widget);
        else
          stackwidget->setCurrentWidget(widget);
      }
    }
  }
  // Search actions in preferences for given key sequence.
  for (Action action : static_cast<const QList<Action>>(
           preferences()->key_actions.values(key_code))) {
    debug_msg(DebugKeyInput, "Global key action:" << action);
    handleAction(action);
  }
  // Search tools in preferences for given key sequence.
  const auto tools = preferences()->key_tools.values(key_code);
  for (const auto &tool : tools)
    if (tool && tool->device()) setTool(tool->copy());
  event->accept();
  return true;
}

void Master::handleAction(const Action action)
{
  switch (action) {
    case InvalidAction:
    case NoAction:
      break;
    case Update:
      navigateToSlide(preferences()->slide);
      break;
    case NextPage:
      navigateToSlide(preferences()->slide + 1);
      break;
    case PreviousPage:
      navigateToSlide(preferences()->slide - 1);
      break;
    case NextSkippingOverlays:
      navigateToPage(documents.first()->overlaysShiftedPage(
          preferences()->page, {1, ShiftOverlays::FirstOverlay}));
      break;
    case PreviousSkippingOverlays:
      navigateToPage(documents.first()->overlaysShiftedPage(
          preferences()->page, {-1, ShiftOverlays::FirstOverlay}));
      break;
    case FirstPage:
      navigateToSlide(0);
      break;
    case LastPage:
      navigateToSlide(page_idx.size() - 1);
      break;
    case FullScreen:
      for (const auto win : std::as_const(windows)) {
        if (win->isActiveWindow()) {
          if (win->isFullScreen())
            win->showNormal();
          else
            win->showFullScreen();
          break;
        }
      }
      break;
    case SaveDrawings: {
      QString filename = master_file;
      if (filename.isEmpty()) filename = getSaveFileName();
      if (!filename.isEmpty()) saveBpr(filename);
      break;
    }
    case SaveDrawingsAs: {
      const QString filename = getSaveFileName();
      if (!filename.isEmpty()) saveBpr(filename);
      break;
    }
    case LoadDrawings:
    case LoadDrawingsNoClear: {
      const QString filename = getOpenFileName();
      if (!filename.isEmpty())
        loadBprDrawings(filename, action == LoadDrawings);
      navigateToSlide(preferences()->slide);
      break;
    }
    case ExportDrawingsSvg:
      for (const auto &doc : std::as_const(documents)) doc->exportAllSvg();
      break;
    case ReloadFiles: {
      // TODO: problems with slide labels, navigation, and videos after
      // reloading files
      bool changed = false;
      for (const auto &doc : std::as_const(documents))
        changed |= doc->loadDocument();
      if (!pdfpc_file.isNull()) loadPdfpcJSON(pdfpc_file);
      if (changed) {
        initializePageIndex();
        WritableGlobalPreferences::writable()->number_of_pages =
            documents.first()->numberOfPages();
        distributeMemory();
        emit clearCache();
        emit sendAction(PdfFilesChanged);
        navigateToSlide(preferences()->slide);
      }
      break;
    }
    case ResizeViews:
      distributeMemory();
      break;
    case Quit:
      if (!askCloseConfirmation()) break;
    case QuitNoConfirmation:
      for (const auto window : std::as_const(windows)) window->close();
      break;
    case Mute:
      debug_msg(DebugMedia, "muting application");
      WritableGlobalPreferences::writable()->global_flags |=
          Preferences::MuteApplication;
      emit sendAction(action);
      break;
    case Unmute:
      debug_msg(DebugMedia, "unmuting application");
      WritableGlobalPreferences::writable()->global_flags &=
          ~Preferences::MuteApplication;
      emit sendAction(action);
      break;
    case InsertSlide: {
      int page = page_to_slide.firstKey() - 1;
      while (pageExists(page)) --page;
      insertSlideAt(preferences()->slide + 1, page);
      navigateToSlide(preferences()->slide + 1);
      for (const auto &doc : std::as_const(documents))
        doc->flags() |= PdfMaster::UnsavedDrawings;
      break;
    }
    case RemoveSlide:
      removeSlide(preferences()->slide);
      navigateToSlide(preferences()->slide);
      for (const auto &doc : std::as_const(documents))
        doc->flags() |= PdfMaster::UnsavedDrawings;
      break;
    case RestoreSlide: {
      int page;
      // If the next page exists but its slide has been removed, restore that.
      if (preferences()->page + 1 < preferences()->number_of_pages &&
          !page_to_slide.contains(preferences()->page + 1))
        page = preferences()->page + 1;
      else if (pageExists(page_to_slide.firstKey() - 1))
        page = page_to_slide.firstKey() - 1;
      else
        break;
      insertSlideAt(preferences()->slide + 1, page);
      navigateToSlide(preferences()->slide + 1);
      for (const auto &doc : std::as_const(documents))
        doc->flags() |= PdfMaster::UnsavedDrawings;
      break;
    }
    default:
      emit sendAction(action);
  }
}

bool Master::pageExists(const int page) const
{
  if (page_to_slide.contains(page)) return true;
  for (const auto &doc : documents)
    if (page >= 0 && doc->numberOfPages() > page || doc->hasPage(page))
      return true;
  return false;
}

bool Master::askCloseConfirmation() noexcept
{
  std::shared_ptr<PdfMaster> doc = documents.first();
  if (doc &&
      ((doc->flags() & (PdfMaster::UnsavedNotes | PdfMaster::UnsavedTimes)) ||
       ((doc->flags() & PdfMaster::UnsavedDrawings) && doc->hasDrawings()))) {
    debug_msg(DebugWidgets, "Asking for close confirmation:" << doc->flags());
    switch (QMessageBox::question(
        nullptr, Master::tr("Unsaved changes"),
        Master::tr("The document may contain unsaved changes. Quit anyway?"),
        QMessageBox::Close | QMessageBox::Save | QMessageBox::Cancel,
        QMessageBox::Save)) {
      case QMessageBox::Cancel:
        return false;
      case QMessageBox::Save: {
        QString filename = doc->drawingsPath();
        if (filename.isEmpty()) filename = getSaveFileName();
        saveBpr(filename);
        break;
      }
      default:
        break;
    }
  }
  return true;
}

void Master::leaveSlide(const int slide) const
{
  WritableGlobalPreferences::writable()->previous_page = preferences()->page;
  bool flexible_page_numbers = false;
  for (const auto &doc : std::as_const(documents)) {
    doc->clearHistory({slide, FullPage},
                      preferences()->history_length_hidden_slides);
    doc->clearHistory({slide, LeftHalf},
                      preferences()->history_length_hidden_slides);
    doc->clearHistory({slide, RightHalf},
                      preferences()->history_length_hidden_slides);
    if (doc->flexiblePageSizes()) flexible_page_numbers = true;
  }
  if (flexible_page_numbers) {
    QLayout *layout;
    for (const auto window : std::as_const(windows)) {
      window->updateGeometry();
      layout = window->layout();
      if (layout) layout->invalidate();
    }
  }
}

void Master::distributeMemory()
{
  if (preferences()->max_memory < 0) return;
  float scale = 0.;
  for (const auto cache : std::as_const(caches)) scale += cache->getPixels();
  if (scale <= 0) return;
  scale = preferences()->max_memory / scale;
  debug_msg(DebugCache, "Distributing memory. scale ="
                            << scale
                            << ", max. memory =" << preferences()->max_memory);
  emit sendScaledMemory(scale);
}

qint64 Master::getTotalCache() const
{
  qint64 cache = 0;
  for (const auto px : std::as_const(caches)) cache += px->getUsedMemory();
  return cache;
}

void Master::navigateToSlide(const int slide)
{
  debug_msg(DebugPageChange, "Change slide to" << slide);
  if (slide < 0 || slide >= page_idx.size()) return;
  if (cacheVideoTimer_id != -1) {
    killTimer(cacheVideoTimer_id);
    cacheVideoTimer_id = -1;
  }
  if (slideDurationTimer_id != -1) {
    killTimer(slideDurationTimer_id);
    slideDurationTimer_id = -1;
  }
  leaveSlide(preferences()->slide);
  const int page = pageForSlide(slide);
  emit prepareNavigationSignal(slide, page);
  for (const auto window : std::as_const(windows)) window->updateGeometry();
  // Get duration of the slide: But only take a nontrivial value if
  // the new page is (old page + 1).
  WritableGlobalPreferences::writable()->slide = slide;
  WritableGlobalPreferences::writable()->page = page;
  emit navigationSignal(slide, page);
}

void Master::postNavigation() noexcept
{
  if (slideDurationTimer_id != -1 || cacheVideoTimer_id != -1) return;
  const int page = preferences()->page;
  const qreal duration =
      preferences()->global_flags & Preferences::AutoSlideChanges &&
              page == preferences()->previous_page + 1
          ? documents.first()->getDocument()->duration(preferences()->page)
          : -1.;
  if (duration == 0.)
    slideDurationTimer_id = startTimer(preferences()->slide_duration_animation);
  else if (duration > 0.)
    slideDurationTimer_id = startTimer(1000 * duration);
  if (duration < 0. || duration > min_duration_cache_videos)
    cacheVideoTimer_id = startTimer(cache_videos_after_ms);
}

void Master::showErrorMessage(const QString &title, const QString &text) const
{
  QMessageBox::critical(windows.isEmpty() ? nullptr : windows.first(), title,
                        text);
}

void Master::setTool(std::shared_ptr<Tool> tool)
{
  if (!tool || !tool->device()) return;
  debug_msg(DebugDrawing | DebugKeyInput,
            "Set tool" << tool->tool() << tool->device());
  WritableGlobalPreferences::writable()->setCurrentTool(tool);
  emit sendNewToolScene(tool);
  emit sendNewToolSoft(tool);

#ifdef QT_DEBUG
  if ((preferences()->debug_level & DebugVerbose) &&
      preferences()->debug_level & DebugDrawing) {
    const auto &current_tools = preferences()->current_tools;
    const auto end = current_tools.cend();
    for (auto tool = current_tools.cbegin(); tool != end; ++tool)
      if (*tool)
        qDebug() << "tool:" << (*tool)->device() << (*tool)->tool()
                 << tool->get();
  }
#endif
}

QString Master::getOpenFileName()
{
  return QFileDialog::getOpenFileName(
      nullptr, tr("Load drawings"), "",
      tr("BeamerPresenter/Xournal++ files (*.bpr *.xoj *.xopp *.xml);;All "
         "files (*)"));
}

QString Master::getSaveFileName()
{
  return QFileDialog::getSaveFileName(
      nullptr, tr("Save drawings"), "",
      tr("BeamerPresenter/Xournal++ files (*.bpr *.xopp);;All files (*)"));
}

void Master::timerEvent(QTimerEvent *event)
{
  debug_msg(DebugPageChange, "timer event" << event->timerId()
                                           << cacheVideoTimer_id
                                           << slideDurationTimer_id);
  killTimer(event->timerId());
  if (event->timerId() == cacheVideoTimer_id) {
    cacheVideoTimer_id = -1;
    emit postRendering();
  } else if (event->timerId() == slideDurationTimer_id) {
    slideDurationTimer_id = -1;
    nextSlide();
  }
}

bool Master::saveBpr(const QString &filename)
{
  // Save elements and attributes specific to BeamerPresenter
  // only if file name does not end with ".xopp".
  const bool save_bp_specific =
      !filename.endsWith(".xopp", Qt::CaseInsensitive);
  QBuffer buffer;
  buffer.open(QBuffer::WriteOnly);
  if (!writeXml(buffer, save_bp_specific)) return false;

  // Write to gzipped file.
  if (!filename.endsWith(".xml")) {
    gzFile zfile = gzopen(filename.toUtf8(), "wb");
    if (zfile) {
      gzwrite(zfile, buffer.data().data(), buffer.data().length());
      gzclose_w(zfile);
      qInfo() << "Saved gzip-compressed XML to" << filename;
      master_file = filename;
      return true;
    }
  }
  QFile file(filename);
  if (file.open(QFile::WriteOnly)) {
    if (filename.endsWith(".xml"))
      qInfo() << "Saving uncompressed XML to" << filename;
    else
      qWarning() << "Compressing document failed. Saving uncompressed XML to"
                 << filename;
    file.write(buffer.data());
    file.close();
  } else {
    preferences()->showErrorMessage(
        tr("Error while saving file"),
        tr("Saving document failed for file path: ") + filename);
    return false;
  }
  master_file = filename;
  return true;
}

bool Master::writeXml(QBuffer &buffer, const bool save_bp_specific)
{
  QXmlStreamWriter writer(&buffer);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(0);
  writer.writeStartDocument();

  writer.writeStartElement("xournal");
  writer.writeAttribute("creator", "beamerpresenter " APP_VERSION);

  // Save elements and attributes specific to BeamerPresenter only if
  // file name does not end with ".xopp".
  if (save_bp_specific)
    writer.writeTextElement(
        "title",
        "BeamerPresenter document, compatible with Xournal++ "
        "- see https://github.com/beamerpresenter/BeamerPresenter");
  else
    writer.writeTextElement(
        "title",
        "Xournal++ document generated by BeamerPresenter in compatibility "
        "mode - see https://github.com/beamerpresenter/BeamerPresenter");

  {
    // Write preview picture from default PDF.
    const std::shared_ptr<PdfMaster> pdf = documents.first();
    const QSizeF &pageSize = pdf->getPageSize(0);
    const qreal resolution =
        128 / std::max(pageSize.width(), pageSize.height());
    const QPixmap pixmap = pdf->exportImage({0, FullPage}, resolution);
    QByteArray data;
    QBuffer preview_buffer(&data);
    if (preview_buffer.open(QBuffer::WriteOnly) &&
        pixmap.save(&preview_buffer, "PNG")) {
      preview_buffer.close();
      writer.writeStartElement("preview");
      writer.writeCharacters(data.toBase64());
      writer.writeEndElement();
    }
  }

  if (save_bp_specific) {
    // Some attributes specific for beamerpresenter (Xournal++ will ignore that)
    writer.writeStartElement("beamerpresenter");
    if (preferences()->msecs_total)
      writer.writeAttribute("duration", QTime::fromMSecsSinceStartOfDay(
                                            preferences()->msecs_total)
                                            .toString("h:mm:ss"));
    writer.writeStartElement("documents");
    // for (auto &&[alias, filename] :
    // preferences()->file_alias.asKeyValueRange())
    for (auto it = preferences()->file_alias.cbegin();
         it != preferences()->file_alias.cend(); ++it) {
      writer.writeStartElement("file");
      writer.writeAttribute("alias", it.key());
      writer.writeAttribute("path", it.value());
      writer.writeEndElement();  // "file" element
    }
    writer.writeEndElement();  // "documents" element
    emit writeNotes(writer);
    writer.writeEndElement();  // "beamerpresenter" element
  }

  for (const auto &pdf : std::as_const(documents))
    pdf->writePages(writer, save_bp_specific);

  writer.writeEndElement();  // "xournal" element
  writer.writeEndDocument();
  if (writer.hasError()) {
    preferences()->showErrorMessage(
        tr("Error while saving bpr/xopp file"),
        tr("Writing document resulted in error! Resulting "
           "document is probably corrupt."));
    return false;
  }
  return true;
}

/**
 * @brief getAbsFile
 * @param file path to a file (not a directory!)
 * @param refering_file file from which first file path was read
 * @return absolute file info
 *
 * Get absolute file path to file. If file is a relative path that does not
 * point to an existing file, try the path relative to the directory containing
 * refering_file.
 */
QFileInfo getAbsFile(const QString &file, const QString &refering_file)
{
  if (file.isEmpty()) return QFileInfo();
  QFileInfo finfo = QFileInfo(file);
  if (finfo.isAbsolute())
    return finfo;
  else if (finfo.isFile()) {
    finfo.makeAbsolute();
    return finfo;
  } else {
    auto alt_finfo = QFileInfo(QFileInfo(refering_file).absoluteDir(), file);
    if (alt_finfo.isFile()) {
      alt_finfo.makeAbsolute();
      return alt_finfo;
    } else
      return finfo;
  }
}

bool Master::loadBprDrawings(const QString &filename, const bool clear_drawings)
{
  QBuffer *buffer = loadZipToBuffer(filename);
  if (!buffer) return false;
  const bool status = loadXmlDrawings(buffer, clear_drawings, filename);
  buffer->close();
  delete buffer;
  if (status) master_file = filename;
  return status;
}

bool Master::loadBprInit(const QString &filename)
{
  QBuffer *buffer = loadZipToBuffer(filename);
  if (!buffer) return false;
  const bool status = loadXmlInit(buffer, filename);
  buffer->close();
  delete buffer;
  if (status) master_file = filename;
  return status;
}

bool Master::loadXmlInit(QBuffer *buffer, const QString &abs_path)
{
  if (!buffer) return false;
  QXmlStreamReader reader(buffer);
  while (!reader.atEnd() &&
         (reader.readNext() != QXmlStreamReader::StartElement ||
          reader.name().toUtf8() != "xournal")) {
  }
  if (reader.atEnd()) {
    preferences()->showErrorMessage(
        tr("Error while loading file"),
        tr("Failed to read bpr/xopp document: ") + reader.errorString());
    reader.clear();
    return false;
  }

  std::shared_ptr<PdfMaster> pdf(nullptr);
  while (reader.readNextStartElement()) {
    if (reader.name().toUtf8() == "beamerpresenter")
      readXmlHeader(reader, false, abs_path);
    else if (reader.name().toUtf8() == "page")
      pdf = readXmlPageBg(reader, pdf, abs_path);
    else if (!reader.isEndElement())
      reader.skipCurrentElement();
  }
  if (reader.hasError())
    preferences()->showErrorMessage(
        tr("Error while loading file"),
        tr("Failed to read bpr/xopp document: ") + reader.errorString());
  reader.clear();
  return true;
}

bool Master::loadXmlDrawings(QBuffer *buffer, const bool clear_drawings,
                             const QString &abs_path)
{
  if (!buffer) return false;
  debug_msg(DebugDrawing, "Loading drawings from buffer");
  QXmlStreamReader reader(buffer);
  while (!reader.atEnd() &&
         (reader.readNext() != QXmlStreamReader::StartElement ||
          reader.name().toUtf8() != "xournal")) {
  }
  if (reader.atEnd()) {
    preferences()->showErrorMessage(
        tr("Error while loading file"),
        tr("Failed to read bpr/xopp document: ") + reader.errorString());
    reader.clear();
    return false;
  }

  std::shared_ptr<PdfMaster> pdf(nullptr);
  if (!documents.isEmpty()) pdf = documents.first();
  int page = 0;
  page_idx.clear();
  page_to_slide.clear();
  while (reader.readNextStartElement()) {
    if (reader.name().toUtf8() == "beamerpresenter")
      readXmlHeader(reader, true, abs_path);
    else if (reader.name().toUtf8() == "page") {
      pdf = readXmlPage(reader, pdf, page, abs_path, clear_drawings);
      page_to_slide[page] = page_idx.size();
      page_idx.append(page);
    } else if (!reader.isEndElement())
      reader.skipCurrentElement();
  }
  if (reader.hasError())
    preferences()->showErrorMessage(
        tr("Error while loading file"),
        tr("Failed to read bpr/xopp document: ") + reader.errorString());
  reader.clear();
  return true;
}

std::shared_ptr<PdfMaster> Master::readXmlPageBg(QXmlStreamReader &reader,
                                                 std::shared_ptr<PdfMaster> pdf,
                                                 const QString &drawings_path)
{
  if (reader.name().toUtf8() != "page") return pdf;
  while (reader.readNextStartElement()) {
    if (reader.name().toUtf8() == "background") {
      const auto filename = reader.attributes().value("filename");
      if (!filename.isEmpty()) {
        const auto finfo = getAbsFile(filename.toString(), drawings_path);
        const QString abs_filename = finfo.absoluteFilePath();
        if (!pdf || (abs_filename != pdf->getFilename() &&
                     filename != pdf->getFilename())) {
          pdf = nullptr;
          for (const auto &doc : std::as_const(documents)) {
            if (doc && (doc->getFilename() == filename ||
                        doc->getFilename() == abs_filename)) {
              debug_msg(DebugDrawing,
                        "Found existing document" << doc->getFilename());
              pdf = doc;
              pdf->setDrawingsPath(drawings_path);
              break;
            }
          }
          if (!pdf) {
            pdf = createPdfMaster(abs_filename);
            pdf->setDrawingsPath(drawings_path);
            if (!pdf) qWarning() << "Document does not exist:" << abs_filename;
          }
        }
      }
      if (!reader.isEndElement()) reader.skipCurrentElement();
    }
    if (!reader.isEndElement()) reader.skipCurrentElement();
  }
  return pdf;
}

std::shared_ptr<PdfMaster> Master::readXmlPage(QXmlStreamReader &reader,
                                               std::shared_ptr<PdfMaster> pdf,
                                               int &page,
                                               const QString &abs_path,
                                               const bool clear_drawings)
{
  /// Regular expression matching exactly 2 non-digit characters at the end of a
  /// string
  static const QRegularExpression regexpr_2nondigits{"[^0-9]{2,2}$"};

  page = INT_MIN;
  if (reader.name().toUtf8() != "page") return pdf;
  while (reader.readNextStartElement()) {
    if (reader.name().toUtf8() == "background") {
      const auto attr = reader.attributes();
      const auto type = attr.value("type").toString();
      if (type == "pdf") {
        // Read page number
#if QT_VERSION_MAJOR >= 6
        auto pageno = attr.value("pageno");
#else
        auto pageno = attr.value("pageno").toString();
#endif
        // Some files created by Xournal++ have a suffix "ll" after the page
        // number.
        if (pageno.contains(regexpr_2nondigits)) pageno.chop(2);
        page = pageno.toInt() - 1;
      }
      if (page < 0)
        page = nextEmptyPage();
      else {
        // Read file name
        const QString filename = attr.value("filename").toString();
        const auto finfo = getAbsFile(filename, abs_path);
        if (finfo.isFile()) {
          const QString abs_filename = finfo.absoluteFilePath();
          if (!pdf || (abs_filename != pdf->getFilename() &&
                       filename != pdf->getFilename())) {
            pdf = nullptr;
            for (const auto &doc : std::as_const(documents)) {
              if (doc && (doc->getFilename() == filename ||
                          doc->getFilename() == abs_filename)) {
                debug_msg(DebugDrawing,
                          "Found existing document" << doc->getFilename());
                pdf = doc;
                break;
              }
            }
            if (pdf && clear_drawings) {
              pdf->clearAllDrawings();
              pdf->flags() &= ~PdfMaster::UnsavedDrawings;
            }
          }
        }
      }

      // Read per-slide time
      if (pdf && page >= 0) {
        const auto endtime = attr.value("endtime");
        if (!endtime.isEmpty()) {
          const QTime time = QTime::fromString(endtime.toString(), "h:mm:ss");
          if (time.isValid())
            pdf->targetTimes()[page] = time.msecsSinceStartOfDay();
        }
      }

      if (!reader.isEndElement()) reader.skipCurrentElement();
    } else if (reader.name().toUtf8() == "layer" && pdf) {
      pdf->readDrawingsFromStream(reader, page);
    }
    if (!reader.isEndElement()) reader.skipCurrentElement();
  }
  return pdf;
}

bool Master::readXmlHeader(QXmlStreamReader &reader, const bool read_notes,
                           const QString &abs_path)
{
  debug_msg(DebugDrawing, "Reading header");
  const QTime time = QTime::fromString(
      reader.attributes().value("duration").toString(), "h:mm:ss");
  if (time.isValid()) {
    emit setTotalTime(time);
    // If may happen that this is called before a timer widget is created.
    // Then setTotalTime(time) will do nothing and preferences()->msecs_total
    // must be set directly.
    WritableGlobalPreferences::writable()->msecs_total =
        time.msecsSinceStartOfDay();
  }
  while (reader.readNextStartElement()) {
    if (read_notes && reader.name().toUtf8() == "speakernotes")
      // TODO: multiple notes widgets with equal id (content) are not supported
      emit readNotes(reader);
    else if (reader.name().toUtf8() == "documents") {
      while (reader.readNextStartElement()) {
        if (reader.name().toUtf8() == "file") {
          const QXmlStreamAttributes &attr = reader.attributes();
          const QString alias = attr.value("alias").toString();
          const QString path = attr.value("path").toString();
          const auto finfo = getAbsFile(path, abs_path);
          const QString old_alias = preferences()->file_alias.value(alias);
          if (!alias.isEmpty() && finfo.isFile() &&
              (old_alias.isEmpty() ||
               !old_alias.endsWith(".pdf", Qt::CaseInsensitive)))
            WritableGlobalPreferences::writable()->file_alias[alias] =
                finfo.absoluteFilePath();
        }
        if (!reader.isEndElement()) reader.skipCurrentElement();
      }
    }
    if (!reader.isEndElement()) reader.skipCurrentElement();
  }
  return true;
}

void Master::loadPdfpcJSON(const QString &filename)
{
  // 1. Find and open file.
  if (documents.isEmpty() || !documents.first()) return;
  QFile file(filename);
  if (filename.isEmpty()) {
    // Special case: empty filename, used for autoload of pdfpc files.
    // Guess filename and return without warning if that file does not exist.
    file.setFileName(documents.constFirst()->getFilename() + "pc");
    if (!file.exists()) return;
  }
  if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << tr("Could not read file:") << filename;
    return;
  }
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || doc.isNull() ||
      doc.isEmpty() || !doc.isObject()) {
    qWarning() << tr("JSON file is empty or parsing failed:")
               << error.errorString();
    return;
  }
  pdfpc_file = file.fileName();

  // 2. Read properties.
  const QJsonValue duration = doc.object().value("duration");
  if (!duration.isUndefined()) {
    int ms = 60000 * duration.toInt(-1);
    if (ms == -60000) ms = 60000 * duration.toDouble(0.0);
    if (ms > 0) {
      emit setTotalTime(QTime::fromMSecsSinceStartOfDay(ms));
      // If may happen that this is called before a timer widget is created.
      // Then setTotalTime(time) will do nothing and preferences()->msecs_total
      // must be set directly.
      WritableGlobalPreferences::writable()->msecs_total = ms;
    }
  }

  // 3. Read page array.
  const QJsonArray array = doc.object().value("pages").toArray();
  if (array.isEmpty()) return;
  QMap<int, QString> labels;
  QJsonObject obj;
  int idx, intlabel;
  QString label, note;
  for (const auto &element : std::as_const(array)) {
    obj = element.toObject();
    idx = obj.value("idx").toInt(-1);
    if (idx < 0) continue;
    if (obj.value("hidden").toBool(false)) {
      const int slide = page_to_slide.value(idx, -1);
      if (slide >= 0) removeSlide(slide);
    }
    label = obj.value("label").toString();
    if (label.isEmpty()) {
      intlabel = obj.value("label").toInt(-1);
      if (intlabel > 0) label = QString::number(intlabel);
    }
    if ((!label.isEmpty() && (labels.empty() || label != labels.last())) ||
        (obj.value("overlay").toInt(-1) == 0))
      labels[idx] = label;
    note = obj.value("note").toString();
    if (!note.isEmpty()) emit sendPageNote(QString::number(idx), label, note);
  }
  if (!labels.isEmpty())
    documents.first()->getDocument()->overrideLabels(labels);
  navigateToSlide(preferences()->slide);
}
