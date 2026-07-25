// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <iterator>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QVBoxLayout>

#include "qt_common/game_list/game_list_p.h"
#include "yuzu/deck/deck_all_software_page.h"
#include "yuzu/deck/deck_game_delegate.h"
#include "yuzu/deck/deck_theme.h"

namespace {
constexpr int kCardW = 168; // denser than the home rail's 272px box art
constexpr int kCardH = 168;

struct SortOption {
    const char* label;
    int role;      // Qt::DisplayRole to sort by name; -1 = source order (recently played)
    Qt::SortOrder order;
};
// Mirrors the Switch All Software sort control (the subset our data supports cleanly).
const SortOption kSorts[] = {
    {QT_TR_NOOP("Recently played"), -1, Qt::DescendingOrder},
    {QT_TR_NOOP("Title (A–Z)"), Qt::DisplayRole, Qt::AscendingOrder},
    {QT_TR_NOOP("Title (Z–A)"), Qt::DisplayRole, Qt::DescendingOrder},
};
constexpr int kSortCount = static_cast<int>(std::size(kSorts));
} // namespace

DeckAllSoftwarePage::DeckAllSoftwarePage(QAbstractItemModel* library_, QWidget* parent)
    : DeckPage(parent) {
    // Sort on our own proxy so re-ordering here never disturbs the home rail's recency order.
    auto* proxy = new QSortFilterProxyModel(this);
    proxy->setSourceModel(library_);
    proxy->setDynamicSortFilter(true);
    library = proxy;

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(64, 40, 64, 24);
    outer->setSpacing(14);

    // Header: a big title on the left, the current sort on the right — a clear, distinct screen, not
    // the home rail continued.
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

    // Thin rule under the header, so the grid reads as its own region.
    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);
    rule->setFixedHeight(1);
    outer->addWidget(rule);

    grid = new QListView(this);
    grid->setModel(library);
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
    grid->setFocusPolicy(Qt::NoFocus); // the shell drives navigation
    grid->setFrameShape(QFrame::NoFrame);
    grid->setStyleSheet(QStringLiteral("QListView{background:transparent;}"));
    outer->addWidget(grid, 1);

    connect(grid, &QAbstractItemView::clicked, this, [this](const QModelIndex&) { OnAccept(); });
    connect(grid->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { UpdateHeader(); });

    // Selection shimmer, matching the home tiles.
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

void DeckAllSoftwarePage::ApplyTheme() {
    title->setStyleSheet(QStringLiteral("font-size:34px; font-weight:500; color:%1;")
                             .arg(DeckTheme::kText.name()));
    const QString dim = QStringLiteral("font-size:22px; color:%1;").arg(DeckTheme::kTextDim.name());
    sort_label->setStyleSheet(dim);
    selected_name->setStyleSheet(
        QStringLiteral("font-size:22px; color:%1; padding-right:18px;")
            .arg(DeckTheme::IsLightMode() ? QStringLiteral("#2f6cb5") : QStringLiteral("#6ab4ff")));
}

int DeckAllSoftwarePage::Columns() const {
    const int cell = kCardW + 2 * DeckTheme::kGridCardMargin;
    return std::max(1, grid->viewport()->width() / cell);
}

void DeckAllSoftwarePage::UpdateHeader() {
    sort_label->setText(tr("Sort: %1").arg(tr(kSorts[sort_mode].label)));
    const QModelIndex idx = grid->currentIndex();
    QString name;
    if (idx.isValid()) {
        name = idx.data(GameListItemPath::TitleRole).toString();
        if (name.isEmpty()) {
            name = idx.data(Qt::DisplayRole).toString();
        }
    }
    selected_name->setText(name);
}

void DeckAllSoftwarePage::CycleSort() {
    sort_mode = (sort_mode + 1) % kSortCount;
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(library);
    if (proxy != nullptr) {
        if (kSorts[sort_mode].role < 0) {
            proxy->sort(-1); // source order = recently played
        } else {
            proxy->setSortRole(kSorts[sort_mode].role);
            proxy->sort(0, kSorts[sort_mode].order);
        }
    }
    if (library->rowCount() > 0) {
        grid->setCurrentIndex(library->index(0, 0));
        grid->scrollToTop();
    }
    UpdateHeader();
    emit HintsChanged();
}

void DeckAllSoftwarePage::OnActivated() {
    shimmer->start();
    if (!grid->currentIndex().isValid() && library->rowCount() > 0) {
        grid->setCurrentIndex(library->index(0, 0));
    }
    UpdateHeader();
    emit HintsChanged();
}

bool DeckAllSoftwarePage::OnNavigate(Qt::Key key) {
    const int n = library->rowCount();
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
        return true; // don't wrap off the edges
    }
    const QModelIndex idx = library->index(next, 0);
    grid->setCurrentIndex(idx);
    grid->scrollTo(idx, QAbstractItemView::EnsureVisible);
    return true;
}

bool DeckAllSoftwarePage::OnAccept() {
    const QModelIndex idx = grid->currentIndex();
    if (!idx.isValid() ||
        idx.data(GameListItem::TypeRole).toInt() != static_cast<int>(GameListItemType::Game)) {
        return true;
    }
    const QString path = idx.data(GameListItemPath::FullPathRole).toString();
    const u64 program_id = idx.data(GameListItemPath::ProgramIdRole).toULongLong();
    if (!path.isEmpty()) {
        emit GamePlayRequested(path, program_id);
    }
    return true;
}

bool DeckAllSoftwarePage::OnBack() {
    shimmer->stop();
    return false; // let the shell return to the home menu
}

bool DeckAllSoftwarePage::OnPrimaryAction() {
    CycleSort();
    return true;
}

bool DeckAllSoftwarePage::OnSecondaryAction() {
    CycleSort();
    return true;
}

std::vector<DeckHint> DeckAllSoftwarePage::Hints() const {
    return {
        {QStringLiteral("A"), tr("Play")},
        {QStringLiteral("X"), tr("Sort")},
        {QStringLiteral("B"), tr("Back")},
    };
}
