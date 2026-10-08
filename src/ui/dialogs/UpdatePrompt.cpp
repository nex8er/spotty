/**
 * \file UpdatePrompt.cpp
 * \brief Реализация spotty::UpdatePrompt.
 */
#include "UpdatePrompt.h"

#include <QMessageBox>
#include <QPushButton>

namespace spotty {

UpdatePrompt::Choice UpdatePrompt::ask(QWidget *parent, const QString &currentVersion,
                                       const ReleaseInfo &release)
{
    QMessageBox box(QMessageBox::Information, tr("Update available"),
                    tr("<b>Spotty %1 is available.</b><br>You have %2.")
                        .arg(release.version.toHtmlEscaped(), currentVersion.toHtmlEscaped()),
                    QMessageBox::NoButton, parent);
    box.setTextFormat(Qt::RichText);

    // Описание релиза в свёрнутой части: оно длинное и нужно не каждому.
    if (!release.notes.isEmpty())
        box.setDetailedText(release.notes);

    QString hint;
    if (!release.assetName.isEmpty())
        hint = tr("Download will start with %1.").arg(release.assetName);
    else
        hint = tr("The release page will open in your browser.");
    box.setInformativeText(hint + QLatin1Char('\n')
                           + tr("You can turn update checks back on in Settings → General."));

    QPushButton *download = box.addButton(tr("Download"), QMessageBox::AcceptRole);
    QPushButton *later = box.addButton(tr("Later"), QMessageBox::RejectRole);
    QPushButton *disable = box.addButton(tr("Don't check for updates"), QMessageBox::DestructiveRole);
    box.setDefaultButton(download);
    box.setEscapeButton(later);

    box.exec();

    if (box.clickedButton() == download)
        return Choice::Download;
    if (box.clickedButton() == disable)
        return Choice::Disable;
    return Choice::Later;
}

} // namespace spotty
