// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <vector>
#include <QString>

#include "yuzu/deck/deck_page.h"

namespace Core {
class System;
}
namespace ConfigurationShared {
class Builder;
}
namespace Settings {
class BasicSetting;
}

class DeckSettingRow;
class QLabel;
class QListWidget;
class QStackedWidget;

/**
 * Nintendo-Switch-System-Settings-style settings page.
 *
 * Layout: a category list on the left, and on the right the rows for the selected category with
 * thin dividers and a bright cursor over the focused row. Each row wraps a hidden
 * ConfigurationShared::Widget (built from the real settings system) as its value model, but paints
 * itself in the console style; Left/Right changes the value, A toggles/cycles.
 *
 * Navigation zones mirror the Switch: the cursor starts in the category list; A or Right steps
 * into the rows; B returns to the category list. Changes are written back on Apply().
 */
class DeckSettingsPage : public DeckPage {
    Q_OBJECT

public:
    explicit DeckSettingsPage(Core::System& system, QWidget* parent = nullptr);
    ~DeckSettingsPage() override;

    bool OnNavigate(Qt::Key key) override;
    bool OnAccept() override;
    bool OnBack() override;
    bool OnPageUp() override;
    bool OnPageDown() override;
    std::vector<DeckHint> Hints() const override;
    void OnActivated() override;
    void ApplyTheme() override;

    /// Writes every row's value back to the settings and asks for a config save.
    void Apply();

signals:
    void SaveConfigRequested();
    /// The user picked a console theme in the Themes section (true = Basic White / light).
    void ThemeChangeRequested(bool light);

private:
    enum class Zone { Sidebar, Rows };

    struct Category {
        QString title;
        QWidget* page = nullptr;      ///< Right-pane page (header + scroll of rows).
        QLabel* header = nullptr;     ///< the page's title label (re-styled on theme change)
        std::vector<DeckSettingRow*> rows;
        bool is_theme = false; ///< the Themes section (a two-option picker, not settings rows)
    };

    void Build(Core::System& system);
    void ApplySidebarStyle(); ///< (re)applies the sidebar stylesheet for the current theme

    void SetZone(Zone zone);
    void SelectCategory(int index);
    void MoveRow(int delta);
    void SetRow(int index);
    void OnRowTapped(DeckSettingRow* row);
    Category* Current();

    QLabel* page_title = nullptr; ///< the fixed "System Settings" header label
    QLabel* page_gear = nullptr;  ///< the gear icon next to the header
    QListWidget* sidebar = nullptr;
    QStackedWidget* pane_stack = nullptr;
    std::vector<Category> categories;
    std::vector<std::function<void(bool)>> apply_funcs;
    class ThemePane* theme_pane = nullptr; ///< the Themes section's picker widget

    Zone zone = Zone::Sidebar;
    int current_category = 0;
    int current_row = -1;
    bool dirty = false; ///< A value was changed since the last apply.
};
