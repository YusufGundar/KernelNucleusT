#ifndef KNST_WINDOW_IDENTIFIERS_HPP
#define KNST_WINDOW_IDENTIFIERS_HPP
#pragma once





// Mouse action
#define KNST_MOUSE_BUTTON_PRESS   13
#define KNST_MOUSE_BUTTON_RELEASE 14
//______________________________________


// Keyboard action
#define KNST_KEY_PRESS            10
#define KNST_KEY_RELEASE          11
#define KNST_KEY_REPEAT           12
//______________________________________


// Mouse buttons
#define KNST_MOUSE_BUTTON_LEFT      1
#define KNST_MOUSE_BUTTON_MIDDLE    2
#define KNST_MOUSE_BUTTON_RIGHT     3
#define KNST_MOUSE_SCROLL_UP        4
#define KNST_MOUSE_SCROLL_DOWN      5
//______________________________________


#define KNST_KEYBOARD_EVENT       25
#define KNST_MOUSE_EVENT          26
#define KNST_UNKNOWN              0


#define KNST_WINDOW_FULL_SCREEN      8
#define KNST_WINDOW_RESTORE         9
#define KNST_WINDOW_MAXIMIZE        27
#define KNST_WINDOW_MINIMIZE        28

#define KNST_DISCONNECT         33

#define KNST_MOUSE_SCROLL         15

#define KNST_MOTION_NOTIFY        16
#define KNST_WINDOW_RESIZE        17
#define KNST_WINDOW_MOVE          18
#define KNST_CLOSE_WINDOW         19
#define KNST_FOCUS_IN             20
#define KNST_FOCUS_OUT            21
#define KNST_ENTER_NOTIFY         22
#define KNST_LEAVE_NOTIFY         23
#define KNST_EXPOSE               24





#define KNST_CURSOR_NORMAL    29   
#define KNST_CURSOR_HIDDEN    30
  
#define KNST_CURSOR_DISABLED  31  




#define KNST_FILE_DROP_ENTER    (1 << 13)  
#define KNST_FILE_DROP_MOVE     (1 << 14) 
#define KNST_FILE_DROP_LEAVE    (1 << 15) 
#define KNST_FILE_DROP          (1 << 16) 





#define KNST_DEFAULT              -10000



#define KNST_WINDOW_ATTRIB_DECORATED      110   
#define KNST_WINDOW_ATTRIB_RESIZABLE      111   
#define KNST_WINDOW_ATTRIB_ALWAYS_ON_TOP  112   
#define KNST_WINDOW_ATTRIB_TRANSPARENT    113   






