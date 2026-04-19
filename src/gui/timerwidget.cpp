// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#include "src/gui/timerwidget.h"

#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QResizeEvent>
#include <QTime>
#include <algorithm>
#include <iterator>

#include "src/log.h"
#include "src/preferences.h"

TimerWidget::TimerWidget(QWidget *parent)
    : QWidget(parent),
      passed(new QLineEdit(this)),
      total(new QLineEdit(this)),
      label(new QLabel(" / ", this))
{
  setMinimumSize(24, 10);
  QHBoxLayout *layout = new QHBoxLayout();
  layout->addWidget(passed);
  layout->addWidget(label);
  layout->addWidget(total);
  setLayout(layout);
  connect(passed, &QLineEdit::returnPressed, this, &TimerWidget::changePassed);
  connect(total, &QLineEdit::editingFinished, this, &TimerWidget::changeTotal);
  passed->setAlignment(Qt::AlignCenter);
  total->setAlignment(Qt::AlignCenter);
  passed->setToolTip(tr("time since beginning of the presentation"));
  total->setToolTip(tr("total duration of the presentation"));
  setToolTip(
      tr("double-click here to set time for this slide "
         "for your orientation while presenting"));
  updateFullText();
  stopTimer();
}

TimerWidget::~TimerWidget()
{
  delete passed;
  delete total;
}

void TimerWidget::updateTimeout() noexcept
{
  QPalette palette;
  if (_flags & Timeout)
    palette.setColor(QPalette::Base, Qt::red);
  else
    palette.setColor(QPalette::Base, Qt::white);
  setPalette(palette);
  emit sendTimeout(_flags & Timeout);
}

void TimerWidget::resizeEvent(QResizeEvent *event) noexcept
{
  QFont thefont = passed->font();
  thefont.setPointSizeF(
      std::min(event->size().height() / 2, event->size().width() / 12));
  passed->setFont(thefont);
  total->setFont(thefont);
  thefont.setPointSizeF(0.75 * thefont.pointSizeF());
  label->setFont(thefont);
}

void TimerWidget::updateFullText() noexcept
{
  const QTime time =
      QTime::fromMSecsSinceStartOfDay(preferences()->msecs_total);
  total->setText(time.toString(time.hour() > 0 ? "h:mm:ss" : "m:ss"));
  updateText();
}

void TimerWidget::updateText() noexcept
{
  const quint32 msecs_passed = timePassed();
  if (!passed->hasFocus())
    passed->setText(QTime::fromMSecsSinceStartOfDay(msecs_passed)
                        .toString(msecs_passed < 3600000 ? "m:ss" : "h:mm:ss"));
  if (!(_flags & Timeout) && msecs_passed >= preferences()->msecs_total &&
      preferences()->msecs_total > 0) {
    _flags |= Timeout;
    updateTimeout();
  }
  if (page_target_time == UINT32_MAX) {
    QPalette palette(passed->palette());
    palette.setColor(QPalette::Base, colormap.last());
    passed->setPalette(palette);
  } else {
    const qint32 diff = page_target_time - msecs_passed;
    QPalette palette(passed->palette());
    palette.setColor(QPalette::Base, time2color(diff));
    passed->setPalette(palette);
  }
}

void TimerWidget::changePassed()
{
  const quint32 new_msecs_passed =
      QTime::fromString(passed->text(),
                        passed->text().count(":") == 2 ? "h:mm:ss" : "m:ss")
          .msecsSinceStartOfDay();
  debug_msg(DebugWidgets, "new msecs passed:" << new_msecs_passed);
  if (preferences()->msecs_passed == UINT_LEAST32_MAX)
    WritableGlobalPreferences::writable()->target_time =
        QDateTime::currentDateTimeUtc().addMSecs(preferences()->msecs_total -
                                                 new_msecs_passed);
  else
    WritableGlobalPreferences::writable()->msecs_passed = new_msecs_passed;
  if (preferences()->msecs_total > 0 &&
      new_msecs_passed >= preferences()->msecs_total)
    _flags |= Timeout;
  else
    _flags &= ~Timeout;
  updateTimeout();
  updateFullText();
}

void TimerWidget::changeTotal()
{
  setTotalTime(QTime::fromString(
      total->text(), total->text().count(":") == 2 ? "h:mm:ss" : "m:ss"));
}

