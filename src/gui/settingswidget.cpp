// SPDX-FileCopyrightText: 2022 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: GPL-3.0-or-later OR AGPL-3.0-or-later

#include "src/gui/settingswidget.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTextEdit>
#include <QVariant>

#include "src/drawing/tool.h"
#include "src/gui/actionbutton.h"
#include "src/gui/keyinputlabel.h"
#include "src/master.h"
#include "src/names.h"
#include "src/preferences.h"

#if (QT_VERSION_MAJOR >= 6)
#define QCHECKBOX_CLICKED &QCheckBox::clicked
#else
#define QCHECKBOX_CLICKED QOverload<bool>::of(&QCheckBox::clicked)
#endif

SettingsWidget::SettingsWidget(QWidget *parent)
    : QTabWidget(parent),
      manual(new QTextEdit(this)),
      misc(new QWidget(this)),
      shortcuts(new QWidget(this)),
      rendering(new QWidget(this))
{
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setMinimumSize(30, 20);

  initManual();
  initMisc();
  initShortcuts();
  initRendering();

  addTab(manual, tr("help"));
  QScroller::grabGesture(manual);
  QScrollArea *scroll_area = new QScrollArea(this);
  scroll_area->setWidget(misc);
  addTab(scroll_area, tr("misc"));
  QScroller::grabGesture(scroll_area);
  scroll_area = new QScrollArea(this);
  scroll_area->setWidget(rendering);
  addTab(scroll_area, tr("rendering"));
  QScroller::grabGesture(scroll_area);
  scroll_area = new QScrollArea(this);
  scroll_area->setWidget(shortcuts);
  addTab(scroll_area, tr("shortcuts"));
  QScroller::grabGesture(scroll_area);
}

void SettingsWidget::initManual()
{
  manual->setReadOnly(true);
  manual->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                  Qt::LinksAccessibleByMouse |
                                  Qt::LinksAccessibleByKeyboard);
  QFile file(preferences()->manual_file);
  if (file.open(QFile::ReadOnly | QFile::Text))
    manual->setHtml(file.readAll());
  else
    manual->setPlainText("Manual file not found or not readable:\n" +
                         preferences()->manual_file);
}

void SettingsWidget::initShortcuts()
{
  QFormLayout *layout = new QFormLayout();
  QLabel *explanation_label = new QLabel(
      tr("Change shortcuts by clicking on them and typing the new shortcut. "
         "Remove shortcuts with delete key. "
         "Actions are documented in man 5 beamerpresenter-ui (in \"tool "
         "selector\")."),
      shortcuts);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);

  KeyInputLabel *input_shortcut;
  QMap<Action, QString> action_to_string;
  for (auto it = get_string_to_action().cbegin();
       it != get_string_to_action().cend(); ++it)
    action_to_string[*it] = it.key();
  const auto &key_actions = preferences()->key_actions;
  for (auto it = key_actions.cbegin(); it != key_actions.cend(); ++it) {
    input_shortcut = new KeyInputLabel(it.key(), *it, shortcuts);
    QLabel *label = new QLabel(
        tr(action_to_string.value(*it).toLatin1().constData()), shortcuts);
    label->setToolTip(ActionButton::tr(action_to_description(*it)));
    layout->addRow(label, input_shortcut);
  }
  QMap<Tool::BasicTool, QString> tool_to_string;
  const auto &string_to_tool = get_string_to_tool();
  for (auto it = string_to_tool.cbegin(); it != string_to_tool.cend(); ++it)
    tool_to_string[*it] = it.key();
  const auto &key_tools = preferences()->key_tools;
  for (auto it = key_tools.cbegin(); it != key_tools.cend(); ++it) {
    input_shortcut = new KeyInputLabel(it.key(), *it, shortcuts);
    QLabel *label = new QLabel(
        Tool::tr(
            tool_to_string.value((*it)->tool(), "none").toStdString().c_str()),
        shortcuts);
    label->setToolTip((*it)->description());
    layout->addRow(label, input_shortcut);
  }
  QPushButton *add_shortcut_button =
      new QPushButton(tr("Add new shortcut"), shortcuts);
  connect(add_shortcut_button, &QPushButton::clicked, this,
          &SettingsWidget::appendShortcut);
  layout->addRow(add_shortcut_button);

  shortcuts->setLayout(layout);
}

void SettingsWidget::initRendering()
{
  QFormLayout *layout = new QFormLayout();

  // Cache management
  QLabel *explanation_label = new QLabel(
      tr("Set general settings for rendering. Many oft these settings "
         "only take effect after restarting the program.\n\n"
         "Configure cache. Slides are rendered to compressed cache. "
         "These settings defined the allowed cache size. Negative "
         "values are interpreted as infinity."),
      rendering);
  explanation_label->setTextFormat(Qt::PlainText);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);

  QDoubleSpinBox *memory_box = new QDoubleSpinBox(rendering);
  memory_box->setMinimum(-1.);
  memory_box->setMaximum(4096.);
  memory_box->setValue(preferences()->max_memory / 1048596);
