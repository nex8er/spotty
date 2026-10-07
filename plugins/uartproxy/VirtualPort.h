/**
 * \file VirtualPort.h
 * \brief Конец перехватчика, к которому подключается чужая программа.
 */
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace spotty {

/**
 * \class VirtualPort
 * \brief «Порт», который видит чужая программа, и через который идёт её обмен.
 *
 * Перехватчик не может подсмотреть порт, уже открытый другим процессом: порты
 * эксклюзивны. Поэтому Spotty сам занимает реальный порт, а программе даёт взамен
 * виртуальный, и переправляет байты между ними в обе стороны, по пути записывая их.
 *
 * Реализаций две, и выбор определяется системой, а не вкусом: на POSIX такой порт можно
 * создать (spotty::PtyPort), на Windows — нельзя без драйвера, и там концом служит
 * существующий порт из пары (spotty::SerialVirtualPort).
 *
 * Живёт в потоке ввода-вывода вместе с каналом, который его создал.
 */
class VirtualPort : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    /**
     * \brief Подготовить порт и сделать его видимым чужой программе.
     * \param settings Настройки канала целиком; порт берёт нужные ему ключи.
     * \param error Куда записать причину отказа; может быть `nullptr`.
     */
    virtual bool open(const QVariantMap &settings, QString *error) = 0;

    /// \brief Убрать порт; безопасен при повторном вызове.
    virtual void close() = 0;

    /// \brief Отдать программе байты, пришедшие от устройства.
    virtual void write(const QByteArray &data) = 0;

    /// \brief Как программе найти этот порт: путь или имя. Для показа пользователю.
    virtual QString description() const = 0;

Q_SIGNALS:
    /// \brief Программа отправила байты устройству.
    void dataRead(const QByteArray &data);

    /**
     * \brief Программа не читает, и отданное ей отбрасывается.
     *
     * Перехват не должен останавливать запись из-за медлительной программы: накопление
     * без предела съело бы память, а ожидание остановило бы приём от устройства.
     * Подаётся один раз за эпизод, пока очередь не опустеет.
     */
    void overflowed();

    /// \brief Порт стал непригоден (исчез, ошибка ввода-вывода).
    void failed(const QString &message);
};

} // namespace spotty
