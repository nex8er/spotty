/**
 * \file PlotterPanel.cpp
 * \brief Реализация spotty::PlotterPanel.
 */
#include "PlotterPanel.h"

#include "PlotCanvas.h"
#include "PlotWidget.h"
#include "ScaleLimitsDialog.h"
#include "SeriesSwatchDelegate.h"
#include <spotty/data/PlotFormat.h>
#include <spotty/data/PlotModel.h>
#include <spotty/data/PlotFormat.h>
#include <spotty/data/PlotViewState.h>

#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QToolButton>

#include <algorithm>

#include <QColorDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QColorDialog>
#include <QComboBox>
#include <QResizeEvent>
#include <QSplitter>
#include <QItemSelectionModel>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <QVBoxLayout>

namespace spotty {

namespace {

constexpr auto kKeySeparator = "separator";
constexpr auto kKeyCapacity = "capacity";
constexpr auto kKeyProfile = "profile";
constexpr auto kKeySplitter = "panelSplitter";

/**
 * \brief Ёмкость буфера по умолчанию, отсчётов.
 *
 * Прежние двести были размером **всего** буфера: старше двухсотой точки данных просто не
 * существовало, и прокручивать было нечего. Теперь буфер и видимое окно — разные вещи:
 * здесь задают, сколько всего хранить, а сколько из этого видно, решают масштаб и
 * прокрутка. Пятьдесят тысяч отсчётов на шестнадцать колонок — около семи мегабайт.
 */
constexpr int kDefaultCapacity = SampleBuffer::kDefaultCapacity;

/**
 * \brief Колонки таблицы рядов.
 *
 * Последнего значения здесь нет намеренно: панель узкая, семь колонок в неё не влезали и
 * уезжали за край вместе с прокруткой. Текущее значение и так показывает перекрестие на
 * самом графике, а таблица отвечает на другой вопрос — «в каких пределах гуляет ряд».
 */
enum Column {
    ColumnSwatch = 0, ///< Цвет ряда и галочка видимости на нём же.
    ColumnName,
    ColumnMin,
    ColumnAverage,
    ColumnMax,
    ColumnCount,
};

/**
 * \brief Колонки со статистикой — у них общее правило ширины и формата.
 *
 * Порядок «мин, сред, макс» задан владельцем, и он же читается легче прежнего: среднее
 * лежит между своими границами и там, где его и ищет взгляд.
 */
constexpr Column kStatColumns[] = {ColumnMin, ColumnAverage, ColumnMax};

/**
 * \name Пределы ширины колонки статистики, в знакоместах
 *
 * Нижний предел держит пять значащих цифр: восемь знакомест это пять цифр плюс минус,
 * десятичная точка и запас на округление, а знак минуса в счёт цифр не идёт. Верхний —
 * там, где точность упирается в double, и дальнейшее расширение только отбирает место у
 * имени ряда.
 */
/// @{
constexpr int kMinimumStatCharacters = 8;
constexpr int kMaximumStatCharacters = 12;
/// @}


/**
 * \class Populating
 * \brief Поднимает флаг «панель правит себя сама» и возвращает **прежнее** значение.
 *
 * Именно прежнее, а не `false`. Вложенный вызов иначе снимал бы флаг за внешний: наложение
 * профиля переименовывает ряд, переименование поднимает seriesAdded(), тот перестраивает
 * таблицу, перестроение по выходе ставит флаг в `false` — и остаток наложения profiles идёт
 * уже без защиты, снова вызывая наложение. Получалась бесконечная рекурсия и переполнение
 * стека на первом же профиле с заданными именами.
 */
class Populating
{
public:
    explicit Populating(bool &flag)
        : m_flag(flag)
        , m_previous(flag)
    {
        flag = true;
    }

    ~Populating() { m_flag = m_previous; }

