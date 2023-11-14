#ifndef TERMINALDISPLAY_H
#define TERMINALDISPLAY_H

#include <QWidget>
#include <QPlainTextEdit>

class TerminalDisplayBase : public QWidget
{
    Q_OBJECT
public:
    using QWidget::QWidget;
    virtual void AddOutput(const QString& input) = 0;
    virtual void setWrap(bool wrap) = 0;
    virtual bool wrap() = 0;
    virtual void setFont(const QFont& font) = 0;
    virtual void scrollToEnd() = 0;
signals:
public slots:
    virtual void clear() = 0;
private:
};


class TerminalDisplay : public TerminalDisplayBase
{
    Q_OBJECT
public:
    explicit TerminalDisplay(QWidget *parent = nullptr);

    void AddOutput(const QString& input) override;
    void clear() override;
    void setWrap(bool value) override;
    bool wrap() override;
    void setFont(const QFont& font) override;
    void scrollToEnd() override;
    QPlainTextEdit* TextWidget();
signals:

private:
    QPlainTextEdit* outputTextEdit;
};

#endif // TERMINALDISPLAY_H
