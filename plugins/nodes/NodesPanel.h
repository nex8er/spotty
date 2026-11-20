/**
 * \file NodesPanel.h
 * \brief Независимые списки узлов всех доступных адаптеров.
 */
#pragma once
#include <spotty/ui/PanelWidget.h>
#include <QHash>
#include <QVariantMap>

class QLabel;
class QTableWidget;
class QTimer;
class QPushButton;
class QCheckBox;
class QVBoxLayout;
class QToolButton;

namespace spotty {
class NodesPanel : public PanelWidget
{
    Q_OBJECT
public:
    explicit NodesPanel(IPanelHost *panelHost, QWidget *parent = nullptr);
    ~NodesPanel() override;
protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    struct AdapterView
    {
        QWidget *widget = nullptr;
        QLabel *title = nullptr;
        QToolButton *connection = nullptr;
        QToolButton *scan = nullptr;
        QTableWidget *table = nullptr;
    };
    void refresh();
    void stopDiscovery();
    void clearLists();
    AdapterView addAdapter(const QString &id);
    QHash<QString, AdapterView> m_adapters;
    QVariantMap m_known;
    QVariantMap m_names;
    QVBoxLayout *m_adaptersLayout = nullptr;
    QLabel *m_empty = nullptr;
    QCheckBox *m_common = nullptr;
    QToolButton *m_hexNodeNumbers = nullptr;
    QTimer *m_timer = nullptr;
};
} // namespace spotty
