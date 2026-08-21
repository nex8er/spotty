/**
 * \file JsonTreeModel.h
 * \brief Дерево путей JSON: последнее значение и частота обновления у каждого поля.
 */
#pragma once

#include <spotty/api/SpottyApiExport.h>

#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QString>

namespace spotty {

/**
 * \enum JsonNodeKind
 * \brief Что за значение лежит в узле.
 *
 * \warning Значения дописываются только в конец: перечисление входит в публичный ABI,
 *          ровно как spotty::IPanelHost::ColorRole.
 */
enum class JsonNodeKind {
    Object,  ///< Ветка; собственного значения нет.
    Array,   ///< Ветка-массив; собственного значения нет.
    Number,
    String,
    Bool,
    Null,
    Compact, ///< Свёрнутое поддерево или массив примитивов — значение готовой строкой.
};

/**
 * \struct JsonNode
 * \brief Один узел дерева.
 *
 * \par Почему имя — один сегмент, а не полный путь
 *
 * Полный путь пришлось бы склеивать на каждый лист каждого документа, то есть тысячи раз в
 * секунду, и хранить вторым экземпляром. Навигация идёт по уровням, а путь целиком строится
 * по требованию — он нужен человеку в меню «скопировать путь», а не программе.
 *
 * \note Указатели на JsonNode хранить нельзя: узлы лежат в QList, который перевыделяется
 *       при росте. Наружу и внутрь ходят только индексы, и они устойчивы навсегда —
 *       spotty::JsonTreeModel::pruneStale() освобождает слоты, а не сдвигает их.
 */
struct JsonNode
{
    QString name;                   ///< Один сегмент пути.
    int parent = -1;                ///< Индекс родителя; -1 только у корня.
    QList<int> children;            ///< Дети в порядке появления.
    QHash<QString, int> childIndex; ///< Сегмент → индекс: у ветки с тысячей детей поиск за O(1).
    JsonNodeKind kind = JsonNodeKind::Object;
    QString value; ///< Готовый текст листа; у веток пуст.
    int depth = 0;

    /// \name Частота обновления
    /// @{
    qint64 lastUpdateNs = 0;   ///< Когда поле пришло в последний раз.
    qint64 lastChangeNs = 0;   ///< Когда изменился текст значения — для вспышки.
    double avgIntervalNs = 0.0; ///< Сглаженный интервал между приходами.
    quint64 updates = 0;        ///< Сколько раз поле приходило всего.
    quint64 epoch = 0;          ///< Документ, в котором узел уже засчитан.
    /// @}

    bool alive = true; ///< Ложь у освобождённого слота: надгробие, см. pruneStale().
};

/**
 * \class JsonTreeModel
 * \brief Накопитель дерева путей: что пришло, когда и как часто.
 *
 * \par Единица наблюдения — путь, а не документ
 *
 * Устройства шлют документы разной формы, и таблица «объект — строка, ключи — колонки» на
 * таком потоке расползается: колонки появляются и исчезают, а вопрос «какое сейчас
 * значение поля» остаётся без ответа. Здесь строка — это поле, найденное по пути, и она
 * живёт, пока живёт дерево, показывая последнее пришедшее значение.
 *
 * \par Частота затухает сама, без таймера
 *
 * Замершее поле обязано быть видно, а отдельной колонки «когда обновлялось» нет — эту
 * работу делает частота. Она считается не счётчиком в окне, а сглаженным интервалом между
 * приходами, и при чтении делится на **большее** из среднего интервала и времени простоя.
 * Пока данные идут, простой не дорастает до среднего, и число на экране стоит неподвижно;
 * стоит потоку прекратиться — частота начинает падать как `1/простой`, непрерывно, без
 * скачка в точке перехода.
 *
 * Важно, что затухание — чистая функция от узла и текущего времени. Никакой таймер его не
 * поддерживает, поэтому состояние не может разъехаться с деревом: пропущенный тик,
 * свёрнутая ветка или закрытая панель не оставляют узел с залипшим числом.
 *
 * \par Владелец
 *
 * Модель принадлежит плагину, а не панели: панель закрывают, а накопление продолжается.
 * Та же причина, что у spotty::PlotModel.
 *
 * \note Ни одного типа из QtGui: слой линкуется только с Qt6::Core. Наружу отдаются числа
 *       (частота в герцах, наносекунды с последнего изменения), а цвет вспышки и ширину
 *       полоски считает делегат в слое виджетов — как у spotty::PlotSeries с его `quint32`.
 */
class SPOTTY_API_EXPORT JsonTreeModel : public QObject
{
    Q_OBJECT

public:
    explicit JsonTreeModel(QObject *parent = nullptr);

