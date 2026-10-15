#pragma once
#include <QWidget>
#include <QVector>

// Simple painted line chart: months on X, dollars on Y.
class ChartWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChartWidget(QWidget *parent = nullptr);
    void setSeries(const QVector<QString> &labels, const QVector<double> &values,
                   const QString &title);
protected:
    void paintEvent(QPaintEvent *) override;
private:
    QVector<QString> m_labels;
    QVector<double> m_values;
    QString m_title;
};
