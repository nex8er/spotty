/**
 * \file SignalGenChannel.h
 * \brief Виртуальный канал, генерирующий математические сигналы для отладки графика.
 */
#pragma once

#include <spotty/api/IInterfaceChannel.h>

#include <QElapsedTimer>

#include <array>

class QTimer;

namespace spotty {

/**
 * \class SignalGenChannel
 * \brief Источник CSV-строк с разными математическими функциями.
 *
 * \par Зачем он существует
 *
 * Плагин plotter разбирает строки вида `12.5,3,-7` и строит по ним график
 * (spotty::CsvSeries::feed()). Проверить его без настоящего устройства, шлющего телеметрию,
 * было нечем — spotty::LoopbackChannel умеет только одну строку фиксированного вида. Этот
 * канал воспроизводит то, с чем графику приходится справляться на практике: несколько рядов
 * сразу, точку отсчёта по времени, внезапно появляющийся новый ряд и нечисловую строку
 * посреди чисел.
 *
 * \par Формат строки
 *
 * `[t,]значение[,значение...]`. Первая колонка — секунды с момента открытия канала,
 * добавляется настройкой `includeAxis`; растёт равномерно и годится в качестве оси X, если
 * выбрать её в панели графика вместо оси времени по умолчанию.
 *
 * \see spotty::SignalGenPlugin
 */
class SignalGenChannel : public IInterfaceChannel
{
    Q_OBJECT

public:
    explicit SignalGenChannel(QObject *parent = nullptr);

    /// \copydoc spotty::IInterfaceChannel::open
    bool open(const QVariantMap &settings, QString *error) override;

    /// \copydoc spotty::IInterfaceChannel::close
    void close() override;

    /**
     * \brief Отбрасывает отправленное.
     *
     * Канал только читает: он занят выдачей сигналов, а не приёмом команд. В отличие от
     * spotty::LoopbackChannel, у которого для проверки записи есть отдельный режим эха,
     * здесь этой роли ни на кого не возложено.
     */
    qint64 write(const QByteArray &data) override;

    /// \copydoc spotty::IInterfaceChannel::state
    ChannelState state() const override { return m_state; }

    /// \copydoc spotty::LoopbackChannel::applySettings
    bool applySettings(const QVariantMap &settings) override;

private:
    /// \brief Базовые формы, из которых складываются режимы `all` и `growing`.
    enum class Wave { Sine, Cosine, Square, Triangle, Sawtooth, Noise };

    /// \brief Полный набор форм в порядке, в котором они появляются в этих режимах.
    static constexpr std::array<Wave, 6> kPalette = {
        Wave::Sine, Wave::Cosine, Wave::Square, Wave::Triangle, Wave::Sawtooth, Wave::Noise,
    };

    /// \brief Собрать и выдать очередную строку.
    void emitSample();

    /// \brief Значения колонок для момента времени \p t по выбранному в настройках режиму.
    QStringList columnValues(double t) const;

    /**
     * \brief Число колонок в режиме `growing`.
     *
     * Растёт от 1 до размера #kPalette и обратно треугольником от #m_counter — так график
     * попеременно то заводит новый ряд, то остаётся с рядом, переставшим получать данные,
     * не сваливаясь ни в одном из направлений в единственное неизменное состояние.
     */
    int growingColumnCount() const;

    static double basicWave(Wave wave, double t, double periodSec, double amplitude);
    static double chirpWave(double t, double periodSec, double amplitude);
    static double decayWave(double t, double periodSec, double amplitude);
    static double pulseWave(double t, double periodSec, double amplitude);
    static double stepsWave(double t, double periodSec, double amplitude);

    /// \brief Случайная добавка к значению — общая для любого режима, не только `noise`.
    static double jitter(double amplitude, double noisePercent);

    static QString formatValue(double value);

    /// \brief Сменить состояние и известить, если оно действительно изменилось.
    void setState(ChannelState state, const QString &detail = {});

    ChannelState m_state = ChannelState::Closed;
    QVariantMap m_settings;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock; ///< С момента open(): даёт t для формул и для колонки оси X.
    quint64 m_counter = 0; ///< Число выданных строк данных.
};

} // namespace spotty