    Populating(const Populating &) = delete;
    Populating &operator=(const Populating &) = delete;

private:
    bool &m_flag;
    bool m_previous;
};

/**
 * \brief Ширина области захвата разделителя, px.
 *
 * Связана с отбивкой правила `#plotterSplitter::handle`: 1 сверху, 1 пиксель линии, 1
 * снизу.
 *
 * \note Три пикселя — осознанный выбор владельца в пользу плотной раскладки: захват
 *       занимает место, и лишние пиксели раздвигают график с таблицей. Обычный для
 *       настольных программ размер захвата вдвое-втрое больше, и попадать мышью в три
 *       пикселя труднее. Курсор при этом меняется на всей ширине захвата, поэтому найти
 *       границу по-прежнему можно.
 */
constexpr int kSplitterHandleWidth = 2;

/// \brief Сколько знаков после запятой оставлять среднему.
constexpr int kMeanDecimals = 3;

} // namespace

PlotterPanel::PlotterPanel(IPanelHost *panelHost, PlotModel *model, PlotViewState *view,
                           QWidget *parent)
    : PanelWidget(panelHost, parent)
    , m_model(model)
    , m_view(view)
    , m_store(panelHost->dataDir())
{
    setPanelTitle(tr("Plotter"));
    QVBoxLayout *layout = content();

    // Подсказка убрана: она объясняла очевидное каждый раз, а место в узкой панели
    // дороже. Что рисует плоттер, видно по самому плоттеру.

    // Плоттер целиком, вместе со своим рядом кнопок: тот же композит, что в полосе вместо
    // терминала и в отдельном окне.
    m_plot = new PlotWidget(panelHost, m_model, m_view, PlotWidget::Placement::Panel, this);
    m_plot->canvas()->setMinimumHeight(120);

    // Профили: набор настроек под конкретное устройство. Список плюс две кнопки — добавить
    // и удалить; всё остальное сохраняется само.
    auto *profileRow = new QHBoxLayout;
    // Тот же шаг, что и у остальных рядов с кнопками-значками в приложении — общий
    // IPanelHost::Metric::Gap, а не свой отдельный номер.
    profileRow->setSpacing(host()->metric(IPanelHost::Metric::Gap));

    m_profiles = new QComboBox(this);
    m_profiles->setToolTip(tr("Settings saved for a particular device"));
    profileRow->addWidget(m_profiles, 1);

    const auto makeSmall = [this](char32_t glyph, const QString &tip) {
        auto *button = new QToolButton(this);
        button->setAutoRaise(true);
        button->setIcon(host()->icon(glyph, 16));
        button->setToolTip(tip);
        return button;
    };
    auto *addProfileButton = makeSmall(mdi::Plus, tr("Save the current settings as a profile"));
    auto *removeProfileButton = makeSmall(mdi::Delete, tr("Delete this profile"));
    profileRow->addWidget(addProfileButton);
    profileRow->addWidget(removeProfileButton);
    layout->addLayout(profileRow);

    connect(addProfileButton, &QToolButton::clicked, this, &PlotterPanel::addProfile);
    connect(removeProfileButton, &QToolButton::clicked, this, &PlotterPanel::deleteProfile);
    connect(m_profiles, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        if (m_populating || name.isEmpty() || name == m_currentProfile)
            return;
        m_profileChosenByUser = true;
        m_currentProfile = name;
        applyProfile(m_store.load(name));
        host()->setValue(QLatin1String(kKeyProfile), name);
    });

