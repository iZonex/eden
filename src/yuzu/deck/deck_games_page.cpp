// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <functional>
#include <QAbstractListModel>
#include <QConcatenateTablesProxyModel>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScroller>
#include <QSortFilterProxyModel>
#include <QFile>
#include <QTime>
#include <QTransform>
#include <QTimer>
#include <QVBoxLayout>

#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/deck/deck_game_delegate.h"
#include "yuzu/deck/deck_games_page.h"
#include "yuzu/deck/deck_theme.h"

#include <fmt/format.h>
#include <QConicalGradient>
#include <QItemSelectionModel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/string_util.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"
#include "frontend_common/play_time_manager.h"

namespace {
/// A round profile picture for a specific user. Falls back to the default avatar; returns a null
/// pixmap only if nothing loads.
QPixmap UserAvatar(const Common::UUID& uuid, int size) {
    const auto path =
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
        fmt::format("system/save/8000000000000010/su/avators/{}.jpg", uuid.FormattedString());
    QPixmap src{QString::fromStdString(Common::FS::PathToUTF8String(path))};
    if (src.isNull()) {
        src.loadFromData(Core::Constants::ACCOUNT_BACKUP_JPEG.data(),
                         static_cast<unsigned int>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
    }
    if (src.isNull()) {
        return {};
    }
    const int px = size * 2; // render at 2x for HiDPI crispness
    QPixmap round(px, px);
    round.fill(Qt::transparent);
    QPainter p(&round);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath clip;
    clip.addEllipse(0, 0, px, px);
    p.setClipPath(clip);
    const QPixmap scaled =
        src.scaled(px, px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    p.drawPixmap((px - scaled.width()) / 2, (px - scaled.height()) / 2, scaled);
    p.end();
    round.setDevicePixelRatio(2.0);
    return round;
}


/// The active user's display name (falls back to "User").
QString ActiveUserName(const Service::Account::ProfileManager& manager, const Common::UUID& uuid) {
    Service::Account::ProfileBase profile{};
    if (!uuid.IsValid() || !manager.GetProfileBase(uuid, profile)) {
        return QObject::tr("User");
    }
    const auto text = Common::StringFromFixedZeroTerminatedBuffer(
        reinterpret_cast<const char*>(profile.username.data()), profile.username.size());
    const QString name = QString::fromStdString(text).trimmed();
    return name.isEmpty() ? QObject::tr("User") : name;
}

// The Switch home shows a short row of the most-recent titles, not the whole library — the rest live
// behind All Software. This caps the home rail; All Software (grid mode) lifts the cap to show every
// game.
constexpr int kHomeRailRecent = 12;

// Caps its (already recency-sorted) source to the first N rows for the home rail; SetLimit(-1) shows
// everything (used by the All Software grid).
class HeadProxy : public QSortFilterProxyModel {
public:
    explicit HeadProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    void SetLimit(int n) {
        if (limit != n) {
            limit = n;
            invalidateFilter();
        }
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex&) const override {
        return limit < 0 || row < limit;
    }

private:
    int limit = -1;
};

// One-row model marking the Nintendo-style round "All Software" button. Concatenated after the
// library filter (see the rail setup) so it is always the last cell of the game row; the delegate
// paints the round button from the DeckAllSoftwareRole marker (no pixmap here, so it stays crisp and
// follows the theme). In grid mode ("See all") the row hides itself so the full library grid shows
// games only, like the Switch.
class AllSoftwareModel : public QAbstractListModel {
public:
    explicit AllSoftwareModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override {
        return (parent.isValid() || hidden) ? 0 : 1;
    }
    // Match the game-list model's column count so QConcatenateTablesProxyModel lines the two up.
    int columnCount(const QModelIndex& parent = {}) const override {
        return parent.isValid() ? 0 : GameListModel::COLUMN_COUNT;
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() != 0 || index.column() != 0) {
            return {};
        }
        switch (role) {
        case DeckAllSoftwareRole:
            return true;
        case Qt::DisplayRole:
            return QAbstractListModel::tr("All Software");
        default:
            return {};
        }
    }
    void SetHidden(bool h) {
        if (hidden == h) {
            return;
        }
        beginResetModel();
        hidden = h;
        endResetModel();
    }

private:
    bool hidden = false;
};

/// The Deck's battery charge as a "NN%" string for the Switch-style status cluster, or empty when no
/// battery is present (e.g. desktop testing). Read straight from sysfs so it needs no extra deps.
QString ReadBatteryText() {
    for (const auto* name : {"BAT0", "BAT1", "BAT2"}) {
        QFile f(QStringLiteral("/sys/class/power_supply/%1/capacity").arg(QLatin1String(name)));
        if (f.open(QIODevice::ReadOnly)) {
            bool ok = false;
            const int pct = QString::fromUtf8(f.readAll()).trimmed().toInt(&ok);
            if (ok && pct > 0) {
                return QStringLiteral("%1%").arg(pct);
            }
        }
    }
    return {};
}

/// Shows only real games in the console library: Game-type rows that actually have box art. This
/// hides the system content that scanning SysNAND/UserNAND surfaces (firmware, applets) — entries
/// with no icon that would otherwise appear as blank, textless tiles.
class LibraryFilter : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;

    void SetPlayTime(const PlayTime::PlayTimeManager* pt) {
        play_time = pt;
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        const QModelIndex idx = sourceModel()->index(row, 0, parent);
        if (idx.data(GameListItem::TypeRole).toInt() != static_cast<int>(GameListItemType::Game)) {
            return false;
        }
        const auto pid = idx.data(GameListItemPath::ProgramIdRole).toULongLong();
        if (const auto it = has_art.find(pid); it != has_art.end()) {
            return it.value();
        }
        bool art = false;
        const QPixmap px = idx.data(Qt::DecorationRole).value<QPixmap>();
        if (!px.isNull()) {
            const QImage img = px.toImage();
            const int w = img.width();
            const int h = img.height();
            static const double pts[][2] = {{0.5, 0.5}, {0.3, 0.3}, {0.7, 0.7}, {0.5, 0.85}};
            for (const auto& pt : pts) {
                if (w > 0 && h > 0 &&
                    img.pixelColor(static_cast<int>(w * pt[0]), static_cast<int>(h * pt[1]))
                            .alpha() > 20) {
                    art = true;
                    break;
                }
            }
        }
        has_art.insert(pid, art);
        return art;
    }

    // Recently/most-played first: games you've actually opened (play time > 0) sort to the front,
    // ordered by play time, then everything you haven't opened follows by title — so the first few
    // tiles are the games you play and the rest are one scroll away. (Replaces the old favourites.)
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        const auto lpid = left.data(GameListItemPath::ProgramIdRole).toULongLong();
        const auto rpid = right.data(GameListItemPath::ProgramIdRole).toULongLong();
        const u64 lt = play_time != nullptr ? play_time->GetPlayTime(lpid) : 0;
        const u64 rt = play_time != nullptr ? play_time->GetPlayTime(rpid) : 0;
        if ((lt > 0) != (rt > 0)) {
            return lt > 0; // played games first
        }
        if (lt != rt) {
            return lt > rt; // more play time first
        }
        const QString lname = left.data(GameListItemPath::TitleRole).toString();
        const QString rname = right.data(GameListItemPath::TitleRole).toString();
        return lname.localeAwareCompare(rname) < 0;
    }

private:
    mutable QHash<quint64, bool> has_art;
    const PlayTime::PlayTimeManager* play_time = nullptr;
};
} // namespace

