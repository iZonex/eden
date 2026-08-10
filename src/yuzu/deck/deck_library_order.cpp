// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QModelIndex>
#include <QSet>

#include "frontend_common/play_time_manager.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/deck/deck_groups.h"
#include "yuzu/deck/deck_library_order.h"
#include "yuzu/deck/deck_library_stats.h"

namespace {
u64 ProgramIdOf(const QModelIndex& index) {
    return index.data(GameListItemPath::ProgramIdRole).toULongLong();
}

QString TitleOf(const QModelIndex& index) {
    return index.data(GameListItemPath::TitleRole).toString();
}

// Size and play time live on sibling columns; their roles are the same numeric value as column 0's
// SortRole, so reading them without the hop returns a lowercased filename instead of a number.
qulonglong SizeOf(const QModelIndex& index) {
    return index.siblingAtColumn(GameListModel::COLUMN_SIZE)
        .data(GameListItemSize::SizeRole)
        .toULongLong();
}

u64 PlayTimeOf(const QModelIndex& index, const PlayTime::PlayTimeManager* play_time) {
    // The model's play-time column is filled when the row is built and does not move while the
    // shell is open; the manager is live. Prefer the manager, fall back to the column.
    if (play_time != nullptr) {
        if (const u64 seconds = play_time->GetPlayTime(ProgramIdOf(index)); seconds > 0) {
            return seconds;
        }
    }
    return index.siblingAtColumn(GameListModel::COLUMN_PLAY_TIME)
        .data(GameListItemPlayTime::PlayTimeRole)
        .toULongLong();
}
} // namespace

QString DeckSortKeyName(DeckSortKey key) {
    switch (key) {
    case DeckSortKey::Recent:
        return QCoreApplication::translate("DeckLibrary", "By Recent Activity");
    case DeckSortKey::LastPlayed:
        return QCoreApplication::translate("DeckLibrary", "By Recently Played");
    case DeckSortKey::DateAdded:
        return QCoreApplication::translate("DeckLibrary", "By Date Added");
    case DeckSortKey::PlayTime:
        return QCoreApplication::translate("DeckLibrary", "By Total Play Time");
    case DeckSortKey::TitleAZ:
        return QCoreApplication::translate("DeckLibrary", "By Title (A–Z)");
    case DeckSortKey::TitleZA:
        return QCoreApplication::translate("DeckLibrary", "By Title (Z–A)");
    case DeckSortKey::Size:
        return QCoreApplication::translate("DeckLibrary", "By Size");
    }
    return {};
}

bool DeckLessThan(const QModelIndex& left, const QModelIndex& right, DeckSortKey key,
                  const DeckLibraryStats& stats, const PlayTime::PlayTimeManager* play_time) {
    const u64 lpid = ProgramIdOf(left);
    const u64 rpid = ProgramIdOf(right);

    switch (key) {
    case DeckSortKey::Recent: {
        // Whichever is later, being added or being launched. A game copied onto the Deck today is
        // as "recent" as one played today, and launching anything puts it back in front — which is
        // the whole point: the games you actually touch stay within reach of the first tile.
        const s64 la = stats.Activity(lpid);
        const s64 ra = stats.Activity(rpid);
        if (la != ra) {
            return la > ra;
        }
        break;
    }
    case DeckSortKey::LastPlayed: {
        const s64 lp = stats.LastPlayed(lpid);
        const s64 rp = stats.LastPlayed(rpid);
        if (lp != rp) {
            return lp > rp; // never-played is 0, so those sink to the bottom together
        }
        break;
    }
    case DeckSortKey::DateAdded: {
        const s64 la = stats.FirstSeen(lpid);
        const s64 ra = stats.FirstSeen(rpid);
        if (la != ra) {
            return la > ra;
        }
        break;
    }
    case DeckSortKey::PlayTime: {
        const u64 lt = PlayTimeOf(left, play_time);
        const u64 rt = PlayTimeOf(right, play_time);
        if (lt != rt) {
            return lt > rt;
        }
        break;
    }
    case DeckSortKey::Size: {
        const qulonglong ls = SizeOf(left);
        const qulonglong rs = SizeOf(right);
        if (ls != rs) {
            return ls > rs;
        }
        break;
    }
    case DeckSortKey::TitleZA: {
        const int c = QString::localeAwareCompare(TitleOf(left), TitleOf(right));
        if (c != 0) {
            return c > 0;
        }
        break;
    }
    case DeckSortKey::TitleAZ:
        break; // the shared tiebreak below already is A–Z
    }

    // localeAwareCompare, not operator<: the default string compare is case-sensitive, which put
    // every capitalised title ahead of every lowercase one and made "A–Z" look broken.
    const int c = QString::localeAwareCompare(TitleOf(left), TitleOf(right));
    return c != 0 ? c < 0 : lpid < rpid;
}

bool DeckAccepts(const QModelIndex& index, const DeckFilterState& filter,
                 const DeckLibraryStats& stats, const DeckGroups* groups) {
    const u64 pid = ProgramIdOf(index);

    switch (filter.played) {
    case DeckFilterState::Played::Never:
        if (stats.LastPlayed(pid) != 0) {
            return false;
        }
        break;
    case DeckFilterState::Played::Played:
        if (stats.LastPlayed(pid) == 0) {
            return false;
        }
        break;
    case DeckFilterState::Played::Any:
        break;
    }

    if (filter.favorites_only && !UISettings::values.favorited_ids.contains(pid)) {
        return false;
    }
    if (filter.group >= 0 && (groups == nullptr || !groups->IsMember(filter.group, pid))) {
        return false;
    }
    if (!filter.file_type.isEmpty() &&
        index.data(GameListItemPath::FileTypeRole).toString().compare(
            filter.file_type, Qt::CaseInsensitive) != 0) {
        return false;
    }
    return true;
}

QStringList DeckFileTypes(const QAbstractItemModel* model) {
    if (model == nullptr) {
        return {};
    }
    QSet<QString> seen;
    const int rows = model->rowCount();
    for (int row = 0; row < rows; ++row) {
        const QString type =
            model->index(row, 0).data(GameListItemPath::FileTypeRole).toString().trimmed();
        if (!type.isEmpty()) {
            seen.insert(type);
        }
    }
    QStringList list(seen.begin(), seen.end());
    list.sort();
    return list;
}
