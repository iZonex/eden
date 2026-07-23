// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QScroller>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "common/settings_common.h"
#include "core/core.h"
#include "qt_common/config/shared_translation.h"
#include "qt_common/config/uisettings.h"
#include "yuzu/configuration/shared_widget.h"
#include "yuzu/deck/deck_setting_row.h"
#include "yuzu/deck/deck_settings_page.h"
#include "yuzu/deck/deck_theme.h"

namespace {
// How a sidebar section's right pane is built: from the real settings system (Settings), the theme
// picker (Theme), or a read-only information screen (all the rest) mirroring the Switch's own
// info/help sections. Info panes show real data or a faithful note — never a fake toggle.
enum class PaneKind {
    Settings,
    Theme,
    Support,
    Brightness,
    Bluetooth,
    Parental,
    Accessibility,
    Storage,
    Notifications,
    Sleep,
    About,
};

// A sidebar entry. For PaneKind::Settings, `sources` + `only_keys` pick which real settings feed its
// rows (only_keys is the curated allow-list). Info panes ignore those.
struct CategoryDef {
    const char* title;
    PaneKind kind;
    std::vector<std::pair<bool, Settings::Category>> sources; // {is_ui_linkage, category}
    std::vector<std::string> only_keys;
};

const std::vector<CategoryDef> kCategories = {
    // The Switch's own System Settings section list, in order. Sections that map to real Eden
    // settings are Builder panes (curated `only_keys` — no emulator hardware-tuning knobs); the rest
    // are info panes showing real data or a faithful note about what the Steam Deck / SteamOS does
    // in place of that Switch feature (never a fake toggle).
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Support / Health & Safety"), PaneKind::Support, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Airplane Mode"), PaneKind::Settings,
     {{false, Settings::Category::Network}},
     {"airplane_mode"}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Screen Brightness"), PaneKind::Brightness, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Bluetooth Audio"), PaneKind::Bluetooth, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Internet"), PaneKind::Settings,
     {{false, Settings::Category::Network}},
     {"network_interface"}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Parental Controls"), PaneKind::Parental, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Accessibility"), PaneKind::Accessibility, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Data Management"), PaneKind::Storage, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Controllers & Accessories"), PaneKind::Settings,
     {{false, Settings::Category::Controls}},
     {"vibration_enabled", "enable_accurate_vibrations", "motion_enabled"}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Audio"), PaneKind::Settings,
     {{false, Settings::Category::Audio}, {false, Settings::Category::SystemAudio}},
     {"volume", "sound_index", "output_device", "audio_muted"}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Display"), PaneKind::Settings,
     {{false, Settings::Category::Renderer}, {false, Settings::Category::RendererAdvanced}},
     {"resolution_setup", "aspect_ratio", "scaling_filter", "anti_aliasing",
      "fsr_sharpening_slider", "use_vsync", "fullscreen_mode"}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Themes"), PaneKind::Theme, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Notifications"), PaneKind::Notifications, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "Sleep Mode"), PaneKind::Sleep, {}, {}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "System"), PaneKind::Settings,
     {{false, Settings::Category::System}},
     {"language_index", "region_index", "time_zone_index", "use_docked_mode",
      "custom_rtc_enabled", "custom_rtc"}},
    {QT_TRANSLATE_NOOP("DeckSettingsPage", "About the Console"), PaneKind::About, {}, {}},
};

bool IsRuntimeList(const Settings::BasicSetting* setting) {
    return (setting->Specialization() & Settings::SpecializationTypeMask) ==
           Settings::RuntimeList;
}

/// The content of a Switch-style information section: an optional sub-header, an intro paragraph,
/// ◆ bullet notes, and/or label/value rows. Only real data or a faithful note — never a fake toggle.
struct InfoContent {
    QString subheader;
    QString paragraph;
    std::vector<QString> bullets;
    std::vector<std::pair<QString, QString>> rows;
};

