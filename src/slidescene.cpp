// SPDX-FileCopyrightText: 2023 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#include "src/slidescene.h"

#include <QBuffer>
#include <QByteArray>
#include <QClipboard>
#include <QDataStream>
#include <QDesktopServices>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsSceneWheelEvent>
#include <QGuiApplication>
#include <QMimeData>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QRegularExpression>
#include <QString>
#include <QSvgGenerator>
#include <QSvgRenderer>
#include <QTouchEvent>
#include <QTransform>
#include <QXmlStreamWriter>
#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>

#include "src/config.h"
#include "src/drawing/arrowgraphicsitem.h"
#include "src/drawing/basicgraphicspath.h"
#include "src/drawing/dragtool.h"
#include "src/drawing/ellipsegraphicsitem.h"
#include "src/drawing/flexgraphicslineitem.h"
#include "src/drawing/fullgraphicspath.h"
#include "src/drawing/graphicspictureitem.h"
#include "src/drawing/linegraphicsitem.h"
#include "src/drawing/pathcontainer.h"
#include "src/drawing/pixmapgraphicsitem.h"
#include "src/drawing/pointingtool.h"
#include "src/drawing/rectgraphicsitem.h"
#include "src/drawing/selectiontool.h"
#include "src/drawing/shaperecognizer.h"
#include "src/drawing/texttool.h"
#include "src/log.h"
#include "src/media/mediaitem.h"
#include "src/pdfmaster.h"
#include "src/preferences.h"
#include "src/slideview.h"

SlideScene::SlideScene(std::shared_ptr<const PdfMaster> master,
                       const PagePart part, QObject *parent)
    : QGraphicsScene(parent),
      pageItem(new PixmapGraphicsItem(sceneRect())),
      master(master),
      page_part(part)
{
  debug_verbose(DebugFunctionCalls, "CREATING SlideScene" << this);
  setSceneRect(0, 0, 4000, 3000);
  connect(this, &SlideScene::sendNewPath, master.get(),
          &PdfMaster::receiveNewPath, Qt::DirectConnection);
  connect(this, &SlideScene::replacePath, master.get(), &PdfMaster::replacePath,
          Qt::DirectConnection);
  connect(this, &SlideScene::sendHistoryStep, master.get(),
          &PdfMaster::addHistoryStep, Qt::DirectConnection);
  connect(this, &SlideScene::requestNewPathContainer, master.get(),
          &PdfMaster::requestNewPathContainer, Qt::DirectConnection);
  connect(this, &SlideScene::createPathContainer, master.get(),
          &PdfMaster::createPathContainer, Qt::DirectConnection);
  connect(this, &SlideScene::sendRemovePaths, master.get(),
          &PdfMaster::removeItems, Qt::DirectConnection);
  connect(this, &SlideScene::sendAddPaths, master.get(),
          &PdfMaster::addItemsForeground, Qt::DirectConnection);
  connect(this, &SlideScene::bringToForeground, master.get(),
          &PdfMaster::bringToForeground, Qt::DirectConnection);
  connect(this, &SlideScene::bringToBackground, master.get(),
          &PdfMaster::bringToBackground, Qt::DirectConnection);
  connect(this, &SlideScene::selectionChanged, this,
          &SlideScene::updateSelectionRect, Qt::DirectConnection);
  pageItem->setZValue(-1e2);
  addItem(&selection_bounding_rect);
  addItem(pageItem);
  pageItem->show();
}

SlideScene::~SlideScene()
{
  debug_verbose(DebugFunctionCalls, "DELETING SlideScene" << this);
  delete animation;
  delete zoom_timer;
  if (searchResults) removeItem(searchResults);
  delete searchResults;
  QList<QGraphicsItem *> list = items();
  while (!list.isEmpty()) removeItem(list.takeLast());
  delete pageItem;
  delete pageTransitionItem;
  mediaItems.clear();
  delete currentlyDrawnItem;
  delete currentItemCollection;
}

void SlideScene::stopDrawing()
{
  debug_msg(DebugDrawing | DebugFunctionCalls,
            "Stop drawing" << page << page_part << currentlyDrawnItem
                           << currentItemCollection << this);
  if (currentlyDrawnItem) {
    BasicGraphicsPath *newpath = nullptr;
    switch (currentlyDrawnItem->type()) {
      case BasicGraphicsPath::Type:
      case FullGraphicsPath::Type: {
        AbstractGraphicsPath *path =
            static_cast<AbstractGraphicsPath *>(currentlyDrawnItem);
        path->finalize();
        emit sendNewPath({page, page_part}, currentlyDrawnItem);
        if (path->getTool().shape() == DrawTool::Recognize) {
          ShapeRecognizer recognizer(path);
          newpath = recognizer.recognize();
          if (newpath) {
            addItem(newpath);
            emit replacePath({page, page_part}, currentlyDrawnItem, newpath);
            currentlyDrawnItem = newpath;
          }
        }
        currentlyDrawnItem->show();
        invalidate(currentlyDrawnItem->sceneBoundingRect(),
                   QGraphicsScene::ItemLayer);
        currentlyDrawnItem = nullptr;
        break;
      }
      case RectGraphicsItem::Type:
        newpath = static_cast<RectGraphicsItem *>(currentlyDrawnItem)->toPath();
        break;
      case EllipseGraphicsItem::Type:
        newpath =
            static_cast<EllipseGraphicsItem *>(currentlyDrawnItem)->toPath();
        break;
      case LineGraphicsItem::Type:
        newpath = static_cast<LineGraphicsItem *>(currentlyDrawnItem)->toPath();
        break;
      case ArrowGraphicsItem::Type: {
        const auto newpaths =
            static_cast<ArrowGraphicsItem *>(currentlyDrawnItem)
                ->toPath<QGraphicsItem>();
        removeItem(currentlyDrawnItem);
        delete currentlyDrawnItem;
        currentlyDrawnItem = nullptr;
        if (!newpaths.isEmpty()) {
          for (auto item : newpaths) {
            addItem(item);
            item->show();
          }
          emit sendAddPaths({page, page_part}, newpaths);
        }
        break;
      }
      default:
        break;
    }
    if (currentlyDrawnItem) {
      removeItem(currentlyDrawnItem);
      delete currentlyDrawnItem;
      currentlyDrawnItem = nullptr;
      if (newpath) {
        addItem(newpath);
        newpath->show();
        emit sendNewPath({page, page_part}, newpath);
      }
      currentlyDrawnItem = nullptr;
    }
  }
  if (currentItemCollection) {
    removeItem(currentItemCollection);
    delete currentItemCollection;
    currentItemCollection = nullptr;
  }
}

bool SlideScene::event(QEvent *event)
{
  debug_verbose(DebugDrawing | DebugFunctionCalls,
                event->type() << event->spontaneous() << event << this);
  Tool::InputDevices device;
  QList<QPointF> pos;
  QPointF start_pos;
  switch (event->type()) {
    case QEvent::GraphicsSceneMousePress: {
      const auto *mouseevent = static_cast<QGraphicsSceneMouseEvent *>(event);
      device =
          Tool::mouseButtonToDevice(mouseevent->buttons()) | Tool::StartEvent;
      pos.append(mouseevent->scenePos());
      break;
    }
    case QEvent::GraphicsSceneMouseMove: {
      const auto *mouseevent = static_cast<QGraphicsSceneMouseEvent *>(event);
      device =
          Tool::mouseButtonToDevice(mouseevent->buttons()) | Tool::UpdateEvent;
      pos.append(mouseevent->scenePos());
      break;
    }
    case QEvent::GraphicsSceneMouseRelease: {
      const auto *mouseevent = static_cast<QGraphicsSceneMouseEvent *>(event);
      device =
          Tool::mouseButtonToDevice(mouseevent->button()) | Tool::StopEvent;
      pos.append(mouseevent->scenePos());
      start_pos = mouseevent->buttonDownScenePos(mouseevent->button());
      break;
    }
    case QEvent::GraphicsSceneWheel: {
      const auto wheelevent = static_cast<QGraphicsSceneWheelEvent *>(event);
      std::shared_ptr<Tool> tool = preferences()->currentTool(
          Tool::mouseButtonToDevice(wheelevent->buttons()));
      if (!tool && wheelevent->buttons() == 0)
        tool = preferences()->currentTool(Tool::MouseLeftButton);
      if (!tool || tool->tool() != Tool::DragViewTool) return false;
      event->accept();
      const auto dtool = std::static_pointer_cast<DragTool>(tool);
      if (dtool->flags().testFlag(DragTool::ScrollWheelZoom)) {
        setZoom((1.0 + 0.002 * wheelevent->delta()) * zoom,
                wheelevent->scenePos(), false);
        slide_flags |= UnrenderedZoom;
        return true;
      } else if (dtool->flags().testFlag(DragTool::ScrollWheelMove)) {
        QRectF rect = sceneRect();
        if (wheelevent->modifiers().testFlag(Qt::ShiftModifier))
          rect.translate(-0.3 * wheelevent->delta(), 0);
        else
          rect.translate(0, -0.3 * wheelevent->delta());
        setSceneRect(rect);
        return true;
      }
      break;
    }
    case QEvent::GraphicsSceneMouseDoubleClick: {
      // Double click: These events are ignored, except if a text or drag tool
      // is used.
      const auto *mouseevent = static_cast<QGraphicsSceneMouseEvent *>(event);
      std::shared_ptr<Tool> tool = preferences()->currentTool(
          Tool::mouseButtonToDevice(mouseevent->buttons()));
      if (!tool) return false;
      event->accept();
      if (tool->tool() == Tool::TextInputTool)
        return QGraphicsScene::event(event);
      if (tool->tool() == Tool::DragViewTool) {
        const auto dtool = std::static_pointer_cast<DragTool>(tool);
        if (dtool->flags().testFlag(DragTool::DoubleClickZoom)) {
          setZoom(1.25 * zoom, mouseevent->scenePos());
          return true;
        }
      }
      return false;
    }
    case QEvent::TouchBegin: {
      device = Tool::TouchInput | Tool::StartEvent;
      const auto touchevent = static_cast<QTouchEvent *>(event);
#if (QT_VERSION_MAJOR >= 6)
      for (const auto &point : touchevent->points())
        pos.append(point.scenePosition());
#else
      for (const auto &point : touchevent->touchPoints())
        pos.append(point.scenePos());
#endif
      break;
    }
    case QEvent::TouchUpdate: {
      device = Tool::TouchInput | Tool::UpdateEvent;
      const auto touchevent = static_cast<QTouchEvent *>(event);
#if (QT_VERSION_MAJOR >= 6)
      for (const auto &point : touchevent->points())
        pos.append(point.scenePosition());
#else
      for (const auto &point : touchevent->touchPoints())
        pos.append(point.scenePos());
#endif
      break;
    }
    case QEvent::TouchEnd: {
      device = Tool::TouchInput | Tool::StopEvent;
      const auto touchevent = static_cast<QTouchEvent *>(event);
#if (QT_VERSION_MAJOR >= 6)
      if (touchevent->points().size() > 0) {
        for (const auto &point : touchevent->points())
          pos.append(point.scenePosition());
        start_pos = touchevent->points().constFirst().scenePressPosition();
      }
#else
      if (touchevent->touchPoints().size() > 0) {
        for (const auto &point : touchevent->touchPoints())
          pos.append(point.scenePos());
        start_pos = touchevent->touchPoints().constFirst().startScenePos();
      }
#endif
      break;
    }
    case QEvent::TouchCancel:
      device = Tool::TouchInput | Tool::CancelEvent;
      break;
    case QEvent::Leave:
    case QEvent::DragLeave:
#if (QT_VERSION_MAJOR >= 6)
    case QEvent::GraphicsSceneLeave:
#endif
    case QEvent::GraphicsSceneHoverLeave:
    case QEvent::GraphicsSceneDragLeave:
      /* Try to clean up pointers on the slide scene when the device
       * leaves the scene. */
      for (const auto &tool : std::as_const(preferences()->current_tools)) {
        if (tool && tool->tool() & Tool::AnyPointingTool) {
          auto ptool = std::dynamic_pointer_cast<PointingTool>(tool);
          if (ptool) ptool->clearPos();
        }
      }
    default:
      debug_verbose(DebugFunctionCalls,
                    "complete event handling" << event << this);
      return QGraphicsScene::event(event);
  }
  if (handleEvents(device, pos, start_pos, 1.)) {
    event->accept();
    debug_verbose(DebugFunctionCalls,
                  "complete event handling" << event << this);
    return true;
  }
  debug_verbose(DebugFunctionCalls, "complete event handling" << event << this);
  return QGraphicsScene::event(event);
}

