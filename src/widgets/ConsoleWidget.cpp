#include <QScrollBar>
#include <QMenu>
#include <QCompleter>
#include <QAction>
#include <QShortcut>
#include <QStringListModel>
#include <QTimer>
#include <QSettings>
#include <QDir>
#include <QUuid>
#include <iostream>
#include "core/Cutter.h"
#include "ConsoleWidget.h"
#include "ui_ConsoleWidget.h"
#include "common/Helpers.h"
#include "common/SvgIconEngine.h"
#include "WidgetShortcuts.h"

#ifdef Q_OS_WIN
#    include <windows.h>
#    include <io.h>
#    define dup2 _dup2
#    define dup _dup
#    define fileno _fileno
#    define fdopen _fdopen
#    define PIPE_SIZE 65536 // Match Linux size
#    define PIPE_NAME "\\\\.\\pipe\\cutteroutput-%1"
#else
#    include <unistd.h>
#    define PIPE_READ (0)
#    define PIPE_WRITE (1)
#    define STDIN_PIPE_NAME "%1/cutter-stdin-%2"
#endif

#define CONSOLE_RIZIN_INPUT ("Rizin Console")
#define CONSOLE_DEBUGEE_INPUT ("Debugee Input")

static const int invalidHistoryPos = -1;

static const char *consoleWrapSettingsKey = "console.wrap";

ConsoleWidget::ConsoleWidget(MainWindow *main)
    : CutterDockWidget(main),
      ui(new Ui::ConsoleWidget),
      debugOutputEnabled(true),
      maxHistoryEntries(100),
      lastHistoryPosition(invalidHistoryPos),
      completer(nullptr),
      historyUpShortcut(nullptr),
      historyDownShortcut(nullptr)
{
    ui->setupUi(this);

    // Adjust console lineedit
    ui->rzInputLineEdit->setTextMargins(10, 0, 0, 0);
    ui->debugeeInputLineEdit->setTextMargins(10, 0, 0, 0);

    //terminalDisplay = new TerminalDisplay(ui->terminalArea);
    terminalDisplay = new QTerminalDisplay(ui->terminalArea);
    ui->terminalArea->layout()->addWidget(terminalDisplay);

    setupFont();

    // Ctrl+` and ';' to toggle console widget
    QAction *toggleConsole = toggleViewAction();
    QList<QKeySequence> toggleShortcuts;
    toggleShortcuts << widgetShortcuts["ConsoleWidget"]
                    << widgetShortcuts["ConsoleWidgetAlternative"];
    toggleConsole->setShortcuts(toggleShortcuts);
    connect(toggleConsole, &QAction::triggered, this, [this, toggleConsole]() {
        if (toggleConsole->isChecked()) {
            widgetToFocusOnRaise()->setFocus();
        }
    });

    QAction *actionClear = new QAction(tr("Clear Output"), this);
    connect(actionClear, &QAction::triggered, terminalDisplay, &TerminalDisplayBase::clear);
    addAction(actionClear);

    // Ctrl+l to clear the output
    actionClear->setShortcut(Qt::CTRL | Qt::Key_L);
    actionClear->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    actions.append(actionClear);

    actionWrapLines = new QAction(tr("Wrap Lines"), terminalDisplay);
    actionWrapLines->setCheckable(true);
    setWrap(QSettings().value(consoleWrapSettingsKey, true).toBool());
    connect(actionWrapLines, &QAction::triggered, this, [this](bool checked) { setWrap(checked); });
    actions.append(actionWrapLines);

    // Completion
    completionActive = false;
    completer = new QCompleter(&completionModel, this);
    completer->setMaxVisibleItems(20);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchStartsWith);
    ui->rzInputLineEdit->setCompleter(completer);

    connect(ui->rzInputLineEdit, &QLineEdit::textEdited, this, &ConsoleWidget::updateCompletion);
    updateCompletion();

    // Set console output context menu
    terminalDisplay->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(terminalDisplay, &QWidget::customContextMenuRequested, this,
            &ConsoleWidget::showCustomContextMenu);

    // Esc clears rzInputLineEdit and debugeeInputLineEdit (like OmniBar)
    QShortcut *rizin_clear_shortcut =
            new QShortcut(QKeySequence(Qt::Key_Escape), ui->rzInputLineEdit);
    connect(rizin_clear_shortcut, &QShortcut::activated, this, &ConsoleWidget::clear);
    rizin_clear_shortcut->setContext(Qt::WidgetShortcut);

    QShortcut *debugee_clear_shortcut =
            new QShortcut(QKeySequence(Qt::Key_Escape), ui->debugeeInputLineEdit);
    connect(debugee_clear_shortcut, &QShortcut::activated, this, &ConsoleWidget::clear);
    debugee_clear_shortcut->setContext(Qt::WidgetShortcut);

    // Up and down arrows show history
    historyUpShortcut = new QShortcut(QKeySequence(Qt::Key_Up), ui->rzInputLineEdit);
    connect(historyUpShortcut, &QShortcut::activated, this, &ConsoleWidget::historyPrev);
    historyUpShortcut->setContext(Qt::WidgetShortcut);

    historyDownShortcut = new QShortcut(QKeySequence(Qt::Key_Down), ui->rzInputLineEdit);
    connect(historyDownShortcut, &QShortcut::activated, this, &ConsoleWidget::historyNext);
    historyDownShortcut->setContext(Qt::WidgetShortcut);

    QShortcut *completionShortcut = new QShortcut(QKeySequence(Qt::Key_Tab), ui->rzInputLineEdit);
    connect(completionShortcut, &QShortcut::activated, this, &ConsoleWidget::triggerCompletion);

    connect(ui->rzInputLineEdit, &QLineEdit::editingFinished, this,
            &ConsoleWidget::disableCompletion);

    connect(Config(), &Configuration::fontsUpdated, this, &ConsoleWidget::setupFont);

    connect(ui->inputCombo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ConsoleWidget::onIndexChange);

    connect(Core(), &CutterCore::debugTaskStateChanged, this, [=]() {
        if (Core()->isRedirectableDebugee()) {
            ui->inputCombo->setVisible(true);
        } else {
            ui->inputCombo->setVisible(false);
            // Return to the rizin console
            ui->inputCombo->setCurrentIndex(ui->inputCombo->findText(CONSOLE_RIZIN_INPUT));
        }
    });

    completer->popup()->installEventFilter(this);

    if (Config()->getOutputRedirectionEnabled()) {
        redirectOutput();
    }
}