InfoContent InfoContentFor(PaneKind kind) {
    InfoContent c;
    switch (kind) {
    case PaneKind::Support:
        c.paragraph = QObject::tr("Eden is a free, open-source emulator. Play comfortably:");
        c.bullets = {QObject::tr("Take a 10–15 minute break every hour."),
                     QObject::tr("Stop if your hands, wrists or eyes feel tired."),
                     QObject::tr("Play in a well-lit room, a comfortable distance from the screen.")};
        break;
    case PaneKind::Brightness:
        c.paragraph = QObject::tr("Screen brightness on the Steam Deck is controlled by SteamOS.");
        c.bullets = {
            QObject::tr("Open the Quick Access menu (the … button) to adjust brightness."),
            QObject::tr("Auto-brightness follows the Deck's ambient-light sensor.")};
        break;
    case PaneKind::Bluetooth:
        c.subheader = QObject::tr("Pair Device");
        c.paragraph = QObject::tr("To use Bluetooth audio, pair a device in SteamOS first.");
        c.bullets = {
            QObject::tr("Up to two wireless controllers can connect while using Bluetooth audio."),
            QObject::tr("Bluetooth microphones can't be used."),
            QObject::tr("You may notice delayed audio depending on your device."),
            QObject::tr("Connect or disconnect devices from the SteamOS Quick Access menu.")};
        break;
    case PaneKind::Parental:
        c.paragraph =
            QObject::tr("Parental controls and screen-time limits are managed by SteamOS.");
        c.bullets = {QObject::tr("Set limits in SteamOS Settings → Family.")};
        break;
    case PaneKind::Accessibility:
        c.paragraph = QObject::tr("Accessibility options are provided by SteamOS.");
        c.bullets = {QObject::tr(
            "Adjust text size, contrast, colour filters and more in SteamOS Settings → "
            "Accessibility.")};
        break;
    case PaneKind::Notifications:
        c.paragraph = QObject::tr("System notifications are handled by SteamOS.");
        break;
    case PaneKind::Sleep:
        c.paragraph = QObject::tr("Sleep and power are handled by SteamOS.");
        c.bullets = {QObject::tr("Press the power button briefly to sleep or wake."),
                     QObject::tr("Hold the power button for the power menu.")};
        break;
    case PaneKind::Storage: {
        const auto data_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::EdenDir);
        const auto nand_dir = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
        std::error_code ec;
        const auto space = std::filesystem::space(nand_dir, ec);
        if (!ec) {
            const double free_gb = static_cast<double>(space.available) / 1'000'000'000.0;
            const double total_gb = static_cast<double>(space.capacity) / 1'000'000'000.0;
            c.rows.emplace_back(QObject::tr("System memory — free"),
                                QStringLiteral("%1 GB / %2 GB")
                                    .arg(free_gb, 0, 'f', 1)
                                    .arg(total_gb, 0, 'f', 1));
        }
        c.rows.emplace_back(QObject::tr("Data folder"),
                            QString::fromStdString(Common::FS::PathToUTF8String(data_dir)));
        c.paragraph = QObject::tr("Game and save data are stored in the Eden data folder.");
        break;
    }
    case PaneKind::About:
        c.rows.emplace_back(QObject::tr("Emulator"), QStringLiteral("Eden"));
        c.rows.emplace_back(QObject::tr("Version"), QString::fromUtf8(Common::g_build_fullname));
        c.rows.emplace_back(QObject::tr("Build date"), QString::fromUtf8(Common::g_build_date));
        c.rows.emplace_back(QObject::tr("Revision"), QString::fromUtf8(Common::g_scm_rev).left(10));
        break;
    default:
        break;
    }
    return c;
}

