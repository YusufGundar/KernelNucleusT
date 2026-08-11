#ifndef KNST_IMAGE_LOADER_HPP
#define KNST_IMAGE_LOADER_HPP
#pragma once



// image loader class , for now, it only supports BMP


#if KNST_USING_PLATFORM_LINUX
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif


#define KNST_BITMAP_16_16       (1 << 0)
#define KNST_BITMAP_24_24       (1 << 1)
#define KNST_BITMAP_32_32       (1 << 2)
#define KNST_BITMAP_48_48       (1 << 3)
#define KNST_BITMAP_64_64       (1 << 4)
#define KNST_BITMAP_96_96       (1 << 5)
#define KNST_BITMAP_128_128     (1 << 6)
#define KNST_BITMAP_256_256     (1 << 7)


#define KNST_BITMAP_OUTPUT_RGB   (1 << 8)   
#define KNST_BITMAP_OUTPUT_RGBA  (1 << 9)   
#define KNST_BITMAP_OUTPUT_BGR   (1 << 10)  
#define KNST_BITMAP_OUTPUT_BGRA  (1 << 11)  


#define KNST_BITMAP_GET_SIZE(flags)     ((flags) & 0xFF)
#define KNST_BITMAP_GET_FORMAT(flags)   ((flags) & 0xFF00)


inline int knst_bitmap_flag_to_size(int flags) noexcept {
    int size_flag = flags & 0xFF;
    switch (size_flag) {
        case KNST_BITMAP_16_16:   return 16;
        case KNST_BITMAP_24_24:   return 24;
        case KNST_BITMAP_32_32:   return 32;
        case KNST_BITMAP_48_48:   return 48;
        case KNST_BITMAP_64_64:   return 64;
        case KNST_BITMAP_96_96:   return 96;
        case KNST_BITMAP_128_128: return 128;
        case KNST_BITMAP_256_256: return 256;
        default: return 0;
    }
}


#pragma pack(push, 1)
struct BMPHeader {
    uint16_t signature;    
    uint32_t file_size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t data_offset;  
    uint32_t header_size;
    int32_t  width;
    int32_t  height;
    uint16_t planes;
    uint16_t bpp;          
    uint32_t compression;
    uint32_t image_size;
    int32_t  x_ppm;
    int32_t  y_ppm;
    uint32_t colors_used;
    uint32_t important_colors;
};
#pragma pack(pop)


class knst_image_loader {
public:
   
