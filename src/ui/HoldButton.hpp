#pragma once

#include <gtkmm/button.h>
#include <gtkmm/eventcontrollerfocus.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/label.h>
#include <gtkmm/snapshot.h>

#include <glibmm/refptr.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <chrono>

// A button that only fires after the user keeps the primary mouse button
// pressed for 1.5 seconds. The fill is drawn directly with Cairo so it stays
// smooth even on compositors that throttle CSS animations.
class HoldButton : public Gtk::Button
{
public:
    HoldButton();
    ~HoldButton() override;

    HoldButton(const HoldButton&) = delete;
    HoldButton& operator=(const HoldButton&) = delete;

    void reset_progress();

    [[nodiscard]] sigc::signal<void()>& signal_hold_completed();

protected:
    void snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot) override;

private:
    void on_pressed(int n_press, double x, double y);
    void on_released(int n_press, double x, double y);
    void cancel_hold();
    bool on_tick();

    Gtk::Label m_label;
    Glib::RefPtr<Gtk::GestureClick> m_click;
    Glib::RefPtr<Gtk::EventControllerMotion> m_motion;
    Glib::RefPtr<Gtk::EventControllerFocus> m_focus;
    sigc::connection m_tick;
    sigc::signal<void()> m_hold_completed;

    std::chrono::steady_clock::time_point m_hold_started;
    double m_progress{0.0};
    bool m_holding{false};
    bool m_completed{false};
};
