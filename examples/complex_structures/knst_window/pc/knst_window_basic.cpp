/*
  knst_window_basic.cpp

  Simplest possible knst_window example — one window that prints events.
  Shows keyboard press/release/repeat, modifier combos, mouse buttons
  and scroll, and file drag-and-drop.

  Close with ESC or the X button.
*/

// On Wayland, drawing is required for the window to appear at all.
// Since this example doesn't draw, the program runs but shows nothing.

#include "../../../../include/KernelNucleusT.hpp"
#include <iostream>


// Runs once per frame. Walks the event queue and prints whatever
// happened since the last call.
KNST_FORCE_INLINE static void on_frame(knst_window& w, void* /*user_data*/) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const knst_window_event& m_event = w.get_window_event_handle(i);

        if (m_event.type == KNST_WINDOW_EVENT_KEYBOARD) {
            const bool shift = (m_event.mods & KNST_WINDOW_MOD_SHIFT) != 0;
            const bool ctrl = (m_event.mods & KNST_WINDOW_MOD_CONTROL) != 0;
            const bool alt = (m_event.mods & KNST_WINDOW_MOD_ALT) != 0;

            if (m_event.key_action == KNST_WINDOW_KEY_ACTION_PRESS) {
                if (m_event.key_code == KNST_WINDOW_KEY_CODE_T) {
                    std::cout << "T pressed\n";
                }
                else if (m_event.key_code == KNST_WINDOW_KEY_CODE_C_CEDILLA) {
                    std::cout << "C-cedilla pressed\n";
                }
                else if (ctrl && !shift && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_C) {
                    std::cout << "Ctrl + C (copy)\n";
                }
                else if (ctrl && !shift && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_V) {
                    std::cout << "Ctrl + V (paste)\n";
                }
                else if (ctrl && shift && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_S) {
                    std::cout << "Ctrl + Shift + S (save as)\n";
                }
                else if (m_event.key_code == KNST_WINDOW_KEY_CODE_NUMPAD_DIVIDE) {
                    std::cout << "/ (numpad) pressed\n";
                }
                else if (m_event.key_code == KNST_WINDOW_KEY_CODE_ESCAPE) {
                    std::cout << "ESC pressed — closing\n";
                    w.should_close();
                }
            }
            else if (m_event.key_action == KNST_WINDOW_KEY_ACTION_RELEASE) {
                if (m_event.key_code == KNST_WINDOW_KEY_CODE_T) {
                    std::cout << "T released\n";
                }
                else if (m_event.key_code == KNST_WINDOW_KEY_CODE_C_CEDILLA) {
                    std::cout << "C-cedilla released\n";
                }
            }
            else if (m_event.key_action == KNST_WINDOW_KEY_ACTION_REPEAT) {
                // Auto-repeat — the OS keeps firing PRESS for held keys
                // (or we synthesize it for platforms that don't). Handy
                // for movement in a game.
                if (!ctrl && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_W)
                    std::cout << "W repeating (move forward)\n";
                else if (!ctrl && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_A)
                    std::cout << "A repeating (move left)\n";
                else if (!ctrl && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_S)
                    std::cout << "S repeating (move backward)\n";
                else if (!ctrl && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_D)
                    std::cout << "D repeating (move right)\n";
            }
        }
        else if (m_event.type == KNST_WINDOW_EVENT_MOUSE) {
            if (m_event.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS) {
                if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                    std::cout << "Left mouse button pressed at ("  << m_event.mouse_x << ", " << m_event.mouse_y << ")\n";
                }
                else if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_RIGHT) {
                    std::cout << "Right mouse button pressed\n";
                }
                else if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_MIDDLE) {
                    std::cout << "Middle mouse button pressed\n";
                }
            }
            else if (m_event.mouse_action == KNST_WINDOW_MOUSE_ACTION_RELEASE) {
                if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                    std::cout << "Left mouse button released\n";
                }
                else if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_RIGHT) {
                    std::cout << "Right mouse button released\n";
                }
            }
            else if (m_event.mouse_action == KNST_WINDOW_MOUSE_ACTION_SCROLL) {
                std::cout << "Mouse scrolled: " << (m_event.mouse_scroll_delta > 0 ? "up" : "down") << "\n";
            }
        }
        else if (m_event.type == KNST_WINDOW_EVENT_FILE_DROP_ENTER) {
            std::cout << "Drag entered window\n";
        }
        else if (m_event.type == KNST_WINDOW_EVENT_FILE_DROP_LEAVE) {
            std::cout << "Drag left window\n";
        }
        else if (m_event.type == KNST_WINDOW_EVENT_FILE_DROP_MOVE) {
            std::cout << "Dragging over window\n";
        }
        else if (m_event.type == KNST_WINDOW_EVENT_FILE_DROP) {
            std::cout << "Files dropped: " << m_event.drop_count << "\n";
            if (m_event.drop_files) {
                for (uint32_t j = 0; j < m_event.drop_count; ++j) {
                    std::cout << "  [" << j << "] " << (*m_event.drop_files)[j] << "\n";
                }
            }
        }
    }

    // The event ring is fixed size — not clearing it drops new events
    // once it fills up.
    w.clear_events();
}


int main() {
    KnstWindowSources::Init();

    knst_window window(800, 800, "Knst Window Basic");
    window.set_redraw_callback(on_frame);
    window.creation_and_show();
    window.set_drag_drop_status(true);

    // Main loop: drain OS events, then run the frame callback.
    //
    // call_redraw_callback() runs present_pre() + your draw code +
    // present_post() under the hood, so the backend sync (X11 counter,
    // Wayland ack_configure) is already handled. You only see your
    // own callback.
    while (!window.is_should_close()) {
        knst_window_event_system::non_block_pool_event();
        window.call_redraw_callback();

        // Yield so the CPU isn't pinned and the WM gets a chance to
        // process events.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    window.destroy();
    KnstWindowSources::CleanUp();
    std::cout << "Closed.\n";

    return 0;
}