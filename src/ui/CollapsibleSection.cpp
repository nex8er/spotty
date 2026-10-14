/**
 * \file CollapsibleSection.cpp
 * \brief Реализация spotty::CollapsibleSection.
 */
#include "CollapsibleSection.h"

#include <QFormLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace spotty {

CollapsibleSection::CollapsibleSection(const QString &title, QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_toggle = new QToolButton(this);
    m_toggle->setObjectName(QStringLiteral("collapsibleToggle"));
    m_toggle->setText(title);
    m_toggle->setCheckable(true);
    m_toggle->setChecked(false);
    m_toggle->setAutoRaise(true);
    m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_toggle->setArrowType(Qt::RightArrow);
    layout->addWidget(m_toggle, 0, Qt::AlignLeft);

    m_content = new QWidget(this);
    m_content->setVisible(false);
    m_form = new QFormLayout(m_content);
    m_form->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_content);

    connect(m_toggle, &QToolButton::toggled, this, [this](bool expanded) {
        m_toggle->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_content->setVisible(expanded);
        Q_EMIT toggled(expanded);
    });
}

bool CollapsibleSection::isExpanded() const
{
    return m_toggle->isChecked();
}

void CollapsibleSection::setExpanded(bool expanded)
{
    m_toggle->setChecked(expanded);
}

} // namespace spotty
