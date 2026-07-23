// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <vector>
#include <fmt/format.h>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVBoxLayout>

#include "common/fs/fs_util.h"
#include "common/fs/path_util.h"
#include "common/string_util.h"
#include "common/uuid.h"
#include "core/constants.h"
#include "core/core.h"
#include "core/hle/service/acc/profile_manager.h"

#include "yuzu/deck/deck_theme.h"
#include "yuzu/deck/deck_users_page.h"

namespace {

struct UserEntry {
    Common::UUID uuid;
    QString name;
    QPixmap avatar;
    bool active = false;
};

QPixmap LoadAvatar(const Common::UUID& uuid) {
    const auto path =
        Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir) /
        fmt::format("system/save/8000000000000010/su/avators/{}.jpg", uuid.FormattedString());
    QPixmap icon{QString::fromStdString(Common::FS::PathToUTF8String(path))};
    if (icon.isNull()) {
        icon.loadFromData(Core::Constants::ACCOUNT_BACKUP_JPEG.data(),
                          static_cast<unsigned int>(Core::Constants::ACCOUNT_BACKUP_JPEG.size()));
    }
    return icon;
}

QString UserName(const Service::Account::ProfileManager& manager, const Common::UUID& uuid) {
    Service::Account::ProfileBase profile{};
    if (!manager.GetProfileBase(uuid, profile)) {
        return QObject::tr("User");
    }
    const auto text = Common::StringFromFixedZeroTerminatedBuffer(
        reinterpret_cast<const char*>(profile.username.data()), profile.username.size());
    const QString name = QString::fromStdString(text).trimmed();
    return name.isEmpty() ? QObject::tr("User") : name;
}

QPixmap RoundAvatar(const QPixmap& src, int size) {
    if (src.isNull()) {
        return {};
    }
    const int px = size * 2;
    QPixmap round(px, px);
    round.fill(Qt::transparent);
    QPainter p(&round);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath clip;
    clip.addEllipse(0, 0, px, px);
    p.setClipPath(clip);
    const QPixmap scaled = src.scaled(px, px, Qt::KeepAspectRatioByExpanding,
                                      Qt::SmoothTransformation);
    p.drawPixmap((px - scaled.width()) / 2, (px - scaled.height()) / 2, scaled);
    p.end();
    round.setDevicePixelRatio(2.0);
    return round;
}

} // namespace

/// The left column: one row per user (avatar + name), the selected one framed in cyan, plus an
/// "Add user" row at the end. The right pane shows the selected user's profile. Plain QWidget (no MOC).
class UserSidebar : public QWidget {
public:
    explicit UserSidebar(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedWidth(360);
    }
    void SetEntries(std::vector<UserEntry> entries_) {
        entries = std::move(entries_);
        update();
    }
    void SetSelected(int s) {
        if (selected != s) {
            selected = s;
            update();
        }
    }

protected:
    QRect RowRect(int i) const {
        constexpr int kRow = 76;
        return QRect(12, 20 + i * (kRow + 8), width() - 24, kRow);
    }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.fillRect(rect(), DeckTheme::kBackground);
        p.setPen(QPen(DeckTheme::kDivider, 1));
        p.drawLine(width() - 1, 0, width() - 1, height());

        const int count = static_cast<int>(entries.size()) + 1;
        for (int i = 0; i < count; ++i) {
            const QRect row = RowRect(i);
            const bool sel = (i == selected);
            const bool is_add = (i == static_cast<int>(entries.size()));
            if (sel) {
                QPainterPath box;
                box.addRoundedRect(row, 12, 12);
                p.fillPath(box, DeckTheme::kAccentSoft);
                p.setPen(QPen(DeckTheme::kAccent, 2));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(row, 12, 12);
            }
            const int av = 52;
            const QRect avatar_rect(row.left() + 14, row.center().y() - av / 2, av, av);
            if (is_add) {
                p.setPen(QPen(sel ? DeckTheme::kAccent : DeckTheme::kPlaceholderBorder, 2,
                              Qt::DashLine));
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(avatar_rect);
                QFont pf = font();
                pf.setPixelSize(30);
                p.setFont(pf);
                p.setPen(sel ? DeckTheme::kAccent : DeckTheme::kTextDim);
                p.drawText(avatar_rect, Qt::AlignCenter, QStringLiteral("+"));
            } else {
                const QPixmap rav = RoundAvatar(entries[i].avatar, av);
                if (!rav.isNull()) {
                    p.drawPixmap(avatar_rect.topLeft(), rav);
                }
                if (entries[i].active) {
                    const int bs = 16;
                    p.setPen(QPen(DeckTheme::kBackground, 2));
                    p.setBrush(DeckTheme::kAccent);
                    p.drawEllipse(QRect(avatar_rect.right() - bs, avatar_rect.top(), bs, bs));
                }
            }
            QFont nf = font();
            nf.setPixelSize(21);
            nf.setBold(sel);
            p.setFont(nf);
            p.setPen(DeckTheme::kText);
            const QString label = is_add ? QObject::tr("Add user") : entries[i].name;
            p.drawText(QRect(avatar_rect.right() + 16, row.top(), row.right() - avatar_rect.right() - 24,
                             row.height()),
                       Qt::AlignVCenter | Qt::AlignLeft,
                       p.fontMetrics().elidedText(label, Qt::ElideRight,
                                                  row.right() - avatar_rect.right() - 24));
        }
    }

