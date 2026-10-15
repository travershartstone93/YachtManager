#include "pages.h"
#include "chartwidget.h"
#include <QtWidgets>
#include <QtSql>

QString money(double v) {
    if (std::abs(v) < 0.005) v = 0;  // avoid "-$0.00"
    QLocale us(QLocale::English, QLocale::UnitedStates);
    QString s = us.toString(std::abs(v), 'f', 2);
    return (v < 0 ? "-$" : "$") + s;
}

void setMoneyItem(QTableWidget *t, int row, int col, double v, bool colorNeg) {
    auto *it = new QTableWidgetItem(money(v));
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    it->setData(Qt::UserRole, v);
    if (colorNeg && v < 0) it->setForeground(QBrush(QColor(200, 60, 60)));
    t->setItem(row, col, it);
}

void setAmountItem(QTableWidget *t, int row, int col, double v) {
    v = zeroSnap(v);
    auto *it = new QTableWidgetItem(v == 0 ? QString() : money(v));
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    it->setData(Qt::UserRole, v);
    if (v < 0) it->setForeground(QBrush(QColor(200, 60, 60)));
    t->setItem(row, col, it);
}

void setHoursItem(QTableWidget *t, int row, int col, double v) {
    auto *it = new QTableWidgetItem(
        v == 0 ? QString() : QString::number(v, 'f', 1));
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    it->setData(Qt::UserRole, v);
    t->setItem(row, col, it);
}

static QTableWidget *makeTable(const QStringList &headers) {
    auto *t = new QTableWidget;
    t->setColumnCount(headers.size());
    t->setHorizontalHeaderLabels(headers);
    t->horizontalHeader()->setStretchLastSection(true);
    t->verticalHeader()->setVisible(false);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setAlternatingRowColors(true);
    t->setShowGrid(false);
    t->verticalHeader()->setDefaultSectionSize(30);
    t->setFocusPolicy(Qt::NoFocus);
    return t;
}

// Scale a photo to fill the target size, cropping the overflow.
static QPixmap coverPixmap(const QString &path, const QSize &size) {
    QPixmap src(path);
    if (src.isNull()) return QPixmap();
    QPixmap scaled = src.scaled(size, Qt::KeepAspectRatioByExpanding,
                                Qt::SmoothTransformation);
    QRect crop((scaled.width() - size.width()) / 2,
               (scaled.height() - size.height()) / 2,
               size.width(), size.height());
    return scaled.copy(crop);
}

static void applyPhoto(QLabel *l, const QString &path, const QSize &size,
                       int radius = 8) {
    l->setFixedSize(size);
    QPixmap px = coverPixmap(path, size);
    if (px.isNull()) {
        l->setAlignment(Qt::AlignCenter);
        l->setText("⚓");
        l->setStyleSheet(QString(
            "background: #16324a; color: #6d8aa3; font-size: 42px;"
            "border-radius: %1px;").arg(radius));
        return;
    }
    // round the corners
    QPixmap rounded(size);
    rounded.fill(Qt::transparent);
    QPainter p(&rounded);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(QPointF(0, 0), QSizeF(size)), radius, radius);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, px);
    p.end();
    l->setStyleSheet(QString());
    l->setPixmap(rounded);
}

static QLabel *photoLabel(const QString &path, const QSize &size,
                          int radius = 8) {
    auto *l = new QLabel;
    applyPhoto(l, path, size, radius);
    return l;
}

// ---------------- Fleet (boat cards) ----------------

BoatCard::BoatCard(const QString &name, const QString &subtitle, double balance,
                   const QString &imagePath, QWidget *parent)
    : QFrame(parent), m_name(name) {
    setObjectName("boatCard");
    setCursor(Qt::PointingHandCursor);
    setFixedSize(228, 240);
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(4);
    lay->addWidget(photoLabel(imagePath, QSize(208, 126)));
    auto *nm = new QLabel(name);
    nm->setObjectName("cardName");
    nm->setFixedHeight(20);
    lay->addWidget(nm);
    auto *sub = new QLabel(subtitle);
    sub->setObjectName("cardSub");
    sub->setWordWrap(true);
    sub->setFixedHeight(42);
    sub->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    lay->addWidget(sub);
    lay->addStretch();
    auto *bal = new QLabel(money(balance));
    bal->setObjectName(balance < 0 ? "badgeNeg" : "badgePos");
    bal->setAlignment(Qt::AlignCenter);
    bal->setFixedHeight(24);
    lay->addWidget(bal);
}

