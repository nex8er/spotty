/**
 * \file FileSendPanel.cpp
 * \brief Реализация spotty::FileSendPanel.
 */
#include "FileSendPanel.h"

#include <spotty/data/Formatting.h>
#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr auto kKeyPath = "lastPath";
constexpr auto kKeyEncoding = "encoding";
constexpr auto kKeyChunk = "chunkSize";
constexpr auto kKeyEnding = "lineEnding";

constexpr int kDefaultChunk = 512;

/// \brief Как перекодировать файл перед отправкой.
enum Encoding {
    Raw = 0,    ///< Как есть, байт в байт.
    Base64,     ///< Base64 без переносов.
    Base64Wrap, ///< Base64 строками фиксированной ширины.
    Hex,        ///< Шестнадцатеричные пары через пробел.
    HexWrap,    ///< Они же строками по шестнадцать байт.
};

/// \brief Ширина строки base64 при переносе — как в MIME.
constexpr int kBase64LineWidth = 76;

/// \brief Байт в строке для HexWrap. Та же ширина, что у дампа в терминале.
constexpr int kHexBytesPerLine = 16;

} // namespace

FileSendPanel::FileSendPanel(IPanelHost *panelHost, QWidget *parent)
    : PanelWidget(panelHost, parent)
{
    setPanelTitle(tr("Send file"));
    QVBoxLayout *layout = content();

    auto *hint = new QLabel(tr("Sends a file to the interface in chunks, optionally "
                               "encoded as base64."),
                            this);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto *pathRow = new QHBoxLayout;
    pathRow->setSpacing(4);
    m_path = new QLineEdit(this);
    m_path->setPlaceholderText(tr("No file selected"));
    m_browse = new QPushButton(tr("Browse..."), this);
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(m_browse);
    layout->addLayout(pathRow);

    auto *form = new QFormLayout;

    m_encoding = new QComboBox(this);
    m_encoding->addItem(tr("Raw bytes"), Raw);
    m_encoding->addItem(tr("Base64"), Base64);
    m_encoding->addItem(tr("Base64, wrapped"), Base64Wrap);
    m_encoding->addItem(tr("Hex"), Hex);
    m_encoding->addItem(tr("Hex, 16 bytes per line"), HexWrap);
    form->addRow(tr("Encoding"), m_encoding);

    m_chunkSize = new QSpinBox(this);
    m_chunkSize->setRange(1, 65536);
    m_chunkSize->setValue(kDefaultChunk);
    m_chunkSize->setSuffix(tr(" bytes"));
    form->addRow(tr("Chunk"), m_chunkSize);

    m_lineEnding = new QLineEdit(this);
    m_lineEnding->setPlaceholderText(QStringLiteral("\\r\\n"));
    m_lineEnding->setToolTip(tr("Appended after the whole payload. Escapes: \\r \\n \\t"));
    form->addRow(tr("Terminator"), m_lineEnding);

    layout->addLayout(form);

    m_send = new QPushButton(this);
    layout->addWidget(m_send);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    layout->addWidget(m_progress);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("hintLabel"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    layout->addStretch(1);

    connect(m_browse, &QPushButton::clicked, this, &FileSendPanel::chooseFile);
    connect(m_send, &QPushButton::clicked, this, &FileSendPanel::toggleSending);
    connect(m_path, &QLineEdit::textChanged, this, [this] {
        host()->setValue(QLatin1String(kKeyPath), m_path->text());
        updateUi();
    });
    connect(m_encoding, &QComboBox::currentIndexChanged, this, [this] {
        host()->setValue(QLatin1String(kKeyEncoding), m_encoding->currentData().toInt());
    });
    connect(m_chunkSize, &QSpinBox::valueChanged, this, [this](int value) {
        host()->setValue(QLatin1String(kKeyChunk), value);
    });
    connect(m_lineEnding, &QLineEdit::textChanged, this, [this](const QString &text) {
        host()->setValue(QLatin1String(kKeyEnding), text);
    });

    // Подтверждение порции. Другого обратного давления API не даёт, и это его слабое
    // место: подтверждается запись в канал, а не приём устройством.
    connect(host(), &IPanelHost::dataLogged, this,
            [this](const QByteArray &data, DataDirection direction) {
                if (!m_sending || direction != DataDirection::Tx)
                    return;
                m_sent += data.size();
                sendNextChunk();
            });

    m_path->setText(host()->value(QLatin1String(kKeyPath)).toString());
    const int encoding = host()->value(QLatin1String(kKeyEncoding), Base64).toInt();
    m_encoding->setCurrentIndex(qMax(0, m_encoding->findData(encoding)));
    m_chunkSize->setValue(host()->value(QLatin1String(kKeyChunk), kDefaultChunk).toInt());
    m_lineEnding->setText(host()->value(QLatin1String(kKeyEnding)).toString());

    updateUi();
}

void FileSendPanel::chooseFile()
{
    // Модальный диалог с окном в родителях — законное обращение панели к window().
    // Запрещено ей другое: вешать на окно сочетания клавиш и фильтры событий.
    const QString chosen = QFileDialog::getOpenFileName(
        window(), tr("Send file"),
        m_path->text().isEmpty() ? host()->documentsDir() : m_path->text());
    if (!chosen.isEmpty())
        m_path->setText(chosen);
}

bool FileSendPanel::prepare()
{
    QFile file(m_path->text());
    if (!file.open(QIODevice::ReadOnly)) {
        m_status->setText(tr("Cannot open the file: %1").arg(file.errorString()));
        return false;
    }

    const QByteArray raw = file.readAll();
    switch (m_encoding->currentData().toInt()) {
    case Raw:
        m_payload = raw;
        break;
    case Base64:
        m_payload = raw.toBase64();
        break;
    case Base64Wrap: {
        const QByteArray encoded = raw.toBase64();
        m_payload.clear();
        m_payload.reserve(encoded.size() + encoded.size() / kBase64LineWidth * 2);
        for (qsizetype i = 0; i < encoded.size(); i += kBase64LineWidth) {
            m_payload += encoded.mid(i, kBase64LineWidth);
            m_payload += "\r\n";
        }
        break;
    }

    case Hex:
        // Пары через пробел, в верхнем регистре: так их принимает большинство загрузчиков
        // и так же показывает HEX-дамп самого Spotty — одно и то же представление в обе
        // стороны легче сверять глазами.
        m_payload = raw.toHex(' ').toUpper();
        break;

    case HexWrap: {
        // Шестнадцать байт в строке — та же ширина, что у дампа в терминале. Совпадение
        // не случайно: отправленное и принятое кладут рядом и сверяют построчно.
        const QByteArray hex = raw.toHex(' ').toUpper();
        m_payload.clear();
        m_payload.reserve(hex.size() + hex.size() / (kHexBytesPerLine * 3) * 2);
        for (qsizetype byte = 0; byte < raw.size(); byte += kHexBytesPerLine) {
            const qsizetype count = qMin<qsizetype>(kHexBytesPerLine, raw.size() - byte);
            // Каждый байт занимает три знака («AB »), у последнего в строке пробела нет.
            m_payload += hex.mid(byte * 3, count * 3 - 1);
            m_payload += "\r\n";
        }
        break;
    }
    default:
        m_payload = raw;
        break;
    }

    QString ending = m_lineEnding->text();
    ending.replace(QLatin1String("\\r"), QLatin1String("\r"));
    ending.replace(QLatin1String("\\n"), QLatin1String("\n"));
    ending.replace(QLatin1String("\\t"), QLatin1String("\t"));
    m_payload += ending.toUtf8();

    if (m_payload.isEmpty()) {
        m_status->setText(tr("The file is empty."));
        return false;
    }

    m_sent = 0;
    m_chunk = m_chunkSize->value();
    return true;
}

void FileSendPanel::toggleSending()
{
    if (m_sending) {
        finish(tr("Cancelled after %1.").arg(Formatting::byteCount(m_sent)));
        return;
    }

    if (host()->channelState() != ChannelState::Open) {
        m_status->setText(tr("Open the interface first."));
        return;
    }
    if (!prepare())
        return;

    m_sending = true;
    m_progress->setValue(0);
    updateUi();

    host()->appendToTerminal(tr("Sending %1 (%2)")
                                 .arg(QFileInfo(m_path->text()).fileName(),
                                      Formatting::byteCount(m_payload.size())));
    sendNextChunk();
}

void FileSendPanel::sendNextChunk()
{
    if (!m_sending)
        return;

    if (m_sent >= m_payload.size()) {
        finish(tr("Sent %1.").arg(Formatting::byteCount(m_payload.size())));
        host()->appendToTerminal(tr("File sent."));
        return;
    }

    m_progress->setValue(int(100 * m_sent / m_payload.size()));
    m_status->setText(tr("%1 of %2")
                          .arg(Formatting::byteCount(m_sent),
                               Formatting::byteCount(m_payload.size())));

    host()->send(m_payload.mid(m_sent, m_chunk));
}

void FileSendPanel::finish(const QString &message)
{
    m_sending = false;
    m_payload.clear();
    m_progress->setValue(0);
    m_status->setText(message);
    updateUi();
}

void FileSendPanel::channelStateChanged(ChannelState state)
{
    if (m_sending && state != ChannelState::Open) {
        // Канал закрылся посреди передачи. Молча остановиться нельзя: пользователь ждёт,
        // что файл ушёл целиком, а ушла его часть.
        finish(tr("The interface closed after %1 — the file was sent only partially.")
                   .arg(Formatting::byteCount(m_sent)));
        host()->showStatusMessage(tr("File transfer interrupted."));
        return;
    }
    updateUi();
}

void FileSendPanel::themeChanged()
{
    updateUi();
}

void FileSendPanel::aboutToClose()
{
    if (m_sending)
        finish(QString());
}

void FileSendPanel::updateUi()
{
    const bool open = host()->channelState() == ChannelState::Open;
    const bool hasFile = !m_path->text().isEmpty();

    m_send->setText(m_sending ? tr("Cancel") : tr("Send"));
    m_send->setIcon(host()->icon(m_sending ? mdi::Stop : mdi::Send, 16));
    m_send->setEnabled(m_sending || (open && hasFile));

    m_browse->setEnabled(!m_sending);
    m_encoding->setEnabled(!m_sending);
    m_chunkSize->setEnabled(!m_sending);
    m_lineEnding->setEnabled(!m_sending);
    m_path->setReadOnly(m_sending);

    if (!m_sending && !open)
        m_status->setText(tr("The interface is not open."));
}

} // namespace spotty
