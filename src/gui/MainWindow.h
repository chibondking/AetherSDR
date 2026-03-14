#pragma once

#include "models/RadioModel.h"
#include "core/RadioDiscovery.h"
#include "core/AudioEngine.h"

#include <QMainWindow>
#include <QLabel>
#include <QStatusBar>

namespace AetherSDR {

class ConnectionPanel;
class SpectrumWidget;
class AppletPanel;
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // Radio/connection events
    void onConnectionStateChanged(bool connected);
    void onConnectionError(const QString& msg);
    void onSliceAdded(SliceModel* slice);
    void onSliceRemoved(int id);

    // Spectrum click-to-tune
    void onFrequencyChanged(double mhz);

private:
    void buildUI();
    void buildMenuBar();
    void applyDarkTheme();
    SliceModel* activeSlice() const;

    // Core objects
    RadioDiscovery m_discovery;
    RadioModel     m_radioModel;
    AudioEngine    m_audio;

    // GUI — left sidebar
    ConnectionPanel* m_connPanel{nullptr};

    // GUI — main area
    SpectrumWidget*  m_spectrum{nullptr};

    // GUI — right applet panel
    AppletPanel*     m_appletPanel{nullptr};

    // Status bar labels
    QLabel* m_connStatusLabel{nullptr};
    QLabel* m_radioInfoLabel{nullptr};

    // Guard: set true while updating controls from the model, so that
    // onFrequencyChanged doesn't echo the change back to the radio.
    bool m_updatingFromModel{false};
};

} // namespace AetherSDR
