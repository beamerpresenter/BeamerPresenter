// SPDX-FileCopyrightText: 2023 Valentin Bruch <software@vbruch.eu>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <QByteArray>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMutex>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QUrl>
#include <memory>
#include <string>
#include <utility>

#include "src/config.h"
#include "src/log.h"
#include "src/preferences.h"
#include "src/rendering/mupdfdocument.h"

#ifndef FZ_VERSION_MAJOR
#define FZ_VERSION_MAJOR 0
#endif
#ifndef FZ_VERSION_MINOR
#define FZ_VERSION_MINOR 0
#endif

#if (FZ_VERSION_MAJOR < 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR < 22))
#include <iterator>
#endif

#if (FZ_VERSION_MAJOR < 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR < 22))
std::string roman(int number)
{
  int tens = number / 10;
  std::string roman;
  while (tens-- > 0) roman.push_back('x');
  int residual = number % 10;
  if (residual == 9)
    roman.append("ix");
  else if (residual >= 5) {
    roman.push_back('v');
    while (residual-- > 5) roman.push_back('i');
  } else if (residual == 4)
    roman.append("iv");
  else
    while (residual-- > 0) roman.push_back('i');
  return roman;
}

std::string decode_pdf_label(int number, const MuPdfDocument::label_item &item)
{
  std::string string = item.prefix ? item.prefix : "";
  if (!item.style) return string;
  number += item.start_value;
  switch (*item.style) {
    case '\0':
      break;
    case 'r':
      string.append(roman(number));
      break;
    case 'R': {
      std::string lower = roman(number);
      for (auto &it : lower) it += 0x20;
      string.append(lower);
      break;
    }
    case 'a': {
      int repetitions = number / 26 + 1;
      while (repetitions-- > 0) string.push_back(0x61 + (number % 26));
      break;
    }
    case 'A': {
      int repetitions = number / 26 + 1;
      while (repetitions-- > 0) string.push_back(0x41 + (number % 26));
      break;
    }
    default:
      // Decimal is the default.
      string.append(std::to_string(number));
      break;
  }
  debug_verbose(DebugRendering, number << QString::fromStdString(string));
  return string;
}
#endif  // FZ_VERSION < 1.22

MuPdfDocument::MuPdfDocument(const QString &filename)
    : PdfDocument(filename),
      mutex(new QMutex()),
      mutex_list(QVector<QMutex *>(FZ_LOCK_MAX))
{
  for (auto &it : mutex_list) it = new QMutex();
  // Load the document
  if (!loadDocument() && !doc) qFatal("Loading document failed");
}

MuPdfDocument::~MuPdfDocument()
{
  mutex->lock();
  for (auto page : std::as_const(pages)) fz_drop_page(ctx, (fz_page *)page);
  pdf_drop_document(ctx, doc);
  fz_drop_context(ctx);
  while (!mutex_list.isEmpty()) delete mutex_list.takeLast();

  // Delete embedded media data. TODO: better memory management?
  char *ptr;
  for (auto it = embedded_media.begin(); it != embedded_media.end();) {
    ptr = const_cast<char *>(it.value()->constData());
    it = embedded_media.erase(it);
    // memory was allocated by MuPDF using malloc
    std::free(ptr);
    // fz_free(ctx, ptr); // leads to a very weird error
  }
  embedded_media.clear();
  mutex->unlock();
  delete mutex;
}

