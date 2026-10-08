// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
// SPDX-License-Identifier: MPL-2.0

#pragma once


/*
----------------------------
knst_window_identifiers.hpp
----------------------------

    The macros used by the library for event-related operations are defined in this .hpp file.

*/



//  Macro names mirror the fields of knst_window_event so that every comparison
//  is self-documenting:
//
//      m_event.type          → KNST_WINDOW_EVENT_*
//      m_event.key_action    → KNST_WINDOW_KEY_ACTION_*
//      m_event.key_code      → KNST_WINDOW_KEY_CODE_*
//      m_event.mouse_action  → KNST_WINDOW_MOUSE_ACTION_*
//      m_event.mouse_button  → KNST_WINDOW_MOUSE_BUTTON_*
//      m_event.mods          → KNST_WINDOW_MOD_*
//
//  Event type ranges (safe, non-overlapping):
//      0            UNKNOWN sentinel (never dispatched)
//      100 .. 199   Mouse events
//      200 .. 299   Keyboard events
//      300 .. 399   Window state events
//      400 .. 499   Focus events
//      500 .. 599   File drop events
//      600 .. 699   Mobile / Android events
//      700 .. 799   Application lifecycle events
//
//  Action values are small integers (1, 2, 3) — they never collide with event
//  type ranges. Modifier values are single-bit flags so multiple modifiers can
//  be combined in a single integer.
// ============================================================================

// ============================================================================
//  1) EVENT TYPES — m_event.type
// ============================================================================
#define KNST_WINDOW_EVENT_UNKNOWN                   0

// ── Mouse events (100..199) ────────────────────────────────────────────────
#define KNST_WINDOW_EVENT_MOUSE                     100
#define KNST_WINDOW_EVENT_MOTION                    101
#define KNST_WINDOW_EVENT_ENTER                     102
#define KNST_WINDOW_EVENT_LEAVE                     103

// ── Keyboard events (200..299) ─────────────────────────────────────────────
#define KNST_WINDOW_EVENT_KEYBOARD                  200

// ── Window state events (300..399) ─────────────────────────────────────────
#define KNST_WINDOW_EVENT_RESIZE                    300
#define KNST_WINDOW_EVENT_MOVE                      301
#define KNST_WINDOW_EVENT_MAXIMIZE                  302
#define KNST_WINDOW_EVENT_MINIMIZE                  303
#define KNST_WINDOW_EVENT_RESTORE                   304
#define KNST_WINDOW_EVENT_FULLSCREEN                305
#define KNST_WINDOW_EVENT_EXPOSE                    306
#define KNST_WINDOW_EVENT_CLOSE                     307
#define KNST_WINDOW_EVENT_DISCONNECT                308

// ── Focus events (400..499) ────────────────────────────────────────────────
#define KNST_WINDOW_EVENT_FOCUS_IN                  400
#define KNST_WINDOW_EVENT_FOCUS_OUT                 401

// ── File drop events (500..599) ────────────────────────────────────────────
#define KNST_WINDOW_EVENT_FILE_DROP                 500
#define KNST_WINDOW_EVENT_FILE_DROP_ENTER           501
#define KNST_WINDOW_EVENT_FILE_DROP_MOVE            502
#define KNST_WINDOW_EVENT_FILE_DROP_LEAVE           503

// ── Mobile events (600..699) ───────────────────────────────────────────────
#define KNST_WINDOW_EVENT_MOBILE_TOUCH              600
#define KNST_WINDOW_EVENT_MOBILE_BACK               601
#define KNST_WINDOW_EVENT_MOBILE_HOME               602
#define KNST_WINDOW_EVENT_MOBILE_MENU               603
#define KNST_WINDOW_EVENT_MOBILE_SEARCH             604
#define KNST_WINDOW_EVENT_MOBILE_APP_SWITCH         605
#define KNST_WINDOW_EVENT_MOBILE_RECENT_APPS        606
#define KNST_WINDOW_EVENT_MOBILE_VOLUME_UP          607
#define KNST_WINDOW_EVENT_MOBILE_VOLUME_DOWN        608
#define KNST_WINDOW_EVENT_MOBILE_VOLUME_MUTE        609
#define KNST_WINDOW_EVENT_MOBILE_POWER              610
#define KNST_WINDOW_EVENT_MOBILE_CAMERA             611
#define KNST_WINDOW_EVENT_MOBILE_HELP               612
#define KNST_WINDOW_EVENT_MOBILE_SETTINGS           613
#define KNST_WINDOW_EVENT_MOBILE_SLEEP              614
#define KNST_WINDOW_EVENT_MOBILE_WAKEUP             615
#define KNST_WINDOW_EVENT_MOBILE_ASSIST             616
#define KNST_WINDOW_EVENT_MOBILE_BOOKMARK           617
#define KNST_WINDOW_EVENT_MOBILE_CALCULATOR         618
#define KNST_WINDOW_EVENT_MOBILE_CALENDAR           619
#define KNST_WINDOW_EVENT_MOBILE_CONTACTS           620
#define KNST_WINDOW_EVENT_MOBILE_EXPLORER           621
#define KNST_WINDOW_EVENT_MOBILE_MUSIC              622
#define KNST_WINDOW_EVENT_MOBILE_MEDIA_PLAY_PAUSE   623
#define KNST_WINDOW_EVENT_MOBILE_MEDIA_STOP         624
#define KNST_WINDOW_EVENT_MOBILE_MEDIA_NEXT         625
#define KNST_WINDOW_EVENT_MOBILE_MEDIA_PREVIOUS     626
#define KNST_WINDOW_EVENT_MOBILE_MEDIA_REWIND       627
#define KNST_WINDOW_EVENT_MOBILE_MEDIA_FORWARD      628
#define KNST_WINDOW_EVENT_MOBILE_MEDIA_RECORD       629
#define KNST_WINDOW_EVENT_MOBILE_MEDIA_PAUSE        630

// ── App lifecycle events (700..799) ────────────────────────────────────────
#define KNST_WINDOW_EVENT_APP_STARTED               700
#define KNST_WINDOW_EVENT_APP_RESUMED               701
#define KNST_WINDOW_EVENT_APP_PAUSED                702
#define KNST_WINDOW_EVENT_APP_STOPPED               703
#define KNST_WINDOW_EVENT_APP_SAVE_STATE            704
#define KNST_WINDOW_EVENT_APP_LOW_MEMORY            705
#define KNST_WINDOW_EVENT_APP_CONFIG_CHANGED        706
#define KNST_WINDOW_EVENT_APP_INPUT_CHANGED         707
#define KNST_WINDOW_EVENT_APP_CONTENT_RECT          708
#define KNST_WINDOW_EVENT_APP_WINDOW_LOST           709

// ============================================================================
//  2) KEY ACTIONS — m_event.key_action
// ============================================================================
#define KNST_WINDOW_KEY_ACTION_PRESS                1
#define KNST_WINDOW_KEY_ACTION_RELEASE              2
#define KNST_WINDOW_KEY_ACTION_REPEAT               3

// ============================================================================
//  3) MOUSE ACTIONS — m_event.mouse_action
// ============================================================================
#define KNST_WINDOW_MOUSE_ACTION_PRESS              1
#define KNST_WINDOW_MOUSE_ACTION_RELEASE            2
#define KNST_WINDOW_MOUSE_ACTION_SCROLL             3

// ============================================================================
//  4) MOUSE BUTTONS — m_event.mouse_button
//  Values follow the X11 / POSIX convention: 1=left, 2=middle, 3=right.
//  8 and 9 are the standard "back" and "forward" side buttons.
// ============================================================================
#define KNST_WINDOW_MOUSE_BUTTON_LEFT               1
#define KNST_WINDOW_MOUSE_BUTTON_MIDDLE             2
#define KNST_WINDOW_MOUSE_BUTTON_RIGHT              3
#define KNST_WINDOW_MOUSE_BUTTON_BACK               8
#define KNST_WINDOW_MOUSE_BUTTON_FORWARD            9

