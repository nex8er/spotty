/**
 * \file PlotProfile.cpp
 * \brief Реализация spotty::PlotProfile и spotty::PlotProfileStore.
 */
#include <spotty/data/PlotProfile.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSaveFile>

namespace spotty {

namespace {

Q_LOGGING_CATEGORY(lcProfiles, "spotty.plotter")

constexpr auto kFileSuffix = ".json";

/// \brief Совпадение имени колонки добавляет столько к счёту профиля.
constexpr int kNameMatchWeight = 10;

/// \brief Само совпадение числа колонок стоит столько — оно обязательно, но слабо.
constexpr int kColumnCountWeight = 1;

QJsonObject seriesToJson(const PlotProfileSeries &series)
{
    QJsonObject object;
    object.insert(QStringLiteral("name"), series.name);
    object.insert(QStringLiteral("nameIsCustom"), series.nameIsCustom);
    object.insert(QStringLiteral("color"), qint64(series.color));
    object.insert(QStringLiteral("visible"), series.visible);
    if (series.hasCustomRange) {
        object.insert(QStringLiteral("minimum"), series.customMinimum);
        object.insert(QStringLiteral("maximum"), series.customMaximum);
    }
    return object;
}

PlotProfileSeries seriesFromJson(const QJsonObject &object)
{
    PlotProfileSeries series;
    series.name = object.value(QStringLiteral("name")).toString();
    series.nameIsCustom = object.value(QStringLiteral("nameIsCustom")).toBool();
    series.color = quint32(object.value(QStringLiteral("color")).toInteger());
    series.visible = object.value(QStringLiteral("visible")).toBool(true);
    // Пределы считаются заданными ровно тогда, когда они в файле есть: отдельный флаг
    // разошёлся бы с числами при правке файла руками.
    series.hasCustomRange = object.contains(QStringLiteral("minimum"))
                            && object.contains(QStringLiteral("maximum"));
    series.customMinimum = object.value(QStringLiteral("minimum")).toDouble();
    series.customMaximum = object.value(QStringLiteral("maximum")).toDouble(1.0);
    return series;
}

} // namespace

QStringList PlotProfile::columnNames() const
{
    QStringList names;
    names.reserve(series.size());
    for (const PlotProfileSeries &item : series)
        names.append(item.name);
    return names;
}

int PlotProfile::matchScore(int columnCount, const QStringList &names) const
{
    // Число колонок обязательно: профиль на шесть колонок не описывает поток из трёх.
    if (series.size() != columnCount || columnCount == 0)
        return 0;

    int score = kColumnCountWeight;
    for (int i = 0; i < names.size() && i < series.size(); ++i) {
        if (!names.at(i).isEmpty() && names.at(i) == series.at(i).name)
            score += kNameMatchWeight;
    }
    return score;
}

PlotProfileStore::PlotProfileStore(QString directory)
    : m_directory(std::move(directory))
{
}

bool PlotProfileStore::isValidName(const QString &name)
{
    if (name.isEmpty() || name.size() > 64)
        return false;
    // Разделители пути и точка в начале отвергаются: имя приходит от пользователя и не
    // должно позволять писать за пределы каталога профилей.
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    if (name.startsWith(u'.'))
        return false;
    for (const QChar ch : name) {
        if (forbidden.contains(ch))
            return false;
    }
    return true;
}

QString PlotProfileStore::filePathFor(const QString &name) const
{
    return QDir(m_directory).filePath(name + QLatin1String(kFileSuffix));
}

QStringList PlotProfileStore::profiles() const
{
    QDir dir(m_directory);
    if (!dir.exists())
        return {};

    QStringList names;
    const QStringList files =
        dir.entryList({QStringLiteral("*%1").arg(QLatin1String(kFileSuffix))}, QDir::Files,
                      QDir::Name);
    names.reserve(files.size());
    for (const QString &file : files)
        names.append(file.chopped(int(qstrlen(kFileSuffix))));
    return names;
}

PlotProfile PlotProfileStore::load(const QString &name) const
{
    PlotProfile profile;
    if (!isValidName(name))
        return profile;

    QFile file(filePathFor(name));
    if (!file.exists() || !file.open(QIODevice::ReadOnly))
        return profile;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        // Испорченный файл не переписывается и не подменяется умолчаниями: пользователь
        // мог править его руками, и молча стереть правку хуже, чем отказать.
        qCWarning(lcProfiles) << "malformed JSON in" << file.fileName() << error.errorString();
        return profile;
    }

    const QJsonObject root = document.object();
    profile.name = name;
    profile.separator = root.value(QStringLiteral("separator")).toString(QStringLiteral(","));
    profile.xAxis = root.value(QStringLiteral("xAxis")).toInt(-1);
    profile.capacity = root.value(QStringLiteral("capacity")).toInt(50000);
    profile.mode = root.value(QStringLiteral("mode")).toString(QStringLiteral("timeseries"));
    profile.lastUsed =
        QDateTime::fromString(root.value(QStringLiteral("lastUsed")).toString(), Qt::ISODate);

    const QJsonArray series = root.value(QStringLiteral("series")).toArray();
    profile.series.reserve(series.size());
    for (const QJsonValue &value : series)
        profile.series.append(seriesFromJson(value.toObject()));

    return profile;
}

bool PlotProfileStore::save(const PlotProfile &profile) const
{
    if (!isValidName(profile.name))
        return false;

    QDir().mkpath(m_directory);

    QJsonArray series;
    for (const PlotProfileSeries &item : profile.series)
        series.append(seriesToJson(item));

    QJsonObject root;
    root.insert(QStringLiteral("series"), series);
    root.insert(QStringLiteral("separator"), profile.separator);
    root.insert(QStringLiteral("xAxis"), profile.xAxis);
    root.insert(QStringLiteral("capacity"), profile.capacity);
    root.insert(QStringLiteral("mode"), profile.mode);
    root.insert(QStringLiteral("lastUsed"),
                (profile.lastUsed.isValid() ? profile.lastUsed : QDateTime::currentDateTimeUtc())
                    .toString(Qt::ISODate));

    // QSaveFile: прерванная запись не оставит наполовину написанный профиль на месте
    // рабочего.
    QSaveFile file(filePathFor(profile.name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcProfiles) << "cannot write" << file.fileName() << file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.commit();
}

bool PlotProfileStore::remove(const QString &name) const
{
    if (!isValidName(name))
        return false;
    return QFile::remove(filePathFor(name));
}

QString PlotProfileStore::bestMatch(int columnCount, const QStringList &names) const
{
    QString best;
    int bestScore = 0;
    QDateTime bestUsed;

    for (const QString &name : profiles()) {
        const PlotProfile profile = load(name);
        const int score = profile.matchScore(columnCount, names);
        if (score == 0)
            continue;

        // Больше совпавших имён — лучше; при равенстве выигрывает тот, которым
        // пользовались позже: человек почти наверняка хочет именно его.
        if (score > bestScore || (score == bestScore && profile.lastUsed > bestUsed)) {
            best = name;
            bestScore = score;
            bestUsed = profile.lastUsed;
        }
    }

    return best;
}

} // namespace spotty
