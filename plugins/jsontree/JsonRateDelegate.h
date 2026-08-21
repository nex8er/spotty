/**
 * \file JsonRateDelegate.h
 * \brief Отрисовка строки дерева: полоска частоты, вспышка изменения, приглушение замерших.
 */
#pragma once

#include <QColor>
#include <QStyledItemDelegate>

namespace spotty {

class JsonTreeModel;

/**
 * \brief Частота в виде текста для колонки; пустой прочерк при нуле.
 *
 * Свободной функцией, а не методом: её проверяют тестом без единой отрисовки, тогда как
 * проверять раскладку пикселями бесполезно — она зависит от стиля и таблицы стилей.
 */
QString formatRate(double hertz);

/**
 * \class JsonRateDelegate
 * \brief Рисует значение и частоту, читая их прямо из модели.
 *
 * \par Почему делегат, а не данные в элементах дерева
 *
 * Частота затухает непрерывно, а вспышка гаснет за доли секунды. Держи их в
 * QTreeWidgetItem — и каждый кадр анимации требовал бы записи в дерево по всем строкам,
 * то есть десятков тысяч операций в секунду. Делегат читает число из модели по индексу узла
 * в момент отрисовки, поэтому кадр анимации — это один `viewport()->update()`, и в дерево
 * не пишется ничего.
 *
 * \par Записанные ловушки, которые здесь обходятся
 *
 * Первая: у `QTreeView::item` в теме есть собственное правило, а значит `QStyleSheetStyle`
 * берёт отрисовку фона на себя и `setBackground()` у элемента молча перестаёт действовать —
 * ровно как это было с `QTableView::item` в подсветке поиска. Поэтому фон рисует базовый
 * `paint()`, а вспышка кладётся поверх него.
 *
 * Вторая: `QStyledItemDelegate::paint()` заново вызывает `initStyleOption()`, поэтому всё,
 * снятое с параметров **до** базового вызова, возвращается обратно. Текст снимается в
 * переопределённом initStyleOption(), а не в paint().
 *
 * \par Почему цвета смешиваются, а не задаются прозрачностью
 *
 * Приглушать прозрачностью нельзя: на тёмной теме полупрозрачная заливка сливается с фоном
 * и признак пропадает совсем (та же беда была у выключенного ряда плоттера). Цвета вспышки
 * и полоски смешиваются с фоном заранее и кладутся непрозрачными.
 */
class JsonRateDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    /// \brief Роль, из которой берётся индекс узла в модели.
    static constexpr int kNodeRole = Qt::UserRole + 1;

    /// \brief Сколько горит вспышка изменения по умолчанию, мс.
    static constexpr int kDefaultFlashMs = 400;

    /// \name Пределы длительности вспышки
    /// Короче 60 мс её не успевает заметить глаз, длиннее двух секунд — на быстром потоке
    /// вспышки перекрывают друг друга, и мигает всё подряд без разбора.
    /// @{
    static constexpr int kMinFlashMs = 60;
    static constexpr int kMaxFlashMs = 2000;
    /// @}

    JsonRateDelegate(JsonTreeModel *model, QObject *parent = nullptr);

    /// \brief Пересобрать цвета под тему; QColor хранит уже готовое значение.
    void setColors(const QColor &text, const QColor &muted, const QColor &accent,
                   const QColor &highlight, const QColor &base);

    /// \brief Показывать ли вспышку при изменении значения.
    void setFlashEnabled(bool enabled) { m_flash = enabled; }
    bool flashEnabled() const { return m_flash; }

    /// \brief Сколько горит вспышка; значение зажимается в [kMinFlashMs, kMaxFlashMs].
    void setFlashDurationMs(int milliseconds);
    int flashDurationMs() const { return m_flashMs; }

    /**
     * \brief Отметка времени, на которую рисуется кадр.
     *
     * Ставится видом перед перерисовкой: все строки одного кадра обязаны затухать по одному
     * и тому же «сейчас», иначе соседние строки показывали бы разное время.
     */
    void setFrameTime(qint64 monotonicNs) { m_frameNs = monotonicNs; }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

protected:
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override;

private:
    /// \brief Смешать цвет с фоном; \p weight от 0 (фон) до 1 (сам цвет).
    static QColor blend(const QColor &color, const QColor &background, double weight);

    JsonTreeModel *m_model = nullptr;

    QColor m_text;
    QColor m_muted;
    QColor m_accent;
    QColor m_highlight;
    QColor m_base;

    bool m_flash = true;
    int m_flashMs = kDefaultFlashMs;
    qint64 m_frameNs = 0;
};

} // namespace spotty