/// Builds a read-only information pane (optional sub-header with rules, an intro paragraph, ◆ bullet
/// notes, and/or label/value rows), styled like the Switch's info/help screens. The section name is
/// shown in the sidebar.
QWidget* BuildInfoPane(PaneKind kind, const QString& title, QWidget* parent) {
    (void)title;
    const InfoContent content = InfoContentFor(kind);

    auto* page = new QWidget(parent);
    auto* page_layout = new QVBoxLayout(page);
    page_layout->setContentsMargins(30, 18, 30, 0);
    page_layout->setSpacing(0);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFocusPolicy(Qt::NoFocus);
    QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);

    auto* host = new QWidget(scroll);
    auto* col = new QVBoxLayout(host);
    col->setContentsMargins(2, 4, 12, 20);
    col->setSpacing(0);
    scroll->setWidget(host);
    page_layout->addWidget(scroll, 1);

    const auto add_rule = [&] {
        auto* rule = new QFrame(host);
        rule->setFrameShape(QFrame::HLine);
        rule->setFixedHeight(1);
        rule->setStyleSheet(
            QStringLiteral("border: none; background: %1;").arg(DeckTheme::kDivider.name()));
        col->addWidget(rule);
    };

    // Sub-header (e.g. "Pair Device") with a rule above and below, like the Switch.
    if (!content.subheader.isEmpty()) {
        add_rule();
        auto* sh = new QLabel(content.subheader, host);
        sh->setStyleSheet(
            QStringLiteral("font-size: 24px; color: %1; padding: 16px 4px;")
                .arg(DeckTheme::kText.name()));
        col->addWidget(sh);
        add_rule();
        col->addSpacing(18);
    }

    // Intro paragraph.
    if (!content.paragraph.isEmpty()) {
        auto* para = new QLabel(content.paragraph, host);
        para->setWordWrap(true);
        para->setStyleSheet(
            QStringLiteral("font-size: 20px; color: %1; padding: 2px 4px;")
                .arg(DeckTheme::kText.name()));
        col->addWidget(para);
        col->addSpacing(14);
    }

    // ◆ bullet notes.
    for (const QString& b : content.bullets) {
        auto* bullet = new QLabel(QStringLiteral("◆  %1").arg(b), host);
        bullet->setWordWrap(true);
        bullet->setStyleSheet(
            QStringLiteral("font-size: 18px; color: %1; padding: 6px 4px;")
                .arg(DeckTheme::kTextDim.name()));
        col->addWidget(bullet);
    }

    // Label/value rows (Data Management, About) with hairline dividers.
    if (!content.rows.empty()) {
        col->addSpacing(8);
        for (const auto& [label, value] : content.rows) {
            auto* row = new QWidget(host);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(4, 16, 4, 16);
            auto* l = new QLabel(label, row);
            l->setStyleSheet(
                QStringLiteral("font-size: 20px; color: %1;").arg(DeckTheme::kText.name()));
            auto* v = new QLabel(value, row);
            v->setStyleSheet(
                QStringLiteral("font-size: 19px; color: %1;").arg(DeckTheme::kTextDim.name()));
            v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            v->setWordWrap(true);
            h->addWidget(l);
            h->addStretch();
            h->addWidget(v, 0, Qt::AlignRight);
            col->addWidget(row);
            add_rule();
        }
    }
    col->addStretch();
    return page;
}
} // namespace

/// The Themes section: the Switch's two-option theme picker (Basic White / Basic Black) drawn in the
/// console style. `applied` is the active theme; `focus` is the cursor. Plain QWidget (no MOC); the
/// page drives it (Up/Down move focus, A applies) and emits ThemeChangeRequested.
class ThemePane : public QWidget {
public:
    explicit ThemePane(QWidget* parent = nullptr) : QWidget(parent) {
        applied = DeckTheme::IsLightMode() ? 0 : 1; // 0 = Basic White, 1 = Basic Black
        focus = applied;
    }
    int Focus() const {
        return focus;
    }
    void SetFocus(int f) {
        focus = std::clamp(f, 0, 1);
        update();
    }
    void SetApplied(int a) {
        applied = std::clamp(a, 0, 1);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), DeckTheme::kBackground);
        const int pad = 30;

        // No section header — the sidebar shows "Themes"; go straight to the options.
        const QString labels[2] = {QObject::tr("Basic White"), QObject::tr("Basic Black")};
        qreal y = 22;
        const qreal row_h = 74;
        for (int i = 0; i < 2; ++i) {
            const QRectF row(pad, y, width() - 2.0 * pad, row_h);
            const QRectF box = row.adjusted(0, 7, 0, -7);
            if (i == focus) {
                QPainterPath fp;
                fp.addRoundedRect(box, 12, 12);
                p.fillPath(fp, DeckTheme::kAccentSoft);
                p.setPen(QPen(DeckTheme::kAccent, 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(box, 12, 12);
            } else {
                p.setPen(DeckTheme::kDivider);
                p.drawLine(QPointF(row.left(), row.bottom()), QPointF(row.right(), row.bottom()));
            }
            // Radio dot on the right marks the active theme.
            const QPointF c(row.right() - 32, row.center().y());
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(i == applied ? DeckTheme::kAccent : DeckTheme::kToggleOff, 2));
            p.drawEllipse(c, 12, 12);
            if (i == applied) {
                p.setPen(Qt::NoPen);
                p.setBrush(DeckTheme::kAccent);
                p.drawEllipse(c, 7, 7);
            }
            QFont lf = font();
            lf.setPixelSize(21);
            p.setFont(lf);
            p.setPen(DeckTheme::kText);
            p.drawText(QRectF(row.left() + 18, row.top(), row.width() - 90, row.height()),
                       Qt::AlignVCenter | Qt::AlignLeft, labels[i]);
            y += row_h;
        }
    }

