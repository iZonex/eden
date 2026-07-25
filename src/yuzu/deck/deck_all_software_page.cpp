// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <iterator>

#include <QConcatenateTablesProxyModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QIdentityProxyModel>
#include <QLabel>
#include <QListView>
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

struct SortOption {
    const char* label;
    int role; // Qt::DisplayRole to sort by name; -1 = source order (recently played)
    Qt::SortOrder order;
};
const SortOption kSorts[] = {
    {QT_TR_NOOP("Recently played"), -1, Qt::DescendingOrder},
    {QT_TR_NOOP("Title (A–Z)"), Qt::DisplayRole, Qt::AscendingOrder},
    {QT_TR_NOOP("Title (Z–A)"), Qt::DisplayRole, Qt::DescendingOrder},
};
constexpr int kSortCount = static_cast<int>(std::size(kSorts));

u64 ProgramIdOf(const QModelIndex& idx) {
    return idx.data(GameListItemPath::ProgramIdRole).toULongLong();
}
bool IsGameRow(const QModelIndex& idx) {
    return idx.data(GameListItem::TypeRole).toInt() == static_cast<int>(GameListItemType::Game);
}
} // namespace

// One-row-per-group model plus a trailing "＋ New Group" row, for the root grid.
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

    auto* concat = new QConcatenateTablesProxyModel(this);
    concat->addSourceModel(groups_model);
    concat->addSourceModel(sorted);
    root_model = concat;

    group_filter = new GroupFilterProxy(this);
    group_filter->setSourceModel(sorted);

    auto* mp = new MemberProxy(this);
    mp->setSourceModel(sorted);
    member_proxy = mp;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(64, 40, 64, 24);
    outer->setSpacing(14);

    auto* header = new QHBoxLayout();
    title = new QLabel(tr("All Software"), this);
    header->addWidget(title);
    header->addStretch();
    selected_name = new QLabel(this);
    selected_name->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    header->addWidget(selected_name, 1);
    sort_label = new QLabel(this);
    sort_label->setAlignment(Qt::AlignVCenter | Qt::AlignRight);
    header->addWidget(sort_label);
    outer->addLayout(header);

    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFixedHeight(1);
    outer->addWidget(rule);

    grid = new QListView(this);
    grid->setModel(root_model);
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

    keyboard = new DeckKeyboard(this);
    keyboard->hide();
    connect(keyboard, &DeckKeyboard::Accepted, this, [this](QString text) {
        if (renaming) {
            groups.RenameGroup(current_group, text);
        } else {
            current_group = groups.CreateGroup(text);
            RefreshGroups();
            SetView(View::Group);
            emit HintsChanged();
            return;
        }
        RefreshGroups();
        UpdateHeader();
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
}

void DeckAllSoftwarePage::ApplyTheme() {
    QPalette pal = palette();
    pal.setColor(QPalette::Window, DeckTheme::kBackground);
    setPalette(pal);
    title->setStyleSheet(QStringLiteral("font-size:34px; font-weight:500; color:%1;")
                             .arg(DeckTheme::kText.name()));
    sort_label->setStyleSheet(
        QStringLiteral("font-size:22px; color:%1;").arg(DeckTheme::kTextDim.name()));
    selected_name->setStyleSheet(
        QStringLiteral("font-size:22px; color:%1; padding-right:18px;")
            .arg(DeckTheme::IsLightMode() ? QStringLiteral("#2f6cb5") : QStringLiteral("#6ab4ff")));
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
    case View::Root:
        current_group = -1;
        grid->setModel(root_model);
        break;
    case View::Group:
        static_cast<GroupFilterProxy*>(group_filter)->SetGroup(&groups, current_group);
        grid->setModel(group_filter);
        break;
    case View::AddGames:
        static_cast<MemberProxy*>(member_proxy)->SetGroup(&groups, current_group);
        grid->setModel(member_proxy);
        break;
    }
    if (grid->model()->rowCount() > 0) {
        grid->setCurrentIndex(grid->model()->index(0, 0));
        grid->scrollToTop();
    }
    UpdateHeader();
    emit HintsChanged();
}

