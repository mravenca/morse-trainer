#include "mainwindow.h"

#include "gpiokeysource.h"
#include "keyscope.h"
#include "keysource.h"
#include "morse.h"
#include "serialkeysource.h"
#include "tonegenerator.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

QLabel *bigLabel(const QString &text, double scale)
{
    auto *l = new QLabel(text);
    QFont f = l->font();
    f.setPointSizeF(f.pointSizeF() * scale);
    f.setStyleHint(QFont::Monospace);
    f.setFamily(QStringLiteral("monospace"));
    l->setFont(f);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QString qs(const std::string &s) { return QString::fromStdString(s); }

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Morse Trainer"));
    m_tone = new ToneGenerator(this);

    // ----- right side: scope + tabs
    auto *right = new QWidget;
    auto *rv = new QVBoxLayout(right);

    m_scope = new KeyScope;
    m_scope->setClock([] { return monotonicUs(); });
    rv->addWidget(m_scope);

    auto *statsRow = new QHBoxLayout;
    m_wpmLabel = new QLabel(QStringLiteral("— WPM"));
    m_ratioLabel = new QLabel;
    m_ratioLabel->setToolTip(QStringLiteral(
        "Dah:dit ratio (ideal 3.0) and weight = dit length / element gap (ideal 1.0)."));
    m_patternLabel = bigLabel(QString(), 1.4);
    m_patternLabel->setMinimumWidth(120);
    m_keyButton = new QPushButton(QStringLiteral("KEY (hold)"));
    m_keyButton->setToolTip(QStringLiteral("Mouse key. The Space bar also keys (except on the Receive tab)."));
    m_keyButton->setMinimumHeight(36);
    m_keyButton->setFocusPolicy(Qt::NoFocus);
    statsRow->addWidget(m_wpmLabel);
    statsRow->addSpacing(16);
    statsRow->addWidget(m_ratioLabel);
    statsRow->addStretch();
    statsRow->addWidget(m_patternLabel);
    statsRow->addWidget(m_keyButton);
    rv->addLayout(statsRow);

    m_tabs = new QTabWidget;
    m_tabs->addTab(buildFreeTab(), QStringLiteral("Free keying"));
    m_tabs->addTab(buildSendTab(), QStringLiteral("Send drill"));
    m_tabs->addTab(buildReceiveTab(), QStringLiteral("Receive (copy)"));
    m_tabs->addTab(buildReferenceTab(), QStringLiteral("Reference"));
    rv->addWidget(m_tabs, 1);

    auto *settingsScroll = new QScrollArea;
    settingsScroll->setWidget(buildSettings());
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setMinimumWidth(320);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *split = new QSplitter;
    split->addWidget(settingsScroll);
    split->addWidget(right);
    split->setStretchFactor(1, 1);
    setCentralWidget(split);
    resize(1150, 720);

    // ----- wiring
    connect(m_keyButton, &QPushButton::pressed, this, [this] { setInput(InButton, true, monotonicUs()); });
    connect(m_keyButton, &QPushButton::released, this, [this] { setInput(InButton, false, monotonicUs()); });

    m_tick.setInterval(5);
    connect(&m_tick, &QTimer::timeout, this, [this] { onDecoded(m_decoder.tick(monotonicUs())); });
    m_tick.start();
    m_statsTimer.setInterval(100);
    connect(&m_statsTimer, &QTimer::timeout, this, &MainWindow::updateLiveStats);
    m_statsTimer.start();

    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (m_inputs & InKeyboard)
            setInput(InKeyboard, false, monotonicUs());
        if (idx == TabReceive)
            m_recvInput->setFocus();
        else
            m_tabs->currentWidget()->setFocus();
    });

    m_sendNextTimer.setSingleShot(true);
    m_sendNextTimer.setInterval(1500);
    connect(&m_sendNextTimer, &QTimer::timeout, this, &MainWindow::sendNext);

    qApp->installEventFilter(this);

    loadSettings();
    applySpeed();
    updateKochLabel();
    restartAudio();
    if (m_sourceCombo->currentIndex() != SrcKeyboard)
        connectKey();
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    disconnectKey();
}

// ------------------------------------------------------------------ settings UI