// ============================================================================
//  5) SCROLL DELTA SIGN — m_event.mouse_scroll_delta
// ============================================================================
#define KNST_WINDOW_MOUSE_SCROLL_UP                 1
#define KNST_WINDOW_MOUSE_SCROLL_DOWN              (-1)

// ============================================================================
//  6) MODIFIER BITS — m_event.mods
//  These are unified single-bit flags. Every platform handler MUST translate
//  its own native state mask into these bit positions before storing the
//  value into m_event.mods.
// ============================================================================
#define KNST_WINDOW_MOD_SHIFT                       (1 << 0)   // 1
#define KNST_WINDOW_MOD_CONTROL                     (1 << 1)   // 2
#define KNST_WINDOW_MOD_ALT                         (1 << 2)   // 4
#define KNST_WINDOW_MOD_SUPER                       (1 << 3)   // 8
#define KNST_WINDOW_MOD_CAPS_LOCK                   (1 << 4)   // 16
#define KNST_WINDOW_MOD_NUM_LOCK                    (1 << 5)   // 32

// ============================================================================
//  7) WINDOW ATTRIBUTES — set_attribute() / get_attribute()
// ============================================================================
#define KNST_WINDOW_ATTR_DECORATED                  1
#define KNST_WINDOW_ATTR_RESIZABLE                  2
#define KNST_WINDOW_ATTR_ALWAYS_ON_TOP              3
#define KNST_WINDOW_ATTR_TRANSPARENT                4

// ============================================================================
//  8) DEFAULT SENTINEL
// ============================================================================
#define KNST_WINDOW_DEFAULT                         (-10000)

// ============================================================================
//  9) PLATFORM-SPECIFIC KEY CODE VALUES
//  Same macro names on every platform; native values differ.
// ============================================================================


