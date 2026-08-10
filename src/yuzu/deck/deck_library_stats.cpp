// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QModelIndex>
#include <QRegularExpression>

#include "common/fs/path_util.h"
#include "core/launch_timestamp_cache.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"
#include "yuzu/deck/deck_library_stats.h"

namespace {
constexpr s64 kNewForSeconds = 7 * 24 * 60 * 60; // a week

s64 Now() {
    return QDateTime::currentSecsSinceEpoch();
}
} // namespace

DeckLibraryStats::DeckLibraryStats() {
    Load();
}

QString DeckLibraryStats::FilePath() const {
    const auto dir =
        QString::fromStdString(Common::FS::GetEdenPathString(Common::FS::EdenPath::ConfigDir));
    return dir + QStringLiteral("/deck_library.json");
}

void DeckLibraryStats::Load() {
    entries.clear();
    QFile f(FilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }
    const QJsonObject root = doc.object();
    for (auto it = root.begin(); it != root.end(); ++it) {
        bool ok = false;
        const u64 id = it.key().toULongLong(&ok, 16);
        if (!ok || id == 0) {
            continue;
        }
        const QJsonObject obj = it.value().toObject();
        Entry e;
        e.first_seen = static_cast<s64>(obj.value(QStringLiteral("first_seen")).toDouble());
        e.last_played = static_cast<s64>(obj.value(QStringLiteral("last_played")).toDouble());
        e.launches = static_cast<u64>(obj.value(QStringLiteral("launches")).toDouble());
        entries.emplace(id, e);
    }
}

void DeckLibraryStats::Save() const {
    QJsonObject root;
    for (const auto& [id, e] : entries) {
        QJsonObject obj;
        obj.insert(QStringLiteral("first_seen"), static_cast<double>(e.first_seen));
        obj.insert(QStringLiteral("last_played"), static_cast<double>(e.last_played));
        obj.insert(QStringLiteral("launches"), static_cast<double>(e.launches));
        root.insert(QString::number(id, 16), obj);
    }
    QFile f(FilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

void DeckLibraryStats::NoteSeen(u64 program_id, const QString& path) {
    if (program_id == 0 || entries.count(program_id) != 0) {
        return;
    }
    Entry e;

    // Date added comes from the file's own timestamp, not from now(). Stamping now() would give an
    // existing library one identical date, making "by date added" useless and putting every game
    // level with a title you just copied over. Games arrive on the Deck by being copied in, so the
    // file's creation (or, where the filesystem has none, modification) time IS when it was added.
    const QFileInfo info(path);
    const QDateTime born = info.birthTime().isValid() ? info.birthTime() : info.lastModified();
    e.first_seen = born.isValid() ? born.toSecsSinceEpoch() : Now();

    // Adopt whatever launch history the core already recorded, once, so upgrading to this store
    // doesn't throw away what the user has played. Gate on the COUNT: GetLaunchTimestamp answers
    // 1767225600 (2026-01-01) for titles it has never seen rather than 0, so trusting it on its own
    // would sort every never-launched game as if it had been played in the future.
    if (const u64 count = Core::LaunchTimestampCache::GetLaunchCount(program_id); count > 0) {
        e.last_played = Core::LaunchTimestampCache::GetLaunchTimestamp(program_id);
        e.launches = count;
    }

    entries.emplace(program_id, e);
}

void DeckLibraryStats::NoteLaunched(u64 program_id) {
    if (program_id == 0) {
        return;
    }
    Entry& e = entries[program_id];
    if (e.first_seen == 0) {
        e.first_seen = Now(); // launched before the scan got to it
    }
    e.last_played = Now();
    ++e.launches;
    Save();
}

s64 DeckLibraryStats::FirstSeen(u64 program_id) const {
    const auto it = entries.find(program_id);
    return it == entries.end() ? 0 : it->second.first_seen;
}

s64 DeckLibraryStats::LastPlayed(u64 program_id) const {
    const auto it = entries.find(program_id);
    return it == entries.end() ? 0 : it->second.last_played;
}

u64 DeckLibraryStats::Launches(u64 program_id) const {
    const auto it = entries.find(program_id);
    return it == entries.end() ? 0 : it->second.launches;
}

s64 DeckLibraryStats::Activity(u64 program_id) const {
    const auto it = entries.find(program_id);
    if (it == entries.end()) {
        return 0;
    }
    return std::max(it->second.first_seen, it->second.last_played);
}

bool DeckLibraryStats::IsNew(u64 program_id) const {
    const auto it = entries.find(program_id);
    if (it == entries.end() || it->second.launches > 0 || it->second.first_seen == 0) {
        return false;
    }
    return Now() - it->second.first_seen < kNewForSeconds;
}

DeckGameInfo DeckGameInfo::FromIndex(const QModelIndex& index, const DeckLibraryStats& stats) {
    DeckGameInfo info;
    if (!index.isValid() ||
        index.data(GameListItem::TypeRole).toInt() != static_cast<int>(GameListItemType::Game)) {
        return info;
    }

    info.path = index.data(GameListItemPath::FullPathRole).toString();
    info.program_id = index.data(GameListItemPath::ProgramIdRole).toULongLong();
    info.title = index.data(GameListItemPath::TitleRole).toString();
    if (info.title.isEmpty()) {
        info.title = index.data(Qt::DisplayRole).toString();
    }
    info.file_type = index.data(GameListItemPath::FileTypeRole).toString();
    info.art = index.data(Qt::DecorationRole).value<QPixmap>();

    // Size and play time live on sibling columns, and their roles collide with column 0's SortRole
    // (all three are Qt::UserRole + 2) — reading them off column 0 silently returns the lowercased
    // filename instead. The hop to the right column is what makes them mean what they say.
    info.size_text = index.siblingAtColumn(GameListModel::COLUMN_SIZE).data(Qt::DisplayRole).toString();
    info.play_time_seconds = index.siblingAtColumn(GameListModel::COLUMN_PLAY_TIME)
                                 .data(GameListItemPlayTime::PlayTimeRole)
                                 .toULongLong();

    // Read the patch list itself, with the same pattern the model uses. The tooltip carries a
    // resolved version too, but behind a translated "Version: %1" prefix — parsing that reported
    // every patched game as 1.0.0 the moment the UI was not in English.
    static const QRegularExpression update_line{QStringLiteral(R"(^Update \(([0-9\.]+)\))")};
    const QString patches =
        index.siblingAtColumn(GameListModel::COLUMN_ADD_ONS).data(Qt::DisplayRole).toString();
    for (const QString& line : patches.split(QLatin1Char('\n'))) {
        const auto match = update_line.match(line);
        if (match.hasMatch()) {
            info.version = match.captured(1);
            break;
        }
    }

    info.first_seen = stats.FirstSeen(info.program_id);
    info.last_played = stats.LastPlayed(info.program_id);
    info.launches = stats.Launches(info.program_id);
    info.favorited =
        info.program_id != 0 && UISettings::values.favorited_ids.contains(info.program_id);
    return info;
}