bool MuPdfDocument::loadDocument()
{
  // Check if the file exists.
  const QFileInfo fileinfo(path);
  if (!fileinfo.exists() || !fileinfo.isFile()) {
    preferences()->showErrorMessage(
        tr("Error while loading file"),
        tr("Given filename is not a file: ") + fileinfo.baseName());
    return false;
  }

  // Check if the file has changed since last (re)load
  if (doc && fileinfo.lastModified() == lastModified) return false;
  mutex->lock();
  if (doc) {
    for (auto page : std::as_const(pages)) fz_drop_page(ctx, (fz_page *)page);
    pdf_drop_document(ctx, doc);
    flexible_page_sizes = -1;
  } else {
    // This code is mainly copied from MuPDF example files, see mupdf.com

    // Initialize the locking structure with function pointers to
    // the locking functions and to the user data. In this case
    // the user data is a pointer to the array of mutexes so the
    // locking functions can find the relevant lock to change when
    // they are called. This way we avoid global variables.

    fz_locks_context locks;
    locks.user = &mutex_list;
    locks.lock = lock_mutex;
    locks.unlock = unlock_mutex;

    // This is the main threads ctx function, so supply the
    // locking structure. This ctx will be used to parse all
    // the pages from the document.

    ctx = fz_new_context(nullptr, &locks, FZ_STORE_UNLIMITED);

    if (ctx == nullptr) {
      qCritical() << tr("Failed to create Fitz context");
      doc = nullptr;
      mutex->unlock();
      return false;
    }

    // Try to register default document handlers.
    fz_try(ctx) fz_register_document_handlers(ctx);
    fz_catch(ctx)
    {
      qCritical() << tr("MuPDF failed to register document handlers:")
                  << fz_caught_message(ctx);
      doc = nullptr;
      fz_drop_context(ctx);
      ctx = nullptr;
      mutex->unlock();
      return false;
    }
  }

  // Open the document.
  {
    // TODO: find out how to check system preferences for encoding
    // const QByteArray &pathdecoded = path.toLatin1();
    const QByteArray &pathdecoded = path.toUtf8();
    const char *name = pathdecoded.data();
    fz_try(ctx) doc = pdf_open_document(ctx, name);
    fz_catch(ctx)
    {
      preferences()->showErrorMessage(
          tr("Error while loading file"),
          tr("MuPDF cannot open document: ") + fz_caught_message(ctx));
      doc = nullptr;
      fz_drop_context(ctx);
      ctx = nullptr;
      mutex->unlock();
      return false;
    }
  }
  // Try to decrypt document if necessary
  if (pdf_needs_password(ctx, doc)) {
    qWarning() << "Document is locked.";
    // Use a QInputDialog to ask for the password.
    bool ok;
    QString const password = QInputDialog::getText(
        nullptr, tr("Document is locked!"),
        tr("Please enter password (leave empty to cancel)."),
        QLineEdit::Password, QString(), &ok);
    // Check if a password was entered.
    if (!ok || password.isEmpty() ||
        !pdf_authenticate_password(ctx, doc, password.toUtf8())) {
      preferences()->showErrorMessage(
          tr("Error while loading file"),
          tr("No or invalid password provided for locked document"));
      pdf_drop_document(ctx, doc);
      doc = nullptr;
      fz_drop_context(ctx);
      ctx = nullptr;
      mutex->unlock();
      return false;
    }
  }

  // Save the modification time.
  lastModified = fileinfo.lastModified();

  // Save number of pages.
  number_of_pages = pdf_count_pages(ctx, doc);

  pages.resize(number_of_pages);
  int i = 0;

  do {
    fz_try(ctx) pages[i] = pdf_load_page(ctx, doc, i);
    fz_catch(ctx) pages[i] = nullptr;
  } while (++i < number_of_pages);

  mutex->unlock();

  debug_msg(DebugRendering, "Loaded PDF document in MuPDF");
  return number_of_pages > 0;
}

const QSizeF MuPdfDocument::pageSize(const int page) const
{
  // Check if the page number is valid.
  if (!pages.value(page)) return QSizeF();

  fz_rect bbox;
  mutex->lock();
  fz_try(ctx)
  // Get bounding box.
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 23))
      bbox = pdf_bound_page(ctx, pages[page], FZ_MEDIA_BOX);
#else
      bbox = pdf_bound_page(ctx, pages[page]);
#endif
  fz_always(ctx) mutex->unlock();
  fz_catch(ctx) return QSizeF();

  // Convert bounding box to QSizeF.
  // bbox.x0 and bbox.y0 should be 0, but keep them anyway:
  return QSizeF(bbox.x1 - bbox.x0, bbox.y1 - bbox.y0);
}

#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 22))