void DeckAllSoftwarePage::UpdateHeader() {
    QString head = tr("All Software");
    if (view == View::Group && current_group >= 0 &&
        current_group < static_cast<int>(groups.Count())) {
        head = groups.Groups()[current_group].name;
    } else if (view == View::AddGames && current_group >= 0 &&
               current_group < static_cast<int>(groups.Count())) {
        head = tr("Add to %1").arg(groups.Groups()[current_group].name);
    }
    title->setText(head);

    if (confirming_delete) {
        sort_label->setText(tr("Delete group?  + : delete   B: cancel"));
    } else if (view == View::Root) {
        sort_label->setText(tr("Sort: %1").arg(tr(kSorts[sort_mode].label)));
    } else if (view == View::AddGames) {
        sort_label->setText(tr("A: toggle   X: done"));
    } else {
        sort_label->setText(tr("X: add games   Y: rename   +: delete"));
    }

    const QModelIndex idx = grid->currentIndex();
    QString name;
    if (idx.isValid() && !idx.data(DeckGroupRole).isValid() &&
        !idx.data(DeckNewGroupRole).toBool()) {
        name = idx.data(GameListItemPath::TitleRole).toString();
        if (name.isEmpty()) {
            name = idx.data(Qt::DisplayRole).toString();
        }
    }
    selected_name->setText(name);
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
    keyboard->Start(tr("Name the group"), QString());
    emit HintsChanged();
}

void DeckAllSoftwarePage::StartRenameGroup() {
    if (current_group < 0 || current_group >= static_cast<int>(groups.Count())) {
        return;
    }
    renaming = true;
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
    SetView(View::Root);
}

void DeckAllSoftwarePage::ToggleCurrentMembership() {
    const u64 pid = CurrentProgramId();
    if (pid == 0 || current_group < 0) {
        return;
    }
    groups.SetMembership(current_group, pid, !groups.IsMember(current_group, pid));
    static_cast<MemberProxy*>(member_proxy)->SetGroup(&groups, current_group);
    grid->viewport()->update(); // reflect the check immediately
}

void DeckAllSoftwarePage::OnActivated() {
    shimmer->start();
    keyboard->hide();
    SetView(View::Root);
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
    UpdateHeader();
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
    if (view == View::Root && idx.data(DeckNewGroupRole).toBool()) {
        StartCreateGroup();
        return true;
    }
    if (view == View::Root && idx.data(DeckGroupRole).isValid()) {
        current_group = idx.data(DeckGroupRole).toInt();
        SetView(View::Group);
        return true;
    }
    if (view == View::AddGames) {
        ToggleCurrentMembership();
        return true;
    }
    // A game tile (root or inside a group): launch it.
    if (IsGameRow(idx)) {
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
        UpdateHeader();
        emit HintsChanged();
        return true;
    }
    if (view == View::AddGames) {
        SetView(View::Group);
        return true;
    }
    if (view == View::Group) {
        SetView(View::Root);
        return true;
    }
    shimmer->stop();
    return false; // Root: let the shell go home
}

bool DeckAllSoftwarePage::OnPrimaryAction() { // X
    if (keyboard->isVisible()) {
        return true;
    }
    if (view == View::Root) {
        CycleSort();
    } else if (view == View::Group) {
        SetView(View::AddGames);
    } else if (view == View::AddGames) {
        SetView(View::Group);
    }
    return true;
}

bool DeckAllSoftwarePage::OnSecondaryAction() { // Y
    if (keyboard->isVisible()) {
        keyboard->Cancel();
        return true;
    }
    if (view == View::Group) {
        StartRenameGroup();
    }
    return true;
}

bool DeckAllSoftwarePage::OnStart() { // +
    if (keyboard->isVisible()) {
        keyboard->Accept();
        return true;
    }
    if (view == View::Group) {
        confirming_delete = true;
        UpdateHeader();
        emit HintsChanged();
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
    if (view == View::Group) {
        return {{QStringLiteral("A"), tr("Play")},
                {QStringLiteral("X"), tr("Add games")},
                {QStringLiteral("Y"), tr("Rename")},
                {QStringLiteral("+"), tr("Delete")},
                {QStringLiteral("B"), tr("Back")}};
    }
    return {{QStringLiteral("A"), tr("Open / Play")},
            {QStringLiteral("X"), tr("Sort")},
            {QStringLiteral("B"), tr("Back")}};
}
