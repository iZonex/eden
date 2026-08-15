// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <map>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QPainter>
#include <QIcon>
#include <QPainterPath>
#include <QPixmap>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QThread>
#include <QVBoxLayout>

#include "core/file_sys/common_funcs.h"
#include "core/file_sys/ncz.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "qt_common/config/uisettings.h"
#include "yuzu/deck/deck_card_storage_page.h"
#include "yuzu/deck/deck_theme.h"

namespace {
constexpr int kCellW = 200;
constexpr int kCellH = 200; // square, like a card standing in its case
constexpr int kSpacing = 20;
constexpr int kIconGutter = 72;
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kSizeRole = Qt::UserRole + 2;

QString Human(qint64 bytes) {
    if (bytes >= 1024LL * 1024 * 1024) {
        return QStringLiteral("%1 GB").arg(bytes / double(1024LL * 1024 * 1024), 0, 'f', 2);
    }
    return QStringLiteral("%1 MB").arg(bytes / double(1024 * 1024), 0, 'f', 0);
}

/// The name a dump carries is the title followed by the bookkeeping a dumper added -- the title id,
/// the version, the size it happened to be. The card only wants the part a person recognises.
QString TitleFromFilename(const QString& stem) {
    const int bracket = stem.indexOf(QLatin1Char('['));
    const QString cut = bracket > 0 ? stem.left(bracket) : stem;
    return cut.trimmed();
}
} // namespace

DeckCardStoragePage::DeckCardStoragePage(QWidget* parent) : DeckPage(parent) {
    // Pages share one stack, and a stack is as tall as its tallest page wants to be. A page that
    // asks for room it does not need pushes the bar with the button hints off the bottom of every
    // other screen -- so this one asks for none.
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(48, 40, 48, 24);
    outer->setSpacing(16);

    auto* header = new QHBoxLayout();
    title = new QLabel(tr("Card Storage"), this);
    header->addWidget(title);
    header->addStretch(1);
    summary = new QLabel(this);
    summary->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    header->addWidget(summary);
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
    grid->setWordWrap(true);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->addSpacing(kIconGutter);
    body->addWidget(grid, 1);
    outer->addLayout(body, 1);

    placeholder = new QLabel(tr("No cards here."), this);
    placeholder->setWordWrap(true);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setVisible(false);
    outer->addWidget(placeholder, 1);

    status = new QLabel(this);
    status->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    status->setVisible(false);
    outer->addWidget(status);

    ApplyTheme();
}

DeckCardStoragePage::~DeckCardStoragePage() {
    if (worker != nullptr) {
        worker->quit();
        worker->wait();
    }
}

void DeckCardStoragePage::ApplyTheme() {
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 600; color: %1;")
                             .arg(DeckTheme::kText.name()));
    const QString muted = QStringLiteral("font-size: 18px; color: %1;").arg(DeckTheme::kTextDim.name());
    summary->setStyleSheet(muted);
    placeholder->setStyleSheet(muted);
    status->setStyleSheet(QStringLiteral("font-size: 20px; color: %1;").arg(DeckTheme::kText.name()));
}

int DeckCardStoragePage::Columns() const {
    const int usable = grid->viewport()->width();
    return std::max(1, usable / (kCellW + kSpacing));
}

void DeckCardStoragePage::UpdateSummary() {
    qint64 total = 0;
    for (int row = 0; row < model->rowCount(); ++row) {
        total += model->item(row)->data(kSizeRole).toLongLong();
    }
    summary->setText(tr("%n card(s)", "", model->rowCount()) +
                     (model->rowCount() > 0 ? QStringLiteral("   |   %1").arg(Human(total))
                                            : QString{}));
}

void DeckCardStoragePage::Reload() {
    model->clear();

    // A game and its update are two files and one thing. The library merges them, and a shelf that
    // did not would be listing bookkeeping as if it were software.
    struct Group {
        QStringList paths;
        QString label;
        QPixmap art;
        qint64 size = 0;
        int parts = 0;
    };
    std::map<QString, Group> groups;

    for (const auto& dir : UISettings::values.game_dirs) {
        if (dir.path.empty()) {
            continue;
        }
        QDirIterator it{QString::fromStdString(dir.path),
                        {QStringLiteral("*.nsz"), QStringLiteral("*.NSZ")}, QDir::Files,
                        dir.deep_scan ? QDirIterator::Subdirectories
                                      : QDirIterator::NoIteratorFlags};
        while (it.hasNext()) {
            const QFileInfo info{it.next()};
            QString label = TitleFromFilename(info.completeBaseName());
            QPixmap art;
            QString key = label; // fallback for a dump that will not say what it is
            bool addon = false;

            FileSys::RealVfsFilesystem vfs;
            if (const auto file = vfs.OpenFile(info.absoluteFilePath().toStdString(),
                                               FileSys::OpenMode::Read)) {
                if (const auto shown = FileSys::ReadNszPresentation(file)) {
                    if (!shown->title.empty()) {
                        label = QString::fromStdString(shown->title);
                    }
                    if (!shown->icon.empty()) {
                        art.loadFromData(shown->icon.data(), static_cast<uint>(shown->icon.size()));
                    }
                    if (shown->title_id != 0) {
                        key = QStringLiteral("%1").arg(FileSys::GetBaseTitleID(shown->title_id), 16,
                                                       16, QLatin1Char('0'));
                    }
                    addon = shown->addon;
                }
            }

            Group& group = groups[key];
            group.paths.push_back(info.absoluteFilePath());
            group.size += info.size();
            group.parts += 1;
            // The game names the card; an update carries no name of its own worth showing.
            if (!addon || group.label.isEmpty()) {
                group.label = label;
            }
            if (!art.isNull() && group.art.isNull()) {
                group.art = art;
            }
        }
    }

    for (const auto& [key, group] : groups) {
        QString caption = group.label + QStringLiteral("\n") + Human(group.size);
        if (group.parts > 1) {
            caption += tr(" · %n part(s)", "", group.parts);
        }
        auto* item = new QStandardItem(caption);
        if (!group.art.isNull()) {
            item->setIcon(QIcon{group.art.scaled(kCellW, kCellW - 40, Qt::KeepAspectRatio,
                                                 Qt::SmoothTransformation)});
        }
        item->setData(group.paths, kPathRole);
        item->setData(group.size, kSizeRole);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        item->setEditable(false);
        model->appendRow(item);
    }

    const bool empty = model->rowCount() == 0;
    grid->setVisible(!empty);
    placeholder->setVisible(empty);
    if (!empty) {
        grid->setCurrentIndex(model->index(0, 0));
    }
    UpdateSummary();
    emit HintsChanged();
}

