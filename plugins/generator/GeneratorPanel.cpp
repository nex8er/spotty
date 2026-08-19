/**
 * \file GeneratorPanel.cpp
 * \brief Реализация spotty::GeneratorPanel.
 */
#include "GeneratorPanel.h"

#include <spotty/data/DataCodec.h>
#include <spotty/ui/IPanelHost.h>
#include <spotty/ui/MdiCodepoints.h>

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QIntValidator>
#include <QListView>
#include <QLocale>
#include <QPlainTextEdit>
#include <QScopedValueRollback>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <array>
#include <utility>

namespace spotty {

namespace {

constexpr int kSignalMode = -1;
constexpr int kToolGlyphSize = 18;
constexpr int kWaveGlyphSize = 16;
constexpr int kMaxIntervalMs = 3'600'000;
constexpr std::array kIntervalsMs = {1,   2,    5,    10,   25,    50,    100,
                                    250, 500,  1000, 2000, 5000,  10000, 30000, 60000};
constexpr int kAmplitudeScale = 100;
constexpr int kOffsetScale = 100;
constexpr int kDutyCycleScale = 10;

constexpr char kKeyMode[] = "mode";
constexpr char kKeyWaveform[] = "waveform";
constexpr char kKeyLength[] = "length";
constexpr char kKeyCounterStart[] = "counterStart";
constexpr char kKeyCounterIncrement[] = "counterIncrement";
constexpr char kKeyRampStart[] = "rampStart";
constexpr char kKeyRampIncrement[] = "rampIncrement";
constexpr char kKeyRandomMinimum[] = "randomMinimum";
constexpr char kKeyRandomMaximum[] = "randomMaximum";
constexpr char kKeyFixedValue[] = "fixedValue";
constexpr char kKeyWavePeriod[] = "wavePeriod";
constexpr char kKeyAmplitude[] = "amplitude";
constexpr char kKeyOffset[] = "offset";
constexpr char kKeyDutyCycle[] = "dutyCycle";
constexpr char kKeyPrecision[] = "precision";
constexpr char kKeyPrefix[] = "prefix";
constexpr char kKeyInterval[] = "interval";
constexpr char kKeyPreviewHex[] = "previewHex";

constexpr std::array kWavePatterns = {
    DataGenerator::Pattern::Sine,
    DataGenerator::Pattern::Square,
    DataGenerator::Pattern::Triangle,
    DataGenerator::Pattern::Sawtooth,
};

constexpr std::array<char32_t, 4> kWaveGlyphs = {
    mdi::SineWave,
    mdi::SquareWave,
    mdi::TriangleWave,
    mdi::SawtoothWave,
};

/// \brief Создать подпись формы и запомнить её для единой ширины колонки.
QLabel *formLabel(const QString &text, QWidget *parent, QList<QLabel *> *labels)
{
    auto *label = new QLabel(text, parent);
    labels->append(label);
    return label;
}

/// \brief На узкой рейке подпись идёт над полем, а не отнимает у него полезную ширину.
void configureSimpleForm(QFormLayout *form)
{
    form->setRowWrapPolicy(QFormLayout::WrapAllRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}

/// \brief Ползунок и числовое поле в одной строке параметра: быстрый подбор и точный ввод.
QWidget *sliderRow(QWidget *parent, QSlider *slider, QWidget *editor)
{
    auto *row = new QWidget(parent);
    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(6);
    rowLayout->addWidget(slider, 1);
    rowLayout->addWidget(editor);
    return row;
}

/// \brief Ширина числового поля ровно под семь знаков и кнопки изменения значения.
void setSevenDigitWidth(QAbstractSpinBox *spin)
{
    spin->setFixedWidth(spin->fontMetrics().horizontalAdvance(QStringLiteral("-000000")) + 42);
}

/// \brief Подпись готового периода: после секунды единица крупнее и читается быстрее.
QString intervalLabel(int milliseconds)
{
    if (milliseconds < 1000)
        return GeneratorPanel::tr("%1 ms").arg(milliseconds);
    return GeneratorPanel::tr("%1 s").arg(double(milliseconds) / 1000.0, 0, 'g', 3);
}

} // namespace

GeneratorPanel::GeneratorPanel(IPanelHost *panelHost, QWidget *parent)
    : PanelWidget(panelHost, parent)
{
    setPanelTitle(tr("Generator"));
    QVBoxLayout *layout = content();
    QList<QLabel *> parameterLabels;

    m_mode = new QComboBox(this);
    m_mode->setObjectName(QStringLiteral("generatorMode"));
    m_mode->addItem(DataGenerator::patternName(DataGenerator::Pattern::Counter),
                    int(DataGenerator::Pattern::Counter));
    m_mode->addItem(DataGenerator::patternName(DataGenerator::Pattern::Ramp),
                    int(DataGenerator::Pattern::Ramp));
    m_mode->addItem(DataGenerator::patternName(DataGenerator::Pattern::Random),
                    int(DataGenerator::Pattern::Random));
    m_mode->addItem(DataGenerator::patternName(DataGenerator::Pattern::Fixed),
                    int(DataGenerator::Pattern::Fixed));
    m_mode->addItem(DataGenerator::patternName(DataGenerator::Pattern::AsciiText),
                    int(DataGenerator::Pattern::AsciiText));
    m_mode->addItem(tr("Signal generator"), kSignalMode);
    layout->addWidget(m_mode);

    auto *parameters = new QGroupBox(tr("Parameters"), this);
    parameters->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *parametersLayout = new QVBoxLayout(parameters);
    m_modeControls = new QStackedWidget(parameters);
    m_modeControls->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    parametersLayout->addWidget(m_modeControls);
    layout->addWidget(parameters);

    auto *counterPage = new QWidget(m_modeControls);
    auto *counterForm = new QFormLayout(counterPage);
    configureSimpleForm(counterForm);
    m_length = new QSpinBox(counterPage);
    m_length->setObjectName(QStringLiteral("length"));
    m_length->setRange(1, 65536);
    m_length->setValue(16);
    m_length->setSuffix(tr(" values"));
    counterForm->addRow(formLabel(tr("Length"), counterPage, &parameterLabels), m_length);

    m_counterStart = new QSpinBox(counterPage);
    m_counterStart->setObjectName(QStringLiteral("counterStart"));
    m_counterStart->setRange(-1'000'000'000, 1'000'000'000);
    counterForm->addRow(formLabel(tr("Initial value"), counterPage, &parameterLabels),
                        m_counterStart);

    m_counterIncrement = new QSpinBox(counterPage);
    m_counterIncrement->setObjectName(QStringLiteral("counterIncrement"));
    m_counterIncrement->setRange(-1'000'000'000, 1'000'000'000);
    m_counterIncrement->setValue(1);
    counterForm->addRow(formLabel(tr("Increment"), counterPage, &parameterLabels),
                        m_counterIncrement);
    m_modeControls->addWidget(counterPage);

    auto *rampPage = new QWidget(m_modeControls);
    auto *rampForm = new QFormLayout(rampPage);
    configureSimpleForm(rampForm);
    auto *rampLength = new QSpinBox(rampPage);
    rampLength->setRange(1, 65536);
    rampLength->setValue(16);
    rampLength->setSuffix(tr(" values"));
    rampForm->addRow(formLabel(tr("Length"), rampPage, &parameterLabels), rampLength);
    connect(rampLength, &QSpinBox::valueChanged, m_length, &QSpinBox::setValue);
    connect(m_length, &QSpinBox::valueChanged, rampLength, &QSpinBox::setValue);

    m_rampStart = new QSpinBox(rampPage);
    m_rampStart->setObjectName(QStringLiteral("rampStart"));
    m_rampStart->setRange(0, 255);
    m_rampStart->setDisplayIntegerBase(16);
    m_rampStart->setPrefix(QStringLiteral("0x"));
    rampForm->addRow(formLabel(tr("Initial value"), rampPage, &parameterLabels), m_rampStart);

    m_rampIncrement = new QSpinBox(rampPage);
    m_rampIncrement->setObjectName(QStringLiteral("rampIncrement"));
    m_rampIncrement->setRange(-255, 255);
    m_rampIncrement->setValue(1);
    rampForm->addRow(formLabel(tr("Increment"), rampPage, &parameterLabels),
                     m_rampIncrement);
    m_modeControls->addWidget(rampPage);

    auto *randomPage = new QWidget(m_modeControls);
    auto *randomForm = new QFormLayout(randomPage);
    configureSimpleForm(randomForm);
    auto *randomLength = new QSpinBox(randomPage);
    randomLength->setRange(1, 65536);
    randomLength->setValue(16);
    randomLength->setSuffix(tr(" values"));
    randomForm->addRow(formLabel(tr("Length"), randomPage, &parameterLabels), randomLength);
    connect(randomLength, &QSpinBox::valueChanged, m_length, &QSpinBox::setValue);
    connect(m_length, &QSpinBox::valueChanged, randomLength, &QSpinBox::setValue);

    m_randomMinimum = new QSpinBox(randomPage);
    m_randomMinimum->setObjectName(QStringLiteral("randomMinimum"));
    m_randomMinimum->setRange(0, 255);
    m_randomMinimum->setToolTip(tr("Inclusive lower byte value"));
    randomForm->addRow(formLabel(tr("Minimum"), randomPage, &parameterLabels),
                       m_randomMinimum);

    m_randomMaximum = new QSpinBox(randomPage);
    m_randomMaximum->setObjectName(QStringLiteral("randomMaximum"));
    m_randomMaximum->setRange(0, 255);
    m_randomMaximum->setValue(255);
    m_randomMaximum->setToolTip(tr("Inclusive upper byte value"));
    randomForm->addRow(formLabel(tr("Maximum"), randomPage, &parameterLabels),
                       m_randomMaximum);
    m_modeControls->addWidget(randomPage);

    auto *fixedPage = new QWidget(m_modeControls);
    auto *fixedForm = new QFormLayout(fixedPage);
    configureSimpleForm(fixedForm);
    auto *fixedLength = new QSpinBox(fixedPage);
    fixedLength->setRange(1, 65536);
    fixedLength->setValue(16);
    fixedLength->setSuffix(tr(" values"));
    fixedForm->addRow(formLabel(tr("Length"), fixedPage, &parameterLabels), fixedLength);
    connect(fixedLength, &QSpinBox::valueChanged, m_length, &QSpinBox::setValue);
    connect(m_length, &QSpinBox::valueChanged, fixedLength, &QSpinBox::setValue);

    m_fixedByte = new QSpinBox(fixedPage);
    m_fixedByte->setObjectName(QStringLiteral("fixedByte"));
    m_fixedByte->setRange(0, 255);
    m_fixedByte->setValue(0x55);
    m_fixedByte->setDisplayIntegerBase(16);
    m_fixedByte->setPrefix(QStringLiteral("0x"));
    fixedForm->addRow(formLabel(tr("Fixed value"), fixedPage, &parameterLabels), m_fixedByte);
    m_modeControls->addWidget(fixedPage);

    auto *asciiPage = new QWidget(m_modeControls);
    auto *asciiForm = new QFormLayout(asciiPage);
    configureSimpleForm(asciiForm);
    auto *asciiLength = new QSpinBox(asciiPage);
    asciiLength->setRange(1, 65536);
    asciiLength->setValue(16);
    asciiLength->setSuffix(tr(" values"));
    asciiForm->addRow(formLabel(tr("Length"), asciiPage, &parameterLabels), asciiLength);
    connect(asciiLength, &QSpinBox::valueChanged, m_length, &QSpinBox::setValue);
    connect(m_length, &QSpinBox::valueChanged, asciiLength, &QSpinBox::setValue);
    m_modeControls->addWidget(asciiPage);

    auto *signalPage = new QWidget(m_modeControls);
    auto *signalForm = new QFormLayout(signalPage);
    m_waveform = new QComboBox(signalPage);
    m_waveform->setObjectName(QStringLiteral("waveform"));
    m_waveform->setIconSize(QSize(kWaveGlyphSize, kWaveGlyphSize));
    m_waveform->view()->setIconSize(QSize(kWaveGlyphSize, kWaveGlyphSize));
    if (auto *view = qobject_cast<QListView *>(m_waveform->view())) {
        view->setUniformItemSizes(true);
        view->setSpacing(0);
    }
    for (const DataGenerator::Pattern pattern : kWavePatterns)
        m_waveform->addItem(DataGenerator::patternName(pattern), int(pattern));
    signalForm->addRow(formLabel(tr("Form"), signalPage, &parameterLabels), m_waveform);

    m_wavePeriod = new QSpinBox(signalPage);
    m_wavePeriod->setObjectName(QStringLiteral("wavePeriod"));
    m_wavePeriod->setRange(1, 1000);
    m_wavePeriod->setValue(32);
    setSevenDigitWidth(m_wavePeriod);
    m_wavePeriod->setToolTip(tr("Period is counted in packets, not milliseconds."));
    m_wavePeriodSlider = new QSlider(Qt::Horizontal, signalPage);
    m_wavePeriodSlider->setObjectName(QStringLiteral("wavePeriodSlider"));
    m_wavePeriodSlider->setRange(1, 1000);
    signalForm->addRow(formLabel(tr("Period"), signalPage, &parameterLabels),
                       sliderRow(signalPage, m_wavePeriodSlider, m_wavePeriod));

    m_amplitude = new QDoubleSpinBox(signalPage);
    m_amplitude->setObjectName(QStringLiteral("amplitude"));
    m_amplitude->setRange(1.0, 100.0);
    m_amplitude->setDecimals(3);
    m_amplitude->setSingleStep(0.1);
    m_amplitude->setValue(100.0);
    setSevenDigitWidth(m_amplitude);
    m_amplitudeSlider = new QSlider(Qt::Horizontal, signalPage);
    m_amplitudeSlider->setObjectName(QStringLiteral("amplitudeSlider"));
    m_amplitudeSlider->setRange(1 * kAmplitudeScale, 100 * kAmplitudeScale);
    signalForm->addRow(formLabel(tr("Amplitude"), signalPage, &parameterLabels),
                       sliderRow(signalPage, m_amplitudeSlider, m_amplitude));

    m_offset = new QDoubleSpinBox(signalPage);
    m_offset->setObjectName(QStringLiteral("offset"));
    m_offset->setRange(-50.0, 50.0);
    m_offset->setDecimals(3);
    m_offset->setSingleStep(0.1);
    setSevenDigitWidth(m_offset);
    m_offsetSlider = new QSlider(Qt::Horizontal, signalPage);
    m_offsetSlider->setObjectName(QStringLiteral("offsetSlider"));
    m_offsetSlider->setRange(-50 * kOffsetScale, 50 * kOffsetScale);
    signalForm->addRow(formLabel(tr("Offset"), signalPage, &parameterLabels),
                       sliderRow(signalPage, m_offsetSlider, m_offset));

    m_dutyLabel = formLabel(tr("Duty cycle"), signalPage, &parameterLabels);
    m_dutyCycle = new QDoubleSpinBox(signalPage);
    m_dutyCycle->setObjectName(QStringLiteral("dutyCycle"));
    m_dutyCycle->setRange(1.0, 99.0);
    m_dutyCycle->setDecimals(1);
    m_dutyCycle->setSingleStep(0.1);
    m_dutyCycle->setSuffix(tr(" %"));
    m_dutyCycle->setValue(50.0);
    setSevenDigitWidth(m_dutyCycle);
    m_dutyCycleSlider = new QSlider(Qt::Horizontal, signalPage);
    m_dutyCycleSlider->setObjectName(QStringLiteral("dutyCycleSlider"));
    m_dutyCycleSlider->setRange(1 * kDutyCycleScale, 99 * kDutyCycleScale);
    m_dutyRow = sliderRow(signalPage, m_dutyCycleSlider, m_dutyCycle);
    signalForm->addRow(m_dutyLabel, m_dutyRow);

    m_wavePrecision = new QSpinBox(signalPage);
    m_wavePrecision->setObjectName(QStringLiteral("wavePrecision"));
    m_wavePrecision->setRange(0, 10);
    m_wavePrecision->setValue(3);
    setSevenDigitWidth(m_wavePrecision);
    m_wavePrecisionSlider = new QSlider(Qt::Horizontal, signalPage);
    m_wavePrecisionSlider->setObjectName(QStringLiteral("wavePrecisionSlider"));
    m_wavePrecisionSlider->setRange(0, 10);
    signalForm->addRow(formLabel(tr("Precision"), signalPage, &parameterLabels),
                       sliderRow(signalPage, m_wavePrecisionSlider, m_wavePrecision));

    m_frequency = new QLabel(signalPage);
    m_frequency->setObjectName(QStringLiteral("hintLabel"));
    signalForm->addRow(formLabel(tr("Frequency"), signalPage, &parameterLabels), m_frequency);
    m_modeControls->addWidget(signalPage);

    auto *commonForm = new QFormLayout;
    m_prefix = new QLineEdit(this);
    m_prefix->setObjectName(QStringLiteral("prefix"));
    m_prefix->setClearButtonEnabled(true);
    m_prefix->setPlaceholderText(tr("Optional text before every packet"));
    m_prefix->setToolTip(tr("UTF-8 text prepended to every generated packet; it does not "
                            "reduce the payload length."));
    commonForm->addRow(formLabel(tr("Prefix"), this, &parameterLabels), m_prefix);

    m_interval = new QComboBox(this);
    m_interval->setObjectName(QStringLiteral("streamInterval"));
    for (const int interval : kIntervalsMs)
        m_interval->addItem(intervalLabel(interval), interval);
    m_interval->setCurrentIndex(m_interval->findData(100));
    m_interval->setEditable(true);
    m_interval->setInsertPolicy(QComboBox::NoInsert);
    m_interval->setValidator(new QIntValidator(1, kMaxIntervalMs, m_interval));
    m_interval->setToolTip(tr("Interval between packets while streaming"));
    commonForm->addRow(formLabel(tr("Send frequency"), this, &parameterLabels), m_interval);
    layout->addLayout(commonForm);

    int labelWidth = 0;
    for (const QLabel *label : std::as_const(parameterLabels))
        labelWidth = qMax(labelWidth, label->fontMetrics().horizontalAdvance(label->text()));
    for (QLabel *label : std::as_const(parameterLabels))
        label->setMinimumWidth(labelWidth);

    // Внутри блока сигнала не нужна ширина меток общих настроек: она отделяла ползунки
    // от названий параметров заметной пустотой. У сигнала своя ровная колонка меток.
    int signalLabelWidth = 0;
    const QList<QLabel *> signalLabels =
        signalPage->findChildren<QLabel *>(QString(), Qt::FindDirectChildrenOnly);
    for (const QLabel *label : signalLabels)
        signalLabelWidth = qMax(signalLabelWidth,
                                label->fontMetrics().horizontalAdvance(label->text()));
    for (QLabel *label : signalLabels)
        label->setMinimumWidth(signalLabelWidth);

    // Пустое место остаётся между настройками и результатом: предпросмотр с командами
    // привязан к нижнему краю панели и не скачет при смене режимов.
    layout->addStretch(1);

    auto *previewTitle = new QLabel(tr("Preview"), this);
    previewTitle->setObjectName(QStringLiteral("hintLabel"));
    layout->addWidget(previewTitle);
    auto *previewContainer = new QWidget(this);
    auto *previewLayout = new QGridLayout(previewContainer);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    m_preview = new QPlainTextEdit(previewContainer);
    m_preview->setReadOnly(true);
    m_preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_preview->setMaximumHeight(76);
    previewLayout->addWidget(m_preview, 0, 0);
    m_hexView = new QToolButton(previewContainer);
    m_hexView->setObjectName(QStringLiteral("previewHex"));
    m_hexView->setAutoRaise(true);
    m_hexView->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));
    m_hexView->setStyleSheet(
        QStringLiteral("QToolButton, QToolButton:hover, QToolButton:pressed, QToolButton:checked, "
                       "QToolButton:focus "
                       "{ background: transparent; border: 1px solid transparent; }"));
    previewLayout->addWidget(m_hexView, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    layout->addWidget(previewContainer);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(host()->metric(IPanelHost::Metric::Gap));
    m_playPause = new QToolButton(this);
    m_playPause->setObjectName(QStringLiteral("playPause"));
    m_playPause->setCheckable(true);
    m_playPause->setAutoRaise(true);
    m_playPause->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));
    actions->addWidget(m_playPause);

    m_reset = new QToolButton(this);
    m_reset->setObjectName(QStringLiteral("resetGenerator"));
    m_reset->setAutoRaise(true);
    m_reset->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));
    actions->addWidget(m_reset);

    m_sendOnce = new QToolButton(this);
    m_sendOnce->setObjectName(QStringLiteral("sendOnce"));
    m_sendOnce->setAutoRaise(true);
    m_sendOnce->setIconSize(QSize(kToolGlyphSize, kToolGlyphSize));
    actions->addWidget(m_sendOnce);
    actions->addStretch(1);

    m_sentLabel = new QLabel(this);
    m_sentLabel->setObjectName(QStringLiteral("hintLabel"));
    actions->addWidget(m_sentLabel);
    layout->addLayout(actions);

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &GeneratorPanel::sendOnce);

    const auto parametersChanged = [this] {
        applySettingsToGenerator();
        updateFrequency();
        updatePreview();
        saveSettings();
    };
    const auto bindInt = [parametersChanged](QSlider *slider, QSpinBox *editor) {
        QObject::connect(slider, &QSlider::valueChanged, editor,
                         [editor, parametersChanged](int value) {
                             const QSignalBlocker blocker(editor);
                             editor->setValue(value);
                             parametersChanged();
                         });
        QObject::connect(editor, &QSpinBox::valueChanged, slider,
                         [slider, parametersChanged](int value) {
                             const QSignalBlocker blocker(slider);
                             slider->setValue(value);
                             parametersChanged();
                         });
    };
    const auto bindDouble = [parametersChanged](QSlider *slider, QDoubleSpinBox *editor,
                                                int scale) {
        QObject::connect(slider, &QSlider::valueChanged, editor,
                         [editor, scale, parametersChanged](int value) {
                             const QSignalBlocker blocker(editor);
                             editor->setValue(double(value) / scale);
                             parametersChanged();
                         });
        QObject::connect(editor, &QDoubleSpinBox::valueChanged, slider,
                         [slider, scale, parametersChanged](double value) {
                             const QSignalBlocker blocker(slider);
                             slider->setValue(qRound(value * scale));
                             parametersChanged();
                         });
    };
    bindInt(m_wavePeriodSlider, m_wavePeriod);
    bindDouble(m_amplitudeSlider, m_amplitude, kAmplitudeScale);
    bindDouble(m_offsetSlider, m_offset, kOffsetScale);
    bindDouble(m_dutyCycleSlider, m_dutyCycle, kDutyCycleScale);
    bindInt(m_wavePrecisionSlider, m_wavePrecision);

    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int index) {
        selectMode(index);
        saveSettings();
    });
    connect(m_waveform, &QComboBox::currentIndexChanged, this,
            [this](int) {
                selectMode(m_mode->currentIndex());
                saveSettings();
            });
    connect(m_length, &QSpinBox::valueChanged, this, parametersChanged);
    connect(m_counterIncrement, &QSpinBox::valueChanged, this, parametersChanged);
    connect(m_rampIncrement, &QSpinBox::valueChanged, this, parametersChanged);
    connect(m_randomMinimum, &QSpinBox::valueChanged, this, [this, parametersChanged](int value) {
        if (value > m_randomMaximum->value()) {
            const QSignalBlocker blocker(m_randomMaximum);
            m_randomMaximum->setValue(value);
        }
        parametersChanged();
    });
    connect(m_randomMaximum, &QSpinBox::valueChanged, this, [this, parametersChanged](int value) {
        if (value < m_randomMinimum->value()) {
            const QSignalBlocker blocker(m_randomMinimum);
            m_randomMinimum->setValue(value);
        }
        parametersChanged();
    });
    connect(m_fixedByte, &QSpinBox::valueChanged, this, parametersChanged);
    connect(m_prefix, &QLineEdit::textChanged, this, parametersChanged);
    const auto initialValueChanged = [this] {
        applySettingsToGenerator();
        m_generator.reset();
        updatePreview();
        saveSettings();
    };
    connect(m_counterStart, &QSpinBox::valueChanged, this, initialValueChanged);
    connect(m_rampStart, &QSpinBox::valueChanged, this, initialValueChanged);
    connect(m_interval, &QComboBox::currentTextChanged, this, [this, parametersChanged] {
        if (m_timer->isActive())
            m_timer->start(selectedIntervalMs());
        parametersChanged();
    });

    connect(m_playPause, &QToolButton::clicked, this,
            [this] { setStreaming(!m_timer->isActive()); });
    connect(m_reset, &QToolButton::clicked, this, &GeneratorPanel::resetRuntime);
    connect(m_sendOnce, &QToolButton::clicked, this, &GeneratorPanel::sendOnce);
    connect(m_hexView, &QToolButton::clicked, this, [this] {
        m_previewHex = !m_previewHex;
        updatePreview();
        saveSettings();
    });

    restoreSettings();
    m_wavePeriodSlider->setValue(m_wavePeriod->value());
    m_amplitudeSlider->setValue(qRound(m_amplitude->value() * kAmplitudeScale));
    m_offsetSlider->setValue(qRound(m_offset->value() * kOffsetScale));
    m_dutyCycleSlider->setValue(qRound(m_dutyCycle->value() * kDutyCycleScale));
    m_wavePrecisionSlider->setValue(m_wavePrecision->value());
    updateIcons();
    selectMode(m_mode->currentIndex());
    resetRuntime();
    setSendEnabled(false);
}