void MuPdfDocument::loadPageLabels()
{
  if (!ctx || !doc) return;
  pageLabels.clear();

  char buffer[32];
  mutex->lock();
  fz_var(pageLabels);
  fz_try(ctx)
  {
    pdf_page_label(ctx, doc, 0, buffer, 32);
    pageLabels[0] = QString(buffer);
    for (int page = 1; page < number_of_pages; ++page) {
      pdf_page_label(ctx, doc, page, buffer, 32);
      if (pageLabels.last() != buffer) pageLabels[page] = QString(buffer);
    }
  }
  fz_always(ctx) mutex->unlock();
  fz_catch(ctx)
  {
    qWarning() << "Failed to load page labels:" << fz_caught_message(ctx);
    return;
  }

  if (pageLabels.empty()) return;
  if (pageLabels.size() == 1 && pageLabels.contains(0) && pageLabels[0] == "") {
    pageLabels.clear();
    return;
  }

  // Add all pages explicitly to pageLabels, which have an own outline entry.
  QMap<int, QString>::key_iterator it;
  for (const auto &entry : std::as_const(outline)) {
    if (entry.page < 0 || entry.page >= number_of_pages) continue;
    it = std::upper_bound(pageLabels.keyBegin(), pageLabels.keyEnd(),
                          entry.page);
    if (it != pageLabels.keyBegin() && *--it != entry.page)
      pageLabels.insert(entry.page, pageLabels[*it]);
  }

  debug_msg(DebugRendering, "Loaded page labels:" << pageLabels);
}

#else  // FZ_VERSION < 1.22

void MuPdfDocument::loadPageLabels()
{
  /* This implementation is designed for documents, in which page labels
   * consist only of prefixes. This is the case for presentations created
   * with LaTeX beamer with overlays.
   * Numbering styles other than decimal which are not explicitly given as
   * strings are currently not supported.
   *
   * This code is based on a patch by Stefan Radermacher and Albert Bloomfield
   * https://bugs.ghostscript.com/show_bug.cgi?id=695351 */

  if (!ctx || !doc) return;
  pageLabels.clear();

  mutex->lock();

  QMap<int, label_item> raw_labels;
  fz_var(raw_labels);
  fz_try(ctx)
  {
    pdf_obj *labels = pdf_dict_gets(
        ctx, pdf_dict_gets(ctx, pdf_trailer(ctx, doc), "Root"), "PageLabels");
    if (!pdf_is_dict(ctx, labels))
      fz_throw(ctx, 1, "document contains no labels");

    pdf_obj *nums = pdf_dict_gets(ctx, labels, "Nums");
    if (!pdf_is_array(ctx, nums))
      fz_throw(ctx, 2, "document contains no valid labels");

    // Read the raw labels from the PDF
    const int len_minus_one = pdf_array_len(ctx, nums) - 1;
    pdf_obj *val;

    // TODO: check if this implementation is reasonable.
    auto pdf_dict_to_int_default = [&](pdf_obj *obj, const char *key,
                                       int def) -> int {
      pdf_obj *value = pdf_dict_gets(ctx, obj, key);
      return (value && pdf_is_int(ctx, value)) ? pdf_to_int(ctx, value) : def;
    };
    auto pdf_dict_to_name = [&](pdf_obj *obj, const char *key) -> const char * {
      pdf_obj *value = pdf_dict_gets(ctx, obj, key);
      return (value && pdf_is_name(ctx, value)) ? pdf_to_name(ctx, value)
                                                : nullptr;
    };
    auto pdf_dict_to_string = [&](pdf_obj *obj,
                                  const char *key) -> const char * {
      pdf_obj *value = pdf_dict_gets(ctx, obj, key);
      return (value && pdf_is_string(ctx, value))
                 ? pdf_to_text_string(ctx, value)
                 : nullptr;
    };

    for (int i = 0, key; i < len_minus_one;) {
      key = pdf_array_get_int(ctx, nums, i++);
      // The following condition should never become true.
      // However, I have found a PDF (generated with LaTeX beamer), for
      // which this is relevant.
      if (key >= number_of_pages || key < 0) break;
      val = pdf_array_get(ctx, nums, i++);

      if (pdf_is_dict(ctx, val)) {
        raw_labels.insert(key, {
                                   pdf_dict_to_name(val, "S"),
                                   pdf_dict_to_string(val, "P"),
                                   pdf_dict_to_int_default(val, "St", 1),
                               });
      }
    }
  }
  fz_always(ctx) mutex->unlock();
  fz_catch(ctx)
  {
    qWarning() << "Failed to load page labels:" << fz_caught_message(ctx);
    return;
  }

  // Check if anything was found.
  if (raw_labels.empty()) return;

  // Convert raw labels to something useful.
  for (auto it = raw_labels.cbegin(); it != raw_labels.cend(); ++it) {
    // Check if style is empty, which indicates that all following pages
    // will have the same label.
    if (!it->style || *it->style == '\0') {
      // This should be the default case for presentations created with
      // LaTeX beamer.
      pageLabels[it.key()] = it->prefix ? QString::fromLatin1(it->prefix) : "";
    } else {
      // Generate range of page labels.
      int i = it.key();
      const int next_num = (std::next(it) == raw_labels.cend())
                               ? number_of_pages
                               : std::next(it).key();
      for (; i < next_num; i++)
        pageLabels[i] =
            QString::fromStdString(decode_pdf_label(i - it.key(), *it));
    }
  }

  // The first label must always be contained in the pageLabels.
  // This should not be necessary, but without that a broken PDF
  // could easily produce segfaults.
  if (pageLabels.firstKey() != 0) pageLabels[0] = "";

  // Add all pages explicitly to pageLabels, which have an own outline entry.
  QMap<int, QString>::key_iterator it;
  for (const auto &entry : std::as_const(outline)) {
    if (entry.page < 0 || entry.page >= number_of_pages) continue;
    it = std::upper_bound(pageLabels.keyBegin(), pageLabels.keyEnd(),
                          entry.page);
    if (it != pageLabels.keyBegin() && *--it != entry.page)
      pageLabels.insert(entry.page, pageLabels[*it]);
  }
}