#if KNST_USING_PLATFORM_WINDOWS
    #include <windows.h>

    #define KNST_KEY_A                      'A'
    #define KNST_KEY_B                      'B'
    #define KNST_KEY_C                      'C'
    #define KNST_KEY_D                      'D'
    #define KNST_KEY_E                      'E'
    #define KNST_KEY_F                      'F'
    #define KNST_KEY_G                      'G'
    #define KNST_KEY_H                      'H'
    #define KNST_KEY_I                      'I'
    #define KNST_KEY_J                      'J'
    #define KNST_KEY_K                      'K'
    #define KNST_KEY_L                      'L'
    #define KNST_KEY_M                      'M'
    #define KNST_KEY_N                      'N'
    #define KNST_KEY_O                      'O'
    #define KNST_KEY_P                      'P'
    #define KNST_KEY_Q                      'Q'
    #define KNST_KEY_R                      'R'
    #define KNST_KEY_S                      'S'
    #define KNST_KEY_T                      'T'
    #define KNST_KEY_U                      'U'
    #define KNST_KEY_V                      'V'
    #define KNST_KEY_W                      'W'
    #define KNST_KEY_X                      'X'
    #define KNST_KEY_Y                      'Y'
    #define KNST_KEY_Z                      'Z'

    #define KNST_KEY_C_CEDILLA              VK_OEM_4      // Ç
    #define KNST_KEY_G_BREVE                VK_OEM_5      // Ğ
    #define KNST_KEY_I_DOTLESS              VK_OEM_6      // ı
    #define KNST_KEY_O_DIAERESIS            VK_OEM_7      // Ö
    #define KNST_KEY_S_CEDILLA              VK_OEM_8      // Ş
    #define KNST_KEY_U_DIAERESIS            VK_OEM_102    // Ü

    #define KNST_KEY_0                      '0'
    #define KNST_KEY_1                      '1'
    #define KNST_KEY_2                      '2'
    #define KNST_KEY_3                      '3'
    #define KNST_KEY_4                      '4'
    #define KNST_KEY_5                      '5'
    #define KNST_KEY_6                      '6'
    #define KNST_KEY_7                      '7'
    #define KNST_KEY_8                      '8'
    #define KNST_KEY_9                      '9'

    
    #define KNST_KEY_NUMPAD_0               VK_NUMPAD0
    #define KNST_KEY_NUMPAD_1               VK_NUMPAD1
    #define KNST_KEY_NUMPAD_2               VK_NUMPAD2
    #define KNST_KEY_NUMPAD_3               VK_NUMPAD3
    #define KNST_KEY_NUMPAD_4               VK_NUMPAD4
    #define KNST_KEY_NUMPAD_5               VK_NUMPAD5
    #define KNST_KEY_NUMPAD_6               VK_NUMPAD6
    #define KNST_KEY_NUMPAD_7               VK_NUMPAD7
    #define KNST_KEY_NUMPAD_8               VK_NUMPAD8
    #define KNST_KEY_NUMPAD_9               VK_NUMPAD9
    #define KNST_KEY_NUMPAD_ADD             VK_ADD
    #define KNST_KEY_NUMPAD_SUBTRACT        VK_SUBTRACT
    #define KNST_KEY_NUMPAD_MULTIPLY        VK_MULTIPLY
    #define KNST_KEY_NUMPAD_DIVIDE          VK_DIVIDE
    #define KNST_KEY_NUMPAD_DECIMAL         VK_DECIMAL
    #define KNST_KEY_NUMPAD_ENTER           VK_RETURN

   
    #define KNST_KEY_F1                     VK_F1
    #define KNST_KEY_F2                     VK_F2
    #define KNST_KEY_F3                     VK_F3
    #define KNST_KEY_F4                     VK_F4
    #define KNST_KEY_F5                     VK_F5
    #define KNST_KEY_F6                     VK_F6
    #define KNST_KEY_F7                     VK_F7
    #define KNST_KEY_F8                     VK_F8
    #define KNST_KEY_F9                     VK_F9
    #define KNST_KEY_F10                    VK_F10
    #define KNST_KEY_F11                    VK_F11
    #define KNST_KEY_F12                    VK_F12

   
    #define KNST_KEY_ESCAPE                 VK_ESCAPE
    #define KNST_KEY_ENTER                  VK_RETURN
    #define KNST_KEY_SPACE                  VK_SPACE
    #define KNST_KEY_BACKSPACE              VK_BACK
    #define KNST_KEY_TAB                    VK_TAB
    #define KNST_KEY_CAPS_LOCK              VK_CAPITAL
    #define KNST_KEY_NUM_LOCK               VK_NUMLOCK
    #define KNST_KEY_SCROLL_LOCK            VK_SCROLL

    
    #define KNST_KEY_SHIFT                  VK_SHIFT
    #define KNST_KEY_CONTROL                VK_CONTROL
    #define KNST_KEY_ALT                    VK_MENU
    #define KNST_KEY_SUPER                  VK_LWIN
    #define KNST_KEY_MENU                   VK_APPS

    
    #define KNST_KEY_LEFT                   VK_LEFT
    #define KNST_KEY_RIGHT                  VK_RIGHT
    #define KNST_KEY_UP                     VK_UP
    #define KNST_KEY_DOWN                   VK_DOWN

    
    #define KNST_KEY_HOME                   VK_HOME
    #define KNST_KEY_END                    VK_END
    #define KNST_KEY_PAGE_UP                VK_PRIOR
    #define KNST_KEY_PAGE_DOWN              VK_NEXT
    #define KNST_KEY_INSERT                 VK_INSERT
    #define KNST_KEY_DELETE                 VK_DELETE
    #define KNST_KEY_PRINT                  VK_SNAPSHOT
    #define KNST_KEY_PAUSE                  VK_PAUSE
    #define KNST_KEY_BREAK                  VK_CANCEL

  
    #define KNST_KEY_SEMICOLON              VK_OEM_1      // ;
    #define KNST_KEY_SLASH                  VK_OEM_2      // /
    #define KNST_KEY_GRAVE                  VK_OEM_3      // `
    #define KNST_KEY_LEFT_BRACKET           VK_OEM_4      // [
    #define KNST_KEY_BACKSLASH              VK_OEM_5      // '\'
    #define KNST_KEY_RIGHT_BRACKET          VK_OEM_6      // ]
    #define KNST_KEY_APOSTROPHE             VK_OEM_7      // '
    #define KNST_KEY_PERIOD                 VK_OEM_PERIOD // .
    #define KNST_KEY_COMMA                  VK_OEM_COMMA  // ,
    #define KNST_KEY_MINUS                  VK_OEM_MINUS  // -
    #define KNST_KEY_PLUS                   VK_OEM_PLUS   // +
    #define KNST_KEY_QUOTE                  VK_OEM_7      // "
    #define KNST_KEY_COLON                  VK_OEM_1      // :
    #define KNST_KEY_TILDE                  VK_OEM_3      // ~
    #define KNST_KEY_LESS                   VK_OEM_102    // <
    #define KNST_KEY_GREATER                VK_OEM_102    // >
    #define KNST_KEY_QUESTION               VK_OEM_2      // ?
    #define KNST_KEY_PIPE                   VK_OEM_5      // |
    #define KNST_KEY_EXCLAM                 '1'           // !
    #define KNST_KEY_AT                     '2'           // @
    #define KNST_KEY_HASH                   '3'           // #
    #define KNST_KEY_DOLLAR                 '4'           // $
    #define KNST_KEY_PERCENT                '5'           // %
    #define KNST_KEY_CIRCUMFLEX             '6'           // ^
    #define KNST_KEY_AMPERSAND              '7'           // &
    #define KNST_KEY_ASTERISK               '8'           // *
    #define KNST_KEY_LEFT_PAREN             '9'           // (
    #define KNST_KEY_RIGHT_PAREN            '0'           // )
    #define KNST_KEY_UNDERSCORE             VK_OEM_MINUS  // _
    #define KNST_KEY_EQUAL                  VK_OEM_PLUS   // =
    #define KNST_KEY_LEFT_BRACE             VK_OEM_4      // {
    #define KNST_KEY_RIGHT_BRACE            VK_OEM_6      // }

   
    #define KNST_KEY_VOLUME_UP              VK_VOLUME_UP
    #define KNST_KEY_VOLUME_DOWN            VK_VOLUME_DOWN
    #define KNST_KEY_VOLUME_MUTE            VK_VOLUME_MUTE
    #define KNST_KEY_MEDIA_PLAY             VK_MEDIA_PLAY_PAUSE
    #define KNST_KEY_MEDIA_STOP             VK_MEDIA_STOP
    #define KNST_KEY_MEDIA_NEXT             VK_MEDIA_NEXT_TRACK
    #define KNST_KEY_MEDIA_PREV             VK_MEDIA_PREV_TRACK
    #define KNST_KEY_MEDIA_PAUSE            VK_MEDIA_PLAY_PAUSE

    
    #define KNST_KEY_BROWSER_HOME           VK_BROWSER_HOME
    #define KNST_KEY_BROWSER_BACK           VK_BROWSER_BACK
    #define KNST_KEY_BROWSER_FORWARD        VK_BROWSER_FORWARD
    #define KNST_KEY_BROWSER_REFRESH        VK_BROWSER_REFRESH
    #define KNST_KEY_BROWSER_SEARCH         VK_BROWSER_SEARCH
    #define KNST_KEY_BROWSER_FAVORITES      VK_BROWSER_FAVORITES

   
    #define KNST_MOD_SHIFT                  MK_SHIFT
    #define KNST_MOD_CONTROL                MK_CONTROL
    #define KNST_MOD_ALT                    MOD_ALT
    #define KNST_MOD_SUPER                  MOD_WIN
    #define KNST_MOD_CAPS_LOCK              VK_CAPITAL
    #define KNST_MOD_NUM_LOCK               VK_NUMLOCK

    
  
    #define KNST_CURSOR_ARROW          OCR_NORMAL
    #define KNST_CURSOR_IBEAM          OCR_IBEAM
    #define KNST_CURSOR_CROSSHAIR      OCR_CROSS
    #define KNST_CURSOR_HAND           OCR_HAND
    #define KNST_CURSOR_HRESIZE        OCR_SIZEWE
    #define KNST_CURSOR_VRESIZE        OCR_SIZENS
    #define KNST_CURSOR_MOVE           OCR_SIZEALL
    #define KNST_CURSOR_WAIT           OCR_WAIT
    #define KNST_CURSOR_HELP           OCR_HELP
    #define KNST_CURSOR_NOT_ALLOWED    OCR_NO


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

    #define KNST_KEY_A                      XK_a
    #define KNST_KEY_B                      XK_b
    #define KNST_KEY_C                      XK_c
    #define KNST_KEY_D                      XK_d
    #define KNST_KEY_E                      XK_e
    #define KNST_KEY_F                      XK_f
    #define KNST_KEY_G                      XK_g
    #define KNST_KEY_H                      XK_h
    #define KNST_KEY_I                      XK_i
    #define KNST_KEY_J                      XK_j
    #define KNST_KEY_K                      XK_k
    #define KNST_KEY_L                      XK_l
    #define KNST_KEY_M                      XK_m
    #define KNST_KEY_N                      XK_n
    #define KNST_KEY_O                      XK_o
    #define KNST_KEY_P                      XK_p
    #define KNST_KEY_Q                      XK_q
    #define KNST_KEY_R                      XK_r
    #define KNST_KEY_S                      XK_s
    #define KNST_KEY_T                      XK_t
    #define KNST_KEY_U                      XK_u
    #define KNST_KEY_V                      XK_v
    #define KNST_KEY_W                      XK_w
    #define KNST_KEY_X                      XK_x
    #define KNST_KEY_Y                      XK_y
    #define KNST_KEY_Z                      XK_z

    
    #define KNST_KEY_C_CEDILLA              XK_Ccedilla
    #define KNST_KEY_G_BREVE                XK_Gbreve
    #define KNST_KEY_I_DOTLESS              XK_idotless
    #define KNST_KEY_O_DIAERESIS            XK_Odiaeresis
    #define KNST_KEY_S_CEDILLA              XK_Scedilla
    #define KNST_KEY_U_DIAERESIS            XK_Udiaeresis

    #define KNST_KEY_0                      XK_0
    #define KNST_KEY_1                      XK_1
    #define KNST_KEY_2                      XK_2
    #define KNST_KEY_3                      XK_3
    #define KNST_KEY_4                      XK_4
    #define KNST_KEY_5                      XK_5
    #define KNST_KEY_6                      XK_6
    #define KNST_KEY_7                      XK_7
    #define KNST_KEY_8                      XK_8
    #define KNST_KEY_9                      XK_9

   
    #define KNST_KEY_NUMPAD_0               XK_KP_0
    #define KNST_KEY_NUMPAD_1               XK_KP_1
    #define KNST_KEY_NUMPAD_2               XK_KP_2
    #define KNST_KEY_NUMPAD_3               XK_KP_3
    #define KNST_KEY_NUMPAD_4               XK_KP_4
    #define KNST_KEY_NUMPAD_5               XK_KP_5
    #define KNST_KEY_NUMPAD_6               XK_KP_6
    #define KNST_KEY_NUMPAD_7               XK_KP_7
    #define KNST_KEY_NUMPAD_8               XK_KP_8
    #define KNST_KEY_NUMPAD_9               XK_KP_9
    #define KNST_KEY_NUMPAD_ADD             XK_KP_Add
    #define KNST_KEY_NUMPAD_SUBTRACT        XK_KP_Subtract
    #define KNST_KEY_NUMPAD_MULTIPLY        XK_KP_Multiply
    #define KNST_KEY_NUMPAD_DIVIDE          XK_KP_Divide
    #define KNST_KEY_NUMPAD_DECIMAL         XK_KP_Decimal
    #define KNST_KEY_NUMPAD_ENTER           XK_KP_Enter

   
    #define KNST_KEY_F1                     XK_F1
    #define KNST_KEY_F2                     XK_F2
    #define KNST_KEY_F3                     XK_F3
    #define KNST_KEY_F4                     XK_F4
    #define KNST_KEY_F5                     XK_F5
    #define KNST_KEY_F6                     XK_F6
    #define KNST_KEY_F7                     XK_F7
    #define KNST_KEY_F8                     XK_F8
    #define KNST_KEY_F9                     XK_F9
    #define KNST_KEY_F10                    XK_F10
    #define KNST_KEY_F11                    XK_F11
    #define KNST_KEY_F12                    XK_F12

    
    #define KNST_KEY_ESCAPE                 XK_Escape
    #define KNST_KEY_ENTER                  XK_Return
    #define KNST_KEY_SPACE                  XK_space
    #define KNST_KEY_BACKSPACE              XK_BackSpace
    #define KNST_KEY_TAB                    XK_Tab
    #define KNST_KEY_CAPS_LOCK              XK_Caps_Lock
    #define KNST_KEY_NUM_LOCK               XK_Num_Lock
    #define KNST_KEY_SCROLL_LOCK            XK_Scroll_Lock

   
    #define KNST_KEY_SHIFT                  XK_Shift_L
    #define KNST_KEY_CONTROL                XK_Control_L
    #define KNST_KEY_ALT                    XK_Alt_L
    #define KNST_KEY_SUPER                  XK_Super_L
    #define KNST_KEY_MENU                   XK_Menu

   
    #define KNST_KEY_LEFT                   XK_Left
    #define KNST_KEY_RIGHT                  XK_Right
    #define KNST_KEY_UP                     XK_Up
    #define KNST_KEY_DOWN                   XK_Down

    
    #define KNST_KEY_HOME                   XK_Home
    #define KNST_KEY_END                    XK_End
    #define KNST_KEY_PAGE_UP                XK_Page_Up
    #define KNST_KEY_PAGE_DOWN              XK_Page_Down
    #define KNST_KEY_INSERT                 XK_Insert
    #define KNST_KEY_DELETE                 XK_Delete
    #define KNST_KEY_PRINT                  XK_Print
    #define KNST_KEY_PAUSE                  XK_Pause
    #define KNST_KEY_BREAK                  XK_Break

  
    #define KNST_KEY_SEMICOLON              XK_semicolon
    #define KNST_KEY_SLASH                  XK_slash
    #define KNST_KEY_GRAVE                  XK_grave
    #define KNST_KEY_LEFT_BRACKET           XK_bracketleft
    #define KNST_KEY_BACKSLASH              XK_backslash
    #define KNST_KEY_RIGHT_BRACKET          XK_bracketright
    #define KNST_KEY_APOSTROPHE             XK_apostrophe
    #define KNST_KEY_PERIOD                 XK_period
    #define KNST_KEY_COMMA                  XK_comma
    #define KNST_KEY_MINUS                  XK_minus
    #define KNST_KEY_PLUS                   XK_plus
    #define KNST_KEY_QUOTE                  XK_quotedbl
    #define KNST_KEY_COLON                  XK_colon
    #define KNST_KEY_TILDE                  XK_asciitilde
    #define KNST_KEY_LESS                   XK_less
    #define KNST_KEY_GREATER                XK_greater
    #define KNST_KEY_QUESTION               XK_question
    #define KNST_KEY_PIPE                   XK_bar
    #define KNST_KEY_EXCLAM                 XK_exclam
    #define KNST_KEY_AT                     XK_at
    #define KNST_KEY_HASH                   XK_numbersign
    #define KNST_KEY_DOLLAR                 XK_dollar
    #define KNST_KEY_PERCENT                XK_percent
    #define KNST_KEY_CIRCUMFLEX             XK_asciicircum
    #define KNST_KEY_AMPERSAND              XK_ampersand
    #define KNST_KEY_ASTERISK               XK_asterisk
    #define KNST_KEY_LEFT_PAREN             XK_parenleft
    #define KNST_KEY_RIGHT_PAREN            XK_parenright
    #define KNST_KEY_UNDERSCORE             XK_underscore
    #define KNST_KEY_EQUAL                  XK_equal
    #define KNST_KEY_LEFT_BRACE             XK_braceleft
    #define KNST_KEY_RIGHT_BRACE            XK_braceright

   
    #define KNST_KEY_VOLUME_UP              XF86XK_AudioRaiseVolume
    #define KNST_KEY_VOLUME_DOWN            XF86XK_AudioLowerVolume
    #define KNST_KEY_VOLUME_MUTE            XF86XK_AudioMute
    #define KNST_KEY_MEDIA_PLAY             XF86XK_AudioPlay
    #define KNST_KEY_MEDIA_STOP             XF86XK_AudioStop
    #define KNST_KEY_MEDIA_NEXT             XF86XK_AudioNext
    #define KNST_KEY_MEDIA_PREV             XF86XK_AudioPrev
    #define KNST_KEY_MEDIA_PAUSE            XF86XK_AudioPause

    
    #define KNST_KEY_BROWSER_HOME           XF86XK_HomePage
    #define KNST_KEY_BROWSER_BACK           XF86XK_Back
    #define KNST_KEY_BROWSER_FORWARD        XF86XK_Forward
    #define KNST_KEY_BROWSER_REFRESH        XF86XK_Refresh
    #define KNST_KEY_BROWSER_SEARCH         XF86XK_Search
    #define KNST_KEY_BROWSER_FAVORITES      XF86XK_Favorites

   
    #define KNST_MOD_SHIFT                  ShiftMask
    #define KNST_MOD_CONTROL                ControlMask
    #define KNST_MOD_ALT                    Mod1Mask
    #define KNST_MOD_SUPER                  Mod4Mask
    #define KNST_MOD_CAPS_LOCK              LockMask
    #define KNST_MOD_NUM_LOCK               Mod2Mask

    
   


    #define KNST_CURSOR_ARROW          XC_left_ptr
    #define KNST_CURSOR_IBEAM          XC_xterm
    #define KNST_CURSOR_CROSSHAIR      XC_crosshair
    #define KNST_CURSOR_HAND           XC_hand2
    #define KNST_CURSOR_HRESIZE        XC_sb_h_double_arrow
    #define KNST_CURSOR_VRESIZE        XC_sb_v_double_arrow
    #define KNST_CURSOR_MOVE           XC_fleur
    #define KNST_CURSOR_WAIT           XC_watch
    #define KNST_CURSOR_HELP           XC_question_arrow
    #define KNST_CURSOR_NOT_ALLOWED    XC_X_cursor

