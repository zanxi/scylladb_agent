#pragma once

#include <QFrame>
#include <QPersistentModelIndex>
#include <QPoint>
#include <QTimer>

class QAbstractItemView;
class QLabel;
class QPlainTextEdit;

// Всплывающее окно со значением ячейки при наведении курсора.
// В отличие от обычной подсказки, окно можно прокручивать и
// выделять/копировать текст: оно не исчезает, пока курсор над ним.
class CellHoverPopup : public QFrame
{
    Q_OBJECT
public:
    explicit CellHoverPopup(QAbstractItemView *view);

    void setEnabledPopup(bool on) { m_enabled = on; if (!on) hide(); }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void enterEvent(QEnterEvent *ev) override;
    void leaveEvent(QEvent *ev) override;

private:
    void showForIndex();

    QAbstractItemView    *m_view;
    QLabel               *m_title;
    QPlainTextEdit       *m_text;
    QTimer                m_showTimer;
    QTimer                m_hideTimer;
    QPersistentModelIndex m_index;
    QPoint                m_globalPos;
    bool                  m_enabled = true;
};
