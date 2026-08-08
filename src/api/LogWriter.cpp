/**
 * \file LogWriter.cpp
 * \brief Реализация spotty::LogWriter.
 */
#include <spotty/data/LogWriter.h>

#include <spotty/data/FileUtils.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>

namespace spotty {

/// \brief Категория журналирования: `spotty.log`.
Q_LOGGING_CATEGORY(lcLog, "spotty.log")

namespace {

constexpr auto kFileSuffix = ".log";

/// \brief Символы, недопустимые в имени файла на любой из поддерживаемых систем.
QString sanitize(const QString &text)
{
    QString result = text;
    static const QString forbidden = QStringLiteral("/\\:*?\"<>|");
    for (QChar &ch : result) {
        if (forbidden.contains(ch) || ch < u' ')
            ch = u'_';
    }
    return result.trimmed();
}

} // namespace

LogWriter::LogWriter(QObject *parent)
    : QObject(parent)
{
    // Каталог по умолчанию не резолвим здесь: Paths::defaultLogDir() на macOS трогает
    // Documents и поднимает системный запрос доступа даже тогда, когда владелец вот-вот
    // задаст свой каталог через setDirectory(). Ту же развилку «пусто — значит умолчание»
    // уже делает AppSettings::effectiveLogDirectory(), вызывающий setDirectory() сразу
    // после конструктора; здесь достаточно оставить путь пустым до этого момента.
}

LogWriter::~LogWriter()
{
    stop();
}

void LogWriter::setDirectory(const QString &directory)
{
    m_directory = directory;
}

void LogWriter::setFileNameTemplate(const QString &fileNameTemplate)
{
    if (!fileNameTemplate.isEmpty())
        m_template = fileNameTemplate;
}

void LogWriter::setFilterAnsi(bool filter)
{
    m_filterAnsi = filter;
}

void LogWriter::setIncludeTx(bool include)
{
    m_includeTx = include;
}

bool LogWriter::isRecording() const
{
    return m_file != nullptr;
}

QString LogWriter::resolveFileName(const QString &interfaceName, const QString &alias) const
{
    const QDateTime now = QDateTime::currentDateTime();

    QString name = m_template;
    name.replace(QLatin1String("{interface}"), sanitize(interfaceName));
    name.replace(QLatin1String("{alias}"), sanitize(alias.isEmpty() ? interfaceName : alias));
    name.replace(QLatin1String("{date}"), now.toString(QStringLiteral("yyyy-MM-dd")));
    name.replace(QLatin1String("{time}"), now.toString(QStringLiteral("HH-mm-ss")));

    name = sanitize(name);
    if (name.isEmpty())
        name = now.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));

    return name + QLatin1String(kFileSuffix);
}

bool LogWriter::start(const QString &interfaceName, const QString &alias)
{
    stop();

    if (!ensureDir(m_directory)) {
        Q_EMIT errorOccurred(tr("Cannot create the log directory: %1").arg(m_directory));
        return false;
    }

    QString path = QDir(m_directory).filePath(resolveFileName(interfaceName, alias));

    // Столкновение имён вероятно: две записи в одну секунду дают одно имя, а дописывать в
    // чужой лог нельзя — он принадлежит другому сеансу.
    int suffix = 1;
    while (QFile::exists(path) && suffix < 1000) {
        QString candidate = resolveFileName(interfaceName, alias);
        candidate.chop(int(qstrlen(kFileSuffix)));
        path = QDir(m_directory).filePath(
            QStringLiteral("%1_%2%3").arg(candidate).arg(suffix).arg(QLatin1String(kFileSuffix)));
        ++suffix;
    }

    m_file = new QFile(path, this);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString message = tr("Cannot open the log file: %1").arg(m_file->errorString());
        delete m_file;
        m_file = nullptr;
        Q_EMIT errorOccurred(message);
        return false;
    }

    m_currentPath = path;
    m_bytesWritten = 0;

    qCInfo(lcLog) << "recording to" << path;
    Q_EMIT recordingStarted(path);
    return true;
}