// ────────────────────────────────────────────────────────────────────────────
//  WINDOWS  (Virtual-Key codes)
// ────────────────────────────────────────────────────────────────────────────
#if KNST_USING_PLATFORM_WINDOWS
    #include <windows.h>

    // ── Letters A..Z ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_A                  'A'
    #define KNST_WINDOW_KEY_CODE_B                  'B'
    #define KNST_WINDOW_KEY_CODE_C                  'C'
    #define KNST_WINDOW_KEY_CODE_D                  'D'
    #define KNST_WINDOW_KEY_CODE_E                  'E'
    #define KNST_WINDOW_KEY_CODE_F                  'F'
    #define KNST_WINDOW_KEY_CODE_G                  'G'
    #define KNST_WINDOW_KEY_CODE_H                  'H'
    #define KNST_WINDOW_KEY_CODE_I                  'I'
    #define KNST_WINDOW_KEY_CODE_J                  'J'
    #define KNST_WINDOW_KEY_CODE_K                  'K'
    #define KNST_WINDOW_KEY_CODE_L                  'L'
    #define KNST_WINDOW_KEY_CODE_M                  'M'
    #define KNST_WINDOW_KEY_CODE_N                  'N'
    #define KNST_WINDOW_KEY_CODE_O                  'O'
    #define KNST_WINDOW_KEY_CODE_P                  'P'
    #define KNST_WINDOW_KEY_CODE_Q                  'Q'
    #define KNST_WINDOW_KEY_CODE_R                  'R'
    #define KNST_WINDOW_KEY_CODE_S                  'S'
    #define KNST_WINDOW_KEY_CODE_T                  'T'
    #define KNST_WINDOW_KEY_CODE_U                  'U'
    #define KNST_WINDOW_KEY_CODE_V                  'V'
    #define KNST_WINDOW_KEY_CODE_W                  'W'
    #define KNST_WINDOW_KEY_CODE_X                  'X'
    #define KNST_WINDOW_KEY_CODE_Y                  'Y'
    #define KNST_WINDOW_KEY_CODE_Z                  'Z'

    // ── Turkish characters ─────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_C_CEDILLA          VK_OEM_4      // Ç
    #define KNST_WINDOW_KEY_CODE_G_BREVE            VK_OEM_5      // Ğ
    #define KNST_WINDOW_KEY_CODE_I_DOTLESS          VK_OEM_6      // ı
    #define KNST_WINDOW_KEY_CODE_O_DIAERESIS        VK_OEM_7      // Ö
    #define KNST_WINDOW_KEY_CODE_S_CEDILLA          VK_OEM_8      // Ş
    #define KNST_WINDOW_KEY_CODE_U_DIAERESIS        VK_OEM_102    // Ü

    // ── Digits ─────────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_0                  '0'
    #define KNST_WINDOW_KEY_CODE_1                  '1'
    #define KNST_WINDOW_KEY_CODE_2                  '2'
    #define KNST_WINDOW_KEY_CODE_3                  '3'
    #define KNST_WINDOW_KEY_CODE_4                  '4'
    #define KNST_WINDOW_KEY_CODE_5                  '5'
    #define KNST_WINDOW_KEY_CODE_6                  '6'
    #define KNST_WINDOW_KEY_CODE_7                  '7'
    #define KNST_WINDOW_KEY_CODE_8                  '8'
    #define KNST_WINDOW_KEY_CODE_9                  '9'

    // ── Numpad ─────────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_NUMPAD_0           VK_NUMPAD0
    #define KNST_WINDOW_KEY_CODE_NUMPAD_1           VK_NUMPAD1
    #define KNST_WINDOW_KEY_CODE_NUMPAD_2           VK_NUMPAD2
    #define KNST_WINDOW_KEY_CODE_NUMPAD_3           VK_NUMPAD3
    #define KNST_WINDOW_KEY_CODE_NUMPAD_4           VK_NUMPAD4
    #define KNST_WINDOW_KEY_CODE_NUMPAD_5           VK_NUMPAD5
    #define KNST_WINDOW_KEY_CODE_NUMPAD_6           VK_NUMPAD6
    #define KNST_WINDOW_KEY_CODE_NUMPAD_7           VK_NUMPAD7
    #define KNST_WINDOW_KEY_CODE_NUMPAD_8           VK_NUMPAD8
    #define KNST_WINDOW_KEY_CODE_NUMPAD_9           VK_NUMPAD9
    #define KNST_WINDOW_KEY_CODE_NUMPAD_ADD         VK_ADD
    #define KNST_WINDOW_KEY_CODE_NUMPAD_SUBTRACT    VK_SUBTRACT
    #define KNST_WINDOW_KEY_CODE_NUMPAD_MULTIPLY    VK_MULTIPLY
    #define KNST_WINDOW_KEY_CODE_NUMPAD_DIVIDE      VK_DIVIDE
    #define KNST_WINDOW_KEY_CODE_NUMPAD_DECIMAL     VK_DECIMAL
    #define KNST_WINDOW_KEY_CODE_NUMPAD_ENTER       VK_RETURN

    // ── Function keys ──────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_F1                 VK_F1
    #define KNST_WINDOW_KEY_CODE_F2                 VK_F2
    #define KNST_WINDOW_KEY_CODE_F3                 VK_F3
    #define KNST_WINDOW_KEY_CODE_F4                 VK_F4
    #define KNST_WINDOW_KEY_CODE_F5                 VK_F5
    #define KNST_WINDOW_KEY_CODE_F6                 VK_F6
    #define KNST_WINDOW_KEY_CODE_F7                 VK_F7
    #define KNST_WINDOW_KEY_CODE_F8                 VK_F8
    #define KNST_WINDOW_KEY_CODE_F9                 VK_F9
    #define KNST_WINDOW_KEY_CODE_F10                VK_F10
    #define KNST_WINDOW_KEY_CODE_F11                VK_F11
    #define KNST_WINDOW_KEY_CODE_F12                VK_F12

    // ── Special keys ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_ESCAPE             VK_ESCAPE
    #define KNST_WINDOW_KEY_CODE_ENTER              VK_RETURN
    #define KNST_WINDOW_KEY_CODE_SPACE              VK_SPACE
    #define KNST_WINDOW_KEY_CODE_BACKSPACE          VK_BACK
    #define KNST_WINDOW_KEY_CODE_TAB                VK_TAB
    #define KNST_WINDOW_KEY_CODE_CAPS_LOCK          VK_CAPITAL
    #define KNST_WINDOW_KEY_CODE_NUM_LOCK           VK_NUMLOCK
    #define KNST_WINDOW_KEY_CODE_SCROLL_LOCK        VK_SCROLL

    // ── Modifier keys ──────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_SHIFT              VK_SHIFT
    #define KNST_WINDOW_KEY_CODE_CONTROL            VK_CONTROL
    #define KNST_WINDOW_KEY_CODE_ALT                VK_MENU
    #define KNST_WINDOW_KEY_CODE_SUPER              VK_LWIN
    #define KNST_WINDOW_KEY_CODE_MENU               VK_APPS

    // ── Arrow keys ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_LEFT               VK_LEFT
    #define KNST_WINDOW_KEY_CODE_RIGHT              VK_RIGHT
    #define KNST_WINDOW_KEY_CODE_UP                 VK_UP
    #define KNST_WINDOW_KEY_CODE_DOWN               VK_DOWN

    // ── Navigation ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_HOME               VK_HOME
    #define KNST_WINDOW_KEY_CODE_END                VK_END
    #define KNST_WINDOW_KEY_CODE_PAGE_UP            VK_PRIOR
    #define KNST_WINDOW_KEY_CODE_PAGE_DOWN          VK_NEXT
    #define KNST_WINDOW_KEY_CODE_INSERT             VK_INSERT
    #define KNST_WINDOW_KEY_CODE_DELETE             VK_DELETE
    #define KNST_WINDOW_KEY_CODE_PRINT              VK_SNAPSHOT
    #define KNST_WINDOW_KEY_CODE_PAUSE              VK_PAUSE
    #define KNST_WINDOW_KEY_CODE_BREAK              VK_CANCEL

    // ── Symbol keys ────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_SEMICOLON          VK_OEM_1
    #define KNST_WINDOW_KEY_CODE_SLASH              VK_OEM_2
    #define KNST_WINDOW_KEY_CODE_GRAVE              VK_OEM_3
    #define KNST_WINDOW_KEY_CODE_LEFT_BRACKET       VK_OEM_4
    #define KNST_WINDOW_KEY_CODE_BACKSLASH          VK_OEM_5
    #define KNST_WINDOW_KEY_CODE_RIGHT_BRACKET      VK_OEM_6
    #define KNST_WINDOW_KEY_CODE_APOSTROPHE         VK_OEM_7
    #define KNST_WINDOW_KEY_CODE_PERIOD             VK_OEM_PERIOD
    #define KNST_WINDOW_KEY_CODE_COMMA              VK_OEM_COMMA
    #define KNST_WINDOW_KEY_CODE_MINUS              VK_OEM_MINUS
    #define KNST_WINDOW_KEY_CODE_PLUS               VK_OEM_PLUS
    #define KNST_WINDOW_KEY_CODE_EQUAL              VK_OEM_PLUS
    #define KNST_WINDOW_KEY_CODE_QUOTE              VK_OEM_7
    #define KNST_WINDOW_KEY_CODE_COLON              VK_OEM_1
    #define KNST_WINDOW_KEY_CODE_TILDE              VK_OEM_3
    #define KNST_WINDOW_KEY_CODE_LESS               VK_OEM_102
    #define KNST_WINDOW_KEY_CODE_GREATER            VK_OEM_102
    #define KNST_WINDOW_KEY_CODE_QUESTION           VK_OEM_2
    #define KNST_WINDOW_KEY_CODE_PIPE               VK_OEM_5
    #define KNST_WINDOW_KEY_CODE_LEFT_BRACE         VK_OEM_4
    #define KNST_WINDOW_KEY_CODE_RIGHT_BRACE        VK_OEM_6
    #define KNST_WINDOW_KEY_CODE_UNDERSCORE         VK_OEM_MINUS

    // ── Media keys ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_VOLUME_UP          VK_VOLUME_UP
    #define KNST_WINDOW_KEY_CODE_VOLUME_DOWN        VK_VOLUME_DOWN
    #define KNST_WINDOW_KEY_CODE_VOLUME_MUTE        VK_VOLUME_MUTE
    #define KNST_WINDOW_KEY_CODE_MEDIA_PLAY         VK_MEDIA_PLAY_PAUSE
    #define KNST_WINDOW_KEY_CODE_MEDIA_STOP         VK_MEDIA_STOP
    #define KNST_WINDOW_KEY_CODE_MEDIA_NEXT         VK_MEDIA_NEXT_TRACK
    #define KNST_WINDOW_KEY_CODE_MEDIA_PREV         VK_MEDIA_PREV_TRACK
    #define KNST_WINDOW_KEY_CODE_MEDIA_PAUSE        VK_MEDIA_PLAY_PAUSE

    // ── Browser keys ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_BROWSER_HOME       VK_BROWSER_HOME
    #define KNST_WINDOW_KEY_CODE_BROWSER_BACK       VK_BROWSER_BACK
    #define KNST_WINDOW_KEY_CODE_BROWSER_FORWARD    VK_BROWSER_FORWARD
    #define KNST_WINDOW_KEY_CODE_BROWSER_REFRESH    VK_BROWSER_REFRESH
    #define KNST_WINDOW_KEY_CODE_BROWSER_SEARCH     VK_BROWSER_SEARCH
    #define KNST_WINDOW_KEY_CODE_BROWSER_FAVORITES  VK_BROWSER_FAVORITES

    // ── Cursors ────────────────────────────────────────────────────────────

    #define KNST_WINDOW_CURSOR_ARROW                32512   // OCR_NORMAL
    #define KNST_WINDOW_CURSOR_IBEAM                32513   // OCR_IBEAM
    #define KNST_WINDOW_CURSOR_CROSSHAIR            32515   // OCR_CROSS
    #define KNST_WINDOW_CURSOR_HAND                 32649   // OCR_HAND
    #define KNST_WINDOW_CURSOR_HRESIZE              32644   // OCR_SIZEWE
    #define KNST_WINDOW_CURSOR_VRESIZE              32645   // OCR_SIZENS
    #define KNST_WINDOW_CURSOR_MOVE                 32646   // OCR_SIZEALL
    #define KNST_WINDOW_CURSOR_WAIT                 32514   // OCR_WAIT
    #define KNST_WINDOW_CURSOR_HELP                 32651   // OCR_HELP
    #define KNST_WINDOW_CURSOR_NOT_ALLOWED          32648   // OCR_NO
    #define KNST_WINDOW_CURSOR_HIDDEN               0
    #define KNST_WINDOW_CURSOR_DISABLED             0


