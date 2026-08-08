/**
 * \file FileSendPanel.h
 * \brief Панель отправки файла в интерфейс.
 */
#pragma once

#include <spotty/data/DataCodec.h>
#include <spotty/ui/PanelWidget.h>

#include <QByteArray>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;

namespace spotty {

/**
 * \class FileSendPanel
 * \brief Отправляет файл порциями, при желании перекодировав его в base64.
 *
 * \par Темп отправки
 *
 * spotty::IPanelHost::send() не блокирует: байты кладутся в очередь потока ввода-вывода.
 * Мегабайт, посланный одним вызовом, вырастил бы очередь на весь объём, а окно перестало
 * бы отвечать — при этом ни одной ошибки не произошло бы, и причину пришлось бы искать.
 *
 * Поэтому панель шлёт по одной порции и ждёт её подтверждения в dataLogged() с
 * DataDirection::Tx, и только потом берётся за следующую. Это единственный вид обратного
 * давления, который даёт API: настоящего «сколько байт ещё в очереди» в нём нет.
 */
class FileSendPanel : public PanelWidget
{
    Q_OBJECT

public:
    explicit FileSendPanel(IPanelHost *host, QWidget *parent = nullptr);

protected:
    /// \brief Обрыв связи посреди передачи останавливает её.
    void channelStateChanged(ChannelState state) override;

    void themeChanged() override;
    void aboutToClose() override;

private:
    void chooseFile();

    /// \brief Начать или прервать передачу.
    void toggleSending();

    /// \brief Прочитать файл, закодировать и разбить на порции.
    bool prepare();

    /// \brief Отправить очередную порцию.
    void sendNextChunk();

    void finish(const QString &message);
    void updateUi();

    QLineEdit *m_path = nullptr;
    QPushButton *m_browse = nullptr;
    QComboBox *m_encoding = nullptr;
    QSpinBox *m_chunkSize = nullptr;
    QLineEdit *m_lineEnding = nullptr;
    QPushButton *m_send = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;

    /// \brief Полностью подготовленные к отправке байты.
    QByteArray m_payload;

    /// \brief Сколько из #m_payload уже подтверждено.
    qint64 m_sent = 0;

    /// \brief Размер порции, зафиксированный на время передачи.
    int m_chunk = 0;

    bool m_sending = false;
};

} // namespace spotty