bool SlideScene::handleEvents(const Tool::InputDevices device,
                              const QList<QPointF> &pos,
                              const QPointF &start_pos, const float pressure)
{
  debug_verbose(DebugFunctionCalls,
                device << pos << start_pos << pressure << this);
  if (pos.size() < 1) return false;

  std::shared_ptr<Tool> tool =
      preferences()->currentTool(device & Tool::AnyDevice);
  // Check if a selection is active. In this case we might use the temporary
  // selection tool.
  if (selection_bounding_rect.isVisible() &&
      (!tool || tool->tool() & Tool::AnyDrawTool ||
       tool->tool() == Tool::NoTool)) {
    if (tmp_selection_tool &&
        tmp_selection_tool->device() == (device & ~Tool::AnyEvent))
      tool = tmp_selection_tool;
    else if ((device & Tool::AnyEvent) == Tool::StartEvent &&
             maybeStartSelectionEvent(pos.first(), (device & ~Tool::AnyEvent)))
      return true;
  }
  // Handle events without any tool.
  if (!tool || tool->tool() == Tool::NoTool) {
    if (pos.size() == 1 && (device & Tool::AnyEvent) == Tool::StopEvent)
      noToolClicked(pos.constFirst(), start_pos);
    return true;
  }

  debug_verbose(DebugDrawing,
                "Handling event" << tool->tool() << tool->device() << device);
  // Handle draw tool events.
  if (tool->tool() & Tool::AnyDrawTool)
    handleDrawEvents(std::dynamic_pointer_cast<const DrawTool>(tool), device,
                     pos, pressure);
  // Handle pointing tool events.
  else if (tool->tool() & Tool::AnyPointingTool)
    handlePointingEvents(std::dynamic_pointer_cast<PointingTool>(tool), device,
                         pos);
  // Handle selection tool events.
  else if (tool->tool() & Tool::AnySelectionTool)
    handleSelectionEvents(std::dynamic_pointer_cast<SelectionTool>(tool),
                          device, pos, start_pos);
  // Handle text tool events.
  // Here we detect start events and not stop events, because otherwise touch
  // events will be discarded: If a touch start event is not handled, the whole
  // touch event is treated as a mouse event.
  else if (tool->tool() == Tool::TextInputTool &&
           (device & Tool::AnyEvent) == Tool::StartEvent && pos.size() == 1)
    return handleTextEvents(std::dynamic_pointer_cast<const TextTool>(tool),
                            device, pos);
  else if (tool->tool() == Tool::DragViewTool)
    return handleDragView(std::dynamic_pointer_cast<DragTool>(tool), device,
                          pos, start_pos);
  // Fall back to no tool.
  else if ((device & Tool::AnyEvent) == Tool::StopEvent && pos.size() == 1)
    noToolClicked(pos.constFirst(), start_pos);
  else
    return false;
  return true;
}

bool SlideScene::handleDragView(std::shared_ptr<DragTool> tool,
                                const Tool::InputDevices device,
                                const QList<QPointF> &pos,
                                const QPointF &start_pos)
{
  if (!tool || pos.size() != 1) return false;
  if ((device & Tool::AnyEvent) == Tool::StartEvent) {
    debug_msg(DebugOtherInput, "Start drag view");
    tool->setReference(pos.constFirst());
  } else {
    if ((slide_flags & UnrenderedZoom) &&
        ((device & Tool::AnyEvent) == Tool::StopEvent))
      setZoom(zoom);
    const QPointF delta = tool->dragTo(
        pos.constFirst(), (device & Tool::AnyEvent) == Tool::StopEvent);
    if (delta.isNull()) return false;
    debug_msg(DebugOtherInput, "drag view" << delta);
    QRectF rect = sceneRect();
    rect.translate(delta);
    setSceneRect(rect);
  }
  return true;
}

bool SlideScene::maybeStartSelectionEvent(
    const QPointF &pos, const Tool::InputDevices device) noexcept
{
  debug_verbose(DebugFunctionCalls, pos << device << this);
  // Try to handle selection (if available)
  if (device == Tool::TabletHover || !selection_bounding_rect.isVisible())
    return false;

  // 1. Check if the user clicked on some special point on the
  // bounding rect of the selection.
  const QPolygonF selection_rect = selection_bounding_rect.scaleHandles();
  const qreal handle_size = preferences()->selection_rect_handle_size;
  for (const auto &point : selection_rect)
    if ((pos - point).manhattanLength() <= handle_size) {
      initTmpSelectionTool(device);
      tmp_selection_tool->startScaling(point,
                                       selection_bounding_rect.sceneCenter());
      return true;
    }
  if ((pos - selection_bounding_rect.sceneRotationHandle()).manhattanLength() <=
      handle_size) {
    initTmpSelectionTool(device);
    tmp_selection_tool->startRotation(pos,
                                      selection_bounding_rect.sceneCenter());
    return true;
  }
  if ((pos - selection_bounding_rect.sceneDeleteHandle()).manhattanLength() <=
      handle_size) {
    removeSelection();
    return true;
  }
  // 2. Check if the user clicked on a selected object.
  /* Option 1: Every click in the bounding rect activates dragging. */
  if (selection_bounding_rect.containsPoint(pos)) {
    initTmpSelectionTool(device);
    tmp_selection_tool->startMove(pos);
    return true;
  }
  // */
  /* Option 2: Selection is only dragged when clicking directly on an object.
  selection.pop_back();
  for (auto item : selection)
      if (item->contains(item->mapFromScene(pos)))
      {
          initTmpSelectionTool(device);
          tmp_selection_tool->startMove(pos);
          return;
      }
  // */
  return false;
}

void SlideScene::handleDrawEvents(std::shared_ptr<const DrawTool> tool,
                                  const Tool::InputDevices device,
                                  const QList<QPointF> &pos,
                                  const float pressure)
{
  debug_verbose(DebugFunctionCalls, tool.get()
                                        << device << pos << pressure << this);
  // TODO: multi-touch for draw tools
  switch (device & Tool::AnyEvent) {
    case Tool::UpdateEvent:
      stepInputEvent(tool, pos.constFirst(), pressure);
      break;
    case Tool::StartEvent:
      startInputEvent(tool, pos.constFirst(), pressure);
      break;
    case Tool::StopEvent:
      stopInputEvent(tool);
      break;
    case Tool::CancelEvent:
      if (stopInputEvent(tool)) {
        PathContainer *container = master->pathContainer({page, page_part});
        if (container) container->undo(this);
        break;
      }
  }
}

