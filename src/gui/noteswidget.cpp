// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#include "src/gui/noteswidget.h"

#include <QFile>
#include <QFileDialog>
#include <QKeyEvent>
#include <QMimeDatabase>
#include <QMimeType>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include "src/log.h"
#include "src/preferences.h"
#include "src/rendering/pdfdocument.h"

NotesWidget::NotesWidget(const bool per_page, const QString &id,
                         QWidget *parent)
    : QTextEdit(parent), _id(id), per_page(per_page)
{
  setReadOnly(false);
  setAutoFormatting(QTextEdit::AutoAll);
}

void NotesWidget::loadNotes(const QString &filename)
{
  // TODO: maybe find better implementation for this:
  // use direct function calls instead of signal-slot mechanism
  QFile file(filename);
  if (!file.open(QFile::ReadOnly | QFile::Text)) {
    qWarning() << "Failed to open notes file:" << filename;
    return;
  }
  QXmlStreamReader reader(&file);
  reader.readNext();
  while (!reader.atEnd()) {
    if (reader.readNext() == QXmlStreamReader::StartElement) {
      // TODO: QStringView::toUtf8() is probably inefficient for string
      // comparison
      if (reader.name().toUtf8() == "speakernotes")
        readNotes(reader);
      else if (reader.name().toUtf8() == "xournal") {
        // Trying to read a file which should probably be read by PdfMaster.
        reader.clear();
        file.close();
        emit loadDrawings(filename);
        return;
      }
      break;
    }
  }
  if (!reader.hasError())
    file_path = filename;
  else
    qWarning() << "Parsing xml failed:" << reader.errorString();
}

void NotesWidget::readNotes(QXmlStreamReader &reader)
{
  /// It is assumed that reader has just reached the beginning of speakernotes
  debug_msg(DebugWidgets, "start reading notes for notes widget");
  if (reader.name().toUtf8() != "speakernotes") {
    qWarning() << "Tried to read notes, but current element in xml tree is not "
                  "speakernotes";
    return;
  }
  const auto attr = reader.attributes();
  const QString new_id = attr.value("id").toString();
  if (!new_id.isEmpty() && !_id.isEmpty() && new_id != _id) {
    qInfo() << "Ignoring notes due to id mismatch:" << new_id << _id;
    return;
  }
  const QString identifier = attr.value("identifier").toString();
  if (identifier == "number")
    per_page = true;
  else if (identifier == "label")
    per_page = false;
  while (reader.readNextStartElement()) {
    debug_msg(DebugWidgets, "read notes:" << reader.name());
    if (reader.name().toUtf8() == "page-notes") {
      const QString label =
          reader.attributes().value(per_page ? "number" : "label").toString();
      if (!label.isEmpty())
        text_per_slide.insert(label, reader.readElementText());
    }
    if (!reader.isEndElement()) reader.skipCurrentElement();
  }

  if (preferences()->document) {
    const QString label =
        preferences()->document->pageLabel(preferences()->page);
    if (!label.isEmpty()) setHtml(text_per_slide.value(label));
  }
}

void NotesWidget::save(QString filename)
{
  if (filename.isEmpty()) {
    filename = QFileDialog::getSaveFileName(
        nullptr, tr("Save notes"), "",
        tr("Note files (*.xml);;BeamerPresenter/Xournal++ files (*.bpr "
           "*.xopp);;All files (*)"));
    if (filename.isNull()) {
      qWarning() << "Saving notes cancelled: empty filename";
      return;
    }
    if (filename.endsWith(".bpr", Qt::CaseInsensitive) ||
        filename.endsWith(".xopp", Qt::CaseInsensitive))
      emit saveDrawings(filename);
    else
      saveNotes(filename);
  } else
    saveNotes(file_path);
}

void NotesWidget::load()
{
  const QString filename = QFileDialog::getOpenFileName(
      nullptr, tr("Open notes"), "",
      tr("Note files (*.xml);;BeamerPresenter/Xournal++ files (*.bpr *.xopp "
         "*.xoj *.xml);;All files (*)"));
  if (filename.isNull()) {
    qWarning() << "Loading notes cancelled: empty filename";
    return;
  }
  QMimeDatabase db;
  const QMimeType type =
      db.mimeTypeForFile(filename, QMimeDatabase::MatchContent);
  if (type.name() == "application/gzip")
    emit loadDrawings(filename);
  else if (type.name() == "application/xml")
    loadNotes(filename);
  else
    qWarning() << "Could not open file: unknown file type" << type.name();
}

