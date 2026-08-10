// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

#include "common/common_types.h"

class QAbstractItemModel;
class QModelIndex;
class DeckGroups;
class DeckLibraryStats;

namespace PlayTime {
class PlayTimeManager;
}

/**
 * How the console library can be ordered. `Recent` is the shell's default everywhere: it is the one
 * the home rail uses, so All Software opens showing the same order the user just came from.
 */
enum class DeckSortKey {
    Recent,     ///< newest activity first — added or launched, whichever is later
    LastPlayed, ///< most recently launched first; never-launched titles trail
    DateAdded,  ///< newest addition to the library first
    PlayTime,   ///< most hours first
    TitleAZ,
    TitleZA,
    Size, ///< largest first
};

/// Which subset of the library is shown. Every field is a narrowing, so they compose.
struct DeckFilterState {
    enum class Played {
        Any,
        Never, ///< never launched
        Played,
    };

    Played played = Played::Any;
    bool favorites_only = false;
    int group = -1;    ///< index into DeckGroups, -1 for every group
    QString file_type; ///< "NSP"/"XCI"/…, empty for every format

    bool IsDefault() const {
        return played == Played::Any && !favorites_only && group < 0 && file_type.isEmpty();
    }
};

/// Human-readable name of a sort key, for the header and the sort menu.
QString DeckSortKeyName(DeckSortKey key);

/**
 * Orders two library rows. `left`/`right` are column-0 indexes in whatever model the proxy sits on.
 *
 * Every key falls through to the same title-then-program-id tiebreak, which keeps the order total
 * and stable — without it, rows with an equal key (say, two never-played games under `PlayTime`)
 * shuffle every time the proxy re-sorts.
 *
 * Note that no key ever touches Qt::DisplayRole. On column 0 that role is computed from the user's
 * game-list row-text settings and, in tree mode, is a two-line "Title\n    0x0100…" string — sorting
 * on it is neither alphabetical nor stable.
 */
bool DeckLessThan(const QModelIndex& left, const QModelIndex& right, DeckSortKey key,
                  const DeckLibraryStats& stats, const PlayTime::PlayTimeManager* play_time);

/// Whether a library row passes the filter. `groups` may be null when no group filter is possible.
bool DeckAccepts(const QModelIndex& index, const DeckFilterState& filter,
                 const DeckLibraryStats& stats, const DeckGroups* groups);

/// The distinct file types present in a model, sorted, for building the format filter menu.
QStringList DeckFileTypes(const QAbstractItemModel* model);