void BoatCard::mouseReleaseEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->pos()))
        emit clicked(m_name);
    QFrame::mouseReleaseEvent(e);
}

FleetPage::FleetPage(QWidget *parent) : QWidget(parent) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 14, 18, 0);
    m_header = new QLabel;
    m_header->setTextFormat(Qt::RichText);
    outer->addWidget(m_header);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_grid = new QWidget;
    m_grid->setObjectName("fleetGrid");
    scroll->setWidget(m_grid);
    outer->addWidget(scroll);
    new QGridLayout(m_grid);
}

void FleetPage::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    relayout();
}

void FleetPage::relayout() {
    auto *grid = static_cast<QGridLayout *>(m_grid->layout());
    QLayoutItem *it;
    while ((it = grid->takeAt(0)))
        delete it;  // widgets stay alive, only layout items go
    const int cardW = 228 + 14;
    int perRow = qMax(1, (width() - 36 - 12) / cardW);
    int col = 0, row = 0;
    auto place = [&](const QList<BoatCard *> &cards) {
        for (BoatCard *c : cards) {
            grid->addWidget(c, row, col);
            if (++col == perRow) { col = 0; ++row; }
        }
        if (col) { col = 0; ++row; }
    };
    place(m_active);
    if (m_leftHeader) {
        m_leftHeader->setVisible(!m_departed.isEmpty());
        if (!m_departed.isEmpty()) {
            grid->addWidget(m_leftHeader, row, 0, 1, perRow);
            ++row;
            place(m_departed);
        }
    }
    grid->setRowStretch(row + 1, 1);
    grid->setColumnStretch(perRow, 1);
}

void FleetPage::reload() {
    auto *grid = static_cast<QGridLayout *>(m_grid->layout());
    QLayoutItem *it;
    while ((it = grid->takeAt(0))) {
        delete it->widget();
        delete it;
    }
    m_active.clear();
    m_departed.clear();
    delete m_leftHeader;  // may not have been in the grid
    m_leftHeader = new QLabel(
        "<span style='font-size:16px; font-weight:600; color:#6b7a87'>"
        "Left the fleet</span> <span style='color:#8fa1b0'>— books kept for "
        "history; not on conchcharters.com</span>");
    m_leftHeader->setParent(m_grid);
    grid->setSpacing(14);
    grid->setContentsMargins(0, 8, 0, 14);

    QSqlQuery q("SELECT name, make, model, year, length_ft, vessel_type,"
                " closing_balance, image, active FROM boats"
                " ORDER BY active DESC, name");
    int n = 0, inRed = 0, nDeparted = 0;
    while (q.next()) {
        QStringList sub;
        for (int c : {1, 2}) {
            QString v = q.value(c).toString().trimmed();
            if (!v.isEmpty()) sub << v;
        }
        QString spec = sub.join(" ");
        QStringList dims;
        if (!q.value(4).toString().isEmpty()) dims << q.value(4).toString() + " ft";
        if (!q.value(3).toString().isEmpty()) dims << q.value(3).toString();
        if (!q.value(5).toString().isEmpty()) dims << q.value(5).toString();
        if (!dims.isEmpty()) spec += (spec.isEmpty() ? "" : "\n") + dims.join(" · ");

        double bal = zeroSnap(q.value(6).toDouble());
        auto *card = new BoatCard(q.value(0).toString(),
                                  spec.isEmpty() ? "—" : spec, bal,
                                  q.value(7).toString());
        connect(card, &BoatCard::clicked, this, &FleetPage::boatSelected);
        card->setParent(m_grid);
        if (q.value(8).toInt()) {
            m_active << card;
            ++n;
            if (bal < 0) ++inRed;
        } else {
            card->setProperty("departed", true);
            m_departed << card;
            ++nDeparted;
        }
    }
    relayout();
    m_header->setText(QString(
        "<span style='font-size:20px; font-weight:600; color:#14354f'>The Fleet</span>"
        "&nbsp;&nbsp;<span style='color:#6b7a87'>%1 boats in fleet · %2 in the red"
        " · %3 left the fleet</span>")
        .arg(n).arg(inRed).arg(nDeparted));
}