ConsoleWidget::~ConsoleWidget()
{
#ifndef Q_OS_WIN
    ::close(stdinFile);
    remove(stdinFifoPath.toStdString().c_str());
#endif
}

bool ConsoleWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (completer && obj == completer->popup() &&
        // disable up/down shortcuts if completer is shown
        (event->type() == QEvent::Type::Show || event->type() == QEvent::Type::Hide)) {
        bool enabled = !completer->popup()->isVisible();
        if (historyUpShortcut) {
            historyUpShortcut->setEnabled(enabled);
        }
        if (historyDownShortcut) {
            historyDownShortcut->setEnabled(enabled);
        }
    }
    return false;
}

QWidget *ConsoleWidget::widgetToFocusOnRaise()
{
    return ui->rzInputLineEdit;
}

void ConsoleWidget::setupFont()
{
    terminalDisplay->setFont(Config()->getFont());
}

void ConsoleWidget::addOutput(const QString &msg)
{
    terminalDisplay->AddOutput(msg);
    scrollOutputToEnd();
}

void ConsoleWidget::addDebugOutput(const QString &msg)
{
    if (debugOutputEnabled) {
        QString formattedMessage = "\033[31m [DEBUG]:\t" + msg + "\033[0m";
        terminalDisplay->AddOutput(formattedMessage);
        scrollOutputToEnd();
    }
}

void ConsoleWidget::focusInputLineEdit()
{
    ui->rzInputLineEdit->setFocus();
}

