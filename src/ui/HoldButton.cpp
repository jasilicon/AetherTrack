#include "ui/HoldButton.hpp"

#include <gdk/gdk.h>
#include <gdkmm/rectangle.h>

#include <cairomm/context.h>
#include <cairomm/pattern.h>

#include <glibmm/main.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
constexpr double kHoldSeconds = 1.5;
constexpr int kTickMilliseconds = 16;

void rounded_rect(const Cairo::RefPtr<Cairo::Context>& cr,
                  double x,
                  double y,
                  double width,
                  double height,
                  double radius)
{
    const double r = std::min(radius, std::min(width, height) / 2.0);
    const double pi = std::numbers::pi_v<double>;

    cr->begin_new_sub_path();
    cr->arc(x + width - r, y + r, r, -pi / 2.0, 0.0);
    cr->arc(x + width - r, y + height - r, r, 0.0, pi / 2.0);
    cr->arc(x + r, y + height - r, r, pi / 2.0, pi);
    cr->arc(x + r, y + r, r, pi, 3.0 * pi / 2.0);
    cr->close_path();
}
} // namespace

HoldButton::HoldButton()
{
    add_css_class("hold-button");
    set_has_frame(false);
    set_size_request(-1, 46);

    m_label.set_text("Hold to Reset");
    m_label.add_css_class("hold-label");
    set_child(m_label);

    m_click = Gtk::GestureClick::create();
    m_click->set_button(GDK_BUTTON_PRIMARY);
    m_click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    m_click->signal_pressed().connect(
        [this](int n_press, double x, double y) { on_pressed(n_press, x, y); });
    m_click->signal_released().connect(
        [this](int n_press, double x, double y) { on_released(n_press, x, y); });
    add_controller(m_click);

    // GestureClick receives the release even when the pointer leaves, but
    // cancelling on leave gives immediate visual feedback and avoids a reset
    // being armed by a drag-away gesture.
    m_motion = Gtk::EventControllerMotion::create();
    m_motion->signal_leave().connect([this]() { cancel_hold(); });
    add_controller(m_motion);

    m_focus = Gtk::EventControllerFocus::create();
    m_focus->signal_leave().connect([this]() { cancel_hold(); });
    add_controller(m_focus);
}

HoldButton::~HoldButton()
{
    m_tick.disconnect();
}

void HoldButton::reset_progress()
{
    m_tick.disconnect();
    m_progress = 0.0;
    m_holding = false;
    m_completed = false;
    queue_draw();
}

sigc::signal<void()>& HoldButton::signal_hold_completed()
{
    return m_hold_completed;
}

void HoldButton::on_pressed(int n_press, double, double)
{
    if (n_press != 1 || m_holding || m_completed) {
        return;
    }

    // Claim the sequence before GtkButton's own gesture sees it. The button is
    // a pure hold target; it never emits a normal click.
    m_click->set_state(Gtk::EventSequenceState::CLAIMED);

    m_tick.disconnect();
    m_holding = true;
    m_completed = false;
    m_progress = 0.0;
    m_hold_started = std::chrono::steady_clock::now();
    queue_draw();

    m_tick = Glib::signal_timeout().connect([this]() { return on_tick(); },
                                            kTickMilliseconds);
}

void HoldButton::on_released(int n_press, double, double)
{
    if (n_press != 1) {
        return;
    }

    m_click->set_state(Gtk::EventSequenceState::CLAIMED);

    if (m_completed) {
        return;
    }

    cancel_hold();
}

void HoldButton::cancel_hold()
{
    if (!m_holding || m_completed) {
        return;
    }

    m_tick.disconnect();
    m_holding = false;
    m_progress = 0.0;
    queue_draw();
}

bool HoldButton::on_tick()
{
    if (!m_holding) {
        return false;
    }

    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - m_hold_started).count();
    m_progress = std::clamp(elapsed / kHoldSeconds, 0.0, 1.0);
    queue_draw();

    if (m_progress >= 1.0) {
        m_tick.disconnect();
        m_holding = false;
        m_completed = true;
        m_hold_completed.emit();
        return false;
    }

    return true;
}

void HoldButton::snapshot_vfunc(const Glib::RefPtr<Gtk::Snapshot>& snapshot)
{
    const int width = get_width();
    const int height = get_height();

    if (width <= 0 || height <= 0) {
        Gtk::Button::snapshot_vfunc(snapshot);
        return;
    }

    auto cr = snapshot->append_cairo(Gdk::Rectangle(0, 0, width, height));
    if (cr) {
        const double w = static_cast<double>(width);
        const double h = static_cast<double>(height);
        const double radius = h / 2.0;

        rounded_rect(cr, 0.0, 0.0, w, h, radius);
        cr->set_source_rgba(0.10, 0.11, 0.16, 0.60);
        cr->fill();

        if (m_progress > 0.0) {
            cr->save();
            rounded_rect(cr, 0.0, 0.0, w, h, radius);
            cr->clip();
            cr->begin_new_path();
            cr->rectangle(0.0, 0.0, w * m_progress, h);

            auto gradient = Cairo::LinearGradient::create(0.0, 0.0, w, 0.0);
            gradient->add_color_stop_rgba(0.0, 0.94, 0.27, 0.23, 0.55);
            gradient->add_color_stop_rgba(1.0, 0.98, 0.57, 0.20, 0.72);
            cr->set_source(gradient);
            cr->fill();
            cr->restore();
        }

        rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, radius);
        cr->set_line_width(1.0);
        cr->set_source_rgba(1.0, 1.0, 1.0, 0.16);
        cr->stroke();
    }

    // The label is drawn by the base class above the Cairo fill.
    Gtk::Button::snapshot_vfunc(snapshot);
}
