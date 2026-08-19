/**
 * \file PlotProfile.h
 * \brief Именованный набор настроек плоттера и хранилище таких наборов.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace spotty {

/**
 * \struct PlotProfileSeries
 * \brief Настройки одного ряда внутри профиля.
 *
 * \note Цвет — `0xAARRGGBB` числом: QColor объявлен в QtGui, с которым этот слой не
 *       линкуется. Так же поступают TextStyle и HighlightRules.
 */
struct PlotProfileSeries
{
    QString name;
    bool nameIsCustom = false;
    quint32 color = 0;
    bool visible = true;
    bool hasCustomRange = false;
    double customMinimum = 0.0;
    double customMaximum = 1.0;
};

/**
 * \struct PlotProfile
 * \brief Всё, что пользователь настроил под конкретное устройство.
 */
struct SPOTTY_API_EXPORT PlotProfile
{
    QString name;
    QList<PlotProfileSeries> series;

    QString separator = QStringLiteral(",");
    int xAxis = -1;
    int capacity = 50000;
    QString mode = QStringLiteral("timeseries");

    /// \brief Ширина окна развёртки и общий вертикальный масштаб, независимые от данных.
    qint64 horizontalDurationNs = 10'000'000'000LL;
    double verticalZoom = 1.0;
    double verticalOffset = 0.0;

    /**
     * \brief Когда профилем пользовались в последний раз.
     *
     * По ней разрешается ничья при автоподборе: если под поток подходят два профиля,
     * человек почти наверняка хочет тот, с которым работал недавно.
     */
    QDateTime lastUsed;

    /// \brief Имена колонок по порядку — по ним профиль себя и опознаёт.
    QStringList columnNames() const;

    /// \brief Насколько профиль подходит потоку; больше — лучше, ноль — не подходит.
    int matchScore(int columnCount, const QStringList &names) const;
};

/**
 * \class PlotProfileStore
 * \brief Профили файлами в каталоге плагина.
 *
 * \par Почему файл на профиль, а не один общий
 *
 * Набор под конкретное устройство переносится одним файлом: его кладут в репозиторий рядом
 * с прошивкой и отдают коллеге. Тот же выбор сделан у макросов (spotty::MacroStore), и
 * расходиться этим двум незачем.
 */
class SPOTTY_API_EXPORT PlotProfileStore
{
public:
    explicit PlotProfileStore(QString directory);

    /// \brief Имена профилей по алфавиту.
    QStringList profiles() const;

    /// \brief Прочитать профиль; при неудаче возвращает профиль с пустым именем.
    PlotProfile load(const QString &name) const;

    /// \brief Записать профиль под его собственным именем.
    bool save(const PlotProfile &profile) const;

    bool remove(const QString &name) const;

    /**
     * \brief Подобрать профиль под поток.
     * \param columnCount Сколько колонок шлёт устройство.
     * \param names Имена колонок, если устройство их прислало.
     * \return Имя подходящего профиля; пустая строка, если ни один не подходит.
     *
     * Сперва по числу колонок — без совпадения профиль не подойдёт вовсе. Среди
     * подошедших выигрывает тот, у кого больше совпало имён, а при равенстве — тот,
     * которым пользовались позже.
     */
    QString bestMatch(int columnCount, const QStringList &names) const;

    /// \brief Допустимо ли имя как имя файла.
    static bool isValidName(const QString &name);

private:
    QString filePathFor(const QString &name) const;

    QString m_directory;
};

} // namespace spotty
