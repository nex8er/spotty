/**
 * \file PanelWidget.cpp
 * \brief Реализация spotty::PanelWidget.
 */
#include <spotty/ui/PanelWidget.h>

#include <spotty/ui/IPanelHost.h>

#include <QLabel>
#include <QVBoxLayout>

namespace spotty {

namespace {

/// \brief Поля панели и шаг между блоками — общий вид всех панелей окна.
constexpr int kMarginH = 6;
constexpr int kMarginV = 10;
constexpr int kSpacing = 8;

} // namespace

PanelWidget::PanelWidget(IPanelHost *host, QWidget *parent)
    : QWidget(parent)
    , m_host(host)
{
    m_content = new QVBoxLayout(this);
    m_content->setContentsMargins(kMarginH, kMarginV, kMarginH, kMarginV);
    m_content->setSpacing(kSpacing);

    if (!m_host)
        return;

    // Подписки делает основа, а не каждая панель заново: три из четырёх встроенных
    // подключались к смене темы одинаковым кодом, и четвёртая про это забыла.
    connect(m_host, &IPanelHost::themeChanged, this, [this] { themeChanged(); });
    connect(m_host, &IPanelHost::channelStateChanged, this,
            [this](ChannelState state) { channelStateChanged(state); });
    connect(m_host, &IPanelHost::settingsReset, this, [this] { settingsReset(); });
    connect(m_host, &IPanelHost::aboutToClose, this, [this] { aboutToClose(); });
}

PanelWidget::~PanelWidget() = default;

void PanelWidget::setPanelTitle(const QString &title)
{
    if (m_title) {
        static_cast<QLabel *>(m_title)->setText(title);
        return;
    }

    auto *label = new QLabel(title, this);
    label->setObjectName(QStringLiteral("panelTitle"));
    m_content->insertWidget(0, label);
    m_title = label;
}

} // namespace spotty