// ────────────────────────────────────────────────────────────────────────────
//  LINUX / X11
// ────────────────────────────────────────────────────────────────────────────
#elif KNST_USING_LINUX_PLATFORM_X11
    #include <X11/Xlib.h>
    #include <X11/XF86keysym.h>
    #include <X11/keysym.h>
    #include <xcb/xcb.h>
    #include <xcb/xproto.h>
    #include <X11/cursorfont.h>
    #include <X11/XKBlib.h>
    #include <X11/extensions/sync.h>
    #include <X11/extensions/syncconst.h>

    // ── Letters A..Z (uppercase — X11 handler normalizes lowercase) ────────
    #define KNST_WINDOW_KEY_CODE_A                  XK_A
    #define KNST_WINDOW_KEY_CODE_B                  XK_B
    #define KNST_WINDOW_KEY_CODE_C                  XK_C
    #define KNST_WINDOW_KEY_CODE_D                  XK_D
    #define KNST_WINDOW_KEY_CODE_E                  XK_E
    #define KNST_WINDOW_KEY_CODE_F                  XK_F
    #define KNST_WINDOW_KEY_CODE_G                  XK_G
    #define KNST_WINDOW_KEY_CODE_H                  XK_H
    #define KNST_WINDOW_KEY_CODE_I                  XK_I
    #define KNST_WINDOW_KEY_CODE_J                  XK_J
    #define KNST_WINDOW_KEY_CODE_K                  XK_K
    #define KNST_WINDOW_KEY_CODE_L                  XK_L
    #define KNST_WINDOW_KEY_CODE_M                  XK_M
    #define KNST_WINDOW_KEY_CODE_N                  XK_N
    #define KNST_WINDOW_KEY_CODE_O                  XK_O
    #define KNST_WINDOW_KEY_CODE_P                  XK_P
    #define KNST_WINDOW_KEY_CODE_Q                  XK_Q
    #define KNST_WINDOW_KEY_CODE_R                  XK_R
    #define KNST_WINDOW_KEY_CODE_S                  XK_S
    #define KNST_WINDOW_KEY_CODE_T                  XK_T
    #define KNST_WINDOW_KEY_CODE_U                  XK_U
    #define KNST_WINDOW_KEY_CODE_V                  XK_V
    #define KNST_WINDOW_KEY_CODE_W                  XK_W
    #define KNST_WINDOW_KEY_CODE_X                  XK_X
    #define KNST_WINDOW_KEY_CODE_Y                  XK_Y
    #define KNST_WINDOW_KEY_CODE_Z                  XK_Z

    // ── Turkish characters (normalized to uppercase by handler) ────────────
    #define KNST_WINDOW_KEY_CODE_C_CEDILLA          XK_Ccedilla
    #define KNST_WINDOW_KEY_CODE_G_BREVE            XK_Gbreve
    #define KNST_WINDOW_KEY_CODE_I_DOTLESS          XK_I
    #define KNST_WINDOW_KEY_CODE_O_DIAERESIS        XK_Odiaeresis
    #define KNST_WINDOW_KEY_CODE_S_CEDILLA          XK_Scedilla
    #define KNST_WINDOW_KEY_CODE_U_DIAERESIS        XK_Udiaeresis

    // ── Digits ─────────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_0                  XK_0
    #define KNST_WINDOW_KEY_CODE_1                  XK_1
    #define KNST_WINDOW_KEY_CODE_2                  XK_2
    #define KNST_WINDOW_KEY_CODE_3                  XK_3
    #define KNST_WINDOW_KEY_CODE_4                  XK_4
    #define KNST_WINDOW_KEY_CODE_5                  XK_5
    #define KNST_WINDOW_KEY_CODE_6                  XK_6
    #define KNST_WINDOW_KEY_CODE_7                  XK_7
    #define KNST_WINDOW_KEY_CODE_8                  XK_8
    #define KNST_WINDOW_KEY_CODE_9                  XK_9

    // ── Numpad ─────────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_NUMPAD_0           XK_KP_0
    #define KNST_WINDOW_KEY_CODE_NUMPAD_1           XK_KP_1
    #define KNST_WINDOW_KEY_CODE_NUMPAD_2           XK_KP_2
    #define KNST_WINDOW_KEY_CODE_NUMPAD_3           XK_KP_3
    #define KNST_WINDOW_KEY_CODE_NUMPAD_4           XK_KP_4
    #define KNST_WINDOW_KEY_CODE_NUMPAD_5           XK_KP_5
    #define KNST_WINDOW_KEY_CODE_NUMPAD_6           XK_KP_6
    #define KNST_WINDOW_KEY_CODE_NUMPAD_7           XK_KP_7
    #define KNST_WINDOW_KEY_CODE_NUMPAD_8           XK_KP_8
    #define KNST_WINDOW_KEY_CODE_NUMPAD_9           XK_KP_9
    #define KNST_WINDOW_KEY_CODE_NUMPAD_ADD         XK_KP_Add
    #define KNST_WINDOW_KEY_CODE_NUMPAD_SUBTRACT    XK_KP_Subtract
    #define KNST_WINDOW_KEY_CODE_NUMPAD_MULTIPLY    XK_KP_Multiply
    #define KNST_WINDOW_KEY_CODE_NUMPAD_DIVIDE      XK_KP_Divide
    #define KNST_WINDOW_KEY_CODE_NUMPAD_DECIMAL     XK_KP_Decimal
    #define KNST_WINDOW_KEY_CODE_NUMPAD_ENTER       XK_KP_Enter

    // ── Function keys ──────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_F1                 XK_F1
    #define KNST_WINDOW_KEY_CODE_F2                 XK_F2
    #define KNST_WINDOW_KEY_CODE_F3                 XK_F3
    #define KNST_WINDOW_KEY_CODE_F4                 XK_F4
    #define KNST_WINDOW_KEY_CODE_F5                 XK_F5
    #define KNST_WINDOW_KEY_CODE_F6                 XK_F6
    #define KNST_WINDOW_KEY_CODE_F7                 XK_F7
    #define KNST_WINDOW_KEY_CODE_F8                 XK_F8
    #define KNST_WINDOW_KEY_CODE_F9                 XK_F9
    #define KNST_WINDOW_KEY_CODE_F10                XK_F10
    #define KNST_WINDOW_KEY_CODE_F11                XK_F11
    #define KNST_WINDOW_KEY_CODE_F12                XK_F12

    // ── Special keys ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_ESCAPE             XK_Escape
    #define KNST_WINDOW_KEY_CODE_ENTER              XK_Return
    #define KNST_WINDOW_KEY_CODE_SPACE              XK_space
    #define KNST_WINDOW_KEY_CODE_BACKSPACE          XK_BackSpace
    #define KNST_WINDOW_KEY_CODE_TAB                XK_Tab
    #define KNST_WINDOW_KEY_CODE_CAPS_LOCK          XK_Caps_Lock
    #define KNST_WINDOW_KEY_CODE_NUM_LOCK           XK_Num_Lock
    #define KNST_WINDOW_KEY_CODE_SCROLL_LOCK        XK_Scroll_Lock

    // ── Modifier keys ──────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_SHIFT              XK_Shift_L
    #define KNST_WINDOW_KEY_CODE_CONTROL            XK_Control_L
    #define KNST_WINDOW_KEY_CODE_ALT                XK_Alt_L
    #define KNST_WINDOW_KEY_CODE_SUPER              XK_Super_L
    #define KNST_WINDOW_KEY_CODE_MENU               XK_Menu

    // ── Arrow keys ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_LEFT               XK_Left
    #define KNST_WINDOW_KEY_CODE_RIGHT              XK_Right
    #define KNST_WINDOW_KEY_CODE_UP                 XK_Up
    #define KNST_WINDOW_KEY_CODE_DOWN               XK_Down

    // ── Navigation ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_HOME               XK_Home
    #define KNST_WINDOW_KEY_CODE_END                XK_End
    #define KNST_WINDOW_KEY_CODE_PAGE_UP            XK_Page_Up
    #define KNST_WINDOW_KEY_CODE_PAGE_DOWN          XK_Page_Down
    #define KNST_WINDOW_KEY_CODE_INSERT             XK_Insert
    #define KNST_WINDOW_KEY_CODE_DELETE             XK_Delete
    #define KNST_WINDOW_KEY_CODE_PRINT              XK_Print
    #define KNST_WINDOW_KEY_CODE_PAUSE              XK_Pause
    #define KNST_WINDOW_KEY_CODE_BREAK              XK_Break

    // ── Symbol keys ────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_SEMICOLON          XK_semicolon
    #define KNST_WINDOW_KEY_CODE_SLASH              XK_slash
    #define KNST_WINDOW_KEY_CODE_GRAVE              XK_grave
    #define KNST_WINDOW_KEY_CODE_LEFT_BRACKET       XK_bracketleft
    #define KNST_WINDOW_KEY_CODE_BACKSLASH          XK_backslash
    #define KNST_WINDOW_KEY_CODE_RIGHT_BRACKET      XK_bracketright
    #define KNST_WINDOW_KEY_CODE_APOSTROPHE         XK_apostrophe
    #define KNST_WINDOW_KEY_CODE_PERIOD             XK_period
    #define KNST_WINDOW_KEY_CODE_COMMA              XK_comma
    #define KNST_WINDOW_KEY_CODE_MINUS              XK_minus
    #define KNST_WINDOW_KEY_CODE_PLUS               XK_plus
    #define KNST_WINDOW_KEY_CODE_EQUAL              XK_equal
    #define KNST_WINDOW_KEY_CODE_QUOTE              XK_quotedbl
    #define KNST_WINDOW_KEY_CODE_COLON              XK_colon
    #define KNST_WINDOW_KEY_CODE_TILDE              XK_asciitilde
    #define KNST_WINDOW_KEY_CODE_LESS               XK_less
    #define KNST_WINDOW_KEY_CODE_GREATER            XK_greater
    #define KNST_WINDOW_KEY_CODE_QUESTION           XK_question
    #define KNST_WINDOW_KEY_CODE_PIPE               XK_bar
    #define KNST_WINDOW_KEY_CODE_LEFT_BRACE         XK_braceleft
    #define KNST_WINDOW_KEY_CODE_RIGHT_BRACE        XK_braceright
    #define KNST_WINDOW_KEY_CODE_UNDERSCORE         XK_underscore

    // ── Media keys ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_VOLUME_UP          XF86XK_AudioRaiseVolume
    #define KNST_WINDOW_KEY_CODE_VOLUME_DOWN        XF86XK_AudioLowerVolume
    #define KNST_WINDOW_KEY_CODE_VOLUME_MUTE        XF86XK_AudioMute
    #define KNST_WINDOW_KEY_CODE_MEDIA_PLAY         XF86XK_AudioPlay
    #define KNST_WINDOW_KEY_CODE_MEDIA_STOP         XF86XK_AudioStop
    #define KNST_WINDOW_KEY_CODE_MEDIA_NEXT         XF86XK_AudioNext
    #define KNST_WINDOW_KEY_CODE_MEDIA_PREV         XF86XK_AudioPrev
    #define KNST_WINDOW_KEY_CODE_MEDIA_PAUSE        XF86XK_AudioPause

    // ── Browser keys ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_BROWSER_HOME       XF86XK_HomePage
    #define KNST_WINDOW_KEY_CODE_BROWSER_BACK       XF86XK_Back
    #define KNST_WINDOW_KEY_CODE_BROWSER_FORWARD    XF86XK_Forward
    #define KNST_WINDOW_KEY_CODE_BROWSER_REFRESH    XF86XK_Refresh
    #define KNST_WINDOW_KEY_CODE_BROWSER_SEARCH     XF86XK_Search
    #define KNST_WINDOW_KEY_CODE_BROWSER_FAVORITES  XF86XK_Favorites

    // ── Cursors ────────────────────────────────────────────────────────────
    #define KNST_WINDOW_CURSOR_ARROW                XC_left_ptr
    #define KNST_WINDOW_CURSOR_IBEAM                XC_xterm
    #define KNST_WINDOW_CURSOR_CROSSHAIR            XC_crosshair
    #define KNST_WINDOW_CURSOR_HAND                 XC_hand2
    #define KNST_WINDOW_CURSOR_HRESIZE              XC_sb_h_double_arrow
    #define KNST_WINDOW_CURSOR_VRESIZE              XC_sb_v_double_arrow
    #define KNST_WINDOW_CURSOR_MOVE                 XC_fleur
    #define KNST_WINDOW_CURSOR_WAIT                 XC_watch
    #define KNST_WINDOW_CURSOR_HELP                 XC_question_arrow
    #define KNST_WINDOW_CURSOR_NOT_ALLOWED          XC_X_cursor
    #define KNST_WINDOW_CURSOR_HIDDEN               0
    #define KNST_WINDOW_CURSOR_DISABLED             0