private:
    std::vector<UserEntry> entries;
    int selected = 0;
};

DeckUsersPage::DeckUsersPage(Core::System& system_, QWidget* parent)
    : DeckPage(parent), system{system_} {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    title = new QLabel(tr("Users"), this);
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 500; color: %1; "
                                        "padding: 26px 0 18px 40px;")
                             .arg(DeckTheme::kText.name()));
    root->addWidget(title);

    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    sidebar = new UserSidebar(this);
    body->addWidget(sidebar);

    // Right pane: the selected user's profile.
    auto* pane = new QWidget(this);
    auto* pane_layout = new QVBoxLayout(pane);
    pane_layout->setContentsMargins(56, 30, 56, 30);
    pane_layout->setSpacing(0);

    profile_avatar = new QLabel(pane);
    profile_avatar->setFixedSize(180, 180);
    profile_avatar->setAlignment(Qt::AlignCenter);
    pane_layout->addWidget(profile_avatar, 0, Qt::AlignHCenter);
    pane_layout->addSpacing(20);
    profile_name = new QLabel(pane);
    profile_name->setAlignment(Qt::AlignHCenter);
    profile_name->setStyleSheet(
        QStringLiteral("font-size: 34px; font-weight: 500; color: %1;").arg(DeckTheme::kText.name()));
    pane_layout->addWidget(profile_name);
    pane_layout->addSpacing(6);
    profile_status = new QLabel(pane);
    profile_status->setAlignment(Qt::AlignHCenter);
    profile_status->setStyleSheet(
        QStringLiteral("font-size: 20px; font-weight: 500; color: %1;").arg(DeckTheme::kAccent.name()));
    pane_layout->addWidget(profile_status);
    pane_layout->addStretch(1);
    hint = new QLabel(
        tr("Each user keeps their own save data.  A: set active   X: add user   B: back"), pane);
    hint->setAlignment(Qt::AlignHCenter);
    hint->setStyleSheet(
        QStringLiteral("font-size: 18px; color: %1;").arg(DeckTheme::kTextDim.name()));
    pane_layout->addWidget(hint);

    body->addWidget(pane, 1);
    root->addLayout(body, 1);
}

DeckUsersPage::~DeckUsersPage() = default;

void DeckUsersPage::Rebuild() {
    const auto& manager = system.GetProfileManager();
    const Common::UUID active = manager.GetLastOpenedUser();
    std::vector<UserEntry> entries;
    users.clear();
    for (const auto& uuid : manager.GetAllUsers()) {
        if (!uuid.IsValid()) {
            continue;
        }
        UserEntry e;
        e.uuid = uuid;
        e.name = UserName(manager, uuid);
        e.avatar = LoadAvatar(uuid);
        e.active = (uuid == active);
        entries.push_back(std::move(e));
        users.push_back(uuid);
    }
    selected = std::clamp(selected, 0, static_cast<int>(users.size())); // last index == "add" row
    sidebar->SetEntries(entries);
    sidebar->SetSelected(selected);
    UpdateDetail();
}

void DeckUsersPage::UpdateDetail() {
    const bool on_add = selected >= static_cast<int>(users.size());
    if (on_add) {
        profile_avatar->clear();
        profile_name->setText(tr("Add user"));
        profile_status->setText(tr("Create a new profile with its own saves"));
        return;
    }
    const auto& manager = system.GetProfileManager();
    const Common::UUID uuid = users[selected];
    profile_avatar->setPixmap(RoundAvatar(LoadAvatar(uuid), 180));
    profile_name->setText(UserName(manager, uuid));
    profile_status->setText(uuid == manager.GetLastOpenedUser() ? tr("Active user") : tr("Tap A to make active"));
}

void DeckUsersPage::SetSelected(int index) {
    selected = std::clamp(index, 0, static_cast<int>(users.size()));
    sidebar->SetSelected(selected);
    UpdateDetail();
}

bool DeckUsersPage::OnNavigate(Qt::Key key) {
    if (key == Qt::Key_Up) {
        SetSelected(selected - 1);
    } else if (key == Qt::Key_Down) {
        SetSelected(selected + 1);
    }
    return true;
}

bool DeckUsersPage::OnAccept() {
    if (selected >= static_cast<int>(users.size())) {
        CreateUser();
        return true;
    }
    auto& manager = system.GetProfileManager();
    if (selected >= 0 && selected < static_cast<int>(users.size())) {
        manager.OpenUser(users[selected]);
        manager.StoreOpenedUsers();
        emit SaveConfigRequested();
        Rebuild();
    }
    return true;
}

bool DeckUsersPage::OnPrimaryAction() {
    CreateUser();
    return true;
}

void DeckUsersPage::CreateUser() {
    auto& manager = system.GetProfileManager();
    const auto name = tr("User %1").arg(static_cast<int>(users.size()) + 1).toStdString();
    manager.CreateNewUser(Common::UUID::MakeRandom(), name);
    manager.WriteUserSaveFile();
    emit SaveConfigRequested();
    Rebuild();
}

void DeckUsersPage::OnActivated() {
    Rebuild();
    emit HintsChanged();
}

std::vector<DeckHint> DeckUsersPage::Hints() const {
    return {
        {QStringLiteral("A"), tr("Set active")},
        {QStringLiteral("X"), tr("Add user")},
        {QStringLiteral("B"), tr("Back")},
    };
}