void GeneratorPanel::themeChanged()
{
    updateIcons();
}

void GeneratorPanel::settingsReset()
{
    restoreSettings();
    selectMode(m_mode->currentIndex());
    resetRuntime();
}

void GeneratorPanel::restoreSettings()
{
    // Блокировка записи нужна не только при запуске: тот же путь вызывается после общего
    // сброса, когда старые значения всё ещё показаны в виджетах.
    const QScopedValueRollback<bool> restoring(m_restoringSettings, true);

    const int mode = host()->value(QLatin1String(kKeyMode),
                                   int(DataGenerator::Pattern::Counter)).toInt();
    const int modeIndex = m_mode->findData(mode);
    m_mode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);

    const int waveform = host()->value(QLatin1String(kKeyWaveform),
                                        int(DataGenerator::Pattern::Sine)).toInt();
    const int waveformIndex = m_waveform->findData(waveform);
    m_waveform->setCurrentIndex(waveformIndex >= 0 ? waveformIndex : 0);

    m_length->setValue(host()->value(QLatin1String(kKeyLength), 16).toInt());
    m_counterStart->setValue(host()->value(QLatin1String(kKeyCounterStart), 0).toInt());
    m_counterIncrement->setValue(host()->value(QLatin1String(kKeyCounterIncrement), 1).toInt());
    m_rampStart->setValue(host()->value(QLatin1String(kKeyRampStart), 0).toInt());
    m_rampIncrement->setValue(host()->value(QLatin1String(kKeyRampIncrement), 1).toInt());
    m_randomMinimum->setValue(host()->value(QLatin1String(kKeyRandomMinimum), 0).toInt());
    m_randomMaximum->setValue(host()->value(QLatin1String(kKeyRandomMaximum), 255).toInt());
    m_fixedByte->setValue(host()->value(QLatin1String(kKeyFixedValue), 0x55).toInt());
    m_wavePeriod->setValue(host()->value(QLatin1String(kKeyWavePeriod), 32).toInt());
    m_amplitude->setValue(host()->value(QLatin1String(kKeyAmplitude), 100.0).toDouble());
    m_offset->setValue(host()->value(QLatin1String(kKeyOffset), 0.0).toDouble());
    m_dutyCycle->setValue(host()->value(QLatin1String(kKeyDutyCycle), 50.0).toDouble());
    m_wavePrecision->setValue(host()->value(QLatin1String(kKeyPrecision), 3).toInt());
    m_prefix->setText(host()->value(QLatin1String(kKeyPrefix)).toString());

    const int interval = host()->value(QLatin1String(kKeyInterval), 100).toInt();
    const int intervalIndex = m_interval->findData(interval);
    if (intervalIndex >= 0)
        m_interval->setCurrentIndex(intervalIndex);
    else
        m_interval->setEditText(QString::number(qBound(1, interval, kMaxIntervalMs)));
    m_previewHex = host()->value(QLatin1String(kKeyPreviewHex), false).toBool();
}

