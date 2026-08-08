/**
 * \file MacroStore.h
 * \brief Пресеты макросов, по файлу на пресет.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>
#include <spotty/data/DataCodec.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace spotty {

/**
 * \struct Macro
 * \brief Одна заготовленная посылка.
 *
 * \note Отдельного имени у макроса нет: им служит сама команда. Имя пришлось бы
 *       придумывать при каждом добавлении, а для «AT+RST» любое имя было бы хуже самой
 *       команды.
 */
struct Macro
{
    QString payload;  ///< Текст в том виде, в каком его набрал бы человек. Он же подпись.

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
class SPOTTY_API_EXPORT MacroStore
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

    /**
     * \brief Дописать макросы из файла в текущий набор.
     * \param filePath Файл в том же формате, что и пресет.
     * \param added Куда записать число добавленных макросов. Может быть `nullptr`.
     * \return `false`, если файл не читается или не разбирается.
     *
     * Именно дописать, а не заменить: импорт нужен, чтобы принести чужие команды к своим,
     * и затирание набора было бы неприятной неожиданностью.
     *
     * Горячие клавиши импортированных макросов сохраняются, если свободны, и снимаются,
     * если уже заняты, — иначе одно сочетание отвечало бы за две разные команды.
     */
    bool importFrom(const QString &filePath, int *added = nullptr);

    /**
     * \brief Выгрузить макросы в отдельный файл.
     * \param filePath Куда писать.
     * \param rows Номера строк для выгрузки; пустой список — выгрузить все.
     *
     * Выборочная выгрузка нужна затем же, зачем и файл на пресет: поделиться половиной
     * набора, не вычищая из него остальное руками.
     */
    bool exportTo(const QString &filePath, const QList<int> &rows = {}) const;

    /**
     * \brief Свободное сочетание клавиш для нового макроса.
     * \return `"F1"`…`"F12"` — первое незанятое в наборе, либо пустая строка.
     *
     * Пустая строка, когда заняты все двенадцать: назначать тринадцатому макросу
     * что-то вроде «Ctrl+Shift+F1» программа не вправе — такое сочетание пользователь
     * должен выбрать сам.
     */
    QString suggestShortcut() const;

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
