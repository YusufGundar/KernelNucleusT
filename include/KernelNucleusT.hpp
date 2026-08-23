#ifndef KERNEL_NUCLEUS_STRUCTS_HPP
#define KERNEL_NUCLEUS_STRUCTS_HPP
#pragma once





// structural
#include "knst_global_functions.hpp"
#include "knst_c16string.hpp"
#include "knst_byte_string.hpp"
#include "knst_vector.hpp"

#include "knst_image_loader.hpp"

// _end structural






// knst_window

#include "knst_window.hpp"

// _end knst_window

// knst_gui_framework
#ifdef KNST_USING_VULKAN
    #include "knst_obj_loader.hpp"
    #include "knst_gui_framework.hpp"

#endif
// knst_gui_framework





#endif // KERNEL_NUCLEUS_STRUCTS_HPP