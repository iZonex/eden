// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <iterator>

#include <QAbstractListModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QIdentityProxyModel>
#include <QLabel>
#include <QListView>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QVBoxLayout>

#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/deck/deck_all_software_page.h"
#include "yuzu/deck/deck_game_delegate.h"
#include "yuzu/deck/deck_keyboard.h"
#include "yuzu/deck/deck_option_menu.h"
#include "yuzu/deck/deck_theme.h"

namespace {
// Sized so five tiles fit across the Deck's 1280px panel once the wider selection margin is taken
// into account (cell = card + 2 * kGridCardMargin), matching the reference All Software row.
constexpr int kCardW = 190;
constexpr int kCardH = 190;
constexpr int kHeaderH = 108;

// The sort menu, in the order it is shown. Publisher, which the console also offers, is missing on
// purpose: the game list model carries no developer/publisher field, so there is nothing to sort by
// without widening the model itself. Size stands in its place.
constexpr DeckSortKey kSortOrder[] = {
    DeckSortKey::Recent,     DeckSortKey::LastPlayed, DeckSortKey::DateAdded,
    DeckSortKey::PlayTime,   DeckSortKey::TitleAZ,    DeckSortKey::TitleZA,
    DeckSortKey::Size,
};

// Filter menu row ids. Groups and formats are appended after these with an offset, so one id space
// covers the whole menu.
enum FilterId {
    FilterFavorites = 0,
    FilterUnplayed = 1,
    FilterPlayed = 2,
    FilterGroupBase = 100,
    FilterTypeBase = 1000,
};

u64 ProgramIdOf(const QModelIndex& idx) {
    return idx.data(GameListItemPath::ProgramIdRole).toULongLong();
}
bool IsGameRow(const QModelIndex& idx) {
    return idx.data(GameListItem::TypeRole).toInt() == static_cast<int>(GameListItemType::Game);
}
} // namespace

// The Switch All Software top bar: either centred "Software | Groups" tabs (with L/R hints) or a
// left-aligned title, a rule beneath, and the sort in effect on the right.
class AllSoftHeader : public QWidget {
public:
    explicit AllSoftHeader(QWidget* parent) : QWidget(parent) {
        setFixedHeight(kHeaderH);
    }
    void ShowTabs(bool groups_active) {
        tabs = true;
        groups = groups_active;
        update();
    }
    void ShowTitle(const QString& t) {
        tabs = false;
        title = t;
        update();
    }
    void SetSort(const QString& s) {
        sort_text = s;
        update();
    }
    void SetShowControls(bool b) {
        show_controls = b;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int cx = width() / 2;
        const QColor blue = DeckTheme::IsLightMode() ? QColor(0x2f, 0x6c, 0xb5)
                                                     : QColor(0x6a, 0xb4, 0xff);
        QFont f = font();

        if (tabs) {
            f.setPixelSize(26);
            p.setFont(f);
            const QString sw = tr("Software");
            const QString gr = tr("Groups");
            const QFontMetrics fm(f);
            const int gap = 80;
            const int sw_w = fm.horizontalAdvance(sw);
            const int gr_w = fm.horizontalAdvance(gr);
            const int total = sw_w + gap + gr_w;
            const int sw_x = cx - total / 2;
            const int gr_x = sw_x + sw_w + gap;
            const int ty = 22;
            // Software tab.
            p.setPen(groups ? DeckTheme::kTextDim : blue);
            p.drawText(QRect(sw_x, ty, sw_w, 34), Qt::AlignCenter, sw);
            // Groups tab.
            p.setPen(groups ? blue : DeckTheme::kTextDim);
            p.drawText(QRect(gr_x, ty, gr_w, 34), Qt::AlignCenter, gr);
            // Active underline.
            p.setPen(Qt::NoPen);
            p.setBrush(blue);
            if (groups) {
                p.drawRoundedRect(QRectF(gr_x, ty + 36, gr_w, 3), 1.5, 1.5);
            } else {
                p.drawRoundedRect(QRectF(sw_x, ty + 36, sw_w, 3), 1.5, 1.5);
            }
            // L / R hints on either side.
            p.setPen(Qt::NoPen);
            p.setBrush(DeckTheme::kSurface);
            const auto draw_lr = [&](int x, const QString& s) {
                const QRectF r(x, ty + 2, 30, 26);
                p.setPen(Qt::NoPen);
                p.setBrush(DeckTheme::kSurface);
                p.drawRoundedRect(r, 6, 6);
                p.setPen(DeckTheme::kTextDim);
                QFont sf = f;
                sf.setPixelSize(18);
                p.setFont(sf);
                p.drawText(r, Qt::AlignCenter, s);
                p.setFont(f);
            };
            draw_lr(sw_x - 54, QStringLiteral("L"));
            draw_lr(gr_x + gr_w + 24, QStringLiteral("R"));
        } else {
            f.setPixelSize(30);
            p.setFont(f);
            p.setPen(DeckTheme::kText);
            p.drawText(QRect(4, 18, width() - 8, 40), Qt::AlignLeft | Qt::AlignVCenter, title);
        }

        // Rule.
        QColor rule = DeckTheme::kText;
        rule.setAlpha(40);
        p.setPen(QPen(rule, 1));
        p.drawLine(0, 66, width(), 66);

        if (show_controls) {
            // Only the sort text lives in the header (top-right). The funnel + sort icons are drawn
            // by the page as a column on the far left, beside the games, like the Switch.
            f.setPixelSize(20);
            p.setFont(f);
            p.setPen(DeckTheme::kTextDim);
            p.drawText(QRect(width() - 460, 74, 456, 30), Qt::AlignRight | Qt::AlignVCenter,
                       sort_text);
        }
    }

private:
    bool tabs = true;
    bool groups = false;
    bool show_controls = true;
    QString title;
    QString sort_text;
};

