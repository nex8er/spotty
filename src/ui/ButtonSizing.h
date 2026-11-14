/**
 * \file ButtonSizing.h
 * \brief Минимальный размер кнопки с учётом текста и отступов действующего стиля.
 */
#pragma once

#include <QPushButton>
#include <QStyle>
#include <QStyleOptionButton>

namespace spotty {

/// \brief Не дать раскладке обрезать перевод внутри оформленной таблицей стилей кнопки.
inline void ensureButtonTextFits(QPushButton *button)
{
    button->ensurePolished();
    const QSize hint = button->sizeHint();
    QStyleOptionButton option;
    option.initFrom(button);
    option.rect = QRect(QPoint(), hint);
    const QRect contents = button->style()->subElementRect(
        QStyle::SE_PushButtonContents, &option, button);
    QSize text = button->fontMetrics().size(Qt::TextShowMnemonic, button->text());
    if (!button->icon().isNull())
        text.rwidth() += button->iconSize().width()
                        + button->style()->pixelMetric(QStyle::PM_ButtonMargin, &option, button);
    button->setMinimumSize(hint + QSize(qMax(0, text.width() - contents.width()),
                                       qMax(0, text.height() - contents.height())));
}

} // namespace spotty