void GeneratorPanel::saveSettings() const
{
    if (m_restoringSettings)
        return;

    host()->setValue(QLatin1String(kKeyMode), m_mode->currentData());
    host()->setValue(QLatin1String(kKeyWaveform), m_waveform->currentData());
    host()->setValue(QLatin1String(kKeyLength), m_length->value());
    host()->setValue(QLatin1String(kKeyCounterStart), m_counterStart->value());
    host()->setValue(QLatin1String(kKeyCounterIncrement), m_counterIncrement->value());
    host()->setValue(QLatin1String(kKeyRampStart), m_rampStart->value());
    host()->setValue(QLatin1String(kKeyRampIncrement), m_rampIncrement->value());
    host()->setValue(QLatin1String(kKeyRandomMinimum), m_randomMinimum->value());
    host()->setValue(QLatin1String(kKeyRandomMaximum), m_randomMaximum->value());
    host()->setValue(QLatin1String(kKeyFixedValue), m_fixedByte->value());
    host()->setValue(QLatin1String(kKeyWavePeriod), m_wavePeriod->value());
    host()->setValue(QLatin1String(kKeyAmplitude), m_amplitude->value());
    host()->setValue(QLatin1String(kKeyOffset), m_offset->value());
    host()->setValue(QLatin1String(kKeyDutyCycle), m_dutyCycle->value());
    host()->setValue(QLatin1String(kKeyPrecision), m_wavePrecision->value());
    host()->setValue(QLatin1String(kKeyPrefix), m_prefix->text());
    host()->setValue(QLatin1String(kKeyInterval), selectedIntervalMs());
    host()->setValue(QLatin1String(kKeyPreviewHex), m_previewHex);
}