QWidget *MainWindow::buildSettings()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);

    // Key input
    auto *keyBox = new QGroupBox(QStringLiteral("Key input"));
    auto *kf = new QFormLayout(keyBox);
    m_sourceCombo = new QComboBox;
    m_sourceCombo->addItems({QStringLiteral("Keyboard / mouse only"), QStringLiteral("Serial port (CTS/DSR)"),
                             QStringLiteral("GPIO line")});
    kf->addRow(QStringLiteral("Source"), m_sourceCombo);

    m_sourceStack = new QStackedWidget;
    auto *kbPage = new QLabel(QStringLiteral("Space bar or the KEY button.\nPick a hardware source to use a real key."));
    kbPage->setWordWrap(true);
    m_sourceStack->addWidget(kbPage);

    auto *serPage = new QWidget;
    auto *sf = new QFormLayout(serPage);
    sf->setContentsMargins(0, 0, 0, 0);
    auto *portRow = new QHBoxLayout;
    m_portCombo = new QComboBox;
    m_portCombo->setEditable(true);
    auto *refresh = new QPushButton(QStringLiteral("↻"));
    refresh->setMaximumWidth(32);
    refresh->setToolTip(QStringLiteral("Rescan serial ports"));
    connect(refresh, &QPushButton::clicked, this, &MainWindow::refreshPorts);
    portRow->addWidget(m_portCombo, 1);
    portRow->addWidget(refresh);
    sf->addRow(QStringLiteral("Port"), portRow);
    m_lineCombo = new QComboBox;
    m_lineCombo->addItems({QStringLiteral("CTS (DB9 pin 8)"), QStringLiteral("DSR (DB9 pin 6)"),
                           QStringLiteral("DCD (DB9 pin 1)"), QStringLiteral("RI (DB9 pin 9)")});
    sf->addRow(QStringLiteral("Key line"), m_lineCombo);
    m_invertCheck = new QCheckBox(QStringLiteral("Invert"));
    sf->addRow(QString(), m_invertCheck);
    auto *serHelp = new QLabel(QStringLiteral("Wire the key between DTR (pin 4) and the key line."));
    serHelp->setWordWrap(true);
    sf->addRow(serHelp);
    m_sourceStack->addWidget(serPage);

    auto *gpioPage = new QWidget;
    auto *gf = new QFormLayout(gpioPage);
    gf->setContentsMargins(0, 0, 0, 0);
    m_chipCombo = new QComboBox;
    m_chipCombo->setEditable(true);
    m_chipCombo->addItems(GpioKeySource::availableChips());
    gf->addRow(QStringLiteral("Chip"), m_chipCombo);
    m_gpioLine = new QSpinBox;
    m_gpioLine->setRange(0, 511);
    gf->addRow(QStringLiteral("Line offset"), m_gpioLine);
    m_activeLowCheck = new QCheckBox(QStringLiteral("Active low (key to GND)"));
    m_activeLowCheck->setChecked(true);
    gf->addRow(QString(), m_activeLowCheck);
    m_pullUpCheck = new QCheckBox(QStringLiteral("Internal pull-up"));
    m_pullUpCheck->setChecked(true);
    gf->addRow(QString(), m_pullUpCheck);
    if (!GpioKeySource::supported())
        gf->addRow(new QLabel(QStringLiteral("Not available in this build.")));
    m_sourceStack->addWidget(gpioPage);
    kf->addRow(m_sourceStack);

    m_debounceSpin = new QSpinBox;
    m_debounceSpin->setRange(0, 30);
    m_debounceSpin->setValue(5);
    m_debounceSpin->setSuffix(QStringLiteral(" ms"));
    m_debounceSpin->setToolTip(QStringLiteral("Leading-edge debounce: first edge is reported immediately, bounce is ignored for this long."));
    kf->addRow(QStringLiteral("Debounce"), m_debounceSpin);

    m_connectBtn = new QPushButton(QStringLiteral("Connect"));
    kf->addRow(m_connectBtn);
    m_keyStatus = new QLabel(QStringLiteral("Not connected"));
    m_keyStatus->setWordWrap(true);
    kf->addRow(m_keyStatus);

    connect(m_sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        m_sourceStack->setCurrentIndex(i);
        fitSourceStack();
        disconnectKey();
        m_connectBtn->setEnabled(i != SrcKeyboard);
    });
    connect(m_connectBtn, &QPushButton::clicked, this, [this] {
        if (m_key)
            disconnectKey();
        else
            connectKey();
    });
    v->addWidget(keyBox);

    // Audio
    auto *audioBox = new QGroupBox(QStringLiteral("Sidetone / audio"));
    auto *af = new QFormLayout(audioBox);
    m_audioCombo = new QComboBox;
    af->addRow(QStringLiteral("Output"), m_audioCombo);
    m_freqSpin = new QSpinBox;
    m_freqSpin->setRange(300, 1200);
    m_freqSpin->setSingleStep(10);
    m_freqSpin->setValue(650);
    m_freqSpin->setSuffix(QStringLiteral(" Hz"));
    af->addRow(QStringLiteral("Pitch"), m_freqSpin);
    m_volSlider = new QSlider(Qt::Horizontal);
    m_volSlider->setRange(0, 100);
    m_volSlider->setValue(50);
    af->addRow(QStringLiteral("Volume"), m_volSlider);
    m_sidetoneCheck = new QCheckBox(QStringLiteral("Sidetone while keying"));
    m_sidetoneCheck->setChecked(true);
    m_sidetoneCheck->setToolTip(QStringLiteral("Turn off if your key/oscillator makes its own tone."));
    af->addRow(QString(), m_sidetoneCheck);
    m_bufferSpin = new QSpinBox;
    m_bufferSpin->setRange(5, 200);
    m_bufferSpin->setValue(20);
    m_bufferSpin->setSuffix(QStringLiteral(" ms"));
    m_bufferSpin->setToolTip(QStringLiteral("Lower = less sidetone delay. Raise it if you hear crackles."));
    af->addRow(QStringLiteral("Buffer"), m_bufferSpin);
    m_audioStatus = new QLabel;
    m_audioStatus->setWordWrap(true);
    af->addRow(m_audioStatus);
    refreshAudioDevices();

    connect(m_freqSpin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int hz) { m_tone->setFrequency(hz); });
    connect(m_volSlider, &QSlider::valueChanged, this, [this](int v) { m_tone->setVolume(v / 100.0); });
    connect(m_sidetoneCheck, &QCheckBox::toggled, this, [this](bool on) { m_tone->setSidetoneEnabled(on); });
    connect(m_audioCombo, qOverload<int>(&QComboBox::activated), this, &MainWindow::restartAudio);
    connect(m_bufferSpin, &QSpinBox::editingFinished, this, &MainWindow::restartAudio);
    v->addWidget(audioBox);

    // Speed
    auto *speedBox = new QGroupBox(QStringLiteral("Speed"));
    auto *spf = new QFormLayout(speedBox);
    m_wpmSpin = new QSpinBox;
    m_wpmSpin->setRange(5, 50);
    m_wpmSpin->setValue(20);
    m_wpmSpin->setSuffix(QStringLiteral(" WPM"));
    m_wpmSpin->setToolTip(QStringLiteral("Character speed for playback; also seeds the decoder."));
    spf->addRow(QStringLiteral("Character speed"), m_wpmSpin);
    m_effWpmSpin = new QSpinBox;
    m_effWpmSpin->setRange(3, 50);
    m_effWpmSpin->setValue(10);
    m_effWpmSpin->setSuffix(QStringLiteral(" WPM"));
    m_effWpmSpin->setToolTip(QStringLiteral("Farnsworth: letters at full speed, gaps stretched to this overall speed."));
    spf->addRow(QStringLiteral("Effective speed"), m_effWpmSpin);
    m_adaptiveCheck = new QCheckBox(QStringLiteral("Decoder follows my speed"));
    m_adaptiveCheck->setChecked(true);
    spf->addRow(QString(), m_adaptiveCheck);
    connect(m_wpmSpin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::applySpeed);
    connect(m_effWpmSpin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::applySpeed);
    connect(m_adaptiveCheck, &QCheckBox::toggled, this, &MainWindow::applySpeed);
    v->addWidget(speedBox);

    // Koch
    auto *kochBox = new QGroupBox(QStringLiteral("Koch lesson"));
    auto *kof = new QFormLayout(kochBox);
    m_kochSpin = new QSpinBox;
    m_kochSpin->setRange(2, int(morse::kochOrder().size()));
    m_kochSpin->setValue(2);
    kof->addRow(QStringLiteral("Characters"), m_kochSpin);
    m_kochChars = bigLabel(QString(), 1.0);
    m_kochChars->setWordWrap(true);
    kof->addRow(m_kochChars);
    m_groupLenSpin = new QSpinBox;
    m_groupLenSpin->setRange(1, 10);
    m_groupLenSpin->setValue(5);
    kof->addRow(QStringLiteral("Group length"), m_groupLenSpin);
    m_groupsSpin = new QSpinBox;
    m_groupsSpin->setRange(1, 20);
    m_groupsSpin->setValue(5);
    kof->addRow(QStringLiteral("Groups per round"), m_groupsSpin);
    connect(m_kochSpin, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::updateKochLabel);
    v->addWidget(kochBox);

    v->addStretch();
    return w;
}

