#define KNST_VULKAN_VALIDATION
#include "../../include/KernelNucleusT.hpp"



int main(){


    // Just stay tuned; I’ve started working on a major project—a large GUI framework—that I’ll be sharing with you all.

    KnstWindowSources::Init();

    knst_window window(800,800,"Deneme");
    window.creation_and_show();

    knst_window_vulkan_content vk_content;
    vk_content.Init(window);
    

    while(!window.is_should_close()){






        if(window.get_window_event_handle().type == KNST_CLOSE_WINDOW || window.get_window_event_handle().type == KNST_DISCONNECT){
            vk_content.Destroy();
            knst_window_vulkan_content::DestroyGlobalSources();
            window.destroy();
            window.should_close();
        }


    }

    KnstWindowSources::CleanUp();

}