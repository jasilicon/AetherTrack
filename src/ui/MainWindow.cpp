#include "ui/MainWindow.hpp"

#include <gtk/gtk.h>
#include <gtkmm/csssection.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/styleprovider.h>

#include <gdk/gdkkeysyms.h>
#include <gdkmm/toplevel.h>
#include <glib.h>
#include <glibmm/datetime.h>
#include <glibmm/error.h>
#include <glibmm/fileutils.h>
#include <glibmm/main.h>
#include <glibmm/miscutils.h>

#ifdef AETHER_HAVE_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
constexpr std::array<std::string_view, 6> kPalette = {
    "#60A5FA",
    "#F97316",
    "#34D399",
    "#A78BFA",
    "#F472B6",
    "#FBBF24",
};

std::string first_existing(const std::vector<std::string>& candidates)
{
    for (const auto& path : candidates) {
        if (Glib::file_test(path, Glib::FileTest::EXISTS)) {
            return path;
        }
    }
    return {};
}

std::string trim_copy(std::string value)
{
    const auto not_space = [](unsigned char c) { return std::isspace(c) == 0; };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(),
                value.end());
    return value;
}

std::string format_hours(int64_t total_seconds)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << static_cast<double>(total_seconds) / 3600.0;
    return out.str();
}

Glib::DateTime local_midnight(const Glib::DateTime& date_time)
{
    return Glib::DateTime::create_local(date_time.get_year(),
                                        date_time.get_month(),
                                        date_time.get_day_of_month(),
                                        0,
                                        0,
                                        0.0);
}

std::string format_session_time(int64_t epoch)
{
    return Glib::DateTime::create_now_local(epoch)
        .format("%b %d \u00B7 %H:%M")
        .raw();
}
} // namespace

MainWindow::MainWindow()
{
    set_title("AetherTrack");
    setup_icon();
    set_default_size(340, 460);
    set_size_request(340, 460);
    set_resizable(false);
    set_decorated(false);

    // The toplevel itself stays fully transparent; all visual weight lives
    // on the .glass-deck child so the compositor can blend it correctly.
    add_css_class("aether-window");

    m_drag_surface.set_hexpand(true);
    m_drag_surface.set_vexpand(true);
    m_drag_surface.set_child(m_root);
    set_child(m_drag_surface);

    m_root.add_css_class("glass-deck");
    m_root.set_size_request(340, 460);
    m_root.set_spacing(0);

    build_header();
    build_deck();
    build_pill();

    m_root.append(m_titlebar);
    m_root.append(m_deck);
    m_root.append(m_pill);
    m_pill.set_visible(false);

    load_stylesheet();
    initialize_data();

    // The UI tick is deliberately absent here. It is connected on demand by
    // sync_tick_timer(), only while a session is running and the window is
    // actually visible. All timing math uses persisted wall-clock epochs, so
    // nothing is lost while the callback is disconnected.
    signal_map().connect([this]() {
        attach_toplevel_monitor();
        refresh_ui();
        if (m_insights_open) {
            refresh_insights();
        }
        sync_tick_timer();
    });
    signal_unmap().connect([this]() { sync_tick_timer(); });
    property_is_active().signal_changed().connect([this]() {
        if (is_active()) {
            refresh_ui();
            sync_tick_timer();
        }
    });

    sync_tick_timer();
}

MainWindow::~MainWindow()
{
    m_tick_connection.disconnect();
    m_focus_idle.disconnect();
    m_always_on_top_idle.disconnect();
    m_toplevel_state_connection.disconnect();

    if (m_add_popover.get_parent() != nullptr) {
        m_add_popover.unparent();
    }
    if (m_reset_note_popover.get_parent() != nullptr) {
        m_reset_note_popover.unparent();
    }
}