private:
    int focus = 0;
    int applied = 0;
};

DeckSettingsPage::DeckSettingsPage(Core::System& system, QWidget* parent) : DeckPage(parent) {
    // Layout mirrors the Switch's System Settings: a fixed "⚙ System Settings" header with a
    // full-width rule under it, then the section sidebar and the content pane split by a vertical
    // rule. The section title lives ONLY in the sidebar (highlighted) — the content goes straight to
    // its rows, like the console.
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(48, 30, 40, 0);
    root->setSpacing(0);

    auto* header_row = new QHBoxLayout();
    header_row->setContentsMargins(0, 0, 0, 0);
    header_row->setSpacing(14);
    page_gear = new QLabel(this);
    page_gear->setPixmap(DeckTheme::Icon(QStringLiteral("settings"), 34));
    page_gear->setFixedSize(34, 34);
    header_row->addWidget(page_gear, 0, Qt::AlignVCenter);
    page_title = new QLabel(tr("System Settings"), this);
    header_row->addWidget(page_title, 0, Qt::AlignVCenter);
    header_row->addStretch();
    root->addLayout(header_row);
    root->addSpacing(16);

    auto* top_rule = new QFrame(this);
    top_rule->setFrameShape(QFrame::HLine);
    top_rule->setFixedHeight(1);
    root->addWidget(top_rule);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 10, 0, 0);
    body->setSpacing(0);

    sidebar = new QListWidget(this);
    sidebar->setFixedWidth(340);
    sidebar->setFocusPolicy(Qt::NoFocus);
    sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    body->addWidget(sidebar);
    connect(sidebar, &QListWidget::itemClicked, this, [this](QListWidgetItem*) {
        SelectCategory(sidebar->currentRow());
        SetZone(Zone::Sidebar);
    });

    auto* v_rule = new QFrame(this);
    v_rule->setFrameShape(QFrame::VLine);
    v_rule->setFixedWidth(1);
    body->addWidget(v_rule);

    pane_stack = new QStackedWidget(this);
    body->addWidget(pane_stack, 1);
    root->addLayout(body, 1);

    // Style the header + rules for the current theme (also re-run on theme change, see ApplyTheme).
    top_rule->setObjectName(QStringLiteral("DeckSettingsRule"));
    v_rule->setObjectName(QStringLiteral("DeckSettingsRule"));
    ApplySidebarStyle();

    Build(system);

    if (!categories.empty()) {
        SelectCategory(0);
    }
}

DeckSettingsPage::~DeckSettingsPage() = default;