    // Разделитель между миниатюрой и остальной панелью: сколько отдать графику, а сколько
    // таблице, зависит от задачи — при трёх рядах нужнее график, при двадцати таблица.
    // Профили остаются над ним: их список короткий и от высоты не выигрывает.
    m_splitter = new QSplitter(Qt::Vertical, this);
    // Имя нужно таблице стилей: у этого разделителя, в отличие от оконных, есть своя
    // видимая линия — рамок соседних карточек, которые служат границей в окне, внутри
    // панели нет, и прозрачный захват было бы не найти.
    m_splitter->setObjectName(QStringLiteral("plotterSplitter"));
    // Три пикселя — это 1 + 1 + 1: отбивка из таблицы стилей сверху и снизу и линия между
    // ними. Число связано с margin у #plotterSplitter::handle: меняя одно, менять и второе.
    m_splitter->setHandleWidth(kSplitterHandleWidth);
    // Схлопывать нечего: панель без графика или без таблицы бесполезна, а вернуть
    // схлопнутое можно только попав в захват шириной в пять пикселей.
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_plot);

    auto *below = new QWidget(m_splitter);
    auto *belowLayout = new QVBoxLayout(below);
    belowLayout->setContentsMargins(0, 0, 0, 0);
    belowLayout->setSpacing(layout->spacing());
    m_splitter->addWidget(below);

    // Тянется таблица: график получает ту высоту, которую ему задали, и держит её при
    // изменении размера панели.
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    layout->addWidget(m_splitter, 1);

    auto *form = new QFormLayout;

    m_points = new QSpinBox(this);
    m_points->setRange(100, 1'000'000);
    m_points->setValue(kDefaultCapacity);
    m_points->setSuffix(tr(" samples"));
    m_points->setToolTip(tr("How many samples to keep. What part of them is on screen is "
                            "set by scrolling and zooming the plot itself."));
    form->addRow(tr("Buffer"), m_points);

    belowLayout->addLayout(form);

    // Таблица рядов. Она же легенда: прежде кривые различались только цветом, что и WCAG
    // нарушает, и просто не позволяет понять, какая из них чья.
    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setHorizontalHeaderLabels(
        {QString(), tr("Series"), tr("Min"), tr("Avg"), tr("Max")});
    // Растягивается только имя ряда; остальные колонки ужимаются под содержимое. Иначе
    // равные доли отдают под галочку и квадратик цвета столько же, сколько под число.
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnSwatch,
                                                      QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColumnName, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Выделение нескольких строк — это второй механизм из запроса: две и более выделенные
    // строки сводятся на общую шкалу. Активный ряд задаётся отдельно, «текущей» строкой.
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Правится только имя ряда, и только по двойному щелчку: одиночный по первой колонке
    // переключает видимость, и режим «правка по клику» отнял бы это у пользователя.
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);

    m_swatch = new SeriesSwatchDelegate(m_table);
    m_table->setItemDelegateForColumn(ColumnSwatch, m_swatch);
    // Горизонтальная прокрутка запрещена: в узкой панели она появлялась всегда и прятала
    // половину колонок. Числа при нехватке места сокращаются многоточием — увидеть, что
    // значение не поместилось, лучше, чем не увидеть колонку вовсе.
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setTextElideMode(Qt::ElideRight);
    // Наименьшая высота: иначе разделитель можно утащить так, что от таблицы остаётся
    // полоска заголовка, из которой ничего не прочесть.
    m_table->setMinimumHeight(80);
    belowLayout->addWidget(m_table, 1);

    connect(m_points, &QSpinBox::valueChanged, this, &PlotterPanel::commit);

    // Окно единственное на всю программу, и владеет им плагин: и миниатюра, и полоса
    // просят открыть его одним и тем же сигналом.
    connect(m_plot, &PlotWidget::openInWindowRequested,
            this, &PlotterPanel::openInWindowRequested);

    // Разделитель и выбор оси X правят меню под графиком. Сохраняем по сигналу настройки,
    // а не по changed(): тот приходит на каждый отсчёт, тысячами в секунду.
    connect(m_model, &PlotModel::configurationChanged, this, [this] {
        // Ячейка первой колонки хранит своё состояние галочки и свой цвет, а рисует их
        // делегат. Без обратной записи модель менялась, а квадратик оставался прежним —
        // со стороны это выглядело так, будто галочка не нажимается вовсе.
        refreshSwatches();
        if (m_populating)
            return;
        host()->setValue(QLatin1String(kKeySeparator), QString(m_model->separator()));
        scheduleProfileSave();
    });
    // Режим показа тоже часть профиля, а живёт он в состоянии вида. Слушаем именно
    // modeChanged(), а не changed(): второй приходит и на сдвиг окна, то есть на каждый
    // отсчёт, и профиль писался бы на диск несколько раз в секунду при живом потоке.
    connect(m_view, &PlotViewState::modeChanged, this, [this] {
        if (!m_populating)
            scheduleProfileSave();
    });

    m_profileTimer = new QTimer(this);
    m_profileTimer->setSingleShot(true);
    m_profileTimer->setInterval(kProfileSaveDelayMs);
    connect(m_profileTimer, &QTimer::timeout, this, &PlotterPanel::saveProfile);

    // Состав рядов меняется, когда устройство прислало новую колонку, — тогда и есть по
    // чему подбирать профиль.
    connect(m_model, &PlotModel::seriesAdded, this, &PlotterPanel::autoSelectProfile);
    // Одиночный таймер: когда данные не идут, ничего не тикает.
    m_statisticsTimer = new QTimer(this);
    m_statisticsTimer->setSingleShot(true);
    m_statisticsTimer->setInterval(kStatisticsIntervalMs);
    connect(m_statisticsTimer, &QTimer::timeout, this, &PlotterPanel::refreshStatistics);

    connect(m_model, &PlotModel::seriesAdded, this, &PlotterPanel::rebuildTable);
    // Профиль читается при запуске, когда рядов ещё ноль, — применять там нечего. Ряды
    // появляются потом, с первой строкой устройства, и вот тогда профиль и надо наложить.
    connect(m_model, &PlotModel::seriesAdded, this, &PlotterPanel::applyStoredProfile);
    connect(m_model, &PlotModel::changed, this, &PlotterPanel::scheduleStatistics);

    // Первая колонка: одиночный щелчок переключает видимость, двойной открывает цвет.
    connect(m_swatch, &SeriesSwatchDelegate::visibilityToggled, this, [this](int row) {
        if (row >= 0 && row < m_model->seriesCount())
            m_model->setSeriesVisible(row, !m_model->series(row).visible);
    });
    connect(m_swatch, &SeriesSwatchDelegate::colourRequested, this, &PlotterPanel::pickColour);

    // Правка имени: сигнал приходит и от нашей же перестройки таблицы, поэтому флаг
    // m_populating обязателен — иначе панель писала бы имя обратно в модель на каждый
    // пришедший отсчёт.
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (m_populating || item->column() != ColumnName)
            return;
        m_model->setSeriesName(item->row(), item->text());
    });

    connect(m_table, &QTableWidget::customContextMenuRequested,
            this, &PlotterPanel::showTableMenu);

    // Два независимых механизма из запроса, и Qt даёт под них два готовых канала:
    // «текущая» строка (рамка фокуса) — активный ряд, чья шкала подписана слева;
    // «выделенные» строки (заливка) — группа с общей шкалой. Спорить им не о чем.
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this] {
        if (m_populating)
            return;
        QList<int> rows;
        const QModelIndexList selected = m_table->selectionModel()->selectedRows();
        rows.reserve(selected.size());
        for (const QModelIndex &index : selected)
            rows.append(index.row());
        m_view->setSelectionGroup(rows);
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current) {
                if (!m_populating && current.isValid())
                    m_view->setActiveSeries(current.row());
            });

    // Клик по шкале на графике тоже назначает активный ряд — таблица обязана это
    // отразить, не трогая при этом выделение.
    connect(m_view, &PlotViewState::changed, this, &PlotterPanel::syncSelectionFromView);

    // Подписки на sectionResized здесь намеренно нет. Ширины колонок мы задаём сами, и
    // этот сигнал приходит в ответ на нашу же правку — получалась петля «ширина → сигнал →
    // ширина», которая не сходилась и роняла процесс. Пересчитывать есть смысл ровно
    // тогда, когда меняется то, от чего ширина зависит: размер панели (resizeEvent) и
    // состав имён (rebuildTable).

    // Положение разделителя переживает перезапуск: его подбирают под задачу один раз, и
    // возвращать график к исходной высоте каждый сеанс значило бы отменять этот выбор.
    const QByteArray splitterState =
        host()->value(QLatin1String(kKeySplitter)).toByteArray();
    if (!splitterState.isEmpty())
        m_splitter->restoreState(splitterState);
    else
        m_splitter->setSizes({160, 340});

    connect(m_splitter, &QSplitter::splitterMoved, this, [this] {
        host()->setValue(QLatin1String(kKeySplitter), m_splitter->saveState());
    });

    reloadFromSettings();
    reloadProfiles();
    rebuildTable();
}

