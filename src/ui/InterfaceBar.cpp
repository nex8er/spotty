/**
 * \file InterfaceBar.cpp
 * \brief Реализация spotty::InterfaceBar.
 */
#include "InterfaceBar.h"

#include "AppContext.h"
#include <spotty/data/Formatting.h>
#include "InterfaceCombo.h"
#include "InterfaceItemDelegate.h"
#include "InterfaceLabel.h"
#include "theme/MdiIcons.h"
#include "theme/ThemeManager.h"

#include <InterfaceRegistry.h>
#include <PluginManager.h>
#include <spotty/api/IInterfacePlugin.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QToolButton>

#include <algorithm>

namespace spotty {

namespace {

constexpr int kStatusGlyphSize = 14; ///< Размер кружка состояния, px.
constexpr int kButtonGlyphSize = 18; ///< Размер значка на кнопке, px.

/// \brief Как выглядит одно состояние канала.
struct StatusLook
{
    char32_t glyph; ///< Глиф Material Design Icons.
    QColor color;   ///< Цвет из текущей темы.
    QString label;  ///< Подпись.
};

/**
 * \brief Подобрать значок, цвет и подпись под состояние канала.
 * \param state Состояние.
 * \param colors Цвета текущей темы.
 */
StatusLook lookFor(ChannelState state, const ThemeColors &colors)
{
    switch (state) {
    case ChannelState::Open:
        // Залитый кружок против контурного: открытость видно и без различения цвета.
        return {mdi::CheckboxBlankCircle, colors.okText, InterfaceBar::tr("Open")};
    case ChannelState::Opening:
        return {mdi::CircleOutline, colors.accent, InterfaceBar::tr("Opening")};
    case ChannelState::Unavailable:
        return {mdi::AlertCircleOutline, colors.textMuted, InterfaceBar::tr("Unavailable")};
    case ChannelState::Error:
        return {mdi::AlertCircleOutline, colors.errorText, InterfaceBar::tr("Error")};
    case ChannelState::Closed:
        break;
    }
    return {mdi::CircleOutline, colors.textMuted, InterfaceBar::tr("Closed")};
}

} // namespace

InterfaceBar::InterfaceBar(const AppContext &context, QWidget *parent)
    : QWidget(parent)
    , m_context(context)
{
    setObjectName(QStringLiteral("interfaceBar"));

    m_statusDot = new QLabel(this);
    m_statusDot->setObjectName(QStringLiteral("statusDot"));

    m_combo = new InterfaceCombo(this);
    m_combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_combo->setMinimumWidth(360);
    // Свой делегат: заголовки групп и раздельная отрисовка имени слева от служебных
    // сведений справа — то, что один QComboBox::addItem() показать не может.
    m_combo->setItemDelegate(new InterfaceItemDelegate(m_combo));
    // Колесо мыши над выпадающим списком по умолчанию переключает пункты, даже когда у
    // него нет фокуса. Для выбора порта это недопустимо: случайная прокрутка над окном
    // молча переключила бы активный интерфейс, закрыв текущий и открыв чужой.
    m_combo->installEventFilter(this);

    m_openButton = new QToolButton(this);
    m_openButton->setAutoRaise(true);
    m_openButton->setEnabled(false);

    m_settingsButton = new QToolButton(this);
    m_settingsButton->setToolTip(tr("Interface settings"));
    m_settingsButton->setAutoRaise(true);
    m_settingsButton->setEnabled(false);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(8);
    layout->addWidget(m_statusDot);
    layout->addWidget(m_combo, 1);
    layout->addWidget(m_openButton);
    layout->addWidget(m_settingsButton);

    connect(m_combo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_rebuilding)
            return;
        const QString id = m_combo->itemData(index).toString();
        m_settingsButton->setEnabled(!id.isEmpty());
        m_openButton->setEnabled(!id.isEmpty());
        Q_EMIT interfaceSelected(id);
    });

    connect(m_openButton, &QToolButton::clicked, this, &InterfaceBar::toggleOpenRequested);

    connect(m_settingsButton, &QToolButton::clicked, this, [this] {
        const QString id = currentInterfaceId();
        if (!id.isEmpty())
            Q_EMIT settingsRequested(id);
    });

    if (m_context.registry) {
        connect(m_context.registry, &InterfaceRegistry::changed, this, &InterfaceBar::rebuild);
    }
    if (m_context.theme) {
        connect(m_context.theme, &ThemeManager::themeChanged, this, [this] {
            updateIcons();
            updateStatusIndicator();
        });
    }

    updateIcons();
    updateStatusIndicator();
    rebuild();
}