QWidget *MainWindow::buildFreeTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *hint = new QLabel(QStringLiteral(
        "Key anything. The decoder adapts to your speed; watch the tape above for even dits, "
        "3:1 dahs and clean spacing."));
    hint->setWordWrap(true);
    v->addWidget(hint);
    m_freeText = new QPlainTextEdit;
    m_freeText->setReadOnly(true);
    QFont f = m_freeText->font();
    f.setFamily(QStringLiteral("monospace"));
    f.setPointSizeF(f.pointSizeF() * 1.6);
    m_freeText->setFont(f);
    m_freeText->setFocusPolicy(Qt::NoFocus);
    v->addWidget(m_freeText, 1);
    auto *clear = new QPushButton(QStringLiteral("Clear"));
    clear->setFocusPolicy(Qt::NoFocus);
    connect(clear, &QPushButton::clicked, this, [this] {
        m_freeText->clear();
        m_decoder.reset();
        m_scope->clear();
    });
    auto *row = new QHBoxLayout;
    row->addStretch();
    row->addWidget(clear);
    v->addLayout(row);
    return w;
}

QWidget *MainWindow::buildSendTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *hint = new QLabel(QStringLiteral(
        "Key the group shown. It is graded when you have sent enough characters or pause for a word space."));
    hint->setWordWrap(true);
    v->addWidget(hint);

    m_sendTarget = bigLabel(QStringLiteral("—"), 3.0);
    m_sendTarget->setAlignment(Qt::AlignCenter);
    v->addWidget(m_sendTarget);
    m_sendAttempt = bigLabel(QString(), 2.2);
    m_sendAttempt->setAlignment(Qt::AlignCenter);
    v->addWidget(m_sendAttempt);

    auto *row = new QHBoxLayout;
    m_sendStartBtn = new QPushButton(QStringLiteral("Start"));
    auto *skip = new QPushButton(QStringLiteral("Skip"));
    auto *hear = new QPushButton(QStringLiteral("Hear it"));
    m_sendPlayFirst = new QCheckBox(QStringLiteral("Play each group first"));
    for (QPushButton *b : {m_sendStartBtn, skip, hear})
        b->setFocusPolicy(Qt::NoFocus);
    m_sendPlayFirst->setFocusPolicy(Qt::NoFocus);
    row->addWidget(m_sendStartBtn);
    row->addWidget(skip);
    row->addWidget(hear);
    row->addWidget(m_sendPlayFirst);
    row->addStretch();
    v->addLayout(row);

    m_sendScore = new QLabel;
    m_sendScore->setWordWrap(true);
    v->addWidget(m_sendScore);
    m_sendHistory = new QTextBrowser;
    m_sendHistory->setFocusPolicy(Qt::NoFocus);
    v->addWidget(m_sendHistory, 1);

    connect(m_sendStartBtn, &QPushButton::clicked, this, [this] {
        m_sendActive = !m_sendActive;
        m_sendStartBtn->setText(m_sendActive ? QStringLiteral("Stop") : QStringLiteral("Start"));
        if (m_sendActive) {
            sendNext();
        } else {
            m_sendNextTimer.stop();
            m_sendTarget->setText(QStringLiteral("—"));
            m_sendAttempt->clear();
        }
    });
    connect(skip, &QPushButton::clicked, this, [this] {
        if (m_sendActive)
            sendNext();
    });
    connect(hear, &QPushButton::clicked, this, [this] {
        if (!m_sendExpected.isEmpty())
            playText(m_sendExpected);
    });
    return w;
}

