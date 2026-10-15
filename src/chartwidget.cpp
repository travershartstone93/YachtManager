#include "chartwidget.h"
#include <QPainter>
#include <QPainterPath>
#include <cmath>

ChartWidget::ChartWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(220);
}

void ChartWidget::setSeries(const QVector<QString> &labels,
                            const QVector<double> &values, const QString &title) {
    m_labels = labels;
    m_values = values;
    m_title = title;
    update();
}

static QString moneyShort(double v) {
    if (std::fabs(v) >= 1000)
        return QString("%1k").arg(v / 1000.0, 0, 'f', 1);
    return QString::number(v, 'f', 0);
}

void ChartWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor text("#51606d"), gridc("#dde3ea"), titlec("#14354f");
    // white rounded card background to match the theme
    p.setPen(QPen(gridc, 1));
    p.setBrush(QColor("#ffffff"));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    p.setBrush(Qt::NoBrush);

    const int left = 64, right = 16, top = 32, bottom = 42;
    QRectF area(left, top, width() - left - right, height() - top - bottom);

    p.setPen(titlec);
    QFont tf = font(); tf.setBold(true);
    p.setFont(tf);
    p.drawText(QRectF(0, 8, width(), 20), Qt::AlignHCenter, m_title);
    p.setFont(font());

    if (m_values.isEmpty()) {
        p.setPen(text);
        p.drawText(area, Qt::AlignCenter, "No data");
        return;
    }

    double lo = m_values.first(), hi = m_values.first();
    for (double v : m_values) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (lo > 0) lo = 0;
    if (hi < 0) hi = 0;
    if (hi - lo < 1) hi = lo + 1;
    const double pad = (hi - lo) * 0.08;
    lo -= pad; hi += pad;

    auto X = [&](int i) {
        if (m_values.size() == 1) return area.center().x();
        return area.left() + area.width() * i / double(m_values.size() - 1);
    };
    auto Y = [&](double v) {
        return area.bottom() - area.height() * (v - lo) / (hi - lo);
    };

    // horizontal gridlines + labels
    QPen grid(gridc); grid.setStyle(Qt::DotLine);
    for (int g = 0; g <= 4; ++g) {
        double v = lo + (hi - lo) * g / 4.0;
        double y = Y(v);
        p.setPen(grid);
        p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
        p.setPen(text);
        p.drawText(QRectF(0, y - 8, left - 6, 16), Qt::AlignRight | Qt::AlignVCenter,
                   moneyShort(v));
    }
    // zero line
    if (lo < 0 && hi > 0) {
        QPen zp(QColor("#8fa1b0")); zp.setWidthF(1.2);
        p.setPen(zp);
        p.drawLine(QPointF(area.left(), Y(0)), QPointF(area.right(), Y(0)));
    }

    // area fill under the line
    QPainterPath path;
    path.moveTo(X(0), Y(m_values[0]));
    for (int i = 1; i < m_values.size(); ++i) path.lineTo(X(i), Y(m_values[i]));
    QPainterPath fill = path;
    fill.lineTo(X(m_values.size() - 1), Y(0));
    fill.lineTo(X(0), Y(0));
    QColor fc = m_values.last() < 0 ? QColor("#c0392b") : QColor("#2a7f9e");
    QColor fcA = fc; fcA.setAlpha(45);
    p.fillPath(fill, fcA);
    QPen lp(fc); lp.setWidthF(2.0);
    p.setPen(lp);
    p.drawPath(path);
    p.setBrush(fc);
    for (int i = 0; i < m_values.size(); ++i)
        p.drawEllipse(QPointF(X(i), Y(m_values[i])), 3, 3);

    // x labels, thinned so they never overlap
    p.setPen(text);
    int step = std::max(1, int(std::ceil(m_values.size() * 52.0 / area.width())));
    for (int i = 0; i < m_labels.size(); i += step) {
        p.drawText(QRectF(X(i) - 30, area.bottom() + 6, 60, 16),
                   Qt::AlignHCenter, m_labels[i]);
    }
}