void PlotterPanel::reloadFromSettings()
{
    const Populating populating(m_populating);

    const QString separator =
        host()->value(QLatin1String(kKeySeparator), QStringLiteral(",")).toString();
    m_model->setSeparator(separator.isEmpty() ? u',' : separator.at(0));
    m_points->setValue(host()->value(QLatin1String(kKeyCapacity), kDefaultCapacity).toInt());

}

void PlotterPanel::commit()
{
    m_model->setCapacity(m_points->value());

    // Разделитель и ось X теперь меняют меню под графиком, а не поля панели, поэтому
    // сохраняются они по сигналу модели, а не отсюда: см. подписку в конструкторе.
    if (!m_populating)
        host()->setValue(QLatin1String(kKeyCapacity), m_points->value());
}

void PlotterPanel::resizeEvent(QResizeEvent *event)
{
    PanelWidget::resizeEvent(event);
    // Ширина панели изменилась — значит, изменилось и то, сколько цифр в неё влезает.
    updateStatisticsWidth();
}

void PlotterPanel::settingsReset()
{
    reloadFromSettings();
}

void PlotterPanel::rebuildTable()
{
    const Populating populating(m_populating);

    m_table->setRowCount(m_model->seriesCount());
    for (int row = 0; row < m_model->seriesCount(); ++row) {
        const PlotSeries &series = m_model->series(row);

        auto *swatch = m_table->item(row, ColumnSwatch);
        if (!swatch) {
            swatch = new QTableWidgetItem;
            swatch->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
            m_table->setItem(row, ColumnSwatch, swatch);
        }
        swatch->setData(Qt::CheckStateRole, series.visible ? Qt::Checked : Qt::Unchecked);
        swatch->setData(SeriesSwatchDelegate::kColorRole, series.color);
        swatch->setToolTip(tr("Click to show or hide, double-click to change the colour"));

        auto *name = m_table->item(row, ColumnName);
        if (!name) {
            name = new QTableWidgetItem;
            name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
            m_table->setItem(row, ColumnName, name);
        }
        name->setText(series.name);
        name->setToolTip(tr("Double-click to rename"));

        for (const Column column : kStatColumns) {
            if (!m_table->item(row, column)) {
                auto *cell = new QTableWidgetItem;
                cell->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                m_table->setItem(row, column, cell);
            }
        }
    }

    updateStatisticsWidth();
    refreshStatistics();
}