void DeckCardStoragePage::OnActivated() {
    Reload();
}

void DeckCardStoragePage::paintEvent(QPaintEvent* event) {
    DeckPage::paintEvent(event);
    // The icon column the console screens keep on the left. Drawn rather than built from widgets
    // because nothing in it is interactive yet.
    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen{DeckTheme::kTextDim, 2});
    const int x = 48 + kIconGutter / 2;
    int y = 150;
    for (int i = 0; i < 3; ++i) {
        painter.drawRoundedRect(QRectF(x - 11, y - 11, 22, 22), 5, 5);
        y += 46;
    }
}

std::vector<DeckHint> DeckCardStoragePage::Hints() const {
    if (busy) {
        return {};
    }
    std::vector<DeckHint> hints;
    if (model->rowCount() > 0) {
        hints.push_back({QStringLiteral("A"), tr("Insert")});
    }
    hints.push_back({QStringLiteral("B"), tr("Back")});
    return hints;
}

bool DeckCardStoragePage::OnNavigate(Qt::Key key) {
    if (busy || model->rowCount() == 0) {
        return busy; // swallow input while a card is going in
    }
    const int columns = Columns();
    int row = grid->currentIndex().row();
    switch (key) {
    case Qt::Key_Left:
        row -= 1;
        break;
    case Qt::Key_Right:
        row += 1;
        break;
    case Qt::Key_Up:
        row -= columns;
        break;
    case Qt::Key_Down:
        row += columns;
        break;
    default:
        return false;
    }
    if (row < 0 || row >= model->rowCount()) {
        return true;
    }
    grid->setCurrentIndex(model->index(row, 0));
    return true;
}

bool DeckCardStoragePage::OnBack() {
    return busy; // do not leave mid-conversion
}

bool DeckCardStoragePage::OnAccept() {
    if (busy || model->rowCount() == 0) {
        return busy;
    }
    InsertSelected();
    return true;
}

void DeckCardStoragePage::InsertSelected() {
    const auto index = grid->currentIndex();
    if (!index.isValid()) {
        return;
    }
    const QStringList sources = model->itemFromIndex(index)->data(kPathRole).toStringList();
    if (sources.isEmpty()) {
        return;
    }
    for (const QString& source : sources) {
        const QFileInfo info{source};
        if (QFileInfo::exists(info.absolutePath() + QDir::separator() + info.completeBaseName() +
                              QStringLiteral(".nsp"))) {
            OnFinished(false, tr("There is already an unpacked copy beside this card."));
            return;
        }
    }

    busy = true;
    status->setVisible(true);
    status->setText(tr("Putting the card in…"));
    emit HintsChanged();

    // Hundreds of megabytes of reading and writing; the shell has to keep drawing.
    worker = QThread::create([this, sources] {
        bool ok = true;
        QString message;
        int part = 0;
        for (const QString& source : sources) {
            ++part;
            const QFileInfo info{source};
            const QString target = info.absolutePath() + QDir::separator() +
                                   info.completeBaseName() + QStringLiteral(".nsp");
            FileSys::RealVfsFilesystem vfs;
            const auto in = vfs.OpenFile(source.toStdString(), FileSys::OpenMode::Read);
            const auto out = vfs.CreateFile(target.toStdString(), FileSys::OpenMode::ReadWrite);
            ok = in != nullptr && out != nullptr;
            if (ok) {
                const int index_of = part;
                const int count = static_cast<int>(sources.size());
                ok = FileSys::ConvertNszToNsp(in, out, [this, index_of, count](u64 done, u64 total) {
                    if (total == 0) {
                        return;
                    }
                    const int percent = static_cast<int>(done * 100 / total);
                    QMetaObject::invokeMethod(
                        this,
                        [this, percent, index_of, count] {
                            status->setText(count > 1
                                                ? tr("Putting the card in… %1%  (%2 of %3)")
                                                      .arg(percent)
                                                      .arg(index_of)
                                                      .arg(count)
                                                : tr("Putting the card in… %1%").arg(percent));
                        },
                        Qt::QueuedConnection);
                });
            }
            if (!ok) {
                QFile::remove(target); // never leave a half-written title behind
                message = tr("This card could not be read. Nothing was changed.");
                break;
            }
            // Only now is this part's compressed copy expendable.
            QFile::remove(source);
        }
        if (ok) {
            message = tr("Card is in. The title is in your library.");
        }
        const bool result = ok;
        QMetaObject::invokeMethod(
            this, [this, result, message] { OnFinished(result, message); }, Qt::QueuedConnection);
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this] { worker = nullptr; });
    worker->start();
}

void DeckCardStoragePage::OnFinished(bool ok, QString message) {
    busy = false;
    status->setText(message);
    if (ok) {
        Reload();
        emit LibraryChanged();
    }
    emit HintsChanged();
}
