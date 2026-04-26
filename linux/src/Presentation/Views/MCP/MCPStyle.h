#pragma once

// Shared stylesheet for MCPWindow and its 5 tab views.
//
// IMPORTANT: The app ships a global stylesheet (resources/style-dark.qss)
// that sets `QWidget { background-color: #1e1e2e; }`. That rule matches
// every widget in the app including our MCP children. To win in the
// cascade, every rule here is prefixed with `#mcpRoot` (the object name
// of MCPWindow) — that raises specificity above the global QWidget rule.

#include <QString>

namespace gridex {

inline QString kMCPStyleSheet = QStringLiteral(R"(
/* Root + all descendant QWidgets inside the MCP window get a uniform
 * surface color (matches the header). Individual tiles and controls then
 * override as needed below. */
QDialog#mcpRoot {
    background-color: #1e1e2e;
}
QDialog#mcpRoot QWidget {
    background-color: transparent;
}

/* ---- Header bar ---- */
QDialog#mcpRoot QWidget#mcpHeader {
    background-color: #1e1e2e;
    border-bottom: 1px solid rgba(255, 255, 255, 30);
}
QDialog#mcpRoot QLabel#mcpHeaderTitle {
    font-size: 15px;
    font-weight: 600;
    color: #cdd6f4;
}
QDialog#mcpRoot QLabel#mcpHeaderDetail {
    font-size: 11px;
    color: rgba(205, 214, 244, 160);
}
QDialog#mcpRoot QLabel#mcpStatusDot {
    background-color: rgba(255, 255, 255, 25);
    border-radius: 12px;
}

/* ---- Tab bar ---- */
QDialog#mcpRoot QTabWidget#mcpTabs::pane {
    border: none;
    background: #1e1e2e;
}
QDialog#mcpRoot QTabBar::tab {
    padding: 10px 22px;
    margin: 0 2px;
    border: none;
    background: transparent;
    color: rgba(205, 214, 244, 140);
    font-size: 13px;
}
QDialog#mcpRoot QTabBar::tab:selected {
    color: #cdd6f4;
    border-bottom: 2px solid #89b4fa;
    font-weight: 600;
}
QDialog#mcpRoot QTabBar::tab:hover:!selected {
    color: #cdd6f4;
}

/* ---- Scroll area ---- */
QDialog#mcpRoot QScrollArea#mcpScroll,
QDialog#mcpRoot QScrollArea#mcpScroll > QWidget,
QDialog#mcpRoot QScrollArea#mcpScroll > QWidget > QWidget {
    background: #1e1e2e;
    border: none;
}

/* ---- Card tile ---- */
QDialog#mcpRoot QFrame[card="true"] {
    background-color: #313244;
    border: 1px solid rgba(255, 255, 255, 20);
    border-radius: 8px;
}

/* ---- Section title ---- */
QDialog#mcpRoot QLabel[role="section-title"] {
    font-size: 14px;
    font-weight: 600;
    color: #cdd6f4;
    background-color: transparent;
}
QDialog#mcpRoot QLabel[role="muted"] {
    color: rgba(205, 214, 244, 170);
    font-size: 12px;
}
QDialog#mcpRoot QLabel[role="hint"] {
    color: rgba(205, 214, 244, 130);
    font-size: 11px;
}
QDialog#mcpRoot QLabel[role="mono"] {
    font-family: "Menlo", "Consolas", monospace;
    font-size: 12px;
    color: rgba(205, 214, 244, 170);
}

/* ---- Buttons ---- */
QDialog#mcpRoot QPushButton[accent="true"] {
    background-color: #89b4fa;
    color: #1e1e2e;
    border: none;
    border-radius: 5px;
    padding: 7px 18px;
    font-weight: 600;
    font-size: 12px;
}
QDialog#mcpRoot QPushButton[accent="true"]:hover  { background-color: #a6c1fc; }
QDialog#mcpRoot QPushButton[accent="true"]:pressed { background-color: #6b96e0; }

QDialog#mcpRoot QPushButton[danger="true"] {
    background-color: #f38ba8;
    color: #1e1e2e;
    border: none;
    border-radius: 5px;
    padding: 7px 18px;
    font-weight: 600;
    font-size: 12px;
}
QDialog#mcpRoot QPushButton[danger="true"]:hover  { background-color: #f59db7; }
QDialog#mcpRoot QPushButton[danger="true"]:pressed { background-color: #d77b96; }

QDialog#mcpRoot QPushButton[link="true"] {
    background: transparent;
    border: none;
    color: #89b4fa;
    padding: 4px 6px;
    text-align: left;
    font-size: 12px;
    font-weight: 500;
}
QDialog#mcpRoot QPushButton[link="true"]:hover { text-decoration: underline; }

/* ---- Inputs ---- */
QDialog#mcpRoot QLineEdit#mcpSearch {
    padding: 7px 10px;
    border: 1px solid rgba(255, 255, 255, 30);
    border-radius: 6px;
    background: #181825;
    color: #cdd6f4;
}
QDialog#mcpRoot QLineEdit#mcpSearch:focus { border-color: #89b4fa; }

QDialog#mcpRoot QComboBox {
    padding: 5px 10px;
    border: 1px solid rgba(255, 255, 255, 30);
    border-radius: 6px;
    background: #181825;
    color: #cdd6f4;
    min-height: 24px;
}
QDialog#mcpRoot QComboBox:focus { border-color: #89b4fa; }

QDialog#mcpRoot QSpinBox {
    padding: 4px 8px;
    border: 1px solid rgba(255, 255, 255, 30);
    border-radius: 4px;
    background: #181825;
    color: #cdd6f4;
}

QDialog#mcpRoot QPlainTextEdit {
    background: #181825;
    color: #cdd6f4;
    border: 1px solid rgba(255, 255, 255, 30);
    border-radius: 6px;
}

/* ---- Tables ---- */
QDialog#mcpRoot QTableWidget {
    background: #1e1e2e;
    alternate-background-color: rgba(255, 255, 255, 10);
    border: 1px solid rgba(255, 255, 255, 20);
    border-radius: 6px;
    gridline-color: transparent;
    selection-background-color: rgba(137, 180, 250, 80);
    selection-color: #cdd6f4;
    color: #cdd6f4;
}
QDialog#mcpRoot QTableWidget::item { padding: 8px 10px; }
QDialog#mcpRoot QHeaderView::section {
    background: #313244;
    color: rgba(205, 214, 244, 200);
    padding: 8px 10px;
    border: none;
    border-bottom: 1px solid rgba(255, 255, 255, 30);
    font-size: 11px;
    font-weight: 600;
}

/* ---- Info banner ---- */
QDialog#mcpRoot QFrame#mcpBanner {
    background-color: rgba(137, 180, 250, 40);
    border: 1px solid rgba(137, 180, 250, 80);
    border-radius: 6px;
}

/* ---- Splitter handle ---- */
QDialog#mcpRoot QSplitter::handle {
    background-color: rgba(255, 255, 255, 30);
    width: 1px;
}
)");

}  // namespace gridex
