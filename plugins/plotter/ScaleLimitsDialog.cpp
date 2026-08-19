/**
 * \file ScaleLimitsDialog.cpp
 * \brief Реализация spotty::ScaleLimitsDialog.
 */
#include "ScaleLimitsDialog.h"

#include <spotty/data/PlotFormat.h>

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace spotty {

namespace {

/// \brief Число знаков, с которым пределы попадают в поля ввода.
constexpr int kEditDigits = 9;

} // namespace

ScaleLimitsDialog::ScaleLimitsDialog(const QString &seriesName, double minimum, double maximum,
                                     double measuredMinimum, double measuredMaximum,
                                     bool hasMeasured, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Scale limits — %1").arg(seriesName));

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_minimum = new QLineEdit(PlotFormat::number(minimum, kEditDigits), this);
    m_maximum = new QLineEdit(PlotFormat::number(maximum, kEditDigits), this);
    form->addRow(tr("Minimum"), m_minimum);
    form->addRow(tr("Maximum"), m_maximum);
    layout->addLayout(form);

    if (hasMeasured) {
        // Что намеряно на самом деле — чтобы было видно, от чего отступаешь.
        auto *hint = new QLabel(tr("Measured: %1 … %2")
                                    .arg(PlotFormat::number(measuredMinimum, 6),
                                         PlotFormat::number(measuredMaximum, 6)),
                                this);
        hint->setObjectName(QStringLiteral("hintLabel"));
        layout->addWidget(hint);
    }

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_accept = buttons->button(QDialogButtonBox::Ok);

    if (hasMeasured) {
        QPushButton *measured = buttons->addButton(tr("Use measured"),
                                                   QDialogButtonBox::ResetRole);
        connect(measured, &QPushButton::clicked, this,
                [this, measuredMinimum, measuredMaximum] {
                    m_minimum->setText(PlotFormat::number(measuredMinimum, kEditDigits));
                    m_maximum->setText(PlotFormat::number(measuredMaximum, kEditDigits));
                });
    }

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_minimum, &QLineEdit::textChanged, this, &ScaleLimitsDialog::validate);
    connect(m_maximum, &QLineEdit::textChanged, this, &ScaleLimitsDialog::validate);
    validate();

    m_minimum->setFocus();
    m_minimum->selectAll();
}

void ScaleLimitsDialog::validate()
{
    bool lowOk = false;
    bool highOk = false;
    const double low = m_minimum->text().trimmed().toDouble(&lowOk);
    const double high = m_maximum->text().trimmed().toDouble(&highOk);

    // Совпавшие пределы дали бы нулевую высоту шкалы и деление на ноль в преобразовании
    // координат. Гасим подтверждение, а не молча правим: пользователь видит свои числа и
    // сам решает, какое из них поменять.
    const bool usable = lowOk && highOk && !qFuzzyCompare(low, high);
    if (m_accept)
        m_accept->setEnabled(usable);
}

double ScaleLimitsDialog::minimum() const
{
    return m_minimum->text().trimmed().toDouble();
}

double ScaleLimitsDialog::maximum() const
{
    return m_maximum->text().trimmed().toDouble();
}

} // namespace spotty