void PlotterPanel::scheduleStatistics()
{
    if (!m_statisticsTimer->isActive())
        m_statisticsTimer->start();
}

void PlotterPanel::refreshStatistics()
{
    // Расхождение чинит перестроение по seriesAdded, а не мы: звать rebuildTable() отсюда
    // значило бы замкнуть цепь «статистика — перестроение — ширины — статистика».
    if (m_table->rowCount() != m_model->seriesCount())
        return;

    const Populating populating(m_populating);
    for (int row = 0; row < m_model->seriesCount(); ++row) {
        // Одна сводка на три числа вместо трёх независимых обходов окна, как было раньше.
        // Пустая колонка отдаёт finiteCount == 0, и в ячейке появляется тире: ноль там
        // читался бы как измеренное значение.
        const SampleBuffer::ColumnStats stats = m_model->samples().stats(row);
        const bool any = stats.finiteCount > 0;

        // fitted(), а не number(): не поместившееся значение обязано стать многоточием, а
        // не обрезком — обрезанное число это другое число, и прочитавший его сделает
        // неверный вывод, ничего не заподозрив.
        m_table->item(row, ColumnMin)
            ->setText(PlotFormat::fitted(any ? stats.minimum : qQNaN(), m_statisticsCharacters));
        // У среднего дробная часть ограничена: оно вычислено, а не измерено, и шесть
        // знаков после запятой у него — шум, которого не было в исходных данных.
        m_table->item(row, ColumnAverage)
            ->setText(PlotFormat::fittedMean(any ? stats.mean : qQNaN(),
                                             m_statisticsCharacters, kMeanDecimals));
        m_table->item(row, ColumnMax)
            ->setText(PlotFormat::fitted(any ? stats.maximum : qQNaN(), m_statisticsCharacters));
    }
}

void PlotterPanel::refreshSwatches()
{
    if (m_table->rowCount() != m_model->seriesCount())
        return;

    const QSignalBlocker blocker(m_table);
    for (int row = 0; row < m_model->seriesCount(); ++row) {
        QTableWidgetItem *item = m_table->item(row, ColumnSwatch);
        if (!item)
            continue;
        const PlotSeries &series = m_model->series(row);
        item->setData(Qt::CheckStateRole, series.visible ? Qt::Checked : Qt::Unchecked);
        item->setData(SeriesSwatchDelegate::kColorRole, series.color);
    }
}

int PlotterPanel::nameColumnNeeds() const
{
    const QFontMetrics metrics = m_table->fontMetrics();

    // Самое длинное имя целиком, плюс заголовок колонки: имя связывает строку с кривой, и
    // обрезанное «volta…» вместо «voltage_in» этой связи не даёт.
    int needed = metrics.horizontalAdvance(m_table->horizontalHeaderItem(ColumnName)->text());
    for (int row = 0; row < m_model->seriesCount(); ++row)
        needed = qMax(needed, metrics.horizontalAdvance(m_model->series(row).name));

    // Отбивки ячейки и запас на рамку выделения.
    return needed + metrics.horizontalAdvance(u'0') * 2;
}

