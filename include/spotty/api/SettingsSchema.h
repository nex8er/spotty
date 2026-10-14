/**
 * \file SettingsSchema.h
 * \brief Декларативное описание настроек плагина.
 */
#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace spotty {

/**
 * \struct SettingsOption
 * \brief Один пункт выпадающего списка для поля типа SettingsField::Choice.
 */
struct SettingsOption
{
    QString label;  ///< Текст для пользователя, обычно через tr().
    QVariant value; ///< Значение, попадающее в настройки.
};

/**
 * \brief Как подать строку сведений (SettingsField::Note): нейтрально, как удачу, как предупреждение или как сбой.
 *
 * Плагин передаёт её в SettingsOption::value первого (единственного) пункта, который
 * возвращает spotty::IInterfacePlugin::liveOptions() для живой строки сведений; пустое
 * значение — Neutral. Ядро ставит перед текстом значок (✓, ⚠, ✕) и красит успех и сбой
 * цветами темы. Значок нужен не для красоты: цвет не должен быть единственным признаком
 * состояния — он не виден дальтонику и пропадает на чёрно-белом экране.
 */
enum class NoteSeverity {
    Neutral = 0, ///< Ход работы или справка: без значка и цвета.
    Ok,          ///< Дело сделано.
    Warning,     ///< Сделано, но есть что учесть, либо ничего не найдено.
    Error,       ///< Не получилось.
};

/// \brief Значение для SettingsOption::value пункта-сведений: см. NoteSeverity.
inline QVariant noteSeverityValue(NoteSeverity severity)
{
    return severity == NoteSeverity::Neutral ? QVariant() : QVariant(static_cast<int>(severity));
}

/**
 * \struct SettingsField
 * \brief Одна настраиваемая величина.
 *
 * Объявлена как агрегат C++20, поэтому плагин описывает настройки назначенными
 * инициализаторами, без builder-обвязки:
 *
 * \code
 * SettingsField{
 *     .key = QStringLiteral("baudRate"),
 *     .label = tr("Baud rate"),
 *     .group = tr("Port"),
 *     .type = SettingsField::Choice,
 *     .defaultValue = 115200,
 *     .options = {{QStringLiteral("9600"), 9600},
 *                 {QStringLiteral("115200"), 115200}},
 *     .editable = true,
 *     .suffix = tr("bps"),
 * }
 * \endcode
 *
 * \note Назначенные инициализаторы в C++ требуют соблюдения порядка объявления полей.
 *       Порядок полей ниже совпадает с порядком, в котором их естественно заполнять.
 */
struct SettingsField
{
    /// \brief Вид редактора, который построит ядро.
    enum Type {
        Choice,  ///< Выпадающий список по #options; при #editable можно ввести своё значение.
        Integer, ///< Числовое поле в границах #minimum и #maximum.
        Toggle,  ///< Флажок.
        Text,    ///< Произвольная строка.

        /**
         * \brief Строка сведений без редактора: состояние, найденное плагином.
         *
         * Слева #label (может быть пустой — тогда текст идёт под полями без подписи, как
         * итог действия над ними), справа текст. Текст берётся из #defaultValue, а при
         * #live — из подписи первого пункта spotty::IInterfacePlugin::liveOptions(), то есть
         * обновляется, пока диалог открыт; значение этого пункта несёт NoteSeverity.
         * Допускается разметка Qt (rich text), ссылки открываются в браузере.
         *
         * \note Значения не хранит: в defaults(), normalized() и настройки канала не попадает.
         */
        Note,

        /**
         * \brief Кнопка с подписью #label. Нажатие зовёт
         *        spotty::IInterfacePlugin::triggerAction() с ключом поля.
         *
         * Нужна там, где плагину есть что сделать по просьбе пользователя, но сам он виджетов
         * создавать не вправе: открыть программу настройки драйвера, сбросить устройство.
         *
         * Кнопка занимает столько места, сколько нужно её подписи, а не всю строку. Несколько
         * кнопок подряд с #compact встают в один ряд, а #compact-кнопка после поля — справа
         * от него («порт устройства — [Найти]»).
         *
         * \note Значения не хранит, как и #Note.
         */
        Action,