#elif KNST_USING_LINUX_PLATFORM_WAYLAND
    #include <xkbcommon/xkbcommon-keysyms.h>
    #include <wayland-client.h>
    #include "../linux/wayland/protocol_files/xdg-shell-client-protocol.h"
    #include "../linux/wayland/protocol_files/xdg-output-client-protocol.h"
    #include "../linux/wayland/protocol_files/xdg-decoration-client-protocol.h"
    #include "../linux/wayland/protocol_files/relative-pointer-unstable-v1-client-protocol.h"
    #include "../linux/wayland/protocol_files/pointer-constraints-unstable-v1-client-protocol.h"
   

    

    #define KNST_KEY_A                      XKB_KEY_A   // 65
    #define KNST_KEY_B                      XKB_KEY_B   // 66
    #define KNST_KEY_C                      XKB_KEY_C   // 67
    #define KNST_KEY_D                      XKB_KEY_D   // 68
    #define KNST_KEY_E                      XKB_KEY_E   // 69
    #define KNST_KEY_F                      XKB_KEY_F   // 70
    #define KNST_KEY_G                      XKB_KEY_G   // 71
    #define KNST_KEY_H                      XKB_KEY_H   // 72
    #define KNST_KEY_I                      XKB_KEY_I   // 73
    #define KNST_KEY_J                      XKB_KEY_J   // 74
    #define KNST_KEY_K                      XKB_KEY_K   // 75
    #define KNST_KEY_L                      XKB_KEY_L   // 76
    #define KNST_KEY_M                      XKB_KEY_M   // 77
    #define KNST_KEY_N                      XKB_KEY_N   // 78
    #define KNST_KEY_O                      XKB_KEY_O   // 79
    #define KNST_KEY_P                      XKB_KEY_P   // 80
    #define KNST_KEY_Q                      XKB_KEY_Q   // 81
    #define KNST_KEY_R                      XKB_KEY_R   // 82
    #define KNST_KEY_S                      XKB_KEY_S   // 83  
    #define KNST_KEY_T                      XKB_KEY_T   // 84
    #define KNST_KEY_U                      XKB_KEY_U   // 85
    #define KNST_KEY_V                      XKB_KEY_V   // 86
    #define KNST_KEY_W                      XKB_KEY_W   // 87
    #define KNST_KEY_X                      XKB_KEY_X   // 88
    #define KNST_KEY_Y                      XKB_KEY_Y   // 89
    #define KNST_KEY_Z                      XKB_KEY_Z   // 90

    #define KNST_KEY_C_CEDILLA              XKB_KEY_Ccedilla      // Ç
    #define KNST_KEY_G_BREVE                XKB_KEY_Gbreve        // Ğ
    #define KNST_KEY_I_DOTLESS              XKB_KEY_idotless      // ı
    #define KNST_KEY_O_DIAERESIS            XKB_KEY_Odiaeresis    // Ö
    #define KNST_KEY_S_CEDILLA              XKB_KEY_Scedilla      // Ş
    #define KNST_KEY_U_DIAERESIS            XKB_KEY_Udiaeresis    // Ü

   
    #define KNST_KEY_0                      XKB_KEY_0
    #define KNST_KEY_1                      XKB_KEY_1
    #define KNST_KEY_2                      XKB_KEY_2
    #define KNST_KEY_3                      XKB_KEY_3
    #define KNST_KEY_4                      XKB_KEY_4
    #define KNST_KEY_5                      XKB_KEY_5
    #define KNST_KEY_6                      XKB_KEY_6
    #define KNST_KEY_7                      XKB_KEY_7
    #define KNST_KEY_8                      XKB_KEY_8
    #define KNST_KEY_9                      XKB_KEY_9

   
    #define KNST_KEY_NUMPAD_0               XKB_KEY_KP_0
    #define KNST_KEY_NUMPAD_1               XKB_KEY_KP_1
    #define KNST_KEY_NUMPAD_2               XKB_KEY_KP_2
    #define KNST_KEY_NUMPAD_3               XKB_KEY_KP_3
    #define KNST_KEY_NUMPAD_4               XKB_KEY_KP_4
    #define KNST_KEY_NUMPAD_5               XKB_KEY_KP_5
    #define KNST_KEY_NUMPAD_6               XKB_KEY_KP_6
    #define KNST_KEY_NUMPAD_7               XKB_KEY_KP_7
    #define KNST_KEY_NUMPAD_8               XKB_KEY_KP_8
    #define KNST_KEY_NUMPAD_9               XKB_KEY_KP_9
    #define KNST_KEY_NUMPAD_ADD             XKB_KEY_KP_Add
    #define KNST_KEY_NUMPAD_SUBTRACT        XKB_KEY_KP_Subtract
    #define KNST_KEY_NUMPAD_MULTIPLY        XKB_KEY_KP_Multiply
    #define KNST_KEY_NUMPAD_DIVIDE          XKB_KEY_KP_Divide
    #define KNST_KEY_NUMPAD_DECIMAL         XKB_KEY_KP_Decimal
    #define KNST_KEY_NUMPAD_ENTER           XKB_KEY_KP_Enter

   
    #define KNST_KEY_F1                     XKB_KEY_F1
    #define KNST_KEY_F2                     XKB_KEY_F2
    #define KNST_KEY_F3                     XKB_KEY_F3
    #define KNST_KEY_F4                     XKB_KEY_F4
    #define KNST_KEY_F5                     XKB_KEY_F5
    #define KNST_KEY_F6                     XKB_KEY_F6
    #define KNST_KEY_F7                     XKB_KEY_F7
    #define KNST_KEY_F8                     XKB_KEY_F8
    #define KNST_KEY_F9                     XKB_KEY_F9
    #define KNST_KEY_F10                    XKB_KEY_F10
    #define KNST_KEY_F11                    XKB_KEY_F11
    #define KNST_KEY_F12                    XKB_KEY_F12

   
    #define KNST_KEY_ESCAPE                 XKB_KEY_Escape
    #define KNST_KEY_ENTER                  XKB_KEY_Return
    #define KNST_KEY_SPACE                  XKB_KEY_space
    #define KNST_KEY_BACKSPACE              XKB_KEY_BackSpace
    #define KNST_KEY_TAB                    XKB_KEY_Tab
    #define KNST_KEY_CAPS_LOCK              XKB_KEY_Caps_Lock
    #define KNST_KEY_NUM_LOCK               XKB_KEY_Num_Lock
    #define KNST_KEY_SCROLL_LOCK            XKB_KEY_Scroll_Lock

   
    #define KNST_KEY_SHIFT                  XKB_KEY_Shift_L
    #define KNST_KEY_CONTROL                XKB_KEY_Control_L
    #define KNST_KEY_ALT                    XKB_KEY_Alt_L
    #define KNST_KEY_SUPER                  XKB_KEY_Super_L
    #define KNST_KEY_MENU                   XKB_KEY_Menu

  
    #define KNST_KEY_LEFT                   XKB_KEY_Left
    #define KNST_KEY_RIGHT                  XKB_KEY_Right
    #define KNST_KEY_UP                     XKB_KEY_Up
    #define KNST_KEY_DOWN                   XKB_KEY_Down

   
    #define KNST_KEY_HOME                   XKB_KEY_Home
    #define KNST_KEY_END                    XKB_KEY_End
    #define KNST_KEY_PAGE_UP                XKB_KEY_Page_Up
    #define KNST_KEY_PAGE_DOWN              XKB_KEY_Page_Down
    #define KNST_KEY_INSERT                 XKB_KEY_Insert
    #define KNST_KEY_DELETE                 XKB_KEY_Delete
    #define KNST_KEY_PRINT                  XKB_KEY_Print
    #define KNST_KEY_PAUSE                  XKB_KEY_Pause
    #define KNST_KEY_BREAK                  XKB_KEY_Break

  
    #define KNST_KEY_SEMICOLON              XKB_KEY_semicolon       // ;
    #define KNST_KEY_SLASH                  XKB_KEY_slash           // /
    #define KNST_KEY_GRAVE                  XKB_KEY_grave           // `
    #define KNST_KEY_LEFT_BRACKET           XKB_KEY_bracketleft     // [
    #define KNST_KEY_BACKSLASH              XKB_KEY_backslash       // '\'
    #define KNST_KEY_RIGHT_BRACKET          XKB_KEY_bracketright    // ]
    #define KNST_KEY_APOSTROPHE             XKB_KEY_apostrophe      // '
    #define KNST_KEY_PERIOD                 XKB_KEY_period          // .
    #define KNST_KEY_COMMA                  XKB_KEY_comma           // ,
    #define KNST_KEY_MINUS                  XKB_KEY_minus           // -
    #define KNST_KEY_PLUS                   XKB_KEY_plus            // +
    #define KNST_KEY_QUOTE                  XKB_KEY_quotedbl        // "
    #define KNST_KEY_COLON                  XKB_KEY_colon           // :
    #define KNST_KEY_TILDE                  XKB_KEY_asciitilde      // ~
    #define KNST_KEY_LESS                   XKB_KEY_less            // <
    #define KNST_KEY_GREATER                XKB_KEY_greater         // >
    #define KNST_KEY_QUESTION               XKB_KEY_question        // ?
    #define KNST_KEY_PIPE                   XKB_KEY_bar             // |
    #define KNST_KEY_EXCLAM                 XKB_KEY_exclam          // !
    #define KNST_KEY_AT                     XKB_KEY_at              // @
    #define KNST_KEY_HASH                   XKB_KEY_numbersign      // #
    #define KNST_KEY_DOLLAR                 XKB_KEY_dollar          // $
    #define KNST_KEY_PERCENT                XKB_KEY_percent         // %
    #define KNST_KEY_CIRCUMFLEX             XKB_KEY_asciicircum     // ^
    #define KNST_KEY_AMPERSAND              XKB_KEY_ampersand       // &
    #define KNST_KEY_ASTERISK               XKB_KEY_asterisk        // *
    #define KNST_KEY_LEFT_PAREN             XKB_KEY_parenleft       // (
    #define KNST_KEY_RIGHT_PAREN            XKB_KEY_parenright      // )
    #define KNST_KEY_UNDERSCORE             XKB_KEY_underscore      // _
    #define KNST_KEY_EQUAL                  XKB_KEY_equal           // =
    #define KNST_KEY_LEFT_BRACE             XKB_KEY_braceleft       // {
    #define KNST_KEY_RIGHT_BRACE            XKB_KEY_braceright      // }

    
    #define KNST_KEY_VOLUME_UP              XKB_KEY_XF86AudioRaiseVolume
    #define KNST_KEY_VOLUME_DOWN            XKB_KEY_XF86AudioLowerVolume
    #define KNST_KEY_VOLUME_MUTE            XKB_KEY_XF86AudioMute
    #define KNST_KEY_MEDIA_PLAY             XKB_KEY_XF86AudioPlay
    #define KNST_KEY_MEDIA_STOP             XKB_KEY_XF86AudioStop
    #define KNST_KEY_MEDIA_NEXT             XKB_KEY_XF86AudioNext
    #define KNST_KEY_MEDIA_PREV             XKB_KEY_XF86AudioPrev
    #define KNST_KEY_MEDIA_PAUSE            XKB_KEY_XF86AudioPause

    #define KNST_KEY_BROWSER_HOME           XKB_KEY_XF86HomePage
    #define KNST_KEY_BROWSER_BACK           XKB_KEY_XF86Back
    #define KNST_KEY_BROWSER_FORWARD        XKB_KEY_XF86Forward
    #define KNST_KEY_BROWSER_REFRESH        XKB_KEY_XF86Refresh
    #define KNST_KEY_BROWSER_SEARCH         XKB_KEY_XF86Search
    #define KNST_KEY_BROWSER_FAVORITES      XKB_KEY_XF86Favorites

   
    #define KNST_MOD_SHIFT                  1       // Shift
    #define KNST_MOD_CONTROL                4       // Ctrl
    #define KNST_MOD_ALT                    8       // Alt
    #define KNST_MOD_SUPER                  64      // Windows/Super
    #define KNST_MOD_CAPS_LOCK              2       // Caps Lock
    #define KNST_MOD_NUM_LOCK               16      // Num Lock

   
    #define KNST_CURSOR_ARROW          1
    #define KNST_CURSOR_IBEAM          2
    #define KNST_CURSOR_CROSSHAIR      3
    #define KNST_CURSOR_HAND           4
    #define KNST_CURSOR_HRESIZE        5
    #define KNST_CURSOR_VRESIZE        6
    #define KNST_CURSOR_MOVE           7
    #define KNST_CURSOR_WAIT           8
    #define KNST_CURSOR_HELP           9
    #define KNST_CURSOR_NOT_ALLOWED    10