void MainWindow::build_header()
{
    m_titlebar.add_css_class("app-titlebar");
    m_titlebar.set_spacing(8);

    m_title.set_text("AetherTrack");
    m_title.add_css_class("app-title");
    m_title.set_xalign(0.0F);
    m_title.set_hexpand(true);

    m_pill_collapse.set_label("\u25BE");
    m_pill_collapse.add_css_class("window-control");
    m_pill_collapse.set_tooltip_text("Micro pill mode");
    m_pill_collapse.signal_clicked().connect([this]() { set_mode(true); });

    m_minimize.set_label("\u2013");
    m_minimize.add_css_class("window-control");
    m_minimize.set_tooltip_text("Minimize");
    m_minimize.signal_clicked().connect([this]() { minimize(); });

    m_close.set_label("\u2715");
    m_close.add_css_class("window-control");
    m_close.add_css_class("close");
    m_close.set_tooltip_text("Close");
    m_close.signal_clicked().connect([this]() { close(); });

    m_controls.set_spacing(2);
    m_controls.append(m_pill_collapse);
    m_controls.append(m_minimize);
    m_controls.append(m_close);

    m_titlebar.append(m_title);
    m_titlebar.append(m_controls);
}

void MainWindow::build_deck()
{
    m_deck.add_css_class("deck");
    m_deck.set_vexpand(true);

    m_activity_row.add_css_class("activity-row");

    m_activity_model = Gtk::StringList::create({});
    m_activity_dropdown.set_model(m_activity_model);
    m_activity_dropdown.add_css_class("activity-dropdown");
    m_activity_dropdown.set_hexpand(true);
    m_activity_dropdown.set_valign(Gtk::Align::CENTER);
    m_activity_dropdown.set_tooltip_text("Select activity");
    m_activity_dropdown.property_selected().signal_changed().connect(
        [this]() { on_activity_selected(); });

    m_add_skill.set_label("+");
    m_add_skill.add_css_class("add-skill");
    m_add_skill.set_tooltip_text("Add activity");
    m_add_skill.signal_clicked().connect([this]() { show_add_skill_popover(); });

    m_activity_row.append(m_activity_dropdown);
    m_activity_row.append(m_add_skill);

    m_timer.set_text("00:00:00");
    m_timer.add_css_class("timer");
    m_timer.set_halign(Gtk::Align::CENTER);
    m_timer.set_valign(Gtk::Align::CENTER);
    m_timer.set_vexpand(true);

    m_lifetime.set_text("Lifetime Total: 0.0 hrs");
    m_lifetime.add_css_class("lifetime");
    m_lifetime.set_halign(Gtk::Align::CENTER);

    m_start_pause.set_label("Start");
    m_start_pause.add_css_class("primary-action");
    m_start_pause.set_hexpand(true);
    m_start_pause.set_halign(Gtk::Align::FILL);
    m_start_pause.set_tooltip_text("Start or pause the selected activity");
    m_start_pause.signal_clicked().connect([this]() { on_start_pause(); });

    m_trim.set_label("-15m Trim");
    m_trim.add_css_class("secondary-action");
    m_trim.set_hexpand(true);
    m_trim.set_halign(Gtk::Align::FILL);
    m_trim.set_tooltip_text("Remove 15 minutes from the current run");
    m_trim.signal_clicked().connect([this]() { on_trim(); });

    m_reset.set_hexpand(true);
    m_reset.set_halign(Gtk::Align::FILL);
    m_reset.set_tooltip_text("Click and hold for 1.5 seconds to reset");
    m_reset.signal_hold_completed().connect(
        [this]() { on_reset_hold_complete(); });

    m_actions.set_spacing(8);
    m_actions.append(m_start_pause);
    m_actions.append(m_trim);
    m_actions.append(m_reset);

    m_deck.append(m_activity_row);
    m_deck.append(m_timer);
    m_deck.append(m_lifetime);
    m_deck.append(m_actions);
    build_insights();

    // Add-activity popover -----------------------------------------------
    m_add_popover.add_css_class("add-popover");
    m_add_popover.set_autohide(true);
    m_add_popover.set_has_arrow(true);

    m_add_form.add_css_class("popover-form");

    m_add_title.set_text("New activity");
    m_add_title.add_css_class("popover-title");
    m_add_title.set_xalign(0.0F);

    m_add_entry.set_placeholder_text("e.g. Deep Work");
    m_add_entry.set_width_chars(18);
    m_add_entry.signal_activate().connect([this]() { add_skill(); });

    m_add_error.add_css_class("form-error");
    m_add_error.set_xalign(0.0F);
    m_add_error.set_visible(false);

    m_add_cancel.set_label("Cancel");
    m_add_cancel.add_css_class("popover-action");
    m_add_cancel.signal_clicked().connect([this]() { m_add_popover.popdown(); });

    m_add_confirm.set_label("Add");
    m_add_confirm.add_css_class("popover-action");
    m_add_confirm.add_css_class("primary");
    m_add_confirm.signal_clicked().connect([this]() { add_skill(); });

    m_add_buttons.set_halign(Gtk::Align::END);
    m_add_buttons.append(m_add_cancel);
    m_add_buttons.append(m_add_confirm);

    m_add_form.append(m_add_title);
    m_add_form.append(m_add_entry);
    m_add_form.append(m_add_error);
    m_add_form.append(m_add_buttons);

    m_add_popover.set_child(m_add_form);
    m_add_popover.set_parent(m_add_skill);
    m_add_popover.signal_closed().connect([this]() {
        m_add_error.set_visible(false);
        m_add_entry.set_text("");
    });

    auto add_key = Gtk::EventControllerKey::create();
    add_key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    add_key->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) {
            if (keyval == GDK_KEY_Escape) {
                m_add_popover.popdown();
                return true;
            }
            return false;
        },
        false);
    m_add_entry.add_controller(add_key);

    // Reset-note popover -------------------------------------------------
    m_reset_note_popover.add_css_class("note-popover");
    m_reset_note_popover.set_autohide(true);
    m_reset_note_popover.set_has_arrow(true);

    m_reset_form.add_css_class("popover-form");

    m_reset_entry.set_placeholder_text("Optional milestone note\u2026");
    m_reset_entry.set_width_chars(22);
    m_reset_entry.signal_activate().connect(
        [this]() { confirm_reset(m_reset_entry.get_text()); });

    m_reset_hint.set_text("Enter to save \u00B7 Esc to skip");
    m_reset_hint.add_css_class("form-hint");
    m_reset_hint.set_xalign(0.0F);

    m_reset_form.append(m_reset_entry);
    m_reset_form.append(m_reset_hint);

    m_reset_note_popover.set_child(m_reset_form);
    m_reset_note_popover.set_parent(m_reset);
    m_reset_note_popover.signal_closed().connect([this]() {
        m_reset_entry.set_text("");
        m_reset.reset_progress();
    });

    auto reset_key = Gtk::EventControllerKey::create();
    reset_key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    reset_key->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType) {
            if (keyval == GDK_KEY_Escape) {
                confirm_reset(Glib::ustring());
                return true;
            }
            return false;
        },
        false);
    m_reset_entry.add_controller(reset_key);
}

