/**
 * \file MacroStore.h
 * \brief Пресеты макросов, по файлу на пресет.
 */
#pragma once

#include "terminal/DataCodec.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace spotty {

/**
 * \struct Macro
 * \brief Одна заготовленная посылка.
 */
struct Macro
{
    QString name;     ///< Подпись на кнопке.
    QString payload;  ///< Текст в том виде, в каком его набрал бы человек.

    /// \brief Как трактовать #payload. У каждого макроса свой формат.
    DataCodec::Format format = DataCodec::Format::Text;

    /// \brief Что дописать в конец. Тоже у каждого своё.
    DataCodec::Termination termination = DataCodec::Termination::CrLf;

    /**
     * \brief Горячая клавиша в переносимой записи QKeySequence, например `"Ctrl+1"`.
     *
     * Пустая строка — без горячей клавиши.
     */
    QString shortcut;

    /// \brief Закодированные байты, готовые к отправке.
    QByteArray encode(QString *error = nullptr) const
    {
        return DataCodec::encode(payload, format, termination, error);
    }
};

/**
 * \class MacroStore
 * \brief Наборы макросов, хранимые по файлу на пресет.
 *
 * \par Почему файл на пресет
 *
 * Так набор для конкретного устройства переносится и передаётся одним файлом, кладётся
 * в репозиторий рядом с прошивкой и не смешивается с чужими. Один общий файл со всеми
 * наборами пришлось бы разбирать вручную, чтобы поделиться половиной.
 *
 * Файлы лежат в spotty::Paths::macrosDir(), имя файла — имя пресета.
 */
class MacroStore
{
public:
    /**
     * \brief Конструктор.
     * \param directory Каталог с файлами пресетов.
     */
    explicit MacroStore(QString directory);

    /// \brief Имена доступных пресетов, отсортированные по алфавиту.
    QStringList presets() const;

    /// \brief Имя загруженного пресета.
    QString currentPreset() const { return m_currentPreset; }

    /**
     * \brief Загрузить пресет.
     * \param name Имя пресета без расширения.
     * \return `false`, если файл не читается; в этом случае список макросов пуст.
     *
     * Отсутствующий пресет с именем по умолчанию не ошибка: он создаётся пустым при
     * первом сохранении.
     */
    bool loadPreset(const QString &name);

    /// \brief Записать текущий пресет на диск.
    bool save();

    /**
     * \brief Создать пустой пресет и сделать его текущим.
     * \return `false`, если имя занято или непригодно для имени файла.
     */
    bool createPreset(const QString &name);

    /// \brief Удалить пресет вместе с файлом.
    bool deletePreset(const QString &name);

    /// \brief Макросы текущего пресета.
    const QList<Macro> &macros() const { return m_macros; }

    /// \brief Изменяемый список макросов. После правки нужен save().
    QList<Macro> &macros() { return m_macros; }

    /// \brief Имя пресета, создаваемого при первом запуске.
    static QString defaultPresetName();

private:
    /// \brief Полный путь к файлу пресета.
    QString filePathFor(const QString &name) const;

    QString m_directory;
    QString m_currentPreset;
    QList<Macro> m_macros;
};

} // namespace spotty
