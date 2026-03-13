#include "SpectrumWidget.h"

#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <cmath>
#include <cstring>

namespace AetherSDR {

SpectrumWidget::SpectrumWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setCursor(Qt::CrossCursor);
}

void SpectrumWidget::setFrequencyRange(double centerMhz, double bandwidthMhz)
{
    m_centerMhz    = centerMhz;
    m_bandwidthMhz = bandwidthMhz;
    update();
}

void SpectrumWidget::setDbmRange(float minDbm, float maxDbm)
{
    // Spectrum (FFT plot) uses the full radio range.
    m_refLevel     = maxDbm;
    m_dynamicRange = maxDbm - minDbm;
    // Waterfall colour range stays at its own focused defaults
    // (m_wfMinDbm / m_wfMaxDbm) for better contrast.
    update();
}

void SpectrumWidget::setSliceFrequency(double freqMhz)
{
    m_sliceFreqMhz = freqMhz;
    update();
}

void SpectrumWidget::setSliceFilter(int lowHz, int highHz)
{
    m_filterLowHz  = lowHz;
    m_filterHighHz = highHz;
    update();
}

void SpectrumWidget::updateSpectrum(const QVector<float>& binsDbm)
{
    if (m_smoothed.size() != binsDbm.size())
        m_smoothed = binsDbm;
    else {
        for (int i = 0; i < binsDbm.size(); ++i)
            m_smoothed[i] = SMOOTH_ALPHA * binsDbm[i] + (1.0f - SMOOTH_ALPHA) * m_smoothed[i];
    }
    m_bins = binsDbm;

    // Only use FFT for waterfall if no native waterfall tiles are arriving.
    if (!m_hasNativeWaterfall && !m_waterfall.isNull())
        pushWaterfallRow(binsDbm, m_waterfall.width());

    update();
}

void SpectrumWidget::updateWaterfallRow(const QVector<float>& binsDbm)
{
    m_hasNativeWaterfall = true;
    if (!m_waterfall.isNull())
        pushWaterfallRow(binsDbm, m_waterfall.width());
    update();
}

// ─── Layout helpers ────────────────────────────────────────────────────────────

int SpectrumWidget::mhzToX(double mhz) const
{
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    return static_cast<int>((mhz - startMhz) / m_bandwidthMhz * width());
}

// ─── Mouse ────────────────────────────────────────────────────────────────────

// Snap a frequency (MHz) to the nearest multiple of m_stepHz.
static double snapToStep(double mhz, int stepHz)
{
    if (stepHz <= 0) return mhz;
    const double stepMhz = stepHz / 1e6;
    return std::round(mhz / stepMhz) * stepMhz;
}

void SpectrumWidget::mousePressEvent(QMouseEvent* ev)
{
    // Only tune if click is in the panadapter area, not the freq scale bar.
    if (ev->position().y() >= height() - FREQ_SCALE_H) return;

    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double rawMhz = startMhz + (ev->position().x() / width()) * m_bandwidthMhz;
    emit frequencyClicked(snapToStep(rawMhz, m_stepHz));
}

void SpectrumWidget::wheelEvent(QWheelEvent* ev)
{
    // Only scroll-tune over the panadapter area, not the freq scale bar.
    if (ev->position().y() >= height() - FREQ_SCALE_H) {
        ev->ignore();
        return;
    }
    const int ticks = ev->angleDelta().y() / 120;   // +1 per notch up, -1 per notch down
    if (ticks == 0) { ev->ignore(); return; }

    const double newMhz = snapToStep(m_sliceFreqMhz + ticks * m_stepHz / 1e6, m_stepHz);
    emit frequencyClicked(newMhz);
    ev->accept();
}

// ─── Resize ───────────────────────────────────────────────────────────────────

void SpectrumWidget::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);

    const int contentH = height() - FREQ_SCALE_H;
    const int wfHeight = static_cast<int>(contentH * (1.0f - SPECTRUM_FRAC));
    if (wfHeight > 0 && width() > 0) {
        QImage newWf(width(), wfHeight, QImage::Format_RGB32);
        newWf.fill(Qt::black);
        if (!m_waterfall.isNull())
            newWf = m_waterfall.scaled(width(), wfHeight, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        m_waterfall = newWf;
    }
}

// ─── Colour map ───────────────────────────────────────────────────────────────