void MainWindow::build_insights()
{
    m_insights_toggle.set_label("Insights & History \u25BE");
    m_insights_toggle.add_css_class("insights-toggle");
    m_insights_toggle.set_hexpand(true);
    m_insights_toggle.set_tooltip_text("Show mastery, heatmap and session history");
    m_insights_toggle.signal_clicked().connect([this]() { toggle_insights(); });

    m_insights.add_css_class("insights-panel");
    m_insights.set_spacing(8);

    m_heatmap_title.set_text("Last 90 days");
    m_heatmap_title.add_css_class("insights-section-title");
    m_heatmap_title.set_xalign(0.0F);

    m_history_title.set_text("Recent sessions");
    m_history_title.add_css_class("insights-section-title");
    m_history_title.set_xalign(0.0F);

    m_history_scroll.add_css_class("history-scroll");
    m_history_scroll.set_policy(Gtk::PolicyType::NEVER,
                                Gtk::PolicyType::AUTOMATIC);
    m_history_scroll.set_size_request(-1, 94);
    m_history_scroll.set_child(m_history_list);

    m_history_list.add_css_class("history-list");
    m_history_list.set_selection_mode(Gtk::SelectionMode::NONE);
    m_history_list.set_show_separators(false);

    m_insights.append(m_mastery);
    m_insights.append(m_heatmap_title);
    m_insights.append(m_heatmap);
    m_insights.append(m_history_title);
    m_insights.append(m_history_scroll);

    m_insights.set_visible(false);

    m_deck.append(m_insights_toggle);
    m_deck.append(m_insights);
}

