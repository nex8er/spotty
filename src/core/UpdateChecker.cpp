/**
 * \file UpdateChecker.cpp
 * \brief Реализация spotty::UpdateChecker.
 */
#include "UpdateChecker.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QVersionNumber>

namespace spotty {

Q_LOGGING_CATEGORY(lcUpdates, "spotty.updates")

namespace {

constexpr auto kLatestReleaseUrl = "https://api.github.com/repos/nex8er/spotty/releases/latest";

/// Ответ GitHub без ответа за это время — сбой, а не медленная сеть: проверка фоновая.
constexpr int kTimeoutMs = 15000;

/// Описание релиза бывает длинным списком коммитов; в окне предложения нужно начало.
constexpr int kMaxNotesLength = 2000;

QString stripPrefix(const QString &version)
{
    const QString trimmed = version.trimmed();
    return trimmed.startsWith(QLatin1Char('v'), Qt::CaseInsensitive) ? trimmed.mid(1) : trimmed;
}

/// Архитектура, зашитая в имя файла; пустая, если имя её не называет.
QString archOfAsset(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains(QLatin1String("arm64")) || lower.contains(QLatin1String("aarch64")))
        return QStringLiteral("arm64");
    if (lower.contains(QLatin1String("x86_64")) || lower.contains(QLatin1String("x64"))
        || lower.contains(QLatin1String("amd64")))
        return QStringLiteral("x64");
    return {};
}

/// Подходит ли файл по виду пакета для системы: установщик, образ, AppImage.
bool isPackageFor(const QString &os, const QString &name)
{
    const QString lower = name.toLower();
    // Windows: именно установщик. Рядом лежит переносной .zip, который распаковывать
    // поверх работающей программы человеку незачем предлагать первым.
    if (os == QLatin1String("windows"))
        return lower.endsWith(QLatin1String("-setup.exe"));
    if (os == QLatin1String("macos"))
        return lower.endsWith(QLatin1String(".dmg"));
    if (os == QLatin1String("linux"))
        return lower.endsWith(QLatin1String(".appimage"));
    return false;
}

QJsonObject pickAsset(const QJsonArray &assets, const QString &os, const QString &arch)
{
    QJsonObject fallback;
    for (const QJsonValue &value : assets) {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value(QLatin1String("name")).toString();
        if (!isPackageFor(os, name))
            continue;

        const QString assetArch = archOfAsset(name);
        if (assetArch == arch)
            return asset;
        // Имя без архитектуры годится на крайний случай; имя с чужой — нет: скачать
        // пакет, который не запустится, хуже, чем открыть страницу релиза.
        if (assetArch.isEmpty() && fallback.isEmpty())
            fallback = asset;
    }
    return fallback;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent, const QUrl &endpoint)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
    , m_endpoint(endpoint.isEmpty() ? defaultEndpoint() : endpoint)
{
}

UpdateChecker::~UpdateChecker()
{
    if (m_reply) {
        // Без отключения abort() поднял бы finished() в уже разрушаемом объекте.
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

QUrl UpdateChecker::defaultEndpoint()
{
    return QUrl(QLatin1String(kLatestReleaseUrl));
}

void UpdateChecker::check(const QString &currentVersion)
{
    if (m_reply)
        return;

    m_currentVersion = currentVersion;

    QNetworkRequest request(m_endpoint);
    // GitHub отклоняет запросы без User-Agent.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Spotty/%1").arg(currentVersion));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(kTimeoutMs);

    m_reply = m_manager->get(request);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::onFinished);
}

void UpdateChecker::onFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 404 у /latest значит «релизов ещё нет», а не «сломан адрес»: текст сетевой
        // ошибки Qt («Not Found») человеку этого не объяснил бы.
        const QString message = status == 404 ? tr("No releases have been published yet.")
                                              : reply->errorString();
        qCInfo(lcUpdates) << "update check failed:" << message;
        Q_EMIT failed(message);
        return;
    }

    QString error;
    const auto release = parseRelease(reply->readAll(), currentOs(), currentArch(), &error);
    if (!release) {
        qCInfo(lcUpdates) << "update check failed:" << error;
        Q_EMIT failed(error);
        return;
    }

    qCInfo(lcUpdates) << "latest release" << release->version << "running" << m_currentVersion;
    if (compareVersions(release->version, m_currentVersion) > 0)
        Q_EMIT updateAvailable(*release);
    else
        Q_EMIT upToDate(release->version);
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    return QVersionNumber::compare(QVersionNumber::fromString(stripPrefix(a)),
                                   QVersionNumber::fromString(stripPrefix(b)));
}

std::optional<ReleaseInfo> UpdateChecker::parseRelease(const QByteArray &json, const QString &os,
                                                       const QString &arch, QString *error)
{
    auto fail = [error](const QString &message) -> std::optional<ReleaseInfo> {
        if (error)
            *error = message;
        return std::nullopt;
    };

    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject())
        return fail(tr("Unexpected reply from the update server."));

    const QJsonObject object = doc.object();
    ReleaseInfo release;
    release.tag = object.value(QLatin1String("tag_name")).toString();
    release.version = stripPrefix(release.tag);

    // Тег без числа («latest», «nightly») версией не является, и сравнивать его нечем:
    // пустой номер оказался бы «старее всего» и молча объявил бы обновления ненужными.
    if (QVersionNumber::fromString(release.version).isNull())
        return fail(tr("The latest release has no version number."));

    release.pageUrl = QUrl(object.value(QLatin1String("html_url")).toString());

    // Описание копируется как есть, но усекается: окно предложения не место для журнала
    // изменений на сотни строк, он лежит на странице релиза.
    release.notes = object.value(QLatin1String("body")).toString().trimmed();
    if (release.notes.size() > kMaxNotesLength)
        release.notes = release.notes.left(kMaxNotesLength).trimmed() + QStringLiteral("…");

    const QJsonObject asset = pickAsset(object.value(QLatin1String("assets")).toArray(), os, arch);
    if (!asset.isEmpty()) {
        release.assetName = asset.value(QLatin1String("name")).toString();
        release.assetUrl = QUrl(asset.value(QLatin1String("browser_download_url")).toString());
    }

    return release;
}

QString UpdateChecker::currentOs()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString UpdateChecker::currentArch()
{
    // Qt и CMake называют одну архитектуру по-разному (arm64 / aarch64, x86_64 / amd64);
    // приводятся к тем же двум словам, которыми размечены имена пакетов.
    const QString arch = QSysInfo::currentCpuArchitecture();
    return arch == QLatin1String("arm64") || arch == QLatin1String("aarch64")
               ? QStringLiteral("arm64")
               : QStringLiteral("x64");
}

} // namespace spotty