void PlotterPanel::updateStatisticsWidth()
{
    // Порядок уступок задан владельцем: при сужении панели первой сжимается статистика — до
    // пяти значащих цифр, — и только когда ей уже некуда, начинает уступать имя. Поэтому
    // ширина статистики считается от того, что **останется** после целого имени.
    //
    // Флаг держится на весь метод, а не только вокруг установки ширин. Имя растянуто, и
    // смена ширины статистики меняет его ширину тоже, отчего sectionResized приходит сюда
    // же — и приходит не только на те колонки, что мы трогали. Без сплошной защиты это
    // давало каскад вызовов, который вместе с пересчётом статистики сходился в бесконечную
    // рекурсию и переполнение стека.
    if (m_adjustingColumns)
        return;
    m_adjustingColumns = true;

    const int zero = qMax(1, m_table->fontMetrics().horizontalAdvance(u'0'));
    const int available = m_table->viewport()->width() - m_table->columnWidth(ColumnSwatch);

    if (available > 0) {
        const int forStatistics = available - nameColumnNeeds();
        const int characters =
            qBound(kMinimumStatCharacters,
                   forStatistics / (int(std::size(kStatColumns)) * zero),
                   kMaximumStatCharacters);

        const int width = characters * zero;
        for (const Column column : kStatColumns)
            m_table->setColumnWidth(column, width);

        if (characters != m_statisticsCharacters) {
            m_statisticsCharacters = characters;
            // Через таймер, а не прямым вызовом: пересчёт статистики умеет позвать
            // перестроение таблицы, а то — снова сюда. Отложенный вызов разрывает цепь.
            scheduleStatistics();
        }
    }

    m_adjustingColumns = false;
}

void PlotterPanel::syncSelectionFromView()
{
    if (m_table->rowCount() != m_model->seriesCount())
        return;

    // Сверка перед правкой обязательна: этот метод висит на PlotViewState::changed, а тот
    // приходит на каждый отсчёт. Переставлять выделение по нему без проверки значило бы
    // трогать таблицу тысячи раз в секунду и отбирать у пользователя правку имени.
    QList<int> wanted = m_view->selectionGroup();
    std::sort(wanted.begin(), wanted.end());

    QList<int> shown;
    const QModelIndexList selected = m_table->selectionModel()->selectedRows();
    shown.reserve(selected.size());
    for (const QModelIndex &index : selected)
        shown.append(index.row());
    std::sort(shown.begin(), shown.end());

    const int active = m_view->activeSeries();
    const bool sameSelection = wanted == shown;
    const bool sameCurrent = m_table->currentRow() == active || active < 0;
    if (sameSelection && sameCurrent)
        return;

    const QSignalBlocker blocker(m_table->selectionModel());

    if (!sameSelection) {
        QItemSelection selection;
        for (const int row : wanted) {
            if (row < 0 || row >= m_table->rowCount())
                continue;
            selection.select(m_table->model()->index(row, 0),
                             m_table->model()->index(row, ColumnCount - 1));
        }
        m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    }

    if (!sameCurrent && active >= 0 && active < m_table->rowCount()) {
        // NoUpdate: «текущая» строка ставится, не трогая выделение. Qt различает эти два
        // состояния изначально — рамка фокуса против заливки, — и на этом различии и
        // держатся два независимых механизма: активная ось и группа с общей шкалой.
        m_table->selectionModel()->setCurrentIndex(
            m_table->model()->index(active, ColumnName), QItemSelectionModel::NoUpdate);
    }
}

void PlotterPanel::pickColour(int row)
{
    if (row < 0 || row >= m_model->seriesCount())
        return;

    const QColor chosen = QColorDialog::getColor(
        QColor::fromRgba(m_model->series(row).color), window(), tr("Series colour"));
    // Отменённый диалог отдаёт недействительный цвет: перестраивать таблицу тогда незачем.
    if (chosen.isValid())
        m_model->setSeriesColor(row, chosen.rgba());
}

void PlotterPanel::showTableMenu(const QPoint &at)
{
    const int row = m_table->rowAt(at.y());
    if (row < 0 || row >= m_model->seriesCount())
        return;

    QMenu menu(m_table);
    connect(menu.addAction(tr("Change colour…")), &QAction::triggered, this,
            [this, row] { pickColour(row); });
    connect(menu.addAction(tr("Rename…")), &QAction::triggered, this, [this, row] {
        m_table->editItem(m_table->item(row, ColumnName));
    });

    menu.addSeparator();

    // Пределы шкалы задаются на ряд и переживают всё остальное: их не перебивает ни
    // автомасштаб, ни общая шкала группы.
    const PlotSeries &series = m_model->series(row);
    QAction *limits = menu.addAction(series.hasCustomRange ? tr("Change scale limits…")
                                                           : tr("Set scale limits…"));
    connect(limits, &QAction::triggered, this, [this, row] { editRange(row); });

    if (series.hasCustomRange) {
        connect(menu.addAction(tr("Back to automatic scale")), &QAction::triggered, this,
                [this, row] { m_model->setSeriesRange(row, false, 0.0, 1.0); });
    }

    menu.addSeparator();

    // Очистка одной колонки, а не всего: остальные ряды при этом продолжают идти.
    connect(menu.addAction(tr("Clear this series only")), &QAction::triggered, this,
            [this, row] { m_model->clearColumn(row); });

    menu.exec(m_table->viewport()->mapToGlobal(at));
}

