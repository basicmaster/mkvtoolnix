#include "common/common_pch.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSaveFile>

#include <matroska/KaxAttached.h>

#include "common/ebml.h"
#include "common/extern_data.h"
#include "common/qt.h"
#include "common/strings/formatting.h"
#include "mkvtoolnix-gui/forms/header_editor/attached_file_page.h"
#include "mkvtoolnix-gui/forms/header_editor/tab.h"
#include "mkvtoolnix-gui/header_editor/attached_file_page.h"
#include "mkvtoolnix-gui/header_editor/tab.h"
#include "mkvtoolnix-gui/util/file_dialog.h"
#include "mkvtoolnix-gui/util/message_box.h"
#include "mkvtoolnix-gui/util/settings.h"
#include "mkvtoolnix-gui/util/widget.h"

namespace mtx { namespace gui { namespace HeaderEditor {

using namespace mtx::gui;

AttachedFilePage::AttachedFilePage(Tab &parent,
                                   PageBase &topLevelPage,
                                   KaxAttachedPtr const &attachment)
  : PageBase{parent, "todo"}
  , ui{new Ui::AttachedFilePage}
  , m_filesDDHandler{Util::FilesDragDropHandler::Mode::Remember}
  , m_topLevelPage(topLevelPage)
  , m_attachment{attachment}
{
  ui->setupUi(this);

  connect(ui->reset, &QPushButton::clicked, this, &AttachedFilePage::setControlsFromAttachment);
}

AttachedFilePage::~AttachedFilePage() {
}

void
AttachedFilePage::retranslateUi() {
  ui->retranslateUi(this);

  ui->size->setText(formatSize());

  Util::setToolTip(ui->name,        Q("%1 %2").arg(QY("Other parts of the file (e.g. a subtitle track) may refer to this attachment via this name.")).arg(QY("The name must not be left empty.")));
  Util::setToolTip(ui->description, Q("%1 %2").arg(QY("An arbitrary description meant for the user.")).arg(QY("The description can be left empty.")));
  Util::setToolTip(ui->mimeType,    Q("%1 %2").arg(QY("The MIME type determines which program can be used for handling its content.")).arg(QY("The MIME type must not be left empty.")));
  Util::setToolTip(ui->uid,         Q("%1 %2").arg(QY("A unique, positive number unambiguously identifying the attachment within the Matroska file.")).arg(QY("The UID must not be left empty.")));
  Util::setToolTip(ui->reset,       QY("Reset the attachment values on this page to how they're saved in the file."));
}

void
AttachedFilePage::init() {
  for (auto &mimeType : mime_types)
    ui->mimeType->addItem(Q(mimeType.name), Q(mimeType.name));

  retranslateUi();

  setControlsFromAttachment();

  m_parent.appendPage(this, m_topLevelPage.m_pageIdx);

  m_topLevelPage.m_children << this;
}

void
AttachedFilePage::setControlsFromAttachment() {
  auto mimeType = Q(FindChildValue<KaxMimeType>(*m_attachment));

  ui->name->setText(Q(FindChildValue<KaxFileName>(*m_attachment)));
  ui->description->setText(Q(FindChildValue<KaxFileDescription>(*m_attachment)));
  ui->mimeType->setEditText(mimeType);
  ui->uid->setText(QString::number(FindChildValue<KaxFileUID>(*m_attachment)));

  Util::setComboBoxTextByData(ui->mimeType, mimeType);

  m_newFileContent.reset();

  ui->size->setText(formatSize());
}

void
AttachedFilePage::dragEnterEvent(QDragEnterEvent *event) {
  m_filesDDHandler.handle(event, false);
}

void
AttachedFilePage::dropEvent(QDropEvent *event) {
  if (m_filesDDHandler.handle(event, true))
    emit filesDropped(m_filesDDHandler.fileNames());
}

QString
AttachedFilePage::title()
  const {
  return Q(FindChildValue<KaxFileName>(*m_attachment, to_wide(Y("<unnamed>"))));
}

void
AttachedFilePage::setItems(QList<QStandardItem *> const &items)
  const {
  PageBase::setItems(items);

  items.at(1)->setText(Q(FindChildValue<KaxMimeType>(*m_attachment)));
  items.at(3)->setText(Q(FindChildValue<KaxFileDescription>(*m_attachment)));
  items.at(4)->setText(QString::number(FindChildValue<KaxFileUID>(*m_attachment)));
  items.at(7)->setText(formatSize());
}

QString
AttachedFilePage::formatSize()
  const {
  if (m_newFileContent)
    return QNY("%1 byte (%2)", "%1 bytes (%2)", m_newFileContent->get_size()).arg(m_newFileContent->get_size()).arg(Q(format_file_size(m_newFileContent->get_size())));

  auto content = FindChild<KaxFileData>(*m_attachment);
  if (content)
    return QNY("%1 byte (%2)", "%1 bytes (%2)", content->GetSize()).arg(content->GetSize()).arg(Q(format_file_size(content->GetSize())));

  return {};
}

bool
AttachedFilePage::hasThisBeenModified()
  const {
  return m_newFileContent
    || (Q(FindChildValue<KaxFileName>(*m_attachment))              != ui->name->text())
    || (Q(FindChildValue<KaxFileDescription>(*m_attachment))       != ui->description->text())
    || (Q(FindChildValue<KaxMimeType>(*m_attachment))              != ui->mimeType->currentText())
    || (QString::number(FindChildValue<KaxFileUID>(*m_attachment)) != ui->uid->text());
}

void
AttachedFilePage::modifyThis() {
  auto description = ui->description->text();

  GetChild<KaxFileName>(*m_attachment).SetValueUTF8(to_utf8(ui->name->text()));
  GetChild<KaxMimeType>(*m_attachment).SetValue(to_utf8(ui->mimeType->currentText()));
  GetChild<KaxFileUID>(*m_attachment).SetValue(ui->uid->text().toULongLong());

  if (description.isEmpty())
    DeleteChildren<KaxFileDescription>(*m_attachment);
  else
    GetChild<KaxFileDescription>(*m_attachment).SetValueUTF8(to_utf8(description));

  if (m_newFileContent)
    GetChild<KaxFileData>(*m_attachment).CopyBuffer(m_newFileContent->get_buffer(), m_newFileContent->get_size());
}

bool
AttachedFilePage::validateThis()
  const {
  auto ok = false;

  ui->uid->text().toULongLong(&ok);

  ok = ok
    && !ui->name->text().isEmpty()
    && !ui->mimeType->currentText().isEmpty();

  return ok;
}

void
AttachedFilePage::saveContent() {
  auto content = FindChild<KaxFileData>(*m_attachment);

  if (!m_newFileContent && !content)
    return;

  auto &settings = Util::Settings::get();
  auto fileName  = Util::getSaveFileName(this, QY("Save attachment"), Q("%1/%2").arg(Util::dirPath(settings.m_lastOutputDir.path())).arg(ui->name->text()), QY("All files") + Q(" (*)"));

  if (fileName.isEmpty())
    return;

  settings.m_lastOutputDir = QFileInfo{ fileName }.absoluteDir();
  settings.save();

  QSaveFile file{fileName};
  auto ok = true;

  if (file.open(QIODevice::WriteOnly)) {
    if (m_newFileContent)
      file.write(reinterpret_cast<char *>(m_newFileContent->get_buffer()), m_newFileContent->get_size());
    else
      file.write(reinterpret_cast<char *>(content->GetBuffer()), content->GetSize());
    ok = file.commit();

  } else
    ok = false;

  if (!ok)
    Util::MessageBox::critical(this)->title(QY("Saving failed")).text(QY("Creating the file failed. Check to make sure you have permission to write to that directory and that the drive is not full.")).exec();
}

void
AttachedFilePage::replaceContent(bool deriveNameAndMimeType) {
  auto &settings = Util::Settings::get();
  auto fileName  = Util::getOpenFileName(this, QY("Replace attachment"), Util::dirPath(settings.m_lastOpenDir.path()), QY("All files") + Q(" (*)"));

  if (fileName.isEmpty())
    return;

  auto fileInfo          = QFileInfo{ fileName };
  settings.m_lastOpenDir = fileInfo.absoluteDir();
  settings.save();

  auto newContent = Tab::readFileData(this, fileName);
  if (!newContent)
    return;

  m_newFileContent = newContent;

  ui->size->setText(formatSize());

  if (!deriveNameAndMimeType)
    return;

  auto mimeType = Q(guess_mime_type(to_utf8(fileName), true));

  ui->name->setText(fileInfo.fileName());
  ui->mimeType->setEditText(mimeType);
  Util::setComboBoxTextByData(ui->mimeType, mimeType);
}

}}}
