/**
 * \file LoggingPanel.cpp
 * \brief Реализация spotty::LoggingPanel и spotty::LogFileList.
 */
#include "LoggingPanel.h"

#include <spotty/data/Formatting.h>

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDrag>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr int kToolGlyphSize = 18;

// Ключи без префикса: пространство `plugins/logging/` подставляет хост.
constexpr auto kKeyDirectory = "directory";
constexpr auto kKeyTemplate = "fileNameTemplate";
constexpr auto kKeyFilterAnsi = "filterAnsi";
constexpr auto kKeyIncludeTx = "includeTx";
constexpr auto kKeyAutoStart = "autoStart";

constexpr auto kDefaultTemplate = "{alias}_{date}_{time}";

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

LoggingPanel::LoggingPanel(IPanelHost *panelHost, QWidget *parent)
    : PanelWidget(panelHost, parent)
{
    setPanelTitle(tr("Logging"));
    QVBoxLayout *layout = content();

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

    m_includeTx = new QCheckBox(tr("Include sent data"), this);

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

    // --- Связывание ------------------------------------------------------------------

    connect(m_recordButton, &QToolButton::clicked, this, &LoggingPanel::toggleRecording);

    // Флажки дублируют поля диалога настроек, поэтому правка отсюда пишется в те же
    // ключи: иначе панель и диалог показывали бы разное.
    const auto onOptionToggled = [this] {
        m_writer.setFilterAnsi(m_filterAnsi->isChecked());
        m_writer.setIncludeTx(m_includeTx->isChecked());
        host()->setValue(QLatin1String(kKeyFilterAnsi), m_filterAnsi->isChecked());
        host()->setValue(QLatin1String(kKeyIncludeTx), m_includeTx->isChecked());
    };
    connect(m_filterAnsi, &QCheckBox::toggled, this, onOptionToggled);
    connect(m_includeTx, &QCheckBox::toggled, this, onOptionToggled);

    connect(m_files, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        host()->showDocument(item->data(Qt::UserRole).toString(), item->text());
    });

    // Сырые байты обоих направлений — то, что было на проводе.
    connect(host(), &IPanelHost::dataLogged, &m_writer, &LogWriter::write);

    connect(&m_writer, &LogWriter::bytesWrittenChanged, this, [this](qint64 bytes) {
        m_sizeLabel->setText(Formatting::byteCount(bytes));
    });
    connect(&m_writer, &LogWriter::recordingStarted, this, [this](const QString &path) {
        host()->showStatusMessage(tr("Recording to %1").arg(QFileInfo(path).fileName()));
        updateRecordingUi();
    });
    connect(&m_writer, &LogWriter::recordingStopped, this, [this] {
        refreshFileList();
        updateRecordingUi();
    });
    connect(&m_writer, &LogWriter::errorOccurred, this, [this](const QString &message) {
        host()->showStatusMessage(message);
        updateRecordingUi();
    });

    host()->setShortcuts({PanelShortcut{
        .id = QStringLiteral("toggle"),
        .title = tr("Start / stop recording"),
        .defaultSequence = QKeySequence(QStringLiteral("Ctrl+R")),
    }});
    connect(host(), &IPanelHost::shortcutActivated, this, [this](const QString &id) {
        if (!id.endsWith(QLatin1String(".toggle")))
            return;
        host()->activatePanel(QStringLiteral("logging"));
        m_recordButton->click();
    });

    reloadFromSettings();
    updateRecordingUi();
}

void LoggingPanel::reloadFromSettings()
{
    const QString directory = host()->value(QLatin1String(kKeyDirectory)).toString();
    // Пусто — значит умолчание. Разворачивать его при записи настройки нельзя: тогда
    // переносной комплект унёс бы с собой абсолютный путь чужой машины.
    m_writer.setDirectory(directory.isEmpty() ? host()->documentsDir() : directory);
    m_writer.setFileNameTemplate(
        host()->value(QLatin1String(kKeyTemplate), QLatin1String(kDefaultTemplate)).toString());

    const bool filterAnsi = host()->value(QLatin1String(kKeyFilterAnsi), true).toBool();
    const bool includeTx = host()->value(QLatin1String(kKeyIncludeTx), true).toBool();
    m_writer.setFilterAnsi(filterAnsi);
    m_writer.setIncludeTx(includeTx);
    m_autoStart = host()->value(QLatin1String(kKeyAutoStart), false).toBool();

    // Флажки приводим в согласие с настройками, не поднимая их обработчики: иначе
    // установка состояния тут же перезаписала бы настройку значением флажка.
    const QSignalBlocker filterBlocker(m_filterAnsi);
    const QSignalBlocker txBlocker(m_includeTx);
    m_filterAnsi->setChecked(filterAnsi);
    m_includeTx->setChecked(includeTx);

    refreshFileList();
}

void LoggingPanel::themeChanged()
{
    updateRecordingUi();
}

void LoggingPanel::settingsReset()
{
    reloadFromSettings();
}

void LoggingPanel::aboutToClose()
{
    m_writer.stop();
}

void LoggingPanel::toggleRecording()
{
    if (m_writer.isRecording()) {
        m_writer.stop();
        return;
    }

    if (host()->interfaceId().isEmpty()) {
        m_recordButton->setChecked(false);
        host()->showStatusMessage(tr("Select an interface before recording."));
        return;
    }

    if (!m_writer.start(host()->interfaceName(), host()->interfaceAlias()))
        m_recordButton->setChecked(false);
}

void LoggingPanel::updateRecordingUi()
{
    const bool recording = m_writer.isRecording();

    m_recordButton->setChecked(recording);
    m_recordButton->setIcon(host()->icon(recording ? mdi::Stop : mdi::Record,
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

void LoggingPanel::channelStateChanged(ChannelState state)
{
    const bool open = state == ChannelState::Open;
    const bool wasOpen = m_interfaceOpen;
    m_interfaceOpen = open;

    // Закрытие канала завершает запись: продолжать её означало бы держать открытым файл,
    // в который уже ничего не придёт.
    if (!open && m_writer.isRecording())
        m_writer.stop();

    updateRecordingUi();

    // Автозапуск именно на переходе в открытое состояние, а не на каждом вызове: иначе
    // повторное сообщение о том же состоянии начинало бы новый файл.
    if (open && !wasOpen && m_autoStart && !m_writer.isRecording())
        toggleRecording();
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
