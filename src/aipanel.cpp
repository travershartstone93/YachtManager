#include "aipanel.h"
#include "pages.h"
#include <QtWidgets>
#include <QtSql>
#include <QProcess>

AiPanel::AiPanel(QWidget *parent) : QDockWidget("AI summary", parent) {
    setObjectName("aiPanel");
    setAllowedAreas(Qt::RightDockWidgetArea);
    setFeatures(QDockWidget::DockWidgetClosable);

    auto *body = new QWidget;
    auto *lay = new QVBoxLayout(body);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    auto *head = new QLabel(
        "Plain-English state of every yacht, written by Claude from the"
        " current database. Nothing runs until you press the button.");
    head->setWordWrap(true);
    head->setObjectName("cardSub");
    lay->addWidget(head);

    auto *row = new QHBoxLayout;
    m_btn = new QPushButton("Summarize fleet");
    connect(m_btn, &QPushButton::clicked, this, [this] { summarize(); });
    row->addWidget(m_btn);
    m_status = new QLabel;
    m_status->setObjectName("cardSub");
    row->addWidget(m_status, 1);
    lay->addLayout(row);

    m_out = new QTextBrowser;
    m_out->setOpenExternalLinks(false);
    m_out->setPlaceholderText("No summary yet.");
    lay->addWidget(m_out, 1);

    setWidget(body);
    setMinimumWidth(420);
}

QString AiPanel::buildDigest() const {
    QString d;
    QSqlQuery q("SELECT b.id, b.name, b.active, b.make, b.model, b.year,"
                " b.first_month, b.last_month, b.closing_balance"
                " FROM boats b ORDER BY b.active DESC, b.name");
    while (q.next()) {
        int id = q.value(0).toInt();
        d += QString("%1 (%2 %3 %4, %5): books %6 to %7, balance %8.")
                 .arg(q.value(1).toString(), q.value(3).toString(),
                      q.value(4).toString(), q.value(5).toString(),
                      q.value(2).toInt() ? "in fleet" : "LEFT FLEET",
                      q.value(6).toString(), q.value(7).toString(),
                      money(zeroSnap(q.value(8).toDouble())));
        QSqlQuery m;
        m.prepare("SELECT month, exp_debits, exp_credits, income_total,"
                  " ta_usd+sm_usd+as_usd FROM statements WHERE boat_id=?"
                  " ORDER BY month DESC LIMIT 3");
        m.addBindValue(id);
        m.exec();
        QStringList recent;
        while (m.next())
            recent << QString("%1 spent %2, labour %3, income %4")
                          .arg(m.value(0).toString(),
                               money(m.value(1).toDouble()),
                               money(m.value(4).toDouble()),
                               money(m.value(3).toDouble()));
        if (!recent.isEmpty())
            d += " Last months: " + recent.join("; ") + ".";
        QSqlQuery e;
        e.prepare("SELECT description, debit FROM expense_lines WHERE boat_id=?"
                  " AND debit >= 300 AND description != ''"
                  " ORDER BY month DESC, debit DESC LIMIT 4");
        e.addBindValue(id);
        e.exec();
        QStringList items;
        while (e.next())
            items << QString("%1 (%2)")
                         .arg(e.value(0).toString().left(90),
                              money(e.value(1).toDouble()));
        if (!items.isEmpty())
            d += " Recent notable work: " + items.join(" | ") + ".";
        d += "\n";
    }
    return d;
}

void AiPanel::summarize() {
    if (m_proc) return;
    m_btn->setEnabled(false);
    m_status->setText("Claude is reading the books…");
    m_out->setPlainText("");

    QString prompt =
        "You are summarizing a BVI charter-fleet management database for the"
        " fleet manager. Below is one line per yacht with its financial state"
        " (negative balance = the owner owes the charter company)."
        " Write a short plain-English summary of the state of EACH yacht in"
        " fleet: 1-2 sentences each, markdown, with the yacht name in bold."
        " Start with a 3-sentence overall fleet picture, then list yachts that"
        " need attention first (big debts, heavy recent spending, unusual"
        " activity), then the healthy ones briefly. Finish with one short"
        " paragraph on the departed boats only if something is still owed."
        " No tables, no jargon.\n\nDATA:\n" + buildDigest();

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus) {
                QString out = QString::fromUtf8(m_proc->readAllStandardOutput());
                QString err = QString::fromUtf8(m_proc->readAllStandardError());
                m_proc->deleteLater();
                m_proc = nullptr;
                m_btn->setEnabled(true);
                if (code == 0 && !out.trimmed().isEmpty()) {
                    m_out->setMarkdown(out);
                    m_status->setText("Done " + QTime::currentTime().toString("HH:mm"));
                    emit done(true, out);
                } else {
                    m_out->setPlainText("Claude call failed:\n" + err.right(1500));
                    m_status->setText("Failed");
                    emit done(false, err);
                }
            });
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (!m_proc) return;
        m_out->setPlainText("Could not run the 'claude' command. Is Claude Code"
                            " installed and on PATH?");
        m_status->setText("Failed");
        m_btn->setEnabled(true);
        m_proc->deleteLater();
        m_proc = nullptr;
        emit done(false, "claude CLI not found");
    });
    m_proc->start("claude", {"-p", "--output-format", "text"});
    m_proc->write(prompt.toUtf8());
    m_proc->closeWriteChannel();
}