void SlideScene::handlePointingEvents(std::shared_ptr<PointingTool> tool,
                                      const Tool::InputDevices device,
                                      const QList<QPointF> &pos)
{
  debug_verbose(DebugFunctionCalls, tool.get() << device << pos << this);
  tool->setScene(this);
  switch (tool->tool()) {
    case Tool::Eraser: {
      PathContainer *container = master->pathContainer({page, page_part});
      if (container) {
        switch (device & Tool::AnyEvent) {
          case Tool::UpdateEvent:
            for (const QPointF &point : pos)
              container->eraserMicroStep(point, tool->size());
            break;
          case Tool::StartEvent:
            container->startMicroStep();
            for (const QPointF &point : pos)
              container->eraserMicroStep(point, tool->size());
            break;
          case Tool::StopEvent:
            if (container->applyMicroStep()) emit newUnsavedDrawings();
            break;
          case Tool::CancelEvent:
            if (container->applyMicroStep()) {
              container->undo(this);
              emit newUnsavedDrawings();
            }
            break;
        }
      }
      if (tool->scale() <= 0.) break;
    }
    default: {
      if ((device & Tool::AnyEvent) == Tool::StopEvent &&
          !(tool->device() & (Tool::TabletHover | Tool::MouseNoButton)))
        tool->clearPos();
      else
        tool->setPos(pos);
      break;
    }
  }
}

void SlideScene::handleSelectionEvents(std::shared_ptr<SelectionTool> tool,
                                       const Tool::InputDevices device,
                                       const QList<QPointF> &pos,
                                       const QPointF &start_pos)
{
  debug_verbose(DebugFunctionCalls, tool.get()
                                        << device << pos << start_pos << this);
  const QPointF &single_pos = pos.constFirst();
  switch (device & Tool::AnyEvent) {
    case Tool::StartEvent:
      handleSelectionStartEvents(tool, single_pos);
      break;
    case Tool::UpdateEvent:
      tool->liveUpdate(single_pos);
      // TODO: select area for higher efficiency
      invalidate(QRectF(), QGraphicsScene::ForegroundLayer);
      break;
    case Tool::StopEvent:
      handleSelectionStopEvents(tool, single_pos, start_pos);
      invalidate(QRectF(), QGraphicsScene::ForegroundLayer);
      break;
  }
}

bool SlideScene::handleTextEvents(std::shared_ptr<const TextTool> tool,
                                  const Tool::InputDevices device,
                                  const QList<QPointF> &pos)
{
  debug_verbose(DebugFunctionCalls, tool.get() << device << pos << this);
  // TODO: bug fixes
  clearSelection();
  debug_msg(DebugDrawing, "Trying to start writing text"
                              << (device & Tool::AnyDevice) << focusItem());
  for (auto item :
       static_cast<const QList<QGraphicsItem *>>(items(pos.constFirst())))
    if (item->type() == TextGraphicsItem::Type) {
      setFocusItem(item);
      return false;
    }
  setFocusItem(nullptr);
  TextGraphicsItem *item = new TextGraphicsItem();
  item->setFont(QFont(tool->font()));
  item->setDefaultTextColor(tool->color());
  addItem(item);
  item->show();
  item->setPos(pos.constFirst());
  PathContainer *container{nullptr};
  emit createPathContainer(&container, {page, page_part});
  if (container) {
    item->setZValue(container->topZValue() + 10);
    connect(item, &TextGraphicsItem::removeMe, container,
            &PathContainer::removeItem);
    connect(item, &TextGraphicsItem::addMe, container,
            &PathContainer::addTextItem);
  } else {
    qWarning() << "Failed to get graph container";
    item->setZValue(10);
  }
  emit sendNewPath({page, page_part}, item);
  setFocusItem(item);
  return true;
}

void SlideScene::handleSelectionStartEvents(std::shared_ptr<SelectionTool> tool,
                                            const QPointF &pos)
{
  debug_verbose(DebugFunctionCalls, tool.get() << pos << this);
  QList<QGraphicsItem *> selection = selectedItems();
  // Check if anything is selected.
  tool->reset();
  if (!selection.isEmpty()) {
    selection.append(&selection_bounding_rect);
    tool->initTransformations(selection);
    // 1. Check if the user clicked on some special point on the
    // bounding rect of the selection.
    const QPolygonF selection_rect = selection_bounding_rect.scaleHandles();
    const qreal handle_size = preferences()->selection_rect_handle_size;
    for (const auto &point : selection_rect)
      if ((pos - point).manhattanLength() <= handle_size) {
        tool->startScaling(point, selection_bounding_rect.sceneCenter());
        return;
      }
    if ((pos - selection_bounding_rect.sceneRotationHandle())
            .manhattanLength() <= handle_size) {
      tool->startRotation(pos, selection_bounding_rect.sceneCenter());
      return;
    }
    if ((pos - selection_bounding_rect.sceneDeleteHandle()).manhattanLength() <=
        handle_size) {
      removeSelection();
      return;
    }
    // 2. Check if the user clicked on a selected object.
    /* Option 1: Every click in the bounding rect activates dragging. */
    if (selection_bounding_rect.containsPoint(pos)) {
      tool->startMove(pos);
      return;
    }
    // */
    /* Option 2: Selection is only dragged when clicking directly on an object.
    selection.pop_back();
    for (auto item : selection)
        if (item->contains(item->mapFromScene(pos)))
        {
            tool->startMove(pos);
            return;
        }
    // */
    selection.clear();
  }
  if (tool->type() == SelectionTool::NoOperation) {
    clearSelection();
    setFocusItem(nullptr);
    switch (tool->tool()) {
      case Tool::BasicSelectionTool: {
        QGraphicsItem *item = itemAt(pos, QTransform());
        if (!(item && item->flags().testFlag(QGraphicsItem::ItemIsSelectable)))
          break;
        item->setSelected(true);
        selection.append(item);
        selection.append(&selection_bounding_rect);
        tool->initTransformations(selection);
        tool->startMove(pos);
        break;
      }
      case Tool::RectSelectionTool:
        tool->startRectSelection(pos, this);
        break;
      case Tool::FreehandSelectionTool:
        tool->startFreehandSelection(pos, this);
        break;
      default:
        break;
    }
  }
}

void SlideScene::handleSelectionStopEvents(std::shared_ptr<SelectionTool> tool,
                                           const QPointF &pos,
                                           const QPointF &start_pos)
{
  debug_verbose(DebugFunctionCalls, tool.get() << pos << start_pos << this);
  switch (tool->type()) {
    case SelectionTool::Move:
    case SelectionTool::Rotate:
    case SelectionTool::Resize: {
      const QHash<QGraphicsItem *, QTransform> &originalTransforms =
          tool->originalTransforms();
      if (originalTransforms.count() <= 1) {
        if (tool == tmp_selection_tool) tmp_selection_tool = nullptr;
        return;
      }
      const bool finalize =
          preferences()->global_flags & Preferences::FinalizeDrawnPaths;
      QTransform transform;
      std::map<QGraphicsItem *, QTransform> transforms;
      for (auto it = originalTransforms.cbegin();
           it != originalTransforms.cend(); ++it) {
        if (it.key() == &selection_bounding_rect) continue;
        transform = it.key()->transform();
        transform *= it->inverted();
        transforms.insert({it.key(), transform});
        if (finalize && (it.key()->type() == BasicGraphicsPath::Type ||
                         it.key()->type() == FullGraphicsPath::Type))
          static_cast<AbstractGraphicsPath *>(it.key())->finalize();
      }
      emit sendHistoryStep({page, page_part}, &transforms, nullptr, nullptr);
      updateSelectionRect();
      break;
    }
    case SelectionTool::SelectRect: {
      tool->liveUpdate(pos);
      QPainterPath path;
      path.addPolygon(tool->polygon());
      setSelectionArea(path, Qt::ReplaceSelection,
                       Qt::ContainsItemBoundingRect);
      break;
    }
    case SelectionTool::SelectPolygon: {
      tool->liveUpdate(pos);
      QPainterPath path;
      path.addPolygon(tool->polygon());
      /* It should be possible to select all items inside path with the
       * following commented line of code. However, because QTBUG-74935 is not
       * fixed (since 3 years!), we need a workaround. */
      // setSelectionArea(path, Qt::ReplaceSelection, Qt::ContainsItemShape);
      clearSelection();
      setFocusItem(nullptr);
      const auto intersect_items =
          items(path.boundingRect(), Qt::IntersectsItemBoundingRect);
      for (QGraphicsItem *item : intersect_items) {
        if (path.contains(item->mapToScene(item->shape())))
          item->setSelected(true);
      }
      break;
    }
    default:
      break;
  }
  if (tool == tmp_selection_tool)
    tmp_selection_tool = nullptr;
  else
    tool->reset();
}

void SlideScene::renderZoom()
{
  const auto all_views = views();
  for (auto view : all_views) {
    auto *sview = dynamic_cast<SlideView *>(view);
    if (sview) sview->setZoom(zoom, true);
  }
}

void SlideScene::setZoom(const qreal new_zoom, const bool render)
{
  const QRectF rect = sceneRect();
  setZoom(new_zoom, rect.center(), render);
}

void SlideScene::setZoom(const qreal new_zoom, const QPointF reference,
                         const bool render)
{
  if (zoom_timer) zoom_timer->stop();
  const QRectF rect = sceneRect();
  const qreal shift = 1 - zoom / new_zoom;
  setSceneRect(rect.x() + shift * (reference.x() - rect.x()),
               rect.y() + shift * (reference.y() - rect.y()),
               zoom / new_zoom * rect.width(), zoom / new_zoom * rect.height());
  zoom = new_zoom;
  const auto all_views = views();
  for (auto view : all_views) {
    auto *sview = dynamic_cast<SlideView *>(view);
    if (sview) sview->setZoom(zoom, render);
  }
  if (render) {
    if (zoom_timer) {
      delete zoom_timer;
      zoom_timer = nullptr;
    }
  } else {
    if (!zoom_timer) {
      zoom_timer = new QTimer(this);
      connect(zoom_timer, &QTimer::timeout, this, &SlideScene::renderZoom);
    }
    zoom_timer->start(250);
  }
}

void SlideScene::resetView()
{
  if (page_part == RightHalf)
    setSceneRect(page_size.width(), 0., page_size.width(), page_size.height());
  else
    setSceneRect(0., 0., page_size.width(), page_size.height());
  if (zoom != 1.0) {
    zoom = 1.0;
    const auto all_views = views();
    for (auto view : all_views) {
      auto *sview = dynamic_cast<SlideView *>(view);
      if (sview) sview->setZoom(1.0);
    }
  }
}