#endif  // FZ_VERSION

void MuPdfDocument::prepareRendering(fz_context **context, fz_rect *bbox,
                                     fz_display_list **list,
                                     const int pagenumber,
                                     const qreal resolution) const
{
  // Check if the page number is valid.
  // If it is not, return without changing the given pointers.
  // The caller should note that the pointers are unchaged and handle this
  // appropriately.
  if (!pages.value(pagenumber) || resolution <= 0. || !ctx) return;

  // This is almost completely copied from a mupdf example.

  mutex->lock();
  fz_try(ctx)
  {
    // sender gets a references to context.
    *context = ctx;
    // Get a page (must be done in the main thread!).
    // This causes warnings if the page contains multimedia content.
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 23))
    *bbox = pdf_bound_page(ctx, pages[pagenumber], FZ_MEDIA_BOX);
#else
    *bbox = pdf_bound_page(ctx, pages[pagenumber]);
#endif
    // Calculate the boundary box and rescale it to the given resolution.
    // bbox is now given in points. Convert to pixels using resolution, which
    // is given in pixels per point.
    bbox->x0 *= resolution;
    bbox->x1 *= resolution;
    bbox->y0 *= resolution;
    bbox->y1 *= resolution;

    fz_device *dev = nullptr;
    fz_var(*list);
    fz_var(dev);
    fz_try(ctx)
    {
      // Prepare a display list for a drawing device.
      // The list (and not the page itself) will then be used to render the
      // page.
      *list = fz_new_display_list(ctx, *bbox);
      // Use a fitz device to fill the list with the content of the page.
      dev = fz_new_list_device(ctx, *list);
      // One could use the "pdf_run_page_contents" function here instead to hide
      // annotations. But there exist PDFs in which images are not rendered by
      // that function.
      pdf_run_page(ctx, pages[pagenumber], dev,
                   fz_scale(resolution, resolution), nullptr);
    }
    fz_always(ctx) fz_drop_device(ctx, dev);
    fz_catch(ctx)
    {
      fz_drop_display_list(ctx, *list);
      *list = nullptr;
    }
  }
  fz_always(ctx) mutex->unlock();
  fz_catch(ctx) qWarning() << "Unhandled exception while preparing rendering"
                           << fz_caught_message(ctx);
}

