// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <map>

#include <QPixmap>
#include <QString>

#include "common/common_types.h"

class QModelIndex;

/**
 * Per-title library history for the console shell: when a game first appeared in the library, when
 * it was last launched, and how often. This is what the home rail and All Software order themselves
 * by, so a game you just added and a game you just played both end up in front of you.
 *
 * Neither field existed before. Play time (PlayTimeManager) is a cumulative duration with no
 * timestamp, and the core's launch cache lives in CacheDir — legitimately wipeable by the OS, and
 * loaded once per process behind a static flag, so the frontend could never see a fresh launch it
 * had just triggered. This store lives in ConfigDir next to deck_groups.json and is owned by the
 * shell, which stamps it directly on every launch.
 */
class DeckLibraryStats {
public:
    DeckLibraryStats();

    /// Records a game the first time it is seen. Later calls for the same id do nothing, so the
    /// "date added" never drifts as the library is re-scanned.
    void NoteSeen(u64 program_id, const QString& path);
    /// Records a launch: last played becomes now, and the launch count goes up. Writes immediately.
    void NoteLaunched(u64 program_id);

    s64 FirstSeen(u64 program_id) const;  ///< 0 when the game has never been seen
    s64 LastPlayed(u64 program_id) const; ///< 0 when it has never been launched
    u64 Launches(u64 program_id) const;

    /// The ordering key: whichever is later, appearing in the library or being launched. Adding a
    /// game counts as activity, so it lands in front exactly like one you just played.
    s64 Activity(u64 program_id) const;

    /// Recently added and still untouched — worth a badge on the tile.
    bool IsNew(u64 program_id) const;

    void Save() const; ///< <ConfigDir>/deck_library.json

private:
    void Load();
    QString FilePath() const;

    struct Entry {
        s64 first_seen = 0;
        s64 last_played = 0;
        u64 launches = 0;
    };
    std::map<u64, Entry> entries;
    // Whether a store was already on disk when we started. Without one, every game in the library
    // is being seen for the first time and "new" would mean nothing; with one, anything missing
    // from it really did just appear.
    bool had_store = false;
};

/**
 * Everything the game detail page shows about one title, gathered from the game list model in one
 * place so the home rail and All Software describe a game identically.
 */
struct DeckGameInfo {
    QString path;
    QString title;
    QString file_type; ///< "NSP", "XCI", …
    QString version;   ///< update version, empty when the base game is unpatched
    QString size_text; ///< already formatted by the model ("8.21 GiB")
    QPixmap art;
    u64 program_id = 0;
    u64 play_time_seconds = 0;
    u64 launches = 0;
    s64 last_played = 0;
    s64 first_seen = 0;
    bool favorited = false;

    /// Reads column 0 plus the size/play-time/add-on sibling columns of `index`. Returns an entry
    /// with an empty path if the row is not a game.
    static DeckGameInfo FromIndex(const QModelIndex& index, const DeckLibraryStats& stats);
};
