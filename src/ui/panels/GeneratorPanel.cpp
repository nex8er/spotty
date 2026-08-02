/**
 * \file GeneratorPanel.cpp
 * \brief Реализация spotty::GeneratorPanel.
 */
#include "GeneratorPanel.h"

#include "../AppContext.h"

#include <terminal/DataCodec.h>

#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr DataGenerator::Pattern kPatterns[] = {
    DataGenerator::Pattern::Counter,
    DataGenerator::Pattern::Ramp,
    DataGenerator::Pattern::Random,
    DataGenerator::Pattern::Fixed,
    DataGenerator::Pattern::AsciiText,
};

/// \brief Периоды потоковой отправки, та же логарифмическая шкала, что у макросов.
constexpr int kIntervalsMs[] = {1, 5, 10, 25, 50, 100, 250, 500, 1000, 5000};

} // namespace

GeneratorPanel::GeneratorPanel(const AppContext &context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("Generator"), this);
    title->setObjectName(QStringLiteral("panelTitle"));
    layout->addWidget(title);

    auto *form = new QFormLayout;

    m_pattern = new QComboBox(this);
    for (const DataGenerator::Pattern pattern : kPatterns)
        m_pattern->addItem(DataGenerator::patternName(pattern), int(pattern));
    form->addRow(tr("Pattern"), m_pattern);

    m_length = new QSpinBox(this);
    m_length->setRange(1, 65536);
    m_length->setValue(16);
    m_length->setSuffix(tr(" bytes"));
    form->addRow(tr("Length"), m_length);

    m_fixedByte = new QSpinBox(this);
    m_fixedByte->setRange(0, 255);
    m_fixedByte->setValue(0x55);
    m_fixedByte->setDisplayIntegerBase(16);
    m_fixedByte->setPrefix(QStringLiteral("0x"));
    form->addRow(tr("Byte value"), m_fixedByte);

    m_interval = new QComboBox(this);
    for (const int interval : kIntervalsMs)
        m_interval->addItem(tr("%1 ms").arg(interval), interval);
    m_interval->setCurrentIndex(5); // 100 мс
    form->addRow(tr("Stream interval"), m_interval);

    layout->addLayout(form);

    auto *previewTitle = new QLabel(tr("Preview"), this);
    previewTitle->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(previewTitle);

    m_preview = new QPlainTextEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_preview->setMaximumHeight(110);
    layout->addWidget(m_preview);

    m_sendOnce = new QPushButton(tr("Send once"), this);
    m_stream = new QPushButton(tr("Stream"), this);
    m_stream->setCheckable(true);
    m_toSendBar = new QPushButton(tr("To send bar"), this);
    m_toSendBar->setToolTip(tr("Put the generated data into the send bar without sending"));

    auto *buttonRow = new QHBoxLayout;
    buttonRow->setSpacing(4);
    buttonRow->addWidget(m_sendOnce);
    buttonRow->addWidget(m_stream);
    layout->addLayout(buttonRow);
    layout->addWidget(m_toSendBar);

    m_sentLabel = new QLabel(this);
    m_sentLabel->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(m_sentLabel);
    layout->addStretch(1);

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &GeneratorPanel::sendOnce);

    const auto onParameterChanged = [this] {
        applySettingsToGenerator();
        updatePreview();
    };
    connect(m_pattern, &QComboBox::currentIndexChanged, this, [this, onParameterChanged] {
        // Значение байта имеет смысл только для одного вида данных — прячем его, чтобы
        // не создавать ощущение, будто оно влияет на остальные.
        const auto pattern = DataGenerator::Pattern(m_pattern->currentData().toInt());
        m_fixedByte->setEnabled(pattern == DataGenerator::Pattern::Fixed);
        onParameterChanged();
    });
    connect(m_length, &QSpinBox::valueChanged, this, onParameterChanged);
    connect(m_fixedByte, &QSpinBox::valueChanged, this, onParameterChanged);

    connect(m_sendOnce, &QPushButton::clicked, this, &GeneratorPanel::sendOnce);
    connect(m_stream, &QPushButton::toggled, this, &GeneratorPanel::toggleStream);

    connect(m_toSendBar, &QPushButton::clicked, this, [this] {
        // В строку отправки кладём шестнадцатеричную запись: двоичные данные текстом в
        // однострочном поле нечитаемы и частью непечатаемы.
        Q_EMIT pushToSendBarRequested(DataCodec::toHex(m_generator.preview()),
                                      int(DataCodec::Format::Hex));
    });

    m_fixedByte->setEnabled(false);
    applySettingsToGenerator();
    updatePreview();
    setSendEnabled(false);
}

void GeneratorPanel::applySettingsToGenerator()
{
    m_generator.setPattern(DataGenerator::Pattern(m_pattern->currentData().toInt()));
    m_generator.setLength(m_length->value());
    m_generator.setFixedByte(quint8(m_fixedByte->value()));
}

void GeneratorPanel::updatePreview()
{
    const QByteArray data = m_generator.preview();
    m_preview->setPlainText(DataCodec::toHex(data));
}

void GeneratorPanel::sendOnce()
{
    if (!m_sendEnabled) {
        m_stream->setChecked(false);
        return;
    }

    Q_EMIT sendRequested(m_generator.generate());

    ++m_sentPackets;
    m_sentLabel->setText(tr("%n packet(s) sent", nullptr, int(m_sentPackets)));
    updatePreview();
}

void GeneratorPanel::toggleStream(bool on)
{
    if (on && !m_sendEnabled) {
        m_stream->setChecked(false);
        return;
    }

    if (on) {
        m_timer->start(m_interval->currentData().toInt());
        m_stream->setText(tr("Stop"));
    } else {
        m_timer->stop();
        m_stream->setText(tr("Stream"));
    }
}

void GeneratorPanel::setSendEnabled(bool enabled)
{
    m_sendEnabled = enabled;
    m_sendOnce->setEnabled(enabled);
    m_stream->setEnabled(enabled);

    if (!enabled && m_stream->isChecked())
        m_stream->setChecked(false);
}

} // namespace spotty
