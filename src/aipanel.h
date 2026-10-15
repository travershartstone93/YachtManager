#pragma once
#include <QDockWidget>

class QPushButton;
class QLabel;
class QTextBrowser;
class QProcess;

// Right-side AI sidebar: on "Summarize", pipes a per-yacht digest of the
// database to the claude CLI and shows its plain-English summary.
class AiPanel : public QDockWidget {
    Q_OBJECT
public:
    explicit AiPanel(QWidget *parent = nullptr);
    void summarize();
signals:
    void done(bool ok, const QString &text);
private:
    QString buildDigest() const;
    QPushButton *m_btn;
    QLabel *m_status;
    QTextBrowser *m_out;
    QProcess *m_proc = nullptr;
};
