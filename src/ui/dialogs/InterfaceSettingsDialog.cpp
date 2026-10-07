/**
 * \file InterfaceSettingsDialog.cpp
 * \brief Реализация spotty::InterfaceSettingsDialog.
 */
#include "InterfaceSettingsDialog.h"

#include "InterfaceSettingsPanel.h"

#include <QDialogButtonBox>
#include <QVBoxLayout>

namespace spotty {

InterfaceSettingsDialog::InterfaceSettingsDialog(InterfaceRegistry *registry,
                                                 PluginManager *plugins,
                                                 const QString &initialId, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Interface settings"));

    // Размер по умолчанию задан явно: у области прокрутки подсказка размера мала, и без него
    // диалог открывался узким, а подсказки под полями переносились на много строк.
    resize(640, 680);

    auto *layout = new QVBoxLayout(this);

    m_panel = new InterfaceSettingsPanel(registry, plugins, this);
    m_panel->selectInterface(initialId);
    layout->addWidget(m_panel);

    // Кнопка ровно одна: применяются правки немедленно самой панелью, отменять нечего.
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace spotty
