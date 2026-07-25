// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLinearGradient>
#include <QListView>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "common/fs/fs_paths.h"
#include "common/fs/path_util.h"
#include "yuzu/deck/deck_album_page.h"
#include "yuzu/deck/deck_theme.h"

namespace {
constexpr int kCellW = 250;
constexpr int kCellH = 141; // 16:9 screenshots, like the Switch Album (4 per row)
constexpr int kSpacing = 18;
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kIconGutter = 72; // left column for the filter/sort icons
} // namespace

DeckAlbumPage::DeckAlbumPage(QWidget* parent) : DeckPage(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(48, 40, 48, 24);
    outer->setSpacing(16);

    // Header: "Album" on the left, the sort/count label on the right (Switch Album).
    auto* header = new QHBoxLayout();
    title = new QLabel(tr("Album"), this);
    header->addWidget(title);
    header->addStretch(1);
    sort_label = new QLabel(this);
    sort_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(sort_label);
    outer->addLayout(header);

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

    // Body: the filter/sort icon gutter (painted) then the thumbnail grid.
    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->addSpacing(kIconGutter);
    body->addWidget(grid, 1);
    outer->addLayout(body, 1);

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
    sort_label->setStyleSheet(
        QStringLiteral("font-size:20px; color:%1;").arg(DeckTheme::kTextDim.name()));
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

void DeckAlbumPage::paintEvent(QPaintEvent*) {
    if (grid == nullptr || !grid->isVisible()) {
        return; // only the grid view has the filter/sort column
    }
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(Qt::NoBrush);
    const QColor col = DeckTheme::kTextDim;
    const qreal cx = 48 + kIconGutter / 2.0 - 8; // centre of the left gutter
    qreal y = grid->geometry().top() + 14;

    // Filter (funnel).
    p.setPen(QPen(col, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath funnel;
    funnel.moveTo(cx - 12, y);
    funnel.lineTo(cx + 12, y);
    funnel.lineTo(cx + 3, y + 11);
    funnel.lineTo(cx + 3, y + 21);
    funnel.lineTo(cx - 3, y + 17);
    funnel.lineTo(cx - 3, y + 11);
    funnel.closeSubpath();
    p.drawPath(funnel);

    // Sort (up/down arrows).
    y += 52;
    p.drawLine(QPointF(cx - 6, y + 16), QPointF(cx - 6, y));
    p.drawLine(QPointF(cx - 6, y), QPointF(cx - 10, y + 5));
    p.drawLine(QPointF(cx - 6, y), QPointF(cx - 2, y + 5));
    p.drawLine(QPointF(cx + 6, y), QPointF(cx + 6, y + 16));
    p.drawLine(QPointF(cx + 6, y + 16), QPointF(cx + 2, y + 11));
    p.drawLine(QPointF(cx + 6, y + 16), QPointF(cx + 10, y + 11));
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

    const int n = model->rowCount();
    const bool empty = n == 0;
    grid->setVisible(!empty && !viewing);
    placeholder->setVisible(empty && !viewing);
    sort_label->setText(tr("Newest First    |    All (%1)").arg(n));
    sort_label->setVisible(!empty && !viewing);
    if (!empty) {
        grid->setCurrentIndex(model->index(0, 0));
    }
    update(); // repaint the left icon gutter
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