#if (QT_VERSION_MAJOR >= 6)
  connect(memory_box, &QDoubleSpinBox::valueChanged,
          WritableGlobalPreferences::writable(), &Preferences::setMemory);
#else
  connect(memory_box, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          WritableGlobalPreferences::writable(), &Preferences::setMemory);
#endif
  layout->addRow(tr("cache memory (MiB)"), memory_box);

  QSpinBox *spin_box = new QSpinBox(rendering);
  spin_box->setMinimum(-1);
  spin_box->setMaximum(10000);
  spin_box->setValue(preferences()->max_cache_pages);
#if (QT_VERSION_MAJOR >= 6)
  connect(spin_box, &QSpinBox::valueChanged,
          WritableGlobalPreferences::writable(), &Preferences::setCacheSize);
#else
  connect(spin_box, QOverload<int>::of(&QSpinBox::valueChanged),
          WritableGlobalPreferences::writable(), &Preferences::setCacheSize);
#endif
  layout->addRow(tr("max. slides in cache"), spin_box);

  // Renderer
  explanation_label = new QLabel(
      tr("Depending on your installation, different PDF engines may "
         "be available. Note that using an external renderer requires "
         "a proper configuration of rendering command and rendering "
         "arguments as documented in man 5 beamerpresenter.conf."),
      rendering);
  explanation_label->setTextFormat(Qt::PlainText);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);

  QComboBox *select_renderer = new QComboBox(rendering);
#ifdef USE_MUPDF
  select_renderer->addItem("MuPDF", QVariant::fromValue(PdfEngine::MuPdf));
#endif
#ifdef USE_POPPLER
  select_renderer->addItem("Poppler", QVariant::fromValue(PdfEngine::Poppler));
#endif
#ifdef USE_QTPDF
  select_renderer->addItem("QtPDF", QVariant::fromValue(PdfEngine::QtPDF));
#endif
#ifdef USE_EXTERNAL_RENDERER
#ifdef USE_MUPDF
  select_renderer->addItem("MuPDF + external",
                           QVariant::fromValue(PdfEngine::MuPdf));
#endif
#ifdef USE_POPPLER
  select_renderer->addItem("Poppler + external",
                           QVariant::fromValue(PdfEngine::Poppler));
#endif
#ifdef USE_QTPDF
  select_renderer->addItem("QtPDF + external",
                           QVariant::fromValue(PdfEngine::QtPDF));
#endif
#endif  // USE_EXTERNAL_RENDERER
  connect(select_renderer, &QComboBox::currentTextChanged,
          WritableGlobalPreferences::writable(), &Preferences::setRenderer);
  layout->addRow(tr("Renderer (requires restart)"), select_renderer);

#ifdef USE_EXTERNAL_RENDERER
  QLineEdit *line_edit = new QLineEdit(rendering);
  line_edit->setText(preferences()->rendering_command);
  connect(line_edit, &QLineEdit::textChanged,
          WritableGlobalPreferences::writable(),
          &Preferences::setRenderingCommand);
  layout->addRow(tr("rendering command"), line_edit);

  line_edit = new QLineEdit(rendering);
  line_edit->setText(preferences()->rendering_arguments.join(","));
  connect(line_edit, &QLineEdit::textChanged,
          WritableGlobalPreferences::writable(),
          &Preferences::setRenderingArguments);
  layout->addRow(tr("rendering arguments"), line_edit);
#endif  // USE_EXTERNAL_RENDERER

  // Page part threshold
  explanation_label = new QLabel(
      tr("Some programs (like LaTeX beamer) can create PDF pages split "
         "into one half for the audience one half for the speaker. "
         "This is assumed by BeamerPresenter if the aspect ratio "
         "(width/height) of the first slide lies above this threshold. "
         "This setting only takes effect after restarting BeamerPresenter."),
      rendering);
  explanation_label->setTextFormat(Qt::PlainText);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);

  QDoubleSpinBox *page_part_box = new QDoubleSpinBox(rendering);
  page_part_box->setMinimum(1.);
  page_part_box->setMaximum(20.);
  page_part_box->setValue(preferences()->page_part_threshold);
#if (QT_VERSION_MAJOR >= 6)
  connect(page_part_box, &QDoubleSpinBox::valueChanged,
          WritableGlobalPreferences::writable(),
          &Preferences::setPagePartThreshold);
#else
  connect(page_part_box, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          WritableGlobalPreferences::writable(),
          &Preferences::setPagePartThreshold);
