#include "mainwindow.h"
#include "pages.h"
#include "aipanel.h"
#include <QtWidgets>
#include <QtSql>

static QString sheetsDir() {
    return QDir::homePath() + "/Downloads/Boat Sheets most recent";
}

// Must mirror ingest.py: count + newest mtime of the real boat books.
static QString sourceSignature() {
    QDir d(sheetsDir());
    qint64 maxM = 0;
    int n = 0;
    for (const QFileInfo &fi : d.entryInfoList({"*.xlsm"}, QDir::Files)) {
        if (fi.baseName().toLower().contains("conch charters turnaround sheet"))
            continue;
        ++n;
        maxM = qMax(maxM, fi.lastModified().toSecsSinceEpoch());
    }
    return QString("%1:%2").arg(n).arg(maxM);
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("Yacht Manager");
    resize(1380, 860);

    auto *tb = addToolBar("Main");
    tb->setMovable(false);
    tb->setObjectName("headerBar");
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    auto *title = new QLabel("  ⚓ YACHT MANAGER");
    title->setObjectName("appTitle");
    tb->addWidget(title);

    auto *split = new QSplitter;
    m_boatList = new QListWidget;
    m_boatList->setObjectName("boatSidebar");
    m_boatList->setFixedWidth(250);
    split->addWidget(m_boatList);

    m_stack = new QStackedWidget;
    m_fleet = new FleetPage;
    m_overview = new OverviewPage;
    m_boatPage = new BoatPage;
    m_maint = new MaintenancePage;
    m_entry = new EntryPage;
    m_stack->addWidget(m_fleet);
    m_stack->addWidget(m_overview);
    m_stack->addWidget(m_boatPage);
    m_stack->addWidget(m_maint);
    m_stack->addWidget(m_entry);
    split->addWidget(m_stack);
    split->setStretchFactor(1, 1);
    split->setHandleWidth(1);
    setCentralWidget(split);

    tb->addAction("Fleet", this, [this] { m_stack->setCurrentWidget(m_fleet); });
    tb->addAction("Finance", this, [this] { m_stack->setCurrentWidget(m_overview); });
    tb->addAction("Maintenance", this, [this] { m_stack->setCurrentWidget(m_maint); });
    tb->addAction("Add entry", this, [this] { m_stack->setCurrentWidget(m_entry); });

    m_ai = new AiPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_ai);
    m_ai->hide();
    tb->addAction("✦ AI", this, [this] { m_ai->setVisible(!m_ai->isVisible()); });

    auto *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    tb->addWidget(spacer);
    tb->addAction("⟳ Reload data", this, [this] { runIngest(); });

    connect(m_boatList, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        openBoat(it->data(Qt::UserRole).toString());
    });
    connect(m_fleet, &FleetPage::boatSelected, this,
            [this](const QString &n) { openBoat(n); });
    connect(m_overview, &OverviewPage::boatSelected, this,
            [this](const QString &n) { openBoat(n); });
    connect(m_maint, &MaintenancePage::boatSelected, this,
            [this](const QString &n) { openBoat(n); });
    connect(m_entry, &EntryPage::entriesChanged, this, [this] { m_boatPage->reload(); });

    reloadAll();
}

bool MainWindow::sourceChanged() const {
    QSqlQuery q("SELECT value FROM meta WHERE key='source_sig'");
    if (!q.next()) return true;
    return q.value(0).toString() != sourceSignature();
}

void MainWindow::runIngest() {
    QProgressDialog dlg("Importing boat books from Downloads…", QString(), 0, 0, this);
    dlg.setWindowModality(Qt::ApplicationModal);
    dlg.setMinimumDuration(0);
    dlg.show();

    QString script = QCoreApplication::applicationDirPath() + "/../ingest.py";
    if (!QFile::exists(script))
        script = QCoreApplication::applicationDirPath() + "/ingest.py";
    QString python = QFileInfo(script).dir().filePath(".venv/bin/python3");
    if (!QFile::exists(python))
        python = "python3";

    QSqlDatabase::database().close();
    QProcess proc;
    proc.start(python, {script});
    while (!proc.waitForFinished(100))
        QCoreApplication::processEvents();
    QSqlDatabase::database().open();
    dlg.close();

    if (proc.exitCode() != 0) {
        QMessageBox::warning(this, "Import failed",
                             QString::fromUtf8(proc.readAllStandardError()).right(2000));
    }
    reloadAll();
}

void MainWindow::reloadAll() {
    m_boatList->clear();
    QSqlQuery q("SELECT name, closing_balance, active FROM boats"
                " ORDER BY active DESC, name");
    while (q.next()) {
        double bal = zeroSnap(q.value(1).toDouble());
        auto *it = new QListWidgetItem;
        it->setData(Qt::UserRole, q.value(0).toString());
        m_boatList->addItem(it);
        auto *w = new QWidget;
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(10, 5, 10, 5);
        auto *nm = new QLabel(q.value(0).toString());
        nm->setObjectName(q.value(2).toInt() ? "sideName" : "sideNameGone");
        h->addWidget(nm, 1);
        auto *bl = new QLabel(money(bal));
        bl->setObjectName(bal < 0 ? "sideBalNeg" : "sideBalPos");
        h->addWidget(bl);
        it->setSizeHint(w->sizeHint());
        m_boatList->setItemWidget(it, w);
    }
    m_fleet->reload();
    m_overview->reload();
    m_maint->reload();
    m_entry->reload();
    m_boatPage->reload();
}

void MainWindow::saveScreenshots(const QString &dir) {
    QDir().mkpath(dir);
    auto shoot = [&](QWidget *page, const QString &name) {
        m_stack->setCurrentWidget(page);
        QCoreApplication::processEvents();
        grab().save(dir + "/" + name + ".png");
    };
    shoot(m_fleet, "fleet");
    shoot(m_overview, "overview");
    if (m_boatList->count() > 0)
        openBoat(m_boatList->item(0)->data(Qt::UserRole).toString());
    shoot(m_boatPage, "boat");
    shoot(m_maint, "maintenance");
    shoot(m_entry, "entry");
}

void MainWindow::openBoat(const QString &name) {
    m_boatPage->setBoat(name);
    m_stack->setCurrentWidget(m_boatPage);
    for (int i = 0; i < m_boatList->count(); ++i)
        if (m_boatList->item(i)->data(Qt::UserRole).toString() == name)
            m_boatList->setCurrentRow(i);
}
