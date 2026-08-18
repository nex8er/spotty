/**
 * \file test_generator_panel.cpp
 * \brief Тесты проводки органов управления панели генератора.
 */
#include "GeneratorPanel.h"

#include "support/FakePanelHost.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QToolButton>

using namespace spotty;

namespace {

class OpenHost final : public test::FakePanelHost
{
public:
    using FakePanelHost::FakePanelHost;

    ChannelState channelState() const override { return ChannelState::Open; }
    QString interfaceId() const override { return QStringLiteral("generator-test"); }

    void send(const QByteArray &data) override { sent.append(data); }

    QList<QByteArray> sent;
};

} // namespace

TEST(GeneratorPanel, SignalControlsDriveTheGeneratedPacket)
{
    test::TempDir dir;
    OpenHost host(dir.path());
    GeneratorPanel panel(&host);
    Q_EMIT host.channelStateChanged(ChannelState::Open);

    auto *mode = panel.findChild<QComboBox *>(QStringLiteral("generatorMode"));
    auto *waveform = panel.findChild<QComboBox *>(QStringLiteral("waveform"));
    auto *prefix = panel.findChild<QLineEdit *>(QStringLiteral("prefix"));
    auto *amplitude = panel.findChild<QDoubleSpinBox *>(QStringLiteral("amplitude"));
    auto *offset = panel.findChild<QDoubleSpinBox *>(QStringLiteral("offset"));
    auto *duty = panel.findChild<QDoubleSpinBox *>(QStringLiteral("dutyCycle"));
    auto *precision = panel.findChild<QSpinBox *>(QStringLiteral("wavePrecision"));
    auto *period = panel.findChild<QSpinBox *>(QStringLiteral("wavePeriod"));
    auto *hex = panel.findChild<QToolButton *>(QStringLiteral("previewHex"));
    auto *playPause = panel.findChild<QToolButton *>(QStringLiteral("playPause"));
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(waveform, nullptr);
    ASSERT_NE(prefix, nullptr);
    ASSERT_NE(amplitude, nullptr);
    ASSERT_NE(offset, nullptr);
    ASSERT_NE(duty, nullptr);
    ASSERT_NE(precision, nullptr);
    ASSERT_NE(period, nullptr);
    ASSERT_NE(hex, nullptr);
    ASSERT_NE(playPause, nullptr);

    mode->setCurrentIndex(mode->findData(-1));
    waveform->setCurrentIndex(waveform->findData(int(DataGenerator::Pattern::Square)));
    prefix->setText(QStringLiteral("sig,"));
    amplitude->setValue(4.0);
    offset->setValue(-1.5);
    duty->setValue(30.0);
    precision->setValue(1);

    EXPECT_EQ(waveform->iconSize(), QSize(16, 16));
    EXPECT_TRUE(period->suffix().isEmpty());
    EXPECT_EQ(period->minimum(), 1);
    EXPECT_EQ(period->maximum(), 1000);
    EXPECT_EQ(amplitude->minimum(), 1);
    EXPECT_EQ(amplitude->maximum(), 100);
    EXPECT_EQ(offset->minimum(), -50);
    EXPECT_EQ(offset->maximum(), 50);
    EXPECT_EQ(precision->maximum(), 10);
    EXPECT_TRUE(precision->suffix().isEmpty());
    EXPECT_FALSE(hex->isCheckable());
    hex->click();

    playPause->click();
    EXPECT_TRUE(playPause->isChecked());
    playPause->click();
    EXPECT_FALSE(playPause->isChecked());

    auto *send = panel.findChild<QToolButton *>(QStringLiteral("sendOnce"));
    ASSERT_NE(send, nullptr);
    send->click();

    ASSERT_EQ(host.sent.size(), 1);
    EXPECT_EQ(host.sent.first(), QByteArrayLiteral("sig,2.5\r\n"));
}