void SlideScene::receiveAction(const Action action)
{
  debug_verbose(DebugFunctionCalls, action << this);
  debug_msg(DebugKeyInput, "SlideScene received action" << action);
  switch (action) {
    case ScrollDown: {
      QRectF rect = sceneRect();
      rect.translate(0, 0.2 * rect.height());
      setSceneRect(rect);
      break;
    }
    case ScrollUp: {
      QRectF rect = sceneRect();
      rect.translate(0, -0.2 * rect.height());
      setSceneRect(rect);
      break;
    }
    case ScrollNormal:
      resetView();
      break;
    case ZoomIn:
      setZoom(1.25 * zoom);
      break;
    case ZoomOut:
      setZoom(0.8 * zoom);
      break;
    case ZoomReset:
      setZoom(1.0);
      break;
    case PauseMedia:
      pauseMedia();
      break;
    case PlayMedia:
      playMedia();
      break;
    case PlayPauseMedia:
      playPauseMedia();
      break;
    case Mute:
      for (const auto &m : std::as_const(mediaItems)) m->setMuted(true);
      break;
    case Unmute:
      if (!(slide_flags & SlideFlag::MuteSlide))
        for (const auto &m : std::as_const(mediaItems))
          if (!(m->flags() & MediaAnnotation::Mute)) m->setMuted(false);
      break;
    case CopyClipboard:
      if (slide_flags & ShowDrawings && hasFocus()) copyToClipboard();
      break;
    case CutClipboard:
      if (slide_flags & ShowDrawings && hasFocus()) {
        copyToClipboard();
        removeSelection();
      }
      break;
    case RemoveSelectedItems:
      if (slide_flags & ShowDrawings && hasFocus()) removeSelection();
      break;
    case PasteClipboard:
      if (slide_flags & ShowDrawings && hasFocus()) pasteFromClipboard();
      break;
    case SelectionToForeground:
      if (slide_flags & ShowDrawings && hasFocus()) {
        const QList<QGraphicsItem *> selection = selectedItems();
        if (!selection.empty())
          emit bringToForeground({page, page_part}, selection);
      }
      break;
    case SelectionToBackground:
      if (slide_flags & ShowDrawings && hasFocus()) {
        const QList<QGraphicsItem *> selection = selectedItems();
        if (!selection.empty())
          emit bringToBackground({page, page_part}, selection);
      }
      break;
    case SelectAll:
      if (slide_flags & ShowDrawings && hasFocus()) {
        const auto all_items = items();
        for (const auto item : all_items)
          if (item->flags() & QGraphicsItem::ItemIsSelectable)
            item->setSelected(true);
      }
      break;
    case ClearSelection:
      clearSelection();
      setFocusItem(nullptr);
      break;
    case PdfFilesChanged:
      mediaItems.clear();
      break;
    default:
      break;
  }
}

void SlideScene::prepareNavigationEvent(const int newslide, const int newpage)
{
  debug_verbose(DebugFunctionCalls, newslide << newpage << this);
  if (newpage < 0) {
    pageItem->clearPixmaps();
    return;
  }
  // Adjust scene size.
  /// Page size in points.
  page_size = master->getPageSize(master->overlaysShiftedPage(newpage, shift));
  if (page_part != FullPage) page_size.rwidth() /= 2;
  debug_verbose(
      DebugPageChange,
      newpage << page_size << master->getDocument()->flexiblePageSizes());
  if (fixed_page_height > 0) page_size.setHeight(fixed_page_height);
  // Don't do anything if page size ist not valid. This avoids cleared slide
  // scenes which could mess up the layout and invalidate cache.
  if ((page_size.isNull() || !page_size.isValid()) &&
      !master->getDocument()->flexiblePageSizes()) {
    pageItem->clearPixmaps();
    return;
  }
  resetView();
}

void SlideScene::navigationEvent(const int newslide, const int newpage,
                                 SlideScene *newscene)
{
  debug_msg(DebugPageChange | DebugFunctionCalls,
            "scene" << this << "navigates to" << newpage << "as" << newscene);
  pauseMedia();
  clearSelection();
  setFocusItem(nullptr);
  if (pageTransitionItem) {
    removeItem(pageTransitionItem);
    delete pageTransitionItem;
    pageTransitionItem = nullptr;
  }
  if (animation) {
    animation->stop();
    delete animation;
    animation = nullptr;
  }
  pageItem->setOpacity(1.);
  pageItem->setRect(sceneRect());
  pageItem->trackNew();
  if ((!newscene || newscene == this) && page != newpage &&
      (slide_flags & ShowTransitions)) {
    SlideTransition transition =
        master->transition(newpage > page ? newpage : page);
    transitionType = transition.type;
    if (transition.type > 0 && transition.duration > 1e-3) {
      if (newpage < page) transition.invert();
      debug_msg(DebugTransitions,
                "Transition:" << transition.type << transition.duration
                              << transition.properties << transition.angle
                              << transition.scale);
      startTransition(newpage, transition);
      return;
    }
  }
  page = newpage;
  emit navigationToViews(page, newscene ? newscene : this);
  QList<QGraphicsItem *> list = items();
  while (!list.isEmpty()) removeItem(list.takeLast());
  if (!newscene || newscene == this) {
    addItem(pageItem);
    loadMedia(page);
    if (slide_flags & ShowDrawings) {
      PathContainer *paths{nullptr};
      emit requestNewPathContainer(&paths, {page, page_part});
      if (paths)
        for (auto path : *paths) addItem(path);
    }
    if (slide_flags & ShowSearchResults) updateSearchResults();
  }
  invalidate();
  emit finishTransition();
}

void SlideScene::loadMedia(const int page)
{
  if (!(slide_flags & LoadMedia)) return;
  const QList<std::shared_ptr<MediaAnnotation>> list =
      master->getDocument()->annotations(page);
  for (const auto &annotation : list) {
    debug_msg(DebugMedia,
              "loading media" << annotation->type() << annotation->rect());
    std::shared_ptr<MediaItem> &item = getMediaItem(annotation, page);
    if (item->asQGraphicsItem()->scene() != this)
      addItem(item->asQGraphicsItem());
    item->asQGraphicsItem()->setZValue(0);
    item->show();
    if ((slide_flags & AutoplayVideo) &&
        (item->flags() & MediaAnnotation::Autoplay))
      item->play();
  }
}

void SlideScene::postRendering()
{
  debug_verbose(DebugFunctionCalls, this);
  pageItem->clearOld();
  int newpage =
      page + 1;  ///< newpage is the next page after the currently shown page.
  if (shift.overlay != ShiftOverlays::NoOverlay)
    newpage = master->getDocument()->overlaysShifted(page, {1, shift.overlay});
  if (slide_flags & CacheVideos) cacheMedia(newpage);
  // Clean up media
  if (mediaItems.size() > 2) {
    debug_verbose(DebugMedia, "Start cleaning up media" << mediaItems.size());
    for (auto &media : mediaItems) {
      if (!media->pages().empty()) {
        // Delete media if the media item is not shown on any of these pages:
        // page (currently shown), newpage, page-1, page-2
        const auto it = media->pages().lower_bound(page);
        if ((it != media->pages().end() && (*it == page || *it == newpage)) ||
            (it != media->pages().begin() && *(std::prev(it)) >= page - 2))
          continue;
      }
      media->deleteProvider();
    }
  }
}

void SlideScene::cacheMedia(const int page)
{
  debug_verbose(DebugFunctionCalls, page << this);
  const QList<std::shared_ptr<MediaAnnotation>> list =
      master->getDocument()->annotations(page);
  for (const auto &annotation : list) {
#if (QT_VERSION_MAJOR >= 6)
    getMediaItem(annotation, page);
#else
    // workaround for strange behavior in Qt 5
    auto &item = getMediaItem(annotation, page);
    item->asQGraphicsItem()->setZValue(-1e9);
    addItem(item->asQGraphicsItem());
#endif
  }
}

std::shared_ptr<MediaItem> &SlideScene::getMediaItem(
    std::shared_ptr<MediaAnnotation> annotation, const int page)
{
  debug_verbose(DebugFunctionCalls, annotation.get() << page << this);
  for (auto &mediaitem : mediaItems) {
    if (*mediaitem->annotation() == *annotation) {
      mediaitem->insertPage(page);
      mediaitem->initializeProvider();
      mediaitem->setMuted(
          (slide_flags & SlideFlag::MuteSlide) ||
          (preferences()->global_flags & Preferences::MuteApplication));
      return mediaitem;
    }
  }
  debug_msg(DebugMedia, "creating new media item" << page);
  const auto mediaitem = MediaItem::fromAnnotation(annotation, page);
  mediaItems.append(mediaitem);
  mediaitem->setMuted(
      (slide_flags & SlideFlag::MuteSlide) ||
      (preferences()->global_flags & Preferences::MuteApplication));
  return mediaItems.last();
}

