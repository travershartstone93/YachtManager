#pragma once
#include <QWidget>
#include <QFrame>
#include <cmath>

inline double zeroSnap(double v) { return std::abs(v) < 0.005 ? 0 : v; }

class QTableWidget;
class QLabel;
class QComboBox;
class QLineEdit;
class QDoubleSpinBox;
class QDateEdit;
class QPushButton;
class ChartWidget;

QString money(double v);
void setMoneyItem(QTableWidget *t, int row, int col, double v, bool colorNeg = true);
// Like setMoneyItem, but zero renders as an empty cell: in the line tables most
// columns are structurally zero and a wall of $0.00 reads like broken data.
void setAmountItem(QTableWidget *t, int row, int col, double v);
void setHoursItem(QTableWidget *t, int row, int col, double v);

// One boat card on the fleet page: photo, name, specs line, balance badge.
class BoatCard : public QFrame {
    Q_OBJECT
public:
    BoatCard(const QString &name, const QString &subtitle, double balance,
             const QString &imagePath, QWidget *parent = nullptr);
signals:
    void clicked(const QString &name);
protected:
    void mouseReleaseEvent(QMouseEvent *e) override;
private:
    QString m_name;
};

class FleetPage : public QWidget {
    Q_OBJECT
public:
    explicit FleetPage(QWidget *parent = nullptr);
    void reload();
signals:
    void boatSelected(const QString &name);
protected:
    void resizeEvent(QResizeEvent *e) override;
private:
    void relayout();
    QWidget *m_grid;
    QLabel *m_header;
    QLabel *m_leftHeader = nullptr;
    QList<BoatCard *> m_active, m_departed;
};

class OverviewPage : public QWidget {
    Q_OBJECT
public:
    explicit OverviewPage(QWidget *parent = nullptr);
    void reload();
signals:
    void boatSelected(const QString &name);
private:
    QLabel *m_summary;
    QTableWidget *m_table;
};

class BoatPage : public QWidget {
    Q_OBJECT
public:
    explicit BoatPage(QWidget *parent = nullptr);
    void setBoat(const QString &name);
    void reload();
private:
    void fillExpenses();
    QString m_boat;
    int m_boatId = -1;
    QString m_manualPdf;
    QLabel *m_title, *m_specs, *m_labour, *m_photo;
    ChartWidget *m_chart;
    QComboBox *m_monthBox;
    QTableWidget *m_expenses, *m_income, *m_appEntries;
    QLabel *m_incomeLabel, *m_appLabel;
    QPushButton *m_manualBtn;
};

class MaintenancePage : public QWidget {
    Q_OBJECT
public:
    explicit MaintenancePage(QWidget *parent = nullptr);
    void reload();
signals:
    void boatSelected(const QString &name);
private:
    QTableWidget *m_table;
    QLabel *m_summary;
};

class EntryPage : public QWidget {
    Q_OBJECT
public:
    explicit EntryPage(QWidget *parent = nullptr);
    void reload();
signals:
    void entriesChanged();
private:
    void save();
    void removeSelected();
    QComboBox *m_boat, *m_tech;
    QDateEdit *m_date;
    QLineEdit *m_desc;
    QDoubleSpinBox *m_credit, *m_debit, *m_hours;
    QTableWidget *m_table;
};