#elif defined(KNST_USING_PLATFORM_ANDROID)

   
    #define KNST_WINDOW_LOST           10000  
    #define KNST_LOW_MEMORY            10002 
    #define KNST_APP_STARTED           10003  
    #define KNST_APP_RESUMED           10004 
    #define KNST_APP_PAUSED            10005 
    #define KNST_SAVE_STATE            10006 
    #define KNST_CONTENT_RECT_CHANGED  10007  
    #define KNST_CONFIG_CHANGED        10008  
    #define KNST_INPUT_CHANGED         10009 
    #define KNST_APP_STOPPED           10046 

    #define KNST_MOBILE_TOUCH_EVENT    10017  

   
    #define KNST_MOBILE_KEY_EVENT      10018  

   
    #define KNST_MOBILE_BACK_PRESS     10040  
    #define KNST_MOBILE_HOME_PRESS     10041  
    #define KNST_MOBILE_MENU_PRESS     10042  
    #define KNST_MOBILE_SEARCH_PRESS   10043  
    #define KNST_MOBILE_VOLUME_UP      10044
    #define KNST_MOBILE_VOLUME_DOWN    10045
    #define KNST_MOBILE_APP_SWITCH     10047
    #define KNST_MOBILE_RECENT_APPS    10048
    #define KNST_MOBILE_VOLUME_MUTE    10049
    #define KNST_MOBILE_POWER          10050
    #define KNST_MOBILE_CAMERA         10051
    #define KNST_MOBILE_HELP           10052
    #define KNST_MOBILE_SETTINGS       10053
    #define KNST_MOBILE_SLEEP          10054
    #define KNST_MOBILE_WAKEUP         10055

   
    #define KNST_MOBILE_MEDIA_PLAY_PAUSE       10060
    #define KNST_MOBILE_MEDIA_STOP             10061
    #define KNST_MOBILE_MEDIA_NEXT             10062
    #define KNST_MOBILE_MEDIA_PREVIOUS         10063
    #define KNST_MOBILE_MEDIA_REWIND           10064
    #define KNST_MOBILE_MEDIA_FAST_FORWARD     10065
    #define KNST_MOBILE_MEDIA_RECORD           10066
    #define KNST_MOBILE_MEDIA_CLOSE            10067
    #define KNST_MOBILE_MEDIA_EJECT            10068
    #define KNST_MOBILE_MEDIA_PAUSE            10069

    
    #define KNST_MOBILE_NOTIFICATION        10080
    #define KNST_MOBILE_ASSIST              10083
    #define KNST_MOBILE_VOICE_ASSIST        10084
    #define KNST_MOBILE_BOOKMARK            10085
    #define KNST_MOBILE_CALCULATOR          10086
    #define KNST_MOBILE_CALENDAR            10087
    #define KNST_MOBILE_CONTACTS            10088
    #define KNST_MOBILE_EXPLORER            10090
    #define KNST_MOBILE_MUSIC               10092

   
    #define KNST_KEY_NUMPAD_COMMA          10094
    #define KNST_KEY_NUMPAD_EQUALS         10095
    #define KNST_KEY_NUMPAD_LEFT_PAREN     10096
    #define KNST_KEY_NUMPAD_RIGHT_PAREN    10097

    
    #define KNST_KEY_A                      AKEYCODE_A
    #define KNST_KEY_B                      AKEYCODE_B
    #define KNST_KEY_C                      AKEYCODE_C
    #define KNST_KEY_D                      AKEYCODE_D
    #define KNST_KEY_E                      AKEYCODE_E
    #define KNST_KEY_F                      AKEYCODE_F
    #define KNST_KEY_G                      AKEYCODE_G
    #define KNST_KEY_H                      AKEYCODE_H
    #define KNST_KEY_I                      AKEYCODE_I
    #define KNST_KEY_J                      AKEYCODE_J
    #define KNST_KEY_K                      AKEYCODE_K
    #define KNST_KEY_L                      AKEYCODE_L
    #define KNST_KEY_M                      AKEYCODE_M
    #define KNST_KEY_N                      AKEYCODE_N
    #define KNST_KEY_O                      AKEYCODE_O
    #define KNST_KEY_P                      AKEYCODE_P
    #define KNST_KEY_Q                      AKEYCODE_Q
    #define KNST_KEY_R                      AKEYCODE_R
    #define KNST_KEY_S                      AKEYCODE_S
    #define KNST_KEY_T                      AKEYCODE_T
    #define KNST_KEY_U                      AKEYCODE_U
    #define KNST_KEY_V                      AKEYCODE_V
    #define KNST_KEY_W                      AKEYCODE_W
    #define KNST_KEY_X                      AKEYCODE_X
    #define KNST_KEY_Y                      AKEYCODE_Y
    #define KNST_KEY_Z                      AKEYCODE_Z

    
    #define KNST_KEY_0                      AKEYCODE_0
    #define KNST_KEY_1                      AKEYCODE_1
    #define KNST_KEY_2                      AKEYCODE_2
    #define KNST_KEY_3                      AKEYCODE_3
    #define KNST_KEY_4                      AKEYCODE_4
    #define KNST_KEY_5                      AKEYCODE_5
    #define KNST_KEY_6                      AKEYCODE_6
    #define KNST_KEY_7                      AKEYCODE_7
    #define KNST_KEY_8                      AKEYCODE_8
    #define KNST_KEY_9                      AKEYCODE_9

   
    #define KNST_KEY_F1                     AKEYCODE_F1
    #define KNST_KEY_F2                     AKEYCODE_F2
    #define KNST_KEY_F3                     AKEYCODE_F3
    #define KNST_KEY_F4                     AKEYCODE_F4
    #define KNST_KEY_F5                     AKEYCODE_F5
    #define KNST_KEY_F6                     AKEYCODE_F6
    #define KNST_KEY_F7                     AKEYCODE_F7
    #define KNST_KEY_F8                     AKEYCODE_F8
    #define KNST_KEY_F9                     AKEYCODE_F9
    #define KNST_KEY_F10                    AKEYCODE_F10
    #define KNST_KEY_F11                    AKEYCODE_F11
    #define KNST_KEY_F12                    AKEYCODE_F12

    
    #define KNST_KEY_ESCAPE                 AKEYCODE_ESCAPE
    #define KNST_KEY_ENTER                  AKEYCODE_ENTER
    #define KNST_KEY_SPACE                  AKEYCODE_SPACE
    #define KNST_KEY_BACKSPACE              AKEYCODE_DEL
    #define KNST_KEY_TAB                    AKEYCODE_TAB
    #define KNST_KEY_CAPS_LOCK              AKEYCODE_CAPS_LOCK
    #define KNST_KEY_NUM_LOCK               AKEYCODE_NUM_LOCK
    #define KNST_KEY_SCROLL_LOCK            AKEYCODE_SCROLL_LOCK
    #define KNST_KEY_DELETE                 AKEYCODE_FORWARD_DEL
    #define KNST_KEY_INSERT                 AKEYCODE_INSERT

    
    #define KNST_KEY_SHIFT                  AKEYCODE_SHIFT_LEFT
    #define KNST_KEY_CONTROL                AKEYCODE_CTRL_LEFT
    #define KNST_KEY_ALT                    AKEYCODE_ALT_LEFT
    #define KNST_KEY_SUPER                  AKEYCODE_META_LEFT
    #define KNST_KEY_MENU                   AKEYCODE_MENU

    
    #define KNST_KEY_LEFT                   AKEYCODE_DPAD_LEFT
    #define KNST_KEY_RIGHT                  AKEYCODE_DPAD_RIGHT
    #define KNST_KEY_UP                     AKEYCODE_DPAD_UP
    #define KNST_KEY_DOWN                   AKEYCODE_DPAD_DOWN
    #define KNST_KEY_HOME                   AKEYCODE_MOVE_HOME
    #define KNST_KEY_END                    AKEYCODE_MOVE_END
    #define KNST_KEY_PAGE_UP                AKEYCODE_PAGE_UP
    #define KNST_KEY_PAGE_DOWN              AKEYCODE_PAGE_DOWN
    #define KNST_KEY_PRINT                  AKEYCODE_SYSRQ
    #define KNST_KEY_PAUSE                  AKEYCODE_MEDIA_PAUSE
    #define KNST_KEY_BREAK                  AKEYCODE_BREAK


    #define KNST_KEY_NUMPAD_0               AKEYCODE_NUMPAD_0
    #define KNST_KEY_NUMPAD_1               AKEYCODE_NUMPAD_1
    #define KNST_KEY_NUMPAD_2               AKEYCODE_NUMPAD_2
    #define KNST_KEY_NUMPAD_3               AKEYCODE_NUMPAD_3
    #define KNST_KEY_NUMPAD_4               AKEYCODE_NUMPAD_4
    #define KNST_KEY_NUMPAD_5               AKEYCODE_NUMPAD_5
    #define KNST_KEY_NUMPAD_6               AKEYCODE_NUMPAD_6
    #define KNST_KEY_NUMPAD_7               AKEYCODE_NUMPAD_7
    #define KNST_KEY_NUMPAD_8               AKEYCODE_NUMPAD_8
    #define KNST_KEY_NUMPAD_9               AKEYCODE_NUMPAD_9
    #define KNST_KEY_NUMPAD_ADD             AKEYCODE_NUMPAD_ADD
    #define KNST_KEY_NUMPAD_SUBTRACT        AKEYCODE_NUMPAD_SUBTRACT
    #define KNST_KEY_NUMPAD_MULTIPLY        AKEYCODE_NUMPAD_MULTIPLY
    #define KNST_KEY_NUMPAD_DIVIDE          AKEYCODE_NUMPAD_DIVIDE
    #define KNST_KEY_NUMPAD_DECIMAL         AKEYCODE_NUMPAD_DOT
    #define KNST_KEY_NUMPAD_ENTER           AKEYCODE_NUMPAD_ENTER
    #define KNST_KEY_NUMPAD_COMMA           AKEYCODE_NUMPAD_COMMA
    #define KNST_KEY_NUMPAD_EQUALS          AKEYCODE_NUMPAD_EQUALS
    #define KNST_KEY_NUMPAD_LEFT_PAREN      AKEYCODE_NUMPAD_LEFT_PAREN
    #define KNST_KEY_NUMPAD_RIGHT_PAREN     AKEYCODE_NUMPAD_RIGHT_PAREN

   
    #define KNST_KEY_SEMICOLON              AKEYCODE_SEMICOLON
    #define KNST_KEY_SLASH                  AKEYCODE_SLASH
    #define KNST_KEY_GRAVE                  AKEYCODE_GRAVE
    #define KNST_KEY_LEFT_BRACKET           AKEYCODE_LEFT_BRACKET
    #define KNST_KEY_BACKSLASH              AKEYCODE_BACKSLASH
    #define KNST_KEY_RIGHT_BRACKET          AKEYCODE_RIGHT_BRACKET
    #define KNST_KEY_APOSTROPHE             AKEYCODE_APOSTROPHE
    #define KNST_KEY_PERIOD                 AKEYCODE_PERIOD
    #define KNST_KEY_COMMA                  AKEYCODE_COMMA
    #define KNST_KEY_MINUS                  AKEYCODE_MINUS
    #define KNST_KEY_PLUS                   AKEYCODE_PLUS
    #define KNST_KEY_EQUALS                 AKEYCODE_EQUALS

    
    #define KNST_MOD_SHIFT                  AMETA_SHIFT_ON
    #define KNST_MOD_CONTROL                AMETA_CTRL_ON
    #define KNST_MOD_ALT                    AMETA_ALT_ON
    #define KNST_MOD_SUPER                  AMETA_META_ON
    #define KNST_MOD_CAPS_LOCK              AMETA_CAPS_LOCK_ON
    #define KNST_MOD_NUM_LOCK               AMETA_NUM_LOCK_ON

    
    #define KNST_MOBILE_TOUCH_ACTION_PRESS          0   
    #define KNST_MOBILE_TOUCH_ACTION_RELEASE        1 
    #define KNST_MOBILE_TOUCH_ACTION_MOVE           2   
    #define KNST_MOBILE_TOUCH_ACTION_CANCEL         3   
    #define KNST_MOBILE_TOUCH_ACTION_OUTSIDE        4   
    #define KNST_MOBILE_TOUCH_ACTION_POINTER_PRESS  5   
    #define KNST_MOBILE_TOUCH_ACTION_POINTER_RELEASE 6  

    
    #define KNST_MOBILE_ORIENTATION_UNDEFINED  0
    #define KNST_MOBILE_ORIENTATION_PORTRAIT   1
    #define KNST_MOBILE_ORIENTATION_LANDSCAPE  2
    #define KNST_MOBILE_ORIENTATION_SQUARE     3

   
    #define KNST_MOBILE_NIGHT_MODE_OFF         0
    #define KNST_MOBILE_NIGHT_MODE_ON          1

#endif

#endif // KNST_WINDOW_IDENTIFIERS_HPP