// ---------------- Overview ----------------

OverviewPage::OverviewPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 14, 18, 10);
    m_summary = new QLabel;
    m_summary->setTextFormat(Qt::RichText);
    lay->addWidget(m_summary);
    m_table = makeTable({"Boat", "First month", "Last month",
                         "Closing balance", "Last-month expenses",
                         "Labour $ (all time)", "Months"});
    m_table->setSortingEnabled(true);
    lay->addWidget(m_table);
    auto *hint = new QLabel("Double-click a boat to open its page. Red = owner owes money.");
    hint->setStyleSheet("color: gray;");
    lay->addWidget(hint);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        emit boatSelected(m_table->item(row, 0)->text());
    });
}

void OverviewPage::reload() {
    QSqlQuery q("SELECT b.name, b.first_month, b.last_month, b.closing_balance,"
                " (SELECT exp_debits - exp_credits FROM statements s"
                "   WHERE s.boat_id=b.id AND s.month=b.last_month),"
                " (SELECT COALESCE(SUM(ta_usd+sm_usd+as_usd),0) FROM statements s"
                "   WHERE s.boat_id=b.id),"
                " (SELECT COUNT(*) FROM statements s WHERE s.boat_id=b.id),"
                " b.active"
                " FROM boats b ORDER BY b.active DESC, b.name");
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    int n = 0, inRed = 0, nDeparted = 0;
    double totalBal = 0, totalLabour = 0;
    while (q.next()) {
        bool active = q.value(7).toInt() != 0;
        int r = m_table->rowCount();
        m_table->insertRow(r);
        auto *nameIt = new QTableWidgetItem(
            active ? q.value(0).toString() : q.value(0).toString() + "  (left fleet)");
        if (!active) {
            nameIt->setForeground(QBrush(QColor("#9aa7b2")));
            QFont f = nameIt->font(); f.setItalic(true); nameIt->setFont(f);
        }
        m_table->setItem(r, 0, nameIt);
        m_table->setItem(r, 1, new QTableWidgetItem(q.value(1).toString()));
        m_table->setItem(r, 2, new QTableWidgetItem(q.value(2).toString()));
        double bal = zeroSnap(q.value(3).toDouble());
        setMoneyItem(m_table, r, 3, bal);
        setAmountItem(m_table, r, 4, q.value(4).toDouble());
        setAmountItem(m_table, r, 5, q.value(5).toDouble());
        auto *mi = new QTableWidgetItem;
        mi->setData(Qt::DisplayRole, q.value(6).toInt());
        m_table->setItem(r, 6, mi);
        if (bal < 0) {
            ++inRed;
            m_table->item(r, 3)->setBackground(QColor(200, 60, 60, 26));
        }
        if (active) ++n; else ++nDeparted;
        totalBal += bal;
        totalLabour += q.value(5).toDouble();
    }
    m_table->setSortingEnabled(true);
    m_table->resizeColumnsToContents();
    QSqlQuery m("SELECT value FROM meta WHERE key='ingested_at'");
    QString when = m.next() ? m.value(0).toString().replace('T', ' ') : "?";
    m_summary->setText(QString(
        "<h2 style='margin:4px'>Fleet overview</h2>"
        "<b>%1</b> boats in fleet (+%2 departed, kept for history) &nbsp;|&nbsp;"
        " combined balance <b>%3</b> &nbsp;|&nbsp; <b>%4</b> owing (in the red)"
        " &nbsp;|&nbsp; labour billed all-time <b>%5</b><br>"
        "<span style='color:gray'>Data imported %6 — read-only from the Excel books.</span>")
        .arg(n).arg(nDeparted).arg(money(totalBal)).arg(inRed)
        .arg(money(totalLabour)).arg(when));
}

// ---------------- Boat page ----------------

