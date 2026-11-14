/**
 * \file UpdateInstaller.cpp
 * \brief Загрузка обновления и запуск помощника для замены после выхода.
 */
#include "UpdateInstaller.h"
#include "UpdateScripts.h"

#include <UpdateChecker.h>

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTimer>

namespace spotty {
namespace {

constexpr qint64 kMaximumPackageBytes = 1024LL * 1024 * 1024;

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.flush();
}

bool runProcess(const QString &program, const QStringList &arguments, QWidget *parent,
                QString *error, bool cancellable = true)
{
    QProgressDialog progress(UpdateInstaller::tr("Preparing the update…"),
                             UpdateInstaller::tr("Cancel"), 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    if (!cancellable)
        progress.setCancelButton(nullptr);
    QProcess process;
    QEventLoop loop;
    bool cancelled = false;
    QObject::connect(&progress, &QProgressDialog::canceled, &process, [&] {
        cancelled = true;
        process.kill();
    });
    QObject::connect(&process, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&process, &QProcess::errorOccurred, &loop, &QEventLoop::quit);
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &process, &QProcess::kill);
    process.start(program, arguments);
    timer.start(300000);
    progress.show();
    loop.exec();
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished();
    }
    if (cancelled) {
        *error = {};
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0
        || process.error() == QProcess::FailedToStart) {
        *error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (error->isEmpty())
            *error = process.errorString();
        return false;
    }
    return true;
}