// The Switch's name bubble: the selected game's title in a rounded card with a small tail pointing
// up at its tile. A plain rounded label reads as a floating chip; the tail is what ties the name to
// the tile it belongs to, which matters once the grid is several rows deep.
class NamePill : public QWidget {
public:
    explicit NamePill(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        // The bubble is a rounded card with a tail; the shell's blanket "fill every widget with the
        // window colour" pass would square it off and paint wedges beside the tail.
        setProperty("deckTranslucent", true);
        setAutoFillBackground(false);
    }

    /// `max_width` bounds the bubble so a long title cannot grow it wider than the page.
    void SetText(const QString& t, int max_width) {
        QFont f = font();
        f.setPixelSize(kFontPx);
        const QFontMetrics fm(f);
        text = fm.elidedText(t, Qt::ElideRight, std::max(60, max_width - 2 * kPadX));
        const int w = fm.horizontalAdvance(text) + 2 * kPadX;
        resize(std::clamp(w, 2 * kRadius + 2 * kTailW, std::max(max_width, 2 * kRadius + 2 * kTailW)),
               kTailH + kBodyH);
        update();
    }

    /// True points the tail up at a tile above the bubble; false flips it to point down, for the
    /// bottom row where there is no room underneath.
    void SetTailUp(bool up) {
        if (tail_up != up) {
            tail_up = up;
            update();
        }
    }

    static constexpr int Height() {
        return kTailH + kBodyH;
    }
    static constexpr int TailHeight() {
        return kTailH;
    }

protected:
    static constexpr int kFontPx = 22;
    static constexpr int kPadX = 18;
    static constexpr int kBodyH = 40;
    static constexpr int kTailH = 9;
    static constexpr int kTailW = 11;
    static constexpr int kRadius = 12;

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF body(0, tail_up ? kTailH : 0, width(), kBodyH);

        // Body and tail as one path, so the tail's base has no seam against the body.
        QPainterPath shape;
        shape.addRoundedRect(body, kRadius, kRadius);
        QPainterPath tail;
        const qreal cx = width() / 2.0;
        const qreal base = tail_up ? kTailH + 1 : kBodyH - 1;
        const qreal tip = tail_up ? 0 : kBodyH + kTailH;
        tail.moveTo(cx - kTailW, base);
        tail.lineTo(cx, tip);
        tail.lineTo(cx + kTailW, base);
        tail.closeSubpath();
        shape = shape.united(tail);

        // Soft lift, matching the focused tile's shadow.
        for (int s = 6; s >= 1; --s) {
            p.fillPath(shape.translated(0, s * 0.5), QColor(0, 0, 0, 6));
        }
        p.fillPath(shape, DeckTheme::kSurface);

        QFont f = font();
        f.setPixelSize(kFontPx);
        p.setFont(f);
        p.setPen(DeckTheme::IsLightMode() ? QColor(0x2f, 0x6c, 0xb5) : QColor(0x6a, 0xb4, 0xff));
        p.drawText(body, Qt::AlignCenter, text);
    }

private:
    QString text;
    bool tail_up = true;
};

// One-row-per-group model plus a trailing "＋ New Group" row, for the Groups tab.
class GroupsModel : public QAbstractListModel {
public:
    GroupsModel(const DeckGroups* g, QObject* parent) : QAbstractListModel(parent), groups(g) {}
    void Refresh() {
        beginResetModel();
        endResetModel();
    }
    int rowCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(groups->Count()) + 1;
    }
    int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : GameListModel::COLUMN_COUNT;
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.column() != 0) {
            return {};
        }
        const int r = index.row();
        if (r < static_cast<int>(groups->Count())) {
            if (role == DeckGroupRole) {
                return r;
            }
            if (role == Qt::DisplayRole) {
                return groups->Groups()[r].name;
            }
        } else if (r == static_cast<int>(groups->Count()) && role == DeckNewGroupRole) {
            return true;
        }
        return {};
    }

private:
    const DeckGroups* groups;
};

// One proxy does both halves of "what is shown and in what order", so the Software tab and a
// group's contents cannot drift apart: they are two instances of the same class with different
// filter states. Ordering and filtering both live in deck_library_order, shared with the home rail.
class LibraryViewProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void SetContext(const DeckLibraryStats* s, const PlayTime::PlayTimeManager* pt,
                    const DeckGroups* g) {
        stats = s;
        play_time = pt;
        groups = g;
    }
    void SetKey(DeckSortKey k) {
        key = k;
    }
    void SetFilter(const DeckFilterState& f) {
        filter = f;
    }
    void Reapply() {
        invalidate();
        sort(0);
    }

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        if (stats == nullptr) {
            return QSortFilterProxyModel::lessThan(left, right);
        }
        return DeckLessThan(left, right, key, *stats, play_time);
    }

    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const QModelIndex idx = sourceModel()->index(row, 0, parent);
        if (!IsGameRow(idx)) {
            return false;
        }
        if (stats == nullptr) {
            return true;
        }
        return DeckAccepts(idx, filter, *stats, groups);
    }

private:
    const DeckLibraryStats* stats = nullptr;
    const PlayTime::PlayTimeManager* play_time = nullptr;
    const DeckGroups* groups = nullptr;
    DeckSortKey key = DeckSortKey::Recent;
    DeckFilterState filter;
};

