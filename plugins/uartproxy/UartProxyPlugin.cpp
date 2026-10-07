/**
 * \file UartProxyPlugin.cpp
 * \brief Реализация spotty::UartProxyPlugin.
 */
#include "UartProxyPlugin.h"

#include "UartProxyChannel.h"
#include "UartProxyPorts.h"
#include "UartProxySettings.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include "Com0com.h"
#endif

namespace spotty {

using namespace uartproxy;

namespace {

constexpr auto kDeviceId = "uartproxy:tap";

#ifdef Q_OS_WIN
constexpr auto kCom0comUrl = "https://sourceforge.net/projects/com0com/";

/// \brief С какого номера предлагать свободное имя: младшие обычно заняты настоящими портами.
constexpr int kFirstSuggestedCom = 20;

/// \brief Ссылка на страницу драйвера в разметке Qt.
QString com0comLink()
{
    return QStringLiteral("<a href=\"%1\">%1</a>").arg(QLatin1String(kCom0comUrl));
}

/// \brief Имя виртуального порта, выбранное в настройках.
QString virtualPortName(const QVariantMap &settings)
{
    return settings.value(QLatin1String(kVirtualPort)).toString().trimmed();
}

/**
 * \brief Текст строки состояния для выбранного имени.
 *
 * Каждый случай лечится по-своему, поэтому и сказать о каждом нужно своё: драйвера нет —
 * скачать; имя занято чужим устройством — выбрать другое; пары ещё нет — она появится при
 * открытии; пара есть — всё готово.
 */
QString com0comStatusText(const Com0comInfo &info, const QString &name)
{
    if (!info.driverInstalled)
        return UartProxyPlugin::tr("com0com is not installed. Download: %1").arg(com0comLink());
    if (name.isEmpty())
        return UartProxyPlugin::tr("Choose the name the other program will open.");

    Com0comPort own;
    Com0comPort peer;
    if (findCom0comPair(info, name, &own, &peer)) {
        return UartProxyPlugin::tr("Ready: the other program opens %1, Spotty uses %2%3.")
            .arg(own.name, peer.name,
                 peer.hidden ? UartProxyPlugin::tr(" (hidden)") : QString());
    }
    if (!isValidPortName(name))
        return UartProxyPlugin::tr("\"%1\" is not a valid port name.").arg(name);
    if (portNameTaken(name))
        return UartProxyPlugin::tr("%1 is already used by another device: choose another name.")
            .arg(name);
    return UartProxyPlugin::tr("%1 will be created when the interface opens (administrator "
                               "rights will be requested).")
        .arg(name);
}
#endif

} // namespace

QList<InterfaceDescriptor> UartProxyPlugin::enumerate() const
{
    // Устройство одно и виртуальное, поэтому и идентификатор постоянный. Для настоящих
    // портов так делать нельзя (см. предупреждение у spotty::InterfaceDescriptor::id), но
    // здесь реальный порт выбирается настройкой и хранится устойчивым идентификатором.
    InterfaceDescriptor tap;
    tap.id = QLatin1String(kDeviceId);
    tap.systemName = QStringLiteral("uartproxy");
    // Описание служит именем в списке интерфейсов, и пояснение «что это делает» там лишнее:
    // оно есть в README и в подсказках диалога.
    tap.description = displayName();
    return {tap};
}

SettingsSchema UartProxyPlugin::settingsSchema() const
{
    SettingsSchema schema;

    const QString realGroup = tr("Real port");
    const QString virtualGroup = tr("Virtual port");

    schema.add(SettingsField{
        .key = QLatin1String(kRealPort),
        .label = tr("Port with the device"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .options = portOptions(/*byName=*/false),
        .live = true,
        .editable = true,
        .required = true,
        .hint = tr("Spotty opens this port itself, so close it in the other program."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kBaudRate),
        .label = tr("Baud rate"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = 115200,
        .options = {{QStringLiteral("1200"), 1200},
                    {QStringLiteral("2400"), 2400},
                    {QStringLiteral("4800"), 4800},
                    {QStringLiteral("9600"), 9600},
                    {QStringLiteral("19200"), 19200},
                    {QStringLiteral("38400"), 38400},
                    {QStringLiteral("57600"), 57600},
                    {QStringLiteral("115200"), 115200},
                    {QStringLiteral("230400"), 230400},
                    {QStringLiteral("460800"), 460800},
                    {QStringLiteral("921600"), 921600},
                    {QStringLiteral("1500000"), 1500000},
                    {QStringLiteral("3000000"), 3000000}},
        .editable = true,
        .hint = tr("Must match what the other program uses: a virtual port cannot report it."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kDataBits),
        .label = tr("Data bits"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = 8,
        .options = {{QStringLiteral("5"), 5},
                    {QStringLiteral("6"), 6},
                    {QStringLiteral("7"), 7},
                    {QStringLiteral("8"), 8}},
    });

    schema.add(SettingsField{
        .key = QLatin1String(kParity),
        .label = tr("Parity"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("N"),
        .options = {{tr("None"), QStringLiteral("N")},
                    {tr("Even"), QStringLiteral("E")},
                    {tr("Odd"), QStringLiteral("O")},
                    {tr("Mark"), QStringLiteral("M")},
                    {tr("Space"), QStringLiteral("S")}},
    });

    schema.add(SettingsField{
        .key = QLatin1String(kStopBits),
        .label = tr("Stop bits"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("1"),
        .options = {{QStringLiteral("1"), QStringLiteral("1")},
                    {QStringLiteral("1.5"), QStringLiteral("1.5")},
                    {QStringLiteral("2"), QStringLiteral("2")}},
    });

    schema.add(SettingsField{
        .key = QLatin1String(kFlowControl),
        .label = tr("Flow control"),
        .group = realGroup,
        .type = SettingsField::Choice,
        .defaultValue = QStringLiteral("none"),
        .options = {{tr("None"), QStringLiteral("none")},
                    {tr("Hardware (RTS/CTS)"), QStringLiteral("hardware")},
                    {tr("Software (XON/XOFF)"), QStringLiteral("software")}},
    });

    schema.add(SettingsField{
        .key = QLatin1String(kDtrOnOpen),
        .label = tr("Assert DTR on open"),
        .group = realGroup,
        .type = SettingsField::Toggle,
        .defaultValue = true,
        .hint = tr("On many boards DTR is wired to reset - clear it to avoid rebooting "
                   "the device when the port opens."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kRtsOnOpen),
        .label = tr("Assert RTS on open"),
        .group = realGroup,
        .type = SettingsField::Toggle,
        .defaultValue = true,
    });

#ifdef Q_OS_UNIX
    schema.add(SettingsField{
        .key = QLatin1String(kLinkPath),
        .label = tr("Link path"),
        .group = virtualGroup,
        .type = SettingsField::Text,
        .defaultValue = QLatin1String(kDefaultLinkPath),
        .hint = tr("Spotty creates a virtual port and puts a link with this fixed path to it: "
                   "enter the path in the other program. Leave empty to use the port's own "
                   "path, shown after opening - it changes on every run."),
    });
#endif

#ifdef Q_OS_WIN
    // На Windows пару портов заводит com0com, и заводит её Spotty сам: пользователь выбирает
    // только имя, которое увидит чужая программа. Второй конец пары скрыт от перечисления и
    // нигде не настраивается — выбирать его было бы не из чего и незачем.
    schema.add(SettingsField{
        .key = QLatin1String(kVirtualPort),
        .label = tr("Port for the other program"),
        .group = virtualGroup,
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .options = {},
        .live = true,
        .editable = true,
        .required = true,
        .hint = tr("The other program opens this port. If it does not exist, Spotty creates it "
                   "with com0com together with a hidden partner port that Spotty uses itself. "
                   "Each side sets its own baud rate."),
    });

    schema.add(SettingsField{
        .key = QLatin1String(kCom0comStatus),
        .label = tr("State"),
        .group = virtualGroup,
        .type = SettingsField::Note,
        .defaultValue = tr("Checking..."),
        .live = true,
    });

    schema.add(SettingsField{
        .key = QLatin1String(kCom0comRemove),
        .label = tr("Remove this virtual port"),
        .group = virtualGroup,
        .type = SettingsField::Action,
    });

    schema.add(SettingsField{
        .key = QLatin1String(kCom0comSetup),
        .label = tr("Open com0com setup..."),
        .group = virtualGroup,
        .type = SettingsField::Action,
    });
#else
    schema.add(SettingsField{
        .key = QLatin1String(kVirtualPort),
        .label = tr("Existing virtual port"),
        .group = virtualGroup,
        .type = SettingsField::Choice,
        .defaultValue = QString(),
        .options = {},
        .live = true,
        .editable = true,
        .hint = tr("Optional: use one end of a ready pair (for example made by socat) "
                   "instead of creating a port."),
    });
#endif

    return schema;
}

QString UartProxyPlugin::settingsSummary(const QVariantMap &settings) const
{
    if (settings.isEmpty())
        return {};

    QString summary = QStringLiteral("%1 %2-%3-%4")
                          .arg(settings.value(QLatin1String(kBaudRate)).toInt())
                          .arg(settings.value(QLatin1String(kDataBits)).toInt())
                          .arg(settings.value(QLatin1String(kParity)).toString())
                          .arg(settings.value(QLatin1String(kStopBits)).toString());

    // То, что нужно ввести в чужой программе, — самое важное сведение об этом
    // интерфейсе, и спрятать его в диалоге значит заставлять искать каждый раз.
    QString where = settings.value(QLatin1String(kVirtualPort)).toString().trimmed();
    if (where.isEmpty())
        where = settings.value(QLatin1String(kLinkPath)).toString().trimmed();
    if (!where.isEmpty())
        summary += QStringLiteral(" → ") + where;
    return summary;
}

QList<SettingsOption> UartProxyPlugin::liveOptions(const InterfaceDescriptor &descriptor,
                                                   const QString &key,
                                                   const QVariantMap &settings)
{
    Q_UNUSED(descriptor);
    Q_UNUSED(settings);

    // Реальный порт хранится устойчивым идентификатором, виртуальный — именем: он не
    // обязан быть USB-устройством, и свойств для идентификатора у него нет.
    if (key == QLatin1String(kRealPort))
        return portOptions(/*byName=*/false);
#ifdef Q_OS_WIN
    if (key == QLatin1String(kCom0comStatus))
        return {{com0comStatusText(detectCom0com(), virtualPortName(settings)), QVariant()}};

    if (key == QLatin1String(kVirtualPort)) {
        // Уже созданные видимые концы пар и одно свободное имя: остальное набирают руками.
        // Скрытые концы не предлагаются — открывать их чужой программе незачем.
        const Com0comInfo info = detectCom0com();
        QList<SettingsOption> options;
        for (const Com0comPair &pair : info.pairs) {
            for (const Com0comPort *port : {&pair.a, &pair.b}) {
                if (!port->hidden)
                    options.append({tr("%1 - com0com, ready").arg(port->name), port->name});
            }
        }
        for (int number = kFirstSuggestedCom; number < 256; ++number) {
            const QString name = QStringLiteral("COM%1").arg(number);
            if (!portNameTaken(name)) {
                options.append({tr("%1 - new").arg(name), name});
                break;
            }
        }
        return options;
    }
#endif

    if (key == QLatin1String(kVirtualPort))
        return portOptions(/*byName=*/true);
    return {};
}

QString UartProxyPlugin::triggerAction(const InterfaceDescriptor &descriptor, const QString &key,
                                       const QVariantMap &settings)
{
    Q_UNUSED(descriptor);
    Q_UNUSED(settings);

#ifdef Q_OS_WIN
    const Com0comInfo info = detectCom0com();
    QString problem;

    if (key == QLatin1String(kCom0comSetup)) {
        if (info.setupGui().isEmpty()) {
            return tr("The com0com setup program was not found. Install com0com from %1 "
                      "and try again.")
                .arg(com0comLink());
        }
        if (!launchCom0comSetup(info, &problem))
            return problem;
    } else if (key == QLatin1String(kCom0comRemove)) {
        const QString name = virtualPortName(settings);
        const Com0comPair *pair = findCom0comPair(info, name);
        if (!pair)
            return tr("There is no com0com port named %1.").arg(name);
        // Не ждём: удаление устройств идёт секунды, а вызов пришёл из потока UI. Строка
        // состояния покажет результат сама.
        if (!removeCom0comPair(info, pair->number, &problem))
            return problem;
    }
#else
    Q_UNUSED(key);
#endif
    return {};
}

IInterfaceChannel *UartProxyPlugin::createChannel(const InterfaceDescriptor &descriptor)
{
    if (descriptor.id != QLatin1String(kDeviceId))
        return nullptr;

    auto *channel = new UartProxyChannel;

#ifdef Q_OS_WIN
    // Подписка заводится здесь, а не в конструкторе: объект плагина создаётся загрузчиком,
    // и QCoreApplication к тому моменту может ещё не существовать.
    if (!m_quitHooked && QCoreApplication::instance()) {
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                &UartProxyPlugin::removeCreatedPairs);
        m_quitHooked = true;
    }
    channel->setPairCreatedHandler([this](int number) {
        const QMutexLocker locker(&m_createdMutex);
        if (!m_createdPairs.contains(number))
            m_createdPairs.append(number);
    });
#endif

    return channel;
}

void UartProxyPlugin::removeCreatedPairs()
{
#ifdef Q_OS_WIN
    QList<int> numbers;
    {
        const QMutexLocker locker(&m_createdMutex);
        numbers.swap(m_createdPairs);
    }
    if (numbers.isEmpty())
        return;

    // Пару могли уже удалить кнопкой в диалоге: удалять отсутствующее значило бы зря
    // спрашивать права администратора.
    const Com0comInfo info = detectCom0com();
    for (const Com0comPair &pair : info.pairs) {
        if (numbers.contains(pair.number))
            removeCom0comPair(info, pair.number, nullptr);
    }
#endif
}

} // namespace spotty
