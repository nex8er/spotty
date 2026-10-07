/**
 * \file StreamServerPanel.cpp
 * \brief Реализация spotty::StreamServerPanel.
 */
#include "StreamServerPanel.h"

#include <spotty/data/Formatting.h>
#include <spotty/ui/IPanelHost.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr auto kKeyKind = "kind";
constexpr auto kKeyLocalName = "localName";
constexpr auto kKeyAllInterfaces = "allInterfaces";
constexpr auto kKeyPort = "port";
constexpr auto kKeyIncludeTx = "includeTx";
constexpr auto kKeyAutoStart = "autoStart";

constexpr int kDefaultPort = 7777;

/// \brief Период обновления счётчиков в строке состояния, мс.
constexpr int kStatusRefreshMs = 500;

/// \brief Имя сокета по умолчанию: путь на Unix, имя канала на Windows.
QString defaultLocalName()
{
#ifdef Q_OS_WIN
    return QStringLiteral("spotty");
#else
    return QStringLiteral("/tmp/spotty.sock");
#endif
}

} // namespace

StreamServerPanel::StreamServerPanel(IPanelHost *panelHost, QWidget *parent)
    : PanelWidget(panelHost, parent)
{
    setPanelTitle(tr("Stream server"));
    QVBoxLayout *layout = content();

    m_form = new QFormLayout;

    m_kind = new QComboBox(this);
    m_kind->addItem(tr("Socket"), int(StreamServer::Kind::Local));
    m_kind->addItem(tr("TCP"), int(StreamServer::Kind::Tcp));
    m_form->addRow(tr("Transport"), m_kind);

    m_localName = new QLineEdit(this);
    m_localName->setPlaceholderText(defaultLocalName());
    m_form->addRow(tr("Path"), m_localName);
    m_localRow = m_form->rowCount() - 1;

    m_bind = new QComboBox(this);
    m_bind->addItem(tr("This computer only"), false);
    m_bind->addItem(tr("All interfaces (remote access)"), true);
    m_form->addRow(tr("Listen on"), m_bind);
    m_bindRow = m_form->rowCount() - 1;

    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(kDefaultPort);
    m_form->addRow(tr("Port"), m_port);
    m_portRow = m_form->rowCount() - 1;

    layout->addLayout(m_form);

    m_includeTx = new QCheckBox(tr("Pass on sent data"), this);
    layout->addWidget(m_includeTx);

    m_autoStart = new QCheckBox(tr("Start when Spotty starts"), this);
    layout->addWidget(m_autoStart);

    m_toggle = new QPushButton(this);
    layout->addWidget(m_toggle);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_status);

    m_warning = new QLabel(tr("No authentication: the port is open to everyone on the network."),
                           this);
    m_warning->setObjectName(QStringLiteral("hintLabel"));
    m_warning->setWordWrap(true);
    layout->addWidget(m_warning);

    layout->addStretch(1);

    // --- Настройки -------------------------------------------------------------------

    const int kind = host()->value(QLatin1String(kKeyKind), int(StreamServer::Kind::Local)).toInt();
    m_kind->setCurrentIndex(qMax(0, m_kind->findData(kind)));
    m_localName->setText(host()->value(QLatin1String(kKeyLocalName), defaultLocalName()).toString());
    m_bind->setCurrentIndex(host()->value(QLatin1String(kKeyAllInterfaces), false).toBool() ? 1 : 0);
    m_port->setValue(host()->value(QLatin1String(kKeyPort), kDefaultPort).toInt());
    m_includeTx->setChecked(host()->value(QLatin1String(kKeyIncludeTx), false).toBool());
    m_autoStart->setChecked(host()->value(QLatin1String(kKeyAutoStart), false).toBool());

    // --- Связывание ------------------------------------------------------------------

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(kStatusRefreshMs);
    connect(m_statusTimer, &QTimer::timeout, this, &StreamServerPanel::updateStatus);

    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
        updateVisibility();
        saveSettings();
    });
    connect(m_bind, &QComboBox::currentIndexChanged, this, [this] {
        updateVisibility();
        saveSettings();
    });
    connect(m_localName, &QLineEdit::editingFinished, this, &StreamServerPanel::saveSettings);
    connect(m_port, &QSpinBox::editingFinished, this, &StreamServerPanel::saveSettings);
    connect(m_includeTx, &QCheckBox::toggled, this, &StreamServerPanel::saveSettings);
    connect(m_autoStart, &QCheckBox::toggled, this, &StreamServerPanel::saveSettings);

    connect(m_toggle, &QPushButton::clicked, this, [this] {
        if (m_server.isListening())
            stopServer();
        else
            startServer();
    });

    connect(host(), &IPanelHost::dataLogged, this, &StreamServerPanel::onDataLogged);
    connect(&m_server, &StreamServer::dataFromClients, this,
            &StreamServerPanel::onDataFromClients);
    connect(&m_server, &StreamServer::clientCountChanged, this, [this] { updateStatus(); });
    connect(&m_server, &StreamServer::clientDropped, this,
            [this](const QString &message) { host()->showStatusMessage(message); });

    updateVisibility();
    updateStatus();

    if (m_autoStart->isChecked())
        startServer();
}