#endif
  layout->addRow(tr("page part threshold"), page_part_box);
  color_button = new QPushButton(this);
  QPalette palette = color_button->palette();
  palette.setBrush(QPalette::Button, preferences()->background_brush);
  color_button->setPalette(palette);
  layout->addRow(tr("slide background color"), color_button);
  connect(color_button, &QPushButton::clicked, this,
          &SettingsWidget::setBackgroundBrushColor);
  connect(this, &SettingsWidget::setPageBackgroundColor,
          WritableGlobalPreferences::writable(),
          &Preferences::setPageBackgroundColor);

  rendering->setLayout(layout);
}

void SettingsWidget::setBackgroundBrushColor()
{
  QPalette palette = color_button->palette();
  const QColor color = QColorDialog::getColor(palette.button().color(), this,
                                              tr("slide background color"),
                                              QColorDialog::ShowAlphaChannel);
  if (!color.isValid()) return;
  emit setPageBackgroundColor(color);
  palette.setColor(QPalette::Button, color);
  color_button->setPalette(palette);
}

void SettingsWidget::initMisc()
{
  QFormLayout *layout = new QFormLayout();

  // GUI config file
  QLabel *explanation_label = new QLabel(
      tr("Configuration file for the graphical user interface (GUI). "
         "This file defines which widgets are shown in the modular GUI. "
         "The file is JSON formatted and documented in man 5 "
         "beamerpresenter-ui. "
         "Examples can be found in ") +
          DOC_PATH "/examples. " +
          tr("This setting only takes effect after restarting BeamerPresenter. "
             "Note "
             "that with an invalid GUI configuration file BeamerPresenter "
             "cannot start. "
             "Currently the default GUI configuration file is ") +
          preferences()->gui_config_file + ".",
      misc);
  explanation_label->setTextFormat(Qt::PlainText);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);
  QPushButton *select_file_button =
      new QPushButton(tr("select GUI configuration file"), misc);
  connect(select_file_button, &QPushButton::clicked, this,
          &SettingsWidget::setGuiConfigFile);
  layout->addRow(select_file_button);

  // Drawing history settings
  explanation_label =
      new QLabel(tr("Number of drawing history steps (undo/redo). Drawing "
                    "history is kept separately for each slide."),
                 misc);
  explanation_label->setTextFormat(Qt::PlainText);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);

  QSpinBox *spin_box = new QSpinBox(misc);
  spin_box->setValue(preferences()->history_length_visible_slides);
#if (QT_VERSION_MAJOR >= 6)
  connect(spin_box, &QSpinBox::valueChanged,
          WritableGlobalPreferences::writable(),
          &Preferences::setHistoryVisibleSlide);
#else
  connect(spin_box, QOverload<int>::of(&QSpinBox::valueChanged),
          WritableGlobalPreferences::writable(),
          &Preferences::setHistoryVisibleSlide);
#endif
  spin_box->setMinimum(0);
  spin_box->setMaximum(1000);
  layout->addRow(tr("History length visible slides"), spin_box);

  spin_box = new QSpinBox(misc);
  spin_box->setValue(preferences()->history_length_hidden_slides);
#if (QT_VERSION_MAJOR >= 6)
  connect(spin_box, &QSpinBox::valueChanged,
          WritableGlobalPreferences::writable(),
          &Preferences::setHistoryHiddenSlide);
#else
  connect(spin_box, QOverload<int>::of(&QSpinBox::valueChanged),
          WritableGlobalPreferences::writable(),
          &Preferences::setHistoryHiddenSlide);