    /// \name Умолчания пределов
    /// @{
    static constexpr int kDefaultMaxNodes = 5000;
    static constexpr int kDefaultMaxDepth = 12;
    static constexpr int kDefaultMaxChildren = 512;
    /// @}

    /// \brief Имя ветки для элемента массива, у которого нет ключа идентификации.
    static QString noIdentityName();

    /**
     * \brief Разложить документ по дереву.
     * \param monotonicNs Отметка чтения строки, из которой собран документ.
     * \return `false`, если документ не объект и не массив.
     */
    bool feed(const QJsonDocument &document, qint64 monotonicNs);

    /// \name Чтение дерева
    /// @{

    /// \brief Сколько узлов живо, не считая корня.
    int nodeCount() const { return m_liveNodes; }

    /// \brief Размер арены; индексы узлов лежат в `[0, arenaSize)`, включая надгробия.
    int arenaSize() const { return int(m_nodes.size()); }

    bool isValidNode(int index) const;
    const JsonNode &node(int index) const { return m_nodes.at(index); }

    /// \brief Дети корня — с них начинается обход дерева.
    QList<int> rootChildren() const { return m_nodes.at(0).children; }

    /// \brief Полный путь узла через точку; строится по требованию.
    QString path(int index) const;

    /// @}
    /// \name Частота и свежесть
    /// @{

    /// \return Частота обновления в герцах; 0, пока поле приходило меньше двух раз.
    double rate(int index, qint64 nowNs) const;

    /**
     * \brief Наибольшая частота среди живых узлов — масштаб для полоски.
     *
     * Считается обходом арены не чаще раза в 200 мс, дальше отдаётся кэш. Обход тысяч
     * чисел стоит микросекунды; дорого было бы не считать, а писать в виджет.
     */
    double maxRate(qint64 nowNs) const;

    /// \brief Поле молчит заметно дольше своего обычного интервала.
    bool isStale(int index, qint64 nowNs) const;

    /// \brief Сколько прошло с последней смены **текста** значения; для вспышки.
    qint64 nsSinceChange(int index, qint64 nowNs) const;

    /// @}
    /// \name Настройки
    /// @{

    /**
     * \brief Поле, по которому различаются элементы массива объектов.
     *
     * Пустая строка — массив схлопывается в одну ветку: поля показывают значение из
     * последнего элемента, а частота считается один раз на весь документ. Заданный ключ
     * разводит элементы по веткам, у каждой своя частота.
     *
     * Автоопределения нет намеренно: угаданный ключ, оказавшийся неуникальным, тихо
     * склеивал бы разные объекты в один, и заметить это по экрану невозможно.
     *
     * \note Смена ключа роняет дерево: форма другая, пересобрать старое под неё нельзя.
     */
    void setIdentityKey(const QString &key);
    QString identityKey() const { return m_identityKey; }

    void setMaxNodes(int nodes);
    int maxNodes() const { return m_maxNodes; }

    void setMaxDepth(int depth);
    int maxDepth() const { return m_maxDepth; }

    void setMaxChildren(int children);
    int maxChildren() const { return m_maxChildren; }

    /// @}
    /// \name Состояние потока
    /// @{

    /// \brief Достигнут какой-либо предел, и часть путей в дерево не попала.
    bool truncated() const { return m_truncated; }

    /// \brief Сколько путей отвергнуто пределами.
    quint64 rejectedPaths() const { return m_rejected; }