BoatPage::BoatPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 14, 18, 10);
    auto *top = new QHBoxLayout;
    top->setSpacing(14);
    m_photo = new QLabel;
    top->addWidget(m_photo);
    auto *head = new QVBoxLayout;
    head->setSpacing(2);
    m_title = new QLabel;
    m_title->setTextFormat(Qt::RichText);
    head->addWidget(m_title);
    m_specs = new QLabel;
    m_specs->setObjectName("cardSub");
    m_specs->setWordWrap(true);
    head->addWidget(m_specs);
    head->addStretch();
    top->addLayout(head, 1);
    m_manualBtn = new QPushButton("Open ops manual (PDF)");
    connect(m_manualBtn, &QPushButton::clicked, this, [this] {
        if (!m_manualPdf.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_manualPdf));
    });
    top->addWidget(m_manualBtn, 0, Qt::AlignTop);
    lay->addLayout(top);

    m_chart = new ChartWidget;
    lay->addWidget(m_chart);

    auto *fl = new QHBoxLayout;
    fl->addWidget(new QLabel("Month:"));
    m_monthBox = new QComboBox;
    connect(m_monthBox, &QComboBox::currentIndexChanged, this, [this] { fillExpenses(); });
    fl->addWidget(m_monthBox);
    m_labour = new QLabel;
    fl->addWidget(m_labour, 1);
    lay->addLayout(fl);

    m_expenses = makeTable({"Month", "Description", "Credit", "Debit",
                            "Turnaround $", "Mechanic $", "Aesthetic $"});
    lay->addWidget(m_expenses, 3);

    m_incomeLabel = new QLabel("<b>Income</b>");
    lay->addWidget(m_incomeLabel);
    m_income = makeTable({"Month", "Description", "Amount", "Charter details"});
    m_income->setMaximumHeight(140);
    lay->addWidget(m_income, 1);

    m_appLabel = new QLabel("<b>App entries</b> (stored in this app only — not in the Excel books)");
    lay->addWidget(m_appLabel);
    m_appEntries = makeTable({"Date", "Description", "Credit", "Debit",
                              "Technician", "Hours", "Labour $"});
    m_appEntries->setMaximumHeight(140);
    lay->addWidget(m_appEntries, 1);
}

void BoatPage::setBoat(const QString &name) {
    m_boat = name;
    reload();
}