    static knst_byte_string load_bmp(
        const knst_c16string& path,
        int* out_width,
        int* out_height,
        int flags = KNST_BITMAP_OUTPUT_RGBA
    ) {
        knst_byte_string real_data(path);
        knst_byte_string result;
        
     
        uint8_t* file_data = nullptr;
        size_t file_size = 0;
        
        if (!read_file(real_data, &file_data, &file_size)) {
            return result;
        }
        

        BMPHeader* header = (BMPHeader*)file_data;
        if (header->signature != 0x4D42) { 
            free_file_data(file_data, file_size);
            return result;
        }
      
        int width = header->width;
        int height = abs(header->height);
        int bpp = header->bpp;
        bool bottom_up = header->height > 0;
        
       
        int output_format = flags & 0xFF00;
        int channels = 0;
        
        if (output_format == KNST_BITMAP_OUTPUT_RGB) {
            channels = 3;
        } else if (output_format == KNST_BITMAP_OUTPUT_RGBA) {
            channels = 4;
        } else if (output_format == KNST_BITMAP_OUTPUT_BGR) {
            channels = 3;
        } else if (output_format == KNST_BITMAP_OUTPUT_BGRA) {
            channels = 4;
        } else {
          
            channels = 4;
            output_format = KNST_BITMAP_OUTPUT_RGBA;
        }
        
       
        uint8_t* pixels = (uint8_t*)file_data + header->data_offset;
        int row_size = ((bpp * width + 31) / 32) * 4;
        
        size_t output_size = width * height * channels;
        uint8_t* output = new uint8_t[output_size];
        
        for (int y = 0; y < height; y++) {
            int src_y = bottom_up ? (height - 1 - y) : y;
            uint8_t* src_row = pixels + src_y * row_size;
            
            for (int x = 0; x < width; x++) {
                int si = x * (bpp / 8);
                int di = (y * width + x) * channels;
                
                
                uint8_t b = src_row[si + 0];
                uint8_t g = src_row[si + 1];
                uint8_t r = src_row[si + 2];
                uint8_t a = (bpp == 32) ? src_row[si + 3] : 255;
                
                switch (output_format) {
                    case KNST_BITMAP_OUTPUT_RGB:
                        output[di + 0] = r;
                        output[di + 1] = g;
                        output[di + 2] = b;
                        break;
                        
                    case KNST_BITMAP_OUTPUT_RGBA:
                        output[di + 0] = r;
                        output[di + 1] = g;
                        output[di + 2] = b;
                        output[di + 3] = a;
                        break;
                        
                    case KNST_BITMAP_OUTPUT_BGR:
                        output[di + 0] = b;
                        output[di + 1] = g;
                        output[di + 2] = r;
                        break;
                        
                    case KNST_BITMAP_OUTPUT_BGRA:
                        output[di + 0] = b;
                        output[di + 1] = g;
                        output[di + 2] = r;
                        output[di + 3] = a;
                        break;
                }
            }
        }
        
      
        int target_size = knst_bitmap_flag_to_size(flags);
        if (target_size > 0 && (width != target_size || height != target_size)) {
            uint8_t* resized = resize_image(output, width, height, channels, target_size, target_size);
            delete[] output;
            
            *out_width = target_size;
            *out_height = target_size;
            output = resized;
            output_size = target_size * target_size * channels;
        } else {
            *out_width = width;
            *out_height = height;
        }
        
        
        free_file_data(file_data, file_size);
        
        return knst_byte_string::take_ownership(output, output_size);
    }

private:
   
    static bool read_file(const knst_byte_string& path, uint8_t** out_data, size_t* out_size) {
#if KNST_USING_PLATFORM_WINDOWS
        HANDLE hFile = CreateFileA(
            (const char*)path.data(),
            GENERIC_READ,
            FILE_SHARE_READ,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );
        if (hFile == INVALID_HANDLE_VALUE) return false;
        
        DWORD fileSize = GetFileSize(hFile, NULL);
        if (fileSize == INVALID_FILE_SIZE) {
            CloseHandle(hFile);
            return false;
        }
        
        HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, fileSize, NULL);
        if (hMapping == NULL) {
            CloseHandle(hFile);
            return false;
        }
        
        *out_data = (uint8_t*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, fileSize);
        *out_size = fileSize;
        
        CloseHandle(hMapping);
        CloseHandle(hFile);
        
        return (*out_data != NULL);
        
#elif KNST_USING_PLATFORM_LINUX
        int fd = open((const char*)path.data(), O_RDONLY);
        if (fd < 0) return false;
        
        struct stat st;
        fstat(fd, &st);
        
        *out_data = (uint8_t*)mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        *out_size = st.st_size;
        
        close(fd);
        
        return (*out_data != MAP_FAILED);
#endif
    }
    
   
    static void free_file_data(uint8_t* data, size_t size) {
        if (!data) return;
        
#if KNST_USING_PLATFORM_WINDOWS
        UnmapViewOfFile(data);
#elif KNST_USING_PLATFORM_LINUX
        munmap(data, size);
#endif
    }
    
   
    static uint8_t* resize_image(
        const uint8_t* src,
        int src_w, int src_h,
        int channels,
        int dst_w, int dst_h
    ) {
        size_t dst_size = dst_w * dst_h * channels;
        uint8_t* dst = new uint8_t[dst_size];
        
        for (int y = 0; y < dst_h; y++) {
            for (int x = 0; x < dst_w; x++) {
                int src_x = x * src_w / dst_w;
                int src_y = y * src_h / dst_h;
                
                int si = (src_y * src_w + src_x) * channels;
                int di = (y * dst_w + x) * channels;
                
                for (int c = 0; c < channels; c++) {
                    dst[di + c] = src[si + c];
                }
            }
        }
        
        return dst;
    }
};

#endif  //KNST_IMAGE_LOADER_HPP