void PlotterPanel::editRange(int row)
{
    if (row < 0 || row >= m_model->seriesCount())
        return;

    const PlotSeries &series = m_model->series(row);
    const SampleBuffer::ColumnStats stats = m_model->samples().stats(row);
    const bool measured = stats.finiteCount > 0;

    // Начальные значения — нынешние пределы, а при их отсутствии измеренные: так окно
    // отвечает на вопрос «а какие они сейчас» ещё до того, как его зададут.
    const double presetMinimum =
        series.hasCustomRange ? series.customMinimum : (measured ? stats.minimum : 0.0);
    const double presetMaximum =
        series.hasCustomRange ? series.customMaximum : (measured ? stats.maximum : 1.0);

    ScaleLimitsDialog dialog(series.name, presetMinimum, presetMaximum, stats.minimum,
                             stats.maximum, measured, window());
    if (dialog.exec() != QDialog::Accepted)
        return;

    m_model->setSeriesRange(row, true, dialog.minimum(), dialog.maximum());
}

namespace {

/// \brief Имя режима для файла профиля.
QString modeKey(PlotViewState::Mode mode)
{
    switch (mode) {
    case PlotViewState::Mode::Xy:         return QStringLiteral("xy");
    case PlotViewState::Mode::Histogram:  return QStringLiteral("histogram");
    case PlotViewState::Mode::Cumulative: return QStringLiteral("cumulative");
    case PlotViewState::Mode::Spectrum:   return QStringLiteral("spectrum");
    case PlotViewState::Mode::MultiPlot:  return QStringLiteral("multiplot");
    case PlotViewState::Mode::TimeSeries: break;
    }
    return QStringLiteral("timeseries");
}

/// \brief Режим по имени из файла профиля.
PlotViewState::Mode modeFromKey(const QString &key)
{
    if (key == QLatin1String("xy"))         return PlotViewState::Mode::Xy;
    if (key == QLatin1String("histogram"))  return PlotViewState::Mode::Histogram;
    if (key == QLatin1String("cumulative")) return PlotViewState::Mode::Cumulative;
    if (key == QLatin1String("spectrum"))   return PlotViewState::Mode::Spectrum;
    if (key == QLatin1String("multiplot"))  return PlotViewState::Mode::MultiPlot;
    return PlotViewState::Mode::TimeSeries;
}

} // namespace

void PlotterPanel::reloadProfiles()
{
    const QSignalBlocker blocker(m_profiles);
    m_profiles->clear();
    m_profiles->addItems(m_store.profiles());

    const QString remembered = host()->value(QLatin1String(kKeyProfile)).toString();
    const int index = m_profiles->findText(remembered);
    if (index >= 0) {
        m_profiles->setCurrentIndex(index);
        m_currentProfile = remembered;
        m_profileChosenByUser = true;
        applyProfile(m_store.load(remembered));
    } else {
        m_profiles->setCurrentIndex(-1);
    }
}

PlotProfile PlotterPanel::currentProfile(const QString &name) const
{
    PlotProfile profile;
    profile.name = name;
    profile.separator = QString(m_model->separator());
    profile.xAxis = m_model->xAxisSeries();
    profile.capacity = m_model->capacity();
    profile.mode = modeKey(m_view->mode());
    profile.lastUsed = QDateTime::currentDateTimeUtc();

    profile.series.reserve(m_model->seriesCount());
    for (int i = 0; i < m_model->seriesCount(); ++i) {
        const PlotSeries &series = m_model->series(i);
        PlotProfileSeries stored;
        stored.name = series.name;
        stored.nameIsCustom = series.nameIsCustom;
        stored.color = series.color;
        stored.visible = series.visible;
        stored.hasCustomRange = series.hasCustomRange;
        stored.customMinimum = series.customMinimum;
        stored.customMaximum = series.customMaximum;
        profile.series.append(stored);
    }
    return profile;
}

