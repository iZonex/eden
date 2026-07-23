// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QTimer>
#include <QVBoxLayout>

#include "common/settings.h"
#include "common/settings_enums.h"
#include "common/settings_input.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"
#include "hid_core/hid_types.h"
#include "input_common/main.h"

#include "frontend_common/deck_input.h"
#include "yuzu/deck/deck_controllers_page.h"
#include "yuzu/deck/deck_theme.h"

namespace {
QString StyleName(Core::HID::NpadStyleIndex style) {
    using Core::HID::NpadStyleIndex;
    switch (style) {
    case NpadStyleIndex::Fullkey:
        return QObject::tr("Pro Controller");
    case NpadStyleIndex::Handheld:
        return QObject::tr("Handheld");
    case NpadStyleIndex::JoyconDual:
        return QObject::tr("Joy-Con (Dual)");
    case NpadStyleIndex::JoyconLeft:
        return QObject::tr("Joy-Con (L)");
    case NpadStyleIndex::JoyconRight:
        return QObject::tr("Joy-Con (R)");
    case NpadStyleIndex::GameCube:
        return QObject::tr("GameCube");
    default:
        return QObject::tr("Controller");
    }
}

QPixmap ControllerArt(Core::HID::NpadStyleIndex style) {
    using Core::HID::NpadStyleIndex;
    QString name;
    switch (style) {
    case NpadStyleIndex::Handheld:
        name = QStringLiteral("applet_handheld");
        break;
    case NpadStyleIndex::JoyconDual:
        name = QStringLiteral("applet_dual_joycon");
        break;
    case NpadStyleIndex::JoyconLeft:
        name = QStringLiteral("applet_joycon_left");
        break;
    case NpadStyleIndex::JoyconRight:
        name = QStringLiteral("applet_joycon_right");
        break;
    default:
        name = QStringLiteral("applet_pro_controller");
        break;
    }
    return QPixmap(QStringLiteral(":/controller/%1").arg(name));
}

bool HasRealBinding(const Core::HID::EmulatedController& controller) {
    const std::string engine =
        controller.GetButtonParam(Settings::NativeButton::A).Get("engine", "");
    return !engine.empty() && engine != "keyboard" && engine != "mouse";
}

/// The power info the card should show for a given Switch style: a single Joy-Con reports on its own
/// side, everything else (Pro, Handheld, dual) reports on the combined `dual` slot.
Core::HID::NpadPowerInfo PowerFor(const Core::HID::EmulatedController& controller) {
    using Core::HID::NpadStyleIndex;
    const auto b = controller.GetBattery();
    switch (controller.GetNpadStyleIndex()) {
    case NpadStyleIndex::JoyconLeft:
        return b.left;
    case NpadStyleIndex::JoyconRight:
        return b.right;
    default:
        return b.dual;
    }
}

/// A charge level in [0,1] and a colour for a battery level — green when healthy, amber when low,
/// red when nearly empty — matching the console's at-a-glance battery read.
struct BatteryVisual {
    qreal fill;
    QColor color;
};
BatteryVisual BatteryVisualFor(Core::HID::NpadBatteryLevel level) {
    using Core::HID::NpadBatteryLevel;
    switch (level) {
    case NpadBatteryLevel::Empty:
        return {0.08, QColor(0xff, 0x45, 0x3a)};
    case NpadBatteryLevel::Critical:
        return {0.22, QColor(0xff, 0x45, 0x3a)};
    case NpadBatteryLevel::Low:
        return {0.45, QColor(0xff, 0xb0, 0x20)};
    case NpadBatteryLevel::High:
        return {0.72, QColor(0x1a, 0xc6, 0x6d)};
    case NpadBatteryLevel::Full:
    default:
        return {1.0, QColor(0x1a, 0xc6, 0x6d)};
    }
}
} // namespace

