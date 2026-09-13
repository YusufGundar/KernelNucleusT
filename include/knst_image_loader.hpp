/*
----------------------------
knst_image_loader.hpp
----------------------------

    Reads PNG, BMP, TGA, and PPM images—includes its own DEFLATE decoder and does not depend on zlib. Converts to RGBA, RGB, BGRA, or BGR formats; offers high performance via SIMD (SSE2/NEON) and multi-threaded row processing. Uses memory-mapped I/O
    It is used within the library to load and apply GUI and cursor images

*/




#pragma once


#include <cmath>
#include <utility>
#include <thread>
#include <algorithm>
#include <functional>

#include "knst_function.hpp"
#include "knst_thread_priority.hpp"
#include "knst_thread.hpp"
#include "knst_thread_queue.hpp"
#include "knst_thread_pool.hpp"




#if KNST_USING_PLATFORM_LINUX
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

#if KNST_USING_PLATFORM_WINDOWS
    #include <memoryapi.h>
#endif

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #include <emmintrin.h>
    #define KNST_HAS_SSE2 1
#endif

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
    #include <arm_neon.h>
    #define KNST_HAS_NEON 1
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


static const uint32_t KNST_MAX_IMAGE_DIMENSION = 16384;

inline int knst_bitmap_flag_to_size(int flags) noexcept { // Extracts the size from the bitmap flag. KNST_BITMAP_128_128 ==> 128, KNST_BITMAP_16_16 ==> 16. Unknown flag ==> 0
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
struct BMPHeader { // BMP file header. #pragma pack(push, 1) ==> no padding; fields are strictly byte-aligned (as required by the file format). Signature 0x4D42 ("BM"), followed by size, offset, width, height, bit depth, compression, etc.; read directly via header->width/height
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


class KnstInflate {
public:
    static bool Inflate(const uint8_t* input, size_t inputSize, knst_byte_string& output) { // It validates the zlib format and extracts the deflate data. It checks the header (CM=8 for deflate, checksum %31) and then passes the data to InflateDeflate. It allocates estimated space for the output (3x + 64)
        if (inputSize < 6) return false;

        uint8_t cmf = input[0];
        uint8_t flg = input[1];

        if ((cmf & 0x0F) != 8) return false;
        if (flg & 0x20) return false;
        if (((uint32_t)cmf * 256 + flg) % 31 != 0) return false;

        const uint8_t* deflateData = input + 2;
        size_t deflateSize = inputSize - 2 - 4;

        output.reserve(deflateSize * 3 + 64);

        return InflateDeflate(deflateData, deflateSize, output);
    }

private:
    struct BitReader { // Bit reader — Operates at the DEFLATE bit level. ReadBits(n) ==> reads N bits (LSB-first); ReadBit() ==> reads a single bit; AlignByte() ==> aligns to the byte boundary (for stored blocks); HasData() ==> checks if data remains. Uses a buffer and a bit counter (bitsInBuffer) to enable bit-by-bit reading rather than byte-by-byte reading
        const uint8_t* data;
        size_t size;
        size_t bytePos = 0;
        uint32_t buffer = 0;
        uint8_t bitsInBuffer = 0;

        BitReader(const uint8_t* d, size_t s) : data(d), size(s) {}

        
        uint32_t ReadBits(uint8_t count) {
            if (count > 32) return 0;
            
            while (bitsInBuffer < count && bytePos < size) {
                buffer |= ((uint32_t)data[bytePos++]) << bitsInBuffer;
                bitsInBuffer += 8;
            }
            uint32_t result = (count == 0) ? 0 : (buffer & ((count < 32) ? ((1u << count) - 1u) : 0xFFFFFFFFu));
            if (bitsInBuffer >= count) {
                buffer >>= count;
                bitsInBuffer -= count;
            } else {
                buffer = 0;
                bitsInBuffer = 0;
            }
            return result;
        }

        uint8_t ReadBit() {
            if (bitsInBuffer == 0) {
                if (bytePos >= size) return 0;
                buffer = data[bytePos++];
                bitsInBuffer = 8;
            }
            uint8_t bit = buffer & 1;
            buffer >>= 1;
            bitsInBuffer--;
            return bit;
        }

        void AlignByte() {
            bitsInBuffer = 0;
            buffer = 0;
        }

        bool HasData() const { return bytePos < size || bitsInBuffer > 0; }
        size_t GetBytePos() const { return bytePos; }
        size_t GetSize() const { return size; }
    };

  
   
    struct HuffmanTable { // Huffman decoding table. Build ==> constructs a canonical Huffman table from code lengths (counts = number of codes for each length, symbols = ordered symbols). Decode ==> reads bit by bit to find the matching symbol. The heart of DEFLATE — converts compressed data into symbols
        uint16_t counts[16];
        uint16_t symbols[288];
        int numSymbols = 0;

        HuffmanTable() { for (int i = 0; i < 16; i++) counts[i] = 0; }

        
        void Build(const uint8_t* lengths, int n) {
            for (int i = 0; i < 16; i++) counts[i] = 0;
            numSymbols = n;

            for (int i = 0; i < n; i++) {
                if (lengths[i] <= 15) counts[lengths[i]]++;
            }
            counts[0] = 0; 

            uint16_t offsets[16];
            offsets[0] = 0;
            offsets[1] = 0;
            for (int len = 1; len < 15; len++) {
                offsets[len + 1] = (uint16_t)(offsets[len] + counts[len]);
            }

            for (int sym = 0; sym < n; sym++) {
                uint8_t len = lengths[sym];
                if (len != 0 && len <= 15) {
                    symbols[offsets[len]++] = (uint16_t)sym;
                }
            }
        }

        bool IsValid() const {
            for (int i = 1; i <= 15; i++) if (counts[i] != 0) return true;
            return false;
        }

       
        int Decode(BitReader& reader) const {
            int code = 0;
            int first = 0;
            int index = 0;
            for (int len = 1; len <= 15; len++) {
                code |= reader.ReadBit();
                int count = counts[len];
                int rel = code - first;
                if (rel < count) {
                    if (index + rel >= numSymbols) return -1;
                    return symbols[index + rel];
                }
                index += count;
                first += count;
                first <<= 1;
                code <<= 1;
            }
            return -1;
        }
    };

    struct LengthCode { uint32_t base; uint8_t extraBits; }; // Length/distance code information. `base` ==> minimum value, `extraBits` ==> number of extra bits to read. In DEFLATE, for example: "length 3–10, 1 extra bit."

    static const LengthCode GetLengthInfo(uint16_t code) { // Converts the DEFLATE length code into a table entry. Symbols 257–285 map to {base, extraBits}. For example, code 265 ==> {11, 1} (11 + 0/1 = copy 11–12 bytes). Invalid code ==> {0, 0}
        static const LengthCode table[29] = {
            {3,0},{4,0},{5,0},{6,0},{7,0},{8,0},{9,0},{10,0},
            {11,1},{13,1},{15,1},{17,1},{19,2},{23,2},{27,2},{31,2},
            {35,3},{43,3},{51,3},{59,3},{67,4},{83,4},{99,4},{115,4},
            {131,5},{163,5},{195,5},{227,5},{258,0}
        };
        if (code >= 257 && code <= 285) return table[code - 257];
        return {0,0};
    }

    static const LengthCode GetDistanceInfo(uint16_t code) { // Converts the DEFLATE distance code into a table. Symbol 0–29 ==> returns {base, extraBits}. For example, code=10 ==> {33, 4} (33 + 0–15 = how far back to copy). Invalid code ==> {0, 0}
        static const LengthCode table[30] = {
            {1,0},{2,0},{3,0},{4,0},{5,1},{7,1},{9,2},{13,2},
            {17,3},{25,3},{33,4},{49,4},{65,5},{97,5},{129,6},{193,6},
            {257,7},{385,7},{513,8},{769,8},{1025,9},{1537,9},{2049,10},
            {3073,10},{4097,11},{6145,11},{8193,12},{12289,12},{16385,13},
            {24577,13}
        };
        if (code <= 29) return table[code];
        return {0,0};
    }

    static HuffmanTable BuildHuffmanTable(const knst_vector<uint8_t>& lengths) { // Constructs a Huffman table from a list of lengths. Limits *n* to 288 (the DEFLATE maximum literal/length symbol count). Delegates to `Build`
        HuffmanTable table;
        int n = (int)lengths.size();
        if (n > 288) n = 288;
        table.Build(lengths.data(), n);
        return table;
    }

   
    static const HuffmanTable& StaticLiteralTable() { // DEFLATE's fixed literal/length Huffman table. It is set up once using standard fixed lengths (as defined in RFC 1951) and statically cached; there is no need to recalculate it for each block
        static const HuffmanTable table = [] {
            uint8_t lengths[288];
            for (int i = 0;   i < 144; i++) lengths[i] = 8;
            for (int i = 144; i < 256; i++) lengths[i] = 9;
            for (int i = 256; i < 280; i++) lengths[i] = 7;
            for (int i = 280; i < 288; i++) lengths[i] = 8;
            HuffmanTable t;
            t.Build(lengths, 288);
            return t;
        }();
        return table;
    }

    static const HuffmanTable& StaticDistanceTable() { // DEFLATE's fixed distance Huffman table. 32 symbols, all 5 bits (RFC 1951 standard). It is set up once using `static` and cached
        static const HuffmanTable table = [] {
            uint8_t lengths[32];
            for (int i = 0; i < 32; i++) lengths[i] = 5;
            HuffmanTable t;
            t.Build(lengths, 32);
            return t;
        }();
        return table;
    }

    static bool InflateDeflate(const uint8_t* input, size_t inputSize, knst_byte_string& output) { // The DEFLATE main loop. At the beginning of each block, it reads `bfinal` (whether it is the last block) and `btype` (block type). It supports three types: stored (uncompressed), static Huffman, and dynamic Huffman. There is a 100MB output limit (protection against decompression bomb attacks). It continues until all blocks are processed or `bfinal` is 1
        if (inputSize == 0 || input == nullptr) return false;

        BitReader reader(input, inputSize);
        const size_t MAX_OUTPUT_SIZE = 100 * 1024 * 1024;

        while (reader.HasData()) {
            uint8_t bfinal = reader.ReadBit();
            uint8_t btype = reader.ReadBits(2);

            if (btype == 0) {
                reader.AlignByte();
                if (reader.GetBytePos() + 4 > reader.GetSize()) return false;

                uint16_t len = reader.data[reader.bytePos] | (reader.data[reader.bytePos + 1] << 8);
                reader.bytePos += 2;
                uint16_t nlen = reader.data[reader.bytePos] | (reader.data[reader.bytePos + 1] << 8);
                reader.bytePos += 2;

                if ((len ^ 0xFFFF) != nlen) return false;
                if (reader.bytePos + len > reader.size) return false;
                if (output.length() + len > MAX_OUTPUT_SIZE) return false;

                output.append(reader.data + reader.bytePos, len);
                reader.bytePos += len;
            } else if (btype == 1) {
                if (!DecodeHuffmanBlock(reader, StaticLiteralTable(), StaticDistanceTable(), output, MAX_OUTPUT_SIZE))
                    return false;
            } else if (btype == 2) {
                HuffmanTable lit, dist;
                if (!BuildDynamicTables(reader, lit, dist)) return false;
                if (!DecodeHuffmanBlock(reader, lit, dist, output, MAX_OUTPUT_SIZE)) return false;
            } else {
                return false;
            }

            if (bfinal) break;
        }
        return true;
    }

    static bool BuildDynamicTables(BitReader& reader, HuffmanTable& litTable, HuffmanTable& distTable) { // DEFLATE sets up the tables for dynamic Huffman blocks. It reads the hlit, hdist, and hclen counts and decodes the code lengths using clTable. It handles the 16, 17, and 18 repeat symbols (shortcuts). The result is the preparation of litTable (literal/length) and distTable (distance). The subsequent DecodeHuffmanBlock uses these tables
        uint16_t hlit = reader.ReadBits(5) + 257;
        uint16_t hdist = reader.ReadBits(5) + 1;
        uint16_t hclen = reader.ReadBits(4) + 4;

        if (hlit > 288 || hdist > 32 || hclen > 19) return false;

        const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

        uint8_t codeLengths[19] = {0};
        for (uint16_t i = 0; i < hclen; i++) {
            codeLengths[order[i]] = (uint8_t)reader.ReadBits(3);
        }

        HuffmanTable clTable;
        clTable.Build(codeLengths, 19);
        if (!clTable.IsValid()) return false;

        knst_vector<uint8_t> litLengths, distLengths;
        litLengths.resize(hlit);
        distLengths.resize(hdist);

        size_t litIdx = 0;
        while (litIdx < hlit) {
            int symbol = clTable.Decode(reader);
            if (symbol < 0) return false;

            if (symbol < 16) {
                litLengths[litIdx++] = (uint8_t)symbol;
            } else if (symbol == 16) {
                if (litIdx == 0) return false;
                uint8_t repeat = (uint8_t)(reader.ReadBits(2) + 3);
                uint8_t prev = litLengths[litIdx - 1];
                for (uint8_t j = 0; j < repeat && litIdx < hlit; j++) litLengths[litIdx++] = prev;
            } else if (symbol == 17) {
                uint8_t repeat = (uint8_t)(reader.ReadBits(3) + 3);
                for (uint8_t j = 0; j < repeat && litIdx < hlit; j++) litLengths[litIdx++] = 0;
            } else if (symbol == 18) {
                uint8_t repeat = (uint8_t)(reader.ReadBits(7) + 11);
                for (uint8_t j = 0; j < repeat && litIdx < hlit; j++) litLengths[litIdx++] = 0;
            } else {
                return false;
            }
        }

        size_t distIdx = 0;
        while (distIdx < hdist) {
            int symbol = clTable.Decode(reader);
            if (symbol < 0) return false;

            if (symbol < 16) {
                distLengths[distIdx++] = (uint8_t)symbol;
            } else if (symbol == 16) {
                if (distIdx == 0) return false;
                uint8_t repeat = (uint8_t)(reader.ReadBits(2) + 3);
                uint8_t prev = distLengths[distIdx - 1];
                for (uint8_t j = 0; j < repeat && distIdx < hdist; j++) distLengths[distIdx++] = prev;
            } else if (symbol == 17) {
                uint8_t repeat = (uint8_t)(reader.ReadBits(3) + 3);
                for (uint8_t j = 0; j < repeat && distIdx < hdist; j++) distLengths[distIdx++] = 0;
            } else if (symbol == 18) {
                uint8_t repeat = (uint8_t)(reader.ReadBits(7) + 11);
                for (uint8_t j = 0; j < repeat && distIdx < hdist; j++) distLengths[distIdx++] = 0;
            } else {
                return false;
            }
        }

        litTable = BuildHuffmanTable(litLengths);
        distTable = BuildHuffmanTable(distLengths);
        return litTable.IsValid() && distTable.IsValid();
    }

    static bool DecodeHuffmanBlock(BitReader& reader, const HuffmanTable& litTable, // Decodes the Huffman block. Symbols: 0–255 ==> literal byte, 256 ==> end of block, 257–285 ==> length-distance pair (LZ77 back-reference). Uses `memcpy` for back-references (sufficient distance) or byte-by-byte copying (overlapping range). Loops until 256 is encountered
                                    const HuffmanTable& distTable, knst_byte_string& output,
                                    size_t maxOutputSize) {
        while (true) {
            int symbol = litTable.Decode(reader);
            if (symbol < 0) return false;

            if (symbol <= 255) {
                if (output.length() + 1 > maxOutputSize) return false;
                output.push_back((uint8_t)symbol);
            } else if (symbol == 256) {
                break;
            } else if (symbol <= 285) {
                LengthCode lenInfo = GetLengthInfo((uint16_t)symbol);
                if (lenInfo.base == 0 && lenInfo.extraBits == 0) return false;

                uint32_t length = lenInfo.base + reader.ReadBits(lenInfo.extraBits);

                int distSymbol = distTable.Decode(reader);
                if (distSymbol < 0) return false;

                LengthCode distInfo = GetDistanceInfo((uint16_t)distSymbol);
                if (distInfo.base == 0 && distInfo.extraBits == 0) return false;

                uint32_t distance = distInfo.base + reader.ReadBits(distInfo.extraBits);

                if (distance == 0 || distance > output.length()) return false;
                if (output.length() + length > maxOutputSize) return false;

               
                size_t start = output.length();
                size_t copyFrom = start - distance;
                output.resize(start + length);
                if (distance >= length) {
                    std::memcpy(&output[start], &output[copyFrom], length);
                } else {
                    for (uint32_t i = 0; i < length; i++) {
                        output[start + i] = output[copyFrom + i];
                    }
                }
            } else {
                return false;
            }
        }
        return true;
    }
};


class knst_image_loader {
public:
    static knst_byte_string load_image( //The main `load_image` function detects the format by examining magic bytes rather than the file extension—specifically PNG (`\x89PNG`), PPM (P3/P6), TGA (type at byte 2), and BMP (`BM`). It routes the task to the appropriate loader and releases the memory map; if the format is unrecognized, it returns null
        const knst_c16string& path,
        int* out_width,
        int* out_height,
        int flags = KNST_BITMAP_OUTPUT_RGBA
    ) {
        knst_byte_string result;
        knst_byte_string pathBytes(path);
        if (pathBytes.empty()) return result;

        uint8_t* file_data = nullptr;
        size_t file_size = 0;

        if (!read_file(pathBytes, &file_data, &file_size)) return result;
        if (file_size < 8) {
            free_file_data(file_data, file_size);
            return result;
        }

        
        if (file_data[0] == 0x89 && file_data[1] == 0x50 &&
            file_data[2] == 0x4E && file_data[3] == 0x47 &&
            file_data[4] == 0x0D && file_data[5] == 0x0A &&
            file_data[6] == 0x1A && file_data[7] == 0x0A) {
            result = load_png(file_data, file_size, out_width, out_height, flags);
            free_file_data(file_data, file_size);
            return result;
        }

        if (file_data[0] == 'P' && (file_data[1] == '3' || file_data[1] == '6')) {
            result = load_ppm(file_data, file_size, out_width, out_height, flags);
            free_file_data(file_data, file_size);
            return result;
        }

        if (file_size > 2) {
            uint8_t imageType = file_data[2];
            if (imageType == 2 || imageType == 3 || imageType == 10) {
                result = load_tga(file_data, file_size, out_width, out_height, flags);
                free_file_data(file_data, file_size);
                return result;
            }
        }

        if (file_data[0] == 'B' && file_data[1] == 'M') {
            result = load_bmp_from_memory(file_data, file_size, out_width, out_height, flags);
            free_file_data(file_data, file_size);
            return result;
        }

        free_file_data(file_data, file_size);
        return result;
    }

    static knst_byte_string load_bmp( // Loads a BMP from a file. It converts the UTF-16 path to bytes, reads the file using `mmap`, passes the data to `load_bmp_from_memory`, and releases the memory map. A shortcut function called with the prior knowledge that the file is a BMP
        const knst_c16string& path,
        int* out_width,
        int* out_height,
        int flags = KNST_BITMAP_OUTPUT_RGBA
    ) {
        knst_byte_string pathBytes(path);
        knst_byte_string result;
        if (pathBytes.empty()) return result;
        uint8_t* file_data = nullptr;
        size_t file_size = 0;
        if (!read_file(pathBytes, &file_data, &file_size)) return result;
        result = load_bmp_from_memory(file_data, file_size, out_width, out_height, flags);
        free_file_data(file_data, file_size);
        return result;
    }

    static knst_byte_string load_png( // Loads a PNG from a file. Converts the UTF-16 path to bytes, reads the file using mmap, passes it to load_png, and releases the mapping. It is the PNG version of load_bmp
        const knst_c16string& path,
        int* out_width,
        int* out_height,
        int flags = KNST_BITMAP_OUTPUT_RGBA
    ) {
        knst_byte_string pathBytes(path);
        knst_byte_string result;
        if (pathBytes.empty()) return result;
        uint8_t* file_data = nullptr;
        size_t file_size = 0;
        if (!read_file(pathBytes, &file_data, &file_size)) return result;
        result = load_png(file_data, file_size, out_width, out_height, flags);
        free_file_data(file_data, file_size);
        return result;
    }

    static knst_byte_string load_ppm( // Loads a PPM from a file. Same pattern: convert path to bytes, mmap, load_ppm, unmap
        const knst_c16string& path,
        int* out_width,
        int* out_height,
        int flags = KNST_BITMAP_OUTPUT_RGBA
    ) {
        knst_byte_string pathBytes(path);
        knst_byte_string result;
        if (pathBytes.empty()) return result;
        uint8_t* file_data = nullptr;
        size_t file_size = 0;
        if (!read_file(pathBytes, &file_data, &file_size)) return result;
        result = load_ppm(file_data, file_size, out_width, out_height, flags);
        free_file_data(file_data, file_size);
        return result;
    }

    static knst_byte_string load_tga( // Loads a TGA from a file. Same pattern — path ==> byte, mmap, load_tga, map
        const knst_c16string& path,
        int* out_width,
        int* out_height,
        int flags = KNST_BITMAP_OUTPUT_RGBA
    ) {
        knst_byte_string pathBytes(path);
        knst_byte_string result;
        if (pathBytes.empty()) return result;
        uint8_t* file_data = nullptr;
        size_t file_size = 0;
        if (!read_file(pathBytes, &file_data, &file_size)) return result;
        result = load_tga(file_data, file_size, out_width, out_height, flags);
        free_file_data(file_data, file_size);
        return result;
    }

private:
  

    static bool read_file(const knst_byte_string& path, uint8_t** out_data, size_t* out_size) { // Opens the file as memory-mapped. Uses `CreateFileA` + `CreateFileMappingA` + `MapViewOfFile` on Windows, and `open` + `mmap` on Linux. It does not copy the content to RAM; instead, it maps it to virtual memory—a zero-copy operation. It provides platform-specific preloading hints (`PrefetchVirtualMemory`, `madvise`, `posix_fadvise`). Only regular files (`S_ISREG`) are accepted; directories and sockets are rejected

        if (path.empty() || out_data == nullptr || out_size == nullptr) return false;

        #if KNST_USING_PLATFORM_WINDOWS
            
                HANDLE hFile = CreateFileA((const char*)path.data(), GENERIC_READ, FILE_SHARE_READ,NULL, OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
                                        
                if (hFile == INVALID_HANDLE_VALUE) return false;

                LARGE_INTEGER fileSizeLI;
                if (!GetFileSizeEx(hFile, &fileSizeLI) || fileSizeLI.QuadPart <= 0) {
                    CloseHandle(hFile);
                    return false;
                }
                size_t fileSize = (size_t)fileSizeLI.QuadPart;

                HANDLE hMapping = CreateFileMappingA(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
                if (hMapping == NULL) {
                    CloseHandle(hFile);
                    return false;
                }

                *out_data = (uint8_t*)MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, fileSize);
                *out_size = fileSize;

                CloseHandle(hMapping);
                CloseHandle(hFile);
                if (*out_data == nullptr) return false;

        #if _WIN32_WINNT >= 0x0602 
                {
                    WIN32_MEMORY_RANGE_ENTRY range;
                    range.VirtualAddress = *out_data;
                    range.NumberOfBytes = fileSize;
                    
                    PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
                }
        #endif
                return true;

        #elif KNST_USING_PLATFORM_LINUX
                int fd = open((const char*)path.data(), O_RDONLY);
                if (fd < 0) return false;

                struct stat st;
                if (fstat(fd, &st) != 0 || st.st_size <= 0) {
                    close(fd);
                    return false;
                }
                if (!S_ISREG(st.st_mode)) {
                    close(fd);
                    return false;
                }

        #if defined(KNST_USING_LINUX_PLATFORM_ANDROID)
                
                int fd_local = fd;
                *out_data = (uint8_t*)mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd_local, 0);
                if (*out_data == MAP_FAILED) { close(fd); return false; }
            #ifdef MADV_SEQUENTIAL
                madvise(*out_data, (size_t)st.st_size, MADV_SEQUENTIAL);
            #endif
            #ifdef MADV_WILLNEED
                madvise(*out_data, (size_t)st.st_size, MADV_WILLNEED);
            #endif

        #elif defined(KNST_USING_LINUX_PLATFORM_X11) || defined(KNST_USING_LINUX_PLATFORM_WAYLAND)
            
            #ifdef POSIX_FADV_SEQUENTIAL
                posix_fadvise(fd, 0, st.st_size, POSIX_FADV_SEQUENTIAL);
            #endif
            #ifdef POSIX_FADV_WILLNEED
                posix_fadvise(fd, 0, st.st_size, POSIX_FADV_WILLNEED);
            #endif
                int mapFlags = MAP_PRIVATE;
            #ifdef MAP_POPULATE
                mapFlags |= MAP_POPULATE;
            #endif
                *out_data = (uint8_t*)mmap(NULL, (size_t)st.st_size, PROT_READ, mapFlags, fd, 0);
                if (*out_data == MAP_FAILED) {
                    *out_data = (uint8_t*)mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
                }
                if (*out_data == MAP_FAILED) { close(fd); return false; }

        #else
            
                int mapFlags = MAP_PRIVATE;
            #ifdef MAP_POPULATE
                mapFlags |= MAP_POPULATE;
            #endif
                *out_data = (uint8_t*)mmap(NULL, (size_t)st.st_size, PROT_READ, mapFlags, fd, 0);
                if (*out_data == MAP_FAILED) {
                    *out_data = (uint8_t*)mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
                }
                if (*out_data == MAP_FAILED) { close(fd); return false; }
            #ifdef MADV_SEQUENTIAL
                madvise(*out_data, (size_t)st.st_size, MADV_SEQUENTIAL);
            #endif
        #endif

                *out_size = (size_t)st.st_size;
                close(fd);
                return true;
        #endif
        return false;
    }

    static void free_file_data(uint8_t* data, size_t size) { // Releases the memory-mapped file. Uses `UnmapViewOfFile` on Windows and `munmap` on Linux. It is the counterpart to `read_file`—you close with this what you opened with that
        if (!data || size == 0) return;
        #if KNST_USING_PLATFORM_WINDOWS
                UnmapViewOfFile(data);
        #elif KNST_USING_PLATFORM_LINUX
                munmap(data, size);
        #endif
    }

    static uint8_t* resize_image(const uint8_t* src, int src_w, int src_h, int channels, int dst_w, int dst_h) { // Resizes the image using the nearest-neighbor method. It allocates a new buffer and pre-calculates the X-mapping using `srcXTable` (avoiding multiplication or division within the loop). For each target pixel, it copies the nearest source pixel. It is fast but low-quality—resulting in a pixelated appearance when enlarged. Memory is allocated via `new uint8_t[]`, so the caller is responsible for calling `delete[]`
        if (src == nullptr || src_w <= 0 || src_h <= 0 ||
            channels <= 0 || dst_w <= 0 || dst_h <= 0) return nullptr;

        size_t dst_size = (size_t)dst_w * (size_t)dst_h * (size_t)channels;
        uint8_t* dst = new uint8_t[dst_size];
        if (!dst) return nullptr;

        
        knst_vector<int> srcXTable;
        srcXTable.resize(dst_w);
        for (int x = 0; x < dst_w; x++) {
            int sx = (x * src_w) / dst_w;
            if (sx >= src_w) sx = src_w - 1;
            srcXTable[x] = sx;
        }

        for (int y = 0; y < dst_h; y++) {
            int src_y = (y * src_h) / dst_h;
            if (src_y >= src_h) src_y = src_h - 1;
            const uint8_t* srcRow = src + (size_t)src_y * src_w * channels;
            uint8_t* dstRow = dst + (size_t)y * dst_w * channels;

            for (int x = 0; x < dst_w; x++) {
                const uint8_t* s = srcRow + (size_t)srcXTable[x] * channels;
                uint8_t* d = dstRow + (size_t)x * channels;
                for (int c = 0; c < channels; c++) d[c] = s[c];
            }
        }
        return dst;
    }

    static int GetPNGChannels(uint8_t colorType) { // Returns the number of channels based on the PNG color type. 0 ==> gray (1), 2 ==> RGB (3), 3 ==> palette (1), 4 ==> gray+alpha (2), 6 ==> RGBA (4). Unknown ==> 0

        switch (colorType) {
            case 0: return 1;
            case 2: return 3;
            case 3: return 1;
            case 4: return 2;
            case 6: return 4;
            default: return 0;
        }
    }

    static inline uint8_t PaethPredictor(uint8_t a, uint8_t b, uint8_t c) { // The Paeth filter predictor for PNG. a = left, b = top, c = top-left pixel. It returns the one among the three that is closest to p = a + b - c. It is the most effective filter for PNG decoding—providing good predictions in edge and gradient regions
        int p = a + b - c;
        int pa = abs(p - a);
        int pb = abs(p - b);
        int pc = abs(p - c);
        if (pa <= pb && pa <= pc) return a;
        if (pb <= pc) return b;
        return c;
    }


        // Kütüphanenin kendi thread pool'unu kullanıyoruz — her görüntü yüklemesinde
    // ham OS thread'i yaratıp yok etmek yerine paylaşımlı, statik bir havuzdan yararlanıyoruz.
    static knst_thread_pool& RowThreadPool() {
        static knst_thread_pool s_pool(0 /* auto hardware_concurrency */);
        static bool s_started = s_pool.start();
        (void)s_started;
        return s_pool;
    }

    static void ParallelForRows(uint32_t height, size_t workPerRow,const std::function<void(uint32_t, uint32_t)>& fn) {
                                 
        unsigned hwThreads = std::thread::hardware_concurrency();
        if (hwThreads == 0) hwThreads = 2;

       
        const size_t PARALLEL_WORK_THRESHOLD = 256 * 1024;
        size_t totalWork = (size_t)height * workPerRow;

        unsigned numThreads = 1;
        if (totalWork > PARALLEL_WORK_THRESHOLD && height > 1) {
            numThreads = hwThreads;
            if (numThreads > height) numThreads = height;
            if (numThreads > 16) numThreads = 16;
        }

        if (numThreads <= 1) {
            fn(0, height);
            return;
        }

        // std::thread yerine knst_thread kullanıyoruz — kütüphanenin kendi thread sarmalayıcısı.
        knst_vector<knst_thread> pool;
        uint32_t rowsPerThread = (height + numThreads - 1) / numThreads;
        for (unsigned t = 0; t < numThreads; t++) {
            uint32_t startRow = t * rowsPerThread;
            uint32_t endRow = startRow + rowsPerThread;
            if (startRow >= height) break;
            if (endRow > height) endRow = height;

            knst_thread th;
            th.start([&fn, startRow, endRow]() {
                fn(startRow, endRow);
            });
            pool.push_back(std::move(th));
        }
        for (auto& th : pool) th.join();
    }

    


// Converts RGBA to BGRA (swapping the R and B channels). There are three implementations: SSE2 (x86, 4 pixels/iteration), NEON (ARM, 8 pixels/iteration), and scalar (fallback). The SIMD versions are 4–8x faster. `SwizzleRGBA_BGRA` selects the appropriate one at compile time using `#if`
#if defined(KNST_HAS_SSE2)
    static inline void SwizzleRGBA_BGRA_SSE2(const uint8_t* src, uint8_t* dst, size_t pixelCount) {
        size_t i = 0;
        const __m128i maskRB = _mm_set1_epi32(0x00FF00FF);
        for (; i + 4 <= pixelCount; i += 4) {
            __m128i px = _mm_loadu_si128((const __m128i*)(src + i * 4));
            __m128i rb = _mm_and_si128(px, maskRB);
            __m128i ga = _mm_andnot_si128(maskRB, px);
            __m128i rbSwapped = _mm_or_si128(
                _mm_slli_epi32(rb, 16),
                _mm_srli_epi32(rb, 16));
            __m128i result = _mm_or_si128(rbSwapped, ga);
            _mm_storeu_si128((__m128i*)(dst + i * 4), result);
        }
        for (; i < pixelCount; i++) {
            dst[i*4+0] = src[i*4+2]; dst[i*4+1] = src[i*4+1];
            dst[i*4+2] = src[i*4+0]; dst[i*4+3] = src[i*4+3];
        }
    }
#endif
#if defined(KNST_HAS_NEON)
    static inline void SwizzleRGBA_BGRA_NEON(const uint8_t* src, uint8_t* dst, size_t pixelCount) {
        size_t i = 0;
        for (; i + 8 <= pixelCount; i += 8) {
            uint8x8x4_t px = vld4_u8(src + i * 4);
            uint8x8x4_t out;
            out.val[0] = px.val[2];
            out.val[1] = px.val[1];
            out.val[2] = px.val[0];
            out.val[3] = px.val[3];
            vst4_u8(dst + i * 4, out);
        }
        for (; i < pixelCount; i++) {
            dst[i*4+0] = src[i*4+2]; dst[i*4+1] = src[i*4+1];
            dst[i*4+2] = src[i*4+0]; dst[i*4+3] = src[i*4+3];
        }
    }
#endif
    static inline void SwizzleRGBA_BGRA(const uint8_t* src, uint8_t* dst, size_t pixelCount) {
#if defined(KNST_HAS_SSE2)
        SwizzleRGBA_BGRA_SSE2(src, dst, pixelCount);
#elif defined(KNST_HAS_NEON)
        SwizzleRGBA_BGRA_NEON(src, dst, pixelCount);
#else
        for (size_t i = 0; i < pixelCount; i++) {
            dst[i*4+0] = src[i*4+2]; dst[i*4+1] = src[i*4+1];
            dst[i*4+2] = src[i*4+0]; dst[i*4+3] = src[i*4+3];
        }
#endif
    }

    static void UnpackRow(const uint8_t* packed, knst_byte_string& output, size_t outOffsetSamples,uint32_t width, int samplesPerPixel, int bitDepth) { // Unpacks bit-packed data from a PNG row into bytes. If bitDepth is 8 or 16, it uses a direct memcpy. For 1, 2, or 4-bit data, it reads bit by bit and writes them into separate bytes. This is required for paletted and low-bit-depth PNGs
        size_t totalSamples = (size_t)width * samplesPerPixel;

        if (bitDepth == 8) {
            std::memcpy(&output[outOffsetSamples], packed, totalSamples);
        } else if (bitDepth == 16) {
            std::memcpy(&output[outOffsetSamples * 2], packed, totalSamples * 2);
        } else {
            uint8_t mask = (uint8_t)((1 << bitDepth) - 1);
            size_t bitPos = 0;
            for (size_t i = 0; i < totalSamples; i++) {
                size_t byteIndex = bitPos / 8;
                int bitOffset = 8 - bitDepth - (int)(bitPos % 8);
                uint8_t v = (packed[byteIndex] >> bitOffset) & mask;
                output[outOffsetSamples + i] = v;
                bitPos += (size_t)bitDepth;
            }
        }
    }


    static knst_byte_string load_png( // The core engine of the PNG decoder. It iterates through chunks (IHDR/PLTE/tRNS/IDAT/IEND), collects and inflates compressed data, reverses filters (including Adam7 de-interlacing), and converts from palette/grayscale/RGB/RGBA to the target format. It utilizes SIMD swizzling and multi-threaded row processing. It handles conversions such as 16-bit to 8-bit and low bit-depth to 8-bit. The result is a byte array in the desired format (RGB/RGBA/BGR/BGRA)
        const uint8_t* data,
        size_t size,
        int* out_width,
        int* out_height,
        int flags
    ) {
        knst_byte_string result;
        if (data == nullptr || size < 8 || out_width == nullptr || out_height == nullptr) return result;

        const uint8_t pngSig[8] = {137,80,78,71,13,10,26,10};
        if (memcmp(data, pngSig, 8) != 0) return result;

        size_t pos = 8;
        uint32_t width = 0, height = 0;
        uint8_t bitDepth = 0, colorType = 0, interlace = 0;
        bool haveIHDR = false;
        knst_byte_string compressedData;
        knst_vector<uint8_t> palette;
        knst_vector<uint8_t> transAlpha;

        const size_t MAX_CHUNK_SIZE = 10 * 1024 * 1024;
        const uint32_t MAX_IMAGE_SIZE = KNST_MAX_IMAGE_DIMENSION;

        compressedData.reserve(size);

        while (pos + 12 <= size) {
            uint32_t chunkLen = (data[pos] << 24) | (data[pos+1] << 16) |
                               (data[pos+2] << 8) | data[pos+3];
            pos += 4;

            if (chunkLen > MAX_CHUNK_SIZE) return result;

            char chunkType[5] = {0};
            if (pos + 4 > size) break;
            memcpy(chunkType, data + pos, 4);
            pos += 4;

            if (pos + chunkLen + 4 > size) break;

            const uint8_t* chunkPtr = data + pos;

            if (strncmp(chunkType, "IHDR", 4) == 0 && chunkLen >= 13) {
                width = (chunkPtr[0] << 24) | (chunkPtr[1] << 16) |
                        (chunkPtr[2] << 8) | chunkPtr[3];
                height = (chunkPtr[4] << 24) | (chunkPtr[5] << 16) |
                         (chunkPtr[6] << 8) | chunkPtr[7];
                bitDepth = chunkPtr[8];
                colorType = chunkPtr[9];
                interlace = chunkPtr[12];

                if (width == 0 || height == 0 ||
                    width > MAX_IMAGE_SIZE || height > MAX_IMAGE_SIZE) {
                    return result;
                }

                bool validDepth = false;
                switch (colorType) {
                    case 0: validDepth = (bitDepth==1||bitDepth==2||bitDepth==4||bitDepth==8||bitDepth==16); break;
                    case 2: validDepth = (bitDepth==8||bitDepth==16); break;
                    case 3: validDepth = (bitDepth==1||bitDepth==2||bitDepth==4||bitDepth==8); break;
                    case 4: validDepth = (bitDepth==8||bitDepth==16); break;
                    case 6: validDepth = (bitDepth==8||bitDepth==16); break;
                    default: validDepth = false; break;
                }
                if (!validDepth) return result;
                haveIHDR = true;
            } else if (strncmp(chunkType, "PLTE", 4) == 0) {
                palette.resize(chunkLen);
                memcpy(palette.data(), chunkPtr, chunkLen);
            } else if (strncmp(chunkType, "tRNS", 4) == 0) {
                transAlpha.resize(chunkLen);
                memcpy(transAlpha.data(), chunkPtr, chunkLen);
            } else if (strncmp(chunkType, "IDAT", 4) == 0) {
                compressedData.append(chunkPtr, chunkLen);
            } else if (strncmp(chunkType, "IEND", 4) == 0) {
                pos += chunkLen + 4;
                break;
            }

            pos += chunkLen;
            pos += 4;
        }

        if (!haveIHDR || width == 0 || height == 0 || compressedData.empty()) return result;
        if (colorType == 3 && palette.empty()) return result;

        *out_width = width;
        *out_height = height;

        int samplesPerPixel = GetPNGChannels(colorType);
        if (samplesPerPixel == 0) return result;

        knst_byte_string decompressed;
        
        {
            size_t sampleStorageBytes = (bitDepth == 16) ? 2 : 1;
            size_t packedStride = ((size_t)width * samplesPerPixel * bitDepth + 7) / 8;
            size_t expectedInflated = (packedStride + 1) * height;
            (void)sampleStorageBytes;
            decompressed.reserve(expectedInflated + 64);
        }
        if (!KnstInflate::Inflate(compressedData.data(), compressedData.length(), decompressed)) {
            return result;
        }

        int sampleStorageBytes = (bitDepth == 16) ? 2 : 1;

        knst_byte_string rawSamples;
        rawSamples.resize((size_t)width * height * samplesPerPixel * sampleStorageBytes);

        bool ok;
        if (interlace == 1) {
            ok = ApplyAdam7(decompressed, rawSamples, width, height, samplesPerPixel, bitDepth);
        } else {
            ok = ApplyPNGFilters(decompressed, rawSamples, width, height, samplesPerPixel, bitDepth);
        }
        if (!ok) return result;

        bool isPaletteType = (colorType == 3);
        size_t totalSamples = (size_t)width * height * samplesPerPixel;

        knst_byte_string samples8;
        samples8.resize(totalSamples);

        if (bitDepth == 16) {
            
            for (size_t i = 0; i < totalSamples; i++) {
                samples8[i] = rawSamples[i * 2];
            }
        } else if (bitDepth == 8) {
            std::memcpy(&samples8[0], rawSamples.data(), totalSamples);
        } else {
            uint8_t maxVal = (uint8_t)((1 << bitDepth) - 1);
            if (isPaletteType) {
                std::memcpy(&samples8[0], rawSamples.data(), totalSamples);
            } else {
                
                uint8_t lut[16];
                for (int v = 0; v <= maxVal; v++) lut[v] = (uint8_t)((v * 255) / maxVal);
                for (size_t i = 0; i < totalSamples; i++) {
                    samples8[i] = lut[rawSamples[i]];
                }
            }
        }

        int channels;
        knst_byte_string rawData;

        if (isPaletteType) {
            channels = 3;
            rawData.resize((size_t)width * height * 3);
            size_t paletteEntries = palette.size() / 3;
            const uint8_t* pal = palette.data();
            const uint8_t* idxBase = samples8.data();
            uint8_t* rdBase = &rawData[0];
            uint32_t w = width;
           
            ParallelForRows(height, (size_t)w * 3, [pal, paletteEntries, idxBase, rdBase, w](uint32_t r0, uint32_t r1) {
                for (uint32_t y = r0; y < r1; y++) {
                    size_t rowStart = (size_t)y * w;
                    for (uint32_t x = 0; x < w; x++) {
                        size_t p = rowStart + x;
                        uint8_t idx = idxBase[p];
                        size_t di = p * 3;
                        if (idx < paletteEntries) {
                            size_t si = (size_t)idx * 3;
                            rdBase[di] = pal[si]; rdBase[di+1] = pal[si+1]; rdBase[di+2] = pal[si+2];
                        } else {
                            rdBase[di] = rdBase[di+1] = rdBase[di+2] = 0;
                        }
                    }
                }
            });
        } else if (colorType == 0) {
            channels = 3;
            rawData.resize((size_t)width * height * 3);
            for (size_t p = 0; p < (size_t)width * height; p++) {
                uint8_t g = samples8[p];
                size_t di = p * 3;
                rawData[di] = g; rawData[di + 1] = g; rawData[di + 2] = g;
            }
        } else if (colorType == 4) {
            channels = 4;
            rawData.resize((size_t)width * height * 4);
            for (size_t p = 0; p < (size_t)width * height; p++) {
                uint8_t g = samples8[p * 2];
                uint8_t a = samples8[p * 2 + 1];
                size_t di = p * 4;
                rawData[di] = g; rawData[di + 1] = g; rawData[di + 2] = g; rawData[di + 3] = a;
            }
        } else if (colorType == 2) {
            channels = 3;
            rawData.resize((size_t)width * height * 3);
            std::memcpy(&rawData[0], samples8.data(), (size_t)width * height * 3);
        } else if (colorType == 6) {
            channels = 4;
            rawData.resize((size_t)width * height * 4);
            std::memcpy(&rawData[0], samples8.data(), (size_t)width * height * 4);
        } else {
            return result;
        }

        
        if (!transAlpha.empty()) {
            if (isPaletteType && channels == 3) {
                knst_byte_string withAlpha;
                withAlpha.resize((size_t)width * height * 4);
                for (size_t p = 0; p < (size_t)width * height; p++) {
                    uint8_t idx = samples8[p];
                    size_t si = p * 3, di = p * 4;
                    withAlpha[di] = rawData[si]; withAlpha[di+1] = rawData[si+1]; withAlpha[di+2] = rawData[si+2];
                    withAlpha[di+3] = ((size_t)idx < transAlpha.size()) ? transAlpha[idx] : 255;
                }
                rawData = std::move(withAlpha);
                channels = 4;
            } else if (colorType == 0 && channels == 3 && transAlpha.size() >= 2) {
                uint16_t rawKey = ((uint16_t)transAlpha[0] << 8) | transAlpha[1];
                uint8_t key8;
                if (bitDepth == 16) {
                    key8 = (uint8_t)(rawKey >> 8);
                } else {
                    uint8_t maxVal = (uint8_t)((1 << bitDepth) - 1);
                    key8 = (uint8_t)(((int)(rawKey & maxVal) * 255) / (maxVal ? maxVal : 1));
                }
                knst_byte_string withAlpha;
                withAlpha.resize((size_t)width * height * 4);
                for (size_t p = 0; p < (size_t)width * height; p++) {
                    size_t si = p * 3, di = p * 4;
                    withAlpha[di] = rawData[si]; withAlpha[di+1] = rawData[si+1]; withAlpha[di+2] = rawData[si+2];
                    withAlpha[di+3] = (rawData[si] == key8 && rawData[si+1] == key8 && rawData[si+2] == key8) ? 0 : 255;
                }
                rawData = std::move(withAlpha);
                channels = 4;
            } else if (colorType == 2 && channels == 3 && transAlpha.size() >= 6) {
                uint8_t rKey, gKey, bKey;
                if (bitDepth == 16) {
                    rKey = transAlpha[0]; gKey = transAlpha[2]; bKey = transAlpha[4];
                } else {
                    rKey = transAlpha[1]; gKey = transAlpha[3]; bKey = transAlpha[5];
                }
                knst_byte_string withAlpha;
                withAlpha.resize((size_t)width * height * 4);
                for (size_t p = 0; p < (size_t)width * height; p++) {
                    size_t si = p * 3, di = p * 4;
                    withAlpha[di] = rawData[si]; withAlpha[di+1] = rawData[si+1]; withAlpha[di+2] = rawData[si+2];
                    withAlpha[di+3] = (rawData[si] == rKey && rawData[si+1] == gKey && rawData[si+2] == bKey) ? 0 : 255;
                }
                rawData = std::move(withAlpha);
                channels = 4;
            }
        }

        
        int outputFormat = flags & 0xFF00;
        int outChannels = (outputFormat == KNST_BITMAP_OUTPUT_RGB ||
                          outputFormat == KNST_BITMAP_OUTPUT_BGR) ? 3 : 4;

        bool isBGR = (outputFormat == KNST_BITMAP_OUTPUT_BGR ||
                     outputFormat == KNST_BITMAP_OUTPUT_BGRA);

        knst_byte_string output;
        output.resize((size_t)width * height * outChannels);

        int srcChannels = channels;
        size_t pixelCount = (size_t)width * height;

       
        if (srcChannels == outChannels && !isBGR) {
            std::memcpy(&output[0], rawData.data(), pixelCount * outChannels);
        } else if (srcChannels == 4 && outChannels == 4) {
            const uint8_t* srcBase = rawData.data();
            uint8_t* dstBase = &output[0];
            uint32_t w = width;
            ParallelForRows(height, (size_t)w * 4, [srcBase, dstBase, w](uint32_t r0, uint32_t r1) {
                size_t off = (size_t)r0 * w * 4;
                SwizzleRGBA_BGRA(srcBase + off, dstBase + off, (size_t)(r1 - r0) * w);
            });
        } else if (srcChannels == 3 && outChannels == 3) {
            const uint8_t* s = rawData.data();
            uint8_t* d = &output[0];
            for (size_t p = 0; p < pixelCount; p++) {
                uint8_t r = s[0], g = s[1], b = s[2];
                if (isBGR) { d[0]=b; d[1]=g; d[2]=r; }
                else       { d[0]=r; d[1]=g; d[2]=b; }
                s += 3; d += 3;
            }
        } else if (srcChannels == 3 && outChannels == 4) {
            const uint8_t* s = rawData.data();
            uint8_t* d = &output[0];
            for (size_t p = 0; p < pixelCount; p++) {
                uint8_t r = s[0], g = s[1], b = s[2];
                if (isBGR) { d[0]=b; d[1]=g; d[2]=r; } else { d[0]=r; d[1]=g; d[2]=b; }
                d[3] = 255;
                s += 3; d += 4;
            }
        } else if (srcChannels == 4 && outChannels == 3) {
            const uint8_t* s = rawData.data();
            uint8_t* d = &output[0];
            for (size_t p = 0; p < pixelCount; p++) {
                uint8_t r = s[0], g = s[1], b = s[2];
                if (isBGR) { d[0]=b; d[1]=g; d[2]=r; } else { d[0]=r; d[1]=g; d[2]=b; }
                s += 4; d += 3;
            }
        } else {
            
            for (size_t p = 0; p < pixelCount; p++) {
                size_t si = p * srcChannels;
                size_t di = p * outChannels;
                uint8_t r = (srcChannels >= 1) ? rawData[si] : 0;
                uint8_t g = (srcChannels >= 2) ? rawData[si + 1] : 0;
                uint8_t b = (srcChannels >= 3) ? rawData[si + 2] : 0;
                uint8_t a = (srcChannels >= 4) ? rawData[si + 3] : 255;
                if (isBGR) { output[di]=b; output[di+1]=g; output[di+2]=r; }
                else       { output[di]=r; output[di+1]=g; output[di+2]=b; }
                if (outChannels == 4) output[di + 3] = a;
            }
        }

        return output;
    }

  
    static bool ApplyPNGFilters( // It resolves PNG row filters. It reads the filter type at the beginning of each row (0=None, 1=Sub, 2=Up, 3=Average, 4=Paeth), applies it, and then unpacks the bit-packed data using UnpackRow. `prevRow` holds the previous row (required for Up/Average/Paeth); it advances within the loop using a swap, avoiding the need for copying
        const knst_byte_string& input,
        knst_byte_string& output,
        uint32_t width, uint32_t height,
        int samplesPerPixel, int bitDepth
    ) {
        if (input.empty() || output.empty() || width == 0 || height == 0) return false;

        int filterBpp = ((size_t)samplesPerPixel * bitDepth + 7) / 8;
        if (filterBpp < 1) filterBpp = 1;

        size_t packedStride = ((size_t)width * samplesPerPixel * bitDepth + 7) / 8;
        size_t rowSize = packedStride + 1;

        if (input.length() < (size_t)height * rowSize) return false;

        knst_vector<uint8_t> prevRow, curRow;
        prevRow.resize(packedStride);
        curRow.resize(packedStride);

        for (uint32_t y = 0; y < height; y++) {
            const uint8_t* rowData = input.data() + (size_t)y * rowSize;
            uint8_t filterType = rowData[0];
            const uint8_t* scanline = rowData + 1;

           
            switch (filterType) {
                case 0: // None
                    std::memcpy(curRow.data(), scanline, packedStride);
                    break;
                case 1: { // Sub
                    for (size_t x = 0; x < (size_t)filterBpp && x < packedStride; x++) curRow[x] = scanline[x];
                    for (size_t x = filterBpp; x < packedStride; x++) curRow[x] = (uint8_t)(scanline[x] + curRow[x - filterBpp]);
                    break;
                }
                case 2: { // Up
                    for (size_t x = 0; x < packedStride; x++) curRow[x] = (uint8_t)(scanline[x] + prevRow[x]);
                    break;
                }
                case 3: { // Average
                    for (size_t x = 0; x < packedStride; x++) {
                        uint8_t left = (x >= (size_t)filterBpp) ? curRow[x - filterBpp] : 0;
                        curRow[x] = (uint8_t)(scanline[x] + ((left + prevRow[x]) / 2));
                    }
                    break;
                }
                case 4: { // Paeth
                    for (size_t x = 0; x < packedStride; x++) {
                        uint8_t left = (x >= (size_t)filterBpp) ? curRow[x - filterBpp] : 0;
                        uint8_t upLeft = (x >= (size_t)filterBpp) ? prevRow[x - filterBpp] : 0;
                        curRow[x] = (uint8_t)(scanline[x] + PaethPredictor(left, prevRow[x], upLeft));
                    }
                    break;
                }
                default:
                    return false;
            }

            UnpackRow(curRow.data(), output, (size_t)y * width * samplesPerPixel, width, samplesPerPixel, bitDepth);

           
            std::swap(prevRow, curRow);
        }

        return true;
    }

    static bool ApplyPNGFiltersToPass( // The pointer version of ApplyPNGFilters — for Adam7 interlacing. It takes a `const uint8_t*` instead of a `knst_byte_string` because the passes appear as consecutive blocks in the file. The filter logic is the same; only the input is a pointer
        const uint8_t* input,
        knst_byte_string& output,
        int width, int height,
        int samplesPerPixel, int bitDepth
    ) {
        if (input == nullptr || output.empty() || width <= 0 || height <= 0) return false;

        int filterBpp = ((size_t)samplesPerPixel * bitDepth + 7) / 8;
        if (filterBpp < 1) filterBpp = 1;

        size_t packedStride = ((size_t)width * samplesPerPixel * bitDepth + 7) / 8;
        size_t rowSize = packedStride + 1;

        knst_vector<uint8_t> prevRow, curRow;
        prevRow.resize(packedStride);
        curRow.resize(packedStride);

        for (int y = 0; y < height; y++) {
            const uint8_t* rowData = input + (size_t)y * rowSize;
            uint8_t filterType = rowData[0];
            const uint8_t* scanline = rowData + 1;

            switch (filterType) {
                case 0:
                    std::memcpy(curRow.data(), scanline, packedStride);
                    break;
                case 1: {
                    for (size_t x = 0; x < (size_t)filterBpp && x < packedStride; x++) curRow[x] = scanline[x];
                    for (size_t x = filterBpp; x < packedStride; x++) curRow[x] = (uint8_t)(scanline[x] + curRow[x - filterBpp]);
                    break;
                }
                case 2: {
                    for (size_t x = 0; x < packedStride; x++) curRow[x] = (uint8_t)(scanline[x] + prevRow[x]);
                    break;
                }
                case 3: {
                    for (size_t x = 0; x < packedStride; x++) {
                        uint8_t left = (x >= (size_t)filterBpp) ? curRow[x - filterBpp] : 0;
                        curRow[x] = (uint8_t)(scanline[x] + ((left + prevRow[x]) / 2));
                    }
                    break;
                }
                case 4: {
                    for (size_t x = 0; x < packedStride; x++) {
                        uint8_t left = (x >= (size_t)filterBpp) ? curRow[x - filterBpp] : 0;
                        uint8_t upLeft = (x >= (size_t)filterBpp) ? prevRow[x - filterBpp] : 0;
                        curRow[x] = (uint8_t)(scanline[x] + PaethPredictor(left, prevRow[x], upLeft));
                    }
                    break;
                }
                default:
                    return false;
            }

            UnpackRow(curRow.data(), output, (size_t)y * width * samplesPerPixel, (uint32_t)width, samplesPerPixel, bitDepth);

            std::swap(prevRow, curRow);
        }

        return true;
    }

    static bool ApplyAdam7( // It decodes the PNG Adam7 interlacing scheme. It combines pixels stored across seven passes into a single, continuous image. Each pass performs sub-sampling using its own startX, startY, stepX, and stepY parameters—resulting in a sparse, checkerboard-like pixel pattern. Each pass is processed via `ApplyPNGFiltersToPass` and then placed into the correct positions; if `stepX` is 1, a fast bulk copy using `memcpy` is performed
        const knst_byte_string& input,
        knst_byte_string& output,
        uint32_t width, uint32_t height,
        int samplesPerPixel, int bitDepth
    ) {
        if (input.empty() || output.empty() || width == 0 || height == 0) return false;

        struct Adam7Pass { int startX, startY, stepX, stepY; };
        const Adam7Pass passes[7] = {
            {0,0,8,8}, {4,0,8,8}, {0,4,4,8}, {2,0,4,4},
            {0,2,2,4}, {1,0,2,2}, {0,1,1,2}
        };

        int sampleStorageBytes = (bitDepth == 16) ? 2 : 1;
        size_t pos = 0;

        for (int pass = 0; pass < 7; pass++) {
            int passWidth = (width - passes[pass].startX + passes[pass].stepX - 1) / passes[pass].stepX;
            int passHeight = (height - passes[pass].startY + passes[pass].stepY - 1) / passes[pass].stepY;

            if (passWidth <= 0 || passHeight <= 0) continue;

            size_t packedStride = ((size_t)passWidth * samplesPerPixel * bitDepth + 7) / 8;
            size_t rowSize = packedStride + 1;

            if (pos + (size_t)passHeight * rowSize > input.length()) return false;

            knst_byte_string passData;
            passData.resize((size_t)passWidth * passHeight * samplesPerPixel * sampleStorageBytes);

            if (!ApplyPNGFiltersToPass(input.data() + pos, passData, passWidth, passHeight,
                                       samplesPerPixel, bitDepth)) {
                return false;
            }
            pos += (size_t)passHeight * rowSize;

            size_t sampleUnit = (size_t)samplesPerPixel * sampleStorageBytes;

            for (int y = 0; y < passHeight; y++) {
                const uint8_t* srcRow = &passData[(size_t)y * passWidth * sampleUnit];
                int outY = passes[pass].startY + y * passes[pass].stepY;
                if (outY >= (int)height) continue;

                if (passes[pass].stepX == 1) {
                   
                    int outX0 = passes[pass].startX;
                    size_t di = ((size_t)outY * width + outX0) * sampleUnit;
                    size_t copyPixels = passWidth;
                    if ((size_t)outX0 + copyPixels > width) copyPixels = width - outX0;
                    std::memcpy(&output[di], srcRow, copyPixels * sampleUnit);
                } else {
                    for (int x = 0; x < passWidth; x++) {
                        int outX = passes[pass].startX + x * passes[pass].stepX;
                        if (outX >= (int)width) continue;
                        size_t si = (size_t)x * sampleUnit;
                        size_t di = ((size_t)outY * width + outX) * sampleUnit;
                        std::memcpy(&output[di], srcRow + si, sampleUnit);
                    }
                }
            }
        }

        return true;
    }


    static knst_byte_string load_ppm( // Loads PPM (P3 ASCII / P6 binary) files. Parses the header—P3/P6, width, height, and maxVal. Includes overflow protection with a 1,000,000 limit. Rejects files where maxVal > 65535. Converts pixel data to the target format (RGB/RGBA/BGR/BGRA). No compression; raw data
        const uint8_t* data,
        size_t size,
        int* out_width,
        int* out_height,
        int flags
    ) {
        knst_byte_string result;
        if (data == nullptr || size < 10 || out_width == nullptr || out_height == nullptr) return result;

        const char* ptr = (const char*)data;
        const char* end = (const char*)data + size;

        if (*ptr != 'P') return result;
        ptr++;
        if (*ptr != '3' && *ptr != '6') return result;
        ptr++;

        while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) ptr++;

        const int PPM_PARSE_LIMIT = 1000000;

        int width = 0;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            if (width > PPM_PARSE_LIMIT) return result;
            width = width * 10 + (*ptr - '0');
            ptr++;
        }
        while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) ptr++;

        int height = 0;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            if (height > PPM_PARSE_LIMIT) return result;
            height = height * 10 + (*ptr - '0');
            ptr++;
        }
        while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) ptr++;

        int maxVal = 0;
        while (ptr < end && *ptr >= '0' && *ptr <= '9') {
            if (maxVal > PPM_PARSE_LIMIT) return result;
            maxVal = maxVal * 10 + (*ptr - '0');
            ptr++;
        }
        while (ptr < end && (*ptr == ' ' || *ptr == '\t' || *ptr == '\n' || *ptr == '\r')) ptr++;

        if (width <= 0 || height <= 0 || maxVal <= 0 || maxVal > 65535) return result;
        if ((uint32_t)width > KNST_MAX_IMAGE_DIMENSION || (uint32_t)height > KNST_MAX_IMAGE_DIMENSION) return result;

        *out_width = width;
        *out_height = height;

        if (*(ptr - 1) != '\n' && *(ptr - 1) != '\r') return result;

        size_t pixelSize = (size_t)width * (size_t)height * 3;
        if ((size_t)(end - ptr) < pixelSize) return result;

        int outputFormat = flags & 0xFF00;
        int outChannels = (outputFormat == KNST_BITMAP_OUTPUT_RGB ||
                          outputFormat == KNST_BITMAP_OUTPUT_BGR) ? 3 : 4;

        bool isBGR = (outputFormat == KNST_BITMAP_OUTPUT_BGR ||
                     outputFormat == KNST_BITMAP_OUTPUT_BGRA);

        knst_byte_string output;
        output.resize((size_t)width * (size_t)height * outChannels);

        const uint8_t* pixelData = (const uint8_t*)ptr;
        size_t pixelCount = (size_t)width * height;

       
        if (outChannels == 3 && !isBGR) {
            std::memcpy(&output[0], pixelData, pixelCount * 3);
        } else {
            const uint8_t* s = pixelData;
            uint8_t* d = &output[0];
            for (size_t p = 0; p < pixelCount; p++) {
                uint8_t r = s[0], g = s[1], b = s[2];
                if (isBGR) { d[0]=b; d[1]=g; d[2]=r; } else { d[0]=r; d[1]=g; d[2]=b; }
                if (outChannels == 4) d[3] = 255;
                s += 3; d += outChannels;
            }
        }

        return output;
    }

    
    static knst_byte_string load_tga( // Loads TGA (Truevision) files. Reads the header—ID length, color map, image type (2=RGB, 3=grayscale). Checks `imageDescriptor & 0x20` to determine if the orientation is bottom-up (TGA defaults to bottom-up). Determines the number of channels based on bit depth (8/24/32). Skips the color map if present. Converts pixel data row by row, using the fast `memcpy` path (BGR and tight packing)
        const uint8_t* data,
        size_t size,
        int* out_width,
        int* out_height,
        int flags
    ) {
        knst_byte_string result;
        if (data == nullptr || size < 18 || out_width == nullptr || out_height == nullptr) return result;

        uint8_t idLength = data[0];
        uint8_t colorMapType = data[1];
        uint8_t imageType = data[2];

        if (imageType != 2 && imageType != 3) return result;

        uint16_t width = data[12] | (data[13] << 8);
        uint16_t height = data[14] | (data[15] << 8);
        uint8_t bitsPerPixel = data[16];
        uint8_t imageDescriptor = data[17];

        if (width == 0 || height == 0 || bitsPerPixel == 0) return result;
        if ((uint32_t)width > KNST_MAX_IMAGE_DIMENSION || (uint32_t)height > KNST_MAX_IMAGE_DIMENSION) return result;

        *out_width = width;
        *out_height = height;

        size_t pos = (size_t)18 + idLength;
        if (colorMapType == 1) {
            uint16_t colorMapLength = data[5] | (data[6] << 8);
            uint8_t colorMapDepth = data[7];
            pos += (size_t)colorMapLength * (size_t)(colorMapDepth / 8);
        }

        if (pos >= size) return result;

        int srcChannels = bitsPerPixel / 8;
        if (srcChannels != 1 && srcChannels != 3 && srcChannels != 4) return result;

        int outputFormat = flags & 0xFF00;
        int outChannels = (outputFormat == KNST_BITMAP_OUTPUT_RGB ||
                          outputFormat == KNST_BITMAP_OUTPUT_BGR) ? 3 : 4;

        bool isBGR = (outputFormat == KNST_BITMAP_OUTPUT_BGR ||
                     outputFormat == KNST_BITMAP_OUTPUT_BGRA);
        bool bottomUp = (imageDescriptor & 0x20) == 0;

        knst_byte_string output;
        output.resize((size_t)width * (size_t)height * outChannels);

        size_t rowBytes = (size_t)width * srcChannels;

        for (int y = 0; y < height; y++) {
            int srcY = bottomUp ? y : (height - 1 - y);
            size_t rowStart = pos + (size_t)srcY * rowBytes;
            bool rowInBounds = (rowStart + rowBytes <= size);

            uint8_t* dRow = &output[(size_t)y * width * outChannels];

            if (!rowInBounds) {

                continue;
            }

            const uint8_t* sRow = data + rowStart;

            if (srcChannels == 1) {
                for (int x = 0; x < width; x++) {
                    uint8_t gray = sRow[x];
                    uint8_t* d = dRow + (size_t)x * outChannels;
                    d[0] = gray; d[1] = gray; d[2] = gray;
                    if (outChannels == 4) d[3] = 255;
                }
            } else if (srcChannels == outChannels && isBGR) {
               
                std::memcpy(dRow, sRow, rowBytes);
            } else {
                for (int x = 0; x < width; x++) {
                    const uint8_t* s = sRow + (size_t)x * srcChannels;
                    uint8_t* d = dRow + (size_t)x * outChannels;
                    uint8_t b = s[0], g = s[1], r = s[2];
                    uint8_t a = (srcChannels == 4) ? s[3] : 255;
                    if (isBGR) { d[0]=b; d[1]=g; d[2]=r; } else { d[0]=r; d[1]=g; d[2]=b; }
                    if (outChannels == 4) d[3] = a;
                }
            }
        }

        return output;
    }

   
    static knst_byte_string load_bmp_from_memory( // Loads a BMP. Reads width, height, and bpp (24 or 32) from the header. Interprets height as top-down if negative, or bottom-up (BMP default) if positive. Handles 4-byte row alignment (calculating `row_size`). Converts pixel data—BGR to RGB, and sets alpha to 255 for 24-bit to 32-bit conversion. Uses a fast `memcpy` path for the tightly packed BGR case
        const uint8_t* data,
        size_t size,
        int* out_width,
        int* out_height,
        int flags
    ) {
        knst_byte_string result;
        if (data == nullptr || size < sizeof(BMPHeader) ||
            out_width == nullptr || out_height == nullptr) return result;

        BMPHeader* header = (BMPHeader*)data;
        if (header->signature != 0x4D42) return result;

        int width = header->width;
        int height = (header->height == INT32_MIN) ? 0 : abs(header->height);
        int bpp = header->bpp;
        bool bottom_up = header->height > 0;

        if (width <= 0 || height <= 0 || (bpp != 24 && bpp != 32)) return result;
        if ((uint32_t)width > KNST_MAX_IMAGE_DIMENSION || (uint32_t)height > KNST_MAX_IMAGE_DIMENSION) return result;

        *out_width = width;
        *out_height = height;

        int outputFormat = flags & 0xFF00;
        int channels = (outputFormat == KNST_BITMAP_OUTPUT_RGB ||
                       outputFormat == KNST_BITMAP_OUTPUT_BGR) ? 3 : 4;

        bool isBGR = (outputFormat == KNST_BITMAP_OUTPUT_BGR ||
                     outputFormat == KNST_BITMAP_OUTPUT_BGRA);

        size_t row_size = (((size_t)bpp * (size_t)width + 31) / 32) * 4;

        if ((size_t)header->data_offset >= size) return result;
        if ((size_t)header->data_offset + (size_t)height * row_size > size) return result;

        const uint8_t* pixels = data + header->data_offset;
        int srcChannels = bpp / 8;

        knst_byte_string output;
        output.resize((size_t)width * (size_t)height * (size_t)channels);

       
        bool rowIsTightlyPacked = (row_size == (size_t)width * srcChannels);

        for (int y = 0; y < height; y++) {
            int src_y = bottom_up ? (height - 1 - y) : y;
            const uint8_t* src_row = pixels + (size_t)src_y * row_size;
            uint8_t* dst_row = &output[(size_t)y * width * channels];

            if (srcChannels == channels && isBGR && rowIsTightlyPacked) {
                std::memcpy(dst_row, src_row, (size_t)width * channels);
                continue;
            }

            for (int x = 0; x < width; x++) {
                size_t si = (size_t)x * srcChannels;
                if (bpp == 24 && si + 2 >= row_size) continue;
                if (bpp == 32 && si + 3 >= row_size) continue;

                uint8_t b = src_row[si];
                uint8_t g = src_row[si + 1];
                uint8_t r = src_row[si + 2];
                uint8_t a = (bpp == 32) ? src_row[si + 3] : 255;

                uint8_t* d = dst_row + (size_t)x * channels;
                if (isBGR) { d[0]=b; d[1]=g; d[2]=r; } else { d[0]=r; d[1]=g; d[2]=b; }
                if (channels == 4) d[3] = a;
            }
        }

        return output;
    }
};

