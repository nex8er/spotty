/**
 * \file JsonRateDelegate.cpp
 * \brief Реализация spotty::JsonRateDelegate.
 */
#include "JsonRateDelegate.h"

#include <spotty/data/JsonTreeModel.h>

#include <QApplication>
#include <QPainter>

namespace spotty {

namespace {

/// \brief Колонки дерева; порядок задаётся видом и здесь только читается.
enum Column { ColumnField = 0, ColumnValue, ColumnRate, ColumnCount };

/// \brief Отбивка полоски частоты от краёв ячейки, px.
constexpr int kBarPadding = 3;

/// \brief Доля ячейки, отдаваемая под число частоты; остальное — полоска.
constexpr double kRateTextFraction = 0.45;

/// \brief Насколько сильно вспышка перекрашивает фон в своей высшей точке.
constexpr double kFlashStrength = 0.55;

/// \brief Насколько заметна полоска частоты на фоне.
constexpr double kBarStrength = 0.45;

} // namespace

QString formatRate(double hertz)
{
    // Ноль — это «частоты ещё нет», а не «ноль герц»: поле приходило меньше двух раз.
    // Прочерк отличает это состояние от честно измеренной низкой частоты.
    if (hertz <= 0.0)
        return QStringLiteral("—");
    if (hertz >= 10.0)
        return QString::number(qRound(hertz));
    return QString::number(hertz, 'f', 1);
}

JsonRateDelegate::JsonRateDelegate(JsonTreeModel *model, QObject *parent)
    : QStyledItemDelegate(parent)
    , m_model(model)
{
}

void JsonRateDelegate::setColors(const QColor &text, const QColor &muted, const QColor &accent,
                                 const QColor &highlight, const QColor &base)
{
    m_text = text;
    m_muted = muted;
    m_accent = accent;
    m_highlight = highlight;
    m_base = base;
}

void JsonRateDelegate::setFlashDurationMs(int milliseconds)
{
    m_flashMs = qBound(kMinFlashMs, milliseconds, kMaxFlashMs);
}

QColor JsonRateDelegate::blend(const QColor &color, const QColor &background, double weight)
{
    const double w = qBound(0.0, weight, 1.0);
    return QColor(int(color.red() * w + background.red() * (1.0 - w)),
                  int(color.green() * w + background.green() * (1.0 - w)),
                  int(color.blue() * w + background.blue() * (1.0 - w)));
}

void JsonRateDelegate::initStyleOption(QStyleOptionViewItem *option,
                                       const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    // Текст рисует делегат сам — цветом, зависящим от свежести поля. Снять его после
    // базового paint() нельзя: тот заново зовёт initStyleOption() и вернёт всё обратно.
    option->text.clear();
}

QSize JsonRateDelegate::sizeHint(const QStyleOptionViewItem &option,
                                 const QModelIndex &index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    // Базовый размер посчитан по пустому тексту — он снят в initStyleOption(). Возвращаем
    // высоту строки шрифта, иначе строки схлопнулись бы в полоски.
    size.setHeight(qMax(size.height(), option.fontMetrics.height() + 2 * kBarPadding));
    return size;
}

void JsonRateDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    // Фон, наведение и выделение остаются за таблицей стилей: у QTreeView::item есть своё
    // правило, и рисовать фон самим значило бы спорить со стилем.
    QStyledItemDelegate::paint(painter, option, index);

    if (!m_model)
        return;

    const int node = index.data(kNodeRole).toInt();
    const bool valid = m_model->isValidNode(node);

    painter->save();

    const bool stale = valid && m_model->isStale(node, m_frameNs);
    const QColor textColor = stale ? m_muted : m_text;

    // Вспышка кладётся поверх фона: сразу видно, какие поля живые, а какие замерли, — даже
    // боковым зрением, не читая чисел.
    if (valid && m_flash && !stale) {
        const qint64 age = m_model->nsSinceChange(node, m_frameNs);
        const qint64 flashNs = qint64(m_flashMs) * 1'000'000;
        if (age < flashNs) {
            const double left = 1.0 - double(age) / double(flashNs);
            painter->fillRect(option.rect,
                              blend(m_highlight, m_base, left * kFlashStrength));
        }
    }

    QRect textRect = option.rect.adjusted(kBarPadding, 0, -kBarPadding, 0);

    if (index.column() == ColumnRate && valid) {
        const double rate = m_model->rate(node, m_frameNs);
        const double top = m_model->maxRate(m_frameNs);

        const int textWidth = int(option.rect.width() * kRateTextFraction);
        const QRect barArea = option.rect.adjusted(kBarPadding, kBarPadding,
                                                   -textWidth - kBarPadding, -kBarPadding);
        // Полоска отвечает на «какое поле быстрее», число — на «насколько именно». Одно
        // без другого не работает: полоска не даёт точности, число не даёт сравнения.
        if (top > 0.0 && rate > 0.0 && barArea.width() > 0) {
            QRect bar = barArea;
            bar.setWidth(qMax(1, int(barArea.width() * qBound(0.0, rate / top, 1.0))));
            painter->fillRect(bar, blend(m_accent, m_base, kBarStrength));
        }

        painter->setPen(textColor);
        painter->drawText(option.rect.adjusted(0, 0, -kBarPadding, 0),
                          Qt::AlignRight | Qt::AlignVCenter, formatRate(rate));
        painter->restore();
        return;
    }

    QString text = index.data(Qt::DisplayRole).toString();
    if (!text.isEmpty()) {
        painter->setPen(textColor);
        text = option.fontMetrics.elidedText(text, Qt::ElideRight, textRect.width());
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    }

    painter->restore();
}

} // namespace spotty