/// One player card (connected player) or the "add a player" prompt. Plain QFrame (no MOC).
class PlayerCard : public QFrame {
public:
    explicit PlayerCard(QWidget* parent = nullptr) : QFrame(parent) {
        setMinimumSize(210, 208);
    }
    void SetPlayer(int player_index, const QPixmap& art_, const QString& name_,
                   Core::HID::NpadBatteryLevel level, bool charging) {
        add = false;
        index = player_index;
        art = art_;
        name = name_;
        battery_level = level;
        battery_charging = charging;
        update();
    }
    void SetAdd(bool listening_) {
        add = true;
        listening = listening_;
        update();
    }
    void SetSelected(bool s) {
        if (selected != s) {
            selected = s;
            update();
        }
    }
    bool IsAdd() const {
        return add;
    }
    int PlayerIndex() const {
        return index;
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.fillRect(rect(), DeckTheme::kBackground); // custom paint: stylesheet bg isn't drawn for us
        const QRectF r = rect().adjusted(5, 5, -5, -5);
        QPainterPath card;
        card.addRoundedRect(r, 14, 14);

        if (add) {
            p.setPen(QPen(listening ? DeckTheme::kAccent : DeckTheme::kPlaceholderBorder, 2,
                          Qt::DashLine));
            p.setBrush(listening ? QBrush(DeckTheme::kAccentSoft) : QBrush(Qt::NoBrush));
            p.drawPath(card);
        }
        // A connected controller is drawn straight on the page (no card fill / border) — the Switch
        // shows pads on the plain background, not boxed like the rest of our surfaces. Focus is shown
        // by the selection ring alone.
        if (selected) {
            QPainterPath ring;
            ring.addRoundedRect(r.adjusted(-2, -2, 2, 2), 15, 15);
            p.setPen(QPen(DeckTheme::kAccent, 3));
            p.setBrush(Qt::NoBrush);
            p.drawPath(ring);
        }

        if (add) {
            QFont f = font();
            f.setPixelSize(17);
            f.setBold(listening);
            p.setFont(f);
            p.setPen(listening ? DeckTheme::kAccent : DeckTheme::kTextDim);
            p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap,
                       listening ? QObject::tr("Press a button on the\ncontroller you want\nto add…")
                                 : QObject::tr("+  Add a player"));
            return;
        }

        // Top-left "P1" chip + the player's JoyCon-style LED beads, like the console shows on each
        // physical controller — so the user can tell which pad is which player at a glance.
        {
            const int dots = std::min(index + 1, 4);
            const qreal dot = 7, gap = 5;
            qreal x = r.left() + 16;
            const qreal y = r.top() + 18;
            QFont pf = font();
            pf.setPixelSize(15);
            pf.setBold(false);
            p.setFont(pf);
            p.setPen(DeckTheme::kTextDim);
            p.drawText(QRectF(x, y - 6, 34, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       QObject::tr("P%1").arg(index + 1));
            x += 34;
            for (int i = 0; i < 4; ++i) {
                p.setBrush(i < dots ? DeckTheme::kAccent : DeckTheme::kToggleOff);
                p.setPen(Qt::NoPen);
                p.drawEllipse(QRectF(x, y + 1, dot, dot));
                x += dot + gap;
            }
        }

        const QRectF art_area(r.center().x() - 58, r.top() + 40, 116, 78);
        if (!art.isNull()) {
            const QPixmap scaled = art.scaled(art_area.size().toSize(), Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation);
            p.drawPixmap(QPointF(art_area.center().x() - scaled.width() / 2.0,
                                 art_area.center().y() - scaled.height() / 2.0),
                         scaled);
        }

        // Physical pad name (Xbox / Steam Deck / …).
        QFont nf = font();
        nf.setPixelSize(18);
        nf.setBold(false);
        p.setFont(nf);
        p.setPen(DeckTheme::kText);
        p.drawText(QRectF(r.left() + 10, art_area.bottom() + 10, r.width() - 20, 24),
                   Qt::AlignHCenter, name);

        // Battery: a glyph filled to the charge level (green→amber→red), with a bolt when charging.
        DrawBattery(p, QRectF(r.center().x() - 26, art_area.bottom() + 40, 52, 22));
    }

    /// Draws a battery outline filled proportionally to the current level; overlays a charging bolt
    /// when the pad is on the charger. Centered horizontally in `box`.
    void DrawBattery(QPainter& p, const QRectF& box) {
        const BatteryVisual v = BatteryVisualFor(battery_level);
        const QColor color = battery_charging ? DeckTheme::kAccent : v.color;
        const qreal nub_w = 3;
        QRectF body(box.left(), box.top(), box.width() - nub_w - 2, box.height());
        // shell
        p.setPen(QPen(DeckTheme::kTextDim, 2));
        p.setBrush(Qt::NoBrush);
        QPainterPath shell;
        shell.addRoundedRect(body, 5, 5);
        p.drawPath(shell);
        // nub
        p.setPen(Qt::NoPen);
        p.setBrush(DeckTheme::kTextDim);
        p.drawRoundedRect(QRectF(body.right() + 2, body.center().y() - 5, nub_w, 10), 1.5, 1.5);
        // fill
        const qreal pad = 3;
        const qreal fill_w = (body.width() - 2 * pad) * (battery_charging ? 1.0 : v.fill);
        if (fill_w > 0) {
            p.setBrush(color);
            p.drawRoundedRect(QRectF(body.left() + pad, body.top() + pad, fill_w,
                                     body.height() - 2 * pad),
                              2.5, 2.5);
        }
        if (battery_charging) {
            // A simple lightning bolt centred in the body.
            const QPointF c = body.center();
            QPolygonF bolt;
            bolt << QPointF(c.x() + 2, c.y() - 6) << QPointF(c.x() - 4, c.y() + 1)
                 << QPointF(c.x(), c.y() + 1) << QPointF(c.x() - 2, c.y() + 6)
                 << QPointF(c.x() + 4, c.y() - 1) << QPointF(c.x(), c.y() - 1);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0xff, 0xff, 0xff));
            p.drawPolygon(bolt);
        }
    }