void LogWriter::stop()
{
    // Придержанный хвост дописывается до закрытия файла. Последняя строка обычно и есть
    // та, ради которой лог включали, — терять её при остановке недопустимо.
    if (m_file && !m_pending.isEmpty()) {
        m_file->write(m_pending);
        m_bytesWritten += m_pending.size();
    }
    m_pending.clear();

    if (!m_file)
        return;

    m_file->flush();
    m_file->close();
    delete m_file;
    m_file = nullptr;

    const QString path = m_currentPath;
    m_currentPath.clear();

    qCInfo(lcLog) << "stopped recording" << path;
    Q_EMIT recordingStopped(path);
}

void LogWriter::setCsvMode(CsvMode mode)
{
    m_csvMode = mode;
}

void LogWriter::setCsvSeparator(QChar separator)
{
    m_csvDetector.setSeparator(separator);
}

QByteArray LogWriter::selectLines(const QByteArray &data)
{
    // Отбор построчный, а поток приходит порциями произвольной длины: судьбу половины
    // строки решить нельзя, не увидев вторую. Незавершённый хвост придерживается до
    // следующей порции.
    m_pending += data;

    QByteArray kept;
    qsizetype start = 0;
    while (true) {
        const qsizetype end = m_pending.indexOf('\n', start);
        if (end < 0)
            break;

        const QByteArray line = m_pending.mid(start, end - start + 1);
        start = end + 1;

        // Сравнивается текст без завершающих \r\n, а записывается строка целиком:
        // журнал должен сохранить исходные окончания строк.
        const QString text = QString::fromUtf8(line).trimmed();
        const bool isData = m_csvDetector.isDataLine(text);
        if ((m_csvMode == CsvMode::OnlyData) == isData)
            kept += line;
    }

    m_pending.remove(0, start);
    return kept;
}

void LogWriter::write(const QByteArray &data, DataDirection direction)
{
    if (!m_file || data.isEmpty())
        return;
    if (direction == DataDirection::Tx && !m_includeTx)
        return;

    QByteArray payload = m_filterAnsi ? stripAnsi(data) : data;
    if (payload.isEmpty())
        return;

    if (m_csvMode != CsvMode::All)
        payload = selectLines(payload);
    if (payload.isEmpty())
        return;

    const qint64 written = m_file->write(payload);
    if (written < 0) {
        const QString message = tr("Log write failed: %1").arg(m_file->errorString());
        stop();
        Q_EMIT errorOccurred(message);
        return;
    }

    // Сброс на каждую порцию: лог нужен ровно тогда, когда что-то пошло не так, и терять
    // последние секунды перед зависанием — терять самое важное.
    m_file->flush();

    m_bytesWritten += written;
    Q_EMIT bytesWrittenChanged(m_bytesWritten);
}

QByteArray LogWriter::stripAnsi(const QByteArray &data)
{
    QByteArray result;
    result.reserve(data.size());

    // Небольшой конечный автомат вместо регулярного выражения: последовательность может
    // быть разорвана между порциями, и регулярное выражение на границе порции ошиблось бы.
    // Здесь незавершённый хвост просто отбрасывается — в файл он всё равно не нужен.
    enum class State { Ground, Escape, Csi, Osc };
    State state = State::Ground;

    for (int i = 0; i < data.size(); ++i) {
        const auto ch = static_cast<unsigned char>(data.at(i));

        switch (state) {
        case State::Ground:
            if (ch == 0x1B)
                state = State::Escape;
            else
                result.append(char(ch));
            break;

        case State::Escape:
            if (ch == '[')
                state = State::Csi;
            else if (ch == ']')
                state = State::Osc;
            else
                state = State::Ground;
            break;

        case State::Csi:
            // Завершающий байт последовательности CSI лежит в диапазоне 0x40..0x7E.
            if (ch >= 0x40 && ch <= 0x7E)
                state = State::Ground;
            break;

        case State::Osc:
            if (ch == 0x07)
                state = State::Ground;
            break;
        }
    }

    return result;
}

QStringList LogWriter::recentLogs(int limit) const
{
    QDir dir(m_directory);
    if (!dir.exists())
        return {};

    const QFileInfoList entries =
        dir.entryInfoList({QStringLiteral("*%1").arg(QLatin1String(kFileSuffix))},
                          QDir::Files, QDir::Time);

    QStringList result;
    result.reserve(qMin(limit, int(entries.size())));
    for (const QFileInfo &entry : entries) {
        if (result.size() >= limit)
            break;
        result.append(entry.absoluteFilePath());
    }
    return result;
}

} // namespace spotty
