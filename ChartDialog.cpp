#include "ChartDialog.h"

#include <QBarCategoryAxis>
#include <QBarSeries>
#include <QBarSet>
#include <QChart>
#include <QChartView>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineSeries>
#include <QListWidget>
#include <QPushButton>
#include <QScatterSeries>
#include <QSplineSeries>
#include <QSplitter>
#include <QValueAxis>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace
{
constexpr int kMaxPoints = 200000;
constexpr int kMaxBars   = 500;

enum XMode { XIndex, XNumeric, XDateTime, XCategory };
enum Kind  { KLine, KSpline, KScatter, KBar };
} // namespace

ChartDialog::ChartDialog(const QVector<ColumnInfo> &columns, const QVector<QJsonArray> &rows,
                         const QList<int> &preselectedY, const QString &title, QWidget *parent)
    : QDialog(parent)
    , m_columns(columns)
    , m_rows(rows)
{
    setWindowTitle(tr("График — %1").arg(title));
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowMaximizeButtonHint);

    auto *root = new QHBoxLayout(this);
    auto *split = new QSplitter;
    root->addWidget(split);

    // --- Панель настроек
    auto *side = new QWidget;
    auto *sl = new QVBoxLayout(side);
    auto *form = new QFormLayout;

    m_kind = new QComboBox;
    m_kind->addItems({ tr("Линия"), tr("Сглаженная линия"), tr("Точки"), tr("Столбцы") });
    form->addRow(tr("Тип:"), m_kind);

    m_x = new QComboBox;
    m_x->addItem(tr("№ строки"));
    for (const ColumnInfo &c : std::as_const(m_columns))
        m_x->addItem(QStringLiteral("%1 (%2)").arg(c.name, c.type));
    form->addRow(tr("Ось X:"), m_x);
    sl->addLayout(form);

    sl->addWidget(new QLabel(tr("Ось Y (числовые колонки):")));
    m_y = new QListWidget;
    for (int i = 0; i < m_columns.size(); ++i) {
        const ColumnInfo &c = m_columns.at(i);
        auto *it = new QListWidgetItem(QStringLiteral("%1 (%2)").arg(c.name, c.type), m_y);
        it->setData(Qt::UserRole, i);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        const bool numeric = Cql::parseType(c.type).isNumeric();
        it->setCheckState(preselectedY.contains(i) ? Qt::Checked : Qt::Unchecked);
        if (!numeric) it->setForeground(Qt::gray);
    }
    sl->addWidget(m_y, 1);

    m_sortX = new QCheckBox(tr("Сортировать по X"));
    m_sortX->setChecked(true);
    sl->addWidget(m_sortX);

    auto *buildBtn = new QPushButton(tr("Построить"));
    auto *resetBtn = new QPushButton(tr("Сбросить масштаб"));
    auto *saveBtn  = new QPushButton(tr("Сохранить PNG…"));
    sl->addWidget(buildBtn);
    sl->addWidget(resetBtn);
    sl->addWidget(saveBtn);

    m_info = new QLabel;
    m_info->setWordWrap(true);
    sl->addWidget(m_info);

    split->addWidget(side);

    // --- График
    m_view = new QChartView(new QChart);
    m_view->setRenderHint(QPainter::Antialiasing);
    m_view->setRubberBand(QChartView::RectangleRubberBand);
    split->addWidget(m_view);
    split->setStretchFactor(1, 1);
    split->setSizes({ 260, 800 });

    // Если X не выбран явно, а первая колонка — время, взять её.
    for (int i = 0; i < m_columns.size(); ++i) {
        const QString t = m_columns.at(i).type;
        if (t == QLatin1String("timestamp") || t == QLatin1String("date")) {
            m_x->setCurrentIndex(i + 1);
            break;
        }
    }

    connect(buildBtn, &QPushButton::clicked, this, &ChartDialog::build);
    connect(resetBtn, &QPushButton::clicked, this, [this] { m_view->chart()->zoomReset(); });
    connect(saveBtn, &QPushButton::clicked, this, &ChartDialog::savePng);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &ChartDialog::build);
    connect(m_x, &QComboBox::currentIndexChanged, this, &ChartDialog::build);
    connect(m_y, &QListWidget::itemChanged, this, &ChartDialog::build);
    connect(m_sortX, &QCheckBox::toggled, this, &ChartDialog::build);

    resize(1100, 650);
    build();
}