void SlideScene::startTransition(const int newpage,
                                 const SlideTransition &transition)
{
  debug_verbose(DebugFunctionCalls, newpage << transition.type << this);
  pageTransitionItem = new PixmapGraphicsItem(sceneRect());
  for (const auto view : static_cast<const QList<QGraphicsView *>>(views()))
    static_cast<SlideView *>(view)->prepareTransition(pageTransitionItem);
  page = newpage;
  PixmapGraphicsItem *oldPage = pageTransitionItem;
  if ((transition.type == SlideTransition::Fly ||
       transition.type == SlideTransition::FlyRectangle) &&
      !views().isEmpty() &&
      (transition.properties & SlideTransition::Outwards) == 0) {
    pageTransitionItem = new PixmapGraphicsItem(sceneRect());
    connect(pageTransitionItem, &QObject::destroyed, oldPage,
            &PixmapGraphicsItem::deleteLater);
    oldPage->setZValue(1e9);
  } else
    emit navigationToViews(page, this);
  pageTransitionItem->setMaskType(PixmapGraphicsItem::OtherMask);
  pageTransitionItem->setZValue(1e10);
  debug_msg(DebugTransitions,
            "transition:" << transition.type << transition.duration
                          << transition.angle << transition.properties);
  QList<QGraphicsItem *> list = items();
  QGraphicsItem *item;
  while (!list.isEmpty()) {
    item = list.takeLast();
    if (item != pageItem) removeItem(item);
  }
  pageItem->setOpacity(1.);
  if (pageItem->scene() != this) addItem(pageItem);
  loadMedia(page);
  if (slide_flags & ShowDrawings) {
    PathContainer *paths{nullptr};
    emit requestNewPathContainer(&paths, {page, page_part});
    if (paths)
      for (const auto path : *paths) addItem(path);
  }
  delete animation;
  animation = nullptr;
  switch (transition.type) {
    case SlideTransition::Split:
      createSplitTransition(transition, pageTransitionItem);
      break;
    case SlideTransition::Blinds:
      createBlindsTransition(transition, pageTransitionItem);
      break;
    case SlideTransition::Box:
      createBoxTransition(transition, pageTransitionItem);
      break;
    case SlideTransition::Wipe:
      createWipeTransition(transition, pageTransitionItem);
      break;
    case SlideTransition::Dissolve: {
      pageTransitionItem->setOpacity(0.);
      QPropertyAnimation *propanim =
          new QPropertyAnimation(pageTransitionItem, "opacity");
      propanim->setDuration(1000 * transition.duration);
      propanim->setStartValue(1.);
      propanim->setEndValue(0.);
      animation = propanim;
      break;
    }
    case SlideTransition::Glitter: {
      pageTransitionItem->setMaskType(PixmapGraphicsItem::Glitter);
      QPropertyAnimation *propanim =
          new QPropertyAnimation(pageTransitionItem, "progress");
      propanim->setDuration(1000 * transition.duration);
      propanim->setStartValue(PixmapGraphicsItem::glitter_number);
      propanim->setEndValue(0);
      propanim->setEasingCurve(QEasingCurve::InOutSine);
      animation = propanim;
      break;
    }
    case SlideTransition::Fly:
    case SlideTransition::FlyRectangle:
      createFlyTransition(transition, pageTransitionItem, oldPage);
      break;
    case SlideTransition::Push:
      createPushTransition(transition, pageTransitionItem);
      break;
    case SlideTransition::Cover:
      createCoverTransition(transition, pageTransitionItem);
      break;
    case SlideTransition::Uncover:
      createUncoverTransition(transition, pageTransitionItem);
      break;
    case SlideTransition::Fade:
      createFadeTransition(transition, pageTransitionItem);
      break;
    default:
      break;
  }
  if (animation) {
    connect(animation, &QAbstractAnimation::finished, this,
            &SlideScene::endTransition);
    addItem(pageTransitionItem);
    animation->start(QAbstractAnimation::KeepWhenStopped);
  }
}

void SlideScene::createSplitTransition(const SlideTransition &transition,
                                       PixmapGraphicsItem *pageTransitionItem)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << this);
  const bool outwards = transition.properties & SlideTransition::Outwards;
  pageTransitionItem->setMaskType(outwards
                                      ? PixmapGraphicsItem::NegativeClipping
                                      : PixmapGraphicsItem::PositiveClipping);
  QPropertyAnimation *propanim =
      new QPropertyAnimation(pageTransitionItem, "mask");
  propanim->setDuration(1000 * transition.duration);
  QRectF rect = sceneRect();
  if (outwards)
    propanim->setEndValue(rect);
  else
    propanim->setStartValue(rect);
  if (transition.properties & SlideTransition::Vertical) {
    rect.moveTop(rect.top() + rect.height() / 2);
    rect.setHeight(0.);
  } else {
    rect.moveLeft(rect.left() + rect.width() / 2);
    rect.setWidth(0.);
  }
  if (outwards) {
    propanim->setStartValue(rect);
    pageTransitionItem->setMask(rect);
  } else
    propanim->setEndValue(rect);
  animation = propanim;
}

void SlideScene::createBlindsTransition(const SlideTransition &transition,
                                        PixmapGraphicsItem *pageTransitionItem)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << this);
  const bool vertical = transition.properties & SlideTransition::Vertical;
  pageTransitionItem->setMaskType(vertical
                                      ? PixmapGraphicsItem::VerticalBlinds
                                      : PixmapGraphicsItem::HorizontalBlinds);
  QPropertyAnimation *propanim =
      new QPropertyAnimation(pageTransitionItem, "mask");
  propanim->setDuration(1000 * transition.duration);
  QRectF rect = sceneRect();
  if (vertical)
    rect.setWidth(rect.width() / PixmapGraphicsItem::blinds_number_v);
  else
    rect.setHeight(rect.height() / PixmapGraphicsItem::blinds_number_h);
  propanim->setStartValue(rect);
  if (vertical)
    rect.setWidth(0);
  else
    rect.setHeight(0);
  propanim->setEndValue(rect);
  animation = propanim;
}

void SlideScene::createBoxTransition(const SlideTransition &transition,
                                     PixmapGraphicsItem *pageTransitionItem)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << this);
  const bool outwards = transition.properties & SlideTransition::Outwards;
  pageTransitionItem->setMaskType(outwards
                                      ? PixmapGraphicsItem::NegativeClipping
                                      : PixmapGraphicsItem::PositiveClipping);
  QPropertyAnimation *propanim =
      new QPropertyAnimation(pageTransitionItem, "mask");
  propanim->setDuration(1000 * transition.duration);
  QRectF rect = sceneRect();
  if (outwards)
    propanim->setEndValue(rect);
  else
    propanim->setStartValue(rect);
  rect.moveTopLeft(rect.center());
  rect.setSize({0, 0});
  if (outwards) {
    propanim->setStartValue(rect);
    pageTransitionItem->setMask(rect);
  } else
    propanim->setEndValue(rect);
  animation = propanim;
}

void SlideScene::createWipeTransition(const SlideTransition &transition,
                                      PixmapGraphicsItem *pageTransitionItem)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << this);
  QPropertyAnimation *propanim =
      new QPropertyAnimation(pageTransitionItem, "mask");
  pageTransitionItem->setMaskType(PixmapGraphicsItem::PositiveClipping);
  propanim->setDuration(1000 * transition.duration);
  QRectF rect = sceneRect();
  pageTransitionItem->setMask(rect);
  propanim->setStartValue(rect);
  switch (transition.angle) {
    case 90:
      rect.setBottom(rect.top() + 1);
      break;
    case 180:
      rect.setRight(rect.left() + 1);
      break;
    case 270:
      rect.setTop(rect.bottom() - 1);
      break;
    default:
      rect.setLeft(rect.right() - 1);
      break;
  }
  propanim->setEndValue(rect);
  animation = propanim;
}

void SlideScene::createFlyTransition(const SlideTransition &transition,
                                     PixmapGraphicsItem *pageTransitionItem,
                                     PixmapGraphicsItem *oldPage)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << oldPage << this);
  const bool outwards = transition.properties & SlideTransition::Outwards;
  for (const auto &view : static_cast<const QList<QGraphicsView *>>(views())) {
    SlideView *slideview = static_cast<SlideView *>(view);
    slideview->pageChangedBlocking(page, this);
    slideview->prepareFlyTransition(outwards, oldPage, pageTransitionItem);
  }
  if (!outwards) addItem(oldPage);
  pageItem->setZValue(-1e4);

  QPropertyAnimation *propanim =
      new QPropertyAnimation(pageTransitionItem, "x");
  propanim->setDuration(1000 * transition.duration);
  switch (transition.angle) {
    case 90:
      propanim->setPropertyName("y");
      propanim->setStartValue(outwards ? 0. : sceneRect().height());
      propanim->setEndValue(outwards ? -sceneRect().height() : 0.);
      break;
    case 180:
      propanim->setStartValue(outwards ? 0. : sceneRect().width());
      propanim->setEndValue(outwards ? -sceneRect().width() : 0.);
      break;
    case 270:
      propanim->setPropertyName("y");
      propanim->setStartValue(outwards ? 0. : -sceneRect().height());
      propanim->setEndValue(outwards ? sceneRect().height() : 0.);
      break;
    default:
      propanim->setStartValue(outwards ? 0. : -sceneRect().width());
      propanim->setEndValue(outwards ? sceneRect().width() : 0.);
      break;
  }
  propanim->setEasingCurve(outwards ? QEasingCurve::InSine
                                    : QEasingCurve::OutSine);
  animation = propanim;
}

void SlideScene::createPushTransition(const SlideTransition &transition,
                                      PixmapGraphicsItem *pageTransitionItem)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << this);
  /* TODO: For push transitions the new page is not ready when
   * the animation starts. Instead of the new page, first the old
   * page is shown where the new page is expected. However, this
   * is usually only noted when the window geometry does not match
   * the slide geometry. */
  QPropertyAnimation *propanim = new QPropertyAnimation(this, "sceneRect");
  propanim->setDuration(1000 * transition.duration);
  pageTransitionItem->setZValue(-1e3);
  QRectF movedrect = sceneRect();
  switch (transition.angle) {
    case 90:
      movedrect.moveBottom(movedrect.top());
      break;
    case 180:
      movedrect.moveRight(movedrect.left());
      break;
    case 270:
      movedrect.moveTop(movedrect.bottom());
      break;
    default:
      movedrect.moveLeft(movedrect.right());
      break;
  }
  pageTransitionItem->setRect(movedrect);
  propanim->setStartValue(movedrect);
  propanim->setEndValue(sceneRect());
  propanim->setEasingCurve(QEasingCurve::InOutSine);
  animation = propanim;
}