        /**
         * \brief Строка, которую вводят скрытыми символами и которая **не попадает на диск**.
         *
         * Для паролей и всего, что не должно лежать в `interfaces.json` открытым текстом.
         * Значение живёт в памяти программы до её закрытия: диалог настроек можно открыть
         * снова и не набирать его заново, а при перезапуске поле пусто. Плагину оно
         * приходит так же, как значение любого поля — в spotty::IInterfacePlugin::triggerAction()
         * и open().
         *
         * \note Хранит значение (holdsValue()), поэтому присутствует в defaults() и
         *       normalized(); исключается из того, что пишется на диск, — см. persistent().
         */
        Password,
    };

    /// \brief Ключ в QVariantMap настроек. Не переводится и не меняется между версиями.
    QString key;

    /// \brief Подпись рядом с редактором, через tr().
    QString label;

    /// \brief Заголовок раздела. Пустая строка — безымянный раздел в начале диалога.
    QString group;

    Type type = Text;      ///< Вид редактора.
    QVariant defaultValue; ///< Значение по умолчанию. Единственное место, где оно задаётся.

    /// \brief Пункты списка. Используется только при #type == #Choice.
    QList<SettingsOption> options;

    /**
     * \brief Пункты списка приходят от плагина в рантайме, а не объявлены здесь.
     *
     * Нужно там, где перечисление становится известно только после опроса: узлы на шине
     * CAN отвечают на широковещательный запрос не мгновенно и не все сразу, а список
     * последовательных портов известен целиком ещё до открытия диалога.
     *
     * Ядро в этом случае строит редактор сразу — по #options, которые могут быть и
     * пустыми, — и дальше, пока поле на экране, повторно спрашивает у плагина
     * spotty::IInterfacePlugin::liveOptions(), дополняя список найденным. Окно поэтому
     * открывается мгновенно и не ждёт опроса; выбранное значение при обновлении не
     * теряется.
     *
     * \note Осмысленно только при #type == #Choice. Стоит держать #editable истинным:
     *       узел, который сейчас молчит, иначе нельзя выбрать вовсе, хотя номер его
     *       пользователю известен.
     */
    bool live = false;

    /**
     * \brief Разрешить ввод значения, отсутствующего в #options.
     *
     * Нужно там, где перечисление принципиально неполно: нестандартная скорость UART,
     * произвольный номер порта.
     */
    bool editable = false;

    /**
     * \brief Открывать канал нельзя, пока это поле пусто.
     *
     * Проверяется как строковое представление значения — «пусто» одинаково понятно для
     * #Text, #Choice и не нужно для #Toggle (у флажка нет пустого состояния) и обычно не
     * нужно для #Integer (число в границах #minimum/#maximum и так не бывает «никаким»).
     * Ядро отказывается открывать канал, если хоть одно обязательное поле пусто —
     * spotty::Session::open() тогда не пытается создать канал вовсе, а сообщает об этом
     * отдельным сигналом, чтобы UI мог не просто показать ошибку, а сразу открыть
     * настройки и подсветить нужное поле.
     */
    bool required = false;

    int minimum = 0; ///< Нижняя граница для #Integer.
    int maximum = 0; ///< Верхняя граница для #Integer.

    /// \brief Единица измерения справа от редактора: `"bps"`, `"ms"`.
    QString suffix;

    /// \brief Однострочное пояснение под редактором. Необязательно.
    QString hint;

    /**
     * \brief Редко нужное поле: ядро прячет его в сворачиваемый раздел «Advanced» своей группы.
     *
     * Свёрнуто по умолчанию, значения при этом хранятся и применяются как обычно. Нужно там, где
     * у настройки есть разумное умолчание и трогают её единицы: порт ssh, путь к ключу, режим
     * управления потоком.
     */
    bool advanced = false;

