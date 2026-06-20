#include "theme.h"

QString globalStyleSheet()
{
    // 全局 QSS —— 亮色主题，覆盖所有常用控件
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
    background: #ffffff;
    color: #1a1a1a;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
    padding: 6px 10px;
    font-size: 13px;
    selection-background-color: #2563eb;
}

QLineEdit:focus {
    border: 1px solid #2563eb;
}

QLineEdit:disabled {
    background: #f0f0f0;
    color: #999999;
}

/* ============================================================
   QPushButton —— 按钮（默认 = secondary 灰色）
   ============================================================ */

QPushButton {
    background: #e0e0e0;
    color: #1a1a1a;
    border: none;
    border-radius: 4px;
    padding: 6px 16px;
    font-size: 13px;
}

QPushButton:hover {
    background: #d0d0d0;
}

QPushButton:pressed {
    background: #c0c0c0;
}

QPushButton:disabled {
    background: #e8e8e8;
    color: #999999;
}

/* 主操作按钮 */
QPushButton[cssClass="primary"] {
    background: #2563eb;
    color: white;
}
QPushButton[cssClass="primary"]:hover {
    background: #1d4ed8;
}
QPushButton[cssClass="primary"]:disabled {
    background: #c0c0c0;
    color: #999999;
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
    color: #1a1a1a;
    font-weight: bold;
    border: 1px solid #d0d0d0;
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
    border: 1px solid #d0d0d0;
    background: #fafafa;
    border-radius: 0 0 4px 4px;
}

QTabBar::tab {
    background: #e8e8e8;
    color: #555555;
    padding: 8px 18px;
    border: 1px solid #d0d0d0;
    border-bottom: none;
    border-top-left-radius: 4px;
    border-top-right-radius: 4px;
    margin-right: 2px;
}

QTabBar::tab:selected {
    background: #ffffff;
    color: #1a1a1a;
    border-bottom: 2px solid #2563eb;
}

QTabBar::tab:hover:!selected {
    background: #f0f0f0;
    color: #1a1a1a;
}

/* ============================================================
   QTableWidget / QHeaderView —— 表格
   ============================================================ */

QTableWidget {
    background: #fafafa;
    color: #1a1a1a;
    gridline-color: #d0d0d0;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
}

QHeaderView::section {
    background: #f0f0f0;
    color: #1a1a1a;
    padding: 6px 8px;
    border: 1px solid #d0d0d0;
    font-weight: bold;
    font-size: 12px;
}

QTableWidget::item {
    padding: 4px 6px;
}

QTableWidget::item:selected {
    background: #2563eb;
    color: white;
}

/* ============================================================
   QListWidget —— 列表
   ============================================================ */

QListWidget {
    background: #f0f0f0;
    color: #555555;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
    font-size: 12px;
    outline: none;
}

QListWidget::item {
    padding: 6px 8px;
    border: none;
}

QListWidget::item:hover {
    background: #e0e0e0;
}

QListWidget::item:selected {
    background: #2563eb;
    color: white;
}

/* ============================================================
   QTextEdit / QTextBrowser —— 文本区域
   ============================================================ */

QTextEdit, QTextBrowser {
    background: #f0f0f0;
    color: #1a1a1a;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
    font-size: 13px;
}

/* ============================================================
   QComboBox —— 下拉框
   ============================================================ */

QComboBox {
    background: #ffffff;
    color: #1a1a1a;
    border: 1px solid #d0d0d0;
    border-radius: 4px;
    padding: 4px 10px;
    font-size: 13px;
}

QComboBox:hover {
    border: 1px solid #2563eb;
}

QComboBox::drop-down {
    border: none;
    width: 20px;
}

QComboBox QAbstractItemView {
    background: #ffffff;
    color: #1a1a1a;
    selection-background-color: #2563eb;
    border: 1px solid #d0d0d0;
    outline: none;
}

/* ============================================================
   QCheckBox —— 复选框
   ============================================================ */

QCheckBox {
    color: #1a1a1a;
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
    color: #1a1a1a;
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
    background: #d0d0d0;
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
    background: #f0f0f0;
    color: #888888;
    border-top: 1px solid #d0d0d0;
    font-size: 12px;
}

QStatusBar::item {
    border: none;
}

/* ============================================================
   QScrollBar —— 滚动条
   ============================================================ */

QScrollBar:vertical {
    background: #fafafa;
    width: 10px;
    margin: 0;
    border: none;
}

QScrollBar::handle:vertical {
    background: #c0c0c0;
    min-height: 30px;
    border-radius: 5px;
}

QScrollBar::handle:vertical:hover {
    background: #a0a0a0;
}

QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
    height: 0;
}

QScrollBar:horizontal {
    background: #fafafa;
    height: 10px;
    margin: 0;
    border: none;
}

QScrollBar::handle:horizontal {
    background: #c0c0c0;
    min-width: 30px;
    border-radius: 5px;
}

QScrollBar::handle:horizontal:hover {
    background: #a0a0a0;
}

QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
    width: 0;
}

/* ============================================================
   QToolTip —— 提示框
   ============================================================ */

QToolTip {
    background: #ffffff;
    color: #1a1a1a;
    border: 1px solid #d0d0d0;
    border-radius: 3px;
    padding: 4px 8px;
    font-size: 12px;
}

/* ============================================================
   QMessageBox —— 对话框（最小覆盖）
   ============================================================ */

QMessageBox {
    background: #fafafa;
}

/* ============================================================
   QLabel —— 标签（不设置全局，由各处按需指定）
   ============================================================ */

QLabel {
    color: #1a1a1a;
}

)QSS");
}