void ConsoleWidget::executeCommand(const QString &command)
{
    if (!commandTask.isNull()) {
        return;
    }
    ui->rzInputLineEdit->setEnabled(false);

    QString cmd_line = "[" + RzAddressString(Core()->getOffset()) + "]> " + command + "\n";
    addOutput(cmd_line);

    RVA oldOffset = Core()->getOffset();
    commandTask =
            QSharedPointer<CommandTask>(new CommandTask(command, CommandTask::ColorMode::MODE_16M));
    connect(commandTask.data(), &CommandTask::finished, this,
            [this, cmd_line, command, oldOffset](const QString &result) {
                terminalDisplay->AddOutput(result);
                scrollOutputToEnd();
                historyAdd(command);
                commandTask.clear();
                ui->rzInputLineEdit->setEnabled(true);
                ui->rzInputLineEdit->setFocus();

                if (oldOffset != Core()->getOffset()) {
                    Core()->updateSeek();
                }
            });

    Core()->getAsyncTaskManager()->start(commandTask);
}

void ConsoleWidget::sendToStdin(const QString &input)
{
#ifndef Q_OS_WIN
    write(stdinFile, (input + "\n").toStdString().c_str(), input.size() + 1);
    fsync(stdinFile);
    addOutput("Sent input: '" + input + "'\n");
#else
    // Stdin redirection isn't currently available in windows because console applications
    // with stdin already get their own console window with stdin when they are launched
    // that the user can type into.
    addOutput("Unsupported feature");
#endif
}

void ConsoleWidget::onIndexChange()
{
    QString console = ui->inputCombo->currentText();
    if (console == CONSOLE_DEBUGEE_INPUT) {
        ui->rzInputLineEdit->setVisible(false);
        ui->debugeeInputLineEdit->setVisible(true);
    } else if (console == CONSOLE_RIZIN_INPUT) {
        ui->rzInputLineEdit->setVisible(true);
        ui->debugeeInputLineEdit->setVisible(false);
    }
}

void ConsoleWidget::setWrap(bool wrap)
{
    QSettings().setValue(consoleWrapSettingsKey, wrap);
    actionWrapLines->setChecked(wrap);
    terminalDisplay->setWrap(wrap);
}

void ConsoleWidget::on_rzInputLineEdit_returnPressed()
{
    QString input = ui->rzInputLineEdit->text();
    if (input.isEmpty()) {
        return;
    }
    executeCommand(input);
    ui->rzInputLineEdit->clear();
}

void ConsoleWidget::on_debugeeInputLineEdit_returnPressed()
{
    QString input = ui->debugeeInputLineEdit->text();
    if (input.isEmpty()) {
        return;
    }
    sendToStdin(input);
    ui->debugeeInputLineEdit->clear();
}

void ConsoleWidget::on_execButton_clicked()
{
    on_rzInputLineEdit_returnPressed();
}

void ConsoleWidget::showCustomContextMenu(const QPoint &pt)
{
    actionWrapLines->setChecked(terminalDisplay->wrap());

    QMenu *menu = new QMenu(terminalDisplay);
    menu->addActions(actions);
    menu->exec(terminalDisplay->mapToGlobal(pt));
    menu->deleteLater();
}

void ConsoleWidget::historyNext()
{
    if (!history.isEmpty()) {
        if (lastHistoryPosition > invalidHistoryPos) {
            if (lastHistoryPosition >= history.size()) {
                lastHistoryPosition = history.size() - 1;
            }

            --lastHistoryPosition;

            if (lastHistoryPosition >= 0) {
                ui->rzInputLineEdit->setText(history.at(lastHistoryPosition));
            } else {
                ui->rzInputLineEdit->clear();
            }
        }
    }
}

void ConsoleWidget::historyPrev()
{
    if (!history.isEmpty()) {
        if (lastHistoryPosition >= history.size() - 1) {
            lastHistoryPosition = history.size() - 2;
        }

        ui->rzInputLineEdit->setText(history.at(++lastHistoryPosition));
    }
}

