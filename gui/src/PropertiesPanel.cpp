/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Didik DW and contributors.
 * See LICENSE, LICENSE_SCOPE.md and NOTICE.md for applicable terms.
 */
#include <cmath>
#include "PropertiesPanel.h"
#include "canvas/ImageItem.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDoubleSpinBox>
#include <QToolButton>
#include <QPushButton>
#include <QLabel>

static QDoubleSpinBox *makeSpin(double lo, double hi, int dec, double step)
{
    auto *s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(dec);
    s->setSingleStep(step);
    s->setKeyboardTracking(false);   // emit only when editing settles
    s->setMinimumWidth(104);
    s->setMaximumWidth(120);
    return s;
}

PropertiesPanel::PropertiesPanel(QWidget *parent) : QWidget(parent)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    m_body = new QWidget(this);
    auto *v = new QVBoxLayout(m_body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    // ── Posisi ──
    auto *posBox = new QGroupBox("Position", m_body);
    auto *posForm = new QFormLayout(posBox);
    m_x = makeSpin(-100000, 100000, 2, 0.1);
    m_y = makeSpin(-100000, 100000, 2, 0.1);
    posForm->addRow("X:", m_x);
    posForm->addRow("Y:", m_y);
    v->addWidget(posBox);

    // ── Ukuran ──
    auto *sizeBox = new QGroupBox("Size", m_body);
    auto *sizeForm = new QFormLayout(sizeBox);
    m_w = makeSpin(0.01, 100000, 2, 0.1);
    m_h = makeSpin(0.01, 100000, 2, 0.1);
    m_pct = makeSpin(1, 5000, 0, 1);
    m_pct->setSuffix(" %");
    m_lock = new QToolButton(sizeBox);
    m_lock->setText("🔒 Lock ratio");
    m_lock->setCheckable(true);
    m_lock->setChecked(true);
    m_lock->setToolTip("Keep W:H ratio when resizing");
    auto *resetBtn = new QPushButton("Reset to original", sizeBox);
    sizeForm->addRow("W:", m_w);
    sizeForm->addRow("H:", m_h);
    sizeForm->addRow("Scale:", m_pct);
    sizeForm->addRow(m_lock);
    sizeForm->addRow(resetBtn);
    v->addWidget(sizeBox);

    // Symbol-only square button helper.
    auto symBtn = [](QWidget *parent, const QString &sym, const QString &tip, bool check) {
        auto *b = new QToolButton(parent);
        b->setText(sym);
        b->setToolTip(tip);
        b->setCheckable(check);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(38, 32);
        b->setStyleSheet("font-size:18px;");
        return b;
    };

    // ── Rotation ──
    auto *rotBox = new QGroupBox("Rotation", m_body);
    auto *rotRow = new QHBoxLayout(rotBox);
    m_rot = makeSpin(0, 360, 1, 1);
    m_rot->setWrapping(true);
    m_rot->setSuffix(" °");
    auto *rotL = symBtn(rotBox, "⟲", "Rotate −90°", false);
    auto *rotR = symBtn(rotBox, "⟳", "Rotate +90°", false);
    rotRow->addWidget(m_rot, 1);
    rotRow->addWidget(rotL);
    rotRow->addWidget(rotR);
    v->addWidget(rotBox);

    // ── Mirror ──
    auto *mirBox = new QGroupBox("Mirror", m_body);
    auto *mirRow = new QHBoxLayout(mirBox);
    m_flipH = symBtn(mirBox, "↔", "Flip horizontal", true);
    m_flipV = symBtn(mirBox, "↕", "Flip vertical", true);
    mirRow->addWidget(m_flipH);
    mirRow->addWidget(m_flipV);
    mirRow->addStretch(1);
    v->addWidget(mirBox);

    root->addWidget(m_body);
    root->addStretch(1);

    // ── Signals ──
    connect(m_x, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyPos);
    connect(m_y, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyPos);
    connect(m_w, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyWidth);
    connect(m_h, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyHeight);
    connect(m_pct, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyPercent);
    connect(m_rot, &QDoubleSpinBox::valueChanged, this, &PropertiesPanel::applyRotation);
    connect(rotL, &QToolButton::clicked, this, [this]() { if (m_item) { m_item->rotateBy(-90); refresh(); } });
    connect(rotR, &QToolButton::clicked, this, [this]() { if (m_item) { m_item->rotateBy(90);  refresh(); } });
    connect(m_flipH, &QToolButton::toggled, this, [this](bool on) { if (!m_updating && m_item) { m_item->setFlipH(on); refresh(); } });
    connect(m_flipV, &QToolButton::toggled, this, [this](bool on) { if (!m_updating && m_item) { m_item->setFlipV(on); refresh(); } });
    connect(resetBtn, &QPushButton::clicked, this, &PropertiesPanel::doReset);

    updateSuffixes();
    bind(nullptr);
}