void BoatPage::reload() {
    if (m_boat.isEmpty()) return;
    QSqlQuery q;
    q.prepare("SELECT id, manual_pdf, make, model, year, length_ft, cabins,"
              " heads, berths, vessel_type, closing_balance, image, active"
              " FROM boats WHERE name=?");
    q.addBindValue(m_boat);
    if (!q.exec() || !q.next()) return;
    m_boatId = q.value(0).toInt();
    m_manualPdf = q.value(1).toString();
    applyPhoto(m_photo, q.value(11).toString(), QSize(190, 112));
    m_manualBtn->setEnabled(!m_manualPdf.isEmpty() && QFile::exists(m_manualPdf));
    if (!m_manualBtn->isEnabled()) m_manualBtn->setText("No ops manual found");
    else m_manualBtn->setText("Open ops manual (PDF)");

    double bal = zeroSnap(q.value(10).toDouble());
    QString leftTag = q.value(12).toInt() ? ""
        : " &nbsp;<span style='font-size:13px; color:#9aa7b2'>· left the fleet</span>";
    m_title->setText(QString("<h2 style='margin:2px'>%1 &nbsp;"
                             "<span style='color:%2'>%3</span>%4</h2>")
                         .arg(m_boat.toHtmlEscaped(),
                              bal < 0 ? "#c83c3c" : "#3c8c50", money(bal), leftTag));
    QStringList sp;
    auto add = [&](const QString &label, int col) {
        QString v = q.value(col).toString().trimmed();
        if (!v.isEmpty() && v != "0") sp << label.arg(v);
    };
    add("%1", 2); add("%1", 3); add("built %1", 4); add("%1 ft", 5);
    add("%1 cabins", 6); add("%1 heads", 7); add("%1 berths", 8); add("%1", 9);
    m_specs->setText(sp.isEmpty() ? "No spec data matched for this boat."
                                  : sp.join("  ·  "));

    QSqlQuery s;
    s.prepare("SELECT month, closing, ta_usd+sm_usd+as_usd FROM statements"
              " WHERE boat_id=? ORDER BY month");
    s.addBindValue(m_boatId);
    s.exec();
    QVector<QString> labels;
    QVector<double> vals;
    double labourTotal = 0;
    QStringList months;
    while (s.next()) {
        QString m = s.value(0).toString();
        months << m;
        labels << QDate::fromString(m + "-01", "yyyy-MM-dd").toString("MMM yy");
        vals << s.value(1).toDouble();
        labourTotal += s.value(2).toDouble();
    }
    m_chart->setSeries(labels, vals, "Balance owed at month end (negative = owner owes)");
    m_labour->setText(QString("Labour billed all-time: <b>%1</b>").arg(money(labourTotal)));

    m_monthBox->blockSignals(true);
    m_monthBox->clear();
    m_monthBox->addItem("All months");
    for (auto it = months.rbegin(); it != months.rend(); ++it) m_monthBox->addItem(*it);
    m_monthBox->blockSignals(false);
    fillExpenses();

    QSqlQuery inc;
    inc.prepare("SELECT month, description, amount, details FROM income_lines"
                " WHERE boat_id=? ORDER BY month DESC");
    inc.addBindValue(m_boatId);
    inc.exec();
    m_income->setRowCount(0);
    while (inc.next()) {
        int r = m_income->rowCount();
        m_income->insertRow(r);
        m_income->setItem(r, 0, new QTableWidgetItem(inc.value(0).toString()));
        m_income->setItem(r, 1, new QTableWidgetItem(inc.value(1).toString()));
        setAmountItem(m_income, r, 2, inc.value(2).toDouble());
        m_income->setItem(r, 3, new QTableWidgetItem(inc.value(3).toString()));
    }
    m_income->resizeColumnsToContents();
    bool hasInc = m_income->rowCount() > 0;
    m_income->setVisible(hasInc);
    m_incomeLabel->setVisible(hasInc);

    QSqlQuery me;
    me.prepare("SELECT entry_date, description, credit, debit, tech_type,"
               " labour_hours, labour_usd FROM manual_entries WHERE boat_name=?"
               " ORDER BY entry_date DESC");
    me.addBindValue(m_boat);
    me.exec();
    m_appEntries->setRowCount(0);
    while (me.next()) {
        int r = m_appEntries->rowCount();
        m_appEntries->insertRow(r);
        m_appEntries->setItem(r, 0, new QTableWidgetItem(me.value(0).toString()));
        m_appEntries->setItem(r, 1, new QTableWidgetItem(me.value(1).toString()));
        setAmountItem(m_appEntries, r, 2, me.value(2).toDouble());
        setAmountItem(m_appEntries, r, 3, me.value(3).toDouble());
        m_appEntries->setItem(r, 4, new QTableWidgetItem(me.value(4).toString()));
        setHoursItem(m_appEntries, r, 5, me.value(5).toDouble());
        setAmountItem(m_appEntries, r, 6, me.value(6).toDouble());
    }
    bool hasApp = m_appEntries->rowCount() > 0;
    m_appEntries->setVisible(hasApp);
    m_appLabel->setVisible(hasApp);
}

void BoatPage::fillExpenses() {
    if (m_boatId < 0) return;
    QString month = m_monthBox->currentIndex() <= 0 ? QString()
                                                    : m_monthBox->currentText();
    QSqlQuery e;
    if (month.isEmpty()) {
        e.prepare("SELECT month, description, credit, debit, ta_usd, sm_usd, as_usd"
                  " FROM expense_lines WHERE boat_id=? ORDER BY month DESC, id");
        e.addBindValue(m_boatId);
    } else {
        e.prepare("SELECT month, description, credit, debit, ta_usd, sm_usd, as_usd"
                  " FROM expense_lines WHERE boat_id=? AND month=? ORDER BY id");
        e.addBindValue(m_boatId);
        e.addBindValue(month);
    }
    e.exec();
    m_expenses->setRowCount(0);
    while (e.next()) {
        int r = m_expenses->rowCount();
        m_expenses->insertRow(r);
        m_expenses->setItem(r, 0, new QTableWidgetItem(e.value(0).toString()));
        m_expenses->setItem(r, 1, new QTableWidgetItem(e.value(1).toString()));
        for (int c = 2; c <= 6; ++c)
            setAmountItem(m_expenses, r, c, e.value(c).toDouble());
    }
    m_expenses->resizeColumnsToContents();
    m_expenses->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
}

