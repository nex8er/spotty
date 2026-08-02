/**
 * \file LoggingPanel.h
 * \brief Панель логирования: запись, список файлов, просмотр и перетаскивание.
 */
#pragma once

#include <LogWriter.h>

// Полный заголовок, а не форвард-объявление: LogFileList наследуется от QListWidget.
#include <QListWidget>
#include <QWidget>

class QCheckBox;
class QLabel;
class QMimeData;
class QToolButton;

namespace spotty {

struct AppContext;

/**
 * \class LogFileList
 * \brief Список файлов логов с перетаскиванием и копированием самих файлов.
 *
 * Отдельный класс, потому что перетаскивание и копирование в буфер обмена должны отдавать
 * **файл**, а не его имя. Пользователь ожидает, что перетащенная из панели строка окажется
 * в почте вложением, а Ctrl+C позволит вставить файл в проводнике — для этого нужен
 * `text/uri-list` в данных обмена.
 */
class LogFileList : public QListWidget
{
    Q_OBJECT

public:
    explicit LogFileList(QWidget *parent = nullptr);

    /// \brief Скопировать выбранные файлы в буфер обмена как файлы.
    void copySelectedFiles();

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    /// \brief Данные обмена с путями выбранных файлов.
    QMimeData *mimeDataForSelection() const;
};

/**
 * \class LoggingPanel
 * \brief Управление записью лога и просмотр записанного.
 *
 * \par Что здесь есть
 *
 * - Кнопка записи с именем текущего файла и объёмом записанного.
 * - Флажок фильтрации управляющих последовательностей ANSI.
 * - Список последних логов: щелчок открывает файл прямо в терминале.
 * - Перетаскивание файлов наружу и копирование их в буфер обмена.
 */
class LoggingPanel : public QWidget
{
    Q_OBJECT

public:
    explicit LoggingPanel(const AppContext &context, QWidget *parent = nullptr);

    /// \brief Обновить доступность записи по состоянию канала.
    void setInterfaceOpen(bool open);

    /// \brief Прекратить запись, если она идёт.
    void stopRecording();

    bool isRecording() const;

Q_SIGNALS:
    /// \brief Пользователь просит показать файл лога в терминале.
    void logFileRequested(const QString &filePath);

    /// \brief Сообщение для строки состояния.
    void statusMessage(const QString &message);

private:
    void toggleRecording();
    void refreshFileList();
    void updateRecordingUi();

    const AppContext &m_context;
    LogWriter m_writer;

    QToolButton *m_recordButton = nullptr;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QCheckBox *m_filterAnsi = nullptr;
    QCheckBox *m_includeTx = nullptr;
    LogFileList *m_files = nullptr;

    bool m_interfaceOpen = false;
};

} // namespace spotty