bool InterfaceBar::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_combo && event->type() == QEvent::Wheel) {
        // Прокрутка не должна менять выбор; список открывается щелчком.
        event->ignore();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void InterfaceBar::updateIcons()
{
    m_settingsButton->setIcon(MdiIcons::icon(mdi::TuneVariant, kButtonGlyphSize));

    // Значок кнопки отражает действие, которое она выполнит, а не текущее состояние:
    // «вилка вынута» на открытом канале означает «закрыть».
    const bool open = m_state == ChannelState::Open || m_state == ChannelState::Opening;
    m_openButton->setIcon(MdiIcons::icon(open ? mdi::PowerPlugOff : mdi::PowerPlug,
                                         kButtonGlyphSize));
    m_openButton->setToolTip(open ? tr("Close the interface") : tr("Open the interface"));
}

QString InterfaceBar::secondaryText(const InterfaceEntry &entry) const
{
    QStringList parts;

    // Системный адрес есть смысл повторять справа, только если слева уже не он сам:
    // без псевдонима и без описания от драйвера primaryText() и так показывает его.
    if (!entry.alias.isEmpty() || !entry.descriptor.description.isEmpty())
        parts << entry.descriptor.systemName;

    if (entry.present) {
        const QString since = Formatting::timeAgo(entry.discoveredAt);
        if (!since.isEmpty())
            parts << since;
    } else {
        parts << tr("unavailable");
    }

    return parts.join(QStringLiteral("  ·  "));
}

void InterfaceBar::addGroupHeader(const QString &label)
{
    m_combo->addItem(label);
    const int index = m_combo->count() - 1;
    m_combo->setItemData(index, true, InterfaceHeaderRole);

    // Заголовок группы не выбирается ни щелчком, ни стрелками: он не устройство.
    if (auto *model = qobject_cast<QStandardItemModel *>(m_combo->model())) {
        if (QStandardItem *item = model->item(index))
            item->setFlags(item->flags() & ~(Qt::ItemIsSelectable | Qt::ItemIsEnabled));
    }
}

void InterfaceBar::rebuild()
{
    if (!m_context.registry)
        return;

    // Выбор пользователя переживает перестроение: при опросе список пересобирается раз в
    // секунду, и сброс выбора прямо под курсором сделал бы виджет неработоспособным.
    const QString previous = currentInterfaceId();

    m_rebuilding = true;
    const QSignalBlocker blocker(m_combo);

    m_combo->clear();
    m_combo->addItem(tr("Not selected"), QString());

    // Группируем по плагину-владельцу: однотипные устройства (все UART, все Loopback)
    // держатся вместе, а не перемешиваются общей сортировкой по имени. entries() уже
    // возвращает список отсортированным (присутствующие выше, дальше по алфавиту) —
    // раскладка по группам ниже сохраняет этот порядок внутри каждой из них.
    QMap<QString, QList<InterfaceEntry>> groups;
    QMap<QString, QString> groupLabels;
    for (const InterfaceEntry &entry : m_context.registry->entries()) {
        // Скрытые пользователем устройства сюда не попадают — этим списком отдельно
        // управляет раздел UART в настройках интерфейса; entries() отдаёт их специально
        // для того списка, а не для этого.
        if (entry.hidden)
            continue;

        const QString &pluginId = entry.descriptor.pluginId;
        groups[pluginId].append(entry);

        if (!groupLabels.contains(pluginId)) {
            QString label = pluginId;
            if (m_context.plugins) {
                if (IInterfacePlugin *plugin = m_context.plugins->plugin(pluginId))
                    label = plugin->displayName();
            }
            groupLabels.insert(pluginId, label);
        }
    }

    QStringList pluginIds = groups.keys();
    std::sort(pluginIds.begin(), pluginIds.end(), [&groupLabels](const QString &a, const QString &b) {
        return groupLabels.value(a).localeAwareCompare(groupLabels.value(b)) < 0;
    });

    for (const QString &pluginId : std::as_const(pluginIds)) {
        addGroupHeader(groupLabels.value(pluginId));

        for (const InterfaceEntry &entry : std::as_const(groups[pluginId])) {
            m_combo->addItem(interfacePrimaryLabel(entry, m_context.plugins), entry.descriptor.id);
            const int index = m_combo->count() - 1;

            m_combo->setItemData(index, secondaryText(entry), InterfaceSecondaryTextRole);

            // Во всплывающей подсказке — описание (если его уже не видно в самой подписи)
            // и поля extra: идентификаторы производителя, модели и серийный номер помогают
            // различить два одинаковых переходника.
            QStringList tooltip;
            if (!entry.alias.isEmpty() && !entry.descriptor.description.isEmpty())
                tooltip << entry.descriptor.description;
            for (auto it = entry.descriptor.extra.constBegin();
                 it != entry.descriptor.extra.constEnd(); ++it) {
                tooltip << QStringLiteral("%1: %2").arg(it.key(), it.value().toString());
            }
            m_combo->setItemData(index, tooltip.join(u'\n'), Qt::ToolTipRole);

            if (!entry.present && m_context.theme) {
                m_combo->setItemData(index, m_context.theme->colors().textMuted,
                                     Qt::ForegroundRole);
            }
        }
    }

    const int restored = m_combo->findData(previous);
    m_combo->setCurrentIndex(restored >= 0 ? restored : 0);
    m_settingsButton->setEnabled(!currentInterfaceId().isEmpty());

    m_rebuilding = false;
}

QString InterfaceBar::currentInterfaceId() const
{
    return m_combo->currentData().toString();
}

void InterfaceBar::setCurrentInterfaceId(const QString &id)
{
    // Восстановление при запуске — не решение пользователя, а memory программы.
    // Без блокировки currentIndexChanged дошёл бы до interfaceSelected, и обработчик в
    // MainWindow открыл бы канал в обход настройки autoOpenLastInterface. Состояние кнопок
    // после этого выставляем вручную — как в rebuild().
    const int index = m_combo->findData(id);
    const QSignalBlocker blocker(m_combo);
    m_combo->setCurrentIndex(index >= 0 ? index : 0);

    const bool hasId = !currentInterfaceId().isEmpty();
    m_settingsButton->setEnabled(hasId);
    m_openButton->setEnabled(hasId);
}

void InterfaceBar::setChannelState(ChannelState state, const QString &detail)
{
    m_state = state;
    m_stateDetail = detail;
    // Новое состояние — сигнал, что прежняя ошибка уже неактуальна: канал либо
    // переоткрылся, либо закрылся, либо сам перешёл в Error со своим отображением.
    m_hasError = false;
    updateStatusIndicator();
    updateIcons();
}

void InterfaceBar::flagError(const QString &detail)
{
    m_hasError = true;
    m_errorDetail = detail;
    updateStatusIndicator();
}

void InterfaceBar::updateStatusIndicator()
{
    const ThemeColors colors =
        m_context.theme ? m_context.theme->colors() : ThemeColors{};

    if (m_hasError) {
        m_statusDot->setPixmap(MdiIcons::icon(mdi::AlertCircleOutline, colors.errorText,
                                              kStatusGlyphSize)
                                   .pixmap(kStatusGlyphSize));
        m_statusDot->setToolTip(m_errorDetail);
        return;
    }

    const StatusLook look = lookFor(m_state, colors);
    m_statusDot->setPixmap(
        MdiIcons::icon(look.glyph, look.color, kStatusGlyphSize).pixmap(kStatusGlyphSize));

    // Подписи «Открыт»/«Закрыт» рядом с кружком больше нет — без неё пояснение в
    // подсказке осталось бы единственным способом узнать состояние, не открывая настройки.
    QString tooltip = look.label;
    if (!m_stateDetail.isEmpty())
        tooltip += QStringLiteral(" — %1").arg(m_stateDetail);
    m_statusDot->setToolTip(tooltip);
}

} // namespace spotty
