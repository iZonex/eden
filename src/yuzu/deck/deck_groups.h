// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QString>

#include "common/common_types.h"

/**
 * Persistent user-created software Groups (the Switch's All Software folders). Each group has a name
 * and a set of game program ids; membership and names are saved to a small JSON file in the config
 * dir so groups survive across launches. A game may belong to several groups.
 */
class DeckGroups {
public:
    struct Group {
        QString name;
        std::vector<u64> programs;
    };

    DeckGroups();

    const std::vector<Group>& Groups() const {
        return groups;
    }
    std::size_t Count() const {
        return groups.size();
    }

    int CreateGroup(const QString& name); ///< returns the new group's index
    void RenameGroup(int index, const QString& name);
    void DeleteGroup(int index);

    void SetMembership(int index, u64 program_id, bool member);
    bool IsMember(int index, u64 program_id) const;

    void Save() const;

private:
    void Load();
    QString FilePath() const;

    std::vector<Group> groups;
};