bool download(const QUrl &url, qint64 expectedSize, const QString &digest,
              const QString &path, QWidget *parent, QString *error)
{
    if (url.scheme() != QLatin1String("https") || url.host().isEmpty()) {
        *error = UpdateInstaller::tr("The update package has an invalid address.");
        return false;
    }
    if (expectedSize < 0 || expectedSize > kMaximumPackageBytes) {
        *error = UpdateInstaller::tr("The update package is too large.");
        return false;
    }
    const QString hash = digest.trimmed().toLower();
    static const QRegularExpression kSha256(QStringLiteral("^sha256:[0-9a-f]{64}$"));
    if (!hash.isEmpty() && !kSha256.match(hash).hasMatch()) {
        *error = UpdateInstaller::tr("The update checksum is not supported.");
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Spotty/%1").arg(SPOTTY_VERSION));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(30000);
    QNetworkReply *reply = manager.get(request);
    QCryptographicHash sha256(QCryptographicHash::Sha256);
    QProgressDialog progress(UpdateInstaller::tr("Downloading the update…"),
                             UpdateInstaller::tr("Cancel"), 0, 1000, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    QEventLoop loop;
    bool cancelled = false;
    bool writeFailed = false;
    qint64 received = 0;
    const auto consume = [&] {
        const QByteArray bytes = reply->readAll();
        received += bytes.size();
        if (received > kMaximumPackageBytes || file.write(bytes) != bytes.size()) {
            writeFailed = true;
            reply->abort();
            return;
        }
        sha256.addData(bytes);
    };
    QObject::connect(reply, &QIODevice::readyRead, &loop, consume);
    QObject::connect(reply, &QNetworkReply::downloadProgress, &progress,
                     [&progress](qint64 current, qint64 total) {
                         if (total > 0)
                             progress.setValue(int(qMin<qint64>(1000, current * 1000 / total)));
                         else
                             progress.setRange(0, 0);
                     });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, [&] {
        cancelled = true;
        reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    progress.show();
    loop.exec();
    if (reply->bytesAvailable() > 0 && !writeFailed)
        consume();
    const auto networkError = reply->error();
    const QString networkMessage = reply->errorString();
    reply->deleteLater();
    if (cancelled) {
        *error = {};
        return false;
    }
    if (writeFailed || !file.flush()) {
        *error = UpdateInstaller::tr("Could not save the downloaded package.");
        return false;
    }
    if (networkError != QNetworkReply::NoError) {
        *error = networkMessage;
        return false;
    }
    if (received == 0 || (expectedSize > 0 && received != expectedSize)
        || (!hash.isEmpty() && sha256.result().toHex() != hash.mid(7).toLatin1())) {
        *error = UpdateInstaller::tr("The downloaded package failed verification.");
        return false;
    }
    return true;
}

} // namespace

void UpdateInstaller::install(QWidget *parent, const ReleaseInfo &release)
{
    QTemporaryDir temporary(QDir::tempPath() + QStringLiteral("/spotty-update-XXXXXX"));
    QString error;
    auto fail = [&] {
        if (!error.isEmpty())
            QMessageBox::critical(parent, tr("Update failed"), error);
    };
    if (!temporary.isValid()) {
        error = tr("Could not create a temporary directory for the update.");
        fail();
        return;
    }

    QUrl url = release.assetUrl;
    QString digest = release.assetDigest;
    qint64 size = release.assetSize;
    QString kind;
    QString target;
#if defined(Q_OS_WIN)
    target = QCoreApplication::applicationFilePath();
    const QDir appDir(QFileInfo(target).absolutePath());
    const bool installed = !appDir.entryList({QStringLiteral("unins*.exe")}, QDir::Files).isEmpty();
    kind = installed ? QStringLiteral("setup") : QStringLiteral("portable");
    if (!installed) {
        url = release.portableAssetUrl;
        digest = release.portableAssetDigest;
        size = release.portableAssetSize;
    }
#elif defined(Q_OS_MACOS)
    QDir app(QCoreApplication::applicationDirPath());
    if (app.dirName() != QLatin1String("MacOS") || !app.cdUp()
        || app.dirName() != QLatin1String("Contents") || !app.cdUp()
        || !app.dirName().endsWith(QLatin1String(".app"), Qt::CaseInsensitive)) {
        error = tr("The running application is not an installed macOS bundle.");
        fail();
        return;
    }
    target = QFileInfo(app.absolutePath()).canonicalFilePath();
    kind = QStringLiteral("macos");
    if (target.isEmpty()) {
        error = tr("The running application is not an installed macOS bundle.");
        fail();
        return;
    }
#else
    target = QFileInfo(QString::fromLocal8Bit(qgetenv("APPIMAGE"))).canonicalFilePath();
    kind = QStringLiteral("linux");
    if (target.isEmpty()) {
        error = tr("Automatic updates are available when Spotty is running as an AppImage.");
        fail();
        return;
    }
#endif
    if (!url.isValid() || url.isEmpty()) {
        error = tr("No update package is available for this installation.");
        fail();
        return;
    }
#if defined(Q_OS_WIN)
    if (kind == QLatin1String("portable"))
#endif
    {
        QTemporaryFile probe(QFileInfo(target).absolutePath()
                             + QStringLiteral("/.spotty-write-XXXXXX"));
        if (!probe.open()) {
            error = tr("Cannot update this location. Move Spotty to a writable location and try again.");
            fail();
            return;
        }
        probe.close();
        probe.remove();
    }

    const QString packageName = kind == QLatin1String("setup") ? QStringLiteral("update.exe")
        : kind == QLatin1String("portable") ? QStringLiteral("update.zip")
        : kind == QLatin1String("macos") ? QStringLiteral("update.dmg")
                                          : QStringLiteral("update.AppImage");
    const QString package = temporary.filePath(packageName);
    if (!download(url, size, digest, package, parent, &error)) {
        fail();
        return;
    }
    QString payload = package;
#if defined(Q_OS_LINUX)
    QFile image(package);
    const QByteArray appImageMagic("AI\x02", 3);
    if (!image.open(QIODevice::ReadOnly)) {
        error = tr("The update package does not contain a valid Spotty application.");
        fail();
        return;
    }
    const QByteArray header = image.read(11);
    if (!header.startsWith(QByteArray("\x7f" "ELF", 4)) || header.mid(8, 3) != appImageMagic) {
        error = tr("The update package does not contain a valid Spotty application.");
        fail();
        return;
    }
#endif
#if defined(Q_OS_WIN)
    if (kind == QLatin1String("setup")) {
        QFile installer(package);
        if (!installer.open(QIODevice::ReadOnly) || installer.read(2) != QByteArrayLiteral("MZ")) {
            error = tr("The update package does not contain a valid Spotty application.");
            fail();
            return;
        }
    }
    if (kind == QLatin1String("portable")) {
        const QString extractScript = temporary.filePath(QStringLiteral("extract.ps1"));
        const QString extracted = temporary.filePath(QStringLiteral("extracted"));
        if (!writeFile(extractScript, update_scripts::kExtractZip)) {
            error = tr("Could not prepare the update package.");
            fail();
            return;
        }
        if (!runProcess(QStringLiteral("powershell.exe"),
                           {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                            QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                            QStringLiteral("-File"), extractScript, package, extracted}, parent, &error)) {
            fail();
            return;
        }
        payload = QDir(extracted).filePath(QStringLiteral("Spotty"));
        QFile executable(QDir(payload).filePath(QStringLiteral("spotty.exe")));
        if (!executable.open(QIODevice::ReadOnly)
            || executable.read(2) != QByteArrayLiteral("MZ")) {
            error = tr("The update package does not contain Spotty.");
            fail();
            return;
        }
    }
#elif defined(Q_OS_MACOS)
    const QString mount = temporary.filePath(QStringLiteral("mount"));
    QDir().mkpath(mount);
    if (!runProcess(QStringLiteral("/usr/bin/hdiutil"),
                    {QStringLiteral("attach"), QStringLiteral("-readonly"), QStringLiteral("-nobrowse"),
                     QStringLiteral("-mountpoint"), mount, package}, parent, &error)) {
        fail();
        return;
    }
    const QStringList bundles = QDir(mount).entryList({QStringLiteral("*.app")}, QDir::Dirs);
    payload = temporary.filePath(QStringLiteral("new.app"));
    bool prepared = false;
    if (bundles.size() == 1) {
        prepared = runProcess(QStringLiteral("/usr/bin/ditto"),
                              {QDir(mount).filePath(bundles.first()), payload}, parent, &error);
    } else {
        error = tr("The update package does not contain a valid Spotty application.");
    }
    QString detachError;
    const bool detached = runProcess(QStringLiteral("/usr/bin/hdiutil"),
                                     {QStringLiteral("detach"), mount}, parent, &detachError,
                                     false);
    if (!detached && error.isEmpty())
        error = detachError;
    if (!prepared || !detached) {
        fail();
        return;
    }
    if (!QFileInfo::exists(QDir(payload).filePath(QStringLiteral("Contents/MacOS/spotty")))) {
        error = tr("The update package does not contain a valid Spotty application.");
        fail();
        return;
    }
    prepared = runProcess(QStringLiteral("/usr/bin/codesign"),
                          {QStringLiteral("--verify"), QStringLiteral("--deep"),
                           QStringLiteral("--strict"), payload}, parent, &error);
    if (!prepared) {
        fail();
        return;
    }
#endif

    const QString script = temporary.filePath(kind == QLatin1String("setup")
                                                   || kind == QLatin1String("portable")
                                               ? QStringLiteral("update.ps1")
                                               : QStringLiteral("update.sh"));
    const bool windows = kind == QLatin1String("setup") || kind == QLatin1String("portable");
    if (!writeFile(script, windows ? update_scripts::kWindowsHelper : update_scripts::kPosixHelper)) {
        error = tr("Could not prepare the update package.");
        fail();
        return;
    }
    const QString pid = QString::number(QCoreApplication::applicationPid());
    const QStringList args = windows
        ? QStringList{QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"),
                      QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                      QStringLiteral("-File"), script, pid, kind, payload, target, temporary.path(),
                      tr("Update failed"), tr("Could not install the update. Check the application folder and the temporary directory.")}
        : QStringList{script, pid, payload, target, temporary.path(), kind,
                      tr("Could not install the update. Check the application folder and the temporary directory.")};
    const QString program = windows ? QStringLiteral("powershell.exe") : QStringLiteral("/bin/sh");
    if (!QProcess::startDetached(program, args)) {
        error = tr("Could not start the update helper.");
        fail();
        return;
    }
    temporary.setAutoRemove(false);
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

} // namespace spotty