/// The bottom system dock: a centered white pill holding colored, rounded-square app icons (SVG),
/// with a ring + label on the focused one. Plain QWidget (no signals) so it needs no MOC; it calls
/// back into the page on a tap.
class DockBar : public QWidget {
public:
    // Order matches the Switch HOME dock (applicable subset): Album, Controllers, System Settings,
    // Sleep, then our own Power/Exit. No Users item — the avatar opens the Users page.
    enum { kAlbum = 0, kControllers = 1, kSettings = 2, kSleep = 3, kPower = 4, kCount = 5 };

    explicit DockBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(168); // room for the focused item's tooltip below the pill
        names[kAlbum] = QStringLiteral("album");
        names[kControllers] = QStringLiteral("controllers");
        names[kSettings] = QStringLiteral("settings");
        names[kSleep] = QStringLiteral("sleep");
        names[kPower] = QStringLiteral("power");
    }

    void SetCurrent(int index) {
        if (current != index) {
            current = index;
            update();
        }
    }
    void SetActive(bool active) {
        if (zone_active != active) {
            zone_active = active;
            update();
        }
    }
    int Current() const {
        return current;
    }
    void SetPhase(int p) {
        phase = p;
    }

    /// Called with the tapped icon index (touch or mouse).
    std::function<void(int)> on_tapped;

