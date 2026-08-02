/**
 * \file LoggingPanel.cpp
 * \brief Реализация spotty::LoggingPanel и spotty::LogFileList.
 */
#include "LoggingPanel.h"

#include "../AppContext.h"
#include "../Formatting.h"
#include "../theme/MdiIcons.h"
#include "../theme/ThemeManager.h"

#include <InterfaceRegistry.h>
#include <Session.h>
#include <settings/Paths.h>
#include <settings/SettingsStore.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDrag>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kToolGlyphSize = 18;

constexpr auto kKeyFilterAnsi = "logging/filterAnsi";
constexpr auto kKeyIncludeTx = "logging/includeTx";
constexpr auto kKeyDirectory = "logging/directory";
constexpr auto kKeyTemplate = "logging/fileNameTemplate";

} // namespace

// --- LogFileList --------------------------------------------------------------------

LogFileList::LogFileList(QWidget *parent)
    : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
}

QMimeData *LogFileList::mimeDataForSelection() const
{
    QList<QUrl> urls;
    const QList<QListWidgetItem *> items = selectedItems();
    urls.reserve(items.size());

    for (const QListWidgetItem *item : items) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty())
            urls.append(QUrl::fromLocalFile(path));
    }

    if (urls.isEmpty())
        return nullptr;

    auto *mime = new QMimeData;
    // Именно urls, а не текст: только так система понимает, что переносится файл, и
    // почтовый клиент делает из него вложение, а проводник — копию.
    mime->setUrls(urls);

    QStringList paths;
    paths.reserve(urls.size());
    for (const QUrl &url : std::as_const(urls))
        paths.append(url.toLocalFile());
    // Текстовое представление добавляем вдогонку: там, где файл принять некуда, вставится
    // хотя бы путь.
    mime->setText(paths.join(u'\n'));

    return mime;
}

void LogFileList::startDrag(Qt::DropActions supportedActions)
{
    Q_UNUSED(supportedActions);

    QMimeData *mime = mimeDataForSelection();
    if (!mime)
        return;

    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    // Только копирование: перемещение вынесло бы лог из каталога, и следующий запуск его
    // не нашёл бы.
    drag->exec(Qt::CopyAction);
}

void LogFileList::copySelectedFiles()
{
    if (QMimeData *mime = mimeDataForSelection())
        QApplication::clipboard()->setMimeData(mime);
}

void LogFileList::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Copy)) {
        copySelectedFiles();
        event->accept();
        return;
    }
    QListWidget::keyPressEvent(event);
}

// --- LoggingPanel -------------------------------------------------------------------

