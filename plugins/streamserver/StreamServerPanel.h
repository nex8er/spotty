/**
 * \file StreamServerPanel.h
 * \brief Панель управления сервером потока.
 */
#pragma once

#include "StreamServer.h"

#include <spotty/api/DataDirection.h>
#include <spotty/ui/PanelWidget.h>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace spotty {

/**
 * \class StreamServerPanel
 * \brief Включает и настраивает spotty::StreamServer и соединяет его с интерфейсом.
 *
 * \par Что куда идёт
 *
 * Принятое от устройства (а по желанию и отправленное) раздаётся клиентам как есть; всё,
 * что клиенты пишут, уходит в интерфейс обычной отправкой — так же, как набранное в строке
 * отправки, но без терминации и перекодировки: клиент отвечает за свои байты сам.
 *
 * \par Живёт, пока открыто приложение
 *
 * Панели создаются при запуске независимо от того, показаны ли они, поэтому сервер работает
 * и тогда, когда вкладка не выбрана.
 *
 * \warning Авторизации нет. При прослушивании всех интерфейсов любой, кто достанет порт по
 *          сети, читает устройство и пишет в него; панель говорит об этом прямо.
 */
class StreamServerPanel : public PanelWidget
{
    Q_OBJECT

public:
    explicit StreamServerPanel(IPanelHost *panelHost, QWidget *parent = nullptr);

    /// \brief Сервер панели — для тестов.
    StreamServer *server() { return &m_server; }

    /// \brief Запустить сервер с текущими настройками; при отказе показывает причину.
    bool startServer();

    void stopServer();

protected:
    void aboutToClose() override;

private:
    /// \brief Принять данные потока от Spotty и раздать клиентам.
    void onDataLogged(const QByteArray &data, DataDirection direction);

    /// \brief Байты клиентов — в интерфейс.
    void onDataFromClients(const QByteArray &data);

    /// \brief Показать то, что зависит от выбранного способа: поля и предупреждение.
    void updateVisibility();

    /// \brief Строка состояния и доступность полей.
    void updateStatus();

    void saveSettings();

    StreamServer m_server;

    QComboBox *m_kind = nullptr;
    QLineEdit *m_localName = nullptr;
    QComboBox *m_bind = nullptr;
    QSpinBox *m_port = nullptr;
    QCheckBox *m_includeTx = nullptr;
    QCheckBox *m_autoStart = nullptr;
    QPushButton *m_toggle = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_warning = nullptr;
    QFormLayout *m_form = nullptr;
    int m_localRow = -1;
    int m_bindRow = -1;
    int m_portRow = -1;

    /// \brief Обновляет счётчики, пока сервер слушает.
    QTimer *m_statusTimer = nullptr;

    /// \brief Причина последнего неудачного запуска; пусто, если запуск удался или не пробовали.
    ///
    /// Хранится отдельно, а не узнаётся по тексту строки состояния: текст переводится, и
    /// сравнение с ним сломалось бы на любом языке, кроме исходного.
    QString m_startError;

    /// \brief Сколько байт от клиентов отброшено из-за закрытого интерфейса.
    qint64 m_droppedBytes = 0;
};

} // namespace spotty
