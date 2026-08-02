/**
 * \file MacroEditDialog.cpp
 * \brief Реализация spotty::MacroEditDialog.
 */
#include "MacroEditDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr DataCodec::Format kFormats[] = {
    DataCodec::Format::Text,
    DataCodec::Format::Hex,
    DataCodec::Format::Base64,
};

constexpr DataCodec::Termination kTerminations[] = {
    DataCodec::Termination::None,
    DataCodec::Termination::Lf,
    DataCodec::Termination::Cr,
    DataCodec::Termination::CrLf,
    DataCodec::Termination::Nul,
};

} // namespace

MacroEditDialog::MacroEditDialog(const Macro &macro, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(macro.name.isEmpty() ? tr("New macro") : tr("Edit macro"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_name = new QLineEdit(macro.name, this);
    m_name->setPlaceholderText(tr("Shown on the macro list"));
    form->addRow(tr("Name"), m_name);

    m_payload = new QPlainTextEdit(macro.payload, this);
    // Моноширинный шрифт: шестнадцатеричный дамп в пропорциональном шрифте не читается.
    m_payload->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_payload->setMinimumHeight(90);
    form->addRow(tr("Data"), m_payload);

    m_format = new QComboBox(this);
    for (const DataCodec::Format format : kFormats)
        m_format->addItem(DataCodec::formatName(format), int(format));
    m_format->setCurrentIndex(m_format->findData(int(macro.format)));
    form->addRow(tr("Format"), m_format);

    m_termination = new QComboBox(this);
    for (const DataCodec::Termination termination : kTerminations)
        m_termination->addItem(DataCodec::terminationName(termination), int(termination));
    m_termination->setCurrentIndex(m_termination->findData(int(macro.termination)));
    form->addRow(tr("Termination"), m_termination);

    m_shortcut = new QKeySequenceEdit(QKeySequence(macro.shortcut), this);
    form->addRow(tr("Shortcut"), m_shortcut);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("errorLabel"));
    m_error->setWordWrap(true);
    m_error->hide();

    layout->addLayout(form);
    layout->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // Проверка по мере ввода, а не при нажатии «ОК»: ошибку в шестнадцатеричной записи
    // лучше показать сразу, пока видно, что именно набрано.
    connect(m_payload, &QPlainTextEdit::textChanged, this, &MacroEditDialog::validate);
    connect(m_format, &QComboBox::currentIndexChanged, this, &MacroEditDialog::validate);
    validate();
}

void MacroEditDialog::validate()
{
    QString error;
    DataCodec::encode(m_payload->toPlainText(),
                      DataCodec::Format(m_format->currentData().toInt()),
                      DataCodec::Termination::None, &error);

    m_error->setText(error);
    m_error->setVisible(!error.isEmpty());
}

Macro MacroEditDialog::macro() const
{
    Macro result;
    result.name = m_name->text().trimmed();
    result.payload = m_payload->toPlainText();
    result.format = DataCodec::Format(m_format->currentData().toInt());
    result.termination = DataCodec::Termination(m_termination->currentData().toInt());
    result.shortcut = m_shortcut->keySequence().toString(QKeySequence::PortableText);
    return result;
}

} // namespace spotty
