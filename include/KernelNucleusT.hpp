/*
----------------------------
KernelNucleusT.hpp
----------------------------

    It is the file where all the library's core and bundled structures are included together

*/




#pragma once


#include <glm/glm.hpp> // include glm
#include <glm/gtc/matrix_transform.hpp> // include glm


// structural
#include "knst_global_functions.hpp"
#include "knst_c16string.hpp"
#include "knst_byte_string.hpp"
#include "knst_vector.hpp"




#include "knst_function.hpp"
#include "knst_thread_priority.hpp"
#include "knst_thread_queue.hpp"
#include "knst_thread.hpp"
#include "knst_thread_pool.hpp"

#include "knst_image_loader.hpp"

// _end structural


#include <knst_vertex_structs.hpp>



// knst_window

#include "knst_window.hpp"

// _end knst_window

// knst_gui_framework
#ifdef KNST_USING_VULKAN
    #include "knst_obj_loader.hpp"
    #include "knst_gui_framework.hpp"

#endif
// knst_gui_framework





