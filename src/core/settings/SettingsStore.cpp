/**
 * \file SettingsStore.cpp
 * \brief Реализация spotty::SettingsStore.
 */
#include "SettingsStore.h"

#include "Paths.h"

#include <spotty/data/FileUtils.h>

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTimer>

namespace spotty {

/// \brief Категория журналирования: `spotty.settings`.
Q_LOGGING_CATEGORY(lcSettings, "spotty.settings")

namespace {

/**
 * \brief Задержка отложенной записи, мс.
 *
 * Достаточно велика, чтобы схлопнуть серию изменений от перетаскивания ползунка, и
 * достаточно мала, чтобы настройки не потерялись при аварийном завершении.
 */
constexpr int kSaveDebounceMs = 400;

/**
 * \brief Пройти по вложенным картам вдоль пути и вернуть значение.
 * \param root Корневая карта.
 * \param path Непустой разобранный путь ключа.
 * \return Значение либо недействительный QVariant, если путь не существует или
 *         упирается в не-карту.
 */
QVariant valueAt(const QVariantMap &root, const QStringList &path)
{
    const auto it = root.constFind(path.first());
    if (it == root.constEnd())
        return {};
    if (path.size() == 1)
        return *it;
    if (it->typeId() != QMetaType::QVariantMap)
        return {};
    return valueAt(it->toMap(), path.mid(1));
}

/**
 * \brief Записать или удалить значение по пути, создавая промежуточные карты.
 * \param root Корневая карта, изменяется на месте.
 * \param path Непустой разобранный путь ключа.
 * \param value Записываемое значение; при \p remove == `true` игнорируется.
 * \param remove Удалить конечный ключ вместо записи.
 */
void setValueAt(QVariantMap &root, const QStringList &path, const QVariant &value, bool remove)
{
    if (path.size() == 1) {
        if (remove)
            root.remove(path.first());
        else
            root.insert(path.first(), value);
        return;
    }

    QVariant &child = root[path.first()];
    if (child.typeId() != QMetaType::QVariantMap)
        child = QVariantMap{};

    QVariantMap nested = child.toMap();
    setValueAt(nested, path.mid(1), value, remove);
    child = nested;
}

/// \brief Разобрать ключ на составляющие, отбрасывая пустые части.
QStringList splitKey(const QString &key)
{
    return key.split(u'/', Qt::SkipEmptyParts);
}

} // namespace

SettingsStore::SettingsStore(QString filePath, QObject *parent)
    : QObject(parent)
    , m_filePath(std::move(filePath))
{
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(kSaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this] { save(); });
}

SettingsStore::~SettingsStore()
{
    if (m_dirty)
        save();
}

QVariant SettingsStore::value(const QString &key, const QVariant &fallback) const
{
    const QStringList path = splitKey(key);
    if (path.isEmpty())
        return fallback;

    const QVariant result = valueAt(m_data, path);
    return result.isValid() ? result : fallback;
}

void SettingsStore::setValue(const QString &key, const QVariant &value)
{
    const QStringList path = splitKey(key);
    if (path.isEmpty())
        return;
    // Без этой проверки перестроение списка интерфейсов раз в секунду дёргало бы диск,
    // не меняя при этом ни байта.
    if (valueAt(m_data, path) == value)
        return;

    setValueAt(m_data, path, value, /*remove=*/false);
    Q_EMIT valueChanged(key, value);
    scheduleSave();
}

bool SettingsStore::contains(const QString &key) const
{
    const QStringList path = splitKey(key);
    return !path.isEmpty() && valueAt(m_data, path).isValid();
}

void SettingsStore::remove(const QString &key)
{
    const QStringList path = splitKey(key);
    if (path.isEmpty() || !valueAt(m_data, path).isValid())
        return;

    setValueAt(m_data, path, {}, /*remove=*/true);
    Q_EMIT valueChanged(key, {});
    scheduleSave();
}

void SettingsStore::clear()
{
    if (m_data.isEmpty())
        return;

    m_data.clear();
    Q_EMIT valueChanged(QString(), {});
    scheduleSave();
}

QVariantMap SettingsStore::group(const QString &key) const
{
    return value(key).toMap();
}

void SettingsStore::setGroup(const QString &key, const QVariantMap &values)
{
    setValue(key, values);
}

bool SettingsStore::load()
{
    QFile file(m_filePath);
    if (!file.exists()) {
        // Первый запуск. Не ошибка: действуют умолчания, файл появится при первой записи.
        m_data.clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSettings) << "cannot read" << m_filePath << file.errorString();
        return false;
    }

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        // Испорченный файл сохраняем как есть, а не затираем: это единственная копия того,
        // что настроил пользователь, и он может захотеть починить её руками.
        qCWarning(lcSettings) << "malformed JSON in" << m_filePath << error.errorString()
                              << "- continuing with defaults, file left untouched";
        m_data.clear();
        return false;
    }

    m_data = document.object().toVariantMap();
    m_dirty = false;
    return true;
}

bool SettingsStore::save()
{
    m_saveTimer->stop();

    if (!ensureDir(QFileInfo(m_filePath).absolutePath()))
        return false;

    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qCWarning(lcSettings) << "cannot write" << m_filePath << file.errorString();
        return false;
    }

    file.write(QJsonDocument(QJsonObject::fromVariantMap(m_data)).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        qCWarning(lcSettings) << "cannot commit" << m_filePath << file.errorString();
        return false;
    }

    m_dirty = false;
    return true;
}

void SettingsStore::scheduleSave()
{
    m_dirty = true;
    m_saveTimer->start();
}

} // namespace spotty