void DeckSettingsPage::ApplySidebarStyle() {
    // The selected section is a card (surface) with a blue border, blue left bar and blue text — the
    // Switch's sidebar selection; unselected sections are plain text on the page.
    const QString blue = DeckTheme::IsLightMode() ? QStringLiteral("#2f6cb5")
                                                  : QStringLiteral("#6ab4ff");
    sidebar->setStyleSheet(
        QStringLiteral("QListWidget { background: transparent; border: none; padding: 6px 2px; "
                       "outline: none; }"
                       "QListWidget::item { padding: 17px 18px; margin: 5px 8px; color: %1; "
                       "border: 2px solid transparent; border-left: 4px solid transparent; "
                       "border-radius: 12px; }"
                       "QListWidget::item:selected { background: %2; color: %3; "
                       "border: 2px solid %4; border-left: 4px solid %4; }")
            .arg(DeckTheme::kText.name(), DeckTheme::kSurface.name(), blue, blue));

    if (page_title != nullptr) {
        page_title->setStyleSheet(
            QStringLiteral("font-size: 32px; color: %1;").arg(DeckTheme::kText.name()));
    }
    if (page_gear != nullptr) {
        page_gear->setPixmap(DeckTheme::Icon(QStringLiteral("settings"), 34));
    }
    for (QFrame* rule : findChildren<QFrame*>(QStringLiteral("DeckSettingsRule"))) {
        rule->setStyleSheet(
            QStringLiteral("background: %1; border: none;").arg(DeckTheme::kDivider.name()));
    }
}

void DeckSettingsPage::ApplyTheme() {
    // Re-apply the colour-baked chrome (sidebar, header, rules) for the new theme; the settings rows,
    // info rows and theme picker are custom-painted and repaint themselves.
    ApplySidebarStyle();
}

void DeckSettingsPage::Build(Core::System& system) {
    ConfigurationShared::Builder builder(this, !system.IsPoweredOn());
    const auto translations = ConfigurationShared::InitializeTranslations(this);

    for (const auto& def : kCategories) {
        Category cat;
        cat.title = tr(def.title);

        // The Themes section is a two-option picker.
        if (def.kind == PaneKind::Theme) {
            theme_pane = new ThemePane(pane_stack);
            cat.page = theme_pane;
            cat.is_theme = true;
            pane_stack->addWidget(cat.page);
            categories.push_back(std::move(cat));
            sidebar->addItem(new QListWidgetItem(tr(def.title)));
            continue;
        }

        // Information sections (Data Management / About) are read-only panes, not settings rows.
        if (def.kind != PaneKind::Settings) {
            cat.page = BuildInfoPane(def.kind, cat.title, pane_stack);
            pane_stack->addWidget(cat.page);
            categories.push_back(std::move(cat));
            sidebar->addItem(new QListWidgetItem(tr(def.title)));
            continue;
        }

        // Right-pane page: just the scrollable row list — the section name is shown (highlighted) in
        // the sidebar, so we don't repeat it here (matches the Switch content pane).
        auto* page = new QWidget(pane_stack);
        auto* page_layout = new QVBoxLayout(page);
        page_layout->setContentsMargins(30, 18, 30, 0);
        page_layout->setSpacing(0);

        auto* scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setFocusPolicy(Qt::NoFocus);
        // Left-mouse gesture (touch is synthesized to it) so a tap still reaches the row.
        QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture);

        auto* rows_host = new QWidget(scroll);
        auto* rows_layout = new QVBoxLayout(rows_host);
        rows_layout->setContentsMargins(0, 0, 0, 20);
        rows_layout->setSpacing(0);
        scroll->setWidget(rows_host);
        page_layout->addWidget(scroll, 1);

        for (const auto& [is_ui, category] : def.sources) {
            auto& linkage = is_ui ? UISettings::values.linkage : Settings::values.linkage;
            const auto it = linkage.by_category.find(category);
            if (it == linkage.by_category.end()) {
                continue;
            }
            for (auto* setting : it->second) {
                if (IsRuntimeList(setting)) {
                    continue; // needs a dedicated tab to populate; keep it in the desktop dialog
                }
                if (!def.only_keys.empty() &&
                    std::find(def.only_keys.begin(), def.only_keys.end(), setting->GetLabel()) ==
                        def.only_keys.end()) {
                    continue; // curated Deck set: skip anything not in this category's allow-list
                }
                // BuildWidget appends this widget's apply function (which captures the widget via a
                // serializer lambda) to apply_funcs as it constructs the widget. If we then drop the
                // widget, that apply function must be dropped too — otherwise Apply() later invokes a
                // serializer that dereferences the deleted widget (SIGSEGV). Trim back on any drop.
                const std::size_t apply_mark = apply_funcs.size();
                auto* widget = builder.BuildWidget(setting, apply_funcs);
                if (widget == nullptr) {
                    apply_funcs.resize(apply_mark);
                    continue;
                }
                if (!widget->Valid()) {
                    apply_funcs.resize(apply_mark);
                    widget->deleteLater();
                    continue;
                }
                // A combobox whose stored value is not in its list starts at index -1; the Builder's
                // serializer then does enumeration->at(-1) on apply and throws. Normalize to the
                // first valid option so the value is always representable.
                if (widget->combobox != nullptr && widget->combobox->count() > 0 &&
                    widget->combobox->currentIndex() < 0) {
                    widget->combobox->setCurrentIndex(0);
                }
                QString label;
                QString description;
                if (const auto t = translations->find(setting->Id()); t != translations->end()) {
                    label = t->second.first;
                    description = t->second.second; // the setting's tooltip = its explanation
                }
                if (label.isEmpty()) {
                    label = QString::fromStdString(setting->GetLabel());
                }
                auto* row = new DeckSettingRow(widget, label, description, rows_host);
                row->on_tapped = [this, row] { OnRowTapped(row); };
                rows_layout->addWidget(row);
                cat.rows.push_back(row);
            }
        }
        rows_layout->addStretch();

        // Skip categories that ended up with no visible rows.
        if (cat.rows.empty()) {
            page->deleteLater();
            continue;
        }

        cat.page = page;
        pane_stack->addWidget(page);
        categories.push_back(std::move(cat));

        sidebar->addItem(new QListWidgetItem(tr(def.title)));
    }
}

