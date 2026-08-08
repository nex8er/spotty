/**
 * \file GeneratorPanel.h
 * \brief Панель генератора тестовых посылок.
 */
#pragma once

#include <spotty/data/DataGenerator.h>
#include <spotty/ui/PanelWidget.h>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QTimer;

namespace spotty {

/**
 * \class GeneratorPanel
 * \brief Порождение тестовых посылок и их отправка, разово или потоком.
 *
 * Нужен там, где важно не содержимое, а сам факт нагрузки: проверить пропускную
 * способность, поймать переполнение приёмного буфера устройства, увидеть, на каком объёме
 * начинают теряться байты.
 *
 * Предпросмотр показывает ровно то, что уйдёт в порт, не сдвигая счётчик генератора.
 */
class GeneratorPanel : public PanelWidget
{
    Q_OBJECT

public:
    explicit GeneratorPanel(IPanelHost *host, QWidget *parent = nullptr);

protected:
    /// \brief Поток останавливается вместе с закрытием канала: слать некуда.
    void channelStateChanged(ChannelState state) override;

private:
    void applySettingsToGenerator();
    void updatePreview();
    void sendOnce();
    void toggleStream(bool on);
    void setSendEnabled(bool enabled);

    DataGenerator m_generator;

    QComboBox *m_pattern = nullptr;
    QSpinBox *m_length = nullptr;
    QSpinBox *m_fixedByte = nullptr;
    QSpinBox *m_wavePeriod = nullptr;
    QDoubleSpinBox *m_amplitude = nullptr;
    QComboBox *m_interval = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_sendOnce = nullptr;
    QPushButton *m_stream = nullptr;
    QPushButton *m_toSendBar = nullptr;
    QLabel *m_sentLabel = nullptr;

    QTimer *m_timer = nullptr;
    qint64 m_sentPackets = 0;
    bool m_sendEnabled = false;
};

} // namespace spotty
