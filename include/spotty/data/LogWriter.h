/**
 * \file LogWriter.h
 * \brief Запись вывода терминала в файл.
 */
#pragma once

// Раньше здесь стоял TerminalBuffer.h целиком, хотя нужен из него был один DataDirection.
// Для SDK это невозможно: буфер живёт в spotty-core, куда плагину дороги нет.
#include <spotty/api/DataDirection.h>
#include <spotty/api/SpottyApiExport.h>

#include <QObject>
#include <QString>

class QFile;

namespace spotty {

/**
 * \class LogWriter
 * \brief Пишет принятое и отправленное в текстовый файл.
 *
 * \par Имя файла
 *
 * Складывается из шаблона с подстановками:
 * - `{interface}` — системное имя устройства;
 * - `{alias}` — псевдоним, а если его нет, то системное имя;
 * - `{date}` — `гггг-ММ-дд`;
 * - `{time}` — `ЧЧ-мм-сс`.
 *
 * По умолчанию `{alias}_{date}_{time}`, то есть «Плата1_2026-08-02_11-57-03.log».
 *
 * \par Сброс на диск
 *
 * Данные пишутся сразу, без промежуточного накопления в памяти. Лог нужен ровно тогда,
 * когда что-то пошло не так, и потерять последние секунды перед зависанием или снятием
 * процесса — потерять самое важное. Плата за это — обращение к диску на каждую порцию;
 * для потока с последовательного порта она незаметна.
 */
class SPOTTY_API_EXPORT LogWriter : public QObject
{
    Q_OBJECT

public:
    explicit LogWriter(QObject *parent = nullptr);
    ~LogWriter() override;

    /// \brief Каталог, в который складываются логи.
    void setDirectory(const QString &directory);
    QString directory() const { return m_directory; }

    /// \brief Шаблон имени файла без расширения.
    void setFileNameTemplate(const QString &fileNameTemplate);
    QString fileNameTemplate() const { return m_template; }

    /**
     * \brief Вырезать управляющие последовательности ANSI из записи.
     *
     * Включено по умолчанию: цветовые коды в файле мешают читать его чем угодно, кроме
     * терминала, и ломают поиск по логу.
     */
    void setFilterAnsi(bool filter);
    bool filterAnsi() const { return m_filterAnsi; }

    /// \brief Записывать ли отправленное вместе с принятым.
    void setIncludeTx(bool include);
    bool includeTx() const { return m_includeTx; }

    /**
     * \brief Начать запись.
     * \param interfaceName Системное имя устройства — для подстановки в шаблон.
     * \param alias Псевдоним устройства; при пустом используется \p interfaceName.
     * \return `false`, если файл не удалось создать.
     */
    bool start(const QString &interfaceName, const QString &alias);

    /// \brief Прекратить запись и закрыть файл.
    void stop();

    bool isRecording() const;

    /// \brief Путь к файлу, в который идёт запись; пустая строка, если запись не идёт.
    QString currentFilePath() const { return m_currentPath; }

    qint64 bytesWritten() const { return m_bytesWritten; }

    /**
     * \brief Файлы логов в каталоге, новые первыми.
     * \param limit Сколько вернуть.
     */
    QStringList recentLogs(int limit = 50) const;

    /// \brief Убрать управляющие последовательности ANSI из текста.
    static QByteArray stripAnsi(const QByteArray &data);

public Q_SLOTS:
    /// \brief Записать порцию данных.
    void write(const QByteArray &data, spotty::DataDirection direction);

Q_SIGNALS:
    void recordingStarted(const QString &filePath);
    void recordingStopped(const QString &filePath);
    void bytesWrittenChanged(qint64 bytes);

    /// \brief Ошибка записи. Запись при этом прекращается.
    void errorOccurred(const QString &message);

private:
    /// \brief Подставить значения в шаблон имени.
    QString resolveFileName(const QString &interfaceName, const QString &alias) const;

    QString m_directory;
    QString m_template = QStringLiteral("{alias}_{date}_{time}");
    bool m_filterAnsi = true;
    bool m_includeTx = true;

    QFile *m_file = nullptr;
    QString m_currentPath;
    qint64 m_bytesWritten = 0;
};

} // namespace spotty