void ConsoleWidget::triggerCompletion()
{
    if (completionActive) {
        return;
    }
    completionActive = true;
    updateCompletion();
    completer->complete();
}

void ConsoleWidget::disableCompletion()
{
    if (!completionActive) {
        return;
    }
    completionActive = false;
    updateCompletion();
    completer->popup()->hide();
}

void ConsoleWidget::updateCompletion()
{
    if (!completionActive) {
        completionModel.setStringList({});
        return;
    }

    auto current = ui->rzInputLineEdit->text();
    auto completions = Core()->autocomplete(current, RZ_LINE_PROMPT_DEFAULT);
    int lastSpace = current.lastIndexOf(' ');
    if (lastSpace >= 0) {
        current = current.left(lastSpace + 1);
        for (auto &s : completions) {
            s = current + s;
        }
    }
    completionModel.setStringList(completions);
}

void ConsoleWidget::clear()
{
    disableCompletion();
    ui->rzInputLineEdit->clear();
    ui->debugeeInputLineEdit->clear();

    invalidateHistoryPosition();

    // Close the potential shown completer popup
    ui->rzInputLineEdit->clearFocus();
    ui->rzInputLineEdit->setFocus();
}

void ConsoleWidget::scrollOutputToEnd()
{
    terminalDisplay->scrollToEnd();
}

void ConsoleWidget::historyAdd(const QString &input)
{
    if (history.size() + 1 > maxHistoryEntries) {
        history.removeLast();
    }

    history.prepend(input);

    invalidateHistoryPosition();
}
void ConsoleWidget::invalidateHistoryPosition()
{
    lastHistoryPosition = invalidHistoryPos;
}

void ConsoleWidget::processQueuedOutput()
{
    // Partial lines are ignored since carriage return is currently unsupported
    while (pipeSocket->canReadLine()) {
        QString output = QString(pipeSocket->readLine());

        /*if (origStderr) {
            fprintf(origStderr, "%s", output.toStdString().c_str());
        }*/

        terminalDisplay->AddOutput(output);
        scrollOutputToEnd();
    }
}

// Haiku doesn't have O_ASYNC
#ifdef Q_OS_HAIKU
#    define O_ASYNC O_NONBLOCK
#endif