void MainWindow::build_pill()
{
    m_pill.add_css_class("micro-pill");
    m_pill.set_spacing(6);
    m_pill.set_vexpand(true);
    m_pill.set_valign(Gtk::Align::FILL);

    m_pill_name.add_css_class("pill-name");
    m_pill_name.set_hexpand(true);
    m_pill_name.set_xalign(0.0F);
    m_pill_name.set_ellipsize(Pango::EllipsizeMode::END);

    m_pill_sep_1.set_text("|");
    m_pill_sep_1.add_css_class("pill-sep");

    m_pill_timer.set_text("00:00:00");
    m_pill_timer.add_css_class("pill-timer");

    m_pill_sep_2.set_text("|");
    m_pill_sep_2.add_css_class("pill-sep");

    m_pill_toggle.set_label("Play");
    m_pill_toggle.add_css_class("pill-control");
    m_pill_toggle.set_size_request(42, -1);
    m_pill_toggle.set_tooltip_text("Pause / play");
    m_pill_toggle.signal_clicked().connect([this]() { on_start_pause(); });

    m_pill_expand.set_label("\u25A2");
    m_pill_expand.add_css_class("pill-control");
    m_pill_expand.set_tooltip_text("Expand deck");
    m_pill_expand.signal_clicked().connect([this]() { set_mode(false); });

    m_pill.append(m_pill_name);
    m_pill.append(m_pill_sep_1);
    m_pill.append(m_pill_timer);
    m_pill.append(m_pill_sep_2);
    m_pill.append(m_pill_toggle);
    m_pill.append(m_pill_expand);
}

void MainWindow::initialize_data()
{
    m_skills = m_database.getAllSkills();
    if (m_skills.empty()) {
        (void)m_database.createSkill("Focus", kPalette.front());
    }

    m_skills = m_database.getAllSkills();
    if (m_skills.empty()) {
        g_warning("AetherTrack: no activities could be loaded");
        return;
    }

    reload_skills(m_skills.front().id);
}

void MainWindow::reload_skills(int64_t select_id)
{
    m_skills = m_database.getAllSkills();
    if (m_skills.empty()) {
        m_engine.reset();
        refresh_ui();
        return;
    }

    std::vector<Glib::ustring> names;
    names.reserve(m_skills.size());

    std::size_t selected_index = 0;
    for (std::size_t i = 0; i < m_skills.size(); ++i) {
        names.emplace_back(m_skills[i].name);
        if (m_skills[i].id == select_id) {
            selected_index = i;
        }
    }

    m_updating_activity = true;
    m_activity_model->splice(0, m_activity_model->get_n_items(), names);
    m_activity_dropdown.set_selected(static_cast<guint>(selected_index));
    m_updating_activity = false;

    m_selected_skill_id = m_skills[selected_index].id;
    m_engine = std::make_unique<TimerEngine>(m_database, m_selected_skill_id);
    refresh_ui();
    if (m_insights_open) {
        refresh_insights();
    }
}

void MainWindow::on_activity_selected()
{
    if (m_updating_activity) {
        return;
    }

    const guint selected = m_activity_dropdown.get_selected();
    if (selected >= m_skills.size()) {
        return;
    }

    const int64_t skill_id = m_skills[selected].id;
    if (skill_id == m_selected_skill_id) {
        return;
    }

    select_skill(skill_id);
}

