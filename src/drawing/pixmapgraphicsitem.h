// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#ifndef PIXMAPGRAPHICSITEM_H
#define PIXMAPGRAPHICSITEM_H

#include <QGraphicsObject>
#include <QMap>
#include <QPixmap>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QtCore>

#include "src/config.h"
#include "src/enumerates.h"

class QPainter;
class QWidget;
class QStyleOptionGraphicsItem;

/**
 * @brief pixmaps with different resolutions of same picture as QGraphicsItem
 *
 * Store multiple pixmaps for the same vector graphic at different resolutions.
 * This makes it possible to use the page background as a QGraphicsItem
 * while showing different pixmaps with the correct resolution for different
 * views of the QGraphicsScene.
 */
class PixmapGraphicsItem : public QGraphicsObject
{
  Q_OBJECT

  // This class is used in animations. Define properties for these animations.
  // TODO: bundle these animation properties in a std::variant?
  /// mask for rendering this
  Q_PROPERTY(QRectF mask READ mask WRITE setMask)
  /// progress of glitter animation.
  Q_PROPERTY(int progress READ progress WRITE setProgress)

 public:
  /// Different masks are used in animations. MaskType defines how
  /// this is drawn and how _mask is interpreted.
  enum MaskType {
    NoMask,
    PositiveClipping,
    NegativeClipping,
    HorizontalBlinds,
    VerticalBlinds,
    Glitter,
    OtherMask,
  };

  static constexpr int blinds_number_h = 6;
  static constexpr int blinds_number_v = 8;
  static constexpr int glitter_row = 71;
  static constexpr int glitter_number = 137;
  static constexpr qreal max_width_tolerance = 0.6;

 private:
  /// List of pixmaps
  QList<QPixmap> pixmaps;

  /// Bounding rect of this QGraphicsItem.
  QRectF bounding_rect;

  /// Rectangular mask. Depending on mask_type, painting will be clipped to
  /// mask, to the inverse of mask, or to repeated copies of mask.
  QRectF _mask;

  /// Animation progress, only relevant for glitter transition.
  /// A value of UINT_MAX means that no glitter animation is currently active.
  unsigned int animation_progress = UINT_MAX;

  /// Mask type defines meaning of mask and depends on the slide transition.
  MaskType mask_type = NoMask;

  /// List of hashs (widths) of pixmaps which were added since the latest
  /// call to trackChanges().
  QSet<unsigned int> newHashs;

 public:
  /// Type of this custom QGraphicsItem.
  enum { Type = UserType + PixmapGraphicsItemType };

  /// Trivial constructor.
  explicit PixmapGraphicsItem(const QRectF &rect,
                              QGraphicsItem *parent = nullptr)
      : QGraphicsObject(parent), bounding_rect(rect)
  {
  }

  /// @return custom QGraphicsItem type
  int type() const noexcept override { return Type; }

  /// Check whether this contains a pixmap with the given width
  bool hasWidth(const unsigned int width) const noexcept;

  /// Check whether this contains a pixmap with the given width within tolerance
  /// 0.6
  bool hasWidth(const qreal width) const noexcept;

  /// Paint this on given painter.
  /// @param painter paint to this painter.
  /// @param option currently ignored.
  /// @param widget currently ignored.
  void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
             QWidget *widget = nullptr) override;

  /// Bounding rect in scene coordinates
  QRectF boundingRect() const override { return bounding_rect; }

  /// Get pixmap with the given width or next larger width.
  QPixmap getPixmap(const unsigned int width) const noexcept;

  /// @return _mask
  /// @see setMask()
  const QRectF mask() const noexcept { return _mask; }

  /// @return mask_type
  /// @see setMaskType()
  MaskType maskType() const noexcept { return mask_type; }

  /// @return animation_progress (used in glitter animation).
  /// @see setProgress()
  int progress() const noexcept { return animation_progress; }

  /// @return number of pixmaps.
  int number() const noexcept { return pixmaps.size(); }

 public slots:
  /// Add a pixmap.
  void addPixmap(const QPixmap &pixmap) noexcept;

  /// Set (overwrite) bounding rect.
  void setRect(const QRectF &rect) noexcept;

  /// Set (overwrite) bounding rect size.
  void setSize(const QSizeF &size) noexcept { bounding_rect.setSize(size); }

  /// Clear everything.
  void clearPixmaps() noexcept { pixmaps.clear(); }

  /// Start tracking changes.
  /// @see clearOld()
  void trackNew() noexcept { newHashs.clear(); }

  /// Clear everything that was added or modified before the latest
  /// call to trackNew().
  /// @see trackNew()
  void clearOld() noexcept;

  /// Set rectangular mask.
  /// @see mask()
  void setMask(const QRectF &rect) noexcept
  {
    _mask = rect;
    update();
  }

  /// Set mask type (meaning of mask).
  /// @see maskType()
  void setMaskType(const MaskType type) noexcept;

  /// Set animation progress (only in glitter transition).
  /// @see progress()
  void setProgress(const int progress) noexcept
  {
    animation_progress = progress;
    update();
  }
};

#endif  // PIXMAPGRAPHICSITEM_H