void SlideScene::createCoverTransition(const SlideTransition &transition,
                                       PixmapGraphicsItem *pageTransitionItem)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << this);
  QParallelAnimationGroup *groupanim = new QParallelAnimationGroup();
  QPropertyAnimation *sceneanim =
      new QPropertyAnimation(this, "sceneRect", groupanim);
  QPropertyAnimation *bganim =
      new QPropertyAnimation(pageTransitionItem, "x", groupanim);
  sceneanim->setDuration(1000 * transition.duration);
  bganim->setDuration(1000 * transition.duration);
  pageTransitionItem->setZValue(-1e9);
  QRectF movedrect = sceneRect();
  switch (transition.angle) {
    case 90:
      bganim->setPropertyName("y");
      movedrect.moveBottom(movedrect.top());
      bganim->setStartValue(movedrect.y());
      bganim->setEndValue(sceneRect().y());
      break;
    case 180:
      movedrect.moveRight(movedrect.left());
      bganim->setStartValue(movedrect.x());
      bganim->setEndValue(sceneRect().x());
      break;
    case 270:
      bganim->setPropertyName("y");
      movedrect.moveTop(movedrect.bottom());
      bganim->setStartValue(movedrect.y());
      bganim->setEndValue(sceneRect().y());
      break;
    default:
      movedrect.moveLeft(movedrect.right());
      bganim->setStartValue(movedrect.x());
      bganim->setEndValue(sceneRect().x());
      break;
  }
  sceneanim->setStartValue(movedrect);
  sceneanim->setEndValue(sceneRect());
  groupanim->addAnimation(sceneanim);
  groupanim->addAnimation(bganim);
  sceneanim->setEasingCurve(QEasingCurve::OutSine);
  bganim->setEasingCurve(QEasingCurve::OutSine);
  animation = groupanim;
}

void SlideScene::createUncoverTransition(const SlideTransition &transition,
                                         PixmapGraphicsItem *pageTransitionItem)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << this);
  QPropertyAnimation *propanim = new QPropertyAnimation();
  propanim->setDuration(1000 * transition.duration);
  switch (transition.angle) {
    case 90:
      propanim->setPropertyName("y");
      propanim->setStartValue(0.);
      propanim->setEndValue(-sceneRect().height());
      break;
    case 180:
      propanim->setPropertyName("x");
      propanim->setStartValue(0.);
      propanim->setEndValue(-sceneRect().width());
      break;
    case 270:
      propanim->setPropertyName("y");
      propanim->setStartValue(0.);
      propanim->setEndValue(sceneRect().height());
      break;
    default:
      propanim->setPropertyName("x");
      propanim->setStartValue(0.);
      propanim->setEndValue(sceneRect().width());
      break;
  }
  propanim->setTargetObject(pageTransitionItem);
  propanim->setEasingCurve(QEasingCurve::InSine);
  animation = propanim;
}

void SlideScene::createFadeTransition(const SlideTransition &transition,
                                      PixmapGraphicsItem *pageTransitionItem)
{
  debug_verbose(DebugFunctionCalls,
                transition.type << pageTransitionItem << this);
  pageTransitionItem->setOpacity(0.);
  QParallelAnimationGroup *groupanim = new QParallelAnimationGroup();
  QPropertyAnimation *oldpageanim =
      new QPropertyAnimation(pageTransitionItem, "opacity", groupanim);
  QPropertyAnimation *newpageanim =
      new QPropertyAnimation(pageItem, "opacity", groupanim);
  oldpageanim->setDuration(1000 * transition.duration);
  oldpageanim->setStartValue(1.);
  oldpageanim->setEndValue(0.);
  newpageanim->setDuration(1000 * transition.duration);
  newpageanim->setStartValue(0.);
  newpageanim->setEndValue(1.);
  groupanim->addAnimation(oldpageanim);
  groupanim->addAnimation(newpageanim);
  oldpageanim->setEasingCurve(QEasingCurve::OutQuart);
  newpageanim->setEasingCurve(QEasingCurve::InQuart);
  animation = groupanim;
}

void SlideScene::endTransition()
{
  debug_verbose(DebugFunctionCalls, this);
  pageItem->setOpacity(1.);
  if (transitionType == SlideTransition::Fly ||
      transitionType == SlideTransition::FlyRectangle) {
    /* TODO: This patch can lead to flickering of the slide view after the
     * transition. */
    pageItem->trackNew();
    emit navigationToViews(page, this);
  }
  if (pageTransitionItem) {
    removeItem(pageTransitionItem);
    delete pageTransitionItem;
    pageTransitionItem = nullptr;
  }
  if (animation) {
    animation->stop();
    delete animation;
    animation = nullptr;
  }
  loadMedia(page);
  if (slide_flags & ShowSearchResults) updateSearchResults();
  invalidate();
  emit finishTransition();
}

void SlideScene::startInputEvent(std::shared_ptr<const DrawTool> tool,
                                 const QPointF &pos, const float pressure)
{
  debug_verbose(DebugFunctionCalls, tool.get() << pos << pressure << this);
  if (!tool || !(tool->tool() & Tool::AnyDrawTool) ||
      !(slide_flags & ShowDrawings))
    return;
  debug_verbose(DebugDrawing, "Start input event" << tool->tool()
                                                  << tool->device()
                                                  << tool.get() << pressure);
  stopDrawing();
  if (currentItemCollection || currentlyDrawnItem) return;
  clearSelection();
  const PathContainer *container = master->pathContainer({page, page_part});
  const qreal z = container ? container->topZValue() + 10 : 10;
  setFocusItem(nullptr);
  currentItemCollection = new QGraphicsItemGroup();
  currentItemCollection->setZValue(z);
  addItem(currentItemCollection);
  currentItemCollection->show();
  switch (tool->shape()) {
    case DrawTool::Freehand:
    case DrawTool::Recognize:
      if (tool->tool() == Tool::Pen &&
          (tool->device() & Tool::PressureSensitiveDevices))
        currentlyDrawnItem = new FullGraphicsPath(*tool, pos, pressure);
      else
        currentlyDrawnItem = new BasicGraphicsPath(*tool, pos);
      currentlyDrawnItem->hide();
      break;
    case DrawTool::Rect: {
      RectGraphicsItem *rect_item = new RectGraphicsItem(*tool, pos);
      rect_item->show();
      currentlyDrawnItem = rect_item;
      break;
    }
    case DrawTool::Ellipse: {
      EllipseGraphicsItem *rect_item = new EllipseGraphicsItem(*tool, pos);
      rect_item->show();
      currentlyDrawnItem = rect_item;
      break;
    }
    case DrawTool::Line: {
      LineGraphicsItem *line_item = new LineGraphicsItem(*tool, pos);
      line_item->show();
      currentlyDrawnItem = line_item;
      break;
    }
    case DrawTool::Arrow: {
      ArrowGraphicsItem *arrow_item = new ArrowGraphicsItem(*tool, pos);
      arrow_item->show();
      currentlyDrawnItem = arrow_item;
      break;
    }
  }
  currentlyDrawnItem->setZValue(z);
  addItem(currentlyDrawnItem);
}

void SlideScene::stepInputEvent(std::shared_ptr<const DrawTool> tool,
                                const QPointF &pos, const float pressure)
{
  debug_verbose(DebugFunctionCalls, tool.get() << pos << pressure << this);
  if (pressure <= 0 || !tool || !(slide_flags & ShowDrawings)) return;
  debug_verbose(DebugDrawing, "Step input event" << tool->tool()
                                                 << tool->device() << tool.get()
                                                 << pressure);
  if (!currentlyDrawnItem) return;
  switch (currentlyDrawnItem->type()) {
    case BasicGraphicsPath::Type: {
      if (!currentItemCollection) break;
      BasicGraphicsPath *current_path =
          static_cast<BasicGraphicsPath *>(currentlyDrawnItem);
      if (current_path->getTool() != *tool) break;
      FlexGraphicsLineItem *item = new FlexGraphicsLineItem(
          QLineF(current_path->lastPoint(), pos), tool->compositionMode());
      current_path->addPoint(current_path->mapFromScene(pos));
      item->setPen(tool->pen());
      currentItemCollection->addToGroup(item);
      currentItemCollection->show();
      invalidate(item->sceneBoundingRect(), QGraphicsScene::ItemLayer);
      break;
    }
    case FullGraphicsPath::Type: {
      if (!currentItemCollection) break;
      FullGraphicsPath *current_path =
          static_cast<FullGraphicsPath *>(currentlyDrawnItem);
      if (current_path->getTool() != *tool) break;
      FlexGraphicsLineItem *item = new FlexGraphicsLineItem(
          QLineF(current_path->lastPoint(), pos), tool->compositionMode());
      current_path->addPoint(current_path->mapFromScene(pos), pressure);
      QPen pen = tool->pen();
      pen.setWidthF(pen.widthF() * pressure);
      item->setPen(pen);
      currentItemCollection->addToGroup(item);
      currentItemCollection->show();
      invalidate(item->sceneBoundingRect(), QGraphicsScene::ItemLayer);
      break;
    }
    case RectGraphicsItem::Type:
      static_cast<RectGraphicsItem *>(currentlyDrawnItem)->setSecondPoint(pos);
      break;
    case EllipseGraphicsItem::Type:
      static_cast<EllipseGraphicsItem *>(currentlyDrawnItem)
          ->setSecondPoint(pos);
      break;
    case LineGraphicsItem::Type:
      static_cast<LineGraphicsItem *>(currentlyDrawnItem)->setSecondPoint(pos);
      break;
    case ArrowGraphicsItem::Type:
      static_cast<ArrowGraphicsItem *>(currentlyDrawnItem)->setSecondPoint(pos);
      break;
  }
}

bool SlideScene::stopInputEvent(std::shared_ptr<const DrawTool> tool)
{
  debug_verbose(DebugFunctionCalls, tool.get() << this);
  if (!tool || !(slide_flags & ShowDrawings)) return false;
  debug_verbose(DebugDrawing, "Stop input event" << tool->tool()
                                                 << tool->device()
                                                 << tool.get());
  const bool changes = currentlyDrawnItem != nullptr;
  stopDrawing();
  if (changes) {
    invalidate({QRect()}, QGraphicsScene::ItemLayer);
    return true;
  }
  return false;
}