void MainWindow::select_skill(int64_t skill_id)
{
    m_selected_skill_id = skill_id;
    m_engine = std::make_unique<TimerEngine>(m_database, skill_id);
    refresh_ui();
    if (m_insights_open) {
        refresh_insights();
    }
}

void MainWindow::refresh_ui()
{
    if (!m_engine) {
        sync_tick_timer();
        return;
    }

    const int64_t current = m_engine->currentRunSeconds();
    const std::string timer_text = TimerEngine::formatDuration(current);
    const bool running = m_engine->isRunning();

    if (m_timer.get_text().raw() != timer_text) {
        m_timer.set_text(timer_text);
    }
    if (m_pill_timer.get_text().raw() != timer_text) {
        m_pill_timer.set_text(timer_text);
    }
    update_timer_font(timer_text.size());

    const Glib::ustring action = running ? "Pause" : "Start";
    if (m_start_pause.get_label() != action) {
        m_start_pause.set_label(action);
    }
    if (m_start_pause.has_css_class("running") != running) {
        if (running) {
            m_start_pause.add_css_class("running");
        } else {
            m_start_pause.remove_css_class("running");
        }
    }

    if (m_trim.get_visible() != !running) {
        m_trim.set_visible(!running);
    }

    const Glib::ustring skill_name(m_engine->skill().name);
    if (m_pill_name.get_text() != skill_name) {
        m_pill_name.set_text(skill_name);
    }

    const Glib::ustring toggle = running ? "Pause" : "Play";
    if (m_pill_toggle.get_label() != toggle) {
        m_pill_toggle.set_label(toggle);
    }

    update_lifetime_label(current);
    sync_tick_timer();
}

void MainWindow::refresh_insights()
{
    if (!m_engine) {
        return;
    }

    const Skill& skill = m_engine->skill();
    const int64_t lifetime =
        skill.lifetime_reset_seconds + m_engine->currentRunSeconds();
    m_mastery.set_total_seconds(lifetime);
    m_mastery.set_accent(skill.color_hex);
    m_heatmap.set_accent(skill.color_hex);

    const Glib::DateTime today = Glib::DateTime::create_now_local();
    const int weekday = today.get_day_of_week();
    const Glib::DateTime week_start =
        local_midnight(today).add_days(-(weekday - 1));
    const Glib::DateTime grid_start = week_start.add_days(-12 * 7);

    const std::vector<HistoryEntry> history =
        m_database.getHistorySince(skill.id, grid_start.to_unix());

    std::unordered_map<int, std::size_t> day_index;
    day_index.reserve(91);
    std::vector<int64_t> daily(91, 0);
    const int64_t now = today.to_unix();

    for (int i = 0; i < 91; ++i) {
        const Glib::DateTime day = grid_start.add_days(i);
        if (day.to_unix() > now) {
            daily[static_cast<std::size_t>(i)] = -1;
        }
        const int key = day.get_year() * 1000 + day.get_day_of_year();
        day_index[key] = static_cast<std::size_t>(i);
    }

    for (const HistoryEntry& entry : history) {
        const Glib::DateTime date =
            Glib::DateTime::create_now_local(entry.ended_at_epoch);
        const int key = date.get_year() * 1000 + date.get_day_of_year();
        const auto it = day_index.find(key);
        if (it != day_index.end() && daily[it->second] >= 0) {
            daily[it->second] += entry.duration_seconds;
        }
    }

    m_heatmap.set_days(std::move(daily));

    while (Gtk::Widget* child = m_history_list.get_first_child()) {
        m_history_list.remove(*child);
    }

    if (history.empty()) {
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_selectable(false);
        row->add_css_class("history-empty-row");

        auto* empty = Gtk::make_managed<Gtk::Label>("No sessions yet");
        empty->add_css_class("history-empty");
        empty->set_xalign(0.0F);
        row->set_child(*empty);
        m_history_list.append(*row);
        return;
    }

    for (const HistoryEntry& entry : history) {
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_selectable(false);
        row->add_css_class("history-row");

        auto* content = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* text = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
        text->set_hexpand(true);

        auto* when = Gtk::make_managed<Gtk::Label>(
            format_session_time(entry.ended_at_epoch));
        when->add_css_class("history-date");
        when->set_xalign(0.0F);
        text->append(*when);

        if (!entry.note.empty()) {
            auto* note = Gtk::make_managed<Gtk::Label>(entry.note);
            note->add_css_class("history-note");
            note->set_xalign(0.0F);
            note->set_ellipsize(Pango::EllipsizeMode::END);
            note->set_max_width_chars(28);
            text->append(*note);
        }

        auto* duration = Gtk::make_managed<Gtk::Label>(
            TimerEngine::formatDuration(entry.duration_seconds));
        duration->add_css_class("history-duration");
        duration->set_xalign(1.0F);
        duration->set_valign(Gtk::Align::START);

        content->append(*text);
        content->append(*duration);
        row->set_child(*content);
        m_history_list.append(*row);
    }
}