void ConsoleWidget::redirectOutput()
{
    // Make sure that we are running in a valid console with initialized output handles
    if (0 > fileno(stderr) && 0 > fileno(stdout)) {
        addOutput("Run cutter in a console to enable rizin output redirection into this widget.");
        return;
    }

    pipeSocket = new QLocalSocket();
    fprintf(stderr, "originerrTest1\n");

    origStdin = fdopen(dup(fileno(stdin)), "r");
    origStderr = fdopen(dup(fileno(stderr)), "a");
    origStdout = fdopen(dup(fileno(stdout)), "a");
    fprintf(origStderr, "originerrTest2\n");
    fflush(origStderr);
#ifdef Q_OS_WIN
    QString pipeName = QString::fromLatin1(PIPE_NAME).arg(QUuid::createUuid().toString());

    SECURITY_ATTRIBUTES attributes = { sizeof(SECURITY_ATTRIBUTES), 0, false };
    hWrite =
            CreateNamedPipeW((wchar_t *)pipeName.utf16(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                             PIPE_TYPE_BYTE | PIPE_WAIT, 1, PIPE_SIZE, PIPE_SIZE, 0, &attributes);

    int writeFd = _open_osfhandle((intptr_t)hWrite, _O_WRONLY | _O_TEXT);
    dup2(writeFd, fileno(stdout));
    dup2(writeFd, fileno(stderr));

    pipeSocket->connectToServer(pipeName, QIODevice::ReadOnly);
#else
    /*if (socketpair(AF_LOCAL, SOCK_STREAM |  SOCK_NONBLOCK, 0, redirectPipeFds)) {
        addOutput("socketpair failed\n");
    }
    stdinFile = redirectPipeFds[PIPE_READ];*/
    pipe(redirectPipeFds);
    int readPipe[2];
    pipe(readPipe);

    fcntl(redirectPipeFds[PIPE_READ], F_SETFL, O_ASYNC | O_NONBLOCK);
    stdinFile = readPipe[PIPE_WRITE];
    //fcntl(readPipe[PIPE_READ], F_SETFL, O_ASYNC | O_NONBLOCK);
    /*pipe(redirectPipeFds);
    stdinFifoPath = QString(STDIN_PIPE_NAME).arg(QDir::tempPath(), QUuid::createUuid().toString());
    mkfifo(stdinFifoPath.toStdString().c_str(), (mode_t)0777);
    stdinFile = open(stdinFifoPath.toStdString().c_str(), O_RDWR | O_ASYNC);*/

    if (dup2(readPipe[PIPE_READ], fileno(stdin)) < 0) {
        addOutput("dup2 1 failed\n");
    }
    if (dup2(redirectPipeFds[PIPE_WRITE], fileno(stderr)) < 0) {
        addOutput("dup2 2 failed\n");
    };
    if (dup2(redirectPipeFds[PIPE_WRITE], fileno(stdout)) < 0) {
        addOutput("dup2 3 failed\n");
    };

    // Attempt to force line buffering to avoid calling processQueuedOutput
    // for partial lines
    //setlinebuf(stderr);
    //setlinebuf(stdout);

    // Configure the pipe to work in async mode
    //fcntl(redirectPipeFds[PIPE_READ], F_SETFL, O_ASYNC | O_NONBLOCK);

    pipeSocket->setSocketDescriptor(redirectPipeFds[PIPE_READ]);
    pipeSocket->connectToServer(QIODevice::ReadWrite);

    pipeSocket->moveToThread(&moveThread);
    connect(&moveThread, &QThread::finished, pipeSocket, &QObject::deleteLater);
    {
        auto pipeSocket = this->pipeSocket;
        auto terminalDisplay = this->terminalDisplay;
        auto origStderr = this->origStderr;

        int terminalDisplayFd = -1;
        if (auto td = dynamic_cast<QTerminalDisplay*>(terminalDisplay)) {
            terminalDisplayFd = td->getFd();

        }

        if (terminalDisplayFd > 0) {
            connect(pipeSocket, &QIODevice::readyRead, pipeSocket, [pipeSocket, terminalDisplayFd, origStderr](){
                while (pipeSocket->canReadLine()) {
                    auto output = QString(pipeSocket->readLine()).toUtf8();
                    if (origStderr) {
                        fprintf(origStderr, "%s", output.toStdString().c_str());
                    }
                    write(terminalDisplayFd, output.data(), output.size());
                    // TODO: use nonblocking write here
                    //scrollOutputToEnd();
                }
            }, Qt::QueuedConnection);
        } else {
            connect(pipeSocket, &QIODevice::readyRead, pipeSocket, [pipeSocket, terminalDisplay, origStderr](){
                if (origStderr) {
                    fprintf(origStderr, "got somedata\n");
                    fflush(origStderr);
                }
                while (pipeSocket->canReadLine()) {
                    QString output = QString(pipeSocket->readLine());
                    if (origStderr) {
                        fprintf(origStderr, "%s", output.toStdString().c_str());
                    }
                    QMetaObject::invokeMethod(terminalDisplay,  [output, terminalDisplay](){
                                terminalDisplay->AddOutput(output);
                            } ,Qt::QueuedConnection);
                    //scrollOutputToEnd();
                }
            }, Qt::QueuedConnection);
        }
    }
    connect(terminalDisplay, &TerminalDisplayBase::dataAvailable, this, [this](QByteArray data){
        write(stdinFile, data.data(), data.size());
        fsync(stdinFile);
    });
    fprintf(origStderr, "originerrTest\n");
    fflush(origStderr);
    moveThread.start();

#endif

    //connect(pipeSocket, &QIODevice::readyRead, this, &ConsoleWidget::processQueuedOutput, Qt::QueuedConnection);
}