QRgb SpectrumWidget::dbmToRgb(float dbm) const
{
    // Normalise into [0, 1] over the waterfall display range.
    const float t = qBound(0.0f, (dbm - m_wfMinDbm) / (m_wfMaxDbm - m_wfMinDbm), 1.0f);

    // Multi-stop gradient: black → blue → cyan → green → yellow → red
    struct Stop { float pos; int r, g, b; };
    static constexpr Stop stops[] = {
        {0.00f,   0,   0,   0},   // black  (noise floor)
        {0.15f,   0,   0, 128},   // dark blue
        {0.30f,   0,  64, 255},   // blue
        {0.45f,   0, 200, 255},   // cyan
        {0.60f,   0, 220,   0},   // green
        {0.80f, 255, 255,   0},   // yellow
        {1.00f, 255,   0,   0},   // red     (strong signal)
    };
    static constexpr int N = sizeof(stops) / sizeof(stops[0]);

    // Find the two stops bracketing t and interpolate.
    int i = 0;
    while (i < N - 2 && stops[i + 1].pos < t) ++i;
    const float seg = (t - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
    const int r = static_cast<int>(stops[i].r + seg * (stops[i + 1].r - stops[i].r));
    const int g = static_cast<int>(stops[i].g + seg * (stops[i + 1].g - stops[i].g));
    const int b = static_cast<int>(stops[i].b + seg * (stops[i + 1].b - stops[i].b));
    return qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}

// ─── Waterfall update ─────────────────────────────────────────────────────────

void SpectrumWidget::pushWaterfallRow(const QVector<float>& bins, int destWidth)
{
    if (m_waterfall.isNull() || destWidth <= 0) return;

    const int h = m_waterfall.height();
    if (h <= 1) return;

    uchar* bits = m_waterfall.bits();
    const qsizetype bpl = m_waterfall.bytesPerLine();
    std::memmove(bits + bpl, bits, static_cast<size_t>(bpl) * (h - 1));

    auto* row = reinterpret_cast<QRgb*>(bits);
    for (int x = 0; x < destWidth; ++x) {
        const int binIdx = x * bins.size() / destWidth;
        const float dbm  = (binIdx < bins.size()) ? bins[binIdx] : m_wfMinDbm;
        row[x] = dbmToRgb(dbm);
    }
}

// ─── Paint ────────────────────────────────────────────────────────────────────

void SpectrumWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const int contentH = height() - FREQ_SCALE_H;
    const int specH    = static_cast<int>(contentH * SPECTRUM_FRAC);
    const int wfH      = contentH - specH;

    const QRect specRect (0, 0,     width(), specH);
    const QRect wfRect   (0, specH, width(), wfH);
    const QRect scaleRect(0, contentH, width(), FREQ_SCALE_H);

    p.fillRect(specRect, QColor(0x0a, 0x0a, 0x14));

    drawGrid(p, specRect);
    drawSpectrum(p, specRect);
    drawWaterfall(p, wfRect);
    drawSliceOverlay(p, specRect, wfRect);
    drawFreqScale(p, scaleRect);
}

// ─── Grid ─────────────────────────────────────────────────────────────────────

void SpectrumWidget::drawGrid(QPainter& p, const QRect& r)
{
    const int w = r.width();
    const int h = r.height();

    // Horizontal dB lines every 20 dB
    const int steps = static_cast<int>(m_dynamicRange / 20.0f);
    for (int i = 0; i <= steps; ++i) {
        const int y = r.top() + static_cast<int>(h * i / static_cast<float>(steps));
        p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1, Qt::DotLine));
        p.drawLine(0, y, w, y);

        const float dbm = m_refLevel - (m_dynamicRange * i / steps);
        p.setPen(QColor(0x40, 0x60, 0x70));
        p.drawText(2, y + 12, QString("%1").arg(static_cast<int>(dbm)));
    }

    // Vertical frequency grid lines (no labels — scale bar handles those)
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;
    const double stepMhz  = 0.050;
    const double firstLine = std::ceil(startMhz / stepMhz) * stepMhz;

    p.setPen(QPen(QColor(0x20, 0x30, 0x40), 1, Qt::DotLine));
    for (double f = firstLine; f <= endMhz; f += stepMhz)
        p.drawLine(mhzToX(f), r.top(), mhzToX(f), r.bottom());
}

// ─── Spectrum line ────────────────────────────────────────────────────────────

void SpectrumWidget::drawSpectrum(QPainter& p, const QRect& r)
{
    if (m_smoothed.isEmpty()) {
        p.setPen(QColor(0x00, 0x60, 0x80));
        p.drawText(r, Qt::AlignCenter, "No panadapter data — waiting for radio stream");
        return;
    }

    const int w = r.width();
    const int h = r.height();
    const int n = m_smoothed.size();

    QPainterPath path;
    bool first = true;

    for (int i = 0; i < n; ++i) {
        const float dbm  = m_smoothed[i];
        const float norm = qBound(0.0f, (m_refLevel - dbm) / m_dynamicRange, 1.0f);
        const int   x    = r.left() + static_cast<int>(static_cast<float>(i) / n * w);
        const int   y    = r.top()  + qMin(static_cast<int>(norm * h), h - 1);

        if (first) { path.moveTo(x, y); first = false; }
        else        path.lineTo(x, y);
    }

    path.lineTo(r.right(), r.bottom());
    path.lineTo(r.left(),  r.bottom());
    path.closeSubpath();

    QLinearGradient grad(0, r.top(), 0, r.bottom());
    grad.setColorAt(0.0, QColor(0x00, 0xe5, 0xff, 200));
    grad.setColorAt(1.0, QColor(0x00, 0x40, 0x60,  60));

    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillPath(path, grad);
    p.setPen(QPen(QColor(0x00, 0xe5, 0xff), 1.5));
    p.drawPath(path);
    p.setRenderHint(QPainter::Antialiasing, false);
}