void MainWindow::sync_tick_timer()
{
    const bool should_tick = m_engine && m_engine->isRunning() &&
                             get_visible() && get_mapped() &&
                             !is_toplevel_minimized();

    if (should_tick == m_tick_connection.connected()) {
        return;
    }

    if (should_tick) {
        m_tick_connection = Glib::signal_timeout().connect_seconds(
            [this]() {
                refresh_ui();
                return true;
            },
            1);
    } else {
        m_tick_connection.disconnect();
    }
}

void MainWindow::attach_toplevel_monitor()
{
    if (m_toplevel_state_connection.connected()) {
        return;
    }

    auto toplevel = std::dynamic_pointer_cast<Gdk::Toplevel>(get_surface());
    if (!toplevel) {
        return;
    }

    // Wayland does not necessarily unmap a minimized surface, so watch the
    // toplevel state as well as map/unmap to keep the UI tick disconnected.
    m_toplevel_state_connection =
        toplevel->property_state().signal_changed().connect([this]() {
            refresh_ui();
            sync_tick_timer();
        });
}

bool MainWindow::is_toplevel_minimized() const
{
    auto toplevel = std::dynamic_pointer_cast<const Gdk::Toplevel>(get_surface());
    if (!toplevel) {
        return false;
    }

    const auto state = toplevel->get_state();
    return (static_cast<unsigned>(state) &
            static_cast<unsigned>(Gdk::Toplevel::State::MINIMIZED)) != 0;
}

void MainWindow::update_timer_font(std::size_t length)
{
    const bool compact = length > 9 && length <= 12;
    const bool tight = length > 12;

    if (m_timer.has_css_class("timer-compact") != compact) {
        if (compact) {
            m_timer.add_css_class("timer-compact");
        } else {
            m_timer.remove_css_class("timer-compact");
        }
    }

    if (m_timer.has_css_class("timer-tight") != tight) {
        if (tight) {
            m_timer.add_css_class("timer-tight");
        } else {
            m_timer.remove_css_class("timer-tight");
        }
    }
}

void MainWindow::update_lifetime_label(int64_t current_seconds)
{
    if (!m_engine) {
        return;
    }

    const int64_t lifetime =
        m_engine->skill().lifetime_reset_seconds + current_seconds;
    const std::string text = "Lifetime Total: " + format_hours(lifetime) + " hrs";

    if (m_lifetime.get_text().raw() != text) {
        m_lifetime.set_text(text);
    }

    m_mastery.set_total_seconds(lifetime);
    m_mastery.set_accent(m_engine->skill().color_hex);
}