void SlideScene::initTmpSelectionTool(const Tool::InputDevices device) noexcept
{
  debug_verbose(DebugFunctionCalls, device << this);
  if (tmp_selection_tool)
    tmp_selection_tool->reset();
  else
    tmp_selection_tool = std::shared_ptr<SelectionTool>(
        new SelectionTool(Tool::BasicSelectionTool, device));
  QList<QGraphicsItem *> selection = selectedItems();
  selection.append(&selection_bounding_rect);
  tmp_selection_tool->initTransformations(selection);
}

bool SlideScene::noToolClicked(const QPointF &pos, const QPointF &startpos)
{
  debug_verbose(DebugFunctionCalls, pos << startpos << this);
  debug_verbose(DebugMedia | DebugDrawing,
                "Clicked without tool" << pos << startpos);
  // Try to handle multimedia annotation.
  for (auto &item : mediaItems)
#if __cplusplus >= 202002L
    // std::set<T>.contains is only available since C++ 20
    if (item->pages().contains(page) && item->rect().contains(pos))
#else
    if (item->pages().find(page) != item->pages().end() &&
        item->rect().contains(pos))
#endif
    {
      if (startpos.isNull() ||
          item->rect().contains(startpos) && item->toggle())
        return true;
      break;
    }
  // Try to handle links.
  const PdfLink *link = master->getDocument()->linkAt(page, pos);
  bool did_something = false;
  if (link && (startpos.isNull() || link->area.contains(startpos)))
    switch (link->type) {
      case PdfLink::PageLink: {
        int page = static_cast<const GotoLink *>(link)->page;
        emit navigationSignal(preferences()->slideForPage(page), page);
        did_something = true;
        break;
      }
      case PdfLink::ActionLink:
        emit sendAction(static_cast<const ActionLink *>(link)->action);
        did_something = true;
        break;
      case PdfLink::RemoteUrl:
      case PdfLink::LocalUrl:
      case PdfLink::ExternalPDF:
        QDesktopServices::openUrl(static_cast<const ExternalLink *>(link)->url);
        did_something = true;
        break;
      case PdfLink::SoundLink:
      case PdfLink::MovieLink: {
        // This is untested!
        std::shared_ptr<MediaItem> &item = getMediaItem(
            static_cast<const MediaLink *>(link)->annotation, page);
        if (item->asQGraphicsItem()->scene() != this)
          addItem(item->asQGraphicsItem());
        item->asQGraphicsItem()->setZValue(0);
        item->show();
        item->play();
        did_something = true;
        break;
      }
      case PdfLink::NoLink:
        break;
    }
  delete link;
  return did_something;
}

void SlideScene::createSliders() const
{
  debug_verbose(DebugFunctionCalls, mediaItems.size() << this);
  for (auto &item : mediaItems)
    if (
#if __cplusplus >= 202002L
        item->pages().contains(page)
#else
        item->pages().find(page) != item->pages().end()
#endif
        && (item->flags() & MediaAnnotation::ShowSlider)) {
      for (const auto view : static_cast<const QList<QGraphicsView *>>(views()))
        static_cast<SlideView *>(view)->addMediaSlider(item);
    }
}

void SlideScene::playMedia() const
{
  debug_verbose(DebugFunctionCalls, mediaItems.size() << this);
  for (auto &item : mediaItems)
    if (
#if __cplusplus >= 202002L
        item->pages().contains(page)
#else
        item->pages().find(page) != item->pages().end()
#endif
    )
      item->play();
}

void SlideScene::pauseMedia() const
{
  debug_verbose(DebugFunctionCalls, mediaItems.size() << this);
  for (auto &item : mediaItems)
    if (
#if __cplusplus >= 202002L
        item->pages().contains(page)
#else
        item->pages().find(page) != item->pages().end()
#endif
    )
      item->pause();
}

void SlideScene::playPauseMedia() const
{
  debug_verbose(DebugFunctionCalls, mediaItems.size() << this);
  for (auto &item : mediaItems)
    if (
#if __cplusplus >= 202002L
        item->pages().contains(page)
#else
        item->pages().find(page) != item->pages().end()
#endif
        && (item->flags() & MediaAnnotation::Interactive) &&
        item->isPlaying()) {
      pauseMedia();
      return;
    }
  playMedia();
}

void SlideScene::updateSelectionRect() noexcept
{
  // TODO: only call manually for higher efficiency?
  // (note: selection can also be changed by eraser / undo / redo / ...)
  const QList<QGraphicsItem *> items = selectedItems();
  debug_verbose(DebugFunctionCalls, items << this);
  if (items.isEmpty()) {
    selection_bounding_rect.hide();
    return;
  }
  // TODO: This is probably quite slow
  QRectF newrect =
      items.first()->mapToScene(items.first()->shape()).controlPointRect();
  for (const auto &item : items)
    newrect =
        newrect.united(item->mapToScene(item->shape()).controlPointRect());
  selection_bounding_rect.setRect(newrect);
  if (selection_bounding_rect.scene() != this)
    addItem(&selection_bounding_rect);
  selection_bounding_rect.show();
}

void SlideScene::removeSelection()
{
  const QList<QGraphicsItem *> selection = selectedItems();
  debug_verbose(DebugFunctionCalls, selection << this);
  emit sendRemovePaths({page, page_part}, selection);
}

void SlideScene::copyToClipboard() const
{
  QList<QGraphicsItem *> selection = selectedItems();
  debug_verbose(DebugFunctionCalls, selection << this);
  if (selection.isEmpty()) return;
  // Sort selection by z order
  std::sort(selection.begin(), selection.end(), &cmp_by_z);
  // Write to native data type
  QByteArray data;
  QDataStream stream(&data, QIODevice::WriteOnly);
  stream << selection;
  QMimeData *mimedata = new QMimeData();
  mimedata->setData("application/beamerpresenter", data);
  data.clear();
  const QRectF rect = selection_bounding_rect.sceneRect().boundingRect();
  // Write svg data
  writeToSVG(data, selection, rect);
  mimedata->setData("image/svg+xml", data);
  data.clear();
  // Calculate resolution for pixel image data (kind of random)
  const qreal resolution = std::min(4., 1600. / std::max(width(), height()));
  // Write png data
  writeToPixelImage(data, selection, rect, resolution, "PNG");
  mimedata->setData("image/png", data);
  data.clear();
  // Write jpeg data
  writeToPixelImage(data, selection, rect, resolution, "JPEG");
  mimedata->setData("image/jpeg", data);
  data.clear();
  /* Disable inefficient and hardly used formats.
   * Since the clipboard has a memory leak in Wayland, we don't want
   * to add these extra data.
  // Write bmp data
  writeToPixelImage(data, selection, rect, resolution, "BMP");
  mimedata->setData("image/bmp", data);
  data.clear();
  // Write ppm data
  writeToPixelImage(data, selection, rect, resolution, "PPM");
  mimedata->setData("image/ppm", data);
  data.clear();
  */
  // Add data to clipboard
  QClipboard *clipboard = QGuiApplication::clipboard();
  clipboard->setMimeData(mimedata);
}

void SlideScene::pasteFromClipboard()
{
  debug_verbose(DebugFunctionCalls, this);
  const QMimeData *mimedata = QGuiApplication::clipboard()->mimeData();
  QList<QGraphicsItem *> items;
  if (mimedata->hasFormat("application/beamerpresenter")) {
    const QByteArray data = mimedata->data("application/beamerpresenter");
    QDataStream stream(data);
    stream >> items;
    items.removeAll(nullptr);
  } else if (mimedata->hasFormat("image/svg+xml"))
    readFromSVG(mimedata->data("image/svg+xml"), items);
  else if (mimedata->hasFormat("image/png"))
    readFromPixelImage(mimedata->data("image/png"), items, "PNG");
  else if (mimedata->hasFormat("image/jpeg"))
    readFromPixelImage(mimedata->data("image/jpeg"), items, "JPEG");
  else if (mimedata->hasFormat("image/gif"))
    readFromPixelImage(mimedata->data("image/gif"), items, "GIF");
  else if (mimedata->hasFormat("image/bmp"))
    readFromPixelImage(mimedata->data("image/bmp"), items, "BMP");
  else if (mimedata->hasFormat("image/ppm"))
    readFromPixelImage(mimedata->data("image/ppm"), items, "PPM");
  if (items.empty()) return;
  clearSelection();
  setFocusItem(nullptr);
  QRectF pasted_rect = items.constFirst()->sceneBoundingRect();
  PathContainer *container = master->pathContainer({page, page_part});
  qreal z = 0;
  if (container) z = container->topZValue();
  for (const auto item : std::as_const(items)) {
    z += 10;
    item->setZValue(z);
    if (item->scene() != this) addItem(item);
    item->show();
    item->setSelected(true);
    pasted_rect = pasted_rect.united(item->sceneBoundingRect());
  }
  // Check if selection is visible. If not, move it to the slide.
  qreal dx = 0, dy = 0;
  const auto &scene_rect = sceneRect();
  if (pasted_rect.left() + 3 >= scene_rect.right())
    dx = scene_rect.right() - pasted_rect.right();
  else if (pasted_rect.right() <= scene_rect.left() + 3)
    dx = scene_rect.left() - pasted_rect.left();
  if (pasted_rect.top() + 3 >= scene_rect.bottom())
    dy = scene_rect.bottom() - pasted_rect.bottom();
  else if (pasted_rect.bottom() <= scene_rect.top() + 3)
    dy = scene_rect.top() - pasted_rect.top();
  if (dx != 0 || dy != 0)
    for (const auto item : std::as_const(items)) item->moveBy(dx, dy);
  emit sendAddPaths({page, page_part}, items);
  updateSelectionRect();
}