// ─── Waterfall ────────────────────────────────────────────────────────────────

void SpectrumWidget::drawWaterfall(QPainter& p, const QRect& r)
{
    if (m_waterfall.isNull()) {
        p.fillRect(r, Qt::black);
        return;
    }
    p.drawImage(r, m_waterfall);
}

// ─── Slice overlay (filter passband + center line) ────────────────────────────

void SpectrumWidget::drawSliceOverlay(QPainter& p, const QRect& specRect, const QRect& wfRect)
{
    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;
    if (m_sliceFreqMhz < startMhz || m_sliceFreqMhz > endMhz) return;

    const double filterLowMhz  = m_sliceFreqMhz + m_filterLowHz  / 1.0e6;
    const double filterHighMhz = m_sliceFreqMhz + m_filterHighHz / 1.0e6;

    const int sliceX   = mhzToX(m_sliceFreqMhz);
    const int filterX1 = mhzToX(filterLowMhz);
    const int filterX2 = mhzToX(filterHighMhz);
    const int filterW  = filterX2 - filterX1;

    // ── Filter passband shading ──────────────────────────────────────────────

    // Spectrum: slightly brighter fill so the passband stands out over the FFT
    p.fillRect(QRect(filterX1, specRect.top(), filterW, specRect.height()),
               QColor(0x00, 0x80, 0xff, 35));

    // Waterfall: subtle overlay so colour map is still readable
    p.fillRect(QRect(filterX1, wfRect.top(), filterW, wfRect.height()),
               QColor(0x00, 0x60, 0xe0, 25));

    // Filter edge lines (spectrum only — would clutter the waterfall)
    p.setPen(QPen(QColor(0x00, 0x90, 0xff, 130), 1));
    p.drawLine(filterX1, specRect.top(), filterX1, specRect.bottom());
    p.drawLine(filterX2, specRect.top(), filterX2, specRect.bottom());

    // ── Slice center line ────────────────────────────────────────────────────

    p.setPen(QPen(QColor(0xff, 0xa0, 0x00, 220), 1.5));
    p.drawLine(sliceX, specRect.top(), sliceX, wfRect.bottom());

    // Triangle marker at top of spectrum
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0xff, 0xa0, 0x00));
    QPolygon tri;
    tri << QPoint(sliceX - 6, specRect.top())
        << QPoint(sliceX + 6, specRect.top())
        << QPoint(sliceX, specRect.top() + 10);
    p.drawPolygon(tri);
}

// ─── Frequency scale bar ──────────────────────────────────────────────────────

void SpectrumWidget::drawFreqScale(QPainter& p, const QRect& r)
{
    p.fillRect(r, QColor(0x06, 0x06, 0x10));

    const double startMhz = m_centerMhz - m_bandwidthMhz / 2.0;
    const double endMhz   = m_centerMhz + m_bandwidthMhz / 2.0;

    // Pick a step that gives roughly 5–10 labels across the visible bandwidth.
    // Candidate steps in MHz; pick the first that gives ≥ 5 divisions.
    static constexpr double kSteps[] = {
        0.010, 0.025, 0.050, 0.100, 0.200, 0.500, 1.000
    };
    double stepMhz = 0.050;
    for (double s : kSteps) {
        if (m_bandwidthMhz / s >= 5.0) { stepMhz = s; break; }
    }

    const double firstLine = std::ceil(startMhz / stepMhz) * stepMhz;

    QFont f = p.font();
    f.setPointSize(8);
    p.setFont(f);
    const QFontMetrics fm(f);

    for (double freq = firstLine; freq <= endMhz; freq += stepMhz) {
        const int x = mhzToX(freq);

        // Tick mark
        p.setPen(QColor(0x40, 0x60, 0x80));
        p.drawLine(x, r.top(), x, r.top() + 4);

        // Label: show MHz with enough decimal places for the step size
        const int decimals = (stepMhz < 0.050) ? 4 : 3;
        const QString label = QString::number(freq, 'f', decimals);
        const int tw = fm.horizontalAdvance(label);
        const int lx = qBound(0, x - tw / 2, width() - tw);

        p.setPen(QColor(0x70, 0x90, 0xb0));
        p.drawText(lx, r.bottom() - 2, label);
    }
}

} // namespace AetherSDR
