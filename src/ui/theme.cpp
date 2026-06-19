#include "theme.h"

QString globalStyleSheet()
{
    // 全局 QSS —— 覆盖所有常用控件，消灭内联 setStyleSheet
    return QStringLiteral(R"QSS(

/* ============================================================
   全局基础
   ============================================================ */

* {
    font-family: 'Segoe UI', 'Microsoft YaHei', 'PingFang SC', sans-serif;
}

/* ============================================================
   QLineEdit —— 输入框
   ============================================================ */

QLineEdit {
    background: #333333;
    color: #cccccc;
    border: 1px solid #555555;
    border-radius: 4px;
    padding: 6px 10px;
    font-size: 13px;
    selection-background-color: #0078d4;
}

QLineEdit:focus {
    border: 1px solid #0078d4;
}

QLineEdit:disabled {
    background: #2a2a2a;
    color: #666666;
}

/* ============================================================
   QPushButton —— 按钮（默认 = secondary 灰色）
   ============================================================ */

QPushButton {
    background: #555555;
    color: #cccccc;
    border: none;
    border-radius: 4px;
    padding: 6px 16px;
    font-size: 13px;
}

QPushButton:hover {
    background: #666666;
}

QPushButton:pressed {
    background: #4a4a4a;
}

QPushButton:disabled {
    background: #444444;
    color: #888888;
}

/* 主操作按钮 */
QPushButton[cssClass="primary"] {
    background: #0078d4;
    color: white;
}
QPushButton[cssClass="primary"]:hover {
    background: #0086f0;
}
QPushButton[cssClass="primary"]:disabled {
    background: #444444;
    color: #888888;
}

/* 成功按钮 */
QPushButton[cssClass="success"] {
    background: #388e3c;
    color: white;
}
QPushButton[cssClass="success"]:hover {
    background: #43a047;
}

/* 危险按钮 */
QPushButton[cssClass="danger"] {
    background: #c62828;
    color: white;
}
QPushButton[cssClass="danger"]:hover {
    background: #d32f2f;
}

/* ============================================================
   QGroupBox —— 分组框
   ============================================================ */

QGroupBox {
    color: #cccccc;
    font-weight: bold;
    border: 1px solid #444444;
    border-radius: 4px;
    margin-top: 8px;
    padding-top: 16px;
}

QGroupBox::title {
    subcontrol-origin: margin;
    left: 12px;
    padding: 0 4px;
}

/* ============================================================
   QTabWidget / QTabBar —— 标签页
   ============================================================ */

QTabWidget::pane {
    border: 1px solid #444444;
    background: #1e1e1e;
    border-radius: 0 0 4px 4px;
}

QTabBar::tab {
    background: #2c2c2c;
    color: #aaaaaa;
    padding: 8px 18px;
    border: 1px solid #444444;
    border-bottom: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    margin-right: 2px;
}

QTabBar::tab:selected {
    background: #333333;
    color: #ffffff;
    border-bottom: 2px solid #0078d4;
}

QTabBar::tab:hover:!selected {
    background: #353535;
    color: #cccccc;
}

/* ============================================================
   QTableWidget / QHeaderView —— 表格
   ============================================================ */

QTableWidget {
    background: #1e1e1e;
    color: #cccccc;
    gridline-color: #444444;
    border: 1px solid #444444;
    border-radius: 4px;
}

QHeaderView::section {
    background: #333333;
    color: #cccccc;
    padding: 6px 8px;
    border: 1px solid #444444;
    font-weight: bold;
    font-size: 12px;
}

QTableWidget::item {
    padding: 4px 6px;
}

QTableWidget::item:selected {
    background: #0078d4;
    color: white;
}

/* ============================================================
   QListWidget —— 列表
   ============================================================ */

QListWidget {
    background: #252525;
    color: #aaaaaa;
    border: 1px solid #444444;
    border-radius: 4px;
    font-size: 12px;
    outline: none;
}

QListWidget::item {
    padding: 6px 8px;
    border: none;
}

QListWidget::item:hover {
    background: #333333;
}

QListWidget::item:selected {
    background: #0078d4;
    color: white;
}

/* ============================================================
   QTextEdit / QTextBrowser —— 文本区域
   ============================================================ */

QTextEdit, QTextBrowser {
    background: #252525;
    color: #cccccc;
    border: 1px solid #444444;
    border-radius: 4px;
    font-size: 13px;
}

/* ============================================================
   QComboBox —— 下拉框
   ============================================================ */

QComboBox {
    background: #333333;
    color: #cccccc;
    border: 1px solid #555555;
    border-radius: 4px;
    padding: 4px 10px;
    font-size: 13px;
}

QComboBox:hover {
    border: 1px solid #0078d4;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QComboBox QAbstractItemView {
    background: #333333;
    color: #cccccc;
    selection-background-color: #0078d4;
    border: 1px solid #555555;
    outline: none;
}

/* ============================================================
   QCheckBox —— 复选框
   ============================================================ */

QCheckBox {
    color: #cccccc;
    spacing: 6px;
    font-size: 13px;
}

QCheckBox::indicator {
    width: 16px;
    height: 16px;
}

/* ============================================================
   QRadioButton —— 单选按钮
   ============================================================ */

QRadioButton {
    color: #cccccc;
    spacing: 6px;
    font-size: 13px;
}

QRadioButton::indicator {
    width: 16px;
    height: 16px;
}

/* ============================================================
   QSplitter —— 分割器
   ============================================================ */

QSplitter::handle {
    background: #444444;
}

QSplitter::handle:horizontal {
    width: 2px;
}

QSplitter::handle:vertical {
    height: 2px;
}

/* ============================================================
   QStatusBar —— 状态栏
   ============================================================ */

QStatusBar {
    background: #252525;
    color: #888888;
    border-top: 1px solid #333333;
    font-size: 12px;
}

QStatusBar::item {
    border: none;
}

/* ============================================================
   QScrollBar —— 滚动条
   ============================================================ */

QScrollBar:vertical {
    background: #1e1e1e;
    width: 10px;
    margin: 0;
    border: none;
}

QScrollBar::handle:vertical {
    background: #555555;
    min-height: 30px;
    border-radius: 5px;
}

QScrollBar::handle:vertical:hover {
    background: #666666;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}

QScrollBar:horizontal {
    background: #1e1e1e;
    height: 10px;
    margin: 0;
    border: none;
}

QScrollBar::handle:horizontal {
    background: #555555;
    min-width: 30px;
    border-radius: 5px;
}

QScrollBar::handle:horizontal:hover {
    background: #666666;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}

/* ============================================================
   QToolTip —— 提示框
   ============================================================ */

QToolTip {
    background: #333333;
    color: #cccccc;
    border: 1px solid #555555;
    border-radius: 3px;
    padding: 4px 8px;
    font-size: 12px;
}

/* ============================================================
   QMessageBox —— 对话框（最小覆盖）
   ============================================================ */

QMessageBox {
    background: #2c2c2c;
}

/* ============================================================
   QLabel —— 标签（不设置全局，由各处按需指定）
   ============================================================ */

QLabel {
    color: #cccccc;
}

)QSS");
}
