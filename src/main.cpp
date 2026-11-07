#include <giomm/application.h>
#include <gtkmm/application.h>

#include <exception>
#include <memory>

#include "ui/MainWindow.hpp"

int main(int argc, char* argv[])
{
    // A stable GApplication id gives us free single-instance behaviour on
    // systems with a session D-Bus: a second launch activates this instance
    // instead of creating a second process/window.
    auto app = Gtk::Application::create(
        "io.github.aethertrack.AetherTrack",
        Gio::Application::Flags::DEFAULT_FLAGS);

    std::unique_ptr<MainWindow> window;

    app->signal_activate().connect([&app, &window]() {
        if (!window) {
            try {
                window = std::make_unique<MainWindow>();
                app->add_window(*window);
            } catch (const std::exception& error) {
                g_warning("AetherTrack: failed to start: %s", error.what());
                app->quit();
                return;
            }
        }

        // A second launch lands here in the existing process. Make sure the
        // current window is restored from a minimized state and focused.
        window->unminimize();
        window->present();
    });

    return app->run(argc, argv);
}
