// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#ifndef NOTESWIDGET_H
#define NOTESWIDGET_H

#include <QMap>
#include <QString>
#include <QTextEdit>
#include <QtConfig>

#include "src/config.h"

class QKeyEvent;
class QXmlStreamReader;
class QXmlStreamWriter;

/**
 * @brief Editable rich-text notes
 *
 * Widget in flexible GUI of BeamerPresenter which represents rich text
 * which can be edited, loaded, and saved (as HTML).
 *
 * File structure of the saved xml files: xml tree with root element
 * "speakernotes" containing elements "page". Each "page" element has
 * an attribute "label" representing the page label (a string) and
 * contains HTML text.
 *
 * Keyboard shortcuts for the page label are hard-coded:
 * Ctrl+S for saving, Ctrl+Shift+S for saving under a different file
 * name and Ctrl+O for loading a different file.
 */
class NotesWidget : public QTextEdit
{
  Q_OBJECT

  /// Identifier distinguishing this from other notes widgets.
  QString _id;

  /// File where everything is loaded / saved.
  QString file_path;

  /// Map slide labels to HTML text
  QMap<QString, QString> text_per_slide;

  /// Currently shown page label.
  QString page_label;

  /// Are notes saved per page? Otherwise per page label.
  bool per_page;

 public:
  /// Nearly trivial constructor.
  /// @param per_page if true, save notes per page number instead of per page
  /// label
  /// @param parent parent widget, passed to QTextEdit
  NotesWidget(const bool per_page = false, const QString &id = "",
              QWidget *parent = nullptr);

  /// Preferred height depends on width (as required for FlexLayout).
  /// @return true
  bool hasHeightForWidth() const noexcept override { return true; }

  /// Load (only) notes from file. (Not the same as loading drawings and notes!)
  /// @param filename file from which notes should be loaded.
  /// @see saveNotes()
  /// @see load()
  /// @see loadDrawings()
  /// @see readNotes()
  void loadNotes(const QString &filename);

  /// Save (only) notes to file. (Not the same as saving drawings and notes!)
  /// @param filename file to which notes should be saved.
  /// @see loadNotes()
  /// @see save()
  /// @see saveDrawings()
  /// @see writeNotes()
  void saveNotes(const QString &filename);

  /// Get id of this notes widget.
  const QString &id() const noexcept { return _id; }

 protected:
  /// Handle keyboard shortcuts (mainly save and load).
  void keyPressEvent(QKeyEvent *event) override;

 public slots:
  /// Choose a file using file dialog and load notes from that file.
  /// @see save()
  /// @see loadNotes()
  /// @see readNotes()
  /// @see loadDrawings()
  void load();

  /// Save notes to given file name. If *filename* is empty, choose from file
  /// dialog.
  /// @see load()
  /// @see saveNotes()
  /// @see writeNotes()
  /// @see saveDrawings()
  void save(QString filename);

  /// Switch page, show notes for new page.
  /// @param page page index of target page
  void pageChanged(const int slide, const int page);

  /// Write notes to stream. This is used for saving as own notes file and
  /// for saving notes as part of a BeamerPresenter file including also
  /// drawings and slide times.
  /// @param writer XML stream writer. A \<speakernotes\> element will be
  ///        written to writer.
  /// @see readNotes()
  /// @see save()
  /// @see saveNotes()
  /// @see saveDrawings()
  void writeNotes(QXmlStreamWriter &writer);

  /// Load notes from stream. This is used for loading as own notes file and
  /// for loading notes as part of a BeamerPresenter file including also
  /// drawings and slide times.
  /// @param reader XML stream reader which initially points to xml element
  ///        \<speakernotes\>. The whole \<speakernotes\> element will be
  ///        read.
  /// @see writeNotes()
  /// @see load()
  /// @see loadNotes()
  /// @see loadDrawings()
  void readNotes(QXmlStreamReader &reader);

  /// Set notes string for slide.
  /// @param number page index as string, used as key if per_page == true
  /// @param label page label, used as key if per_page == false
  /// @param note plain text or markdown note
  void setPageNote(const QString number, const QString label,
                   const QString note);

 signals:
  /// Notify Master of new unsaved changes.
  void newUnsavedChanges();

  /// Notify Master that drawings should be saved.
  /// @param filename file path for saving drawings, should never be empty.
  void saveDrawings(const QString filename);

  /// Notify Master that drawings should be loaded.
  /// @param filename file path for loading drawings, should never be empty.
  void loadDrawings(const QString filename);
};

#if (QT_VERSION_MAJOR >= 6)
#define combine_keys(keys) (keys).toCombined()
#else
#define combine_keys(keys) (keys)
#endif

#endif  // NOTESWIDGET_H
