// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#include "src/drawing/pixmapgraphicsitem.h"
#include "src/preferences.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <utility>

#include "src/log.h"

constexpr int RANDOM_INT = 42;

/// Generate a random array for glitter transitions.
/// Once shuffled, this array contains the numbers between 0 and GLITTER_NUMBER
/// in random order.
static std::array<unsigned int, PixmapGraphicsItem::glitter_number> &
shuffled_array()
{
  static std::array<unsigned int, PixmapGraphicsItem::glitter_number> array = {
      PixmapGraphicsItem::glitter_number + 1};
  if (array[0] == PixmapGraphicsItem::glitter_number + 1) {
    int i = 0;
    do array[i] = i;
    while (++i < PixmapGraphicsItem::glitter_number);
  }
  return array;
}

/// Shuffle the glitter array.
static void reshuffle_array(const int seed = RANDOM_INT)
{
  static std::random_device rd;
  static std::default_random_engine re(rd());
  std::array<unsigned int, PixmapGraphicsItem::glitter_number> &array =
      shuffled_array();
  std::shuffle(array.begin(), array.end(), re);
}

/// Get random value between 0 and GLITTER_NUMBER from shuffled array.
inline static int shuffled(const int i)
{
  return shuffled_array()[i % PixmapGraphicsItem::glitter_number];
}

void PixmapGraphicsItem::paint(QPainter *painter,
                               const QStyleOptionGraphicsItem *option,
                               QWidget *widget)
{
  debug_msg(DebugRendering, "start painting pixmap" << this);
  if (mask_type == NoMask)
    painter->fillRect(bounding_rect, preferences()->background_brush);
  if (pixmaps.isEmpty()) return;
  const QRectF target_rect = painter->transform().mapRect(bounding_rect);
  const int ref_width = target_rect.width();
  QPixmap pixmap;
  for (const auto &pix : std::as_const(pixmaps)) {
    if (pix.width() >= ref_width) {
      pixmap = pix;
      break;
    }
  }
  if (pixmap.isNull()) {
    pixmap = pixmaps.last();
    qWarning() << "Showing pixmap with insufficient resolution";
  }
  if (mask_type && !_mask.isNull()) {
    switch (mask_type) {
      case NoMask:
      case OtherMask:
      case Glitter:
        break;
      case PositiveClipping:
        painter->setClipRect(_mask);
        break;
      case NegativeClipping: {
        QPainterPath outerpath, innerpath;
        outerpath.addRect(bounding_rect);
        innerpath.addRect(_mask);
        painter->setClipPath(outerpath - innerpath);
        break;
      }
      case VerticalBlinds: {
        QPainterPath path;
        QRectF rect_bl(_mask);
        path.addRect(rect_bl);
        int i = 0;
        while (++i < blinds_number_v) {
          rect_bl.moveLeft(rect_bl.left() +
                           bounding_rect.width() / blinds_number_v);
          path.addRect(rect_bl);
        }
        painter->setClipPath(path);
        break;
      }
      case HorizontalBlinds: {
        QPainterPath path;
        QRectF rect_bl(_mask);
        path.addRect(rect_bl);
        int i = 0;
        while (++i < blinds_number_h) {
          rect_bl.moveTop(rect_bl.top() +
                          bounding_rect.height() / blinds_number_h);
          path.addRect(rect_bl);
        }
        painter->setClipPath(path);
        break;
      }
    }
  }
  painter->resetTransform();
  if (mask_type == Glitter && animation_progress != UINT_MAX) {
    const unsigned int glitter_pixel = pixmap.width() / glitter_row,
                       n = ref_width * target_rect.height() / glitter_pixel,
                       w = ref_width / glitter_pixel + 1;
    for (unsigned int j = 0; j < animation_progress; j++) {
      for (unsigned int i = shuffled(j); i < n; i += glitter_number) {
        painter->drawPixmap(target_rect.x() + glitter_pixel * (i % w),
                            target_rect.y() + glitter_pixel * (i / w), pixmap,
                            glitter_pixel * (i % w), glitter_pixel * (i / w),
                            glitter_pixel, glitter_pixel);
      }
    }
  } else if (std::abs(ref_width - pixmap.width()) < 2) {
    painter->drawPixmap(target_rect.topLeft().toPoint(), pixmap);
  } else {
    debug_msg(DebugRendering, "painting pixmap not pixel-aligned" << this);
    painter->drawPixmap(target_rect.toRect(), pixmap);
  }
#ifdef QT_DEBUG
  if ((preferences()->debug_level & (DebugRendering | DebugVerbose)) ==
      (DebugRendering | DebugVerbose)) {
    const QString str = QString::number(pixmap.width()) + "x" +
                        QString::number(pixmap.height());
    painter->drawText(target_rect, str);
  }
#endif
}

void PixmapGraphicsItem::addPixmap(const QPixmap &pixmap) noexcept
{
  if (pixmap.isNull()) return;
  auto it = pixmaps.begin();
  for (; it != pixmaps.end(); ++it) {
    if (pixmap.width() <= it->width()) break;
  }
  if (it == pixmaps.end())
    pixmaps.append(pixmap);
  else if (pixmap.width() == it->width())
    *it = pixmap;
  else
    pixmaps.insert(it, pixmap);
  newHashs.insert(pixmap.width());
  update();
}

void PixmapGraphicsItem::clearOld() noexcept
{
  if (newHashs.empty()) return;
  debug_verbose(DebugRendering,
                "clearing pixmaps:" << newHashs.size() << pixmaps.size());
  for (auto it = pixmaps.begin(); it != pixmaps.end();) {
    if (newHashs.contains(it->width()))
      ++it;
    else
      it = pixmaps.erase(it);
  }
  newHashs.clear();
}

void PixmapGraphicsItem::setMaskType(const MaskType type) noexcept
{
  mask_type = type;
  if (mask_type == Glitter)
    reshuffle_array(pixmaps.isEmpty() ? RANDOM_INT : pixmaps.first().width());
}

QPixmap PixmapGraphicsItem::getPixmap(const unsigned int width) const noexcept
{
  for (const auto &pix : pixmaps) {
    if (pix.width() >= width) {
#ifdef QT_DEBUG
      if (pix.width() != width)
        debug_msg(DebugRendering,
                  "possibly wrong resolution:" << pix.width() << width);
#endif
      return pix;
    }
  }
  if (pixmaps.isEmpty()) return QPixmap();
  return pixmaps.last();
}

bool PixmapGraphicsItem::hasWidth(const unsigned int width) const noexcept
{
  for (const auto &pix : pixmaps) {
    if (pix.width() == width) return true;
    if (pix.width() > width) return false;
  }
  return false;
}

bool PixmapGraphicsItem::hasWidth(const qreal width) const noexcept
{
  for (const auto &pix : pixmaps) {
    if (pix.width() > width + max_width_tolerance) return false;
    if (pix.width() > width - max_width_tolerance) return true;
  }
  return false;
}

void PixmapGraphicsItem::setRect(const QRectF &rect) noexcept
{
  bounding_rect = rect;
  if (pixmaps.size() > 0) {
    const QSize ref_size = pixmaps.last().size();
    if (ref_size.width() / ref_size.height() <
        0.98 * rect.width() / rect.height()) {
      bounding_rect.setHeight(ref_size.height() * rect.width() /
                              ref_size.width());
    }
  }
  update();
}