namespace {
// Passes the library through unchanged but answers DeckGroupMemberRole for the add-games picker.
class MemberProxy : public QIdentityProxyModel {
public:
    using QIdentityProxyModel::QIdentityProxyModel;
    void SetGroup(const DeckGroups* g, int idx) {
        groups = g;
        group = idx;
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (role == DeckGroupMemberRole && groups != nullptr && group >= 0) {
            return groups->IsMember(group, ProgramIdOf(QIdentityProxyModel::mapToSource(index)));
        }
        return QIdentityProxyModel::data(index, role);
    }

private:
    const DeckGroups* groups = nullptr;
    int group = -1;
};
} // namespace

DeckAllSoftwarePage::DeckAllSoftwarePage(QAbstractItemModel* library_, DeckLibraryStats& stats_,
                                         const PlayTime::PlayTimeManager& play_time_,
                                         QWidget* parent)
    : DeckPage(parent), stats{stats_}, play_time{play_time_}, base(library_) {
    setAutoFillBackground(true);

    LoadPreferences();

    sorted = new LibraryViewProxy(this);
    sorted->SetContext(&stats, &play_time, &groups);
    sorted->setSourceModel(base);
    sorted->setDynamicSortFilter(true);

    groups_model = new GroupsModel(&groups, this);

    // Both the group view and the add-games picker read the LIBRARY, not the filtered Software tab.
    // Chaining them off the filtered view meant a filter chosen on the Software tab silently emptied
    // groups and hid games from the picker, which looked like data loss.
    group_filter = new LibraryViewProxy(this);
    group_filter->SetContext(&stats, &play_time, &groups);
    group_filter->setSourceModel(base);
    group_filter->setDynamicSortFilter(true);

    auto* mp = new MemberProxy(this);
    mp->setSourceModel(base);
    member_proxy = mp;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(64, 34, 64, 24);
    outer->setSpacing(6);

    header = new AllSoftHeader(this);
    outer->addWidget(header);

    grid = new QListView(this);
    grid->setModel(sorted);
    delegate = new DeckGameDelegate(grid);
    delegate->SetCardSize(kCardW, kCardH);
    // The grid is the only focus zone on this page (unlike the home rail, which hands focus off to
    // the dock), so the selection is always the bright variant. Stated rather than inherited from
    // the delegate's default, since that default exists for the rail's sake.
    delegate->SetRailActive(true);
    delegate->SetStats(&stats);
    grid->setItemDelegate(delegate);
    grid->setViewMode(QListView::IconMode);
    grid->setFlow(QListView::LeftToRight);
    grid->setWrapping(true);
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setUniformItemSizes(true);
    grid->setSelectionMode(QAbstractItemView::SingleSelection);
    grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    grid->setFocusPolicy(Qt::NoFocus);
    grid->setFrameShape(QFrame::NoFrame);
    grid->setStyleSheet(QStringLiteral("QListView{background:transparent;}"));
    // A left gutter holds the filter/sort column, so the games start indented (Switch layout).
    auto* grid_row = new QHBoxLayout();
    grid_row->setContentsMargins(0, 0, 0, 0);
    grid_row->addSpacing(56);
    grid_row->addWidget(grid, 1);
    outer->addLayout(grid_row, 1);

    connect(grid, &QAbstractItemView::clicked, this, [this](const QModelIndex&) { OnAccept(); });

    // Floating name bubble under the selected tile (Switch style).
    name_pill = new NamePill(this);
    name_pill->setVisible(false);

    keyboard = new DeckKeyboard(this);
    keyboard->hide();
    connect(keyboard, &DeckKeyboard::Accepted, this, [this](QString text) {
        if (renaming) {
            groups.RenameGroup(current_group, text);
            RefreshGroups();
            // The group's title is drawn by SetView, so re-enter the view to pick up the new name —
            // UpdateHeader alone only refreshes the sort line, leaving the old name on screen.
            SetView(View::GroupDetail);
        } else {
            current_group = groups.CreateGroup(text);
            RefreshGroups();
            SetView(View::GroupDetail);
        }
        emit HintsChanged();
    });
    connect(keyboard, &DeckKeyboard::Cancelled, this, [this] { emit HintsChanged(); });

    menu = new DeckOptionMenu(this);
    connect(menu, &DeckOptionMenu::Picked, this, [this](int id) {
        sort_key = kSortOrder[std::clamp(id, 0, static_cast<int>(std::size(kSortOrder)) - 1)];
        ApplyOrdering();
        SavePreferences();
        UpdateHeader();
    });
    connect(menu, &DeckOptionMenu::Toggled, this, [this](int id, bool on) {
        if (id == FilterFavorites) {
            filter_state.favorites_only = on;
        } else if (id == FilterUnplayed) {
            filter_state.played = on ? DeckFilterState::Played::Never : DeckFilterState::Played::Any;
            // "Unplayed" and "Played" are opposites, so turning one on clears the other.
            if (on) {
                menu->SetChecked(FilterPlayed, false);
            }
        } else if (id == FilterPlayed) {
            filter_state.played =
                on ? DeckFilterState::Played::Played : DeckFilterState::Played::Any;
            if (on) {
                menu->SetChecked(FilterUnplayed, false);
            }
        } else if (id >= FilterTypeBase) {
            // Resolved against the list captured when the menu opened, so a scan finishing mid-menu
            // cannot make a row mean a different format than the one it is labelled with.
            const int index = id - FilterTypeBase;
            if (index < 0 || index >= menu_types.size()) {
                return;
            }
            // One format at a time — clear any other format row that was on.
            for (int i = 0; i < menu_types.size(); ++i) {
                if (i != index) {
                    menu->SetChecked(FilterTypeBase + i, false);
                }
            }
            filter_state.file_type = on ? menu_types.at(index) : QString();
        } else if (id >= FilterGroupBase) {
            const int index = id - FilterGroupBase;
            if (index < 0 || index >= menu_groups.size()) {
                return;
            }
            for (int i = 0; i < menu_groups.size(); ++i) {
                if (i != index) {
                    menu->SetChecked(FilterGroupBase + i, false);
                }
            }
            filter_state.group = on ? index : -1;
        }
        ApplyOrdering();
        SavePreferences();
        UpdateHeader();
    });
    connect(menu, &DeckOptionMenu::Closed, this, [this] {
        delegate->SetRailActive(true);
        grid->viewport()->update();
        update();
        emit HintsChanged();
    });

    shimmer = new QTimer(this);
    shimmer->setInterval(40);
    connect(shimmer, &QTimer::timeout, this, [this] {
        phase = (phase + 5) % 360;
        if (delegate != nullptr) {
            delegate->SetPhase(phase);
        }
        grid->viewport()->update();
    });

    ApplyOrdering();
    ApplyTheme();
}

