/**
 * \file DataGenerator.h
 * \brief Порождение тестовых посылок.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

namespace spotty {

/**
 * \class DataGenerator
 * \brief Собирает посылки заданного вида для проверки устройства.
 *
 * Нужен там, где важно не содержимое, а сам факт потока: проверить пропускную
 * способность, поймать переполнение приёмного буфера, увидеть, на каком объёме устройство
 * начинает терять байты.
 *
 * Состояние сохраняется между вызовами: счётчик и последовательность продолжаются, чтобы
 * в потоке было видно, какая посылка потерялась.
 */
class SPOTTY_API_EXPORT DataGenerator
{
    Q_DECLARE_TR_FUNCTIONS(spotty::DataGenerator)

public:
    /// \brief Вид порождаемых данных.
    enum class Pattern {
        Counter,   ///< Десятичный счётчик посылок с дополнением до длины.
        Random,    ///< Случайные байты.
        Fixed,     ///< Один и тот же байт.
        Ramp,      ///< Пила 0x00…0xFF, продолжающаяся между посылками.
        AsciiText, ///< Печатные символы — удобно смотреть глазами.

        /// \name Формы сигнала
        /// Выдают одно десятичное значение на посылку, а не набор байт: так их видно и
        /// глазами в терминале, и графиком, который разбирает числа из вывода. Проверять
        /// приём двоичного мусора удобнее прочими видами, а эти нужны, чтобы получить на
        /// экране узнаваемую форму и убедиться, что тракт её не искажает.
        ///
        /// #setLength() на них не действует: длина строки определяется значением.
        /// @{
        Sine,     ///< Синус.
        Square,   ///< Меандр: два уровня без промежуточных значений.
        Triangle, ///< Треугольник.
        Sawtooth, ///< Пила: линейный рост и мгновенный сброс.
        /// @}
    };

    /// \return `true`, если вид данных — форма сигнала и подчиняется периоду с амплитудой.
    static bool isWaveform(Pattern pattern);

    void setPattern(Pattern pattern) { m_pattern = pattern; }
    Pattern pattern() const { return m_pattern; }

    /// \brief Длина посылки в байтах.
    void setLength(int length);
    int length() const { return m_length; }

    /// \brief Значение байта для Pattern::Fixed.
    void setFixedByte(quint8 value) { m_fixedByte = value; }
    quint8 fixedByte() const { return m_fixedByte; }

    /// \brief Включительные границы случайного байта для Pattern::Random.
    void setRandomRange(int minimum, int maximum);
    int randomMinimum() const { return m_randomMinimum; }
    int randomMaximum() const { return m_randomMaximum; }

    /// \brief Начальное значение десятичного счётчика.
    void setCounterStart(qint64 value) { m_counterStart = value; }
    qint64 counterStart() const { return m_counterStart; }

    /// \brief Приращение десятичного счётчика после каждой посылки.
    void setCounterIncrement(qint64 increment) { m_counterIncrement = increment; }
    qint64 counterIncrement() const { return m_counterIncrement; }

    /// \brief Первый байт двоичной пилы.
    void setRampStart(int value);
    int rampStart() const { return m_rampStart; }

    /// \brief Шаг двоичной пилы между соседними байтами; может быть отрицательным.
    void setRampIncrement(int increment);
    int rampIncrement() const { return m_rampIncrement; }

    /**
     * \brief Период формы сигнала в посылках.
     *
     * Считается в посылках, а не в миллисекундах: период отправки задаётся отдельно и
     * операционной системой не выдерживается точно. Единица допустима для постоянного
     * уровня; большее число даёт форму, не плывущую от опозданий таймера.
     */
    void setWavePeriod(int samples);
    int wavePeriod() const { return m_wavePeriod; }

    /// \brief Размах формы сигнала: значения идут от нуля до этой величины.
    void setAmplitude(double amplitude);
    double amplitude() const { return m_amplitude; }

    /// \brief Постоянное смещение, прибавляемое к значению математической формы.
    void setOffset(double offset);
    double offset() const { return m_offset; }

    /// \brief Доля периода, в которой меандр находится на верхнем уровне, в процентах.
    void setDutyCycle(double percent);
    double dutyCycle() const { return m_dutyCycle; }

    /// \brief Число знаков после запятой в математических формах.
    void setWavePrecision(int digits);
    int wavePrecision() const { return m_wavePrecision; }

    /**
     * \brief Статический префикс каждой посылки.
     *
     * Не входит в #length(): длина задаёт только полезную нагрузку выбранного байтового
     * режима, а префикс позволяет добавить маркер протокола, CSV-поле или имя канала.
     */
    void setPrefix(const QByteArray &prefix) { m_prefix = prefix; }
    QByteArray prefix() const { return m_prefix; }

    /// \brief Породить очередную посылку и продвинуть внутреннее состояние.
    QByteArray generate();

    /**
     * \brief Породить посылку, не меняя состояния.
     *
     * Нужен для предпросмотра: показывать пользователю то, что уйдёт в порт, но не
     * сдвигать при этом счётчик.
     */
    QByteArray preview() const;

    /// \brief Сбросить счётчик и позицию пилы.
    void reset();

    /// \brief Название вида данных для интерфейса.
    static QString patternName(Pattern pattern);

private:
    /// \brief Общая реализация для generate() и preview().
    QByteArray build(qint64 counter, quint64 packetIndex, int rampOffset) const;

    Pattern m_pattern = Pattern::Counter;
    int m_length = 16;
    quint8 m_fixedByte = 0x55;
    int m_randomMinimum = 0;
    int m_randomMaximum = 255;

    qint64 m_counterStart = 0;
    qint64 m_counterIncrement = 1;
    int m_rampStart = 0;
    int m_rampIncrement = 1;

    qint64 m_counter = 0;
    quint64 m_packetIndex = 0;
    int m_rampOffset = 0;
    int m_wavePeriod = 32;
    double m_amplitude = 100.0;
    double m_offset = 0.0;
    double m_dutyCycle = 50.0;
    int m_wavePrecision = 3;
    QByteArray m_prefix;
};

} // namespace spotty
