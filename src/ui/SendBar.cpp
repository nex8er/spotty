/**
 * \file SendBar.cpp
 * \brief Реализация spotty::SendBar.
 */
#include "SendBar.h"

#include "theme/MdiIcons.h"
#include "theme/ThemeManager.h"

#include <HistoryStore.h>

#include <QAction>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QMenu>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolTip>

#include <memory>

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
    // Кнопки очистки здесь нет намеренно. Строка отправки опустошается сама после
    // каждой отправки, а до неё содержимое стирается тем же движением, каким набирается,
    // — Ctrl+A и любая клавиша. Крестик занимал место у правого края поля, куда уезжает
    // курсор при длинной команде, и убирал ровно то, что человек только что набрал.
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

    // Оба списка ужимаются до самого длинного своего пункта плюс запас под стрелку.
    // По умолчанию QComboBox берёт ширину с большим запасом «на вырост», и в полосе
    // отправки два таких списка отъедали место у поля ввода — того единственного, чего
    // здесь всегда мало. Считаем по метрикам шрифта, а не числом: при переводе «CR+LF»
    // становится «ВК+ПС», а кегль задаёт система.
    const auto shrinkToContents = [](QComboBox *combo) {
        const QFontMetrics metrics(combo->font());
        int widest = 0;
        for (int i = 0; i < combo->count(); ++i)
            widest = qMax(widest, metrics.horizontalAdvance(combo->itemText(i)));

        // Запас под стрелку раскрытия, отступы поля и рамку. Стрелка рисуется размером
        // со строку, поэтому считается от высоты, а не задаётся числом.
        const int arrow = combo->sizeHint().height();
        combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        combo->setMinimumWidth(widest + arrow + 2 * ThemeManager::metrics().padH / 2);
        combo->setMaximumWidth(widest + arrow + 2 * ThemeManager::metrics().padH);
    };
    shrinkToContents(m_format);
    shrinkToContents(m_termination);

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

void SendBar::resetCompletion()
{
    m_completionStem.clear();
    m_completionMatches.clear();
    m_completionIndex = -1;
}

void SendBar::completeFromHistory(bool backwards)
{
    if (!m_history)
        return;

    // Первый Tab запоминает набранное начало и собирает варианты. Повторные только
    // двигают указатель: искать заново по уже подставленному тексту значило бы сузить
    // список до одной записи и остановить перебор на первом же шаге.
    if (m_completionIndex < 0) {
        m_completionStem = m_input->text();
        if (m_completionStem.isEmpty())
            return;

        // HistoryStore::complete() отдаёт совпадения от новых к старым — тот же порядок,
        // в котором их перебирает bash по Ctrl+R: свежая команда вероятнее нужной.
        m_history->complete(m_completionStem, &m_completionMatches);
        if (m_completionMatches.isEmpty())
            return;

        m_completionIndex = 0;
    } else {
        const int count = int(m_completionMatches.size());
        // По кругу: дойдя до конца, перебор возвращается к самому свежему варианту.
        // Упереться в край и замолчать хуже — человек не знает, кончились варианты или
        // клавиша перестала работать.
        m_completionIndex = (m_completionIndex + (backwards ? -1 : 1) + count) % count;
    }

    const QString candidate = m_completionMatches.at(m_completionIndex);

    // Подставленное выделяется от набранного до конца: следующее нажатие любой клавиши
    // заменит подсказку, а Enter отправит её целиком. Так же ведёт себя дополнение в
    // командных оболочках.
    const QSignalBlocker blocker(m_input);
    m_input->setText(candidate);
    m_input->setSelection(int(m_completionStem.size()),
                          int(candidate.size() - m_completionStem.size()));

    if (m_completionMatches.size() > 1) {
        QToolTip::showText(m_input->mapToGlobal(QPoint(0, -m_input->height())),
                           tr("%1 of %2 — Tab for next")
                               .arg(m_completionIndex + 1)
                               .arg(m_completionMatches.size()),
                           m_input);
    }
}

void SendBar::showInputContextMenu(const QPoint &globalPosition)
{
    // Своё меню строится поверх штатного меню поля ввода, а не вместо него: вырезать
    // «Копировать» и «Вставить» ради одного своего пункта было бы обменом полезного на
    // удобное.
    const auto menu = std::unique_ptr<QMenu>(m_input->createStandardContextMenu());
    menu->setObjectName(QStringLiteral("sendInputMenu"));

    menu->addSeparator();
    QAction *makeMacro = menu->addAction(tr("Save as macro"));
    makeMacro->setEnabled(!m_input->text().isEmpty());
    connect(makeMacro, &QAction::triggered, this, [this] {
        Q_EMIT makeMacroRequested(m_input->text(), int(format()), int(termination()));
    });

    menu->exec(globalPosition);
}

bool SendBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_input)
        return QWidget::eventFilter(watched, event);

    if (event->type() == QEvent::ContextMenu) {
        auto *contextEvent = static_cast<QContextMenuEvent *>(event);
        showInputContextMenu(contextEvent->globalPos());
        return true;
    }

    if (event->type() != QEvent::KeyPress)
        return QWidget::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);

    switch (keyEvent->key()) {
    case Qt::Key_Tab:
        completeFromHistory(/*backwards=*/false);
        return true;
    case Qt::Key_Backtab:
        // Shift+Tab приходит отдельной клавишей, а не Tab с модификатором.
        completeFromHistory(/*backwards=*/true);
        return true;
    case Qt::Key_Up:
        resetCompletion();
        stepHistory(-1);
        return true;
    case Qt::Key_Down:
        resetCompletion();
        stepHistory(+1);
        return true;
    default:
        break;
    }

    // Любое редактирование выводит и из перебора истории, и из перебора дополнений:
    // иначе следующая стрелка вверх прыгнула бы от только что набранного текста к записи,
    // к которой он не относится, а следующий Tab продолжал бы искать по устаревшему
    // началу.
    if (!keyEvent->text().isEmpty()) {
        resetCompletion();
        if (m_history)
            m_historyIndex = int(m_history->entries().size());
    }

    return QWidget::eventFilter(watched, event);
}

} // namespace spotty
