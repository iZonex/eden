// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <functional>
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

/// Every user's round avatar, active user first — the Switch's top-left "My Page" cluster.
std::vector<QPixmap> AllUserAvatars(const Service::Account::ProfileManager& manager, int size) {
    std::vector<QPixmap> out;
    const Common::UUID active = manager.GetLastOpenedUser();
    if (active.IsValid()) {
        out.push_back(UserAvatar(active, size));
    }
    for (const auto& uuid : manager.GetAllUsers()) {
        if (!uuid.IsValid() || uuid == active) {
            continue;
        }
        out.push_back(UserAvatar(uuid, size));
    }
    if (out.empty()) {
        out.push_back(UserAvatar(active, size)); // fall back to the default avatar
    }
    return out;
}

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
    // No Users item — the avatar opens the Users page. Just the system functions, like the Switch
    // dock's grey icons.
    enum { kControllers = 0, kSettings = 1, kPower = 2, kCount = 3 };

    explicit DockBar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(150);
        names[kControllers] = QStringLiteral("controllers");
        names[kSettings] = QStringLiteral("settings");
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

        const std::array<QString, kCount> labels{QObject::tr("Controllers"), QObject::tr("Settings"),
                                                 QObject::tr("Power")};
        const QColor blue = DeckTheme::IsLightMode() ? QColor(0x2f, 0x6c, 0xb5)
                                                     : QColor(0x6a, 0xb4, 0xff);
        for (int i = 0; i < kCount; ++i) {
            const bool sel = (i == current) && zone_active;
            const QRectF sq = IconRect(i);

            if (sel) {
                // Animated iridescent ring (blue→purple→pink→cyan), sweeping with the phase.
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

            // Grey monochrome glyph, centred in its cell (no coloured background). Draw at the point
            // so the pixmap's DPR is honoured and it stays crisp.
            const QPixmap glyph = DeckTheme::Icon(names[i], kIconPx, DeckTheme::kTextDim);
            p.drawPixmap(QPointF(sq.center().x() - kIconPx / 2.0, sq.center().y() - kIconPx / 2.0),
                         glyph);

            // Label under every icon (the Switch shows the focused one's name; we keep them all so
            // each icon is clear). Selected = blue, others = dim grey.
            QFont f = font();
            f.setPixelSize(18);
            p.setFont(f);
            p.setPen(sel ? blue : DeckTheme::kTextDim);
            p.drawText(QRectF(sq.center().x() - 120, pill.bottom() + 6, 240, 24), Qt::AlignHCenter,
                       labels[i]);
        }
    }

private:
    int current = 0;
    int phase = 0;
    bool zone_active = false;
    std::array<QString, kCount> names;
};

/// The Switch home's top-left "My Page": a row of every user's round avatar (active user first). A
/// focusable cluster — when focused, the active user's avatar grows a clean iridescent ring; A opens
/// the Users page. Plain QWidget (no MOC).
class AvatarBadge : public QWidget {
public:
    explicit AvatarBadge(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedHeight(kCell);
        setFixedWidth(kCell);
    }
    void SetAvatars(std::vector<QPixmap> a) {
        avatars = std::move(a);
        if (avatars.empty()) {
            avatars.emplace_back();
        }
        setFixedWidth(kCell + (static_cast<int>(avatars.size()) - 1) * kStep);
        update();
    }
    void SetFocused(bool f) {
        if (focused != f) {
            focused = f;
            update();
        }
    }

protected:
    static constexpr int kCell = 60; // per-avatar slot (52px face + ring room)
    static constexpr int kFace = 52;
    static constexpr int kStep = 60; // horizontal advance per avatar (face + gap)

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (std::size_t i = 0; i < avatars.size(); ++i) {
            const int x = static_cast<int>(i) * kStep;
            const QRectF slot(x, 0, kCell, kCell);
            const QRectF face((kCell - kFace) / 2.0 + x, (kCell - kFace) / 2.0, kFace, kFace);
            if (!avatars[i].isNull()) {
                QPainterPath clip;
                clip.addEllipse(face);
                p.save();
                p.setClipPath(clip);
                p.drawPixmap(face.toRect(), avatars[i]);
                p.restore();
            } else {
                p.setPen(Qt::NoPen);
                p.setBrush(DeckTheme::kSurface);
                p.drawEllipse(face);
            }
            // Focus ring on the active (first) avatar only — the entry point to My Page.
            if (focused && i == 0) {
                QLinearGradient lg(slot.topLeft(), slot.bottomRight());
                lg.setColorAt(0.0, QColor(0x4f, 0x86, 0xff));
                lg.setColorAt(0.5, QColor(0xa9, 0x5c, 0xf0));
                lg.setColorAt(1.0, QColor(0xff, 0x6b, 0xb0));
                p.setBrush(Qt::NoBrush);
                p.setPen(QPen(QBrush(lg), 3));
                p.drawEllipse(slot.adjusted(2, 2, -2, -2));
            }
        }
    }

private:
    std::vector<QPixmap> avatars;
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

    rail = new QListView(this);
    rail->setModel(filter);
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
        clock->setText(QTime::currentTime().toString(QStringLiteral("HH:mm")));
        const QString bat = ReadBatteryText();
        battery->setText(bat);
        battery->setVisible(!bat.isEmpty());
        avatar->SetAvatars(AllUserAvatars(system.GetProfileManager(), 52));
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

QModelIndex DeckGamesPage::CurrentGameIndex() const {
    QModelIndex index = rail->currentIndex();
    if (!index.isValid() && filter->rowCount() > 0) {
        index = filter->index(0, 0);
    }
    return index;
}

void DeckGamesPage::MoveRail(int delta) {
    const int n = filter->rowCount();
    if (n == 0) {
        return;
    }
    const int cur = rail->currentIndex().isValid() ? rail->currentIndex().row() : 0;
    const QModelIndex idx = filter->index(qBound(0, cur + delta, n - 1), 0);
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
        rail->setCurrentIndex(filter->index(0, 0));
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
            SetZone(Zone::Rail); // the avatar is never a dead-end: any sideways move drops to the games
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
        emit OpenUsers(); // A on the avatar opens the Users page
    } else if (zone == Zone::Dock) {
        ActivateDock();
    } else {
        PlayCurrentGame(); // A boots the game straight away
    }
    return true;
}

void DeckGamesPage::ActivateDock() {
    switch (dock->Current()) {
    case DockBar::kControllers:
        emit OpenControllers();
        break;
    case DockBar::kSettings:
        emit OpenSettings();
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