const SlideTransition MuPdfDocument::transition(const int page) const
{
  SlideTransition trans;
  if (!pages.value(page) || !ctx) return trans;

  mutex->lock();
  fz_transition doc_trans = {0, 0., 0, 0, 0, 0, 0};
  float duration = 0.;
  fz_try(ctx) pdf_page_presentation(ctx, pages[page], &doc_trans, &duration);
  fz_catch(ctx)
  {
    mutex->unlock();
    return trans;
  }

  trans.properties = (doc_trans.vertical ? SlideTransition::Vertical : 0) |
                     (doc_trans.outwards ? SlideTransition::Outwards : 0);
  if (doc_trans.type > 12 || doc_trans.type < 0)
    trans.type = SlideTransition::Invalid;
  else
    trans.type = static_cast<SlideTransition::Type>(doc_trans.type);
  trans.angle = doc_trans.direction;
  trans.duration = doc_trans.duration;

  if (trans.type == SlideTransition::Fly) {
    fz_try(ctx)
    {
      pdf_obj *transdict = pdf_dict_get(ctx, pages[page]->obj, PDF_NAME(Trans));
      if (pdf_dict_get_bool(ctx, transdict, PDF_NAME(B)))
        trans.type = SlideTransition::FlyRectangle;
      pdf_obj *ss_obj = pdf_dict_gets(ctx, transdict, "SS");
      if (ss_obj) trans.scale = pdf_to_real(ctx, ss_obj);
    }
    fz_catch(ctx) qWarning() << "failed to completely load slide transition"
                             << fz_caught_message(ctx);
  }

  mutex->unlock();
  return trans;
}

const PdfLink *MuPdfDocument::linkAt(const int page,
                                     const QPointF &position) const
{
  if (!pages.value(page) || !ctx || !doc) return nullptr;

  mutex->lock();
  PdfLink *result = nullptr;
  fz_link *clink = nullptr;
  fz_var(clink);
  fz_var(result);
  fz_try(ctx)
  {
    clink = pdf_load_links(ctx, pages[page]);
    for (fz_link *link = clink; link != nullptr; link = link->next) {
      if (link->uri && link->rect.x0 <= position.x() &&
          link->rect.x1 >= position.x() && link->rect.y0 <= position.y() &&
          link->rect.y1 >= position.y()) {
        const QRectF rect =
            QRectF(link->rect.x0, link->rect.y0, link->rect.x1 - link->rect.x0,
                   link->rect.y1 - link->rect.y0)
                .normalized();
        debug_verbose(DebugRendering, "Link to" << link->uri);
        // Currently MuPDF only provides a simple way to access navigation
        // links and links to URLs. Action links are not handled in MuPDF.
        if (link->uri[0] == '#') {
          // Internal navigation link
          float x, y;
          const int location = pdf_resolve_link(ctx, doc, link->uri, &x, &y);
          result = new GotoLink(rect, location);
          break;
        } else {
          // External link
          const QUrl url = preferences()->resolvePath(link->uri);
          if (url.isValid()) {
            result = new ExternalLink(
                url.isLocalFile() ? PdfLink::LocalUrl : PdfLink::RemoteUrl,
                rect, url);
            break;
          }
        }
      }
    }
  }
  fz_always(ctx)
  {
    fz_drop_link(ctx, clink);
    mutex->unlock();
  }
  fz_catch(ctx) qWarning() << "Error while loading link"
                           << fz_caught_message(ctx);
  return result;
}