    quint64 documents() const { return m_documents; }

    /// @}
    /// \name Изменения и уборка
    /// @{

    /**
     * \brief Забрать список узлов, чьё значение изменилось, и очистить его.
     *
     * Вид перечитывает только их, а не всё дерево: при сотне полей и десяти документах в
     * секунду полный обход был бы работой впустую.
     */
    QList<int> takeDirty();

    /// \brief Уронить дерево целиком; настройки и счётчики документов остаются.
    void clear();

    /**
     * \brief Убрать замершие ветки вместе с потомками.
     * \return Индексы удалённых узлов — виду, чтобы вычистить свои элементы.
     *
     * Слоты не сдвигаются, а помечаются надгробием и кладутся в свободный список: сдвиг
     * переназначил бы индексы, а на их устойчивости держится всё, что смотрит на модель
     * снаружи.
     */
    QList<int> pruneStale(qint64 nowNs);

    /// @}

Q_SIGNALS:
    /// \brief Пришёл документ: значения могли измениться.
    void changed();

    /// \brief В дереве появились узлы — виду нужно создать элементы.
    void nodesAdded();

    /// \brief Узлы удалены; индексы больше не действительны.
    void nodesRemoved(QList<int> nodes);

    /// \brief Дерево сброшено целиком — вид обязан построиться заново.
    void modelReset();

private:
    /// \brief Взять слот под новый узел: из свободного списка либо в конце арены.
    int allocateNode();

    /**
     * \brief Найти ребёнка по имени или создать.
     * \return -1, если помешал предел; вызывающий обязан это проверить.
     */
    int ensureChild(int parent, const QString &name, JsonNodeKind kind, int depth);

    /// \brief Засчитать приход узла в этом документе; повторный за тот же документ не считается.
    void touch(int index, qint64 monotonicNs);

    /// \brief Записать текст значения; отметку изменения двигает, только если текст другой.
    void setNodeValue(int index, const QString &text, qint64 monotonicNs);

    /// \name Разбор
    /// @{
    void mergeObject(int parent, const QJsonObject &object, qint64 ns, int depth);
    void mergeValue(int parent, const QString &name, const QJsonValue &value, qint64 ns,
                    int depth);
    void mergeArray(int node, const QJsonArray &array, qint64 ns, int depth);
    /// @}

    /// \brief Освободить узел и всё под ним, дописав индексы в \p removed.
    void releaseSubtree(int index, QList<int> *removed);

    /// \brief Все ли элементы массива — скаляры: такой массив показывается одной строкой.
    static bool isScalarArray(const QJsonArray &array);

    /// \brief Текст скаляра для показа.
    static QString formatScalar(const QJsonValue &value);

    /// \brief Компактный JSON, укороченный до читаемой длины.
    static QString compactText(const QJsonValue &value);

    static JsonNodeKind kindOf(const QJsonValue &value);

    /**
     * \brief Арена узлов; индекс 0 — невидимый корень.
     *
     * Корень существует, чтобы у разбора не было развилки «первый уровень или не первый»:
     * документ вливается в него так же, как любой вложенный объект в свою ветку.
     */
    QList<JsonNode> m_nodes;
    QList<int> m_freeSlots;
    int m_liveNodes = 0;

    QList<int> m_dirty;

    QString m_identityKey;
    int m_maxNodes = kDefaultMaxNodes;
    int m_maxDepth = kDefaultMaxDepth;
    int m_maxChildren = kDefaultMaxChildren;

    quint64 m_epoch = 0;
    quint64 m_documents = 0;
    quint64 m_rejected = 0;
    bool m_truncated = false;

    /// \brief Появились ли узлы в текущем feed() — чтобы не слать nodesAdded() впустую.
    bool m_added = false;

    /// \name Кэш наибольшей частоты
    /// @{
    mutable double m_maxRate = 0.0;
    mutable qint64 m_maxRateAtNs = 0;
    mutable bool m_maxRateValid = false;
    /// @}
};

} // namespace spotty
