/**
 * \file DataCodec.h
 * \brief Преобразование вводимого текста в байты для отправки.
 */
#pragma once

#include <QByteArray>
#include <QCoreApplication>
#include <QString>

namespace spotty {

/**
 * \class DataCodec
 * \brief Кодирование строки ввода в байты и разбор шестнадцатеричной записи.
 *
 * Все методы статические; класс существует как пространство имён с поддержкой tr(): текст
 * ошибок показывается пользователю и должен переводиться.
 */
class DataCodec
{
    Q_DECLARE_TR_FUNCTIONS(spotty::DataCodec)

public:
    /// \brief Как трактовать введённый текст.
    enum class Format {
        Text,   ///< Как есть, в выбранной кодировке.
        Hex,    ///< Пары шестнадцатеричных цифр; разделители игнорируются.
        Base64, ///< Base64.
    };

    /// \brief Что дописать в конец отправляемого.
    enum class Termination {
        None,
        Lf,   ///< `\n`
        Cr,   ///< `\r`
        CrLf, ///< `\r\n`
        Nul,  ///< `\0`
    };

    /**
     * \brief Байты завершения строки.
     */
    static QByteArray terminationBytes(Termination termination);

    /**
     * \brief Преобразовать введённый текст в байты.
     * \param input Текст из строки отправки.
     * \param format Как его трактовать.
     * \param termination Что дописать в конец.
     * \param error Куда записать причину отказа. Может быть `nullptr`.
     * \return Байты для отправки; при ошибке — пустой массив и заполненный \p error.
     *
     * Пустой ввод — не ошибка: отправка одной терминации строки (например, «Enter»)
     * совершенно осмысленна и часто нужна.
     */
    static QByteArray encode(const QString &input, Format format, Termination termination,
                             QString *error = nullptr);

    /**
     * \brief Разобрать шестнадцатеричную запись.
     * \param input Например `"01 A5 FF"`, `"01a5ff"`, `"01,A5,FF"`.
     * \param error Куда записать причину отказа.
     * \return Байты; при ошибке — пустой массив.
     *
     * Разделители — пробелы, запятые, двоеточия, дефисы — игнорируются, потому что байты
     * копируются из самых разных источников: даташитов, логов анализатора, чужого кода.
     * Приставка `0x` тоже допускается.
     */
    static QByteArray fromHex(const QString &input, QString *error = nullptr);

    /**
     * \brief Представить байты шестнадцатеричной записью через пробел.
     */
    static QString toHex(const QByteArray &data);

    /// \brief Имя формата для интерфейса.
    static QString formatName(Format format);

    /// \brief Имя терминации для интерфейса.
    static QString terminationName(Termination termination);
};

} // namespace spotty
