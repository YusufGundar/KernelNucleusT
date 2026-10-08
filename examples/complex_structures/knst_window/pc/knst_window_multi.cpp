// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0


/*
  knst_window_multi.cpp

  Two knst_window instances running side by side.

  Each window has its own callback. The "tag" ("A" / "B") just tells
  us which window an event came from in the console output.

  Close each window with ESC or its X button.
*/

// In Linux Wayland, you are required to draw, but this example doesn't involve any drawing, so the program runs but doesn't display anything.

#include "../../../../include/KernelNucleusT.hpp"
#include <iostream>


// Shared dispatcher — same logic for both windows, only the tag differs.
KNST_FORCE_INLINE static void handle_window_events(knst_window& w, const char* tag) {
    for (size_t i = 0; i < w.event_count(); ++i) {
        const knst_window_event& m_event = w.get_window_event_handle(i);

        // Close request (X button)
        if (m_event.type == KNST_WINDOW_EVENT_CLOSE) {
            std::cout << "[" << tag << "] close requested\n";
            w.should_close();
            w.destroy();
            break;
        }

        // Keyboard
        if (m_event.type == KNST_WINDOW_EVENT_KEYBOARD) {
            const bool shift = (m_event.mods & KNST_WINDOW_MOD_SHIFT)   != 0;
            const bool ctrl  = (m_event.mods & KNST_WINDOW_MOD_CONTROL) != 0;
            const bool alt   = (m_event.mods & KNST_WINDOW_MOD_ALT)     != 0;

            if (m_event.key_action == KNST_WINDOW_KEY_ACTION_PRESS) {
                if (m_event.key_code == KNST_WINDOW_KEY_CODE_T) {
                    std::cout << "[" << tag << "] T pressed\n";
                }
                else if (m_event.key_code == KNST_WINDOW_KEY_CODE_C_CEDILLA) {
                    std::cout << "[" << tag << "] C-cedilla pressed\n";
                }
                else if (ctrl && !shift && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_C) {
                    std::cout << "[" << tag << "] Ctrl + C (copy)\n";
                }
                else if (ctrl && !shift && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_V) {
                    std::cout << "[" << tag << "] Ctrl + V (paste)\n";
                }
                else if (ctrl && shift && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_S) {
                    std::cout << "[" << tag << "] Ctrl + Shift + S (save as)\n";
                }
                else if (m_event.key_code == KNST_WINDOW_KEY_CODE_NUMPAD_DIVIDE) {
                    std::cout << "[" << tag << "] / (numpad) pressed\n";
                }
                else if (m_event.key_code == KNST_WINDOW_KEY_CODE_ESCAPE) {
                    std::cout << "[" << tag << "] ESC pressed\n";
                    w.should_close();
                    w.destroy();
                    break;
                }
            }
            else if (m_event.key_action == KNST_WINDOW_KEY_ACTION_RELEASE) {
                if (m_event.key_code == KNST_WINDOW_KEY_CODE_T) {
                    std::cout << "[" << tag << "] T released\n";
                }
                else if (m_event.key_code == KNST_WINDOW_KEY_CODE_C_CEDILLA) {
                    std::cout << "[" << tag << "] C-cedilla released\n";
                }
            }
            else if (m_event.key_action == KNST_WINDOW_KEY_ACTION_REPEAT) {
                if (!ctrl && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_W)
                    std::cout << "[" << tag << "] W repeating (move forward)\n";
                else if (!ctrl && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_A)
                    std::cout << "[" << tag << "] A repeating (move left)\n";
                else if (!ctrl && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_S)
                    std::cout << "[" << tag << "] S repeating (move backward)\n";
                else if (!ctrl && !alt && m_event.key_code == KNST_WINDOW_KEY_CODE_D)
                    std::cout << "[" << tag << "] D repeating (move right)\n";
            }
        }
        // Mouse
        else if (m_event.type == KNST_WINDOW_EVENT_MOUSE) {
            if (m_event.mouse_action == KNST_WINDOW_MOUSE_ACTION_PRESS) {
                if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                    std::cout << "[" << tag << "] Left mouse button pressed at ("
                              << m_event.mouse_x << ", " << m_event.mouse_y << ")\n";
                }
                else if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_RIGHT) {
                    std::cout << "[" << tag << "] Right mouse button pressed\n";
                }
                else if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_MIDDLE) {
                    std::cout << "[" << tag << "] Middle mouse button pressed\n";
                }
            }
            else if (m_event.mouse_action == KNST_WINDOW_MOUSE_ACTION_RELEASE) {
                if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_LEFT) {
                    std::cout << "[" << tag << "] Left mouse button released\n";
                }
                else if (m_event.mouse_button == KNST_WINDOW_MOUSE_BUTTON_RIGHT) {
                    std::cout << "[" << tag << "] Right mouse button released\n";
                }
            }
            else if (m_event.mouse_action == KNST_WINDOW_MOUSE_ACTION_SCROLL) {
                std::cout << "[" << tag << "] Mouse scrolled: "
                          << (m_event.mouse_scroll_delta > 0 ? "up" : "down") << "\n";
            }
        }
        // Drag and drop
        else if (m_event.type == KNST_WINDOW_EVENT_FILE_DROP_ENTER) {
            std::cout << "[" << tag << "] Drag entered window\n";
        }
        else if (m_event.type == KNST_WINDOW_EVENT_FILE_DROP_LEAVE) {
            std::cout << "[" << tag << "] Drag left window\n";
        }
        else if (m_event.type == KNST_WINDOW_EVENT_FILE_DROP_MOVE) {
            std::cout << "[" << tag << "] Dragging over window\n";
        }
        else if (m_event.type == KNST_WINDOW_EVENT_FILE_DROP) {
            std::cout << "[" << tag << "] Files dropped: " << m_event.drop_count << "\n";
            if (m_event.drop_files) {
                for (uint32_t j = 0; j < m_event.drop_count; ++j) {
                    std::cout << "[" << tag << "]   [" << j << "] "
                              << (*m_event.drop_files)[j] << "\n";
                }
            }
        }
    }

    w.clear_events();
}


static void on_frame_a(knst_window& w, void* /*user_data*/) {
    handle_window_events(w, "A");
}

static void on_frame_b(knst_window& w, void* /*user_data*/) {
    handle_window_events(w, "B");
}


int main() {
    KnstWindowSources::Init();

    // Window A at (100, 200), Window B at (750, 200), both 600x500.
    knst_window window_a(600, 500, "Window A", 100, 200);
    knst_window window_b(600, 500, "Window B", 750, 200);

    window_a.set_redraw_callback(on_frame_a);
    window_b.set_redraw_callback(on_frame_b);

    window_a.creation_and_show();
    window_b.creation_and_show();

    window_a.set_drag_drop_status(true);
    window_b.set_drag_drop_status(true);

    std::cout << "Two windows opened. Press ESC on either to close it.\n\n";

    bool a_reported = false;
    bool b_reported = false;

    // Run until both windows are closed.
    while (!window_a.is_should_close() || !window_b.is_should_close()) {
        knst_window_event_system::non_block_pool_event();

        window_a.call_redraw_callback();
        window_b.call_redraw_callback();

        if (!a_reported && window_a.is_should_close()) {
            std::cout << "A closed\n";
            a_reported = true;
        }
        if (!b_reported && window_b.is_should_close()) {
            std::cout << "B closed\n";
            b_reported = true;
        }
    }

    window_a.destroy();
    window_b.destroy();

    KnstWindowSources::CleanUp();
    std::cout << "\nBoth windows closed. Exiting.\n";

    return 0;
}