QList<std::shared_ptr<MediaAnnotation>> MuPdfDocument::annotations(
    const int page)
{
  QList<std::shared_ptr<MediaAnnotation>> list;
  if (!pages.value(page) || !ctx) return {};
  mutex->lock();
  fz_var(list);
  fz_try(ctx)
  {
    for (pdf_annot *annot = pdf_first_annot(ctx, pages[page]); annot != nullptr;
         annot = pdf_next_annot(ctx, annot)) {
      debug_verbose(DebugMedia,
                    "PDF annotation:" << pdf_annot_type(ctx, annot) << page);
      switch (pdf_annot_type(ctx, annot)) {
        case PDF_ANNOT_SCREEN: {
          // TODO: structure this? This only seems to cover one edge case.
          debug_verbose(DebugMedia, "PDF screen annotation:"
                                        << pdf_annot_type(ctx, annot) << page);
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 19))
          pdf_obj *action_obj =
              pdf_dict_get(ctx, pdf_annot_obj(ctx, annot), PDF_NAME(A));
#else
          pdf_obj *action_obj = pdf_dict_get(ctx, annot->obj, PDF_NAME(A));
#endif
          if (!action_obj ||
              std::string(pdf_to_name(
                  ctx, pdf_dict_get(ctx, action_obj, PDF_NAME(S)))) !=
                  "Rendition")
            break;

          pdf_obj *rend_obj = pdf_dict_get(ctx, action_obj, PDF_NAME(R));
          if (!rend_obj ||
              std::string(pdf_to_name(
                  ctx, pdf_dict_get(ctx, rend_obj, PDF_NAME(S)))) != "MR")
            break;

          pdf_obj *media_criterion_obj =
              pdf_dict_get(ctx, rend_obj, PDF_NAME(C));
          if (!media_criterion_obj ||
              std::string(pdf_to_name(
                  ctx, pdf_dict_get(ctx, media_criterion_obj, PDF_NAME(S)))) !=
                  "MCD")
            break;

          const auto media_type = std::string(pdf_to_text_string(
              ctx, pdf_dict_gets(ctx, media_criterion_obj, "CT")));
          // debug_verbose(DebugMedia, media_type);
          pdf_obj *data_obj =
              pdf_dict_get(ctx, media_criterion_obj, PDF_NAME(D));
          const auto embed =
              std::string(pdf_dict_get_text_string(ctx, data_obj, PDF_NAME(F)));
          // debug_verbose(DebugMedia, embed);
          pdf_obj *stream = pdf_dict_get(
              ctx, pdf_dict_get(ctx, data_obj, PDF_NAME(EF)), PDF_NAME(F));
          if (!stream || !pdf_is_stream(ctx, stream)) break;

          const int obj_id = pdf_obj_parent_num(ctx, stream);
          if (!embedded_media.contains(obj_id)) {
            fz_buffer *buffer = pdf_load_stream(ctx, stream);
            if (!buffer) break;
            unsigned char *data;
            const auto size = fz_buffer_extract(ctx, buffer, &data);
            // TODO: with this definition, embedded_media[obj_id] does not own
            // the data!
            embedded_media[obj_id] = std::make_shared<QByteArray>(
                QByteArray::fromRawData((const char *)data, size));
          }

          MediaAnnotation::Mode mode = MediaAnnotation::Once;
          const fz_rect bound = pdf_bound_annot(ctx, annot);
          const QRectF rect = QRectF(bound.x0, bound.y0, bound.x1 - bound.x0,
                                     bound.y1 - bound.y0);
          list.append(std::shared_ptr<EmbeddedMedia>(
              new EmbeddedMedia(embedded_media[obj_id], rect, mode)));
          break;
        }
        case PDF_ANNOT_MOVIE: {
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 19))
          pdf_obj *media_obj =
              pdf_dict_gets(ctx, pdf_annot_obj(ctx, annot), "Movie");
#else
          pdf_obj *media_obj = pdf_dict_gets(ctx, annot->obj, "Movie");
#endif
          if (!media_obj) {
            qWarning() << "Error while reading movie annotation";
            break;
          }
          const QUrl url = preferences()->resolvePath(
              pdf_dict_get_text_string(ctx, media_obj, PDF_NAME(F)));
          // pdf_drop_obj(ctx, media_obj);
          if (!url.isValid()) {
            debug_msg(DebugMedia, "Invalid URL given:" << url);
            continue;
          }
          const fz_rect bound = pdf_bound_annot(ctx, annot);
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 19))
          pdf_obj *activation_obj =
              pdf_dict_get(ctx, pdf_annot_obj(ctx, annot), PDF_NAME(A));
#else
          pdf_obj *activation_obj = pdf_dict_get(ctx, annot->obj, PDF_NAME(A));
#endif
          MediaAnnotation::Mode mode = MediaAnnotation::Once;
          if (activation_obj) {
            const QString mode_str(
                pdf_to_name(ctx, pdf_dict_gets(ctx, activation_obj, "Mode")));
            if (!mode_str.isEmpty()) {
              if (mode_str == "Open")
                mode = MediaAnnotation::Open;
              else if (mode_str == "Palindrome")
                mode = MediaAnnotation::Palindrome;
              else if (mode_str == "Repeat")
                mode = MediaAnnotation::Repeat;
            }
            // pdf_drop_obj(ctx, activation_obj);
          }
          list.append(std::shared_ptr<MediaAnnotation>(
              new ExternalMedia(url,
                                QRectF(bound.x0, bound.y0, bound.x1 - bound.x0,
                                       bound.y1 - bound.y0),
                                mode)));
          break;
        }
        case PDF_ANNOT_SOUND: {
          // TODO: embedded sounds
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 19))
          pdf_obj *media_obj =
              pdf_dict_gets(ctx, pdf_annot_obj(ctx, annot), "Sound");
