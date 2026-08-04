/**
 * \file MacroStore.cpp
 * \brief Реализация spotty::MacroStore.
 */
#include "MacroStore.h"

#include "settings/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>

namespace spotty {

/// \brief Категория журналирования: `spotty.macros`.
Q_LOGGING_CATEGORY(lcMacros, "spotty.macros")

namespace {

constexpr auto kFileSuffix = ".json";

/// \brief Отсекает имена, непригодные для имени файла или уводящие из каталога.
bool isValidPresetName(const QString &name)
{
    if (name.isEmpty() || name.size() > 64)
        return false;
    // Разделители пути и точки в начале отвергаем: имя пресета приходит от пользователя и
    // не должно позволять писать за пределы каталога макросов.
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    if (name.startsWith(u'.'))
        return false;
    for (const QChar ch : name) {
        if (forbidden.contains(ch))
            return false;
    }
    return true;
}

QJsonObject macroToJson(const Macro &macro)
{
    QJsonObject object;
    object.insert(QStringLiteral("payload"), macro.payload);
    object.insert(QStringLiteral("format"), int(macro.format));
    object.insert(QStringLiteral("termination"), int(macro.termination));
    if (!macro.shortcut.isEmpty())
        object.insert(QStringLiteral("shortcut"), macro.shortcut);
    return object;
}

Macro macroFromJson(const QJsonObject &object)
{
    Macro macro;
    macro.payload = object.value(QStringLiteral("payload")).toString();

    // Наборы, записанные прежними версиями, содержали отдельное имя. Если команда пуста,
    // а имя есть, то имя и было командой — иначе такой макрос молча превратился бы в
    // пустую строку.
    if (macro.payload.isEmpty())
        macro.payload = object.value(QStringLiteral("name")).toString();

    macro.format = DataCodec::Format(
        object.value(QStringLiteral("format")).toInt(int(DataCodec::Format::Text)));
    macro.termination = DataCodec::Termination(
        object.value(QStringLiteral("termination")).toInt(int(DataCodec::Termination::CrLf)));
    macro.shortcut = object.value(QStringLiteral("shortcut")).toString();
    return macro;
}

} // namespace

MacroStore::MacroStore(QString directory)
    : m_directory(std::move(directory))
{
}

QString MacroStore::defaultPresetName()
{
    return QStringLiteral("default");
}

QString MacroStore::filePathFor(const QString &name) const
{
    return QDir(m_directory).filePath(name + QLatin1String(kFileSuffix));
}

QStringList MacroStore::presets() const
{
    QDir dir(m_directory);
    if (!dir.exists())
        return {};

    QStringList result;
    const QFileInfoList entries =
        dir.entryInfoList({QStringLiteral("*%1").arg(QLatin1String(kFileSuffix))},
                          QDir::Files, QDir::Name);
    result.reserve(entries.size());
    for (const QFileInfo &entry : entries)
        result.append(entry.completeBaseName());

    return result;
}

bool MacroStore::loadPreset(const QString &name)
{
    if (!isValidPresetName(name))
        return false;

    m_currentPreset = name;
    m_macros.clear();

    QFile file(filePathFor(name));
    if (!file.exists())
        return true; // Пресет ещё не создан — это нормально.

    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcMacros) << "cannot read" << file.fileName() << file.errorString();
        return false;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        // Файл не трогаем: это единственная копия того, что настроил пользователь.
        qCWarning(lcMacros) << "malformed JSON in" << file.fileName() << error.errorString();
        return false;
    }

    // Допускаем и голый массив, и объект с полем macros: первый формат проще писать
    // руками, второй оставляет место для будущих полей пресета.
    const QJsonArray array = document.isArray()
                                 ? document.array()
                                 : document.object().value(QStringLiteral("macros")).toArray();

    for (const QJsonValue &value : array) {
        if (value.isObject())
            m_macros.append(macroFromJson(value.toObject()));
    }

    return true;
}

bool MacroStore::save()
{
    if (!isValidPresetName(m_currentPreset))
        return false;
    if (!Paths::ensureDir(m_directory))
        return false;

    QJsonArray array;
    for (const Macro &macro : std::as_const(m_macros))
        array.append(macroToJson(macro));

    QJsonObject root;
    root.insert(QStringLiteral("macros"), array);

    QSaveFile file(filePathFor(m_currentPreset));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcMacros) << "cannot write" << file.fileName() << file.errorString();
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcMacros) << "cannot commit" << file.fileName() << file.errorString();
        return false;
    }
    return true;
}

bool MacroStore::createPreset(const QString &name)
{
    if (!isValidPresetName(name) || QFile::exists(filePathFor(name)))
        return false;

    m_currentPreset = name;
    m_macros.clear();
    return save();
}

QString MacroStore::suggestShortcut() const
{
    QSet<QString> taken;
    for (const Macro &macro : m_macros) {
        if (!macro.shortcut.isEmpty())
            taken.insert(macro.shortcut);
    }

    // Функциональные клавиши — единственный ряд, который не отнимает сочетаний у самой
    // программы и не требует модификатора. Дальше двенадцати не идём: назначать
    // тринадцатому макросу «Ctrl+Shift+F1» программа не вправе.
    for (int i = 1; i <= 12; ++i) {
        const QString candidate = QStringLiteral("F%1").arg(i);
        if (!taken.contains(candidate))
            return candidate;
    }
    return {};
}

bool MacroStore::importFrom(const QString &filePath, int *added)
{
    if (added)
        *added = 0;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcMacros) << "cannot read" << filePath << file.errorString();
        return false;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        qCWarning(lcMacros) << "malformed JSON in" << filePath << error.errorString();
        return false;
    }

    const QJsonArray array = document.isArray()
                                 ? document.array()
                                 : document.object().value(QStringLiteral("macros")).toArray();

    QSet<QString> taken;
    for (const Macro &macro : m_macros) {
        if (!macro.shortcut.isEmpty())
            taken.insert(macro.shortcut);
    }

    int count = 0;
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;

        Macro macro = macroFromJson(value.toObject());
        if (macro.payload.isEmpty())
            continue;

        // Занятое сочетание снимаем: одно и то же нажатие не может отвечать за две разные
        // команды, а молча перехватывать чужую хуже, чем оставить импортированную без
        // клавиши.
        if (!macro.shortcut.isEmpty() && taken.contains(macro.shortcut))
            macro.shortcut.clear();
        else if (!macro.shortcut.isEmpty())
            taken.insert(macro.shortcut);

        m_macros.append(macro);
        ++count;
    }

    if (added)
        *added = count;

    return count > 0 || array.isEmpty();
}

bool MacroStore::exportTo(const QString &filePath) const
{
    QJsonArray array;
    for (const Macro &macro : m_macros)
        array.append(macroToJson(macro));

    QJsonObject root;
    root.insert(QStringLiteral("macros"), array);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcMacros) << "cannot write" << filePath << file.errorString();
        return false;
    }

    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcMacros) << "cannot commit" << filePath << file.errorString();
        return false;
    }
    return true;
}

bool MacroStore::deletePreset(const QString &name)
{
    if (!isValidPresetName(name))
        return false;

    if (!QFile::remove(filePathFor(name)))
        return false;

    if (m_currentPreset == name) {
        m_currentPreset.clear();
        m_macros.clear();
    }
    return true;
}

} // namespace spotty