    /**
     * \brief Короткое поле, которое ставится в ту же строку правее предыдущего.
     *
     * Для пары «адрес — порт»: порт узкий и стоит рядом, а не отдельной строкой под адресом.
     * Подпись такого поля идёт слева от его редактора; пояснение (#hint) становится его
     * всплывающей подсказкой, а не строкой под полем.
     *
     * Для #Action это значит «в тот же ряд, что и предыдущее»: справа от поля, к которому кнопка
     * относится, или рядом с предыдущей кнопкой. Подписи у такой кнопки нет — она на самой кнопке.
     *
     * \note Применяется только в диалоге настроек интерфейса, и только если предыдущее поле той же
     *       группы лежит в той же области (обычной или #advanced) и не является #Note; рядом с
     *       кнопкой встаёт только другая кнопка. Иначе игнорируется, и поле идёт обычной строкой.
     */
    bool compact = false;

    /**
     * \brief Главное действие группы: кнопка получает акцентный контур.
     *
     * Осмысленно только для #Action, и одной на группу. Заливки акцентом здесь нет намеренно:
     * она принадлежит кнопке по умолчанию самого окна («OK», «Send»), и вторая залитая кнопка
     * рядом с ней отнимала бы у неё это значение — глаз снова искал бы главное.
     */
    bool primary = false;

    /**
     * \brief Хранит ли поле значение в настройках.
     * \return `false` для #Note и #Action: они только показывают и действуют.
     *
     * Без этой проверки кнопка попала бы в `interfaces.json` пустым значением, а
     * обязательная строка сведений не давала бы открыть канал.
     */
    bool holdsValue() const { return type != Note && type != Action; }
};

/**
 * \class SettingsSchema
 * \brief Полный набор настроек интерфейса, предоставляемый плагином.
 *
 * \par Зачем это нужно
 *
 * Плагин **не создаёт виджеты**. Он возвращает схему, а диалог настроек интерфейса строит
 * ядро. Отсюда два следствия, ради которых всё и затевалось:
 *
 * 1. Требование «набор настроек зависит от плагина» выполняется бесплатно — новый
 *    транспорт со своими параметрами получает рабочий диалог, не написав ни строчки UI.
 * 2. Qt6::Widgets вообще не входит в зависимости SDK. Плагин линкуется только с
 *    Qt6::Core, его нельзя случайно связать с UI, и он остаётся пригодным для
 *    консольного или тестового окружения.
 *
 * \par Пример
 *
 * \code
 * SettingsSchema UartPlugin::settingsSchema() const
 * {
 *     SettingsSchema schema;
 *     schema.add(SettingsField{
 *         .key = QStringLiteral("baudRate"),
 *         .label = tr("Baud rate"),
 *         .group = tr("Port"),
 *         .type = SettingsField::Choice,
 *         .defaultValue = 115200,
 *         .options = {{QStringLiteral("9600"), 9600},
 *                     {QStringLiteral("115200"), 115200}},
 *         .editable = true,
 *     });
 *     return schema;
 * }
 * \endcode
 */
class SettingsSchema
{
public:
    /**
     * \brief Добавить поле в конец схемы.
     * \param field Описание поля.
     * \return Ссылку на себя — вызовы можно объединять в цепочку.
     *
     * Порядок добавления определяет порядок полей и разделов в диалоге.
     */
    SettingsSchema &add(SettingsField field)
    {
        m_fields.append(std::move(field));
        return *this;
    }

    /// \brief Все поля в порядке объявления.
    const QList<SettingsField> &fields() const { return m_fields; }

    /// \return `true`, если у плагина нет настраиваемых параметров.
    ///         Ядро в этом случае просто не показывает кнопку настроек.
    bool isEmpty() const { return m_fields.isEmpty(); }

    /**
     * \brief Имена разделов в порядке объявления.
     * \return Список уникальных значений SettingsField::group.
     *
     * Порядок именно объявления, а не алфавитный: раскладка диалога следует замыслу
     * автора плагина, который знает, какие параметры логично держать рядом.
     */
    QStringList groups() const
    {
        QStringList result;
        for (const SettingsField &field : m_fields) {
            if (!result.contains(field.group))
                result.append(field.group);
        }
        return result;
    }