LoggingPanel::LoggingPanel(const AppContext &context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("Logging"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);

    // --- Управление записью ----------------------------------------------------------

    auto *controlRow = new QHBoxLayout;
    controlRow->setSpacing(6);

    m_recordButton = new QToolButton(this);
    m_recordButton->setAutoRaise(true);
    m_recordButton->setCheckable(true);
    m_recordButton->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));

    m_sizeLabel = new QLabel(this);
    m_sizeLabel->setObjectName(QStringLiteral("hintLabel"));

    controlRow->addWidget(m_recordButton);
    controlRow->addWidget(m_sizeLabel, 1);
    layout->addLayout(controlRow);

    m_fileLabel = new QLabel(this);
    m_fileLabel->setObjectName(QStringLiteral("hintLabel"));
    m_fileLabel->setWordWrap(true);
    layout->addWidget(m_fileLabel);

    // --- Настройки записи ------------------------------------------------------------

    m_filterAnsi = new QCheckBox(tr("Strip ANSI escape sequences"), this);
    m_filterAnsi->setToolTip(
        tr("Colour codes make the file hard to read outside a terminal and break "
           "searching through it."));
    m_filterAnsi->setChecked(
        m_context.settings->value(QLatin1String(kKeyFilterAnsi), true).toBool());

    m_includeTx = new QCheckBox(tr("Include sent data"), this);
    m_includeTx->setChecked(
        m_context.settings->value(QLatin1String(kKeyIncludeTx), true).toBool());

    layout->addWidget(m_filterAnsi);
    layout->addWidget(m_includeTx);

    // --- Список файлов ---------------------------------------------------------------

    auto *listTitle = new QLabel(tr("Recent logs"), this);
    listTitle->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(listTitle);

    m_files = new LogFileList(this);
    m_files->setToolTip(tr("Click to view in the terminal. Drag out or press Ctrl+C to "
                           "copy the file itself."));
    layout->addWidget(m_files, 1);

    // --- Настройка писателя ----------------------------------------------------------

    m_writer.setDirectory(
        m_context.settings->value(QLatin1String(kKeyDirectory), Paths::defaultLogDir())
            .toString());
    m_writer.setFileNameTemplate(
        m_context.settings->value(QLatin1String(kKeyTemplate)).toString());
    m_writer.setFilterAnsi(m_filterAnsi->isChecked());
    m_writer.setIncludeTx(m_includeTx->isChecked());

    // --- Связывание ------------------------------------------------------------------

    connect(m_recordButton, &QToolButton::clicked, this, &LoggingPanel::toggleRecording);

    connect(m_filterAnsi, &QCheckBox::toggled, this, [this](bool on) {
        m_writer.setFilterAnsi(on);
        m_context.settings->setValue(QLatin1String(kKeyFilterAnsi), on);
    });
    connect(m_includeTx, &QCheckBox::toggled, this, [this](bool on) {
        m_writer.setIncludeTx(on);
        m_context.settings->setValue(QLatin1String(kKeyIncludeTx), on);
    });

    connect(m_files, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        Q_EMIT logFileRequested(item->data(Qt::UserRole).toString());
    });

    if (m_context.session) {
        connect(m_context.session, &Session::dataLogged, &m_writer, &LogWriter::write);
    }

    connect(&m_writer, &LogWriter::bytesWrittenChanged, this, [this](qint64 bytes) {
        m_sizeLabel->setText(Formatting::byteCount(bytes));
    });
    connect(&m_writer, &LogWriter::recordingStarted, this, [this](const QString &path) {
        Q_EMIT statusMessage(tr("Recording to %1").arg(QFileInfo(path).fileName()));
        updateRecordingUi();
    });
    connect(&m_writer, &LogWriter::recordingStopped, this, [this] {
        refreshFileList();
        updateRecordingUi();
    });
    connect(&m_writer, &LogWriter::errorOccurred, this, [this](const QString &message) {
        Q_EMIT statusMessage(message);
        updateRecordingUi();
    });

    if (m_context.theme) {
        connect(m_context.theme, &ThemeManager::themeChanged, this,
                [this] { updateRecordingUi(); });
    }

    refreshFileList();
    updateRecordingUi();
    setInterfaceOpen(false);
}

void LoggingPanel::toggleRecording()
{
    if (m_writer.isRecording()) {
        m_writer.stop();
        return;
    }

    if (!m_context.session || !m_context.registry) {
        m_recordButton->setChecked(false);
        return;
    }

    const InterfaceEntry *entry = m_context.registry->entry(m_context.session->interfaceId());
    if (!entry) {
        m_recordButton->setChecked(false);
        Q_EMIT statusMessage(tr("Select an interface before recording."));
        return;
    }

    if (!m_writer.start(entry->descriptor.systemName, entry->alias))
        m_recordButton->setChecked(false);
}

void LoggingPanel::stopRecording()
{
    if (m_writer.isRecording())
        m_writer.stop();
}

bool LoggingPanel::isRecording() const
{
    return m_writer.isRecording();
}

void LoggingPanel::updateRecordingUi()
{
    const bool recording = m_writer.isRecording();

    m_recordButton->setChecked(recording);
    m_recordButton->setIcon(MdiIcons::icon(recording ? mdi::Stop : mdi::Record,
                                           kToolGlyphSize));
    m_recordButton->setToolTip(recording ? tr("Stop recording") : tr("Start recording"));

    if (recording) {
        m_fileLabel->setText(QFileInfo(m_writer.currentFilePath()).fileName());
        m_fileLabel->setToolTip(m_writer.currentFilePath());
    } else {
        m_fileLabel->clear();
        m_fileLabel->setToolTip({});
        m_sizeLabel->clear();
    }

    // Запись имеет смысл только при открытом канале: писать нечего, и имя файла было бы
    // не из чего составить.
    m_recordButton->setEnabled(recording || m_interfaceOpen);
}

void LoggingPanel::setInterfaceOpen(bool open)
{
    m_interfaceOpen = open;

    // Закрытие канала завершает запись: продолжать её означало бы держать открытым файл,
    // в который уже ничего не придёт.
    if (!open && m_writer.isRecording())
        m_writer.stop();

    updateRecordingUi();
}

void LoggingPanel::refreshFileList()
{
    m_files->clear();

    const QStringList logs = m_writer.recentLogs();
    for (const QString &path : logs) {
        const QFileInfo info(path);
        auto *item = new QListWidgetItem(info.fileName(), m_files);
        item->setData(Qt::UserRole, path);
        item->setToolTip(tr("%1\n%2, %3")
                             .arg(path, Formatting::byteCount(info.size()),
                                  info.lastModified().toString(Qt::ISODate)));
    }
}

} // namespace spotty