void PropertiesPanel::setUnit(Unit u)
{
    m_unit = u;
    updateSuffixes();
    refresh();
}

void PropertiesPanel::updateSuffixes()
{
    const QString s = QString(" ") + unitSuffix(m_unit);
    for (QDoubleSpinBox *b : {m_x, m_y, m_w, m_h})
        b->setSuffix(s);
}

void PropertiesPanel::bind(ImageItem *item)
{
    m_item = item;
    m_body->setEnabled(item != nullptr);
    refresh();
}

bool PropertiesPanel::axesSwappedFor(double deg)
{
    // Which of the item's own axes is currently the more horizontal one.
    const double r = std::fmod(std::fabs(deg), 180.0);
    return r > 45.0 && r < 135.0;
}

bool PropertiesPanel::axesSwapped() const
{
    return m_item && axesSwappedFor(m_item->rotationDeg());
}

void PropertiesPanel::refresh()
{
    if (!m_item)
        return;
    const double um = unitMm(m_unit);
    m_updating = true;
    const QPointF tl = m_item->topLeftMm();
    m_x->setValue(tl.x() / um);
    m_y->setValue(tl.y() / um);
    const bool sw = axesSwapped();
    m_w->setValue((sw ? m_item->heightMm() : m_item->widthMm()) / um);
    m_h->setValue((sw ? m_item->widthMm()  : m_item->heightMm()) / um);
    m_pct->setValue(m_item->nativeWmm() > 0 ? m_item->widthMm() / m_item->nativeWmm() * 100.0 : 100.0);
    m_rot->setValue(m_item->rotationDeg());
    m_flipH->setChecked(m_item->flipH());
    m_flipV->setChecked(m_item->flipV());
    m_updating = false;
}

void PropertiesPanel::refreshPosition()
{
    if (!m_item)
        return;
    const double um = unitMm(m_unit);
    m_updating = true;
    const QPointF tl = m_item->topLeftMm();
    m_x->setValue(tl.x() / um);
    m_y->setValue(tl.y() / um);
    m_updating = false;
}

void PropertiesPanel::applyPos()
{
    if (m_updating || !m_item)
        return;
    const double um = unitMm(m_unit);
    m_item->setTopLeftMm(QPointF(m_x->value() * um, m_y->value() * um));
    refreshPosition();   // reflect any snap clamping
}

void PropertiesPanel::applyWidth()
{
    if (m_updating || !m_item)
        return;
    const double um = unitMm(m_unit);
    // The field is the horizontal extent on the bed; under a quarter turn that
    // is the item's own height.
    const bool sw = axesSwapped();
    const double target = m_w->value() * um;
    double driven = sw ? m_item->heightMm() : m_item->widthMm();
    double other  = sw ? m_item->widthMm()  : m_item->heightMm();
    if (m_lock->isChecked() && driven > 0)
        other = target * (other / driven);
    driven = target;
    m_item->setSizeMm(sw ? other : driven, sw ? driven : other);
    refresh();
}

void PropertiesPanel::applyHeight()
{
    if (m_updating || !m_item)
        return;
    const double um = unitMm(m_unit);
    const bool sw = axesSwapped();
    const double target = m_h->value() * um;
    double driven = sw ? m_item->widthMm()  : m_item->heightMm();
    double other  = sw ? m_item->heightMm() : m_item->widthMm();
    if (m_lock->isChecked() && driven > 0)
        other = target * (other / driven);
    driven = target;
    m_item->setSizeMm(sw ? driven : other, sw ? other : driven);
    refresh();
}

void PropertiesPanel::applyPercent()
{
    if (m_updating || !m_item)
        return;
    const double f = m_pct->value() / 100.0;   // relative to native, preserves aspect
    m_item->setSizeMm(m_item->nativeWmm() * f, m_item->nativeHmm() * f);
    refresh();
}

void PropertiesPanel::applyRotation()
{
    if (m_updating || !m_item)
        return;
    m_item->setRotationDeg(m_rot->value());
    // Full refresh, not just the position: crossing a quarter turn swaps which
    // item axis the Width/Height fields drive, so their values must swap too.
    refresh();
}

void PropertiesPanel::doReset()
{
    if (!m_item)
        return;
    m_item->setFlipH(false);
    m_item->setFlipV(false);
    m_item->setRotationDeg(0);
    m_item->setSizeMm(m_item->nativeWmm(), m_item->nativeHmm());
    refresh();
}
