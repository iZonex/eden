// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <iterator>

#include <QFrame>
#include <QIdentityProxyModel>
#include <QLabel>
#include <QListView>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QVBoxLayout>

#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/deck/deck_all_software_page.h"
#include "yuzu/deck/deck_game_delegate.h"
#include "yuzu/deck/deck_keyboard.h"
#include "yuzu/deck/deck_theme.h"

namespace {
constexpr int kCardW = 200;
constexpr int kCardH = 200;
constexpr int kHeaderH = 108;

struct SortOption {
    const char* label;
    int role; // Qt::DisplayRole to sort by name; -1 = source order (recently played)
    Qt::SortOrder order;
};
const SortOption kSorts[] = {
    {QT_TR_NOOP("By Recently Played"), -1, Qt::DescendingOrder},
    {QT_TR_NOOP("By Title (A–Z)"), Qt::DisplayRole, Qt::AscendingOrder},
    {QT_TR_NOOP("By Title (Z–A)"), Qt::DisplayRole, Qt::DescendingOrder},
};
constexpr int kSortCount = static_cast<int>(std::size(kSorts));

u64 ProgramIdOf(const QModelIndex& idx) {
    return idx.data(GameListItemPath::ProgramIdRole).toULongLong();
}
bool IsGameRow(const QModelIndex& idx) {
    return idx.data(GameListItem::TypeRole).toInt() == static_cast<int>(GameListItemType::Game);
}
} // namespace