void GeneratorPanel::selectMode(int index)
{
    if (index < 0)
        return;

    m_modeControls->setCurrentIndex(index);
    const int selected = m_mode->itemData(index).toInt();
    m_activePattern = selected == kSignalMode
                          ? DataGenerator::Pattern(m_waveform->currentData().toInt())
                          : DataGenerator::Pattern(selected);

    const bool square = m_activePattern == DataGenerator::Pattern::Square;
    m_dutyLabel->setVisible(square);
    m_dutyRow->setVisible(square);
    m_modeControls->setFixedHeight(m_modeControls->currentWidget()->sizeHint().height());
    applySettingsToGenerator();
    updateFrequency();
    updatePreview();
}

void GeneratorPanel::applySettingsToGenerator()
{
    const bool signal = m_mode->currentData().toInt() == kSignalMode;
    m_activePattern = signal ? DataGenerator::Pattern(m_waveform->currentData().toInt())
                             : DataGenerator::Pattern(m_mode->currentData().toInt());
    m_generator.setPattern(m_activePattern);
    m_generator.setLength(m_length->value());
    m_generator.setFixedByte(quint8(m_fixedByte->value()));
    m_generator.setCounterStart(m_counterStart->value());
    m_generator.setCounterIncrement(m_counterIncrement->value());
    m_generator.setRampStart(m_rampStart->value());
    m_generator.setRampIncrement(m_rampIncrement->value());
    m_generator.setRandomRange(m_randomMinimum->value(), m_randomMaximum->value());
    m_generator.setWavePeriod(m_wavePeriod->value());
    m_generator.setAmplitude(m_amplitude->value());
    m_generator.setOffset(m_offset->value());
    m_generator.setDutyCycle(m_dutyCycle->value());
    m_generator.setWavePrecision(m_wavePrecision->value());
    m_generator.setPrefix(m_prefix->text().toUtf8());
}

