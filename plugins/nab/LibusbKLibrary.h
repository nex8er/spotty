/**
 * \file LibusbKLibrary.h
 * \brief Тонкая обёртка над libusbK, загружаемой в рантайме.
 */
#pragma once

#include <QLibrary>
#include <QList>
#include <QString>

namespace spotty {

/**
 * \class LibusbKLibrary
 * \brief Доступ к vendor-specific интерфейсам NAB через драйвер libusbK.
 *
 * Библиотека libusbK.dll ставится драйвером устройства, а не приложением. Зависимость
 * намеренно динамическая: собранный Spotty остаётся запускаемым и на компьютере без NAB.
 * Все объявления ABI здесь минимальны и соответствуют публичному libusbk.h 3.0.
 */
class LibusbKLibrary
{
public:
    /// \brief Направление и параметры одного bulk endpoint-а.
    struct PipeInfo
    {
        quint8 address = 0;
        quint16 maxPacketSize = 0;
    };

    /// \brief Сведения об одном USB-интерфейсе NAB, прочитанные из дескрипторов.
    struct InterfaceInfo
    {
        QString identity;
        QString manufacturer;
        QString product;
        QString interfaceName;
        QString serialNumber;
        int interfaceNumber = -1;
        quint16 usbVersion = 0;
        quint16 deviceVersion = 0;
        quint8 interfaceClass = 0;
        quint8 interfaceSubClass = 0;
        quint8 interfaceProtocol = 0;
        PipeInfo input;
        PipeInfo output;
    };

    /// \brief Результат одного синхронного обращения к endpoint-у.
    struct TransferResult
    {
        bool ok = false;
        quint32 errorCode = 0;
        qint64 transferred = 0;
    };

    /// \return Общий экземпляр обёртки для процесса.
    static LibusbKLibrary &instance();

    /// \return true, когда установленная libusbK совместима с нужным минимумом API.
    bool isAvailable();

    /// \brief Найти все пригодные для терминала интерфейсы NAB.
    QList<InterfaceInfo> enumerateNabInterfaces();

    /**
     * \brief Открыть ранее обнаруженный интерфейс.
     * \param identity Устойчивый ключ физического устройства.
     * \param interfaceNumber Номер USB-интерфейса.
     * \param handle Куда вернуть ручку libusbK.
     * \param input Куда вернуть bulk IN endpoint.
     * \param output Куда вернуть bulk OUT endpoint.
     * \param error Текст причины отказа.
     * \return true, если устройство всё ещё подключено и интерфейс открыт.
     */
    bool open(const QString &identity, int interfaceNumber, void **handle, PipeInfo *input,
              PipeInfo *output, QString *error);

    /// \brief Освободить ручку, полученную из open().
    void close(void *handle);

    /// \brief Настроить ограничение времени одной передачи endpoint-а.
    bool setTransferTimeout(void *handle, quint8 endpoint, int milliseconds, QString *error);

    /// \brief Прочитать до maxBytes из bulk IN endpoint-а.
    TransferResult read(void *handle, quint8 endpoint, int maxBytes, QByteArray *data);

    /// \brief Записать данные в bulk OUT endpoint.
    TransferResult write(void *handle, quint8 endpoint, const QByteArray &data);

    /// \brief Определить, что код Windows означает потерю устройства.
    static bool isDisconnectError(quint32 errorCode);

    /// \brief Преобразовать системный код Windows в краткий текст.
    static QString errorText(quint32 errorCode);

private:
    LibusbKLibrary() = default;

    /// \brief Загрузить DLL и разрешить нужные символы.
    bool load();

    QLibrary m_library;
    bool m_loadAttempted = false;
    bool m_loaded = false;

    QFunctionPointer m_lstInit = nullptr;
    QFunctionPointer m_lstFree = nullptr;
    QFunctionPointer m_lstMoveNext = nullptr;
    QFunctionPointer m_usbInit = nullptr;
    QFunctionPointer m_usbFree = nullptr;
    QFunctionPointer m_usbGetDescriptor = nullptr;
    QFunctionPointer m_usbQueryInterfaceSettings = nullptr;
    QFunctionPointer m_usbQueryPipe = nullptr;
    QFunctionPointer m_usbSetPipePolicy = nullptr;
    QFunctionPointer m_usbReadPipe = nullptr;
    QFunctionPointer m_usbWritePipe = nullptr;
};

} // namespace spotty