void ChartDialog::build()
{
    const int kind = m_kind->currentIndex();
    const int xCol = m_x->currentIndex() - 1; // -1 => номер строки

    QList<int> yCols;
    for (int i = 0; i < m_y->count(); ++i)
        if (m_y->item(i)->checkState() == Qt::Checked)
            yCols << m_y->item(i)->data(Qt::UserRole).toInt();

    auto *chart = new QChart;
    chart->setAnimationOptions(QChart::NoAnimation);
    chart->legend()->setAlignment(Qt::AlignBottom);

    auto replaceChart = [this, chart] {
        QChart *old = m_view->chart();
        m_view->setChart(chart);
        delete old;
    };

    if (yCols.isEmpty()) {
        chart->setTitle(tr("Отметьте колонки для оси Y"));
        replaceChart();
        m_info->clear();
        return;
    }

    QVector<int> rows;
    const int total = qMin(int(m_rows.size()), kMaxPoints);
    rows.reserve(total);
    for (int r = 0; r < total; ++r) rows << r;

    // Определяем характер оси X.
    XMode xmode = XIndex;
    if (xCol >= 0) {
        const QString t = m_columns.at(xCol).type;
        if (t == QLatin1String("timestamp") || t == QLatin1String("date")) {
            xmode = XDateTime;
        } else {
            bool allNum = true, allDt = true, any = false;
            for (int r : rows) {
                const QJsonValue v = value(r, xCol);
                if (v.isNull()) continue;
                double d; bool dt = false;
                if (!Cql::toNumber(v, &d, &dt)) { allNum = allDt = false; break; }
                any = true;
                if (!dt) allDt = false;
            }
            xmode = !any ? XIndex : allDt ? XDateTime : allNum ? XNumeric : XCategory;
        }
    }

    QString info;
    int skipped = 0;

    if (kind == KBar) {
        auto *series = new QBarSeries;
        QStringList cats;
        const int n = qMin(int(rows.size()), kMaxBars);
        for (int i = 0; i < n; ++i)
            cats << (xCol < 0 ? QString::number(i + 1) : Cql::displayValue(value(rows[i], xCol)));

        double ymin = 0, ymax = 0;
        for (int yc : yCols) {
            auto *set = new QBarSet(m_columns.at(yc).name);
            for (int i = 0; i < n; ++i) {
                double d = 0;
                if (!Cql::toNumber(value(rows[i], yc), &d)) { d = 0; ++skipped; }
                *set << d;
                ymin = qMin(ymin, d);
                ymax = qMax(ymax, d);
            }
            series->append(set);
        }
        chart->addSeries(series);
        auto *ax = new QBarCategoryAxis;
        ax->append(cats);
        if (cats.size() > 30) ax->setLabelsVisible(false);
        auto *ay = new QValueAxis;
        ay->setRange(ymin, ymax == ymin ? ymin + 1 : ymax * 1.05);
        chart->addAxis(ax, Qt::AlignBottom);
        chart->addAxis(ay, Qt::AlignLeft);
        series->attachAxis(ax);
        series->attachAxis(ay);
        if (rows.size() > kMaxBars)
            info += tr("Показаны первые %1 строк из %2. ").arg(kMaxBars).arg(rows.size());
    } else {
        double xmin = std::numeric_limits<double>::max(), xmax = -xmin;
        double ymin = xmin, ymax = -xmin;

        QList<QXYSeries *> all;
        for (int yc : yCols) {
            QXYSeries *s = nullptr;
            if (kind == KScatter) {
                auto *sc = new QScatterSeries;
                sc->setMarkerSize(7);
                s = sc;
            } else if (kind == KSpline) {
                s = new QSplineSeries;
            } else {
                s = new QLineSeries;
            }
            s->setName(m_columns.at(yc).name);

            QList<QPointF> pts;
            pts.reserve(rows.size());
            for (int i = 0; i < rows.size(); ++i) {
                double y = 0;
                if (!Cql::toNumber(value(rows[i], yc), &y)) { ++skipped; continue; }
                double x = i;
                if (xmode == XIndex || xmode == XCategory) {
                    x = i + 1;
                } else if (!Cql::toNumber(value(rows[i], xCol), &x)) {
                    ++skipped;
                    continue;
                }
                pts << QPointF(x, y);
            }
            if (m_sortX->isChecked() && xmode != XCategory)
                std::sort(pts.begin(), pts.end(), [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
            for (const QPointF &p : std::as_const(pts)) {
                xmin = qMin(xmin, p.x()); xmax = qMax(xmax, p.x());
                ymin = qMin(ymin, p.y()); ymax = qMax(ymax, p.y());
            }
            s->replace(pts);
            chart->addSeries(s);
            all << s;
        }

        if (xmin > xmax) { xmin = 0; xmax = 1; ymin = 0; ymax = 1; }
        if (xmin == xmax) { xmin -= 1; xmax += 1; }
        if (ymin == ymax) { ymin -= 1; ymax += 1; }
        const double pad = (ymax - ymin) * 0.05;

        QAbstractAxis *ax = nullptr;
        if (xmode == XDateTime) {
            auto *dt = new QDateTimeAxis;
            dt->setFormat(QStringLiteral("dd.MM.yy HH:mm"));
            dt->setRange(QDateTime::fromMSecsSinceEpoch(qint64(xmin)),
                         QDateTime::fromMSecsSinceEpoch(qint64(xmax)));
            dt->setTickCount(7);
            ax = dt;
        } else {
            auto *va = new QValueAxis;
            va->setRange(xmin, xmax);
            if (xmode == XIndex || xmode == XCategory) va->setLabelFormat(QStringLiteral("%d"));
            ax = va;
        }
        auto *ay = new QValueAxis;
        ay->setRange(ymin - pad, ymax + pad);
        chart->addAxis(ax, Qt::AlignBottom);
        chart->addAxis(ay, Qt::AlignLeft);
        for (QXYSeries *s : std::as_const(all)) {
            s->attachAxis(ax);
            s->attachAxis(ay);
        }
        if (xmode == XCategory)
            info += tr("Значения X не числовые — по оси X номер точки. Для категорий выберите «Столбцы». ");
    }

    chart->setTitle(xCol < 0 ? tr("по номеру строки")
                             : tr("X: %1").arg(m_columns.at(xCol).name));
    replaceChart();

    info += tr("Строк: %1.").arg(rows.size());
    if (skipped) info += tr(" Пропущено нечисловых значений: %1.").arg(skipped);
    m_info->setText(info);
}

void ChartDialog::savePng()
{
    const QString file = QFileDialog::getSaveFileName(this, tr("Сохранить график"),
                                                      QStringLiteral("chart.png"),
                                                      tr("PNG (*.png)"));
    if (!file.isEmpty())
        m_view->grab().save(file, "PNG");
}
