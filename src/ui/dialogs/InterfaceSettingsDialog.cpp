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