void SlideScene::toolChanged(std::shared_ptr<Tool> tool) noexcept
{
  debug_verbose(DebugFunctionCalls, tool.get() << this);
  if (!tool ||
      (tool->tool() & (Tool::AnySelectionTool | Tool::AnyPointingTool)))
    return;
  if (tool->tool() & Tool::AnyDrawTool) {
    const QList<QGraphicsItem *> selection = selectedItems();
    if (selection.isEmpty()) return;
    std::map<AbstractGraphicsPath *, drawHistory::DrawToolDifference>
        tool_changes;
    const auto draw_tool = std::static_pointer_cast<const DrawTool>(tool);
    for (const auto item : selection)
      if (item->type() == BasicGraphicsPath::Type ||
          item->type() == FullGraphicsPath::Type) {
        const auto path = static_cast<AbstractGraphicsPath *>(item);
        if (path->getTool() != *draw_tool)
          tool_changes.insert({path, drawHistory::DrawToolDifference(
                                         path->getTool(), *draw_tool)});
        path->changeTool(*draw_tool);
        path->update();
      }
    if (!tool_changes.empty())
      emit sendHistoryStep({page, page_part}, nullptr, &tool_changes, nullptr);
  } else if (tool->tool() == Tool::TextInputTool) {
    QList<QGraphicsItem *> selection = selectedItems();
    if (focusItem() && focusItem()->type() == TextGraphicsItem::Type)
      selection.append(focusItem());
    if (selection.isEmpty()) return;
    std::map<TextGraphicsItem *, drawHistory::TextPropertiesDifference>
        text_changes;
    const auto text_tool = std::static_pointer_cast<const TextTool>(tool);
    TextGraphicsItem *text;
    for (auto item : std::as_const(selection))
      if (item->type() == TextGraphicsItem::Type) {
        text = static_cast<TextGraphicsItem *>(item);
        const QColor &old_color = text->defaultTextColor(),
                     &new_color = text_tool->color();
        if (text->font() != text_tool->font() || old_color != new_color)
          text_changes.insert({text,
                               {text->font(), text_tool->font(),
                                old_color.rgba() ^ new_color.rgba()}});
        text->setFont(text_tool->font());
        text->setDefaultTextColor(new_color);
      }
    if (!text_changes.empty())
      emit sendHistoryStep({page, page_part}, nullptr, nullptr, &text_changes);
  }
}

void SlideScene::toolPropertiesChanged(const tool_variant &properties) noexcept
{
  debug_verbose(DebugFunctionCalls, this);
  QList<QGraphicsItem *> selection = selectedItems();
  if (focusItem() && focusItem()->type() == TextGraphicsItem::Type)
    selection.append(focusItem());
  if (selection.empty()) return;
  if (std::holds_alternative<QFont>(properties)) {
    std::map<TextGraphicsItem *, drawHistory::TextPropertiesDifference>
        text_changes;
    const QFont font = std::get<QFont>(properties);
    TextGraphicsItem *text;
    for (auto item : std::as_const(selection))
      if (item->type() == TextGraphicsItem::Type) {
        text = static_cast<TextGraphicsItem *>(item);
        if (text->font() != font)
          text_changes.insert({text, {text->font(), font, 0}});
        text->setFont(font);
      }
    if (!text_changes.empty())
      emit sendHistoryStep({page, page_part}, nullptr, nullptr, &text_changes);
    return;
  }
  if (std::holds_alternative<QColor>(properties)) {
    const QColor color = std::get<QColor>(properties);
    std::map<AbstractGraphicsPath *, drawHistory::DrawToolDifference>
        tool_changes;
    std::map<TextGraphicsItem *, drawHistory::TextPropertiesDifference>
        text_changes;
    for (auto item : std::as_const(selection)) {
      switch (item->type()) {
        case BasicGraphicsPath::Type:
        case FullGraphicsPath::Type: {
          auto path = static_cast<AbstractGraphicsPath *>(item);
          if (path->getTool().color() != color) {
            DrawTool tool = path->getTool();
            if (tool.brush().color() == tool.color())
              tool.brush().setColor(color);
            tool.setColor(color);
            tool_changes.insert(
                {path, drawHistory::DrawToolDifference(path->getTool(), tool)});
            path->changeTool(tool);
            path->update();
          }
          break;
        }
        case TextGraphicsItem::Type: {
          const auto text = static_cast<TextGraphicsItem *>(item);
          text_changes.insert(
              {text,
               {text->font(), text->font(),
                text->defaultTextColor().rgba() ^ color.rgba()}});
          text->setDefaultTextColor(color);
          break;
        }
      }
    }
    if (!tool_changes.empty() || !text_changes.empty())
      emit sendHistoryStep({page, page_part}, nullptr, &tool_changes,
                           &text_changes);
    return;
  }

  std::map<AbstractGraphicsPath *, drawHistory::DrawToolDifference>
      tool_changes;
  for (auto item : std::as_const(selection)) {
    switch (item->type()) {
      case BasicGraphicsPath::Type:
      case FullGraphicsPath::Type: {
        auto path = static_cast<AbstractGraphicsPath *>(item);
        DrawTool newtool = path->getTool();
        switch (properties.index()) {
          case 0: {
            if (newtool.width() == std::get<qreal>(properties)) continue;
            newtool.setWidth(std::get<qreal>(properties));
            break;
          }
          case 1: {
            QPen pen = newtool.pen();
            if (pen.style() == std::get<Qt::PenStyle>(properties)) continue;
            pen.setStyle(std::get<Qt::PenStyle>(properties));
            newtool.setPen(pen);
            break;
          }
          case 2: {
            if (newtool.brush().style() == std::get<Qt::BrushStyle>(properties))
              continue;
            if (newtool.brush().style() == Qt::NoBrush)
              newtool.brush().setColor(newtool.color());
            newtool.brush().setStyle(std::get<Qt::BrushStyle>(properties));
            break;
          }
          case 3: {
            if (newtool.compositionMode() ==
                std::get<QPainter::CompositionMode>(properties))
              continue;
            newtool.setCompositionMode(
                std::get<QPainter::CompositionMode>(properties));
            break;
          }
        }
        tool_changes.insert(
            {path, drawHistory::DrawToolDifference(path->getTool(), newtool)});
        path->changeTool(newtool);
        path->update();
        break;
      }
    }
  }
  if (!tool_changes.empty())
    emit sendHistoryStep({page, page_part}, nullptr, &tool_changes, nullptr);
}

void SlideScene::updateSearchResults()
{
  debug_verbose(DebugFunctionCalls, this);
  const auto &pair = master->searchResults();
  if (pair.second.isEmpty() && searchResults) {
    if (searchResults->scene()) removeItem(searchResults);
    delete searchResults;
    searchResults = nullptr;
  } else if (pair.first == page) {
    if (searchResults) {
      const auto child_items = searchResults->childItems();
      for (const auto &item : child_items) {
        searchResults->removeFromGroup(item);
        delete item;
      }
      if (!searchResults->scene()) addItem(searchResults);
    } else {
      searchResults = new QGraphicsItemGroup();
      addItem(searchResults);
    }
    QGraphicsRectItem *item;
    const QBrush brush(preferences()->search_highlighting_color);
    for (const auto &rect : pair.second) {
      item = new QGraphicsRectItem(rect);
      item->setBrush(brush);
      item->setPen(Qt::NoPen);
      searchResults->addToGroup(item);
    }
    invalidate(searchResults->boundingRect());
  }
}

void readFromSVG(const QByteArray &data, QList<QGraphicsItem *> &target)
{
  debug_verbose(DebugFunctionCalls, data.size() << target);
  QSvgRenderer renderer(data);
  QPicture picture;
  QPainter painter;
  if (!painter.begin(&picture)) return;
  renderer.render(&painter);
  painter.end();
  target.append(new GraphicsPictureItem(picture));
}

void readFromPixelImage(const QByteArray &data, QList<QGraphicsItem *> &target,
                        const char *format)
{
  debug_verbose(DebugFunctionCalls, data.size() << target << format);
  QPicture picture;
  QImage image{QImage::fromData(data, format)};
  QPainter painter;
  if (!painter.begin(&picture)) return;
  painter.drawImage(0, 0, image);
  painter.end();
  target.append(new GraphicsPictureItem(picture));
}

void writeToSVG(QByteArray &data, const QList<QGraphicsItem *> &source,
                const QRectF &rect)
{
  debug_verbose(DebugFunctionCalls, data.size() << source << rect);
  QSvgGenerator generator;
  QBuffer buffer(&data);
  buffer.open(QIODevice::WriteOnly);
  generator.setOutputDevice(&buffer);
  generator.setViewBox(rect);
  QPainter painter;
  if (!painter.begin(&generator)) return;
  QStyleOptionGraphicsItem style;
  for (const auto item : source) {
    painter.setTransform(item->sceneTransform(), false);
    item->paint(&painter, &style);
  }
  painter.end();
}

void writeToPixelImage(QByteArray &data, const QList<QGraphicsItem *> &source,
                       const QRectF &rect, const qreal resolution,
                       const char *format)
{
  debug_verbose(DebugFunctionCalls,
                data.size() << source << rect << resolution << format);
  QImage image((rect.size() * resolution).toSize(), QImage::Format_ARGB32);
  image.fill(0x00ffffff);
  QPainter painter;
  if (!painter.begin(&image)) return;
  QStyleOptionGraphicsItem style;
  const QPointF origin = -rect.topLeft();
  for (const auto item : source) {
    painter.resetTransform();
    painter.scale(resolution, resolution);
    painter.translate(origin);
    painter.setTransform(item->sceneTransform(), true);
    item->paint(&painter, &style);
  }
  painter.end();
  QBuffer buffer(&data);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, format);
  buffer.close();
}

void SlideScene::setFixedAspect(const qreal aspect)
{
  setFixedHeight(master->getPageSize(0).width() / aspect);
}