protected:
    static constexpr int kSq = 72;      // icon cell
    static constexpr int kGap = 34;     // gap between icons
    static constexpr int kPad = 16;     // pill inner padding
    static constexpr int kIconPx = 54;  // grey glyph size

    qreal PillWidth() const {
        return kCount * kSq + (kCount - 1) * kGap + 2 * kPad;
    }
    QRectF PillRect() const {
        const qreal w = PillWidth();
        const qreal h = kSq + 2 * kPad;
        return {(width() - w) / 2.0, 8, w, h};
    }
    QRectF IconRect(int i) const {
        const QRectF pill = PillRect();
        const qreal x = pill.left() + kPad + i * (kSq + kGap);
        return {x, pill.top() + kPad, static_cast<qreal>(kSq), static_cast<qreal>(kSq)};
    }

    void mousePressEvent(QMouseEvent* event) override {
        for (int i = 0; i < kCount; ++i) {
            if (IconRect(i).adjusted(-8, -8, 8, 8).contains(event->position())) {
                SetCurrent(i);
                SetActive(true);
                if (on_tapped) {
                    on_tapped(i);
                }
                return;
            }
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.fillRect(rect(), DeckTheme::kBackground);

        const QRectF pill = PillRect();
        // Flat white pill, no shadow (the Switch dock sits flat on the page).
        QPainterPath pill_path;
        pill_path.addRoundedRect(pill, pill.height() / 2, pill.height() / 2);
        p.fillPath(pill_path, DeckTheme::kSurface);

        const std::array<QString, kCount> labels{QObject::tr("Album"), QObject::tr("Controllers"),
                                                 QObject::tr("Settings"), QObject::tr("Sleep"),
                                                 QObject::tr("Power")};
        // The Switch dock colours app icons and greys the system ones. Album is our lone "app"; the
        // rest (Controllers/Settings/Sleep/Power) are system icons, drawn grey.
        const std::array<QColor, kCount> tints{QColor(0x3a, 0x9b, 0xd8), DeckTheme::kTextDim,
                                               DeckTheme::kTextDim, DeckTheme::kTextDim,
                                               DeckTheme::kTextDim};
        int focused = -1;
        for (int i = 0; i < kCount; ++i) {
            const bool sel = (i == current) && zone_active;
            const QRectF sq = IconRect(i);
            if (sel) {
                focused = i;
                // Subtle round highlight behind the focused icon (the Switch's soft selection).
                const QRectF ring = sq.adjusted(-3, -3, 3, 3);
                QConicalGradient cg(ring.center(), static_cast<qreal>(phase));
                cg.setColorAt(0.00, QColor(0x4f, 0x86, 0xff));
                cg.setColorAt(0.30, QColor(0xa9, 0x5c, 0xf0));
                cg.setColorAt(0.55, QColor(0xff, 0x6b, 0xb0));
                cg.setColorAt(0.80, QColor(0x37, 0xd0, 0xf0));
                cg.setColorAt(1.00, QColor(0x4f, 0x86, 0xff));
                p.setPen(QPen(QBrush(cg), 4));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(ring);
            }
            // Icon glyph — coloured for the app, grey for system items (no per-icon label; the Switch
            // shows only the focused item's name).
            const QPixmap glyph = DeckTheme::Icon(names[i], kIconPx, tints[i]);
            p.drawPixmap(QPointF(sq.center().x() - kIconPx / 2.0, sq.center().y() - kIconPx / 2.0),
                         glyph);
        }

        // Focused item's name in a rounded pill below its icon (the Switch's dock tooltip).
        if (focused >= 0) {
            const QRectF sq = IconRect(focused);
            QFont f = font();
            f.setPixelSize(19);
            p.setFont(f);
            const QString text = labels[focused];
            const int tw = QFontMetrics(f).horizontalAdvance(text) + 28;
            const QRectF tip(sq.center().x() - tw / 2.0, pill.bottom() + 10, tw, 32);
            QPainterPath tip_path;
            tip_path.addRoundedRect(tip, 10, 10);
            p.fillPath(tip_path, DeckTheme::kSurface);
            p.setPen(DeckTheme::IsLightMode() ? QColor(0x2f, 0x6c, 0xb5) : QColor(0x6a, 0xb4, 0xff));
            p.drawText(tip, Qt::AlignCenter, text);
        }
    }

private:
    int current = 0;
    int phase = 0;
    bool zone_active = false;
    std::array<QString, kCount> names;
};

