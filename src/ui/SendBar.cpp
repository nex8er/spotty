/**
 * \file SendBar.cpp
 * \brief Реализация spotty::SendBar.
 */
#include "SendBar.h"

#include "theme/MdiIcons.h"

#include <HistoryStore.h>

#include <QComboBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolTip>

namespace spotty {

namespace {

/// \brief Форматы ввода в порядке показа.
constexpr DataCodec::Format kFormats[] = {
    DataCodec::Format::Text,
    DataCodec::Format::Hex,
    DataCodec::Format::Base64,
};

/// \brief Терминации в порядке показа.
constexpr DataCodec::Termination kTerminations[] = {
    DataCodec::Termination::None,
    DataCodec::Termination::Lf,
    DataCodec::Termination::Cr,
    DataCodec::Termination::CrLf,
    DataCodec::Termination::Nul,
};

} // namespace

SendBar::SendBar(HistoryStore *history, QWidget *parent)
    : QWidget(parent)
    , m_history(history)
{
    setObjectName(QStringLiteral("sendBar"));

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Data to send"));
    m_input->setClearButtonEnabled(true);
    // Фильтр нужен потому, что Tab и стрелки до keyPressEvent поля не доходят: Tab
    // перехватывает механизм перехода по фокусу, а стрелки обрабатывает сам QLineEdit.
    m_input->installEventFilter(this);

    m_format = new QComboBox(this);
    for (const DataCodec::Format format : kFormats)
        m_format->addItem(DataCodec::formatName(format), int(format));
    m_format->setToolTip(tr("How the entered text is interpreted"));

    m_termination = new QComboBox(this);
    for (const DataCodec::Termination termination : kTerminations)
        m_termination->addItem(DataCodec::terminationName(termination), int(termination));
    m_termination->setCurrentIndex(3); // CR+LF — самая частая для устройств.
    m_termination->setToolTip(tr("Appended to every message"));

    m_send = new QPushButton(tr("Send"), this);
    m_send->setDefault(true);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("errorLabel"));
    m_error->hide();

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);
    layout->addWidget(m_input, 1);
    layout->addWidget(m_error);
    layout->addWidget(m_format);
    layout->addWidget(m_termination);
    layout->addWidget(m_send);

    connect(m_send, &QPushButton::clicked, this, &SendBar::submit);
    connect(m_input, &QLineEdit::returnPressed, this, &SendBar::submit);
    connect(m_format, &QComboBox::currentIndexChanged, this, [this] {
        m_error->hide();
        Q_EMIT optionsChanged();
    });
    connect(m_termination, &QComboBox::currentIndexChanged, this, &SendBar::optionsChanged);

    if (m_history)
        m_historyIndex = int(m_history->entries().size());

    setSendEnabled(false);
}

DataCodec::Format SendBar::format() const
{
    return DataCodec::Format(m_format->currentData().toInt());
}

void SendBar::setFormat(DataCodec::Format format)
{
    const int index = m_format->findData(int(format));
    if (index >= 0)
        m_format->setCurrentIndex(index);
}

DataCodec::Termination SendBar::termination() const
{
    return DataCodec::Termination(m_termination->currentData().toInt());
}

void SendBar::setTermination(DataCodec::Termination termination)
{
    const int index = m_termination->findData(int(termination));
    if (index >= 0)
        m_termination->setCurrentIndex(index);
}

void SendBar::setSendEnabled(bool enabled)
{
    m_send->setEnabled(enabled);
    m_input->setEnabled(enabled);
    m_input->setPlaceholderText(enabled ? tr("Data to send")
                                        : tr("Open an interface to send data"));
}

void SendBar::focusInput()
{
    m_input->setFocus();
}

void SendBar::setText(const QString &text)
{
    m_input->setText(text);
    m_input->end(false);
    showError({});
}

void SendBar::showError(const QString &message)
{
    if (message.isEmpty()) {
        m_error->hide();
        return;
    }
    m_error->setText(message);
    m_error->show();
}

void SendBar::submit()
{
    const QString text = m_input->text();

    QString error;
    const QByteArray data = DataCodec::encode(text, format(), termination(), &error);

    if (!error.isEmpty()) {
        // Ошибку показываем рядом с полем и ввод не трогаем: пользователь должен видеть,
        // что именно набрал неправильно, и поправить это, а не набирать заново.
        showError(error);
        return;
    }

    if (data.isEmpty())
        return;

    showError({});
    Q_EMIT sendRequested(data);

    if (m_history && !text.isEmpty()) {
        m_history->append(text);
        m_history->save();
        m_historyIndex = int(m_history->entries().size());
    }

    m_draft.clear();
    m_input->clear();
}

void SendBar::stepHistory(int direction)
{
    if (!m_history || m_history->entries().isEmpty())
        return;

    const QStringList &entries = m_history->entries();

    // Вход в историю запоминает набранное: вернуть его при выходе вниз важнее, чем
    // сэкономить поле.
    if (m_historyIndex == entries.size() && direction < 0)
        m_draft = m_input->text();

    const int next = qBound(0, m_historyIndex + direction, int(entries.size()));
    if (next == m_historyIndex)
        return;

    m_historyIndex = next;
    m_input->setText(m_historyIndex == entries.size() ? m_draft : entries.at(m_historyIndex));
    m_input->end(false);
}

void SendBar::completeFromHistory()
{
    if (!m_history)
        return;

    const QString prefix = m_input->text();
    QStringList matches;
    const QString completed = m_history->complete(prefix, &matches);

    if (matches.isEmpty())
        return;

    if (completed != prefix) {
        m_input->setText(completed);
        m_input->end(false);
    }

    // Если общий префикс не удлинился, показываем варианты: иначе Tab выглядел бы
    // сломанным, хотя подходящих записей несколько.
    if (matches.size() > 1) {
        const QStringList shown = matches.mid(0, 8);
        QToolTip::showText(m_input->mapToGlobal(QPoint(0, -m_input->height())),
                           shown.join(u'\n'), m_input);
    }
}

bool SendBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_input || event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);

    switch (keyEvent->key()) {
    case Qt::Key_Tab:
        completeFromHistory();
        return true;
    case Qt::Key_Up:
        stepHistory(-1);
        return true;
    case Qt::Key_Down:
        stepHistory(+1);
        return true;
    default:
        break;
    }

    // Любое редактирование выводит из перебора истории: иначе следующая стрелка вверх
    // прыгнула бы от только что набранного текста к записи, к которой он не относится.
    if (!keyEvent->text().isEmpty() && m_history)
        m_historyIndex = int(m_history->entries().size());

    return QWidget::eventFilter(watched, event);
}

} // namespace spotty
