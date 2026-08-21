/**
 * \file GeneratorPanel.h
 * \brief Панель генератора тестовых посылок.
 */
#pragma once

#include <spotty/data/DataGenerator.h>
#include <spotty/ui/PanelDescriptor.h>
#include <spotty/ui/PanelWidget.h>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QWidget;

namespace spotty {

/**
 * \class GeneratorPanel
 * \brief Порождение тестовых посылок и их отправка, разово или потоком.
 *
 * Режим выбирается один раз вверху, а ниже показываются только параметры, которые влияют
 * на выбранную посылку. Это сохраняет форму короткой для простых режимов и не смешивает
 * настройки формы сигнала с настройками остальных шаблонов.
 */
class GeneratorPanel : public PanelWidget
{
    Q_OBJECT

public:
    explicit GeneratorPanel(IPanelHost *host, QWidget *parent = nullptr);

protected:
    void themeChanged() override;

    /// \brief Вернуть значения по умолчанию после глобального сброса настроек.
    void settingsReset() override;

    /// \brief Поток останавливается вместе с закрытием канала: слать некуда.
    void channelStateChanged(ChannelState state) override;

private:
    void selectMode(int index);
    void restoreSettings();
    void saveSettings() const;
    void applySettingsToGenerator();
    void updatePreview();
    void updateFrequency();
    void updateIcons();
    void updateSentLabel();

    /**
     * \brief Отправить один пакет.
     * \param userInitiated Ложно только для вызова из таймера потока: выбор получателя из
     *        m_streamTarget относится к потоку, а ручное «отправить один раз» ведёт себя
     *        так же, как раньше, и идёт первому доступному.
     */
    void sendOnce(bool userInitiated = true);

    void resetRuntime();
    void setStreaming(bool on);
    void setSendEnabled(bool enabled);
    QByteArray withSendTermination(const QByteArray &payload) const;
    int selectedIntervalMs() const;

    /// \brief Получатель потока, выбранный в списке.
    SendTarget selectedStreamTarget() const;

    /// \brief Показать или спрятать выбор получателя — виден только в режиме двух интерфейсов.
    void updateStreamTargetVisibility(bool dualTransportEnabled);

    DataGenerator m_generator;
    DataGenerator::Pattern m_activePattern = DataGenerator::Pattern::Counter;

    QComboBox *m_mode = nullptr;
    QStackedWidget *m_modeControls = nullptr;

    QSpinBox *m_length = nullptr;
    QSpinBox *m_counterStart = nullptr;
    QSpinBox *m_counterIncrement = nullptr;
    QSpinBox *m_rampStart = nullptr;
    QSpinBox *m_rampIncrement = nullptr;
    QSpinBox *m_randomMinimum = nullptr;
    QSpinBox *m_randomMaximum = nullptr;
    QSpinBox *m_fixedByte = nullptr;

    QComboBox *m_waveform = nullptr;
    QSpinBox *m_wavePeriod = nullptr;
    QSlider *m_wavePeriodSlider = nullptr;
    QDoubleSpinBox *m_amplitude = nullptr;
    QSlider *m_amplitudeSlider = nullptr;
    QDoubleSpinBox *m_offset = nullptr;
    QSlider *m_offsetSlider = nullptr;
    QLabel *m_dutyLabel = nullptr;
    QDoubleSpinBox *m_dutyCycle = nullptr;
    QSlider *m_dutyCycleSlider = nullptr;
    QWidget *m_dutyRow = nullptr;
    QSpinBox *m_wavePrecision = nullptr;
    QSlider *m_wavePrecisionSlider = nullptr;
    QLabel *m_frequency = nullptr;

    QLineEdit *m_prefix = nullptr;
    QComboBox *m_interval = nullptr;
    QLabel *m_streamTargetLabel = nullptr;
    /// \brief Получатель потока; вместе с меткой видна, только пока открыт второй интерфейс.
    QComboBox *m_streamTarget = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QToolButton *m_hexView = nullptr;
    QToolButton *m_playPause = nullptr;
    QToolButton *m_reset = nullptr;
    QToolButton *m_sendOnce = nullptr;
    QLabel *m_sentLabel = nullptr;

    QTimer *m_timer = nullptr;
    qint64 m_sentPackets = 0;
    bool m_sendEnabled = false;
    bool m_previewHex = false;
    bool m_restoringSettings = false;
};

} // namespace spotty
