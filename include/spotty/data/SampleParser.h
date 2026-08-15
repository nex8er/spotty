/**
 * \file SampleParser.h
 * \brief Разбор строки устройства в отсчёт телеметрии.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>

#include <QList>
#include <QString>
#include <QStringList>

namespace spotty {

/**
 * \class SampleParser
 * \brief Решает, чем является строка вывода: отсчётом, заголовком или сообщением человеку.
 *
 * \par Пустое поле — это пропуск, а не пробел в строке
 *
 * Разбиение идёт с `Qt::KeepEmptyParts`. Прежний разбор с `SkipEmptyParts` на строке
 * `1,,3` давал два поля, и тройка попадала во **вторую** колонку вместо третьей — одно
 * незаполненное значение сдвигало весь хвост строки и молча перемешивало колонки. Пустое
 * поле здесь даёт NaN на своём месте: колонка остаётся своей колонкой.
 *
 * \note Отличие от spotty::CsvDetector намеренное, и оно не в разбиении, а в строгости.
 *       Тот отвергает строку с пустым полем целиком: терминальный фильтр решает, прятать
 *       ли строку от человека, и в сомнении обязан показать. Плоттер, наоборот, рисует
 *       график по просьбе пользователя, и терять из-за одного пропуска остальные пять
 *       колонок ему незачем. По той же причине плоттер принимает строку из одного поля,
 *       а `CsvDetector` требует не меньше двух.
 */
class SPOTTY_API_EXPORT SampleParser
{
public:
    /// \brief Чем оказалась строка.
    enum class Outcome {
        Data,    ///< Числа; годится в отсчёт.
        Header,  ///< Имена колонок; строку не добавляет, но переименовывает ряды.
        Ignored, ///< Сообщение устройства человеку либо смесь текста с числом.
    };

    /**
     * \struct Result
     * \brief Что вышло из разбора.
     */
    struct Result
    {
        Outcome outcome = Outcome::Ignored;
        int fieldCount = 0;
    };

    void setSeparator(QChar separator) { m_separator = separator; }
    QChar separator() const { return m_separator; }

    /**
     * \brief Разобрать строку.
     * \param line Строка вывода без управляющих последовательностей.
     * \param currentColumnCount Нынешняя ширина отсчёта; ноль, пока колонок нет.
     * \param values Куда сложить значения при `Outcome::Data`; пропуски приходят NaN.
     * \param names Куда сложить имена при `Outcome::Header`.
     *
     * Строка считается заголовком, если полей не меньше двух, ни одно из них не число и
     * не пусто, а их количество сходится с нынешней шириной (либо ширины ещё нет).
     * Ограничение по ширине нужно, чтобы случайная фраза из двух слов не переименовала
     * ряды у шестиколоночного потока.
     */
    Result parse(const QString &line, int currentColumnCount,
                 QList<double> *values, QStringList *names) const;

private:
    /// \brief Пробел и табуляция схлопывают серии; прочие разделители — нет.
    bool separatorIsWhitespace() const;

    QChar m_separator = u',';
};

} // namespace spotty