TEST(GeneratorPanel, CounterResetUsesInitialValueAndSendBarTerminator)
{
    test::TempDir dir;
    OpenHost host(dir.path());
    GeneratorPanel panel(&host);
    Q_EMIT host.channelStateChanged(ChannelState::Open);

    auto *start = panel.findChild<QSpinBox *>(QStringLiteral("counterStart"));
    auto *increment = panel.findChild<QSpinBox *>(QStringLiteral("counterIncrement"));
    auto *length = panel.findChild<QSpinBox *>(QStringLiteral("length"));
    auto *reset = panel.findChild<QToolButton *>(QStringLiteral("resetGenerator"));
    auto *send = panel.findChild<QToolButton *>(QStringLiteral("sendOnce"));
    ASSERT_NE(start, nullptr);
    ASSERT_NE(increment, nullptr);
    ASSERT_NE(length, nullptr);
    ASSERT_NE(reset, nullptr);
    ASSERT_NE(send, nullptr);

    length->setValue(4);
    start->setValue(10);
    increment->setValue(-2);
    send->click();
    send->click();
    reset->click();
    send->click();

    ASSERT_EQ(host.sent.size(), 3);
    EXPECT_EQ(host.sent.at(0), QByteArrayLiteral("0010\r\n"));
    EXPECT_EQ(host.sent.at(1), QByteArrayLiteral("0008\r\n"));
    EXPECT_EQ(host.sent.at(2), QByteArrayLiteral("0010\r\n"));
}

