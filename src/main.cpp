#include "mainwindow.h"
#include "aipanel.h"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QTimer>
#include <QtSql/QSqlDatabase>

static const char *kTheme = R"(
QMainWindow { background: #eef1f5; }
QToolBar#headerBar { background: #14354f; border: none; padding: 5px 10px; spacing: 2px; }
#appTitle { color: #ffffff; font-size: 15px; font-weight: 700; letter-spacing: 2px;
            padding-right: 20px; }
QToolBar#headerBar QToolButton { color: #cfe0ee; font-size: 13px; font-weight: 600;
            padding: 8px 14px; border-radius: 6px; background: transparent; }
QToolBar#headerBar QToolButton:hover { background: #24547a; color: #ffffff; }
#boatSidebar { background: #ffffff; border: none; border-right: 1px solid #dbe2e9;
            outline: 0; }
#boatSidebar::item { border-bottom: 1px solid #f0f3f6; }
#boatSidebar::item:selected, #boatSidebar::item:hover { background: #e9f2f9; }
#sideName { color: #22313f; font-size: 12px; background: transparent; }
#sideBalNeg { color: #c0392b; font-size: 11px; font-weight: 600; background: transparent; }
#sideBalPos { color: #2e7d32; font-size: 11px; font-weight: 600; background: transparent; }
QScrollArea { background: transparent; border: none; }
#fleetGrid { background: transparent; }
QFrame#boatCard { background: #ffffff; border: 1px solid #dde3ea; border-radius: 10px; }
QFrame#boatCard:hover { border: 1px solid #2a7f9e; }
QFrame#boatCard[departed="true"] { background: #f1f4f6; border: 1px dashed #c9d2da; }
#sideNameGone { color: #9aa7b2; font-size: 12px; font-style: italic;
            background: transparent; }
#cardName { font-size: 14px; font-weight: 700; color: #14354f; background: transparent; }
#cardSub { color: #6b7a87; font-size: 11px; background: transparent; }
#badgeNeg { background: #fdecea; color: #c0392b; border-radius: 6px;
            font-weight: 700; font-size: 12px; }
#badgePos { background: #e8f5e9; color: #2e7d32; border-radius: 6px;
            font-weight: 700; font-size: 12px; }
QTableWidget { background: #ffffff; alternate-background-color: #f7fafc;
            color: #22313f; border: 1px solid #dde3ea; border-radius: 8px;
            font-size: 12px;
            selection-background-color: #dcebf7; selection-color: #22313f; }
QTableWidget::item:selected { background: #dcebf7; }
QHeaderView::section { background: #f0f4f8; color: #51606d; font-weight: 600;
            border: none; border-bottom: 1px solid #dde3ea; padding: 6px 8px; }
QTableCornerButton::section { background: #f0f4f8; border: none; }
QPushButton { background: #14354f; color: #ffffff; border: none; border-radius: 6px;
            padding: 8px 14px; font-weight: 600; }
QPushButton:hover { background: #24547a; }
QPushButton:disabled { background: #b9c4cd; color: #f2f5f7; }
QComboBox, QLineEdit, QDoubleSpinBox, QDateEdit {
            background: #ffffff; border: 1px solid #ccd6de; border-radius: 6px;
            padding: 5px 8px; color: #22313f; }
QComboBox:focus, QLineEdit:focus, QDoubleSpinBox:focus, QDateEdit:focus {
            border: 1px solid #2a7f9e; }
QComboBox QAbstractItemView { background: #ffffff; color: #22313f;
            selection-background-color: #dcebf7; selection-color: #22313f; }
QLabel { color: #22313f; }
QSplitter::handle { background: #dbe2e9; }
QDockWidget#aiPanel { background: #ffffff; color: #22313f; }
QDockWidget#aiPanel::title { background: #f0f4f8; padding: 8px 12px;
            font-weight: 600; }
QTextBrowser { background: #ffffff; color: #22313f; border: 1px solid #dde3ea;
            border-radius: 8px; padding: 8px; font-size: 13px; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #c3cdd6; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #a8b5c0; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal { background: #c3cdd6; border-radius: 5px; min-width: 30px; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
)";

int main(int argc, char **argv) {
    QApplication::setStyle("Fusion");
    QApplication app(argc, argv);
    app.setApplicationName("Yacht Manager");

    // The theme is light-only; never inherit the desktop's (possibly dark)
    // color scheme or text comes out white-on-white.
    QPalette pal;
    pal.setColor(QPalette::Window, QColor("#eef1f5"));
    pal.setColor(QPalette::WindowText, QColor("#22313f"));
    pal.setColor(QPalette::Base, QColor("#ffffff"));
    pal.setColor(QPalette::AlternateBase, QColor("#f7fafc"));
    pal.setColor(QPalette::Text, QColor("#22313f"));
    pal.setColor(QPalette::Button, QColor("#e4e9ee"));
    pal.setColor(QPalette::ButtonText, QColor("#22313f"));
    pal.setColor(QPalette::Highlight, QColor("#dcebf7"));
    pal.setColor(QPalette::HighlightedText, QColor("#22313f"));
    pal.setColor(QPalette::ToolTipBase, QColor("#ffffff"));
    pal.setColor(QPalette::ToolTipText, QColor("#22313f"));
    pal.setColor(QPalette::PlaceholderText, QColor("#8fa1b0"));
    pal.setColor(QPalette::Disabled, QPalette::Text, QColor("#9aa7b2"));
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#9aa7b2"));
    app.setPalette(pal);

    app.setStyleSheet(kTheme);

    // Database lives next to the project, not inside build/.
    QString appDir = QCoreApplication::applicationDirPath();
    QString dbPath = QFileInfo(appDir + "/../fleet.db").absoluteFilePath();
    if (!QFileInfo::exists(dbPath))
        dbPath = appDir + "/fleet.db";

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbPath);
    if (!db.open()) {
        QMessageBox::critical(nullptr, "Yacht Manager",
                              "Could not open the fleet database at\n" + dbPath);
        return 1;
    }

    MainWindow w;
    if (w.sourceChanged())
        w.runIngest();
    w.show();

    QStringList args = app.arguments();

    // Developer check: --ai-test runs the AI summary headlessly and exits.
    if (args.contains("--ai-test")) {
        QObject::connect(w.aiPanel(), &AiPanel::done, &w,
                         [](bool ok, const QString &text) {
                             QTextStream(stdout) << (ok ? "OK\n" : "FAIL\n")
                                                 << text << "\n";
                             QCoreApplication::exit(ok ? 0 : 1);
                         });
        w.aiPanel()->summarize();
        return app.exec();
    }

    // Developer check: --screenshot <dir> renders each page to PNG and exits.
    int si = args.indexOf("--screenshot");
    if (si >= 0 && si + 1 < args.size()) {
        QString dir = args[si + 1];
        QTimer::singleShot(400, &w, [&w, dir] {
            w.saveScreenshots(dir);
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
