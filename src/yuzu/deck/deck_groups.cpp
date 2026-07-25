// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "common/fs/path_util.h"
#include "yuzu/deck/deck_groups.h"

DeckGroups::DeckGroups() {
    Load();
}

QString DeckGroups::FilePath() const {
    const auto dir =
        QString::fromStdString(Common::FS::GetEdenPathString(Common::FS::EdenPath::ConfigDir));
    return dir + QStringLiteral("/deck_groups.json");
}

void DeckGroups::Load() {
    groups.clear();
    QFile f(FilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        return;
    }
    for (const QJsonValue g : doc.object().value(QStringLiteral("groups")).toArray()) {
        const QJsonObject obj = g.toObject();
        Group group;
        group.name = obj.value(QStringLiteral("name")).toString();
        for (const QJsonValue p : obj.value(QStringLiteral("programs")).toArray()) {
            bool ok = false;
            const u64 id = p.toString().toULongLong(&ok, 16);
            if (ok && id != 0) {
                group.programs.push_back(id);
            }
        }
        if (!group.name.isEmpty()) {
            groups.push_back(std::move(group));
        }
    }
}

void DeckGroups::Save() const {
    QJsonArray arr;
    for (const Group& group : groups) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), group.name);
        QJsonArray programs;
        for (const u64 id : group.programs) {
            programs.append(QString::number(id, 16));
        }
        obj.insert(QStringLiteral("programs"), programs);
        arr.append(obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("groups"), arr);
    QFile f(FilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    }
}

int DeckGroups::CreateGroup(const QString& name) {
    groups.push_back({name, {}});
    Save();
    return static_cast<int>(groups.size()) - 1;
}

void DeckGroups::RenameGroup(int index, const QString& name) {
    if (index < 0 || index >= static_cast<int>(groups.size()) || name.isEmpty()) {
        return;
    }
    groups[index].name = name;
    Save();
}

void DeckGroups::DeleteGroup(int index) {
    if (index < 0 || index >= static_cast<int>(groups.size())) {
        return;
    }
    groups.erase(groups.begin() + index);
    Save();
}

void DeckGroups::SetMembership(int index, u64 program_id, bool member) {
    if (index < 0 || index >= static_cast<int>(groups.size()) || program_id == 0) {
        return;
    }
    auto& programs = groups[index].programs;
    const auto it = std::find(programs.begin(), programs.end(), program_id);
    if (member && it == programs.end()) {
        programs.push_back(program_id);
    } else if (!member && it != programs.end()) {
        programs.erase(it);
    }
    Save();
}

bool DeckGroups::IsMember(int index, u64 program_id) const {
    if (index < 0 || index >= static_cast<int>(groups.size())) {
        return false;
    }
    const auto& programs = groups[index].programs;
    return std::find(programs.begin(), programs.end(), program_id) != programs.end();
}
