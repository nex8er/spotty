/**
 * \file PlotterPlugin.h
 * \brief Плагин: график из CSV поверх терминала.
 */
#pragma once

#include <spotty/ui/IPanelPlugin.h>

namespace spotty {

class PlotModel;
class PlotViewState;

/**
 * \class PlotterPlugin
 * \brief Две панели: настройки в рейке и сам график слоем поверх вывода.
 *
 * \par Что он показывает про API
 *
 * Это первый плагин, написанный не автором SDK и не переделанный из встроенной панели.
 * Он проверяет три вещи сразу: что слой поверх собственной отрисовки терминала работает,
 * что панель может рисовать сама, не зная ни одного типа из `spotty-core`, и что два
 * виджета одного плагина умеют делить общее состояние.
 *
 * \par Что он вскрыл
 *
 * Панели нужны **разобранные строки, а не байты**. CSV приходит порциями, границы которых
 * к переводам строк отношения не имеют, и повторять внутри плагина то, что уже делают
 * пакетизатор и буфер терминала, было бы нелепо. Отсюда в spotty::IPanelHost появились
 * line(), firstLineNumber(), nextLineNumber() и сигнал terminalLinesAppended(). Без этого
 * плагина API остался бы «байтовым».
 */
class PlotterPlugin : public QObject, public IPanelPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID SPOTTY_PANEL_PLUGIN_IID FILE "plotter.json")
    Q_INTERFACES(spotty::IPanelPlugin)

public:
    PlotterPlugin();
    ~PlotterPlugin() override;

    QString pluginId() const override { return QStringLiteral("plotter"); }
    QString displayName() const override { return tr("Plotter"); }

    QList<PanelDescriptor> panels() const override;
    QWidget *createPanel(const QString &panelId, IPanelHost *host, QWidget *parent) override;

private:
    /**
     * \brief Общая модель обеих панелей.
     *
     * Владеет плагин, а не панель: график должен продолжать накапливать данные, даже
     * когда страница настроек не выбрана в рейке и её виджет не на экране.
     */
    PlotModel *m_model = nullptr;

    /// \brief Состояние вида, общее на все три холста: миниатюру, полосу и окно.
    PlotViewState *m_view = nullptr;

    /// \brief Хост, с которым связана подписка на строки; один на плагин.
    IPanelHost *m_host = nullptr;

    /// \brief Номер строки, до которой уже разобрано.
    qint64 m_nextLine = 0;
};

} // namespace spotty