void NotesWidget::saveNotes(const QString &filename)
{
  QFile file(filename);
  if (!file.open(QFile::WriteOnly | QFile::Text) || !file.isWritable()) {
    qWarning() << "Saving notes failed, failed to open file for writing:"
               << filename;
    return;
  }
  QXmlStreamWriter writer(&file);
  writer.setAutoFormatting(true);
  writer.setAutoFormattingIndent(0);
  writer.writeStartDocument();
  writeNotes(writer);
  writer.writeEndDocument();
  if (writer.hasError())
    qWarning() << "Saving notes resulted in an error";
  else
    file_path = filename;
}

void NotesWidget::writeNotes(QXmlStreamWriter &writer)
{
  if (!document()->isEmpty()) text_per_slide.insert(page_label, toHtml());
  writer.writeStartElement("speakernotes");
  writer.writeAttribute("identifier", per_page ? "number" : "label");
  if (!_id.isEmpty()) writer.writeAttribute("id", _id);
  const QString label = per_page ? "number" : "label";
  for (auto it = text_per_slide.cbegin(); it != text_per_slide.cend(); ++it) {
    if (!it->isEmpty()) {
      writer.writeStartElement("page-notes");
      writer.writeAttribute(label, it.key());
      writer.writeCharacters(it.value());
      writer.writeEndElement();
    }
  }
  writer.writeEndElement();
}

void NotesWidget::keyPressEvent(QKeyEvent *event)
{
#if (QT_VERSION_MAJOR >= 6)
  switch (event->keyCombination().toCombined())
#else
  switch (event->key() | event->modifiers())
#endif
  {
    case combine_keys(Qt::ControlModifier | Qt::Key_O):
      load();
      event->accept();
      break;
    case combine_keys(Qt::ControlModifier | Qt::Key_S):
      save(file_path);
      event->accept();
      break;
    case combine_keys(Qt::ControlModifier | Qt::ShiftModifier | Qt::Key_S):
      save("");
      event->accept();
      break;
    case combine_keys(Qt::ControlModifier | Qt::Key_Plus):
    case combine_keys(Qt::ShiftModifier | Qt::ControlModifier | Qt::Key_Plus):
      zoomIn();
      break;
    case combine_keys(Qt::ControlModifier | Qt::Key_Minus):
      zoomOut();
      break;
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
      event->ignore();
      break;
    default:
      QTextEdit::keyPressEvent(event);
  }
}

void NotesWidget::pageChanged(const int slide, const int page)
{
  if (document()->isEmpty()) {
    const QMap<QString, QString>::iterator it = text_per_slide.find(page_label);
    if (it != text_per_slide.end()) {
      emit newUnsavedChanges();
      text_per_slide.erase(it);
    }
  } else {
    // Save current text to old page_label.
    const QString string = toHtml();
    if (text_per_slide.value(page_label) != string) {
      text_per_slide.insert(page_label, string);
      emit newUnsavedChanges();
    }
  }
  // Update page_label.
  if (per_page)
    page_label = QString::number(page);
  else if (page >= 0) {
    const auto doc = preferences()->document;
    page_label = doc ? doc->pageLabel(page) : "0";
  }
  // Load text from new page_label.
  setHtml(text_per_slide.value(page_label));
}

void NotesWidget::setPageNote(const QString number, const QString label,
                              const QString note)
{
  const QString &target = per_page ? number : label;
  if (target.isEmpty() || note.isEmpty()) return;
  QTextDocument *tmp_doc =
      target == page_label ? document() : document()->clone();
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
  tmp_doc->setMarkdown(note);
#else
  tmp_doc->setPlainText(note);
#endif
  text_per_slide[target] = tmp_doc->toHtml();
  if (tmp_doc != document()) delete tmp_doc;
  emit newUnsavedChanges();
}