#else
          pdf_obj *media_obj = pdf_dict_gets(ctx, annot->obj, "Sound");
#endif
          if (!media_obj) {
            qWarning() << "Error while reading sound annotation";
            break;
          }
          const QUrl url = preferences()->resolvePath(
              pdf_dict_get_text_string(ctx, media_obj, PDF_NAME(F)));
          // pdf_drop_obj(ctx, media_obj);
          if (!url.isValid()) {
            qWarning() << "Failed to load sound object: file not found or "
                          "unsupported embedded sound";
            continue;
          }
          const fz_rect bound = pdf_bound_annot(ctx, annot);
          list.append(std::shared_ptr<MediaAnnotation>(
              new ExternalMedia(url,
                                QRectF(bound.x0, bound.y0, bound.x1 - bound.x0,
                                       bound.y1 - bound.y0),
                                MediaAnnotation::Once)));
          break;
        }
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 18))
        case PDF_ANNOT_RICH_MEDIA:
          // TODO: check what that does
          qWarning() << "Unsupported media type: rich media";
          break;
#endif
        default:
          break;
      }
    }
  }
  fz_always(ctx) mutex->unlock();
  fz_catch(ctx) qWarning() << "Error while searching annotations:"
                           << fz_caught_message(ctx);
  return list;
}

bool MuPdfDocument::flexiblePageSizes() noexcept
{
  if (flexible_page_sizes >= 0 || !ctx || !pages.value(0))
    return flexible_page_sizes;

  flexible_page_sizes = 0;
  mutex->lock();
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 23))
  const fz_rect ref_bbox = pdf_bound_page(ctx, pages[0], FZ_MEDIA_BOX);
#else
  const fz_rect ref_bbox = pdf_bound_page(ctx, pages[0]);
#endif
  fz_rect bbox;
  for (auto page : std::as_const(pages)) {
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 23))
    bbox = pdf_bound_page(ctx, page, FZ_MEDIA_BOX);
#else
    bbox = pdf_bound_page(ctx, page);
#endif
    if (bbox.x1 != ref_bbox.x1 || bbox.y1 != ref_bbox.y1) {
      flexible_page_sizes = 1;
      break;
    }
  }
  mutex->unlock();
  return flexible_page_sizes;
}

void MuPdfDocument::loadLabels()
{
  loadOutline();
  loadPageLabels();
}

void MuPdfDocument::loadOutline()
{
  if (!ctx || !doc) return;

  outline.clear();
  outline.append(PdfOutlineEntry({"", -1, 1}));
  mutex->lock();

  fz_outline *root = nullptr;
  fz_var(root);
  fz_var(outline);
  fz_try(ctx)
  {
    root = pdf_load_outline(ctx, doc);
    if (root) fillOutline(root);
  }
  fz_always(ctx)
  {
    fz_drop_outline(ctx, root);
    mutex->unlock();
  }
  fz_catch(ctx) qWarning() << "Error while loading outline"
                           << fz_caught_message(ctx);

#ifdef QT_DEBUG
  if ((preferences()->debug_level & (DebugRendering | DebugVerbose)) ==
      (DebugRendering | DebugVerbose))
    for (int i = 0; i < outline.length(); i++)
      qDebug() << DebugRendering << i << outline[i].page << outline[i].next
               << outline[i].title;
#endif
}

void MuPdfDocument::fillOutline(fz_outline *entry)
{
  while (entry) {
    const int idx = outline.length();
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 20))
    outline.append(
        PdfOutlineEntry({QString(entry->title), entry->page.page, -1}));
#else
    outline.append(PdfOutlineEntry({QString(entry->title), entry->page, -1}));
#endif
    if (entry->down) fillOutline(entry->down);
    entry = entry->next;
    outline[idx].next = entry ? outline.length() : -outline.length();
  }
};

