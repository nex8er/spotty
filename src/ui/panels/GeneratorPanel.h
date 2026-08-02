/**
 * \file GeneratorPanel.h
 * \brief Панель генератора тестовых посылок.
 */
#pragma once

#include <DataGenerator.h>

#include <QWidget>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

namespace spotty {

struct AppContext;

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
class GeneratorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit GeneratorPanel(const AppContext &context, QWidget *parent = nullptr);

    /// \brief Разрешить отправку. Выключается, когда канал закрыт.
    void setSendEnabled(bool enabled);

Q_SIGNALS:
    /// \brief Требуется отправить готовые байты.
    void sendRequested(const QByteArray &data);

    /// \brief Положить текст в строку отправки, не отправляя.
    void pushToSendBarRequested(const QString &text, int format);

private:
    void applySettingsToGenerator();
    void updatePreview();
    void sendOnce();
    void toggleStream(bool on);

    const AppContext &m_context;
    DataGenerator m_generator;

    QComboBox *m_pattern = nullptr;
    QSpinBox *m_length = nullptr;
    QSpinBox *m_fixedByte = nullptr;
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
