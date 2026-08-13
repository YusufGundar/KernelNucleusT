// A simple example demonstrating the multi-window feature of a knst_window



#include <iostream>
#include <chrono>

// Windows automatic detection
//#define KNST_LINUX_PLATFORM_WAYLAND     If you are using Linux Wayland
//#define KNST_LINUX_PLATFORM_X11    If you are using Linux X11


#define KNST_LINUX_PLATFORM_X11


#include "../../include/KernelNucleusT.hpp"



int main(){



    KnstWindowSources::Init();



    knst_window window1(600,600,"Window 1");
    knst_window window2(600,600,"Window 2");


    window1.creation_and_show();
    window2.creation_and_show();




    while(!window1.is_should_close() || !window2.is_should_close()){



        knst_window_event_system::block_pool_event();


        if(window1.get_window_event_handle().type == KNST_CLOSE_WINDOW || window1.get_window_event_handle().type == KNST_DISCONNECT ||
        window2.get_window_event_handle().type == KNST_CLOSE_WINDOW || window2.get_window_event_handle().type == KNST_DISCONNECT ){

            window1.destroy();
            window2.destroy();

            window1.should_close();
            window2.should_close();
            
        }        
        
        if(window1.get_window_event_handle().key_code == KNST_KEY_W){
            std::cout << ".The 'W' key was triggered.   ~~~'Window 1' " << std::endl;
        }

        if(window2.get_window_event_handle().key_code == KNST_KEY_W){
            std::cout << ".The 'W' key was triggered.   ~~~'Window 2' " << std::endl;
        }


        window1.clear_temporary_events();
        window2.clear_temporary_events();

    }

    KnstWindowSources::CleanUp();

    std::cout << "Cleaning all sources..." << std::endl;
    return 0;
}