std::pair<int, QRectF> MuPdfDocument::search(const QString &needle,
                                             int start_page, bool forward) const
{
  std::pair<int, QRectF> result = {-1, QRectF()};
  if (needle.isEmpty() || !doc) return result;
  if (start_page < 0)
    start_page = 0;
  else if (start_page >= number_of_pages)
    start_page = number_of_pages - 1;
  const QByteArray byte_needle = needle.toUtf8();
  const char *raw_needle = byte_needle.data();
  int hit = 0;
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 20))
  int hit_mark = 0;
#endif
  fz_quad rect;
  mutex->lock();
  if (forward)
    for (int page = start_page; page < number_of_pages; ++page) {
      fz_try(ctx)
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 20))
          hit = fz_search_page(ctx, (fz_page *const)(pages[page]), raw_needle,
                               &hit_mark, &rect, 1);
#else
          hit = fz_search_page(ctx, (fz_page *const)(pages[page]), raw_needle,
                               &rect, 1);
#endif
      fz_catch(ctx) hit = 0;
      if (hit) {
        result.first = page;
        result.second =
            QRectF(QPointF(rect.ll.x, rect.ll.y), QPoint(rect.ur.x, rect.ur.y));
        break;
      }
    }
  else
    for (int page = start_page; page >= 0; --page) {
      fz_try(ctx)
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 20))
          hit = fz_search_page(ctx, (fz_page *const)(pages[page]), raw_needle,
                               &hit_mark, &rect, 1);
#else
          hit = fz_search_page(ctx, (fz_page *const)(pages[page]), raw_needle,
                               &rect, 1);
#endif
      fz_catch(ctx) hit = 0;
      if (hit) {
        result.first = page;
        result.second =
            QRectF(QPointF(rect.ll.x, rect.ll.y), QPoint(rect.ur.x, rect.ur.y));
        break;
      }
    }
  mutex->unlock();
  return result;
}

std::pair<int, QList<QRectF>> MuPdfDocument::searchAll(const QString &needle,
                                                       int start_page,
                                                       bool forward) const
{
  std::pair<int, QList<QRectF>> result{-1, {}};
  if (needle.isEmpty() || !doc) return result;
  if (start_page < 0)
    start_page = 0;
  else if (start_page >= number_of_pages)
    start_page = number_of_pages - 1;
  const QByteArray byte_needle = needle.toUtf8();
  const char *raw_needle = byte_needle.data();
  if (forward) {
    for (int page = start_page; page < number_of_pages; ++page)
      if (searchPage(page, raw_needle, result.second)) {
        result.first = page;
        break;
      }
  } else {
    for (int page = start_page; page >= 0; --page)
      if (searchPage(page, raw_needle, result.second)) {
        result.first = page;
        break;
      }
  }
  return result;
}

int MuPdfDocument::searchPage(const int page, const char *raw_needle,
                              QList<QRectF> &target) const
{
  debug_msg(DebugRendering, "Start searching page" << page << raw_needle);
  int count = 0;
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 20))
  int hit_mark[max_search_results];
#endif
  fz_quad rects[max_search_results];
  mutex->lock();
  fz_try(ctx)
#if (FZ_VERSION_MAJOR > 1) || \
    ((FZ_VERSION_MAJOR == 1) && (FZ_VERSION_MINOR >= 20))
      count = fz_search_page(ctx, (fz_page *const)(pages[page]), raw_needle,
                             hit_mark, rects, max_search_results);
#else
      count = fz_search_page(ctx, (fz_page *const)(pages[page]), raw_needle,
                             rects, max_search_results);
#endif
  fz_always(ctx) mutex->unlock();
  fz_catch(ctx) count = 0;
  debug_msg(DebugRendering, "done with search: count =" << count);
  if (count > max_search_results) count = max_search_results;
  for (int i = 0; i < count; ++i)
    target.append(QRectF(QPointF(rects[i].ll.x, rects[i].ll.y),
                         QPoint(rects[i].ur.x, rects[i].ur.y)));
  return count;
}

qreal MuPdfDocument::duration(const int page) const noexcept
{
  if (!pages.value(page) || !ctx) return -1.;
  mutex->lock();
  qreal duration = 0.;
  fz_try(ctx)
  {
    pdf_obj *obj = pdf_dict_get(ctx, pages[page]->obj, PDF_NAME(Dur));
    duration = obj ? pdf_to_real(ctx, obj) : -1.;
  }
  fz_always(ctx) mutex->unlock();
  fz_catch(ctx) duration = -1.;
  return duration;
}