// ────────────────────────────────────────────────────────────────────────────
//  LINUX / WAYLAND
// ────────────────────────────────────────────────────────────────────────────
#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    #include <xkbcommon/xkbcommon-keysyms.h>
    #include <wayland-client.h>
    #include "../linux/wayland/protocol_files/xdg-shell-client-protocol.h"
    #include "../linux/wayland/protocol_files/xdg-output-client-protocol.h"
    #include "../linux/wayland/protocol_files/xdg-decoration-client-protocol.h"
    #include "../linux/wayland/protocol_files/relative-pointer-unstable-v1-client-protocol.h"
    #include "../linux/wayland/protocol_files/pointer-constraints-unstable-v1-client-protocol.h"

    // ── Letters A..Z ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_A                  XKB_KEY_A
    #define KNST_WINDOW_KEY_CODE_B                  XKB_KEY_B
    #define KNST_WINDOW_KEY_CODE_C                  XKB_KEY_C
    #define KNST_WINDOW_KEY_CODE_D                  XKB_KEY_D
    #define KNST_WINDOW_KEY_CODE_E                  XKB_KEY_E
    #define KNST_WINDOW_KEY_CODE_F                  XKB_KEY_F
    #define KNST_WINDOW_KEY_CODE_G                  XKB_KEY_G
    #define KNST_WINDOW_KEY_CODE_H                  XKB_KEY_H
    #define KNST_WINDOW_KEY_CODE_I                  XKB_KEY_I
    #define KNST_WINDOW_KEY_CODE_J                  XKB_KEY_J
    #define KNST_WINDOW_KEY_CODE_K                  XKB_KEY_K
    #define KNST_WINDOW_KEY_CODE_L                  XKB_KEY_L
    #define KNST_WINDOW_KEY_CODE_M                  XKB_KEY_M
    #define KNST_WINDOW_KEY_CODE_N                  XKB_KEY_N
    #define KNST_WINDOW_KEY_CODE_O                  XKB_KEY_O
    #define KNST_WINDOW_KEY_CODE_P                  XKB_KEY_P
    #define KNST_WINDOW_KEY_CODE_Q                  XKB_KEY_Q
    #define KNST_WINDOW_KEY_CODE_R                  XKB_KEY_R
    #define KNST_WINDOW_KEY_CODE_S                  XKB_KEY_S
    #define KNST_WINDOW_KEY_CODE_T                  XKB_KEY_T
    #define KNST_WINDOW_KEY_CODE_U                  XKB_KEY_U
    #define KNST_WINDOW_KEY_CODE_V                  XKB_KEY_V
    #define KNST_WINDOW_KEY_CODE_W                  XKB_KEY_W
    #define KNST_WINDOW_KEY_CODE_X                  XKB_KEY_X
    #define KNST_WINDOW_KEY_CODE_Y                  XKB_KEY_Y
    #define KNST_WINDOW_KEY_CODE_Z                  XKB_KEY_Z

    // ── Turkish characters ─────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_C_CEDILLA          XKB_KEY_Ccedilla
    #define KNST_WINDOW_KEY_CODE_G_BREVE            XKB_KEY_Gbreve
    #define KNST_WINDOW_KEY_CODE_I_DOTLESS          XKB_KEY_I
    #define KNST_WINDOW_KEY_CODE_O_DIAERESIS        XKB_KEY_Odiaeresis
    #define KNST_WINDOW_KEY_CODE_S_CEDILLA          XKB_KEY_Scedilla
    #define KNST_WINDOW_KEY_CODE_U_DIAERESIS        XKB_KEY_Udiaeresis

    // ── Digits ─────────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_0                  XKB_KEY_0
    #define KNST_WINDOW_KEY_CODE_1                  XKB_KEY_1
    #define KNST_WINDOW_KEY_CODE_2                  XKB_KEY_2
    #define KNST_WINDOW_KEY_CODE_3                  XKB_KEY_3
    #define KNST_WINDOW_KEY_CODE_4                  XKB_KEY_4
    #define KNST_WINDOW_KEY_CODE_5                  XKB_KEY_5
    #define KNST_WINDOW_KEY_CODE_6                  XKB_KEY_6
    #define KNST_WINDOW_KEY_CODE_7                  XKB_KEY_7
    #define KNST_WINDOW_KEY_CODE_8                  XKB_KEY_8
    #define KNST_WINDOW_KEY_CODE_9                  XKB_KEY_9

    // ── Numpad ─────────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_NUMPAD_0           XKB_KEY_KP_0
    #define KNST_WINDOW_KEY_CODE_NUMPAD_1           XKB_KEY_KP_1
    #define KNST_WINDOW_KEY_CODE_NUMPAD_2           XKB_KEY_KP_2
    #define KNST_WINDOW_KEY_CODE_NUMPAD_3           XKB_KEY_KP_3
    #define KNST_WINDOW_KEY_CODE_NUMPAD_4           XKB_KEY_KP_4
    #define KNST_WINDOW_KEY_CODE_NUMPAD_5           XKB_KEY_KP_5
    #define KNST_WINDOW_KEY_CODE_NUMPAD_6           XKB_KEY_KP_6
    #define KNST_WINDOW_KEY_CODE_NUMPAD_7           XKB_KEY_KP_7
    #define KNST_WINDOW_KEY_CODE_NUMPAD_8           XKB_KEY_KP_8
    #define KNST_WINDOW_KEY_CODE_NUMPAD_9           XKB_KEY_KP_9
    #define KNST_WINDOW_KEY_CODE_NUMPAD_ADD         XKB_KEY_KP_Add
    #define KNST_WINDOW_KEY_CODE_NUMPAD_SUBTRACT    XKB_KEY_KP_Subtract
    #define KNST_WINDOW_KEY_CODE_NUMPAD_MULTIPLY    XKB_KEY_KP_Multiply
    #define KNST_WINDOW_KEY_CODE_NUMPAD_DIVIDE      XKB_KEY_KP_Divide
    #define KNST_WINDOW_KEY_CODE_NUMPAD_DECIMAL     XKB_KEY_KP_Decimal
    #define KNST_WINDOW_KEY_CODE_NUMPAD_ENTER       XKB_KEY_KP_Enter

    // ── Function keys ──────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_F1                 XKB_KEY_F1
    #define KNST_WINDOW_KEY_CODE_F2                 XKB_KEY_F2
    #define KNST_WINDOW_KEY_CODE_F3                 XKB_KEY_F3
    #define KNST_WINDOW_KEY_CODE_F4                 XKB_KEY_F4
    #define KNST_WINDOW_KEY_CODE_F5                 XKB_KEY_F5
    #define KNST_WINDOW_KEY_CODE_F6                 XKB_KEY_F6
    #define KNST_WINDOW_KEY_CODE_F7                 XKB_KEY_F7
    #define KNST_WINDOW_KEY_CODE_F8                 XKB_KEY_F8
    #define KNST_WINDOW_KEY_CODE_F9                 XKB_KEY_F9
    #define KNST_WINDOW_KEY_CODE_F10                XKB_KEY_F10
    #define KNST_WINDOW_KEY_CODE_F11                XKB_KEY_F11
    #define KNST_WINDOW_KEY_CODE_F12                XKB_KEY_F12

    // ── Special keys ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_ESCAPE             XKB_KEY_Escape
    #define KNST_WINDOW_KEY_CODE_ENTER              XKB_KEY_Return
    #define KNST_WINDOW_KEY_CODE_SPACE              XKB_KEY_space
    #define KNST_WINDOW_KEY_CODE_BACKSPACE          XKB_KEY_BackSpace
    #define KNST_WINDOW_KEY_CODE_TAB                XKB_KEY_Tab
    #define KNST_WINDOW_KEY_CODE_CAPS_LOCK          XKB_KEY_Caps_Lock
    #define KNST_WINDOW_KEY_CODE_NUM_LOCK           XKB_KEY_Num_Lock
    #define KNST_WINDOW_KEY_CODE_SCROLL_LOCK        XKB_KEY_Scroll_Lock

    // ── Modifier keys ──────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_SHIFT              XKB_KEY_Shift_L
    #define KNST_WINDOW_KEY_CODE_CONTROL            XKB_KEY_Control_L
    #define KNST_WINDOW_KEY_CODE_ALT                XKB_KEY_Alt_L
    #define KNST_WINDOW_KEY_CODE_SUPER              XKB_KEY_Super_L
    #define KNST_WINDOW_KEY_CODE_MENU               XKB_KEY_Menu

    // ── Arrow keys ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_LEFT               XKB_KEY_Left
    #define KNST_WINDOW_KEY_CODE_RIGHT              XKB_KEY_Right
    #define KNST_WINDOW_KEY_CODE_UP                 XKB_KEY_Up
    #define KNST_WINDOW_KEY_CODE_DOWN               XKB_KEY_Down

    // ── Navigation ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_HOME               XKB_KEY_Home
    #define KNST_WINDOW_KEY_CODE_END                XKB_KEY_End
    #define KNST_WINDOW_KEY_CODE_PAGE_UP            XKB_KEY_Page_Up
    #define KNST_WINDOW_KEY_CODE_PAGE_DOWN          XKB_KEY_Page_Down
    #define KNST_WINDOW_KEY_CODE_INSERT             XKB_KEY_Insert
    #define KNST_WINDOW_KEY_CODE_DELETE             XKB_KEY_Delete
    #define KNST_WINDOW_KEY_CODE_PRINT              XKB_KEY_Print
    #define KNST_WINDOW_KEY_CODE_PAUSE              XKB_KEY_Pause
    #define KNST_WINDOW_KEY_CODE_BREAK              XKB_KEY_Break

    // ── Symbol keys ────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_SEMICOLON          XKB_KEY_semicolon
    #define KNST_WINDOW_KEY_CODE_SLASH              XKB_KEY_slash
    #define KNST_WINDOW_KEY_CODE_GRAVE              XKB_KEY_grave
    #define KNST_WINDOW_KEY_CODE_LEFT_BRACKET       XKB_KEY_bracketleft
    #define KNST_WINDOW_KEY_CODE_BACKSLASH          XKB_KEY_backslash
    #define KNST_WINDOW_KEY_CODE_RIGHT_BRACKET      XKB_KEY_bracketright
    #define KNST_WINDOW_KEY_CODE_APOSTROPHE         XKB_KEY_apostrophe
    #define KNST_WINDOW_KEY_CODE_PERIOD             XKB_KEY_period
    #define KNST_WINDOW_KEY_CODE_COMMA              XKB_KEY_comma
    #define KNST_WINDOW_KEY_CODE_MINUS              XKB_KEY_minus
    #define KNST_WINDOW_KEY_CODE_PLUS               XKB_KEY_plus
    #define KNST_WINDOW_KEY_CODE_EQUAL              XKB_KEY_equal
    #define KNST_WINDOW_KEY_CODE_QUOTE              XKB_KEY_quotedbl
    #define KNST_WINDOW_KEY_CODE_COLON              XKB_KEY_colon
    #define KNST_WINDOW_KEY_CODE_TILDE              XKB_KEY_asciitilde
    #define KNST_WINDOW_KEY_CODE_LESS               XKB_KEY_less
    #define KNST_WINDOW_KEY_CODE_GREATER            XKB_KEY_greater
    #define KNST_WINDOW_KEY_CODE_QUESTION           XKB_KEY_question
    #define KNST_WINDOW_KEY_CODE_PIPE               XKB_KEY_bar
    #define KNST_WINDOW_KEY_CODE_LEFT_BRACE         XKB_KEY_braceleft
    #define KNST_WINDOW_KEY_CODE_RIGHT_BRACE        XKB_KEY_braceright
    #define KNST_WINDOW_KEY_CODE_UNDERSCORE         XKB_KEY_underscore

    // ── Media keys ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_VOLUME_UP          XKB_KEY_XF86AudioRaiseVolume
    #define KNST_WINDOW_KEY_CODE_VOLUME_DOWN        XKB_KEY_XF86AudioLowerVolume
    #define KNST_WINDOW_KEY_CODE_VOLUME_MUTE        XKB_KEY_XF86AudioMute
    #define KNST_WINDOW_KEY_CODE_MEDIA_PLAY         XKB_KEY_XF86AudioPlay
    #define KNST_WINDOW_KEY_CODE_MEDIA_STOP         XKB_KEY_XF86AudioStop
    #define KNST_WINDOW_KEY_CODE_MEDIA_NEXT         XKB_KEY_XF86AudioNext
    #define KNST_WINDOW_KEY_CODE_MEDIA_PREV         XKB_KEY_XF86AudioPrev
    #define KNST_WINDOW_KEY_CODE_MEDIA_PAUSE        XKB_KEY_XF86AudioPause

    // ── Browser keys ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_BROWSER_HOME       XKB_KEY_XF86HomePage
    #define KNST_WINDOW_KEY_CODE_BROWSER_BACK       XKB_KEY_XF86Back
    #define KNST_WINDOW_KEY_CODE_BROWSER_FORWARD    XKB_KEY_XF86Forward
    #define KNST_WINDOW_KEY_CODE_BROWSER_REFRESH    XKB_KEY_XF86Refresh
    #define KNST_WINDOW_KEY_CODE_BROWSER_SEARCH     XKB_KEY_XF86Search
    #define KNST_WINDOW_KEY_CODE_BROWSER_FAVORITES  XKB_KEY_XF86Favorites

    // ── Cursors (abstract IDs) ─────────────────────────────────────────────
    #define KNST_WINDOW_CURSOR_ARROW                1
    #define KNST_WINDOW_CURSOR_IBEAM                2
    #define KNST_WINDOW_CURSOR_CROSSHAIR            3
    #define KNST_WINDOW_CURSOR_HAND                 4
    #define KNST_WINDOW_CURSOR_HRESIZE              5
    #define KNST_WINDOW_CURSOR_VRESIZE              6
    #define KNST_WINDOW_CURSOR_MOVE                 7
    #define KNST_WINDOW_CURSOR_WAIT                 8
    #define KNST_WINDOW_CURSOR_HELP                 9
    #define KNST_WINDOW_CURSOR_NOT_ALLOWED          10
    #define KNST_WINDOW_CURSOR_HIDDEN               11
    #define KNST_WINDOW_CURSOR_DISABLED             12


