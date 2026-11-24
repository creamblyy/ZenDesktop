/*
This file is part of Telegram Desktop fork - ZenDesktop (Material You helpers).
Tonal surface derivation inspired by material-foundation/material-color-utilities.
*/
#pragma once

#include <QtGui/QColor>
#include <algorithm>
#include <cmath>

namespace Relay::M3 {

[[nodiscard]] inline QColor Blend(const QColor &base, const QColor &overlay, float alpha) {
	const auto a = std::clamp(alpha, 0.0f, 1.0f);
	const auto inv = 1.0f - a;
	return QColor::fromRgbF(
		base.redF() * inv + overlay.redF() * a,
		base.greenF() * inv + overlay.greenF() * a,
		base.blueF() * inv + overlay.blueF() * a,
		base.alphaF());
}

[[nodiscard]] inline QColor SurfaceContainer(const QColor &bg, const QColor &fg, const QColor &accent = QColor()) {
	auto c = Blend(bg, fg, 0.055f);
	if (accent.isValid()) {
		c = Blend(c, accent, 0.04f);
	}
	return c;
}

[[nodiscard]] inline QColor SurfaceContainerHigh(const QColor &bg, const QColor &fg, const QColor &accent = QColor()) {
	auto c = Blend(bg, fg, 0.09f);
	if (accent.isValid()) {
		c = Blend(c, accent, 0.06f);
	}
	return c;
}

[[nodiscard]] inline QColor OutlineVariant(const QColor &fg) {
	auto c = fg;
	c.setAlphaF(0.11);
	return c;
}

[[nodiscard]] inline QColor PrimaryContainer(const QColor &bg, const QColor &active) {
	return Blend(bg, active, 0.16f);
}

} // namespace Relay::M3