// ---------------- Maintenance ----------------

MaintenancePage::MaintenancePage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 14, 18, 10);
    m_summary = new QLabel("<h2 style='margin:4px'>Maintenance &amp; labour</h2>");
    m_summary->setTextFormat(Qt::RichText);
    lay->addWidget(m_summary);
    m_table = makeTable({"Boat", "Turnaround hrs", "Turnaround $",
                         "Mechanic hrs", "Mechanic $",
                         "Aesthetic hrs", "Aesthetic $",
                         "Total labour $", "Total debits $"});
    m_table->setSortingEnabled(true);
    lay->addWidget(m_table);
    auto *hint = new QLabel("All-time totals from the boat books."
                            " Highlight = top 5 heaviest labour spend."
                            " Double-click a boat to open its page.");
    hint->setStyleSheet("color: gray;");
    lay->addWidget(hint);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        emit boatSelected(m_table->item(row, 0)->text());
    });
}

void MaintenancePage::reload() {
    QSqlQuery q("SELECT b.name,"
                " SUM(e.ta_h + e.ta_m/60.0), SUM(e.ta_usd),"
                " SUM(e.sm_h + e.sm_m/60.0), SUM(e.sm_usd),"
                " SUM(e.as_h + e.as_m/60.0), SUM(e.as_usd),"
                " SUM(e.ta_usd + e.sm_usd + e.as_usd), SUM(e.debit)"
                " FROM boats b LEFT JOIN expense_lines e ON e.boat_id=b.id"
                " GROUP BY b.id ORDER BY 8 DESC");
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    QVector<double> labourTotals;
    while (q.next()) {
        int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, 0, new QTableWidgetItem(q.value(0).toString()));
        for (int c = 1; c <= 8; ++c) {
            double v = q.value(c).toDouble();
            if (c == 1 || c == 3 || c == 5)
                setHoursItem(m_table, r, c, v);
            else
                setAmountItem(m_table, r, c, v);
        }
        labourTotals << q.value(7).toDouble();
    }
    for (int r = 0; r < qMin(5, m_table->rowCount()); ++r)
        if (labourTotals[r] > 0)
            for (int c = 0; c < m_table->columnCount(); ++c)
                m_table->item(r, c)->setBackground(QColor(220, 160, 40, 40));
    m_table->setSortingEnabled(true);
    m_table->resizeColumnsToContents();
}

// ---------------- Entry ----------------

EntryPage::EntryPage(QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(18, 14, 18, 10);
    auto *head = new QLabel("<h2 style='margin:4px'>Add entry</h2>"
                            "Entries are saved in this app's own database."
                            " The Excel boat books are never modified.");
    head->setTextFormat(Qt::RichText);
    lay->addWidget(head);

    auto *formHost = new QWidget;
    formHost->setMaximumWidth(560);
    auto *form = new QFormLayout(formHost);
    form->setContentsMargins(0, 4, 0, 0);
    m_boat = new QComboBox;
    form->addRow("Boat:", m_boat);
    m_date = new QDateEdit(QDate::currentDate());
    m_date->setCalendarPopup(true);
    form->addRow("Date:", m_date);
    m_desc = new QLineEdit;
    form->addRow("Description:", m_desc);
    m_credit = new QDoubleSpinBox; m_credit->setRange(0, 1e7); m_credit->setPrefix("$");
    form->addRow("Credit (money in):", m_credit);
    m_debit = new QDoubleSpinBox; m_debit->setRange(0, 1e7); m_debit->setPrefix("$");
    form->addRow("Debit (charge to owner):", m_debit);
    m_tech = new QComboBox;
    m_tech->addItems({"None", "Turnaround technician", "Senior mechanic",
                      "Aesthetic specialist"});
    form->addRow("Labour by:", m_tech);
    m_hours = new QDoubleSpinBox; m_hours->setRange(0, 500); m_hours->setDecimals(2);
    form->addRow("Labour hours:", m_hours);
    lay->addWidget(formHost);

    auto *btns = new QHBoxLayout;
    auto *saveBtn = new QPushButton("Save entry");
    connect(saveBtn, &QPushButton::clicked, this, [this] { save(); });
    btns->addWidget(saveBtn);
    auto *delBtn = new QPushButton("Delete selected entry");
    connect(delBtn, &QPushButton::clicked, this, [this] { removeSelected(); });
    btns->addWidget(delBtn);
    btns->addStretch();
    lay->addLayout(btns);

    m_table = makeTable({"ID", "Boat", "Date", "Description", "Credit", "Debit",
                         "Technician", "Hours", "Labour $"});
    m_table->hideColumn(0);
    lay->addWidget(m_table);
}