// ────────────────────────────────────────────────────────────────────────────
//  ANDROID
// ────────────────────────────────────────────────────────────────────────────
#elif defined(KNST_USING_PLATFORM_ANDROID)

    // ── Letters A..Z (normalized to ASCII by handler) ──────────────────────
    #define KNST_WINDOW_KEY_CODE_A                  'A'
    #define KNST_WINDOW_KEY_CODE_B                  'B'
    #define KNST_WINDOW_KEY_CODE_C                  'C'
    #define KNST_WINDOW_KEY_CODE_D                  'D'
    #define KNST_WINDOW_KEY_CODE_E                  'E'
    #define KNST_WINDOW_KEY_CODE_F                  'F'
    #define KNST_WINDOW_KEY_CODE_G                  'G'
    #define KNST_WINDOW_KEY_CODE_H                  'H'
    #define KNST_WINDOW_KEY_CODE_I                  'I'
    #define KNST_WINDOW_KEY_CODE_J                  'J'
    #define KNST_WINDOW_KEY_CODE_K                  'K'
    #define KNST_WINDOW_KEY_CODE_L                  'L'
    #define KNST_WINDOW_KEY_CODE_M                  'M'
    #define KNST_WINDOW_KEY_CODE_N                  'N'
    #define KNST_WINDOW_KEY_CODE_O                  'O'
    #define KNST_WINDOW_KEY_CODE_P                  'P'
    #define KNST_WINDOW_KEY_CODE_Q                  'Q'
    #define KNST_WINDOW_KEY_CODE_R                  'R'
    #define KNST_WINDOW_KEY_CODE_S                  'S'
    #define KNST_WINDOW_KEY_CODE_T                  'T'
    #define KNST_WINDOW_KEY_CODE_U                  'U'
    #define KNST_WINDOW_KEY_CODE_V                  'V'
    #define KNST_WINDOW_KEY_CODE_W                  'W'
    #define KNST_WINDOW_KEY_CODE_X                  'X'
    #define KNST_WINDOW_KEY_CODE_Y                  'Y'
    #define KNST_WINDOW_KEY_CODE_Z                  'Z'

    // ── Digits (normalized to ASCII) ───────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_0                  '0'
    #define KNST_WINDOW_KEY_CODE_1                  '1'
    #define KNST_WINDOW_KEY_CODE_2                  '2'
    #define KNST_WINDOW_KEY_CODE_3                  '3'
    #define KNST_WINDOW_KEY_CODE_4                  '4'
    #define KNST_WINDOW_KEY_CODE_5                  '5'
    #define KNST_WINDOW_KEY_CODE_6                  '6'
    #define KNST_WINDOW_KEY_CODE_7                  '7'
    #define KNST_WINDOW_KEY_CODE_8                  '8'
    #define KNST_WINDOW_KEY_CODE_9                  '9'

    // ── Function keys ──────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_F1                 AKEYCODE_F1
    #define KNST_WINDOW_KEY_CODE_F2                 AKEYCODE_F2
    #define KNST_WINDOW_KEY_CODE_F3                 AKEYCODE_F3
    #define KNST_WINDOW_KEY_CODE_F4                 AKEYCODE_F4
    #define KNST_WINDOW_KEY_CODE_F5                 AKEYCODE_F5
    #define KNST_WINDOW_KEY_CODE_F6                 AKEYCODE_F6
    #define KNST_WINDOW_KEY_CODE_F7                 AKEYCODE_F7
    #define KNST_WINDOW_KEY_CODE_F8                 AKEYCODE_F8
    #define KNST_WINDOW_KEY_CODE_F9                 AKEYCODE_F9
    #define KNST_WINDOW_KEY_CODE_F10                AKEYCODE_F10
    #define KNST_WINDOW_KEY_CODE_F11                AKEYCODE_F11
    #define KNST_WINDOW_KEY_CODE_F12                AKEYCODE_F12

    // ── Special keys ───────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_ESCAPE             AKEYCODE_ESCAPE
    #define KNST_WINDOW_KEY_CODE_ENTER              AKEYCODE_ENTER
    #define KNST_WINDOW_KEY_CODE_SPACE              AKEYCODE_SPACE
    #define KNST_WINDOW_KEY_CODE_BACKSPACE          AKEYCODE_DEL
    #define KNST_WINDOW_KEY_CODE_TAB                AKEYCODE_TAB
    #define KNST_WINDOW_KEY_CODE_CAPS_LOCK          AKEYCODE_CAPS_LOCK
    #define KNST_WINDOW_KEY_CODE_NUM_LOCK           AKEYCODE_NUM_LOCK
    #define KNST_WINDOW_KEY_CODE_SCROLL_LOCK        AKEYCODE_SCROLL_LOCK
    #define KNST_WINDOW_KEY_CODE_DELETE             AKEYCODE_FORWARD_DEL
    #define KNST_WINDOW_KEY_CODE_INSERT             AKEYCODE_INSERT

    // ── Modifier keys ──────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_SHIFT              AKEYCODE_SHIFT_LEFT
    #define KNST_WINDOW_KEY_CODE_CONTROL            AKEYCODE_CTRL_LEFT
    #define KNST_WINDOW_KEY_CODE_ALT                AKEYCODE_ALT_LEFT
    #define KNST_WINDOW_KEY_CODE_SUPER              AKEYCODE_META_LEFT
    #define KNST_WINDOW_KEY_CODE_MENU               AKEYCODE_MENU

    // ── Arrow keys ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_LEFT               AKEYCODE_DPAD_LEFT
    #define KNST_WINDOW_KEY_CODE_RIGHT              AKEYCODE_DPAD_RIGHT
    #define KNST_WINDOW_KEY_CODE_UP                 AKEYCODE_DPAD_UP
    #define KNST_WINDOW_KEY_CODE_DOWN               AKEYCODE_DPAD_DOWN

    // ── Navigation ─────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_HOME               AKEYCODE_MOVE_HOME
    #define KNST_WINDOW_KEY_CODE_END                AKEYCODE_MOVE_END
    #define KNST_WINDOW_KEY_CODE_PAGE_UP            AKEYCODE_PAGE_UP
    #define KNST_WINDOW_KEY_CODE_PAGE_DOWN          AKEYCODE_PAGE_DOWN
    #define KNST_WINDOW_KEY_CODE_PRINT              AKEYCODE_SYSRQ
    #define KNST_WINDOW_KEY_CODE_PAUSE              AKEYCODE_MEDIA_PAUSE
    #define KNST_WINDOW_KEY_CODE_BREAK              AKEYCODE_BREAK

    // ── Numpad ─────────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_NUMPAD_0           AKEYCODE_NUMPAD_0
    #define KNST_WINDOW_KEY_CODE_NUMPAD_1           AKEYCODE_NUMPAD_1
    #define KNST_WINDOW_KEY_CODE_NUMPAD_2           AKEYCODE_NUMPAD_2
    #define KNST_WINDOW_KEY_CODE_NUMPAD_3           AKEYCODE_NUMPAD_3
    #define KNST_WINDOW_KEY_CODE_NUMPAD_4           AKEYCODE_NUMPAD_4
    #define KNST_WINDOW_KEY_CODE_NUMPAD_5           AKEYCODE_NUMPAD_5
    #define KNST_WINDOW_KEY_CODE_NUMPAD_6           AKEYCODE_NUMPAD_6
    #define KNST_WINDOW_KEY_CODE_NUMPAD_7           AKEYCODE_NUMPAD_7
    #define KNST_WINDOW_KEY_CODE_NUMPAD_8           AKEYCODE_NUMPAD_8
    #define KNST_WINDOW_KEY_CODE_NUMPAD_9           AKEYCODE_NUMPAD_9
    #define KNST_WINDOW_KEY_CODE_NUMPAD_ADD         AKEYCODE_NUMPAD_ADD
    #define KNST_WINDOW_KEY_CODE_NUMPAD_SUBTRACT    AKEYCODE_NUMPAD_SUBTRACT
    #define KNST_WINDOW_KEY_CODE_NUMPAD_MULTIPLY    AKEYCODE_NUMPAD_MULTIPLY
    #define KNST_WINDOW_KEY_CODE_NUMPAD_DIVIDE      AKEYCODE_NUMPAD_DIVIDE
    #define KNST_WINDOW_KEY_CODE_NUMPAD_DECIMAL     AKEYCODE_NUMPAD_DOT
    #define KNST_WINDOW_KEY_CODE_NUMPAD_ENTER       AKEYCODE_NUMPAD_ENTER

    // ── Symbol keys ────────────────────────────────────────────────────────
    #define KNST_WINDOW_KEY_CODE_SEMICOLON          AKEYCODE_SEMICOLON
    #define KNST_WINDOW_KEY_CODE_SLASH              AKEYCODE_SLASH
    #define KNST_WINDOW_KEY_CODE_GRAVE              AKEYCODE_GRAVE
    #define KNST_WINDOW_KEY_CODE_LEFT_BRACKET       AKEYCODE_LEFT_BRACKET
    #define KNST_WINDOW_KEY_CODE_BACKSLASH          AKEYCODE_BACKSLASH
    #define KNST_WINDOW_KEY_CODE_RIGHT_BRACKET      AKEYCODE_RIGHT_BRACKET
    #define KNST_WINDOW_KEY_CODE_APOSTROPHE         AKEYCODE_APOSTROPHE
    #define KNST_WINDOW_KEY_CODE_PERIOD             AKEYCODE_PERIOD
    #define KNST_WINDOW_KEY_CODE_COMMA              AKEYCODE_COMMA
    #define KNST_WINDOW_KEY_CODE_MINUS              AKEYCODE_MINUS
    #define KNST_WINDOW_KEY_CODE_PLUS               AKEYCODE_PLUS
    #define KNST_WINDOW_KEY_CODE_EQUAL              AKEYCODE_EQUALS

    // ── Mobile touch actions — m_event.touch_action ────────────────────────
    #define KNST_WINDOW_TOUCH_ACTION_PRESS          0
    #define KNST_WINDOW_TOUCH_ACTION_RELEASE        1
    #define KNST_WINDOW_TOUCH_ACTION_MOVE           2
    #define KNST_WINDOW_TOUCH_ACTION_CANCEL         3
    #define KNST_WINDOW_TOUCH_ACTION_OUTSIDE        4
    #define KNST_WINDOW_TOUCH_ACTION_POINTER_PRESS  5
    #define KNST_WINDOW_TOUCH_ACTION_POINTER_RELEASE 6

    // ── Screen orientation — m_event.orientation ───────────────────────────
    #define KNST_WINDOW_ORIENTATION_UNDEFINED       0
    #define KNST_WINDOW_ORIENTATION_PORTRAIT        1
    #define KNST_WINDOW_ORIENTATION_LANDSCAPE       2
    #define KNST_WINDOW_ORIENTATION_SQUARE          3

    // ── Cursors (Android has no cursor; stubs for API compatibility) ───────
    #define KNST_WINDOW_CURSOR_ARROW                1
    #define KNST_WINDOW_CURSOR_IBEAM                2
    #define KNST_WINDOW_CURSOR_CROSSHAIR            3
    #define KNST_WINDOW_CURSOR_HAND                 4
    #define KNST_WINDOW_CURSOR_HRESIZE              5
    #define KNST_WINDOW_CURSOR_VRESIZE              6
    #define KNST_WINDOW_CURSOR_MOVE                 7
    #define KNST_WINDOW_CURSOR_WAIT                 8
    #define KNST_WINDOW_CURSOR_HELP                 9
    #define KNST_WINDOW_CURSOR_NOT_ALLOWED          10
    #define KNST_WINDOW_CURSOR_HIDDEN               11
    #define KNST_WINDOW_CURSOR_DISABLED             12

#endif