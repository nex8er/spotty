/**
 * \file DataGenerator.h
 * \brief Порождение тестовых посылок.
 */
#pragma once

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
class DataGenerator
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
    };

    void setPattern(Pattern pattern) { m_pattern = pattern; }
    Pattern pattern() const { return m_pattern; }

    /// \brief Длина посылки в байтах.
    void setLength(int length);
    int length() const { return m_length; }

    /// \brief Значение байта для Pattern::Fixed.
    void setFixedByte(quint8 value) { m_fixedByte = value; }
    quint8 fixedByte() const { return m_fixedByte; }

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
    QByteArray build(quint64 counter, int rampOffset) const;

    Pattern m_pattern = Pattern::Counter;
    int m_length = 16;
    quint8 m_fixedByte = 0x55;

    quint64 m_counter = 0;
    int m_rampOffset = 0;
};

} // namespace spotty
