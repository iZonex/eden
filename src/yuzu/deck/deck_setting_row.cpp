// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QSpinBox>

#include "yuzu/configuration/shared_widget.h"
#include "yuzu/deck/deck_setting_row.h"
#include "yuzu/deck/deck_theme.h"

DeckSettingRow::DeckSettingRow(ConfigurationShared::Widget* source_, QString label_,
                               QString description_, QWidget* parent)
    : QFrame(parent), source{source_}, label{std::move(label_)},
      description{std::move(description_)} {
    // Taller than a plain row so the setting's description (Switch shows one under each item) fits
    // as a second line; rows without a description just leave that space empty and read fine.
    setFixedHeight(description.isEmpty() ? 68 : 86);
    // The Builder control is our value model only; never shown.
    if (source != nullptr) {
        source->hide();
    }
}

bool DeckSettingRow::IsToggle() const {
    // A pure bool row exposes only a checkbox.
    return source != nullptr && source->checkbox != nullptr && source->combobox == nullptr &&
           source->slider == nullptr && source->spinbox == nullptr &&
           source->double_spinbox == nullptr;
}

bool DeckSettingRow::ToggleOn() const {
    return source != nullptr && source->checkbox != nullptr && source->checkbox->isChecked();
}

QString DeckSettingRow::ValueText() const {
    if (source == nullptr) {
        return {};
    }
    if (source->combobox != nullptr) {
        return source->combobox->currentText();
    }
    if (source->spinbox != nullptr) {
        return source->spinbox->text();
    }
    if (source->double_spinbox != nullptr) {
        return source->double_spinbox->text();
    }
    if (source->slider != nullptr) {
        return QString::number(source->slider->value());
    }
    if (source->line_edit != nullptr) {
        return source->line_edit->text();
    }
    return {};
}

void DeckSettingRow::SetFocused(bool focused_) {
    focused = focused_;
    update();
}

void DeckSettingRow::Adjust(int delta) {
    if (source == nullptr || delta == 0) {
        return;
    }
    if (IsToggle()) {
        source->checkbox->setChecked(delta > 0);
    } else if (source->combobox != nullptr) {
        const int count = source->combobox->count();
        const int next = source->combobox->currentIndex() + delta;
        if (count > 0 && next >= 0 && next < count) {
            source->combobox->setCurrentIndex(next);
        }
    } else if (source->slider != nullptr) {
        source->slider->setValue(source->slider->value() +
                                 delta * std::max(1, source->slider->singleStep()));
    } else if (source->spinbox != nullptr) {
        source->spinbox->setValue(source->spinbox->value() + delta * source->spinbox->singleStep());
    } else if (source->double_spinbox != nullptr) {
        source->double_spinbox->setValue(source->double_spinbox->value() +
                                         delta * source->double_spinbox->singleStep());
    }
    update();
}

void DeckSettingRow::Activate() {
    if (source == nullptr) {
        return;
    }
    if (IsToggle()) {
        source->checkbox->toggle();
    } else if (source->combobox != nullptr) {
        const int count = source->combobox->count();
        if (count > 0) {
            source->combobox->setCurrentIndex((source->combobox->currentIndex() + 1) % count);
        }
    }
    update();
}

void DeckSettingRow::mousePressEvent(QMouseEvent*) {
    if (on_tapped) {
        on_tapped();
    }
}

void DeckSettingRow::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), DeckTheme::kBackground); // custom paint: stylesheet bg isn't drawn for us

    const QRectF r = rect();
    const int pad = 22;

    const QColor name_color = DeckTheme::kText;
    const QColor value_color = DeckTheme::kAccent; // Switch shows setting values in cyan

    if (focused) {
        // Switch highlights the focused row with a cyan glowing rounded border over a dark-teal wash.
        const QRectF box = r.adjusted(3, 5, -3, -5);
        for (int s = 6; s >= 1; --s) {
            QPainterPath glow;
            glow.addRoundedRect(box.adjusted(-s, -s, s, s), 10 + s, 10 + s);
            QColor c = DeckTheme::kAccentGlow;
            c.setAlpha(6 + (6 - s) * 7);
            painter.fillPath(glow, c);
        }
        QPainterPath fill;
        fill.addRoundedRect(box, 10, 10);
        painter.fillPath(fill, DeckTheme::kAccentSoft);
        painter.setPen(QPen(DeckTheme::kAccent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(box, 10, 10);
    } else {
        // Thin divider at the bottom.
        painter.setPen(DeckTheme::kDivider);
        painter.drawLine(QPointF(r.left() + pad, r.bottom()),
                         QPointF(r.right() - pad, r.bottom()));
    }

    // With a description, the name sits in the upper part of the row and the description below it;
    // the value control aligns to the name's vertical centre (not the taller row's centre).
    const bool has_desc = !description.isEmpty();
    const qreal name_cy = has_desc ? r.top() + 26 : r.center().y();

    // Name.
    QFont name_font = font();
    name_font.setPixelSize(21);
    painter.setFont(name_font);
    painter.setPen(name_color);
    const QRectF name_rect(r.left() + pad, name_cy - 16, r.width() * 0.58 - pad, 32);
    painter.drawText(name_rect, Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(label, Qt::ElideRight,
                                                      static_cast<int>(name_rect.width())));

    // Description: a dim second line under the name, wrapped to the row width (Switch shows a short
    // explanation for each setting so it never feels like a bare list).
    if (has_desc) {
        QFont desc_font = font();
        desc_font.setPixelSize(15);
        painter.setFont(desc_font);
        painter.setPen(DeckTheme::kTextDim);
        const QRectF desc_rect(r.left() + pad, r.top() + 44, r.width() - 2 * pad, r.height() - 48);
        painter.drawText(desc_rect, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap,
                         painter.fontMetrics().elidedText(description, Qt::ElideRight,
                                                          static_cast<int>(desc_rect.width()) * 2));
    }

    // Value on the right — either a toggle pill or text with arrows.
    if (IsToggle()) {
        const bool on = ToggleOn();
        const int pw = 52;
        const int ph = 28;
        const QRectF pill(r.right() - pad - pw, name_cy - ph / 2.0, pw, ph);
        painter.setPen(Qt::NoPen);
        painter.setBrush(on ? DeckTheme::kAccent : DeckTheme::kToggleOff);
        painter.drawRoundedRect(pill, ph / 2.0, ph / 2.0);
        const qreal knob = ph - 6;
        const qreal kx = on ? pill.right() - knob - 3 : pill.left() + 3;
        painter.setBrush(Qt::white);
        painter.drawEllipse(QRectF(kx, pill.top() + 3, knob, knob));
    } else {
        QFont value_font = font();
        value_font.setPixelSize(20);
        value_font.setBold(focused);
        painter.setFont(value_font);
        const QString value = ValueText();
        const QRectF value_rect(r.left() + r.width() * 0.5, name_cy - 16, r.width() * 0.5 - pad, 32);

        if (focused) {
            // Draw ‹ value › to signal it can be changed with Left/Right.
            painter.setPen(DeckTheme::kAccent);
            painter.drawText(value_rect, Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("‹"));
            painter.drawText(value_rect, Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("›"));
            painter.setPen(value_color);
            painter.drawText(value_rect.adjusted(20, 0, -20, 0), Qt::AlignVCenter | Qt::AlignRight,
                             value);
        } else {
            painter.setPen(value_color);
            painter.drawText(value_rect, Qt::AlignVCenter | Qt::AlignRight,
                             painter.fontMetrics().elidedText(
                                 value, Qt::ElideRight, static_cast<int>(value_rect.width())));
        }
    }
}