void MainWindow::on_start_pause()
{
    if (!m_engine) {
        return;
    }

    try {
        if (m_engine->isRunning()) {
            m_engine->pause();
        } else {
            m_engine->start();
        }
    } catch (const std::exception& error) {
        g_warning("AetherTrack: could not update timer state: %s", error.what());
    }

    refresh_ui();
}

void MainWindow::on_trim()
{
    if (!m_engine || m_engine->isRunning()) {
        return;
    }

    try {
        m_engine->trimSeconds(15 * 60);
    } catch (const std::exception& error) {
        g_warning("AetherTrack: could not trim timer: %s", error.what());
    }

    refresh_ui();
}

void MainWindow::on_reset_hold_complete()
{
    if (!m_engine) {
        m_reset.reset_progress();
        return;
    }

    m_add_popover.popdown();
    m_reset_entry.set_text("");
    m_reset_note_popover.popup();

    m_focus_idle.disconnect();
    m_focus_idle = Glib::signal_idle().connect(
        [this]() {
            m_reset_entry.grab_focus();
            return false;
        });
}

void MainWindow::confirm_reset(const Glib::ustring& note)
{
    const std::string note_text = trim_copy(note.raw());
    m_reset_note_popover.popdown();

    if (!m_engine) {
        m_reset.reset_progress();
        return;
    }

    try {
        m_engine->resetToHistory(note_text);
    } catch (const std::exception& error) {
        g_warning("AetherTrack: could not reset activity: %s", error.what());
    }

    refresh_ui();
    if (m_insights_open) {
        refresh_insights();
    }
    m_reset.reset_progress();
}

void MainWindow::show_add_skill_popover()
{
    m_reset_note_popover.popdown();
    m_add_error.set_visible(false);
    m_add_entry.set_text("");
    m_add_popover.popup();

    m_focus_idle.disconnect();
    m_focus_idle = Glib::signal_idle().connect(
        [this]() {
            m_add_entry.grab_focus();
            return false;
        });
}

void MainWindow::add_skill()
{
    const std::string name = trim_copy(m_add_entry.get_text().raw());
    if (name.empty()) {
        m_add_error.set_text("Enter an activity name");
        m_add_error.set_visible(true);
        return;
    }

    try {
        const std::string_view color = kPalette[m_skills.size() % kPalette.size()];
        const int64_t id = m_database.createSkill(name, color);
        m_add_popover.popdown();
        m_add_entry.set_text("");
        reload_skills(id);
    } catch (const std::exception& error) {
        m_add_error.set_text(error.what());
        m_add_error.set_visible(true);
    }
}

void MainWindow::toggle_insights()
{
    set_insights_open(!m_insights_open);
}

void MainWindow::set_insights_open(bool open)
{
    if (m_insights_open == open) {
        return;
    }

    m_insights_open = open;
    if (open) {
        refresh_insights();
    }

    m_insights.set_visible(open);
    m_insights_toggle.set_label(open ? "Hide Insights \u25B4"
                                     : "Insights & History \u25BE");
    m_insights_toggle.set_tooltip_text(
        open ? "Hide mastery, heatmap and session history"
             : "Show mastery, heatmap and session history");

    if (m_pill_mode) {
        return;
    }

    const int height = open ? 804 : 460;
    m_root.set_size_request(340, height);
    set_size_request(340, height);
    set_default_size(340, height);
    queue_resize();
}

void MainWindow::set_mode(bool pill)
{
    if (m_pill_mode == pill) {
        return;
    }

    m_pill_mode = pill;
    m_add_popover.popdown();
    m_reset_note_popover.popdown();

    if (pill) {
        set_insights_open(false);
    }

    m_titlebar.set_visible(!pill);
    m_deck.set_visible(!pill);
    m_pill.set_visible(pill);

    const int width = pill ? 240 : 340;
    const int height = pill ? 48 : 460;
    m_root.set_size_request(width, height);
    set_size_request(width, height);
    set_default_size(width, height);
    queue_resize();

    // X11 exposes _NET_WM_STATE_ABOVE. Wayland intentionally gives clients no
    // portable always-on-top request, so this is a best-effort hint.
    m_always_on_top_idle.disconnect();
    m_always_on_top_idle = Glib::signal_idle().connect(
        [this, pill]() {
            apply_always_on_top(pill);
            return false;
        });
}