#endif
  spin_box->setMinimum(0);
  spin_box->setMaximum(1000);
  layout->addRow(tr("History length hidden slides"), spin_box);

  // Enable/disable logging output
  explanation_label =
      new QLabel(tr("If opened in a terminal, slide changes can be logged to "
                    "standard output with a time stamp."),
                 misc);
  explanation_label->setTextFormat(Qt::PlainText);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);

  QCheckBox *box = new QCheckBox(tr("log slide changes"), misc);
  box->setChecked(preferences()->global_flags & Preferences::LogSlideChanges);
  connect(box, QCHECKBOX_CLICKED, WritableGlobalPreferences::writable(),
          &Preferences::setLogSlideChanges);
  layout->addRow(box);

  // Enable/disable automatic slide changes
  explanation_label =
      new QLabel(tr("Enable/disable automatic slide switching if durations for "
                    "slides are defined in the PDF."),
                 misc);
  explanation_label->setTextFormat(Qt::PlainText);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);

  box = new QCheckBox(tr("automatic slide changes"), misc);
  box->setChecked(preferences()->global_flags & Preferences::AutoSlideChanges);
  connect(box, QCHECKBOX_CLICKED, WritableGlobalPreferences::writable(),
          &Preferences::setAutoSlideChanges);
  layout->addRow(box);

  // Enable/disable external links
  box = new QCheckBox(tr("open external links"), misc);
  box->setChecked(preferences()->global_flags & Preferences::OpenExternalLinks);
  connect(box, QCHECKBOX_CLICKED, WritableGlobalPreferences::writable(),
          &Preferences::setExternalLinks);
  layout->addRow(box);

  // Enable/disable path finalization
  box = new QCheckBox(tr("finalize drawn paths"), misc);
  box->setChecked(preferences()->global_flags &
                  Preferences::FinalizeDrawnPaths);
  connect(box, QCHECKBOX_CLICKED, WritableGlobalPreferences::writable(),
          &Preferences::setFinalizePaths);
  layout->addRow(box);

  // Drawing mode
  explanation_label = new QLabel(
      "<br>" +
          tr("Define how drawings should be handled if multiple "
             "successive pages share the same page label (e.g. "
             "because they show overlays of the same slide).") +
          "\n<ul>"
          "<li>\"per page\": " +
          tr("all pages are treated separately.") +
          "</li>\n"
          "<li>\"per slide\": " +
          tr("pages with the same label also have the same drawings.") +
          "</li>\n"
          "<li>\"cumulative\": " +
          tr("when reaching a page with no drawings (and no drawings history), "
             "which has the same label as the previous page, the drawings from "
             "the previous page are copied to this page.") +
          "</li>"
          "</ul>",
      misc);
  explanation_label->setTextFormat(Qt::RichText);
  explanation_label->setWordWrap(true);
  layout->addRow(explanation_label);

  QComboBox *combo_box = new QComboBox(misc);
  for (auto it = get_string_to_overlay_mode().cbegin();
       it != get_string_to_overlay_mode().cend(); ++it)
    combo_box->addItem(it.key());
  combo_box->setCurrentText(
      get_string_to_overlay_mode().key(preferences()->overlay_mode));
  connect(combo_box, &QComboBox::currentTextChanged,
          WritableGlobalPreferences::writable(), &Preferences::setOverlayMode);
  layout->addRow(tr("drawing mode for overlays"), combo_box);

  box = new QCheckBox(tr("automatically load pdfpc files"), rendering);
  box->setChecked(preferences()->global_flags & Preferences::AutoloadPdfpc);
  connect(box, QCHECKBOX_CLICKED, WritableGlobalPreferences::writable(),
          &Preferences::setAutoloadPdfpc);
  layout->addRow(box);
  select_file_button = new QPushButton(
      tr("select pdfpc/JSON file for overlays and notes"), rendering);
  connect(select_file_button, &QPushButton::clicked, this,
          &SettingsWidget::setPdfpcJSONFile);
  layout->addRow(select_file_button);

  misc->setLayout(layout);
}

void SettingsWidget::appendShortcut()
{
  QComboBox *select_menu = new QComboBox(shortcuts);
#if (QT_VERSION >= QT_VERSION_CHECK(5, 15, 0))
  select_menu->setPlaceholderText(tr("tool/action"));
#endif
  select_menu->addItem(tr("tool..."),
                       QVariant::fromValue(Action::InvalidAction));
  for (auto it = get_string_to_action().cbegin();
       it != get_string_to_action().cend(); ++it)
    select_menu->addItem(tr(it.key().toLatin1().constData()),
                         QVariant::fromValue(*it));
  select_menu->setCurrentText("");
  select_menu->setEditable(true);
  KeyInputLabel *input_shortcut =
      new KeyInputLabel(0, Action::InvalidAction, shortcuts);
  QFormLayout *layout = static_cast<QFormLayout *>(shortcuts->layout());
#if (QT_VERSION >= QT_VERSION_CHECK(5, 14, 0))
  connect(select_menu, &QComboBox::textActivated, input_shortcut,
          &KeyInputLabel::changeAction);
#else
  connect(select_menu, &QComboBox::currentTextChanged, input_shortcut,
          &KeyInputLabel::changeAction);
#endif
  connect(input_shortcut, &KeyInputLabel::sendName, select_menu,
          &QComboBox::setEditText);
  layout->addRow(select_menu, input_shortcut);
}

void SettingsWidget::setGuiConfigFile()
{
  const QString newfile = QFileDialog::getOpenFileName(
      this, tr("Select new GUI configuration file"),
      preferences()->gui_config_file, tr("JSON files (*.json);;all files (*)"));
  if (!newfile.isNull() &&
      WritableGlobalPreferences::writable()->setGuiConfigFile(newfile))
    setTabText(1, tr("misc (restart required)"));
}

void SettingsWidget::setPdfpcJSONFile()
{
  const QString newfile = QFileDialog::getOpenFileName(
      this, tr("Select JSON file containing overlay information"), "",
      tr("pdfpc/JSON files (*.pdfpc *.json);;all files (*)"));
  if (!newfile.isNull()) master()->loadPdfpcJSON(newfile);
}