/// The Switch home's top-left "My Page": the active user's round avatar with "<Name>'s Page" beside
/// it (the full multi-user row lives on the Users page). Focusable — a shimmering ring when focused;
/// A opens the Users page. Plain QWidget (no MOC).
class AvatarBadge : public QWidget {
public:
    explicit AvatarBadge(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(kCell);
    }
    void SetAvatar(QPixmap a, const QString& user_name) {
        avatar = std::move(a);
        name = user_name;
        RefitWidth();
        update();
    }
    void SetFocused(bool f) {
        if (focused != f) {
            focused = f;
            update();
        }
    }

protected:
    static constexpr int kCell = 60; // avatar slot (52px face + ring room)
    static constexpr int kFace = 52;

    void RefitWidth() {
        QFont f = font();
        f.setPixelSize(22);
        const QString label = tr("%1's Page").arg(name);
        setFixedWidth(kCell + 12 + QFontMetrics(f).horizontalAdvance(label) + 8);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF slot(0, 0, kCell, kCell);
        const QRectF face((kCell - kFace) / 2.0, (kCell - kFace) / 2.0, kFace, kFace);
        if (!avatar.isNull()) {
            QPainterPath clip;
            clip.addEllipse(face);
            p.save();
            p.setClipPath(clip);
            p.drawPixmap(face.toRect(), avatar);
            p.restore();
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(DeckTheme::kSurface);
            p.drawEllipse(face);
        }
        if (focused) {
            QLinearGradient lg(slot.topLeft(), slot.bottomRight());
            lg.setColorAt(0.0, QColor(0x4f, 0x86, 0xff));
            lg.setColorAt(0.5, QColor(0xa9, 0x5c, 0xf0));
            lg.setColorAt(1.0, QColor(0xff, 0x6b, 0xb0));
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QBrush(lg), 3));
            p.drawEllipse(slot.adjusted(2, 2, -2, -2));
        }
        // "<Name>'s Page" label beside the avatar, in Switch blue.
        QFont f = font();
        f.setPixelSize(22);
        p.setFont(f);
        p.setPen(DeckTheme::IsLightMode() ? QColor(0x2f, 0x6c, 0xb5) : QColor(0x6a, 0xb4, 0xff));
        p.drawText(QRectF(kCell + 12, 0, width() - kCell - 12, kCell),
                   Qt::AlignVCenter | Qt::AlignLeft, tr("%1's Page").arg(name));
    }

private:
    QPixmap avatar;
    QString name;
    bool focused = false;
};

namespace {
/// The empty-library state: a row of blank rounded tiles with a hint. Plain QWidget, no MOC.
class PlaceholderRail : public QWidget {
public:
    explicit PlaceholderRail(QWidget* parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), DeckTheme::kBackground); // custom paint: draw our own dark ground
        constexpr int tiles = 5;
        const int size = 210;
        const int gap = DeckTheme::kGridCardSpacing;
        const int total = tiles * size + (tiles - 1) * gap;
        int x = (width() - total) / 2;
        const int y = height() / 2 - size / 2 - 16;
        for (int i = 0; i < tiles; ++i) {
            QRectF tile(x, y, size, size);
            QPainterPath path;
            path.addRoundedRect(tile, DeckTheme::kCornerRadius, DeckTheme::kCornerRadius);
            p.fillPath(path, DeckTheme::kPlaceholder);
            p.setPen(QPen(DeckTheme::kPlaceholderBorder, 1));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            x += size + gap;
        }
        QFont f = font();
        f.setPixelSize(21);
        p.setFont(f);
        p.setPen(DeckTheme::kTextDim);
        p.drawText(QRectF(0, y + size + 22, width(), 36), Qt::AlignHCenter,
                   QObject::tr("No games yet — add a game folder from the desktop app."));
    }
};
} // namespace

