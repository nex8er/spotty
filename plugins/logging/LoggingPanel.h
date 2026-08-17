/**
 * \file LoggingPanel.h
 * \brief Панель записи вывода в файл.
 */
#pragma once

#include <spotty/data/LogWriter.h>
#include <spotty/ui/PanelWidget.h>

#include <QListWidget>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QTimer;
class QLabel;
class QMimeData;
class QMouseEvent;
class QToolButton;

namespace spotty {

/**
 * \class LogFileList
 * \brief Список файлов логов, из которого файл можно вытащить наружу.
 *
 * Обычный QListWidget отдал бы при перетаскивании текст. Нужен `text/uri-list`: только с
 * ним система понимает, что переносится файл, и почтовый клиент делает из него вложение,
 * а проводник — копию.
 */
class LogFileList : public QListWidget
{
    Q_OBJECT

public:
    explicit LogFileList(QWidget *parent = nullptr);

    /// \brief Положить выделенные файлы в буфер обмена.
    void copySelectedFiles();

protected:
    void startDrag(Qt::DropActions supportedActions) override;
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    /// \return Данные для переноса или `nullptr`, если выделять нечего.
    QMimeData *mimeDataForSelection() const;

    /**
     * \brief Вернуть групповое выделение, если базовый класс его схлопнул.
     * \param event Событие мыши, уже обработанное базовым классом.
     * \param before Выделение непосредственно перед вызовом базового обработчика.
     *
     * Общий код для mousePressEvent() и mouseReleaseEvent() — щелчок правой кнопкой по
     * уже выделенной группе не должен сужать её до одного пункта, а Qt делает это на одном
     * из двух событий в зависимости от платформы и версии.
     */
    void restoreGroupSelectionAfterRightClick(QMouseEvent *event,
                                              const QList<QListWidgetItem *> &before);
};

/**
 * \class LoggingPanel
 * \brief Управление записью вывода в файл и список записанного.
 *
 * \par Единственная панель, читающая поток
 *
 * Подписывается на сырые байты обоих направлений и отдаёт их spotty::LogWriter. Именно
 * сырые: журнал должен содержать то, что было на проводе, а не то, что из этого сделали
 * разбор ANSI и пакетизация.
 */
class LoggingPanel : public PanelWidget
{
    Q_OBJECT

public:
    explicit LoggingPanel(IPanelHost *host, QWidget *parent = nullptr);

protected:
    void themeChanged() override;

    /// \brief Закрытие канала завершает запись, открытие — может её начать.
    void channelStateChanged(ChannelState state) override;

    void settingsReset() override;

    /// \brief Окно закрывается: дописать и закрыть файл, а не бросить его на произвол.
    void aboutToClose() override;

private:
    void toggleRecording();
    void updateRecordingUi();
    void refreshFileList();

    /// \brief Прочитать своё из настроек и раздать записывателю.
    void reloadFromSettings();

    /// \brief Обновить бегущие точки в надписи «идёт запись».
    void updateRecordingCaption();

    /// \brief Меню по правой кнопке над списком файлов.
    void showFileMenu(const QPoint &position);

    /// \brief Обновить строку об общем объёме логов на диске.
    void updateDiskUsage();

    /// \brief Удалить все выделенные файлы после подтверждения.
    void deleteSelectedFiles();

    /// \return Пути к выделенным файлам.
    QStringList selectedFilePaths() const;

    /// \return Путь к текущему файлу либо пустая строка.
    QString selectedFilePath() const;

    LogWriter m_writer;

    QToolButton *m_recordButton = nullptr;
    QLabel *m_captionLabel = nullptr;  ///< «Начать запись» / «Идёт запись…».
    QComboBox *m_csvMode = nullptr;    ///< Что делать с телеметрией.
    QLabel *m_usageLabel = nullptr;    ///< Общий объём логов на диске.

    /// \brief Тикает, пока идёт запись; двигает точки в надписи.
    QTimer *m_captionTimer = nullptr;
    int m_captionPhase = 0;
    QLabel *m_fileLabel = nullptr;
    QLabel *m_sizeLabel = nullptr;
    QCheckBox *m_filterAnsi = nullptr;
    QCheckBox *m_includeTx = nullptr;
    LogFileList *m_files = nullptr;

    bool m_interfaceOpen = false;
    bool m_autoStart = false;
};

} // namespace spotty