QWidget *MainWindow::buildReceiveTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    auto *hint = new QLabel(QStringLiteral(
        "Press Play, type what you hear, then Enter. Spaces are optional. "
        "Uses the Koch characters and Farnsworth speed from the left."));
    hint->setWordWrap(true);
    v->addWidget(hint);

    auto *row = new QHBoxLayout;
    m_recvPlayBtn = new QPushButton(QStringLiteral("Play new round"));
    m_recvReplayBtn = new QPushButton(QStringLiteral("Replay"));
    m_recvReplayBtn->setEnabled(false);
    row->addWidget(m_recvPlayBtn);
    row->addWidget(m_recvReplayBtn);
    row->addStretch();
    v->addLayout(row);

    m_recvInput = new QLineEdit;
    QFont f = m_recvInput->font();
    f.setFamily(QStringLiteral("monospace"));
    f.setPointSizeF(f.pointSizeF() * 1.8);
    m_recvInput->setFont(f);
    m_recvInput->setPlaceholderText(QStringLiteral("type here…"));
    v->addWidget(m_recvInput);

    m_recvScore = new QLabel;
    m_recvScore->setWordWrap(true);
    v->addWidget(m_recvScore);
    m_recvHistory = new QTextBrowser;
    m_recvHistory->setFocusPolicy(Qt::NoFocus);
    v->addWidget(m_recvHistory, 1);

    connect(m_recvPlayBtn, &QPushButton::clicked, this, &MainWindow::receivePlay);
    connect(m_recvReplayBtn, &QPushButton::clicked, this, [this] {
        if (!m_recvExpected.isEmpty())
            playText(m_recvExpected);
        m_recvInput->setFocus();
    });
    connect(m_recvInput, &QLineEdit::returnPressed, this, &MainWindow::receiveCheck);
    return w;
}