QByteArray GeneratorPanel::withSendTermination(const QByteArray &payload) const
{
    return payload + DataCodec::terminationBytes(host()->sendTermination());
}

void GeneratorPanel::updatePreview()
{
    const QByteArray payload = withSendTermination(m_generator.preview());
    if (DataGenerator::isWaveform(m_activePattern) && !m_previewHex) {
        QString text = QString::fromUtf8(m_generator.preview());
        const DataCodec::Termination termination = host()->sendTermination();
        if (termination != DataCodec::Termination::None)
            text += QStringLiteral("  [%1]").arg(DataCodec::terminationName(termination));
        m_preview->setPlainText(text);
    } else {
        m_preview->setPlainText(DataCodec::toHex(payload));
    }
}

void GeneratorPanel::updateFrequency()
{
    const double frequency =
        1000.0 / (double(selectedIntervalMs()) * m_wavePeriod->value());
    m_frequency->setText(QLocale().toString(frequency, 'g', 6) + tr(" Hz"));
}

int GeneratorPanel::selectedIntervalMs() const
{
    // У пресета число хранится отдельно от локализованной подписи («1 s»), а введённое
    // руками приходит только текстом. Не пытаемся разбирать переведённую строку.
    const QVariant data = m_interval->currentData();
    if (data.isValid() && m_interval->findText(m_interval->currentText()) >= 0)
        return data.toInt();

    bool ok = false;
    const int typed = m_interval->currentText().toInt(&ok);
    return ok ? qBound(1, typed, kMaxIntervalMs) : 100;
}