DeckSettingsPage::Category* DeckSettingsPage::Current() {
    if (current_category < 0 || current_category >= static_cast<int>(categories.size())) {
        return nullptr;
    }
    return &categories[current_category];
}

void DeckSettingsPage::SelectCategory(int index) {
    if (categories.empty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(categories.size()) - 1);

    // Leaving a category: drop the row cursor.
    if (auto* cat = Current(); cat != nullptr && current_row >= 0 &&
                               current_row < static_cast<int>(cat->rows.size())) {
        cat->rows[current_row]->SetFocused(false);
    }

    current_category = index;
    current_row = -1;
    sidebar->setCurrentRow(index);
    pane_stack->setCurrentIndex(index);
}

void DeckSettingsPage::SetRow(int index) {
    auto* cat = Current();
    if (cat == nullptr || cat->rows.empty()) {
        return;
    }
    index = std::clamp(index, 0, static_cast<int>(cat->rows.size()) - 1);
    if (current_row >= 0 && current_row < static_cast<int>(cat->rows.size())) {
        cat->rows[current_row]->SetFocused(false);
    }
    current_row = index;
    cat->rows[current_row]->SetFocused(true);

    if (auto* scroll = cat->page->findChild<QScrollArea*>()) {
        scroll->ensureWidgetVisible(cat->rows[current_row], 0, 80);
    }
}

void DeckSettingsPage::MoveRow(int delta) {
    if (current_row < 0) {
        SetRow(0);
        return;
    }
    SetRow(current_row + delta);
}

void DeckSettingsPage::OnRowTapped(DeckSettingRow* row) {
    auto* cat = Current();
    if (cat == nullptr) {
        return;
    }
    for (int j = 0; j < static_cast<int>(cat->rows.size()); ++j) {
        if (cat->rows[j] == row) {
            SetZone(Zone::Rows);
            SetRow(j);
            row->Activate(); // a tap toggles a bool / cycles an enum
            dirty = true;
            return;
        }
    }
}

void DeckSettingsPage::SetZone(Zone new_zone) {
    zone = new_zone;
    if (zone == Zone::Rows) {
        SetRow(current_row < 0 ? 0 : current_row);
    } else {
        if (auto* cat = Current(); cat != nullptr && current_row >= 0 &&
                                   current_row < static_cast<int>(cat->rows.size())) {
            cat->rows[current_row]->SetFocused(false);
        }
    }
    emit HintsChanged();
}