void TimerWidget::setTotalTime(const QTime time) noexcept
{
  debug_msg(DebugWidgets, time << time.isValid());
  if (time.isValid()) {
    const quint32 msecs_total = time.msecsSinceStartOfDay();
    if (preferences()->msecs_passed == UINT_LEAST32_MAX) {
      WritableGlobalPreferences::writable()->target_time =
          preferences()->target_time.addMSecs(msecs_total -
                                              preferences()->msecs_total);
      if (msecs_total > 0 &&
          QDateTime::currentDateTimeUtc() >= preferences()->target_time)
        _flags |= Timeout;
      else
        _flags &= ~Timeout;
    } else {
      if (msecs_total > 0 && msecs_total <= preferences()->msecs_passed)
        _flags |= Timeout;
      else
        _flags &= ~Timeout;
    }
    updateTimeout();
    WritableGlobalPreferences::writable()->msecs_total = msecs_total;
    updateFullText();
  }
}

void TimerWidget::startTimer() noexcept
{
  QPalette timer_palette(passed->palette());
  timer_palette.setColor(QPalette::Text, Qt::black);
  passed->setPalette(timer_palette);
  if (preferences()->msecs_passed != UINT_LEAST32_MAX) {
    WritableGlobalPreferences::writable()->target_time =
        QDateTime::currentDateTimeUtc().addMSecs(preferences()->msecs_total -
                                                 preferences()->msecs_passed);
    WritableGlobalPreferences::writable()->msecs_passed = UINT_LEAST32_MAX;
  }
  timer_id = QWidget::startTimer(1000);
  emit updateStatus(StartStopTimer, 1);
}

void TimerWidget::stopTimer() noexcept
{
  if (timer_id != -1) {
    killTimer(timer_id);
    timer_id = -1;
  }
  QPalette palette(passed->palette());
  palette.setColor(QPalette::Text, Qt::gray);
  passed->setPalette(palette);
  if (preferences()->msecs_passed == UINT_LEAST32_MAX) {
    WritableGlobalPreferences::writable()->msecs_passed =
        preferences()->msecs_total -
        QDateTime::currentDateTimeUtc().msecsTo(preferences()->target_time);
    updateFullText();
  }
  emit updateStatus(StartStopTimer, 0);
}

void TimerWidget::handleAction(const Action action) noexcept
{
  switch (action) {
    case StartTimer:
      startTimer();
      break;
    case StopTimer:
      stopTimer();
      break;
    case StartStopTimer:
      if (preferences()->msecs_passed == UINT_LEAST32_MAX)
        stopTimer();
      else
        startTimer();
      break;
    case ResetTimePassed:
      if (preferences()->msecs_passed == UINT_LEAST32_MAX)
        WritableGlobalPreferences::writable()->target_time =
            QDateTime::currentDateTimeUtc().addMSecs(
                preferences()->msecs_total);
      else
        WritableGlobalPreferences::writable()->msecs_passed = 0;
      _flags &= ~Timeout;
      updateTimeout();
      updateFullText();
      break;
    default:
      break;
  }
}

quint32 TimerWidget::timePassed() const noexcept
{
  return preferences()->msecs_passed == UINT_LEAST32_MAX
             ? preferences()->msecs_total -
                   QDateTime::currentDateTimeUtc().msecsTo(
                       preferences()->target_time)
             : preferences()->msecs_passed;
}

void TimerWidget::mouseDoubleClickEvent(QMouseEvent *)
{
  if (passed->hasFocus()) changePassed();
  if ((_flags & SetTimeWithoutConfirmation) ||
      QMessageBox::question(
          this, tr("Timer"),
          tr("Save current time as target end time for current slide?"),
          QMessageBox::Yes | QMessageBox::No,
          _flags & SetTimerConfirmationDefault
              ? QMessageBox::Yes
              : QMessageBox::No) == QMessageBox::Yes) {
    page_target_time = timePassed();
    emit setTimeForPage(preferences()->page, page_target_time);
    updateFullText();
  }
}

QColor TimerWidget::time2color(const qint32 time) const noexcept
{
  const auto next_it = colormap.lowerBound(time);
  if (next_it == colormap.cend()) return colormap.last();
  if (next_it == colormap.cbegin()) return colormap.first();

  const int diff = next_it.key() - time;
  const int total = next_it.key() - std::prev(next_it).key();
  return QColor(
      ((total - diff) * qRed(*next_it) + diff * qRed(*std::prev(next_it))) /
          total,
      ((total - diff) * qGreen(*next_it) + diff * qGreen(*std::prev(next_it))) /
          total,
      ((total - diff) * qBlue(*next_it) + diff * qBlue(*std::prev(next_it))) /
          total);
}

void TimerWidget::updatePage(const int slide, const int page)
{
  page_target_time = UINT32_MAX;
  emit getTimeForPage(page, page_target_time);
  updateText();
}
