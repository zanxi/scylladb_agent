#include "CellHoverPopup.h"
#include "ResultModel.h"

#include <QAbstractItemView>
#include <QEnterEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QScreen>
#include <QTextCursor>
#include <QVBoxLayout>

CellHoverPopup::CellHoverPopup(QAbstractItemView *view)
    : QFrame(view, Qt::ToolTip | Qt::FramelessWindowHint)
    , m_view(view)
{
    setFrameShape(QFrame::StyledPanel);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAutoFillBackground(true);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(6, 4, 6, 6);
    lay->setSpacing(3);

    m_title = new QLabel(this);
    m_title->setTextFormat(Qt::RichText);
    lay->addWidget(m_title);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_text->setFrameShape(QFrame::NoFrame);
    lay->addWidget(m_text);

    m_showTimer.setSingleShot(true);
    m_showTimer.setInterval(550);
    connect(&m_showTimer, &QTimer::timeout, this, &CellHoverPopup::showForIndex);

    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(250);
    connect(&m_hideTimer, &QTimer::timeout, this, [this] {
        if (!underMouse()) hide();
    });

    view->viewport()->setMouseTracking(true);
    view->viewport()->installEventFilter(this);
}

bool CellHoverPopup::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj != m_view->viewport())
        return QFrame::eventFilter(obj, ev);

    switch (ev->type()) {
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(ev);
        const QModelIndex idx = m_view->indexAt(me->position().toPoint());
        m_globalPos = me->globalPosition().toPoint();
        if (idx != QModelIndex(m_index)) {
            m_index = idx;
            if (isVisible()) m_hideTimer.start();
            if (idx.isValid() && m_enabled) m_showTimer.start();
            else m_showTimer.stop();
        }
        break;
    }
    case QEvent::Leave:
        m_showTimer.stop();
        m_index = QPersistentModelIndex();
        if (isVisible()) m_hideTimer.start();
        break;
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::Wheel:
    case QEvent::KeyPress:
        m_showTimer.stop();
        hide();
        break;
    default:
        break;
    }
    return false;
}

void CellHoverPopup::enterEvent(QEnterEvent *ev)
{
    m_hideTimer.stop();
    m_showTimer.stop(); // курсор пересёк другие ячейки по пути к окну
    QFrame::enterEvent(ev);
}

void CellHoverPopup::leaveEvent(QEvent *ev)
{
    m_hideTimer.start();
    QFrame::leaveEvent(ev);
}

void CellHoverPopup::showForIndex()
{
    if (!m_index.isValid() || !m_view->isVisible())
        return;

    const QModelIndex idx = m_index;
    const QString full = idx.data(ResultModel::FullTextRole).toString();
    const QString type = idx.data(ResultModel::TypeRole).toString();
    const bool isNull = idx.data(ResultModel::IsNullRole).toBool();
    const QString colName = idx.model()->headerData(idx.column(), Qt::Horizontal, Qt::DisplayRole).toString();

    m_title->setText(QStringLiteral("<b>%1</b> &nbsp;<i>%2</i> &nbsp;строка %3%4")
                         .arg(colName.toHtmlEscaped(), type.toHtmlEscaped())
                         .arg(idx.row() + 1)
                         .arg(isNull ? QStringLiteral(" &nbsp;<span style='color:gray'>NULL</span>")
                                     : QStringLiteral(" &nbsp;(%1 симв.)").arg(full.size())));
    m_text->setPlainText(isNull ? QStringLiteral("null") : full);

    // Размер под содержимое, но в разумных пределах.
    const QFontMetrics fm(m_text->font());
    int maxLine = 0, lines = 0;
    for (const QString &l : full.split('\n')) {
        maxLine = qMax(maxLine, fm.horizontalAdvance(l));
        if (++lines > 40) break;
    }
    const int w = qBound(220, maxLine + 40, 640);
    const int wrapLines = lines + (maxLine / qMax(1, w - 40));
    const int h = qBound(60, wrapLines * fm.lineSpacing() + m_title->sizeHint().height() + 30, 420);
    resize(w, h);

    QPoint pos = m_globalPos + QPoint(14, 18);
    if (QScreen *scr = QGuiApplication::screenAt(m_globalPos)) {
        const QRect g = scr->availableGeometry();
        if (pos.x() + w > g.right())  pos.setX(qMax(g.left(), m_globalPos.x() - w - 10));
        if (pos.y() + h > g.bottom()) pos.setY(qMax(g.top(), m_globalPos.y() - h - 10));
    }
    move(pos);
    m_text->moveCursor(QTextCursor::Start);
    show();
    raise();
}
