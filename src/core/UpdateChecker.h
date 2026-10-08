/**
 * \file UpdateChecker.h
 * \brief Проверка наличия новой версии среди релизов на GitHub.
 */
#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>

#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace spotty {

/**
 * \struct ReleaseInfo
 * \brief Опубликованный релиз: то, что нужно показать и куда отправить пользователя.
 */
struct ReleaseInfo
{
    QString version;   ///< Номер без префикса `v`: `"0.9.0"`.
    QString tag;       ///< Тег как на GitHub: `"v0.9.0"`.
    QUrl pageUrl;      ///< Страница релиза с описанием и всеми файлами.
    QUrl assetUrl;     ///< Пакет для этой системы; пустой, если подходящего нет.
    QString assetName; ///< Имя файла пакета (для показа); пустое вместе с \ref assetUrl.
    QString notes;     ///< Описание релиза (Markdown как есть), усечённое до разумной длины.

    /// \brief Куда вести кнопку «скачать»: пакет, а при его отсутствии — страница релиза.
    QUrl downloadUrl() const { return assetUrl.isValid() ? assetUrl : pageUrl; }
};

/**
 * \class UpdateChecker
 * \brief Спрашивает у GitHub последний релиз и сообщает, новее ли он запущенной версии.
 *
 * \par Почему API релизов, а не свой файл на сервере
 *
 * Хостинг уже есть: `releases/latest` отдаёт тег, описание и список файлов, а публикует
 * их тот же джоб CI, что собирает пакеты. Отдельного манифеста, который можно забыть
 * обновить, нет вовсе. Эндпоинт возвращает только опубликованный не-черновик и не
 * предварительный выпуск, поэтому фильтровать их самим не нужно.
 *
 * \par Что проверка не делает
 *
 * Ничего не скачивает и не подменяет: найдя версию, лишь сообщает о ней. Установка
 * поверх работающей программы на трёх системах (подпись macOS, AppImage, установщик
 * Windows) — отдельная задача с отдельными рисками, а ссылка на пакет не требует ни
 * прав, ни доверия к собственному обновлятору.
 *
 * \note Анонимный доступ к API ограничен 60 запросами в час с адреса. Для проверки раз в
 *       запуск этого хватает с запасом; при превышении GitHub отвечает 403, и это
 *       обычная ошибка \ref failed().
 */
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    /**
     * \brief Конструктор.
     * \param endpoint Адрес, по которому запрашивается последний релиз. Пустой — настоящий
     *        GitHub. Тест подставляет `file://`: сетевой слой Qt читает и его, так что
     *        проверка обходится без сервера.
     */
    explicit UpdateChecker(QObject *parent = nullptr, const QUrl &endpoint = {});
    ~UpdateChecker() override;

    /// \brief Адрес последнего релиза настоящего репозитория.
    static QUrl defaultEndpoint();

    /**
     * \brief Начать проверку.
     * \param currentVersion Версия запущенной программы.
     *
     * Результат приходит ровно одним из сигналов. Вызов во время идущей проверки
     * игнорируется — иначе двойной щелчок по кнопке давал бы два ответа.
     */
    void check(const QString &currentVersion);

    /// \return `true`, пока ответ не получен.
    bool isRunning() const { return m_reply != nullptr; }

    /**
     * \brief Сравнить две версии по числовым составляющим.
     * \return Отрицательное, если \p a старее \p b; ноль, если равны; положительное иначе.
     *
     * Префикс `v` и хвост после числовой части (`-rc1`) отбрасываются. Строковое
     * сравнение здесь не годится: `"0.10.0" < "0.9.0"` посимвольно.
     */
    static int compareVersions(const QString &a, const QString &b);

    /**
     * \brief Разобрать ответ `releases/latest`.
     * \param json Тело ответа.
     * \param os Система, под которую искать пакет: `"windows"`, `"macos"` или `"linux"`.
     * \param arch Архитектура: `"x64"` или `"arm64"`.
     * \param error Если не `nullptr`, получает причину отказа.
     * \return Релиз или `std::nullopt`, если ответ не JSON-объект или в нём нет версии.
     */
    static std::optional<ReleaseInfo> parseRelease(const QByteArray &json, const QString &os,
                                                   const QString &arch, QString *error = nullptr);

    /// \brief Система, на которой запущена программа, в записи \ref parseRelease().
    static QString currentOs();

    /// \brief Архитектура процессора в записи \ref parseRelease().
    static QString currentArch();

Q_SIGNALS:
    /// \brief Вышла версия новее запущенной.
    void updateAvailable(const spotty::ReleaseInfo &release);

    /// \brief Запущена последняя версия (или новее — сборка из разработки).
    void upToDate(const QString &latestVersion);

    /// \brief Проверить не удалось: нет сети, лимит запросов, неразборчивый ответ.
    void failed(const QString &message);

private:
    void onFinished();

    QNetworkAccessManager *m_manager = nullptr;
    QNetworkReply *m_reply = nullptr;
    QUrl m_endpoint;
    QString m_currentVersion;
};

} // namespace spotty

Q_DECLARE_METATYPE(spotty::ReleaseInfo)