    /**
     * \brief Поля одного раздела.
     * \param group Имя раздела (пустая строка — безымянный раздел).
     */
    QList<SettingsField> fieldsInGroup(const QString &group) const
    {
        QList<SettingsField> result;
        for (const SettingsField &field : m_fields) {
            if (field.group == group)
                result.append(field);
        }
        return result;
    }

    /**
     * \brief Найти поле по ключу.
     * \param key Значение SettingsField::key.
     * \return Указатель на поле или `nullptr`, если такого ключа в схеме нет.
     * \warning Указатель действителен, пока схема не изменена вызовом add().
     */
    const SettingsField *field(const QString &key) const
    {
        for (const SettingsField &field : m_fields) {
            if (field.key == key)
                return &field;
        }
        return nullptr;
    }

    /**
     * \brief Настройки по умолчанию, собранные из SettingsField::defaultValue.
     *
     * Отдельного метода `defaultSettings()` в spotty::IInterfacePlugin намеренно нет:
     * умолчания объявлены в полях схемы, и второе место для того же факта неминуемо
     * разошлось бы с первым.
     */
    QVariantMap defaults() const
    {
        QVariantMap result;
        for (const SettingsField &field : m_fields) {
            if (field.holdsValue())
                result.insert(field.key, field.defaultValue);
        }
        return result;
    }

    /**
     * \brief Привести сохранённые настройки в соответствие со схемой.
     * \param values Настройки, прочитанные из `interfaces.json`.
     * \return Карту, где присутствуют ровно объявленные схемой ключи.
     *
     * Отсутствующие ключи получают значение по умолчанию, ключи, которых схема больше не
     * объявляет, отбрасываются.
     *
     * Через этот метод проходит всё чтение настроек — именно поэтому конфигурация,
     * записанная прежней версией плагина, остаётся рабочей после того, как плагин обрёл
     * новый параметр или потерял старый. Без нормализации канал получил бы
     * QVariantMap без нужного ключа и открылся бы с нулевой скоростью или вовсе упал.
     *
     * \see spotty::InterfaceRegistry::settingsFor()
     */
    QVariantMap normalized(const QVariantMap &values) const
    {
        QVariantMap result;
        for (const SettingsField &field : m_fields) {
            if (!field.holdsValue())
                continue;
            const auto it = values.constFind(field.key);
            result.insert(field.key, it != values.constEnd() ? *it : field.defaultValue);
        }
        return result;
    }

    /**
     * \brief Настройки без значений полей SettingsField::Password — то, что можно писать на диск.
     * \param values Настройки устройства.
     * \return Копию без ключей-паролей.
     *
     * Единственное место, где решается, что секретно: сохранение в `interfaces.json` обязано
     * пройти через него, иначе пароль окажется в читаемом файле, который к тому же уезжает
     * вместе с переносным режимом.
     */
    QVariantMap persistent(const QVariantMap &values) const
    {
        QVariantMap result = values;
        for (const SettingsField &field : m_fields) {
            if (field.type == SettingsField::Password)
                result.remove(field.key);
        }
        return result;
    }

    /**
     * \brief Ключи обязательных полей (SettingsField::required), которые сейчас пусты.
     * \param values Настройки, обычно уже нормализованные (см. normalized()).
     * \return Пустой список — можно открывать канал.
     *
     * \see spotty::Session::open(), которая отказывается создавать канал, пока список
     *      не пуст, и сообщает об этом через Session::requiredSettingsMissing() вместо
     *      попытки открыть заведомо нерабочее соединение.
     */
    QStringList missingRequiredFields(const QVariantMap &values) const
    {
        QStringList result;
        for (const SettingsField &field : m_fields) {
            if (!field.required || !field.holdsValue())
                continue;
            if (values.value(field.key, field.defaultValue).toString().trimmed().isEmpty())
                result.append(field.key);
        }
        return result;
    }

private:
    QList<SettingsField> m_fields;
};

} // namespace spotty
