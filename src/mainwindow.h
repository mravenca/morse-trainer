#pragma once
#include "decoder.h"
#include "trainer.h"

#include <QMainWindow>
#include <QTimer>

#include <memory>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTabWidget;
class QTextBrowser;
class KeyScope;
class KeySource;
class ToneGenerator;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void closeEvent(QCloseEvent *ev) override;

private:
    enum Source { SrcKeyboard = 0, SrcSerial = 1, SrcGpio = 2 };
    enum Tab { TabFree = 0, TabSend = 1, TabReceive = 2, TabReference = 3 };
    enum Input { InKeyboard = 1, InButton = 2, InHardware = 4 };

    QWidget *buildSettings();
    QWidget *buildFreeTab();
    QWidget *buildSendTab();
    QWidget *buildReceiveTab();
    QWidget *buildReferenceTab();

    void loadSettings();
    void saveSettings();
    void refreshPorts();
    void fitSourceStack();
    void refreshAudioDevices();
    void restartAudio();
    void connectKey();
    void disconnectKey();

    void setInput(Input which, bool down, qint64 tUs);
    void onDecoded(const std::string &text);
    void updateLiveStats();
    void applySpeed();
    void updateKochLabel();
    void updateScore(QLabel *label);

    void sendNext();
    void sendGrade();
    void receivePlay();
    void receiveCheck();
    void playText(const QString &text);

    static QString renderResult(const Trainer::Result &r);

    // Engine
    Decoder m_decoder;
    Trainer m_trainer;
    ToneGenerator *m_tone = nullptr;
    std::unique_ptr<KeySource> m_key;
    QTimer m_tick;
    QTimer m_statsTimer;
    int m_inputs = 0;          // bitmask of Input currently held
    bool m_keyDown = false;

    // Settings widgets
    QComboBox *m_sourceCombo;
    QStackedWidget *m_sourceStack;
    QComboBox *m_portCombo;
    QComboBox *m_lineCombo;
    QCheckBox *m_invertCheck;
    QComboBox *m_chipCombo;
    QSpinBox *m_gpioLine;
    QCheckBox *m_activeLowCheck;
    QCheckBox *m_pullUpCheck;
    QSpinBox *m_debounceSpin;
    QPushButton *m_connectBtn;
    QLabel *m_keyStatus;

    QComboBox *m_audioCombo;
    QSpinBox *m_freqSpin;
    QSlider *m_volSlider;
    QCheckBox *m_sidetoneCheck;
    QSpinBox *m_bufferSpin;
    QLabel *m_audioStatus;

    QSpinBox *m_wpmSpin;
    QSpinBox *m_effWpmSpin;
    QCheckBox *m_adaptiveCheck;

    QSpinBox *m_kochSpin;
    QLabel *m_kochChars;
    QSpinBox *m_groupLenSpin;
    QSpinBox *m_groupsSpin;

    // Shared view
    QTabWidget *m_tabs;
    KeyScope *m_scope;
    QLabel *m_wpmLabel;
    QLabel *m_ratioLabel;
    QLabel *m_patternLabel;
    QPushButton *m_keyButton;

    // Free keying
    QPlainTextEdit *m_freeText;

    // Send drill
    QLabel *m_sendTarget;
    QLabel *m_sendAttempt;
    QTextBrowser *m_sendHistory;
    QLabel *m_sendScore;
    QCheckBox *m_sendPlayFirst;
    QPushButton *m_sendStartBtn;
    QString m_sendExpected;
    QString m_sendGiven;
    bool m_sendActive = false;
    QTimer m_sendNextTimer;

    // Receive
    QPushButton *m_recvPlayBtn;
    QPushButton *m_recvReplayBtn;
    QLineEdit *m_recvInput;
    QTextBrowser *m_recvHistory;
    QLabel *m_recvScore;
    QString m_recvExpected;
};