bool StreamServerPanel::startServer()
{
    StreamServer::Config config;
    config.kind = StreamServer::Kind(m_kind->currentData().toInt());
    config.localName = m_localName->text().trimmed();
    if (config.localName.isEmpty())
        config.localName = defaultLocalName();
    config.address = m_bind->currentData().toBool() ? QHostAddress(QHostAddress::Any)
                                                    : QHostAddress(QHostAddress::LocalHost);
    config.port = quint16(m_port->value());

    m_droppedBytes = 0;
    QString error;
    if (!m_server.start(config, &error)) {
        m_startError = error;
        host()->showStatusMessage(tr("Stream server: %1").arg(error));
        updateStatus();
        return false;
    }
    m_startError.clear();

    m_statusTimer->start();
    updateStatus();
    return true;
}

void StreamServerPanel::stopServer()
{
    m_startError.clear();
    m_server.stop();
    m_statusTimer->stop();
    updateStatus();
}

void StreamServerPanel::aboutToClose()
{
    // Файл сокета убирается при закрытии сервера; оставленный после выхода, он вынуждал бы
    // следующий запуск гадать, жив ли его владелец.
    stopServer();
}

void StreamServerPanel::onDataLogged(const QByteArray &data, DataDirection direction)
{
    if (!m_server.isListening())
        return;

    // Приём идёт всегда; отправка — только если попросили: иначе клиент, написавший в порт,
    // получал бы собственные байты обратно как будто их прислало устройство.
    if (direction == DataDirection::Rx
        || (direction == DataDirection::Tx && m_includeTx->isChecked())) {
        m_server.broadcast(data);
    }
}

void StreamServerPanel::onDataFromClients(const QByteArray &data)
{
    // В закрытый интерфейс не отправляем: сессия ответила бы ошибкой на каждую порцию, а
    // скрипт, который пишет без остановки, завалил бы строку состояния сообщениями.
    if (host()->channelState() != ChannelState::Open) {
        m_droppedBytes += data.size();
        return;
    }
    host()->send(data);
}

void StreamServerPanel::updateVisibility()
{
    const bool local = StreamServer::Kind(m_kind->currentData().toInt())
                       == StreamServer::Kind::Local;
    m_form->setRowVisible(m_localRow, local);
    m_form->setRowVisible(m_bindRow, !local);
    m_form->setRowVisible(m_portRow, !local);

    // Предупреждение только там, где доступ действительно виден сети.
    m_warning->setVisible(!local && m_bind->currentData().toBool());
}

void StreamServerPanel::updateStatus()
{
    const bool listening = m_server.isListening();

    // Пока сервер слушает, настройки менять нельзя: изменённое поле ничего не делало бы
    // до следующего запуска, и по экрану этого не заметить.
    m_kind->setEnabled(!listening);
    m_localName->setEnabled(!listening);
    m_bind->setEnabled(!listening);
    m_port->setEnabled(!listening);
    m_toggle->setText(listening ? tr("Stop") : tr("Start"));

    if (!listening) {
        // Причину отказа запуска оставляем на экране, пока человек не нажмёт «Start» снова.
        m_status->setText(m_startError.isEmpty() ? tr("Stopped.")
                                                 : tr("Cannot start: %1").arg(m_startError));
        return;
    }

    QString text = tr("Listening on %1.").arg(m_server.listenDescription());
    if (m_server.serverPort() != 0 && m_bind->currentData().toBool()) {
        const QStringList addresses = StreamServer::reachableAddresses();
        if (!addresses.isEmpty()) {
            text += QLatin1Char(' ') + tr("Connect to: %1.")
                                           .arg(addresses.join(QStringLiteral(", ")));
        }
    }
    text += QLatin1Char('\n')
            + tr("Clients: %1 · to clients %2 · from clients %3")
                  .arg(m_server.clientCount())
                  .arg(Formatting::byteCount(m_server.bytesToClients()),
                       Formatting::byteCount(m_server.bytesFromClients()));
    if (m_droppedBytes > 0) {
        text += QLatin1Char('\n')
                + tr("Dropped %1: the interface is not open.")
                      .arg(Formatting::byteCount(m_droppedBytes));
    }
    m_status->setText(text);
}

void StreamServerPanel::saveSettings()
{
    host()->setValue(QLatin1String(kKeyKind), m_kind->currentData().toInt());
    host()->setValue(QLatin1String(kKeyLocalName), m_localName->text().trimmed());
    host()->setValue(QLatin1String(kKeyAllInterfaces), m_bind->currentData().toBool());
    host()->setValue(QLatin1String(kKeyPort), m_port->value());
    host()->setValue(QLatin1String(kKeyIncludeTx), m_includeTx->isChecked());
    host()->setValue(QLatin1String(kKeyAutoStart), m_autoStart->isChecked());
}

} // namespace spotty