QWidget *MainWindow::buildReferenceTab()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);
    v->addWidget(new QLabel(QStringLiteral("Double-click a row to hear it. Koch # is the lesson that introduces it.")));
    auto *table = new QTableWidget;
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({QStringLiteral("Symbol"), QStringLiteral("Code"), QStringLiteral("Koch #")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setStretchLastSection(true);
    table->setFocusPolicy(Qt::NoFocus);

    std::vector<std::string> rows = morse::kochOrder();
    for (const char *extra : {"-", "+", "'", "(", ")", ":", "@", "\"", "<SK>", "<KA>", "<AS>"})
        rows.emplace_back(extra);
    table->setRowCount(int(rows.size()));
    QFont mono = table->font();
    mono.setFamily(QStringLiteral("monospace"));
    mono.setPointSizeF(mono.pointSizeF() * 1.3);
    for (int i = 0; i < int(rows.size()); ++i) {
        QString code = qs(morse::encode(rows[i]));
        code.replace('.', QChar(0x00B7)).replace('-', QChar(0x2013));
        auto *sym = new QTableWidgetItem(qs(rows[i]));
        auto *pat = new QTableWidgetItem(code);
        sym->setFont(mono);
        pat->setFont(mono);
        table->setItem(i, 0, sym);
        table->setItem(i, 1, pat);
        if (i < int(morse::kochOrder().size()))
            table->setItem(i, 2, new QTableWidgetItem(QString::number(i + 1)));
    }
    table->resizeColumnsToContents();
    connect(table, &QTableWidget::cellDoubleClicked, this, [this, table](int r, int) {
        playText(table->item(r, 0)->text());
    });
    v->addWidget(table, 1);
    return w;
}

// Size the stacked widget to the visible page only.
void MainWindow::fitSourceStack()
{
    for (int i = 0; i < m_sourceStack->count(); ++i)
        m_sourceStack->widget(i)->setSizePolicy(QSizePolicy::Preferred,
            i == m_sourceStack->currentIndex() ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    m_sourceStack->adjustSize();
}

// ------------------------------------------------------------------ settings persistence

void MainWindow::loadSettings()
{
    QSettings s;
    refreshPorts();
    m_sourceCombo->setCurrentIndex(s.value("key/source", int(SrcKeyboard)).toInt());
    m_sourceStack->setCurrentIndex(m_sourceCombo->currentIndex());
    fitSourceStack();
    m_connectBtn->setEnabled(m_sourceCombo->currentIndex() != SrcKeyboard);
    const QString port = s.value("key/port").toString();
    if (!port.isEmpty())
        m_portCombo->setCurrentText(port);
    m_lineCombo->setCurrentIndex(s.value("key/line", 0).toInt());
    m_invertCheck->setChecked(s.value("key/invert", false).toBool());
    const QString chip = s.value("key/chip").toString();
    if (!chip.isEmpty())
        m_chipCombo->setCurrentText(chip);
    m_gpioLine->setValue(s.value("key/gpioLine", 0).toInt());
    m_activeLowCheck->setChecked(s.value("key/activeLow", true).toBool());
    m_pullUpCheck->setChecked(s.value("key/pullUp", true).toBool());
    m_debounceSpin->setValue(s.value("key/debounceMs", 5).toInt());

    const QByteArray dev = s.value("audio/device").toByteArray();
    for (int i = 0; i < m_audioCombo->count(); ++i)
        if (m_audioCombo->itemData(i).toByteArray() == dev)
            m_audioCombo->setCurrentIndex(i);
    m_freqSpin->setValue(s.value("audio/freq", 650).toInt());
    m_volSlider->setValue(s.value("audio/volume", 50).toInt());
    m_sidetoneCheck->setChecked(s.value("audio/sidetone", true).toBool());
    m_bufferSpin->setValue(s.value("audio/bufferMs", 20).toInt());
    m_tone->setFrequency(m_freqSpin->value());
    m_tone->setVolume(m_volSlider->value() / 100.0);
    m_tone->setSidetoneEnabled(m_sidetoneCheck->isChecked());

    m_wpmSpin->setValue(s.value("speed/wpm", 20).toInt());
    m_effWpmSpin->setValue(s.value("speed/effWpm", 10).toInt());
    m_adaptiveCheck->setChecked(s.value("speed/adaptive", true).toBool());

    m_kochSpin->setValue(s.value("koch/level", 2).toInt());
    m_groupLenSpin->setValue(s.value("koch/groupLen", 5).toInt());
    m_groupsSpin->setValue(s.value("koch/groups", 5).toInt());
    m_sendPlayFirst->setChecked(s.value("send/playFirst", false).toBool());
    restoreGeometry(s.value("window/geometry").toByteArray());
}

void MainWindow::saveSettings()
{
    QSettings s;
    s.setValue("key/source", m_sourceCombo->currentIndex());
    s.setValue("key/port", m_portCombo->currentText());
    s.setValue("key/line", m_lineCombo->currentIndex());
    s.setValue("key/invert", m_invertCheck->isChecked());
    s.setValue("key/chip", m_chipCombo->currentText());
    s.setValue("key/gpioLine", m_gpioLine->value());
    s.setValue("key/activeLow", m_activeLowCheck->isChecked());
    s.setValue("key/pullUp", m_pullUpCheck->isChecked());
    s.setValue("key/debounceMs", m_debounceSpin->value());
    s.setValue("audio/device", m_audioCombo->currentData().toByteArray());
    s.setValue("audio/freq", m_freqSpin->value());
    s.setValue("audio/volume", m_volSlider->value());
    s.setValue("audio/sidetone", m_sidetoneCheck->isChecked());
    s.setValue("audio/bufferMs", m_bufferSpin->value());
    s.setValue("speed/wpm", m_wpmSpin->value());
    s.setValue("speed/effWpm", m_effWpmSpin->value());
    s.setValue("speed/adaptive", m_adaptiveCheck->isChecked());
    s.setValue("koch/level", m_kochSpin->value());
    s.setValue("koch/groupLen", m_groupLenSpin->value());
    s.setValue("koch/groups", m_groupsSpin->value());
    s.setValue("send/playFirst", m_sendPlayFirst->isChecked());
    s.setValue("window/geometry", saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent *ev)
{
    saveSettings();
    disconnectKey();
    m_tone->stopAudio();
    ev->accept();
}

void MainWindow::refreshPorts()
{
    const QString cur = m_portCombo->currentText();
    m_portCombo->clear();
    m_portCombo->addItems(SerialKeySource::availablePorts());
    if (!cur.isEmpty())
        m_portCombo->setCurrentText(cur);
}

void MainWindow::refreshAudioDevices()
{
    m_audioCombo->clear();
    m_audioCombo->addItem(QStringLiteral("System default"), QByteArray());
    for (const QAudioDevice &d : QMediaDevices::audioOutputs())
        m_audioCombo->addItem(d.description(), d.id());
}

void MainWindow::restartAudio()
{
    QAudioDevice dev;
    const QByteArray id = m_audioCombo->currentData().toByteArray();
    for (const QAudioDevice &d : QMediaDevices::audioOutputs())
        if (d.id() == id)
            dev = d;
    QString err;
    if (m_tone->startAudio(dev, m_bufferSpin->value(), &err))
        m_audioStatus->setText(m_tone->status());
    else
        m_audioStatus->setText(QStringLiteral("<span style='color:#c33'>%1</span>").arg(err.toHtmlEscaped()));
}

// ------------------------------------------------------------------ key handling

void MainWindow::connectKey()
{
    disconnectKey();
    const int src = m_sourceCombo->currentIndex();
    if (src == SrcKeyboard)
        return;
    if (src == SrcSerial) {
        m_key = std::make_unique<SerialKeySource>(m_portCombo->currentText(),
                                                  SerialKeySource::Line(m_lineCombo->currentIndex()),
                                                  m_invertCheck->isChecked(), m_debounceSpin->value());
    } else {
        m_key = std::make_unique<GpioKeySource>(m_chipCombo->currentText(), unsigned(m_gpioLine->value()),
                                                m_activeLowCheck->isChecked(), m_pullUpCheck->isChecked(),
                                                m_debounceSpin->value());
    }
    connect(m_key.get(), &KeySource::keyChanged, this,
            [this](bool down, qint64 t) { setInput(InHardware, down, t); }, Qt::QueuedConnection);
    connect(m_key.get(), &KeySource::failed, this, [this](const QString &why) {
        disconnectKey();
        m_keyStatus->setText(QStringLiteral("<span style='color:#c33'>%1</span>").arg(why.toHtmlEscaped()));
    }, Qt::QueuedConnection);

    QString err;
    if (!m_key->start(&err)) {
        m_key.reset();
        m_keyStatus->setText(QStringLiteral("<span style='color:#c33'>%1</span>").arg(err.toHtmlEscaped().replace('\n', "<br>")));
        return;
    }
    m_keyStatus->setText(QStringLiteral("Connected: %1").arg(m_key->description()));
    m_connectBtn->setText(QStringLiteral("Disconnect"));
}

void MainWindow::disconnectKey()
{
    if (m_key) {
        m_key->stop();
        m_key.reset();
    }
    if (m_inputs & InHardware)
        setInput(InHardware, false, monotonicUs());
    if (m_connectBtn) {
        m_connectBtn->setText(QStringLiteral("Connect"));
        m_keyStatus->setText(QStringLiteral("Not connected"));
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::KeyPress || ev->type() == QEvent::KeyRelease) {
        auto *ke = static_cast<QKeyEvent *>(ev);
        if (ke->key() == Qt::Key_Space && m_tabs->currentIndex() != TabReceive && isActiveWindow()
            && !qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
            if (!ke->isAutoRepeat())
                setInput(InKeyboard, ev->type() == QEvent::KeyPress, monotonicUs());
            return true;
        }
    } else if (ev->type() == QEvent::ApplicationDeactivate && (m_inputs & InKeyboard)) {
        setInput(InKeyboard, false, monotonicUs());
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::setInput(Input which, bool down, qint64 tUs)
{
    const int before = m_inputs;
    m_inputs = down ? (m_inputs | which) : (m_inputs & ~which);
    const bool nowDown = m_inputs != 0;
    if ((before != 0) == nowDown)
        return;
    m_keyDown = nowDown;
    m_tone->setKeyed(nowDown);
    m_scope->addEdge(nowDown, tUs);
    onDecoded(nowDown ? m_decoder.keyDown(tUs) : m_decoder.keyUp(tUs));
}

// ------------------------------------------------------------------ decoding & stats

void MainWindow::onDecoded(const std::string &text)
{
    if (text.empty())
        return;
    const int tab = m_tabs->currentIndex();
    if (tab == TabFree || tab == TabReference) {
        m_freeText->moveCursor(QTextCursor::End);
        m_freeText->insertPlainText(qs(text));
        m_freeText->ensureCursorVisible();
        return;
    }
    if (tab == TabSend && m_sendActive && !m_sendNextTimer.isActive()) {
        for (const auto &sym : morse::symbols(text))
            if (sym != " ")
                m_sendGiven += qs(sym);
        const bool wordEnd = text.find(' ') != std::string::npos;
        m_sendAttempt->setText(m_sendGiven.isEmpty() ? QStringLiteral(" ") : m_sendGiven.toHtmlEscaped());
        const size_t want = morse::symbols(m_sendExpected.toStdString()).size();
        const size_t have = morse::symbols(m_sendGiven.toStdString()).size();
        if (have >= want || (wordEnd && have > 0))
            sendGrade();
    }
}

void MainWindow::updateLiveStats()
{
    m_wpmLabel->setText(QStringLiteral("<b>%1</b> WPM").arg(m_decoder.wpm(), 0, 'f', 1));
    const auto &st = m_decoder.stats();
    if (st.dits > 0 && st.dahs > 0)
        m_ratioLabel->setText(QStringLiteral("dit %1 ms · dah %2 ms · ratio %3 · weight %4")
                                  .arg(st.ditMs, 0, 'f', 0)
                                  .arg(st.dahMs, 0, 'f', 0)
                                  .arg(st.ratio(), 0, 'f', 2)
                                  .arg(st.weight(), 0, 'f', 2));
    QString pat = qs(m_decoder.pattern());
    pat.replace('.', QChar(0x00B7)).replace('-', QChar(0x2013));
    m_patternLabel->setText(pat);
    m_scope->setTiming(m_decoder.unitMs(), m_decoder.dotDashThresholdMs());
}

void MainWindow::applySpeed()
{
    if (m_effWpmSpin->value() > m_wpmSpin->value())
        m_effWpmSpin->setValue(m_wpmSpin->value());
    m_decoder.setAdaptive(m_adaptiveCheck->isChecked());
    m_decoder.setWpm(m_wpmSpin->value());
}

void MainWindow::updateKochLabel()
{
    m_trainer.setLevel(m_kochSpin->value());
    QStringList chars;
    for (const auto &c : m_trainer.charset())
        chars << qs(c);
    m_kochChars->setText(QStringLiteral("%1   <b>(new: %2)</b>")
                             .arg(chars.join(' ').toHtmlEscaped(), qs(m_trainer.newestChar()).toHtmlEscaped()));
}

void MainWindow::updateScore(QLabel *label)
{
    QString s;
    const double acc = m_trainer.recentAccuracy();
    if (acc < 0) {
        s = QStringLiteral("Accuracy: collecting…");
    } else {
        s = QStringLiteral("Accuracy (last %1 chars): <b>%2 %</b>").arg(m_trainer.recentCount()).arg(acc * 100, 0, 'f', 0);
    }
    const auto weak = m_trainer.weakest(4);
    if (!weak.empty()) {
        QStringList w;
        for (const auto &[sym, rate] : weak)
            w << QStringLiteral("%1 (%2 %)").arg(qs(sym).toHtmlEscaped()).arg(rate * 100, 0, 'f', 0);
        s += QStringLiteral(" · Weak: ") + w.join(QStringLiteral(", "));
    }
    if (m_trainer.readyToAdvance() && m_kochSpin->value() < m_kochSpin->maximum())
        s += QStringLiteral("<br><b style='color:#2a2'>≥ 90 % — time to add the next character (Koch %1).</b>")
                 .arg(m_kochSpin->value() + 1);
    label->setText(s);
}

QString MainWindow::renderResult(const Trainer::Result &r)
{
    QString html;
    for (size_t i = 0; i < r.expected.size(); ++i) {
        const QString sym = qs(r.expected[i]).toHtmlEscaped();
        html += r.ok[i] ? QStringLiteral("<span style='color:#2a2'>%1</span>").arg(sym)
                        : QStringLiteral("<span style='color:#d33;text-decoration:underline'><b>%1</b></span>").arg(sym);
    }
    return html;
}

void MainWindow::playText(const QString &text)
{
    m_tone->play(morse::timing(text.toStdString(), m_wpmSpin->value(), m_effWpmSpin->value()));
}

// ------------------------------------------------------------------ send drill

void MainWindow::sendNext()
{
    m_sendExpected = qs(m_trainer.group(m_groupLenSpin->value()));
    m_sendGiven.clear();
    m_sendTarget->setText(m_sendExpected);
    m_sendAttempt->setText(QStringLiteral(" "));
    m_decoder.tick(monotonicUs() + 10'000'000);   // flush any half-keyed character
    if (m_sendPlayFirst->isChecked())
        playText(m_sendExpected);
}

void MainWindow::sendGrade()
{
    const auto r = m_trainer.grade(m_sendExpected.toStdString(), m_sendGiven.toStdString());
    m_sendAttempt->setText(renderResult(r));
    m_sendHistory->append(QStringLiteral("<tt>%1 → %2</tt> &nbsp; %3 %")
                              .arg(m_sendExpected.toHtmlEscaped(), m_sendGiven.toHtmlEscaped())
                              .arg(r.accuracy() * 100, 0, 'f', 0));
    updateScore(m_sendScore);
    m_sendNextTimer.start();
}

// ------------------------------------------------------------------ receive

void MainWindow::receivePlay()
{
    m_recvExpected = qs(m_trainer.text(m_groupsSpin->value(), m_groupLenSpin->value()));
    m_recvInput->clear();
    m_recvInput->setFocus();
    m_recvReplayBtn->setEnabled(true);
    playText(m_recvExpected);
}

void MainWindow::receiveCheck()
{
    if (m_recvExpected.isEmpty())
        return;
    m_tone->stopPlayback();
    const auto r = m_trainer.grade(m_recvExpected.toStdString(), m_recvInput->text().toStdString());
    m_recvHistory->append(QStringLiteral("<p><tt>%1</tt><br><tt>%2</tt> &nbsp; %3 %</p>")
                              .arg(renderResult(r), m_recvInput->text().toUpper().toHtmlEscaped())
                              .arg(r.accuracy() * 100, 0, 'f', 0));
    updateScore(m_recvScore);
    m_recvExpected.clear();
    m_recvReplayBtn->setEnabled(false);
    m_recvInput->clear();
}