bool DeckSettingsPage::OnNavigate(Qt::Key key) {
    if (zone == Zone::Sidebar) {
        switch (key) {
        case Qt::Key_Up:
            SelectCategory(current_category - 1);
            return true;
        case Qt::Key_Down:
            SelectCategory(current_category + 1);
            return true;
        case Qt::Key_Right:
            SetZone(Zone::Rows);
            return true;
        default:
            return false;
        }
    }
    // Zone::Rows — the Themes picker moves its own cursor between the two options.
    if (auto* cat = Current(); cat != nullptr && cat->is_theme && theme_pane != nullptr) {
        if (key == Qt::Key_Up) {
            theme_pane->SetFocus(theme_pane->Focus() - 1);
            return true;
        }
        if (key == Qt::Key_Down) {
            theme_pane->SetFocus(theme_pane->Focus() + 1);
            return true;
        }
        return false;
    }
    switch (key) {
    case Qt::Key_Up:
        MoveRow(-1);
        return true;
    case Qt::Key_Down:
        MoveRow(1);
        return true;
    case Qt::Key_Left:
        if (auto* cat = Current(); cat != nullptr && current_row >= 0) {
            cat->rows[current_row]->Adjust(-1);
            dirty = true;
        }
        return true;
    case Qt::Key_Right:
        if (auto* cat = Current(); cat != nullptr && current_row >= 0) {
            cat->rows[current_row]->Adjust(1);
            dirty = true;
        }
        return true;
    default:
        return false;
    }
}

bool DeckSettingsPage::OnAccept() {
    if (zone == Zone::Sidebar) {
        SetZone(Zone::Rows);
        return true;
    }
    // Themes: A applies the focused option (Basic White = light).
    if (auto* cat = Current(); cat != nullptr && cat->is_theme && theme_pane != nullptr) {
        const int choice = theme_pane->Focus();
        theme_pane->SetApplied(choice);
        emit ThemeChangeRequested(choice == 0);
        return true;
    }
    if (auto* cat = Current(); cat != nullptr && current_row >= 0 &&
                               current_row < static_cast<int>(cat->rows.size())) {
        cat->rows[current_row]->Activate();
        dirty = true;
    }
    return true;
}

bool DeckSettingsPage::OnBack() {
    if (zone == Zone::Rows) {
        SetZone(Zone::Sidebar);
        return true; // consumed
    }
    return false; // let the shell leave settings
}

bool DeckSettingsPage::OnPageUp() {
    if (zone == Zone::Rows) {
        MoveRow(-5);
    } else {
        SelectCategory(current_category - 1);
    }
    return true;
}

bool DeckSettingsPage::OnPageDown() {
    if (zone == Zone::Rows) {
        MoveRow(5);
    } else {
        SelectCategory(current_category + 1);
    }
    return true;
}

void DeckSettingsPage::OnActivated() {
    zone = Zone::Sidebar;
    if (!categories.empty() && current_category < 0) {
        SelectCategory(0);
    }
    emit HintsChanged();
}

void DeckSettingsPage::Apply() {
    // Nothing to write if the user did not change anything — avoids needless writes and any risk
    // from re-serializing untouched settings.
    if (!dirty) {
        return;
    }
    dirty = false;
    for (const auto& f : apply_funcs) {
        f(false); // Big Picture configures only while no game runs.
    }
    emit SaveConfigRequested();
}

std::vector<DeckHint> DeckSettingsPage::Hints() const {
    if (zone == Zone::Sidebar) {
        return {
            {QStringLiteral("A"), tr("Open")},
            {QStringLiteral("B"), tr("Back")},
        };
    }
    return {
        {QStringLiteral("A"), tr("Toggle")},
        {QString::fromUtf8("\xE2\x80\xB9\xE2\x80\xBA"), tr("Change")}, // ‹›
        {QStringLiteral("B"), tr("Categories")},
    };
}