void EntryPage::reload() {
    QString keep = m_boat->currentText();
    m_boat->clear();
    QSqlQuery b("SELECT name FROM boats ORDER BY name");
    while (b.next()) m_boat->addItem(b.value(0).toString());
    if (!keep.isEmpty()) m_boat->setCurrentText(keep);

    QSqlQuery q("SELECT id, boat_name, entry_date, description, credit, debit,"
                " tech_type, labour_hours, labour_usd FROM manual_entries"
                " ORDER BY entry_date DESC, id DESC");
    m_table->setRowCount(0);
    while (q.next()) {
        int r = m_table->rowCount();
        m_table->insertRow(r);
        m_table->setItem(r, 0, new QTableWidgetItem(q.value(0).toString()));
        m_table->setItem(r, 1, new QTableWidgetItem(q.value(1).toString()));
        m_table->setItem(r, 2, new QTableWidgetItem(q.value(2).toString()));
        m_table->setItem(r, 3, new QTableWidgetItem(q.value(3).toString()));
        setAmountItem(m_table, r, 4, q.value(4).toDouble());
        setAmountItem(m_table, r, 5, q.value(5).toDouble());
        m_table->setItem(r, 6, new QTableWidgetItem(q.value(6).toString()));
        setHoursItem(m_table, r, 7, q.value(7).toDouble());
        setAmountItem(m_table, r, 8, q.value(8).toDouble());
    }
    m_table->resizeColumnsToContents();
}

void EntryPage::save() {
    if (m_desc->text().trimmed().isEmpty() && m_credit->value() == 0
        && m_debit->value() == 0 && m_hours->value() == 0) {
        QMessageBox::information(this, "Nothing to save",
                                 "Fill in a description or an amount first.");
        return;
    }
    // Labour $ from the standard rates in the latest statements.
    double rate = 0;
    QString tech = m_tech->currentText();
    if (tech != "None") {
        QString col = tech.startsWith("Turnaround") ? "rate_ta"
                    : tech.startsWith("Senior") ? "rate_sm" : "rate_as";
        QSqlQuery r(QString("SELECT %1 FROM statements WHERE %1 IS NOT NULL"
                            " ORDER BY month DESC LIMIT 1").arg(col));
        if (r.next()) rate = r.value(0).toDouble();
    }
    QSqlQuery q;
    q.prepare("INSERT INTO manual_entries(boat_name, entry_date, description,"
              " credit, debit, tech_type, labour_hours, labour_usd, created_at)"
              " VALUES(?,?,?,?,?,?,?,?,datetime('now','localtime'))");
    q.addBindValue(m_boat->currentText());
    q.addBindValue(m_date->date().toString("yyyy-MM-dd"));
    q.addBindValue(m_desc->text().trimmed());
    q.addBindValue(m_credit->value());
    q.addBindValue(m_debit->value());
    q.addBindValue(tech == "None" ? QString() : tech);
    q.addBindValue(m_hours->value());
    q.addBindValue(m_hours->value() * rate);
    if (!q.exec()) {
        QMessageBox::warning(this, "Save failed", q.lastError().text());
        return;
    }
    m_desc->clear();
    m_credit->setValue(0);
    m_debit->setValue(0);
    m_hours->setValue(0);
    reload();
    emit entriesChanged();
}

void EntryPage::removeSelected() {
    int row = m_table->currentRow();
    if (row < 0) return;
    QString desc = m_table->item(row, 3)->text();
    if (QMessageBox::question(this, "Delete entry",
            QString("Delete the entry \"%1\"?").arg(desc))
        != QMessageBox::Yes)
        return;
    QSqlQuery q;
    q.prepare("DELETE FROM manual_entries WHERE id=?");
    q.addBindValue(m_table->item(row, 0)->text().toInt());
    q.exec();
    reload();
    emit entriesChanged();
}