void PlotterPanel::applyProfile(const PlotProfile &profile)
{
    if (profile.name.isEmpty())
        return;

    // Флаг обязателен: применение профиля правит модель, а правка модели просит сохранить
    // профиль — без него применение немедленно переписало бы то, что только что прочитали.
    // Область видимости охватывает весь метод: внутри вызывается перестроение таблицы,
    // которое тоже поднимает флаг и обязано вернуть его поднятым, а не снятым.
    {
        const Populating populating(m_populating);

    m_model->setSeparator(profile.separator.isEmpty() ? u',' : profile.separator.at(0));
    m_model->setCapacity(profile.capacity);
    m_points->setValue(profile.capacity);
    m_view->setMode(modeFromKey(profile.mode));

    for (int i = 0; i < profile.series.size() && i < m_model->seriesCount(); ++i) {
        const PlotProfileSeries &stored = profile.series.at(i);
        m_model->setSeriesColor(i, stored.color);
        m_model->setSeriesVisible(i, stored.visible);
        if (stored.nameIsCustom)
            m_model->setSeriesName(i, stored.name);
        m_model->setSeriesRange(i, stored.hasCustomRange, stored.customMinimum,
                                stored.customMaximum);
    }

    // Ось X назначается после рядов: до их появления номер колонки не с чем сверять, и
    // модель отвергла бы его как выходящий за границы.
    m_model->setXAxisSeries(profile.xAxis);

    }

    m_appliedSeriesCount = m_model->seriesCount();
    rebuildTable();
}

void PlotterPanel::applyStoredProfile()
{
    // Ряды приходят по одному, и профиль накладывается заново на каждую новую колонку:
    // иначе шестая колонка семиколоночного потока осталась бы с цветом по умолчанию.
    // Флаг обязателен: наложение переименовывает ряды, setSeriesName() испускает
    // seriesAdded(), а тот приводит сюда же — без проверки получалась бы бесконечная
    // рекурсия и переполнение стека на первом же профиле с заданными именами.
    if (m_populating || m_currentProfile.isEmpty() || m_model->seriesCount() == 0)
        return;
    if (m_appliedSeriesCount >= m_model->seriesCount())
        return;

    const PlotProfile profile = m_store.load(m_currentProfile);
    if (profile.series.isEmpty())
        return;

    applyProfile(profile);
}

void PlotterPanel::scheduleProfileSave()
{
    if (m_currentProfile.isEmpty())
        return;
    if (!m_profileTimer->isActive())
        m_profileTimer->start();
}

void PlotterPanel::saveProfile()
{
    if (m_currentProfile.isEmpty())
        return;
    m_store.save(currentProfile(m_currentProfile));
}

void PlotterPanel::addProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(window(), tr("New profile"),
                                               tr("Profile name:"), QLineEdit::Normal,
                                               host()->interfaceAlias(), &ok);
    if (!ok || name.isEmpty())
        return;

    if (!PlotProfileStore::isValidName(name)) {
        host()->showStatusMessage(tr("That name cannot be used for a file"));
        return;
    }

    if (!m_store.save(currentProfile(name))) {
        host()->showStatusMessage(tr("Could not save the profile"));
        return;
    }

    m_currentProfile = name;
    m_profileChosenByUser = true;
    host()->setValue(QLatin1String(kKeyProfile), name);
    reloadProfiles();
}

void PlotterPanel::deleteProfile()
{
    const QString name = m_profiles->currentText();
    if (name.isEmpty())
        return;

    m_store.remove(name);
    m_currentProfile.clear();
    m_profileChosenByUser = false;
    host()->setValue(QLatin1String(kKeyProfile), QString());
    reloadProfiles();
}

void PlotterPanel::autoSelectProfile()
{
    // Выбранный человеком профиль не трогаем: иначе автоподбор отменял бы его выбор при
    // каждой новой колонке потока.
    if (m_profileChosenByUser || m_model->seriesCount() == 0)
        return;

    const QString best = m_store.bestMatch(m_model->seriesCount(), m_model->seriesNames());
    if (best.isEmpty() || best == m_currentProfile)
        return;

    m_currentProfile = best;
    applyProfile(m_store.load(best));

    const QSignalBlocker blocker(m_profiles);
    m_profiles->setCurrentIndex(m_profiles->findText(best));
    host()->showStatusMessage(tr("Plotter profile: %1").arg(best));
}

} // namespace spotty