// The Switch All Software top bar: either centred "Software | Groups" tabs (with L/R hints) or a
// left-aligned title, a rule beneath, and the filter + sort icons on the left with the current sort
// on the right.
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
            // Only the current sort text lives in the header (top-right). The funnel + sort icons are
            // drawn by the page as a column on the far left, beside the games, like the Switch.
            f.setPixelSize(20);
            p.setFont(f);
            p.setPen(DeckTheme::kTextDim);
            p.drawText(QRect(width() - 360, 74, 356, 30), Qt::AlignRight | Qt::AlignVCenter,
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

namespace {
// Filters the library to the current group's member games.
class GroupFilterProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void SetGroup(const DeckGroups* g, int idx) {
        groups = g;
        group = idx;
        invalidateFilter();
    }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override {
        if (groups == nullptr || group < 0) {
            return false;
        }
        const QModelIndex i = sourceModel()->index(row, 0, parent);
        return groups->IsMember(group, ProgramIdOf(i));
    }

private:
    const DeckGroups* groups = nullptr;
    int group = -1;
};

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

DeckAllSoftwarePage::DeckAllSoftwarePage(QAbstractItemModel* library_, QWidget* parent)
    : DeckPage(parent), base(library_) {
    setAutoFillBackground(true);

    sorted = new QSortFilterProxyModel(this);
    sorted->setSourceModel(base);
    sorted->setDynamicSortFilter(true);

    groups_model = new GroupsModel(&groups, this);
    group_filter = new GroupFilterProxy(this);
    group_filter->setSourceModel(sorted);
    auto* mp = new MemberProxy(this);
    mp->setSourceModel(sorted);
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
    outer->addWidget(grid, 1);

    connect(grid, &QAbstractItemView::clicked, this, [this](const QModelIndex&) { OnAccept(); });

    // Floating name pill under the selected tile (Switch style).
    name_pill = new QLabel(this);
    name_pill->setAlignment(Qt::AlignCenter);
    name_pill->setVisible(false);

    keyboard = new DeckKeyboard(this);
    keyboard->hide();
    connect(keyboard, &DeckKeyboard::Accepted, this, [this](QString text) {
        if (renaming) {
            groups.RenameGroup(current_group, text);
            RefreshGroups();
            UpdateHeader();
        } else {
            current_group = groups.CreateGroup(text);
            RefreshGroups();
            SetView(View::GroupDetail);
        }
        emit HintsChanged();
    });
    connect(keyboard, &DeckKeyboard::Cancelled, this, [this] { emit HintsChanged(); });

    shimmer = new QTimer(this);
    shimmer->setInterval(40);
    connect(shimmer, &QTimer::timeout, this, [this] {
        phase = (phase + 5) % 360;
        if (delegate != nullptr) {
            delegate->SetPhase(phase);
        }
        grid->viewport()->update();
    });

    ApplyTheme();
}

DeckAllSoftwarePage::~DeckAllSoftwarePage() = default;

void DeckAllSoftwarePage::resizeEvent(QResizeEvent* event) {
    DeckPage::resizeEvent(event);
    keyboard->setGeometry(rect());
    PositionNamePill();
}

void DeckAllSoftwarePage::paintEvent(QPaintEvent* event) {
    DeckPage::paintEvent(event);
    // Funnel + sort icons as a column on the far left, beside the games (not on the Groups tab).
    if (view == View::Groups || keyboard->isVisible()) {
        return;
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = DeckTheme::kTextDim;
    const qreal cx = 30;                 // left gutter (page margin is 64, grid starts there)
    const qreal top = grid->y() + 26.0;  // aligned with the first game row
    p.setBrush(Qt::NoBrush);
    // Funnel.
    {
        const qreal x = cx - 14, y = top, w = 28, h = 24;
        QPainterPath funnel;
        funnel.moveTo(x, y);
        funnel.lineTo(x + w, y);
        funnel.lineTo(x + w * 0.62, y + h * 0.5);
        funnel.lineTo(x + w * 0.62, y + h);
        funnel.lineTo(x + w * 0.38, y + h * 0.82);
        funnel.lineTo(x + w * 0.38, y + h * 0.5);
        funnel.closeSubpath();
        p.setPen(QPen(ink, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(funnel);
    }
    // Sort arrows, below the funnel.
    {
        const qreal x = cx - 12, y = top + 40, w = 24, h = 24;
        p.setPen(QPen(ink, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
    const QString blue =
        DeckTheme::IsLightMode() ? QStringLiteral("#2f6cb5") : QStringLiteral("#6ab4ff");
    name_pill->setStyleSheet(
        QStringLiteral("background:%1; color:%2; border-radius:12px; padding:6px 16px; "
                       "font-size:22px;")
            .arg(DeckTheme::kSurface.name(), blue));
    if (header != nullptr) {
        header->update();
    }
    if (keyboard != nullptr) {
        keyboard->update();
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

void DeckAllSoftwarePage::PositionNamePill() {
    const QModelIndex idx = grid->currentIndex();
    const bool is_game = idx.isValid() && !idx.data(DeckGroupRole).isValid() &&
                         !idx.data(DeckNewGroupRole).toBool();
    if (!is_game || keyboard->isVisible()) {
        name_pill->setVisible(false);
        return;
    }
    QString name = idx.data(GameListItemPath::TitleRole).toString();
    if (name.isEmpty()) {
        name = idx.data(Qt::DisplayRole).toString();
    }
    name_pill->setText(name);
    name_pill->adjustSize();
    // Map the tile rect (viewport coords) into the page so the pill sits just under the tile.
    const QRect vr = grid->visualRect(idx);
    const QPoint tl = grid->viewport()->mapTo(this, vr.topLeft());
    int x = tl.x() + vr.width() / 2 - name_pill->width() / 2;
    x = std::clamp(x, 8, width() - name_pill->width() - 8);
    int y = tl.y() + vr.height() - name_pill->height() / 2;
    name_pill->move(x, y);
    name_pill->setVisible(true);
    name_pill->raise();
}

void DeckAllSoftwarePage::RefreshGroups() {
    groups_model->Refresh();
    static_cast<GroupFilterProxy*>(group_filter)->SetGroup(&groups, current_group);
    static_cast<MemberProxy*>(member_proxy)->SetGroup(&groups, current_group);
    groups.Save();
}

void DeckAllSoftwarePage::SetView(View v) {
    view = v;
    confirming_delete = false;
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
    case View::GroupDetail:
        static_cast<GroupFilterProxy*>(group_filter)->SetGroup(&groups, current_group);
        grid->setModel(group_filter);
        header->ShowTitle(current_group >= 0 && current_group < static_cast<int>(groups.Count())
                              ? groups.Groups()[current_group].name
                              : tr("Group"));
        header->SetShowControls(true);
        break;
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
    }
    UpdateHeader();
    PositionNamePill();
    update(); // repaint the left filter/sort column for the new view
    emit HintsChanged();
}

void DeckAllSoftwarePage::UpdateHeader() {
    header->SetSort(tr(kSorts[sort_mode].label));
    if (confirming_delete) {
        header->ShowTitle(tr("Delete this group?  + : delete   B: cancel"));
    }
    PositionNamePill();
}

void DeckAllSoftwarePage::CycleSort() {
    sort_mode = (sort_mode + 1) % kSortCount;
    if (kSorts[sort_mode].role < 0) {
        sorted->sort(-1);
    } else {
        sorted->setSortRole(kSorts[sort_mode].role);
        sorted->sort(0, kSorts[sort_mode].order);
    }
    if (grid->model()->rowCount() > 0) {
        grid->setCurrentIndex(grid->model()->index(0, 0));
        grid->scrollToTop();
    }
    UpdateHeader();
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
    groups.DeleteGroup(current_group);
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
    sort_mode = 0;
    sorted->sort(-1);
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
    const int n = grid->model()->rowCount();
    if (n == 0) {
        return true;
    }
    const int cur = grid->currentIndex().isValid() ? grid->currentIndex().row() : 0;
    const int cols = Columns();
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
    if (keyboard->isVisible()) {
        return true;
    }
    if (view == View::Software || view == View::GroupDetail || view == View::AddGames) {
        if (view == View::GroupDetail) {
            SetView(View::AddGames);
        } else if (view == View::AddGames) {
            SetView(View::GroupDetail);
        } else {
            CycleSort();
        }
    }
    return true;
}

bool DeckAllSoftwarePage::OnSecondaryAction() { // Y
    if (keyboard->isVisible()) {
        keyboard->Cancel();
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
    if (view == View::GroupDetail) {
        confirming_delete = true;
        UpdateHeader();
        emit HintsChanged();
    }
    return true;
}

// L / R switch the Software <-> Groups tabs, like the Switch.
bool DeckAllSoftwarePage::OnPageUp() {
    if (!keyboard->isVisible() && (view == View::Software || view == View::Groups)) {
        SetView(View::Software);
        return true;
    }
    return true;
}
bool DeckAllSoftwarePage::OnPageDown() {
    if (!keyboard->isVisible() && (view == View::Software || view == View::Groups)) {
        SetView(View::Groups);
        return true;
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
    if (confirming_delete) {
        return {{QStringLiteral("+"), tr("Delete")}, {QStringLiteral("B"), tr("Cancel")}};
    }
    if (view == View::AddGames) {
        return {{QStringLiteral("A"), tr("Toggle")},
                {QStringLiteral("X"), tr("Done")},
                {QStringLiteral("B"), tr("Back")}};
    }
    if (view == View::GroupDetail) {
        return {{QStringLiteral("A"), tr("Play")},
                {QStringLiteral("X"), tr("Add games")},
                {QStringLiteral("Y"), tr("Rename")},
                {QStringLiteral("+"), tr("Delete")},
                {QStringLiteral("B"), tr("Back")}};
    }
    if (view == View::Groups) {
        return {{QStringLiteral("A"), tr("Open")},
                {QStringLiteral("L/R"), tr("Software / Groups")},
                {QStringLiteral("B"), tr("Back")}};
    }
    return {{QStringLiteral("A"), tr("Play")},
            {QStringLiteral("X"), tr("Sort")},
            {QStringLiteral("L/R"), tr("Software / Groups")},
            {QStringLiteral("B"), tr("Back")}};
}
