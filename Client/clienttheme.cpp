#include "clienttheme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace {

// 在统一的 24×24 逻辑坐标内画图，外层负责缩放；图标不持有任何 QWidget。
void drawSymbol(QPainter &painter, ClientTheme::Symbol symbol, const QColor &color)
{
    painter.setPen(QPen(color, 1.65, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    using Symbol = ClientTheme::Symbol;
    switch (symbol) {
    case Symbol::Cloud: {
        // 重叠圆与圆角矩形形成云朵；白色小叶片对应参考图的品牌视觉。
        painter.setPen(Qt::NoPen);
        QLinearGradient blue(4, 3, 21, 22);
        blue.setColorAt(0, QColor("#38a2ff"));
        blue.setColorAt(1, QColor("#0758eb"));
        painter.setBrush(blue);
        painter.drawEllipse(QRectF(6, 2, 13, 14));
        painter.drawEllipse(QRectF(1, 9, 13, 12));
        painter.drawEllipse(QRectF(12, 8, 11, 13));
        painter.drawRoundedRect(QRectF(6, 12, 13, 9), 4, 4);
        painter.setBrush(Qt::white);
        QPainterPath leaf;
        leaf.moveTo(11.7, 7.6);
        leaf.cubicTo(14.8, 8, 16, 10.1, 13.9, 13.2);
        leaf.lineTo(11.8, 16.3);
        leaf.cubicTo(7.8, 14, 8.6, 11.5, 11.7, 7.6);
        painter.drawPath(leaf);
        painter.drawRoundedRect(QRectF(14, 12, 4.4, 5.5), 2, 2);
        painter.drawRoundedRect(QRectF(6.2, 12, 3.5, 5.5), 1.7, 1.7);
        break;
    }
    case Symbol::Server:
        painter.drawRoundedRect(QRectF(4, 4, 16, 6), 2, 2);
        painter.drawRoundedRect(QRectF(4, 14, 16, 6), 2, 2);
        painter.drawPoint(QPointF(7, 7));
        painter.drawPoint(QPointF(7, 17));
        painter.drawLine(QPointF(12, 7), QPointF(17, 7));
        painter.drawLine(QPointF(12, 17), QPointF(17, 17));
        break;
    case Symbol::Account:
        painter.drawEllipse(QRectF(8, 3, 8, 8));
        painter.drawArc(QRectF(4, 13, 16, 15), 0, 180 * 16);
        break;
    case Symbol::Lock:
        painter.drawRoundedRect(QRectF(5, 10, 14, 11), 2, 2);
        painter.drawArc(QRectF(8, 2, 8, 13), 0, 180 * 16);
        painter.drawLine(QPointF(12, 14), QPointF(12, 17));
        break;
    case Symbol::Eye: {
        QPainterPath eye;
        eye.moveTo(2, 12);
        eye.cubicTo(8, 3, 16, 3, 22, 12);
        eye.cubicTo(16, 21, 8, 21, 2, 12);
        painter.drawPath(eye);
        painter.drawEllipse(QRectF(9, 9, 6, 6));
        break;
    }
    case Symbol::Link:
        painter.save();
        painter.translate(12, 12);
        painter.rotate(40);
        painter.drawRoundedRect(QRectF(-4, -11, 8, 13), 4, 4);
        painter.drawRoundedRect(QRectF(-4, -2, 8, 13), 4, 4);
        painter.restore();
        break;
    case Symbol::ArrowRight:
        painter.drawLine(QPointF(4, 12), QPointF(20, 12));
        painter.drawLine(QPointF(14, 6), QPointF(20, 12));
        painter.drawLine(QPointF(14, 18), QPointF(20, 12));
        break;
    case Symbol::Upload:
    case Symbol::Download: {
        const bool upload = symbol == Symbol::Upload;
        painter.drawLine(QPointF(12, 4), QPointF(12, 17));
        const qreal tip = upload ? 4 : 17;
        const qreal tail = upload ? 9 : 12;
        painter.drawLine(QPointF(7, tail), QPointF(12, tip));
        painter.drawLine(QPointF(17, tail), QPointF(12, tip));
        painter.drawLine(QPointF(4, 17), QPointF(4, 21));
        painter.drawLine(QPointF(4, 21), QPointF(20, 21));
        painter.drawLine(QPointF(20, 21), QPointF(20, 17));
        break;
    }
    case Symbol::Folder: {
        QPainterPath folder;
        folder.moveTo(3, 7);
        folder.lineTo(3, 5);
        folder.lineTo(10, 5);
        folder.lineTo(12, 8);
        folder.lineTo(21, 8);
        folder.lineTo(21, 20);
        folder.lineTo(3, 20);
        folder.closeSubpath();
        painter.drawPath(folder);
        break;
    }
    case Symbol::File:
        painter.drawRoundedRect(QRectF(6, 3, 13, 18), 2, 2);
        painter.drawLine(QPointF(9, 10), QPointF(16, 10));
        painter.drawLine(QPointF(9, 14), QPointF(16, 14));
        painter.drawLine(QPointF(9, 18), QPointF(13, 18));
        break;
    case Symbol::Refresh:
        painter.drawArc(QRectF(4, 4, 16, 16), 35 * 16, 290 * 16);
        painter.drawLine(QPointF(19, 3), QPointF(20, 9));
        painter.drawLine(QPointF(20, 9), QPointF(14, 8));
        break;
    }
}

} // namespace

// 提供多种尺寸，登录页大标志和工具栏小图标都能保持清晰。
QIcon ClientTheme::icon(Symbol symbol, const QColor &color)
{
    QIcon result;
    for (int size : {24, 48, 96, 160}) {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(size / 24.0, size / 24.0);
        drawSymbol(painter, symbol, color);
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}

// 样式只控制外观，不靠颜色或按钮状态判断认证是否成功。
QString ClientTheme::styleSheet()
{
    return QStringLiteral(R"(
        QWidget { font-family: "Microsoft YaHei UI", "Microsoft YaHei", sans-serif;
                  font-size: 13px; color: #172c50; }
        QFrame#loginCard, QFrame#workspaceCard { background: white; border-radius: 16px; }
        QWidget#brandBar { background: rgba(255,255,255,210); }
        QLabel#brandName { font-size: 18px; font-weight: 700; }
        QLabel#loginBrand { font-size: 28px; font-weight: 700; }
        QLabel#loginTitle { font-size: 20px; font-weight: 600; }
        QLabel#pageTitle { font-size: 25px; font-weight: 700; }
        QLabel#fieldLabel { font-size: 13px; font-weight: 600; }
        QLabel#mutedText, QLabel#loginStatusLabel { color: #7c8aa2; font-size: 12px; }
        QLabel#pageSubtitle, QLabel#sidebarDescription, QLabel#serverCaption,
        QLabel#serverHintLabel, QLabel#emptyDescription, QLabel#transferStatusLabel,
        QLabel#connectionStatusLabel { color: #7c8aa2; font-size: 12px; }
        QLabel#localFileTitle, QLabel#downloadTitle, QLabel#connectionTitle,
        QLabel#emptyTitle { font-weight: 600; }
        QLabel#loginStatusLabel[state="error"] { color: #c33c4b; }
        QLabel#loginStatusLabel[state="success"] { color: #17846c; }
        QLineEdit, QComboBox { background: white; border: 1px solid #dce4ef;
            border-radius: 7px; min-height: 38px; padding: 0px 12px;
            selection-background-color: #cfe3ff; }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #2580ff; }
        QComboBox { padding-left: 8px; padding-right: 22px; }
        QComboBox QLineEdit { border: none; min-height: 36px; padding: 0px 4px; }
        QComboBox::drop-down { border: none; width: 26px; }
        QPushButton { background: white; border: 1px solid #dce4ef; border-radius: 7px;
            min-height: 38px; padding: 0px 16px; color: #324c74; }
        QPushButton:hover { border-color: #92bdff; background: #f4f8ff; }
        QPushButton:pressed { background: #e6f0ff; }
        QPushButton[primary="true"] { background: #1979ff; color: white;
            border: 1px solid #1979ff; font-size: 14px; font-weight: 600; }
        QPushButton[primary="true"]:hover { background: #0868ee; }
        QPushButton[primary="true"]:pressed { background: #075bd1; }
        QPushButton#testConnectionButton { background: #f0f7ff; border-color: #c5ddff; }
        QPushButton#textButton { border: none; background: transparent;
            color: #1979ff; padding: 0px; min-height: 24px; text-align: left; }
        QPushButton#filesButton { text-align: left; background: #e8f2ff; color: #1979ff;
            border: none; font-weight: 600; min-height: 44px; }
        QPushButton:disabled { color: #a1afc2; background: #f1f4f8; border-color: #e5eaf2; }
        QLineEdit:disabled, QComboBox:disabled { color: #9aa8bd; background: #f8faff; }
        QCheckBox { color: #60718e; spacing: 7px; font-size: 12px; }
        QCheckBox:disabled { color: #99a6ba; }
        QCheckBox::indicator { width: 15px; height: 15px; }
        QFrame#separator { background: #edf1f7; max-height: 1px; border: none; }
        QWidget#sideBar { background: #f8faff; border-right: 1px solid #e7edf6; }
        QWidget#workspace { background: #f3f6fc; }
        QTableWidget { border: none; background: white; gridline-color: #edf1f7;
            selection-background-color: #edf5ff; selection-color: #172c50; }
        QHeaderView::section { background: #f8faff; border: none; border-bottom: 1px solid #e8edf5;
            color: #74839b; padding: 12px; font-weight: 400; }
        QProgressBar { border: none; border-radius: 4px; background: #eaf0f8;
            min-height: 8px; max-height: 8px; }
        QProgressBar::chunk { background: #2580ff; border-radius: 4px; }
        QStatusBar { background: white; color: #71819b; }
        QToolTip { background: #172c50; color: white; border: none; padding: 6px; }
    )");
}