void GeneratorPanel::updateIcons()
{
    for (int index = 0; index < int(kWavePatterns.size()); ++index)
        m_waveform->setItemIcon(index, host()->icon(kWaveGlyphs.at(index), kToolGlyphSize));

    const bool running = m_timer && m_timer->isActive();
    m_playPause->setIcon(host()->icon(running ? mdi::Pause : mdi::Play, kToolGlyphSize));
    const QSignalBlocker blocker(m_playPause);
    m_playPause->setChecked(running);
    m_playPause->setToolTip(running ? tr("Pause stream") : tr("Start stream"));
    m_playPause->setAccessibleName(m_playPause->toolTip());
    m_reset->setIcon(host()->icon(mdi::BackupRestore, kToolGlyphSize));
    m_reset->setToolTip(tr("Reset generated sequence and packet counter"));
    m_reset->setAccessibleName(m_reset->toolTip());
    m_sendOnce->setIcon(host()->icon(mdi::StepForward, kToolGlyphSize));
    m_sendOnce->setToolTip(tr("Send one packet"));
    m_sendOnce->setAccessibleName(m_sendOnce->toolTip());
    m_hexView->setIcon(host()->icon(mdi::Hexadecimal, kToolGlyphSize));
    m_hexView->setToolTip(tr("Show preview as hexadecimal"));
    m_hexView->setAccessibleName(m_hexView->toolTip());
}

void GeneratorPanel::updateSentLabel()
{
    m_sentLabel->setText(tr("Sent %1").arg(QLocale().toString(m_sentPackets)));
}

void GeneratorPanel::sendOnce()
{
    if (!m_sendEnabled)
        return;

    host()->send(withSendTermination(m_generator.generate()));
    ++m_sentPackets;
    updateSentLabel();
    updatePreview();
}

void GeneratorPanel::resetRuntime()
{
    applySettingsToGenerator();
    m_generator.reset();
    m_sentPackets = 0;
    updateSentLabel();
    updatePreview();
}

void GeneratorPanel::setStreaming(bool on)
{
    if (on && !m_sendEnabled)
        return;

    if (on)
        m_timer->start(selectedIntervalMs());
    else
        m_timer->stop();
    updateIcons();
}

void GeneratorPanel::channelStateChanged(ChannelState state)
{
    setSendEnabled(state == ChannelState::Open);
}

void GeneratorPanel::setSendEnabled(bool enabled)
{
    m_sendEnabled = enabled;
    m_playPause->setEnabled(enabled);
    m_sendOnce->setEnabled(enabled);
    if (!enabled)
        setStreaming(false);
}

} // namespace spotty
