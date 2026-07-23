// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QLabel>
#include <QListView>
#include <QPixmap>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "yuzu/deck/deck_album_page.h"
#include "yuzu/deck/deck_theme.h"

namespace {
constexpr int kCellW = 300;
constexpr int kCellH = 188; // Deck screenshots are ~16:10
constexpr int kSpacing = 20;
constexpr int kPathRole = Qt::UserRole + 1;
} // namespace

DeckAlbumPage::DeckAlbumPage(QWidget* parent) : DeckPage(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(64, 48, 64, 24);
    outer->setSpacing(18);

    title = new QLabel(tr("Album"), this);
    outer->addWidget(title);

    grid = new QListView(this);
    model = new QStandardItemModel(this);
    grid->setModel(model);
    grid->setViewMode(QListView::IconMode);
    grid->setFlow(QListView::LeftToRight);
    grid->setWrapping(true);
    grid->setResizeMode(QListView::Adjust);
    grid->setMovement(QListView::Static);
    grid->setUniformItemSizes(true);
    grid->setIconSize(QSize(kCellW, kCellH));
    grid->setGridSize(QSize(kCellW + kSpacing, kCellH + kSpacing));
    grid->setSelectionMode(QAbstractItemView::SingleSelection);
    grid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid->setFocusPolicy(Qt::NoFocus); // the shell drives navigation
    grid->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    grid->setFrameShape(QFrame::NoFrame);
    outer->addWidget(grid, 1);

    placeholder = new QLabel(tr("No screenshots yet — press the Capture button in a game."), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setVisible(false);
    outer->addWidget(placeholder, 1);

    viewer = new QLabel(this);
    viewer->setAlignment(Qt::AlignCenter);
    viewer->setVisible(false);
    outer->addWidget(viewer, 1);

    ApplyTheme();
}

DeckAlbumPage::~DeckAlbumPage() = default;

void DeckAlbumPage::ApplyTheme() {
    title->setStyleSheet(QStringLiteral("font-size:34px; font-weight:500; color:%1;")
                             .arg(DeckTheme::kText.name()));
    placeholder->setStyleSheet(
        QStringLiteral("font-size:22px; color:%1;").arg(DeckTheme::kTextDim.name()));
    viewer->setStyleSheet(QStringLiteral("background:#000000;"));
    grid->setStyleSheet(
        QStringLiteral("QListView{background:transparent;} "
                       "QListView::item{border:none;} "
                       "QListView::item:selected{background:transparent; "
                       "border:3px solid %1; border-radius:8px;}")
            .arg(DeckTheme::kAccent.name()));
}

void DeckAlbumPage::Reload() {
    model->clear();
    const auto dir_path =
        QString::fromStdString(Common::FS::GetEdenPathString(Common::FS::EdenPath::ScreenshotsDir));
    QDir dir(dir_path);
    const auto files = dir.entryInfoList({QStringLiteral("*.png"), QStringLiteral("*.jpg"),
                                          QStringLiteral("*.jpeg"), QStringLiteral("*.bmp")},
                                         QDir::Files, QDir::Time); // newest first
    for (const QFileInfo& fi : files) {
        QImageReader reader(fi.absoluteFilePath());
        reader.setAutoTransform(true);
        // Downscale on decode so a full album stays light in memory.
        QSize sz = reader.size();
        if (sz.isValid()) {
            sz.scale(kCellW * 2, kCellH * 2, Qt::KeepAspectRatio);
            reader.setScaledSize(sz);
        }
        const QImage img = reader.read();
        if (img.isNull()) {
            continue;
        }
        auto* item = new QStandardItem();
        item->setData(QPixmap::fromImage(img).scaled(kCellW, kCellH, Qt::KeepAspectRatio,
                                                      Qt::SmoothTransformation),
                      Qt::DecorationRole);
        item->setData(fi.absoluteFilePath(), kPathRole);
        model->appendRow(item);
    }
    const bool empty = model->rowCount() == 0;
    grid->setVisible(!empty && !viewing);
    placeholder->setVisible(empty && !viewing);
    if (!empty) {
        grid->setCurrentIndex(model->index(0, 0));
    }
}

int DeckAlbumPage::Columns() const {
    const int w = grid->viewport()->width();
    return std::max(1, w / (kCellW + kSpacing));
}

void DeckAlbumPage::ShowViewer(bool on) {
    viewing = on;
    if (on) {
        const QModelIndex idx = grid->currentIndex();
        const QString path = idx.data(kPathRole).toString();
        QPixmap pm(path);
        if (!pm.isNull()) {
            viewer->setPixmap(pm.scaled(size() - QSize(0, 40), Qt::KeepAspectRatio,
                                        Qt::SmoothTransformation));
        }
    }
    title->setVisible(!on);
    grid->setVisible(!on && model->rowCount() > 0);
    placeholder->setVisible(!on && model->rowCount() == 0);
    viewer->setVisible(on);
    emit HintsChanged();
}

void DeckAlbumPage::OnActivated() {
    viewing = false;
    viewer->setVisible(false);
    Reload();
    emit HintsChanged();
}

bool DeckAlbumPage::OnNavigate(Qt::Key key) {
    if (viewing || model->rowCount() == 0) {
        return true; // nothing to move in the viewer / empty album
    }
    const int cur = grid->currentIndex().isValid() ? grid->currentIndex().row() : 0;
    const int n = model->rowCount();
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
    next = std::clamp(next, 0, n - 1);
    const QModelIndex idx = model->index(next, 0);
    grid->setCurrentIndex(idx);
    grid->scrollTo(idx, QAbstractItemView::EnsureVisible);
    return true;
}

bool DeckAlbumPage::OnAccept() {
    if (!viewing && grid->currentIndex().isValid()) {
        ShowViewer(true); // A opens the highlighted shot full-screen
    }
    return true;
}

bool DeckAlbumPage::OnBack() {
    if (viewing) {
        ShowViewer(false); // B leaves the viewer, back to the grid
        return true;
    }
    return false; // B on the grid lets the shell go home
}

std::vector<DeckHint> DeckAlbumPage::Hints() const {
    if (viewing) {
        return {{QStringLiteral("B"), tr("Back")}};
    }
    if (model->rowCount() == 0) {
        return {{QStringLiteral("B"), tr("Back")}};
    }
    return {{QStringLiteral("A"), tr("View")}, {QStringLiteral("B"), tr("Back")}};
}