DeckGamesPage::DeckGamesPage(GameListModel* model_, Core::System& system_,
                             const PlayTime::PlayTimeManager& play_time_manager, QWidget* parent)
    : DeckPage(parent), system{system_}, model{model_} {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Top status strip, laid out like the Switch home: the active user's avatar on the LEFT, the
    // clock + battery cluster on the RIGHT, both on the same line.
    auto* topbar = new QWidget(this);
    topbar->setFixedHeight(74);
    auto* top_row = new QHBoxLayout(topbar);
    top_row->setContentsMargins(40, 0, 44, 0);
    avatar = new AvatarBadge(topbar); // active user's avatar (left); focusable to open Users
    top_row->addWidget(avatar, 0, Qt::AlignVCenter);
    top_row->addStretch();
    clock = new QLabel(topbar);
    clock->setStyleSheet(
        QStringLiteral("font-size:28px; font-weight: 500; color:%1;").arg(DeckTheme::kText.name()));
    top_row->addWidget(clock, 0, Qt::AlignVCenter);
    battery = new QLabel(topbar);
    battery->setStyleSheet(QStringLiteral("font-size:21px; font-weight: 500; color:%1; padding-left:16px;")
                               .arg(DeckTheme::kTextDim.name()));
    top_row->addWidget(battery, 0, Qt::AlignVCenter);
    outer->addWidget(topbar);

    outer->addStretch();

    // The selected game's name, shown above the rail in Switch blue (only the current selection).
    game_title = new QLabel(this);
    game_title->setStyleSheet(
        QStringLiteral("font-size:26px; font-weight: 500; color:%1; padding: 2px 0 10px 96px;")
            .arg(DeckTheme::IsLightMode() ? QStringLiteral("#2f6cb5") : QStringLiteral("#6ab4ff")));
    outer->addWidget(game_title);

    auto* library = new LibraryFilter(this);
    library->SetPlayTime(&play_time_manager);
    filter = library;
    filter->setSourceModel(model);
    filter->sort(0); // recently/most-played first, then the rest by title

    // Home shows only the most-recent titles (Switch-style); the full library is behind All Software.
    // head caps the recency-sorted filter to kHomeRailRecent on home, and lifts the cap in the grid.
    auto* head_proxy = new HeadProxy(this);
    head_proxy->setSourceModel(filter);
    head_proxy->SetLimit(kHomeRailRecent);
    head = head_proxy;

    // The rail shows the recent games (head) followed by a trailing round "All Software" tile that
    // opens the full-library grid — concatenated so it is always the last cell of the row.
    // QConcatenateTablesProxyModel preserves row order, so the mapping to head stays 1:1.
    auto* concat = new QConcatenateTablesProxyModel(this);
    concat->addSourceModel(head);
    all_software = new AllSoftwareModel(this);
    concat->addSourceModel(all_software);
    rail_model = concat;

    rail = new QListView(this);
    rail->setModel(rail_model);
    delegate = new DeckGameDelegate(rail);
    rail->setItemDelegate(delegate);
    rail->setViewMode(QListView::IconMode);
    rail->setFlow(QListView::LeftToRight);
    rail->setWrapping(false);
    rail->setResizeMode(QListView::Adjust);
    rail->setMovement(QListView::Static);
    // Not uniform: the first tile's cell is wider (the leading indent) — see the delegate.
    rail->setUniformItemSizes(false);
    rail->setSelectionMode(QAbstractItemView::SingleSelection);
    rail->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rail->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    rail->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Spacing + the leading indent live entirely in the cell sizes (see the delegate); no gridSize
    // (which would force uniform cells) and no viewport padding (which would clip the scroll before
    // the screen edge). The first tile is indented but scrolls out to the edge.
    rail->setSpacing(0);
    delegate->SetLeadIndent(DeckTheme::kGridLeadIndent);
    rail->setFixedHeight(DeckTheme::kGridCardHeight + 2 * DeckTheme::kGridCardMargin + 12);
    rail->setFrameShape(QFrame::NoFrame);
    // The shell drives navigation (gamepad/keyboard), so the rail never takes keyboard focus.
    rail->setFocusPolicy(Qt::NoFocus);
    rail->setContentsMargins(0, 0, 0, 0);
    rail->setStyleSheet(QStringLiteral("QListView { background: transparent; }"));
    // Kinetic scrolling via the left-mouse gesture (touch is synthesized to it), matching the
    // desktop game list. A plain tap still reaches the item so touch selection/launch works.
    QScroller::grabGesture(rail->viewport(), QScroller::LeftMouseButtonGesture);
    outer->addWidget(rail);

    placeholder = new PlaceholderRail(this);
    placeholder->setFixedHeight(DeckTheme::kGridCardHeight + 2 * DeckTheme::kGridCardMargin + 12);
    placeholder->setVisible(false);
    outer->addWidget(placeholder);

    // The system dock sits directly under the game rail (not pinned to the bottom), so the whole
    // games + dock group reads as one centered block like the Switch home screen.
    outer->addSpacing(6);
    dock = new DockBar(this);
    dock->on_tapped = [this](int) {
        zone = Zone::Dock;
        dock->SetActive(true);
        ActivateDock();
        emit HintsChanged();
    };
    outer->addWidget(dock);
    outer->addStretch();

    // Only `clicked` (a tap / single click) launches; gamepad and keyboard go through OnAccept.
    // Connecting `activated` too would double-fire on single-click-activation styles.
    connect(rail, &QAbstractItemView::clicked, this,
            [this](const QModelIndex&) { EmitCurrentGame(); });

    // Live clock + battery, like the Switch home status cluster.
    clock_timer = new QTimer(this);
    clock_timer->setInterval(10'000);
    const auto update_status = [this] {
        clock->setText(QTime::currentTime().toString(QStringLiteral("h:mm AP"))); // 2:22 PM, like the Switch
        const QString bat = ReadBatteryText();
        battery->setText(bat);
        battery->setVisible(!bat.isEmpty());
        auto& pm = system.GetProfileManager();
        active_uuid = pm.GetLastOpenedUser();
        avatar->SetAvatar(UserAvatar(active_uuid, 52), ActiveUserName(pm, active_uuid));
    };
    connect(clock_timer, &QTimer::timeout, this, update_status);
    update_status();

    // Selection-shimmer animation: advance the phase and repaint the selected tile + dock ring.
    shimmer_timer = new QTimer(this);
    shimmer_timer->setInterval(40); // ~25 fps
    connect(shimmer_timer, &QTimer::timeout, this, [this] {
        phase = (phase + 5) % 360;
        if (delegate != nullptr) {
            delegate->SetPhase(phase);
        }
        if (dock != nullptr) {
            dock->SetPhase(phase);
            dock->update();
        }
        // The avatar ring is static (only shown while selected), so it needs no per-frame repaint.
        if (rail != nullptr) {
            rail->viewport()->update();
        }
    });
    shimmer_timer->start();

    // Keep the game-name label in sync with the selected tile.
    connect(rail->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { UpdateGameTitle(); });
}

DeckGamesPage::~DeckGamesPage() = default;

void DeckGamesPage::ApplyTheme() {
    // Re-apply the status-strip label colours for the new theme (custom-painted widgets — tiles,
    // dock, avatar — read DeckTheme live and repaint on their own).
    clock->setStyleSheet(
        QStringLiteral("font-size:28px; font-weight: 500; color:%1;").arg(DeckTheme::kText.name()));
    battery->setStyleSheet(
        QStringLiteral("font-size:21px; font-weight: 500; color:%1; padding-left:16px;")
            .arg(DeckTheme::kTextDim.name()));
    game_title->setStyleSheet(
        QStringLiteral("font-size:26px; font-weight: 500; color:%1; padding: 2px 0 10px 96px;")
            .arg(DeckTheme::IsLightMode() ? QStringLiteral("#2f6cb5") : QStringLiteral("#6ab4ff")));
}

bool DeckGamesPage::IsEmpty() const {
    return filter == nullptr || filter->rowCount() == 0;
}

QAbstractItemModel* DeckGamesPage::LibraryModel() const {
    return filter; // the LibraryFilter: every game with art, recency-sorted (uncapped)
}

void DeckGamesPage::SetSuspendedGame(u64 program_id) {
    if (delegate != nullptr) {
        delegate->SetSuspendedProgramId(program_id);
    }
    if (rail != nullptr) {
        rail->viewport()->update();
    }
}

QModelIndex DeckGamesPage::CurrentGameIndex() const {
    QModelIndex index = rail->currentIndex();
    if (!index.isValid() && rail_model->rowCount() > 0) {
        index = rail_model->index(0, 0);
    }
    return index;
}

void DeckGamesPage::MoveRail(int delta) {
    const int n = rail_model->rowCount(); // games + the trailing All Software tile
    if (n == 0) {
        return;
    }
    const int cur = rail->currentIndex().isValid() ? rail->currentIndex().row() : 0;
    const QModelIndex idx = rail_model->index(qBound(0, cur + delta, n - 1), 0);
    rail->setCurrentIndex(idx);
    // Single-row rail: keep the selected tile centred so the list scrolls left as you move right,
    // always leaving partial tiles peeking on both edges (the Switch's "scrolling a list" feel). The
    // scroll clamps at the ends, so the first/last tiles don't drift into empty space. Grid mode just
    // keeps the item visible.
    rail->scrollTo(idx, grid_mode ? QAbstractItemView::EnsureVisible
                                  : QAbstractItemView::PositionAtCenter);
}

int DeckGamesPage::GridColumns() const {
    const int cell = DeckTheme::kGridCardWidth + DeckTheme::kGridCardSpacing;
    const int w = rail->viewport()->width();
    return std::max(1, w / cell);
}

void DeckGamesPage::SetGridMode(bool on) {
    if (grid_mode == on) {
        return;
    }
    grid_mode = on;
    // The full library grid shows every game (lift the recent-N cap) and only games — hide the round
    // All Software button there (it is the control that opened the grid), like the Switch.
    if (head != nullptr) {
        static_cast<HeadProxy*>(head)->SetLimit(on ? -1 : kHomeRailRecent);
    }
    if (all_software != nullptr) {
        static_cast<AllSoftwareModel*>(all_software)->SetHidden(on);
    }
    if (on) {
        // "See all": reflow the single-row rail into a full wrapping grid of every game, hiding the
        // dock so the whole area is the library. No leading indent here — a plain aligned grid.
        zone = Zone::Rail;
        delegate->SetLeadIndent(0);
        rail->setWrapping(true);
        rail->setMinimumHeight(0);
        rail->setMaximumHeight(QWIDGETSIZE_MAX);
        rail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        rail->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        dock->setVisible(false);
        // The All Software cell we came from is gone now; land on the first game.
        if (rail_model->rowCount() > 0) {
            rail->setCurrentIndex(rail_model->index(0, 0));
        }
    } else {
        delegate->SetLeadIndent(DeckTheme::kGridLeadIndent);
        rail->setWrapping(false);
        rail->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        rail->setFixedHeight(DeckTheme::kGridCardHeight + 2 * DeckTheme::kGridCardMargin + 12);
        rail->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        dock->setVisible(true);
    }
    rail->doItemsLayout(); // re-layout for the new cell sizes (lead indent changed)
    if (rail->currentIndex().isValid()) {
        rail->scrollTo(rail->currentIndex(), QAbstractItemView::PositionAtCenter);
    }
    emit HintsChanged();
}

void DeckGamesPage::SetZone(Zone new_zone) {
    // Never focus an empty rail.
    if (new_zone == Zone::Rail && IsEmpty()) {
        new_zone = Zone::Dock;
    }
    zone = new_zone;
    dock->SetActive(zone == Zone::Dock);
    avatar->SetFocused(zone == Zone::Avatar); // shimmering round ring, not a square border
    if (zone == Zone::Rail && !rail->currentIndex().isValid() && filter->rowCount() > 0) {
        rail->setCurrentIndex(rail_model->index(0, 0));
    }
    UpdateGameTitle();
    emit HintsChanged();
}

void DeckGamesPage::UpdateGameTitle() {
    if (game_title == nullptr) {
        return;
    }
    const QModelIndex idx = CurrentGameIndex();
    QString t;
    if (!IsEmpty() && idx.isValid()) {
        t = idx.data(GameListItemPath::TitleRole).toString();
        if (t.isEmpty()) {
            t = idx.data(Qt::DisplayRole).toString();
        }
    }
    game_title->setText(t);
}

void DeckGamesPage::OnActivated() {
    launched = false; // returned to the console; allow launching again
    SetGridMode(false); // always land on the home rail, not the "see all" grid
    clock_timer->start();
    const bool empty = IsEmpty();
    placeholder->setVisible(empty);
    rail->setVisible(!empty);
    SetZone(empty ? Zone::Dock : Zone::Rail);
}

bool DeckGamesPage::OnNavigate(Qt::Key key) {
    if (grid_mode) {
        // Full 2D grid navigation; the dock is hidden in this mode.
        switch (key) {
        case Qt::Key_Left:
            MoveRail(-1);
            break;
        case Qt::Key_Right:
            MoveRail(1);
            break;
        case Qt::Key_Up:
            MoveRail(-GridColumns());
            break;
        case Qt::Key_Down:
            MoveRail(GridColumns());
            break;
        default:
            break;
        }
        return true;
    }
    switch (key) {
    case Qt::Key_Down:
        if (zone == Zone::Avatar) {
            SetZone(IsEmpty() ? Zone::Dock : Zone::Rail);
        } else if (zone == Zone::Rail) {
            SetZone(Zone::Dock);
        }
        return true;
    case Qt::Key_Up:
        if (zone == Zone::Dock && !IsEmpty()) {
            SetZone(Zone::Rail);
        } else if (zone == Zone::Rail || (zone == Zone::Dock && IsEmpty())) {
            SetZone(Zone::Avatar); // reach the user avatar at the top-right
        }
        return true;
    case Qt::Key_Left:
        if (zone == Zone::Dock) {
            dock->SetCurrent(qMax(0, dock->Current() - 1));
        } else if (zone == Zone::Rail) {
            MoveRail(-1);
        } else if (zone == Zone::Avatar) {
            SetZone(Zone::Rail); // one avatar on home — sideways drops into the games
        }
        return true;
    case Qt::Key_Right:
        if (zone == Zone::Dock) {
            dock->SetCurrent(qMin(DockCount - 1, dock->Current() + 1));
        } else if (zone == Zone::Rail) {
            MoveRail(1);
        } else if (zone == Zone::Avatar) {
            SetZone(Zone::Rail);
        }
        return true;
    default:
        return false;
    }
}

bool DeckGamesPage::OnPrimaryAction() {
    // No "See all" grid — the Switch home has no such mode, and it disrupted the layout.
    return false;
}

bool DeckGamesPage::OnStart() {
    // + opens the selected game's options (Switch: ＋ Параметры).
    if (zone == Zone::Rail && !IsEmpty()) {
        EmitCurrentGame();
    }
    return true;
}

bool DeckGamesPage::OnAccept() {
    if (zone == Zone::Avatar) {
        emit OpenUsers(active_uuid); // A on the avatar opens the active user's My Page
    } else if (zone == Zone::Dock) {
        ActivateDock();
    } else if (rail->currentIndex().data(DeckAllSoftwareRole).toBool()) {
        emit OpenAllSoftware(); // A on the trailing All Software tile opens the full-library page
    } else {
        PlayCurrentGame(); // A boots the game straight away
    }
    return true;
}

void DeckGamesPage::ActivateDock() {
    switch (dock->Current()) {
    case DockBar::kAlbum:
        emit OpenAlbum();
        break;
    case DockBar::kControllers:
        emit OpenControllers();
        break;
    case DockBar::kSettings:
        emit OpenSettings();
        break;
    case DockBar::kSleep:
        emit SleepRequested();
        break;
    case DockBar::kPower:
        emit ExitRequested();
        break;
    default:
        break;
    }
}

bool DeckGamesPage::OnSecondaryAction() {
    // Favourites were removed in favour of recently-played sorting; Y does nothing on the home rail.
    return false;
}

bool DeckGamesPage::OnBack() {
    // In the "See all" grid, B returns to the home rail.
    if (grid_mode) {
        SetGridMode(false);
        return true;
    }
    // On a game tile, B opens its options page (the console standard: A plays, B shows options).
    // On the dock, B does nothing — leaving the console is the dock's Power item, so an accidental
    // Back never drops the user into the old desktop window.
    if (zone == Zone::Rail && !IsEmpty()) {
        EmitCurrentGame();
    }
    return true;
}

void DeckGamesPage::PlayCurrentGame() {
    if (launched) {
        return; // a boot is already in flight; don't double-launch
    }
    const QModelIndex index = CurrentGameIndex();
    if (!index.isValid() ||
        index.data(GameListItem::TypeRole).toInt() != static_cast<int>(GameListItemType::Game)) {
        return;
    }
    const QString path = index.data(GameListItemPath::FullPathRole).toString();
    const u64 program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    if (!path.isEmpty()) {
        launched = true;
        emit GamePlayRequested(path, program_id);
    }
}

void DeckGamesPage::EmitCurrentGame() {
    const QModelIndex index = CurrentGameIndex();
    if (!index.isValid()) {
        return;
    }
    if (index.data(GameListItem::TypeRole).toInt() != static_cast<int>(GameListItemType::Game)) {
        return;
    }
    const QString path = index.data(GameListItemPath::FullPathRole).toString();
    const u64 program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    QString title = index.data(GameListItemPath::TitleRole).toString();
    if (title.isEmpty()) {
        title = index.data(Qt::DisplayRole).toString();
    }
    const QPixmap art = index.data(Qt::DecorationRole).value<QPixmap>();
    const QString meta = index.data(Qt::ToolTipRole).toString();
    const u64 pid = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    const bool fav = pid != 0 && UISettings::values.favorited_ids.contains(pid);
    if (!path.isEmpty()) {
        emit GameActivated(path, program_id, title, art, meta, fav);
    }
}

std::vector<DeckHint> DeckGamesPage::Hints() const {
    if (grid_mode) {
        return {
            {QStringLiteral("A"), tr("Play")},
            {QStringLiteral("+"), tr("Options")},
            {QStringLiteral("B"), tr("Back")},
        };
    }
    if (zone == Zone::Avatar) {
        return {{QStringLiteral("A"), tr("Users")}};
    }
    if (zone == Zone::Dock) {
        return {{QStringLiteral("A"), tr("Open")}};
    }
    return {
        {QStringLiteral("A"), tr("Play")},
        {QStringLiteral("+"), tr("Options")},
    };
}