private:
    bool add = false;
    bool listening = false;
    bool selected = false;
    int index = 0;
    QPixmap art;
    QString name;
    Core::HID::NpadBatteryLevel battery_level = Core::HID::NpadBatteryLevel::Full;
    bool battery_charging = false;
};

DeckControllersPage::DeckControllersPage(Core::HID::HIDCore& hid_core_,
                                         InputCommon::InputSubsystem& input_, QWidget* parent)
    : DeckPage(parent), hid_core{hid_core_}, input{input_} {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(48, 34, 48, 20);
    outer->setSpacing(0);

    title = new QLabel(tr("Controllers"), this);
    title->setStyleSheet(
        QStringLiteral("font-size: 32px; font-weight: 500; color: %1;").arg(DeckTheme::kText.name()));
    outer->addWidget(title);

    hint = new QLabel(
        tr("Controllers connect automatically — turn one on and it shows up here. External pads are "
           "the players; the Deck's built-in is used only when no external controller is connected."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("font-size: 19px; color: %1; padding-top: 8px;")
                            .arg(DeckTheme::kTextDim.name()));
    outer->addWidget(hint);
    outer->addSpacing(20);

    auto* grid = new QGridLayout();
    grid->setSpacing(20);
    for (int i = 0; i < kMaxPlayers; ++i) {
        cards[i] = new PlayerCard(this);
        cards[i]->setVisible(false);
        grid->addWidget(cards[i], i / 4, i % 4);
    }
    outer->addLayout(grid, 1);

    mode = new QLabel(this);
    mode->setStyleSheet(QStringLiteral("font-size: 18px; color: %1;").arg(DeckTheme::kTextDim.name()));
    outer->addWidget(mode);

    // Refresh live so a pad turned on (or off) while the screen is open appears (or disappears)
    // without any user action — the reconcile does the actual connecting.
    poll_timer = new QTimer(this);
    poll_timer->setInterval(250);
    connect(poll_timer, &QTimer::timeout, this, &DeckControllersPage::Rebuild);
}

DeckControllersPage::~DeckControllersPage() = default;

void DeckControllersPage::Rebuild() {
    // One card per connected player — a pure status view; there is no "add"/"remove" card.
    connected_count = 0;
    for (int i = 0; i < kMaxPlayers; ++i) {
        auto* c = hid_core.GetEmulatedControllerByIndex(i);
        const bool connected = c != nullptr && c->IsConnected() && HasRealBinding(*c);
        if (connected) {
            // Label the card with the PHYSICAL pad (Xbox / Steam Deck / …) so the user can tell which
            // controller is which player; fall back to the Switch style name if it can't be resolved.
            const std::string phys = FrontendCommon::DescribePlayerController(input, hid_core, i);
            const QString label = phys.empty() ? StyleName(c->GetNpadStyleIndex())
                                               : QString::fromStdString(phys);
            const Core::HID::NpadPowerInfo power = PowerFor(*c);
            cards[connected_count]->SetPlayer(i, ControllerArt(c->GetNpadStyleIndex()), label,
                                              power.battery_level, power.is_charging);
            cards[connected_count]->setVisible(true);
            ++connected_count;
        }
    }
    for (int i = connected_count; i < kMaxPlayers; ++i) {
        cards[i]->setVisible(false);
    }

    if (connected_count == 0) {
        mode->setText(tr("No controller connected — turn one on and it will appear here."));
    } else if (connected_count == 1) {
        mode->setText(tr("1 controller connected."));
    } else {
        mode->setText(tr("%1 controllers connected.").arg(connected_count));
    }
}

void DeckControllersPage::OnActivated() {
    // Just reflect what SDL currently reports (the reconcile connects pads). We must NOT rescan the
    // SDL subsystem here: under Steam that tears down Steam's injected virtual pads and they do not
    // come back, which was popping up the "controller lost" notification and killing input.
    Rebuild();
    poll_timer->start();
    emit HintsChanged();
}

void DeckControllersPage::OnDeactivated() {
    poll_timer->stop();
}

std::vector<DeckHint> DeckControllersPage::Hints() const {
    return {{QStringLiteral("B"), tr("Back")}};
}