void MainWindow::apply_always_on_top(bool enabled)
{
#ifdef AETHER_HAVE_X11
    auto surface = get_surface();
    if (!surface) {
        return;
    }

    GdkSurface* gdk_surface = surface->gobj();
    if (!GDK_IS_X11_SURFACE(gdk_surface)) {
        return;
    }

    GdkDisplay* gdk_display = gdk_surface_get_display(gdk_surface);
    if (!GDK_IS_X11_DISPLAY(gdk_display)) {
        return;
    }

    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display* xdisplay = gdk_x11_display_get_xdisplay(gdk_display);
    const ::Window xid = gdk_x11_surface_get_xid(gdk_surface);
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (xdisplay == nullptr || xid == 0) {
        return;
    }

    const Atom wm_state = XInternAtom(xdisplay, "_NET_WM_STATE", False);
    const Atom above = XInternAtom(xdisplay, "_NET_WM_STATE_ABOVE", False);

    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = xid;
    event.xclient.message_type = wm_state;
    event.xclient.format = 32;
    event.xclient.data.l[0] = enabled ? 1 : 0;
    event.xclient.data.l[1] = static_cast<long>(above);
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 1;

    XSendEvent(xdisplay,
               DefaultRootWindow(xdisplay),
               False,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &event);
    XFlush(xdisplay);
#else
    (void)enabled;
#endif
}

void MainWindow::setup_icon()
{
    property_icon_name() = "io.github.aethertrack.AetherTrack";

    const auto display = get_display();
    if (!display) {
        return;
    }

    const auto theme = Gtk::IconTheme::get_for_display(display);
    if (!theme) {
        return;
    }

    std::vector<std::string> candidates;

    // AppImage/portable bundles can point at their own icon directory.
    if (const char* icon_dir = g_getenv("AETHER_ICON_DIR");
        icon_dir && *icon_dir) {
        candidates.emplace_back(icon_dir);
    }
#ifdef AETHER_SOURCE_ICON_DIR
    candidates.emplace_back(AETHER_SOURCE_ICON_DIR);
#endif

    for (const auto& path : candidates) {
        if (Glib::file_test(path, Glib::FileTest::IS_DIR)) {
            theme->add_search_path(path);
        }
    }
}

void MainWindow::load_stylesheet()
{
    const auto display = get_display();
    if (!display) {
        g_warning("AetherTrack: no GDK display available for CSS");
        return;
    }

    std::vector<std::string> candidates;

    // AppImage/portable bundles can point at their own asset directory.
    if (const char* asset_dir = g_getenv("AETHER_ASSET_DIR");
        asset_dir && *asset_dir) {
        candidates.emplace_back(Glib::build_filename(asset_dir, "style.css"));
    }

#ifdef AETHER_SOURCE_ASSET_DIR
    candidates.emplace_back(Glib::build_filename(AETHER_SOURCE_ASSET_DIR, "style.css"));
#endif
#ifdef AETHER_INSTALL_ASSET_DIR
    candidates.emplace_back(Glib::build_filename(AETHER_INSTALL_ASSET_DIR, "style.css"));
#endif
    candidates.emplace_back("assets/style.css");

    const std::string path = first_existing(candidates);
    if (path.empty()) {
        g_warning("AetherTrack: assets/style.css not found; using theme defaults");
        return;
    }

    m_css = Gtk::CssProvider::create();
    m_css->signal_parsing_error().connect(
        [](const Glib::RefPtr<const Gtk::CssSection>&, const Glib::Error& error) {
            g_warning("AetherTrack: CSS parse error: %s", error.what());
        });

    m_css->load_from_path(path);

    Gtk::StyleProvider::add_provider_for_display(
        display,
        m_css,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}
