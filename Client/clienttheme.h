#ifndef CLIENTTHEME_H
#define CLIENTTHEME_H

#include <QColor>
#include <QIcon>
#include <QString>

// 只放两页共用的视觉资源，不放认证、网络或文件逻辑。
// 图标由 QPainter 绘制，避免额外下载素材，也不需要新增 QtSvg 依赖。
namespace ClientTheme {

enum class Symbol {
    Cloud, Server, Account, Lock, Eye, Link, ArrowRight,
    Upload, Download, Folder, File, Refresh
};

// 生成多分辨率图标；QIcon 按值返回并管理内部图像，调用方不需要 delete。
QIcon icon(Symbol symbol, const QColor &color = QColor("#61728f"));

// 两个窗口使用相同的控件样式；通过 objectName 区分卡片、主按钮和导航按钮。
QString styleSheet();

} // namespace ClientTheme

#endif // CLIENTTHEME_H
