#pragma once
#include <QMainWindow>

class QListWidget;
class QStackedWidget;
class FleetPage;
class AiPanel;
class OverviewPage;
class BoatPage;
class MaintenancePage;
class EntryPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    // Compares the Excel folder against what the database last saw.
    bool sourceChanged() const;
    void runIngest();
    void saveScreenshots(const QString &dir);
    AiPanel *aiPanel() const { return m_ai; }
private:
    void reloadAll();
    void openBoat(const QString &name);
    QListWidget *m_boatList;
    QStackedWidget *m_stack;
    FleetPage *m_fleet;
    OverviewPage *m_overview;
    BoatPage *m_boatPage;
    MaintenancePage *m_maint;
    EntryPage *m_entry;
    AiPanel *m_ai;
};