DeckAllSoftwarePage::~DeckAllSoftwarePage() {
    // The models below hold a pointer to `groups` (and the proxies to `stats`), and QObject
    // children are destroyed after this class's members. Detach the view and drop them here, while
    // what they point at still exists.
    grid->setModel(nullptr);
    delete sorted;
    delete group_filter;
    delete member_proxy;
    delete groups_model;
}

void DeckAllSoftwarePage::resizeEvent(QResizeEvent* event) {
    DeckPage::resizeEvent(event);
    keyboard->setGeometry(rect());
    if (menu->isVisible()) {
        menu->setGeometry(rect());
    }
    PositionNamePill();
}

bool DeckAllSoftwarePage::MenuOpen() const {
    return menu != nullptr && menu->isVisible();
}

void DeckAllSoftwarePage::paintEvent(QPaintEvent* event) {
    DeckPage::paintEvent(event);
    // Funnel + sort icons as a column on the far left, beside the games (not on the Groups tab).
    if (view == View::Groups || keyboard->isVisible()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const qreal cx = 90;                // centred in the left gutter (grid starts at ~120)
    const qreal top = grid->y() + 26.0; // aligned with the first game row

    // Each icon lights up when its menu has something in effect, and wears the same soft disc the
    // dock uses when the cursor is on it — otherwise the column reads as decoration, which is
    // exactly what it was before it became reachable.
    const auto icon_ink = [&](int item) {
        const bool active = item == GutterFilter ? !filter_state.IsDefault()
                                                 : sort_key != DeckSortKey::Recent;
        return active ? DeckTheme::kAccent : DeckTheme::kTextDim;
    };
    const auto draw_focus = [&](int item, qreal centre_y) {
        if (zone != Zone::Gutter || gutter_item != item || MenuOpen()) {
            return;
        }
        QColor hl = DeckTheme::kText;
        hl.setAlpha(DeckTheme::IsLightMode() ? 30 : 44);
        p.setPen(Qt::NoPen);
        p.setBrush(hl);
        p.drawEllipse(QPointF(cx, centre_y), 27, 27);
    };

    p.setBrush(Qt::NoBrush);
    // Funnel.
    {
        const qreal x = cx - 14, y = top, w = 28, h = 24;
        draw_focus(GutterFilter, y + h / 2);
        QPainterPath funnel;
        funnel.moveTo(x, y);
        funnel.lineTo(x + w, y);
        funnel.lineTo(x + w * 0.62, y + h * 0.5);
        funnel.lineTo(x + w * 0.62, y + h);
        funnel.lineTo(x + w * 0.38, y + h * 0.82);
        funnel.lineTo(x + w * 0.38, y + h * 0.5);
        funnel.closeSubpath();
        p.setPen(QPen(icon_ink(GutterFilter), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(funnel);
    }
    // Say why the grid is empty, and how to undo it — a blank page under an active filter reads as
    // a lost library.
    if (view == View::Software && grid->model() != nullptr && grid->model()->rowCount() == 0) {
        QFont f = font();
        f.setPixelSize(21);
        p.setFont(f);
        p.setPen(DeckTheme::kTextDim);
        p.drawText(QRect(grid->x(), grid->y() + 60, grid->width(), 40), Qt::AlignHCenter,
                   filter_state.IsDefault()
                       ? tr("No software yet — add a game folder from the desktop app.")
                       : tr("No software matches the filter."));
    }

    // Sort arrows, below the funnel.
    {
        const qreal x = cx - 12, y = top + 56, w = 24, h = 24;
        draw_focus(GutterSort, y + h / 2);
        p.setPen(QPen(icon_ink(GutterSort), 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawLine(QPointF(x + 5, y + h), QPointF(x + 5, y));
        p.drawLine(QPointF(x + 5, y), QPointF(x + 1, y + 4));
        p.drawLine(QPointF(x + 5, y), QPointF(x + 9, y + 4));
        p.drawLine(QPointF(x + w - 5, y), QPointF(x + w - 5, y + h));
        p.drawLine(QPointF(x + w - 5, y + h), QPointF(x + w - 9, y + h - 4));
        p.drawLine(QPointF(x + w - 5, y + h), QPointF(x + w - 1, y + h - 4));
    }
}

void DeckAllSoftwarePage::ApplyTheme() {
    QPalette pal = palette();
    pal.setColor(QPalette::Window, DeckTheme::kBackground);
    setPalette(pal);
    if (name_pill != nullptr) {
        name_pill->update(); // custom-painted: it reads the theme colours live
    }
    if (header != nullptr) {
        header->update();
    }
    if (keyboard != nullptr) {
        keyboard->update();
    }
    if (menu != nullptr) {
        menu->update();
    }
}

int DeckAllSoftwarePage::Columns() const {
    const int cell = kCardW + 2 * DeckTheme::kGridCardMargin;
    return std::max(1, grid->viewport()->width() / cell);
}

u64 DeckAllSoftwarePage::CurrentProgramId() const {
    const QModelIndex idx = grid->currentIndex();
    return idx.isValid() ? ProgramIdOf(idx) : 0;
}

void DeckAllSoftwarePage::SelectProgram(u64 program_id) {
    QAbstractItemModel* const model = grid->model();
    const int rows = model->rowCount();
    if (rows == 0) {
        return;
    }
    if (program_id != 0) {
        for (int row = 0; row < rows; ++row) {
            const QModelIndex idx = model->index(row, 0);
            if (ProgramIdOf(idx) == program_id) {
                grid->setCurrentIndex(idx);
                grid->scrollTo(idx, QAbstractItemView::EnsureVisible);
                PositionNamePill();
                return;
            }
        }
    }
    grid->setCurrentIndex(model->index(0, 0));
    grid->scrollToTop();
    PositionNamePill();
}

void DeckAllSoftwarePage::PositionNamePill() {
    const QModelIndex idx = grid->currentIndex();
    const bool is_game = idx.isValid() && !idx.data(DeckGroupRole).isValid() &&
                         !idx.data(DeckNewGroupRole).toBool();
    if (!is_game || keyboard->isVisible() || MenuOpen() || zone == Zone::Gutter) {
        name_pill->setVisible(false);
        return;
    }
    QString name = idx.data(GameListItemPath::TitleRole).toString();
    if (name.isEmpty()) {
        name = idx.data(Qt::DisplayRole).toString();
    }
    name_pill->SetText(name, width() - 16);
    // Map the tile rect (viewport coords) into the page so the bubble's tail meets the bottom edge
    // of the focused art. The focused tile insets by (margin - grow), so that edge is that far in
    // from the edge of the cell.
    const QRect vr = grid->visualRect(idx);
    const QPoint tl = grid->viewport()->mapTo(this, vr.topLeft());
    const int inset = DeckTheme::kGridCardMargin - DeckTheme::kFocusGrow;
    int x = tl.x() + vr.width() / 2 - name_pill->width() / 2;
    x = std::max(8, std::min(x, std::max(8, width() - name_pill->width() - 8)));

    // Below the tile normally; above it for the bottom row, where the page ends before the bubble
    // would. Without the flip the last row's names were clipped in half by the page edge.
    const int below = tl.y() + vr.height() - inset + 2;
    const bool fits_below = below + NamePill::Height() <= height() - 4;
    name_pill->SetTailUp(fits_below);
    name_pill->move(x, fits_below ? below : tl.y() + inset - NamePill::Height() - 2);
    name_pill->setVisible(true);
    name_pill->raise();
}

void DeckAllSoftwarePage::RefreshGroups() {
    groups_model->Refresh();
    DeckFilterState group_only;
    group_only.group = current_group;
    group_filter->SetFilter(group_only);
    group_filter->Reapply();
    static_cast<MemberProxy*>(member_proxy)->SetGroup(&groups, current_group);
    groups.Save();
}

void DeckAllSoftwarePage::LoadPreferences() {
    QSettings settings(QStringLiteral("Eden"), QStringLiteral("deck"));
    const int key = settings.value(QStringLiteral("all_software/sort_key"), 0).toInt();
    sort_key = kSortOrder[std::clamp(key, 0, static_cast<int>(std::size(kSortOrder)) - 1)];
    filter_state.favorites_only =
        settings.value(QStringLiteral("all_software/favorites_only"), false).toBool();
    filter_state.played = static_cast<DeckFilterState::Played>(
        settings.value(QStringLiteral("all_software/played"), 0).toInt());
    filter_state.group = settings.value(QStringLiteral("all_software/group"), -1).toInt();
    filter_state.file_type = settings.value(QStringLiteral("all_software/file_type")).toString();
    if (filter_state.group >= static_cast<int>(groups.Count())) {
        filter_state.group = -1; // the group was deleted since we last ran
    }
}

void DeckAllSoftwarePage::SavePreferences() const {
    QSettings settings(QStringLiteral("Eden"), QStringLiteral("deck"));
    const auto* const it = std::find(std::begin(kSortOrder), std::end(kSortOrder), sort_key);
    settings.setValue(QStringLiteral("all_software/sort_key"),
                      static_cast<int>(std::distance(std::begin(kSortOrder), it)));
    settings.setValue(QStringLiteral("all_software/favorites_only"), filter_state.favorites_only);
    settings.setValue(QStringLiteral("all_software/played"), static_cast<int>(filter_state.played));
    settings.setValue(QStringLiteral("all_software/group"), filter_state.group);
    settings.setValue(QStringLiteral("all_software/file_type"), filter_state.file_type);
}

void DeckAllSoftwarePage::ApplyOrdering() {
    const u64 keep = CurrentProgramId();
    sorted->SetKey(sort_key);
    sorted->SetFilter(filter_state);
    sorted->Reapply();
    group_filter->SetKey(sort_key);
    group_filter->Reapply();
    // Re-sorting drops the view's current index, so put the cursor back where it was.
    SelectProgram(keep);
    update(); // the gutter icons show whether a filter/sort is in effect
}

void DeckAllSoftwarePage::Resort() {
    ApplyOrdering();
}

void DeckAllSoftwarePage::RestoreOnReturn() {
    // Called by the shell immediately before it shows this page again, so the flag is always
    // consumed by the very next activation. Setting it when the options page was OPENED instead
    // left it armed if the user played the game from there, and a later cold entry from the home
    // screen then re-opened inside a stale group folder.
    restore_pid = CurrentProgramId();
    restore_view = view;
    restore_group = current_group;
    restore_pending = true;
}

void DeckAllSoftwarePage::SetZone(Zone z) {
    if (view != View::Software && z == Zone::Gutter) {
        return; // the filter/sort column only applies to the full library
    }
    zone = z;
    PositionNamePill();
    update();
    emit HintsChanged();
}

void DeckAllSoftwarePage::SetView(View v) {
    view = v;
    confirming_delete = false;
    zone = Zone::Grid;
    switch (v) {
    case View::Software:
        grid->setModel(sorted);
        header->ShowTabs(false);
        header->SetShowControls(true);
        break;
    case View::Groups:
        groups_model->Refresh();
        grid->setModel(groups_model);
        header->ShowTabs(true);
        header->SetShowControls(false);
        break;
    case View::GroupDetail: {
        DeckFilterState group_only;
        group_only.group = current_group;
        group_filter->SetFilter(group_only);
        group_filter->Reapply();
        grid->setModel(group_filter);
        header->ShowTitle(current_group >= 0 && current_group < static_cast<int>(groups.Count())
                              ? groups.Groups()[current_group].name
                              : tr("Group"));
        header->SetShowControls(true);
        break;
    }
    case View::AddGames:
        static_cast<MemberProxy*>(member_proxy)->SetGroup(&groups, current_group);
        grid->setModel(member_proxy);
        header->ShowTitle(tr("Select Software to Add"));
        header->SetShowControls(true);
        break;
    }
    if (grid->model()->rowCount() > 0) {
        grid->setCurrentIndex(grid->model()->index(0, 0));
        grid->scrollToTop();
    } else if (v == View::Software) {
        // Nothing to select — start on the filter icon, which is the only thing worth pressing when
        // the grid is empty (and the way back if a filter is what emptied it).
        zone = Zone::Gutter;
        gutter_item = GutterFilter;
    }
    UpdateHeader();
    PositionNamePill();
    update(); // repaint the left filter/sort column for the new view
    emit HintsChanged();
}

void DeckAllSoftwarePage::UpdateHeader() {
    QString sort_text = DeckSortKeyName(sort_key);
    if (!filter_state.IsDefault()) {
        sort_text = tr("%1  ·  Filtered").arg(sort_text);
    }
    header->SetSort(sort_text);
    if (confirming_delete) {
        header->ShowTitle(tr("Delete this group?  - : delete   B: cancel"));
    }
    PositionNamePill();
}

void DeckAllSoftwarePage::OpenSortMenu() {
    std::vector<DeckOptionMenu::Item> items;
    for (int i = 0; i < static_cast<int>(std::size(kSortOrder)); ++i) {
        items.push_back({DeckSortKeyName(kSortOrder[i]), i, kSortOrder[i] == sort_key, false});
    }
    const auto* const it = std::find(std::begin(kSortOrder), std::end(kSortOrder), sort_key);
    sort_menu_open = true;
    delegate->SetRailActive(false);
    name_pill->setVisible(false);
    menu->Open(tr("Sort"), DeckOptionMenu::Mode::Radio, std::move(items),
               static_cast<int>(std::distance(std::begin(kSortOrder), it)));
    emit HintsChanged();
}

void DeckAllSoftwarePage::OpenFilterMenu() {
    // Snapshot the group and format lists the ids are built from, and resolve toggles against this
    // snapshot rather than re-deriving it later: a background scan finishing (or a delete) while the
    // menu is open would otherwise renumber the rows under the cursor.
    menu_groups.clear();
    for (std::size_t i = 0; i < groups.Count(); ++i) {
        menu_groups.push_back(groups.Groups()[i].name);
    }
    menu_types = DeckFileTypes(base);
    // A format that is filtering right now must stay in the list even if it is the only one left —
    // otherwise deleting the last game of another format removes the only row that could switch the
    // filter off, and the library reads as empty forever (the setting is persisted).
    if (!filter_state.file_type.isEmpty() && !menu_types.contains(filter_state.file_type)) {
        menu_types.push_back(filter_state.file_type);
        menu_types.sort();
    }

    std::vector<DeckOptionMenu::Item> items;
    items.push_back({tr("Favorites only"), FilterFavorites, filter_state.favorites_only, false});
    items.push_back({tr("Never played"), FilterUnplayed,
                     filter_state.played == DeckFilterState::Played::Never, false});
    items.push_back({tr("Played"), FilterPlayed,
                     filter_state.played == DeckFilterState::Played::Played, false});
    if (!menu_groups.empty()) {
        items.push_back({tr("Groups"), -1, false, true});
        for (int i = 0; i < menu_groups.size(); ++i) {
            items.push_back({menu_groups.at(i), FilterGroupBase + i, filter_state.group == i,
                             false});
        }
    }
    if (menu_types.size() > 1 || !filter_state.file_type.isEmpty()) {
        items.push_back({tr("Format"), -1, false, true});
        for (int i = 0; i < menu_types.size(); ++i) {
            items.push_back(
                {menu_types.at(i), FilterTypeBase + i,
                 filter_state.file_type.compare(menu_types.at(i), Qt::CaseInsensitive) == 0,
                 false});
        }
    }
    sort_menu_open = false;
    delegate->SetRailActive(false);
    name_pill->setVisible(false);
    menu->Open(tr("Filter"), DeckOptionMenu::Mode::Check, std::move(items), FilterFavorites);
    emit HintsChanged();
}

void DeckAllSoftwarePage::StartCreateGroup() {
    renaming = false;
    name_pill->setVisible(false);
    keyboard->Start(tr("Name the group"), QString());
    emit HintsChanged();
}

void DeckAllSoftwarePage::StartRenameGroup() {
    if (current_group < 0 || current_group >= static_cast<int>(groups.Count())) {
        return;
    }
    renaming = true;
    name_pill->setVisible(false);
    keyboard->Start(tr("Rename group"), groups.Groups()[current_group].name);
    emit HintsChanged();
}

void DeckAllSoftwarePage::DeleteCurrentGroup() {
    if (current_group < 0 || current_group >= static_cast<int>(groups.Count())) {
        return;
    }
    const int deleted = current_group;
    groups.DeleteGroup(deleted);
    // The filter holds a position in the group vector, and deleting shifts everything after it
    // down. Left alone, filtering on "B" and deleting "A" silently switches the filter to "C" —
    // and once the index runs off the end nothing matches and no menu row can undo it.
    if (filter_state.group == deleted) {
        filter_state.group = -1;
    } else if (filter_state.group > deleted) {
        --filter_state.group;
    }
    if (filter_state.group >= static_cast<int>(groups.Count())) {
        filter_state.group = -1;
    }
    SavePreferences();
    ApplyOrdering();
    current_group = -1;
    RefreshGroups();
    SetView(View::Groups);
}

void DeckAllSoftwarePage::ToggleCurrentMembership() {
    const u64 pid = CurrentProgramId();
    if (pid == 0 || current_group < 0) {
        return;
    }
    groups.SetMembership(current_group, pid, !groups.IsMember(current_group, pid));
    static_cast<MemberProxy*>(member_proxy)->SetGroup(&groups, current_group);
    grid->viewport()->update();
}

void DeckAllSoftwarePage::OnActivated() {
    shimmer->start();
    keyboard->hide();
    if (menu->isVisible()) {
        menu->Close();
    }
    if (restore_pending) {
        // Coming back from a game's options page: stay on the same tab, group and tile. A cold
        // entry from the home screen still lands on the Software tab at the top, as it should.
        restore_pending = false;
        current_group = restore_group;
        ApplyOrdering();
        SetView(restore_view);
        SelectProgram(restore_pid);
        return;
    }
    ApplyOrdering();
    SetView(View::Software);
}

bool DeckAllSoftwarePage::OnNavigate(Qt::Key key) {
    if (keyboard->isVisible()) {
        switch (key) {
        case Qt::Key_Left:
            keyboard->MoveCursor(0, -1);
            break;
        case Qt::Key_Right:
            keyboard->MoveCursor(0, 1);
            break;
        case Qt::Key_Up:
            keyboard->MoveCursor(-1, 0);
            break;
        case Qt::Key_Down:
            keyboard->MoveCursor(1, 0);
            break;
        default:
            break;
        }
        return true;
    }
    if (MenuOpen()) {
        if (key == Qt::Key_Up) {
            menu->MoveCursor(-1);
        } else if (key == Qt::Key_Down) {
            menu->MoveCursor(1);
        } else if (key == Qt::Key_Left) {
            menu->Close();
        }
        return true;
    }
    if (zone == Zone::Gutter) {
        // The filter/sort column: Up/Down pick an icon, Right steps back into the games.
        if (key == Qt::Key_Up) {
            gutter_item = std::max(0, gutter_item - 1);
            update();
        } else if (key == Qt::Key_Down) {
            gutter_item = std::min(static_cast<int>(GutterCount) - 1, gutter_item + 1);
            update();
        } else if (key == Qt::Key_Right) {
            SetZone(Zone::Grid);
        }
        return true;
    }

    const int n = grid->model()->rowCount();
    const int cur = grid->currentIndex().isValid() ? grid->currentIndex().row() : 0;
    const int cols = Columns();
    // Left reaches the filter/sort column even with nothing in the grid — otherwise a filter that
    // matches no games hides the only control that could switch it off.
    if (key == Qt::Key_Left && view == View::Software && (n == 0 || cur % cols == 0)) {
        SetZone(Zone::Gutter);
        return true;
    }
    if (n == 0) {
        return true;
    }
    int next = cur;
    switch (key) {
    case Qt::Key_Left:
        next = cur - 1;
        break;
    case Qt::Key_Right:
        next = cur + 1;
        break;
    case Qt::Key_Up:
        next = cur - cols;
        break;
    case Qt::Key_Down:
        next = cur + cols;
        break;
    default:
        return true;
    }
    if (next < 0 || next >= n) {
        return true;
    }
    const QModelIndex idx = grid->model()->index(next, 0);
    grid->setCurrentIndex(idx);
    grid->scrollTo(idx, QAbstractItemView::EnsureVisible);
    PositionNamePill();
    return true;
}

bool DeckAllSoftwarePage::OnAccept() {
    if (keyboard->isVisible()) {
        keyboard->PressKey();
        return true;
    }
    if (MenuOpen()) {
        menu->Activate();
        return true;
    }
    if (zone == Zone::Gutter) {
        if (gutter_item == GutterFilter) {
            OpenFilterMenu();
        } else {
            OpenSortMenu();
        }
        return true;
    }
    if (confirming_delete) {
        DeleteCurrentGroup();
        return true;
    }
    const QModelIndex idx = grid->currentIndex();
    if (!idx.isValid()) {
        return true;
    }
    if (view == View::Groups) {
        if (idx.data(DeckNewGroupRole).toBool()) {
            StartCreateGroup();
        } else if (idx.data(DeckGroupRole).isValid()) {
            current_group = idx.data(DeckGroupRole).toInt();
            SetView(View::GroupDetail);
        }
        return true;
    }
    if (view == View::AddGames) {
        ToggleCurrentMembership();
        return true;
    }
    if (IsGameRow(idx)) { // Software or GroupDetail: launch
        const QString path = idx.data(GameListItemPath::FullPathRole).toString();
        const u64 program_id = ProgramIdOf(idx);
        if (!path.isEmpty()) {
            emit GamePlayRequested(path, program_id);
        }
    }
    return true;
}

bool DeckAllSoftwarePage::OnBack() {
    if (keyboard->isVisible()) {
        keyboard->Backspace();
        return true;
    }
    if (MenuOpen()) {
        menu->Close();
        return true;
    }
    if (zone == Zone::Gutter) {
        SetZone(Zone::Grid);
        return true;
    }
    if (confirming_delete) {
        confirming_delete = false;
        SetView(view); // redraw header without the confirm banner
        return true;
    }
    switch (view) {
    case View::AddGames:
        SetView(View::GroupDetail);
        return true;
    case View::GroupDetail:
        SetView(View::Groups);
        return true;
    case View::Groups:
        SetView(View::Software);
        return true;
    case View::Software:
    default:
        shimmer->stop();
        name_pill->setVisible(false);
        return false; // leave to the home menu
    }
}

bool DeckAllSoftwarePage::OnPrimaryAction() { // X
    if (keyboard->isVisible() || MenuOpen()) {
        return true;
    }
    if (view == View::GroupDetail) {
        SetView(View::AddGames);
    } else if (view == View::AddGames) {
        SetView(View::GroupDetail);
    }
    return true;
}

bool DeckAllSoftwarePage::OnSecondaryAction() { // Y
    if (keyboard->isVisible()) {
        keyboard->Cancel();
        return true;
    }
    if (MenuOpen()) {
        menu->Close();
        return true;
    }
    if (view == View::GroupDetail) {
        StartRenameGroup();
    }
    return true;
}

bool DeckAllSoftwarePage::OnStart() { // +
    if (keyboard->isVisible()) {
        keyboard->Accept();
        return true;
    }
    if (MenuOpen() || zone == Zone::Gutter) {
        return true;
    }
    // The console's "＋ Options": everything about the selected game — play time, when it was added,
    // size, and the destructive actions. Reachable from here as well as the home rail, so you never
    // have to walk back out of the library to delete something you just found in it.
    if (view == View::Software || view == View::GroupDetail) {
        const DeckGameInfo info = DeckGameInfo::FromIndex(grid->currentIndex(), stats);
        if (!info.path.isEmpty()) {
            emit GameOptionsRequested(info);
        }
    }
    return true;
}

bool DeckAllSoftwarePage::OnSelect() { // -
    if (keyboard->isVisible() || MenuOpen()) {
        return true;
    }
    if (view == View::GroupDetail) {
        confirming_delete = true;
        UpdateHeader();
        emit HintsChanged();
    }
    return true;
}

// L / R switch the Software <-> Groups tabs, like the Switch.
bool DeckAllSoftwarePage::OnPageUp() {
    if (!keyboard->isVisible() && !MenuOpen() &&
        (view == View::Software || view == View::Groups)) {
        SetView(View::Software);
    }
    return true;
}
bool DeckAllSoftwarePage::OnPageDown() {
    if (!keyboard->isVisible() && !MenuOpen() &&
        (view == View::Software || view == View::Groups)) {
        SetView(View::Groups);
    }
    return true;
}

std::vector<DeckHint> DeckAllSoftwarePage::Hints() const {
    if (keyboard->isVisible()) {
        return {{QStringLiteral("A"), tr("Type")},
                {QStringLiteral("B"), tr("Backspace")},
                {QStringLiteral("+"), tr("Done")},
                {QStringLiteral("Y"), tr("Cancel")}};
    }
    if (MenuOpen()) {
        return {{QStringLiteral("A"), sort_menu_open ? tr("Select") : tr("Toggle")},
                {QStringLiteral("B"), tr("Close")}};
    }
    if (confirming_delete) {
        return {{QStringLiteral("-"), tr("Delete")}, {QStringLiteral("B"), tr("Cancel")}};
    }
    if (zone == Zone::Gutter) {
        return {{QStringLiteral("A"), gutter_item == GutterFilter ? tr("Filter") : tr("Sort")},
                {QStringLiteral("B"), tr("Back to games")}};
    }
    if (view == View::AddGames) {
        return {{QStringLiteral("A"), tr("Toggle")},
                {QStringLiteral("X"), tr("Done")},
                {QStringLiteral("B"), tr("Back")}};
    }
    if (view == View::GroupDetail) {
        return {{QStringLiteral("A"), tr("Play")},
                {QStringLiteral("+"), tr("Options")},
                {QStringLiteral("X"), tr("Add games")},
                {QStringLiteral("Y"), tr("Rename")},
                {QStringLiteral("-"), tr("Delete group")},
                {QStringLiteral("B"), tr("Back")}};
    }
    if (view == View::Groups) {
        return {{QStringLiteral("A"), tr("Open")},
                {QStringLiteral("L/R"), tr("Software / Groups")},
                {QStringLiteral("B"), tr("Back")}};
    }
    return {{QStringLiteral("A"), tr("OK")},
            {QStringLiteral("+"), tr("Options")},
            {QStringLiteral("L/R"), tr("Software / Groups")},
            {QStringLiteral("B"), tr("Back")}};
}