TEST(GeneratorPanel, RestoresConfigurationAfterPanelRecreation)
{
    test::TempDir dir;
    OpenHost host(dir.path());
    {
        GeneratorPanel panel(&host);

        auto *mode = panel.findChild<QComboBox *>(QStringLiteral("generatorMode"));
        auto *waveform = panel.findChild<QComboBox *>(QStringLiteral("waveform"));
        auto *length = panel.findChild<QSpinBox *>(QStringLiteral("length"));
        auto *counterStart = panel.findChild<QSpinBox *>(QStringLiteral("counterStart"));
        auto *counterIncrement = panel.findChild<QSpinBox *>(QStringLiteral("counterIncrement"));
        auto *rampStart = panel.findChild<QSpinBox *>(QStringLiteral("rampStart"));
        auto *rampIncrement = panel.findChild<QSpinBox *>(QStringLiteral("rampIncrement"));
        auto *randomMinimum = panel.findChild<QSpinBox *>(QStringLiteral("randomMinimum"));
        auto *randomMaximum = panel.findChild<QSpinBox *>(QStringLiteral("randomMaximum"));
        auto *fixedValue = panel.findChild<QSpinBox *>(QStringLiteral("fixedByte"));
        auto *period = panel.findChild<QSpinBox *>(QStringLiteral("wavePeriod"));
        auto *amplitude = panel.findChild<QDoubleSpinBox *>(QStringLiteral("amplitude"));
        auto *offset = panel.findChild<QDoubleSpinBox *>(QStringLiteral("offset"));
        auto *duty = panel.findChild<QDoubleSpinBox *>(QStringLiteral("dutyCycle"));
        auto *precision = panel.findChild<QSpinBox *>(QStringLiteral("wavePrecision"));
        auto *prefix = panel.findChild<QLineEdit *>(QStringLiteral("prefix"));
        auto *interval = panel.findChild<QComboBox *>(QStringLiteral("streamInterval"));
        auto *hex = panel.findChild<QToolButton *>(QStringLiteral("previewHex"));
        ASSERT_NE(mode, nullptr);
        ASSERT_NE(waveform, nullptr);
        ASSERT_NE(length, nullptr);
        ASSERT_NE(counterStart, nullptr);
        ASSERT_NE(counterIncrement, nullptr);
        ASSERT_NE(rampStart, nullptr);
        ASSERT_NE(rampIncrement, nullptr);
        ASSERT_NE(randomMinimum, nullptr);
        ASSERT_NE(randomMaximum, nullptr);
        ASSERT_NE(fixedValue, nullptr);
        ASSERT_NE(period, nullptr);
        ASSERT_NE(amplitude, nullptr);
        ASSERT_NE(offset, nullptr);
        ASSERT_NE(duty, nullptr);
        ASSERT_NE(precision, nullptr);
        ASSERT_NE(prefix, nullptr);
        ASSERT_NE(interval, nullptr);
        ASSERT_NE(hex, nullptr);

        length->setValue(23);
        counterStart->setValue(-123);
        counterIncrement->setValue(-7);
        rampStart->setValue(0x12);
        rampIncrement->setValue(-4);
        randomMinimum->setValue(23);
        randomMaximum->setValue(187);
        fixedValue->setValue(0x9A);
        mode->setCurrentIndex(mode->findData(-1));
        waveform->setCurrentIndex(waveform->findData(int(DataGenerator::Pattern::Square)));
        period->setValue(250);
        amplitude->setValue(7.5);
        offset->setValue(-4.25);
        duty->setValue(33.0);
        precision->setValue(5);
        prefix->setText(QStringLiteral("sig,"));
        interval->setCurrentIndex(interval->findData(250));
        hex->click();
    }

    GeneratorPanel restored(&host);
    const auto find = [&restored](const char *name) { return restored.findChild<QWidget *>(name); };
    auto *mode = qobject_cast<QComboBox *>(find("generatorMode"));
    auto *waveform = qobject_cast<QComboBox *>(find("waveform"));
    auto *length = qobject_cast<QSpinBox *>(find("length"));
    auto *counterStart = qobject_cast<QSpinBox *>(find("counterStart"));
    auto *counterIncrement = qobject_cast<QSpinBox *>(find("counterIncrement"));
    auto *rampStart = qobject_cast<QSpinBox *>(find("rampStart"));
    auto *rampIncrement = qobject_cast<QSpinBox *>(find("rampIncrement"));
    auto *randomMinimum = qobject_cast<QSpinBox *>(find("randomMinimum"));
    auto *randomMaximum = qobject_cast<QSpinBox *>(find("randomMaximum"));
    auto *fixedValue = qobject_cast<QSpinBox *>(find("fixedByte"));
    auto *period = qobject_cast<QSpinBox *>(find("wavePeriod"));
    auto *amplitude = qobject_cast<QDoubleSpinBox *>(find("amplitude"));
    auto *offset = qobject_cast<QDoubleSpinBox *>(find("offset"));
    auto *duty = qobject_cast<QDoubleSpinBox *>(find("dutyCycle"));
    auto *precision = qobject_cast<QSpinBox *>(find("wavePrecision"));
    auto *prefix = qobject_cast<QLineEdit *>(find("prefix"));
    auto *interval = qobject_cast<QComboBox *>(find("streamInterval"));
    auto *preview = restored.findChild<QPlainTextEdit *>();
    ASSERT_NE(mode, nullptr);
    ASSERT_NE(waveform, nullptr);
    ASSERT_NE(length, nullptr);
    ASSERT_NE(counterStart, nullptr);
    ASSERT_NE(counterIncrement, nullptr);
    ASSERT_NE(rampStart, nullptr);
    ASSERT_NE(rampIncrement, nullptr);
    ASSERT_NE(randomMinimum, nullptr);
    ASSERT_NE(randomMaximum, nullptr);
    ASSERT_NE(fixedValue, nullptr);
    ASSERT_NE(period, nullptr);
    ASSERT_NE(amplitude, nullptr);
    ASSERT_NE(offset, nullptr);
    ASSERT_NE(duty, nullptr);
    ASSERT_NE(precision, nullptr);
    ASSERT_NE(prefix, nullptr);
    ASSERT_NE(interval, nullptr);
    ASSERT_NE(preview, nullptr);

    EXPECT_EQ(mode->currentData().toInt(), -1);
    EXPECT_EQ(waveform->currentData().toInt(), int(DataGenerator::Pattern::Square));
    EXPECT_EQ(length->value(), 23);
    EXPECT_EQ(counterStart->value(), -123);
    EXPECT_EQ(counterIncrement->value(), -7);
    EXPECT_EQ(rampStart->value(), 0x12);
    EXPECT_EQ(rampIncrement->value(), -4);
    EXPECT_EQ(randomMinimum->value(), 23);
    EXPECT_EQ(randomMaximum->value(), 187);
    EXPECT_EQ(fixedValue->value(), 0x9A);
    EXPECT_EQ(period->value(), 250);
    EXPECT_DOUBLE_EQ(amplitude->value(), 7.5);
    EXPECT_DOUBLE_EQ(offset->value(), -4.25);
    EXPECT_DOUBLE_EQ(duty->value(), 33.0);
    EXPECT_EQ(precision->value(), 5);
    EXPECT_EQ(prefix->text(), QStringLiteral("sig,"));
    EXPECT_EQ(interval->currentData().toInt(), 250);
    EXPECT_TRUE(preview->toPlainText().startsWith(QStringLiteral("73 69 67 2C")));
}
