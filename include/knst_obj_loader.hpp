/*
----------------------------
knst_obj_loader.hpp
----------------------------

  The GLM library is essential
  It reads Wavefront OBJ files—specifically 3D mesh data (vertices, normals, UVs, indices). It utilizes memory-mapped I/O (mmap/CreateFileMapping), meaning it reads the file without copying it into RAM. It deduplicates faces (reusing the same vertices), optionally corrects winding order, and generates missing normals. It also extracts texture paths from the associated MTL file


*/




#pragma once




#if KNST_USING_PLATFORM_LINUX
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif
#if KNST_USING_PLATFORM_WINDOWS
    #include <memoryapi.h>
#endif


struct KnstObjLoadOptions {
   
    bool autoFixWinding = true;
    bool generateNormalsIfMissing = true;
};


class knst_obj_loader {
public:

    struct MeshData { // Holds the result of the loaded OBJ.
        knst_vector<KnstVertex3D> vertices;
        knst_vector<uint32_t> indices;
        knst_byte_string texturePath;
        knst_byte_string materialName;
        knst_byte_string objName;
        bool loaded = false;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;
    };


         static bool Load(const knst_c16string& path, MeshData& outData,const KnstObjLoadOptions& options = KnstObjLoadOptions()) {
        knst_byte_string pathBytes(path);

        uint8_t* file_data = nullptr;
        size_t file_size = 0;

        if (!read_file(pathBytes, &file_data, &file_size)) {
            return false;
        }

        knst_byte_string baseDir;
        uint32_t last_sep = 0;
        bool found_sep = false;
        for (uint32_t i = 0; i < pathBytes.length(); i++) {
            unsigned char c = pathBytes[i];
            if (c == '/' || c == '\\') { last_sep = i; found_sep = true; }
        }
        if (found_sep) {
            baseDir = knst_byte_string(pathBytes.data(), last_sep + 1);
        }

        bool result = LoadFromMemory(file_data, file_size, outData, options, baseDir);
        free_file_data(file_data, file_size);

        return result;
    }


        static bool LoadFromMemory(const uint8_t* data, size_t size, MeshData& outData, const KnstObjLoadOptions& options = KnstObjLoadOptions(), const knst_byte_string& baseDir = knst_byte_string()) {
        if (data == nullptr || size == 0) {
            return false;
        }

        knst_vector<glm::vec3> positions;
        knst_vector<glm::vec2> texCoords;
        knst_vector<glm::vec3> normals;

        knst_byte_string currentMaterial;
        knst_byte_string mtlFile;
        knst_byte_string objName;

        size_t estV = 0, estVt = 0, estVn = 0, estF = 0;
        CountElements(data, size, estV, estVt, estVn, estF);

        positions.reserve(estV > 0 ? estV : 64);
        texCoords.reserve(estVt > 0 ? estVt : 16);
        normals.reserve(estVn > 0 ? estVn : 16);

        size_t estTriVerts = estF * 3;
        outData.indices.reserve(estTriVerts > 0 ? estTriVerts : 64);
        outData.vertices.reserve(estV > 0 ? estV : 64);

        VertexDedupMap dedup;
        dedup.Init(estTriVerts > 0 ? estTriVerts : 64);

        const char* ptr = (const char*)data;
        const char* end = (const char*)data + size;

        uint32_t lineNum = 0;
        uint32_t faceCount = 0;

        while (ptr < end) {
            const char* lineStart = ptr;
            while (ptr < end && *ptr != '\n' && *ptr != '\r') ptr++;
            const char* lineEnd = ptr;

            if (lineEnd > lineStart) {
                ParseLineFast(lineStart, lineEnd, positions, texCoords, normals,outData, mtlFile, currentMaterial, objName,lineNum, faceCount, dedup, options);
            }

            while (ptr < end && (*ptr == '\n' || *ptr == '\r')) ptr++;
            lineNum++;
        }


        if (options.generateNormalsIfMissing && normals.empty() && !outData.vertices.empty()) {
            GenerateSmoothNormals(outData);
        }

        outData.vertexCount = (uint32_t)outData.vertices.size();
        outData.indexCount = (uint32_t)outData.indices.size();
        outData.objName = objName;
        outData.loaded = !outData.vertices.empty();

               if (!mtlFile.empty() && !currentMaterial.empty()) {
            knst_byte_string fullMtlPath = baseDir;
            fullMtlPath.append(mtlFile);
            knst_byte_string texPath = FindTextureInMTL(fullMtlPath, currentMaterial);
            if (!texPath.empty()) {
                knst_byte_string fullTexPath = baseDir;
                fullTexPath.append(texPath);
                outData.texturePath = fullTexPath;
            }
        }

        return outData.loaded;
    }

private:

    static void CountElements(const uint8_t* data, size_t size,size_t& outV, size_t& outVt, size_t& outVn, size_t& outF) { // It performs a preliminary count to determine the number of v, vt, vn, and f lines. It checks the first two characters of each line and increments the counters. This allows it to reserve the correct amount of space, avoiding reallocation. It is a single-pass, fast process
        outV = outVt = outVn = outF = 0;
        const uint8_t* p = data;
        const uint8_t* end = data + size;
        bool atLineStart = true;

        while (p < end) {
            if (atLineStart) {
                uint8_t c0 = p[0];
                uint8_t c1 = (p + 1 < end) ? p[1] : 0;
                if (c0 == 'v') {
                    if (c1 == ' ' || c1 == '\t') outV++;
                    else if (c1 == 't') outVt++;
                    else if (c1 == 'n') outVn++;
                } else if (c0 == 'f' && (c1 == ' ' || c1 == '\t')) {
                    outF++;
                }
                atLineStart = false;
            }
            if (*p == '\n' || *p == '\r') atLineStart = true;
            p++;
        }
    }

    static inline float ParseFloatFast(const char*& p, const char* end) { // A fast float parser. It reads digits manually instead of strtof — performance critical as it is called millions of times on OBJ files. Supports pointers, integers, decimals, and exponents (e±N). It retrieves the pointer by reference, so it tells the caller where it left off
        while (p < end && (*p == ' ' || *p == '\t')) p++;

        bool neg = false;
        if (p < end && (*p == '+' || *p == '-')) { neg = (*p == '-'); p++; }

        double intPart = 0.0;
        while (p < end && *p >= '0' && *p <= '9') { intPart = intPart * 10.0 + (*p - '0'); p++; }

        double fracPart = 0.0, fracDiv = 1.0;
        if (p < end && *p == '.') {
            p++;
            while (p < end && *p >= '0' && *p <= '9') {
                fracPart = fracPart * 10.0 + (*p - '0');
                fracDiv *= 10.0;
                p++;
            }
        }

        double value = intPart + fracPart / fracDiv;

        if (p < end && (*p == 'e' || *p == 'E')) {
            const char* save = p;
            p++;
            bool expNeg = false;
            if (p < end && (*p == '+' || *p == '-')) { expNeg = (*p == '-'); p++; }
            if (p < end && *p >= '0' && *p <= '9') {
                int expVal = 0;
                while (p < end && *p >= '0' && *p <= '9') { expVal = expVal * 10 + (*p - '0'); p++; }
                value *= std::pow(10.0, expNeg ? -expVal : expVal);
            } else {
                p = save;
            }
        }

        return (float)(neg ? -value : value);
    }


    class VertexDedupMap {
    public:
        VertexDedupMap() { slots.resize(16); capacityMask = 15; count = 0; } // Constructor. Starts with 16 slots, a mask of 15, and a counter of 0. It begins small and grows via "Grow" as it fills up

        void Init(size_t expectedInsertions) { // It performs pre-allocation based on the expected number of elements. It targets a load factor of ~0.5 (doubling the capacity) and rounds the capacity up to a power of 2. It avoids reallocations by utilizing the estimate provided by CountElements
            size_t cap = 16;
            size_t need = expectedInsertions * 2;
            while (cap < need) cap <<= 1;
            slots.clear();
            slots.resize(cap);
            capacityMask = cap - 1;
            count = 0;
        }


        uint32_t FindOrInsert(int32_t v, int32_t vt, int32_t vn, uint32_t newIndexIfMissing, bool& wasNew) { // It searches for the vertex in the hash table. If found, it returns the existing index; otherwise, it adds the vertex and returns the new index. The `v/vt/vn` triplet serves as the key. Collisions are resolved using linear probing. The `wasNew` output parameter lets the caller know whether to add a new vertex or use an existing one ,, Even if the same vertex appears in multiple triangles, only a single copy is stored—this is deduplication


            if ((count + 1) * 2 > (capacityMask + 1)) Grow();

            uint64_t h = Hash(v, vt, vn);
            size_t idx = (size_t)h & capacityMask;
            while (slots[idx].occupied) {
                if (slots[idx].hash == h && slots[idx].v == v && slots[idx].vt == vt && slots[idx].vn == vn) {
                    wasNew = false;
                    return slots[idx].index;
                }
                idx = (idx + 1) & capacityMask;
            }

            slots[idx].hash = h;
            slots[idx].v = v; slots[idx].vt = vt; slots[idx].vn = vn;
            slots[idx].index = newIndexIfMissing;
            slots[idx].occupied = true;
            count++;
            wasNew = true;
            return newIndexIfMissing;
        }

    private:
        struct Slot { // A cell of the hash table. hash ==>  for fast comparison. v/vt/vn ==>  original OBJ indices (key). index ==>  ​​final index in the mesh. occupied ==>  whether the slot is occupied
            uint64_t hash = 0;
            int32_t v = -1, vt = -1, vn = -1;
            uint32_t index = 0;
            bool occupied = false;
        };

        knst_vector<Slot> slots; // The hash table's slots — `knst_vector<Slot>`. Initially 16; doubles in size via `Grow`
        size_t capacityMask = 15; // Capacity mask. cap - 1 (power of 2 minus 1). hash & capacityMask ==>  fast modulo. 15 = mask for 16 slots
        size_t count = 0; // The number of occupied slots in the table. For load factor monitoring: grow when count/capacity exceeds 50%

        static inline uint64_t Hash(int32_t v, int32_t vt, int32_t vn) { // Hash function. FNV-1a initialization + mixing + finalizer (Murmur3-style). Reduces the v/vt/vn triplet to a single 64-bit hash. Good distribution ==> fewer collisions ==>  fast lookup
            uint64_t h = 1469598103934665603ULL;
            auto mix = [&](uint32_t x) { h ^= x; h *= 1099511628211ULL; };
            mix((uint32_t)v);
            mix((uint32_t)vt);
            mix((uint32_t)vn);
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdULL;
            h ^= h >> 33;
            return h;
        }

        void Grow() { // It doubles the size of the table. It allocates a new array and reinserts the existing elements using the new mask (rehash). The capacityMask is updated. The load factor decreases, and collisions are reduced
            size_t newCap = (capacityMask + 1) * 2;
            knst_vector<Slot> newSlots;
            newSlots.resize(newCap);
            size_t newMask = newCap - 1;

            size_t oldCap = capacityMask + 1;
            for (size_t i = 0; i < oldCap; i++) {
                if (!slots[i].occupied) continue;
                size_t idx = (size_t)slots[i].hash & newMask;
                while (newSlots[idx].occupied) idx = (idx + 1) & newMask;
                newSlots[idx] = slots[i];
            }

            slots = newSlots;
            capacityMask = newMask;
        }
    };


    static bool read_file(const knst_byte_string& path, uint8_t** out_data, size_t* out_size) { // Opens the file as memory-mapped. It does not copy the content but maps the file directly into virtual memory—resulting in zero-copy and fast reads for large files ,, Windows: Uses CreateFileA + CreateFileMappingA + MapViewOfFile. Preloads using PrefetchVirtualMemory ,, Linux/Android: Uses open + mmap. Instructs the kernel to "read sequentially and preload" via madvise(MADV_SEQUENTIAL | MADV_WILLNEED) ,, X11/Wayland: Preloads using posix_fadvise + MAP_POPULATE. Fallback: Uses mmap + MAP_POPULATE; retries with MAP_PRIVATE if that fails.Returns out_data and out_size. The user must call free_file_data (which invokes munmap/UnmapViewOfFile) when finished.Supports only regular files (S_ISREG); directories and sockets are rejected. On error, the file descriptor (fd) is closed to prevent leaks.

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
                *out_data = (uint8_t*)mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
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


    static void ParseLineFast( // Parses the OBJ line based on the initial characters—adds data for v/vt/vn, processes faces for f, records metadata for mtllib/usemtl/o, and skips comments and blank lines
        const char* lineStart, const char* lineEnd,
        knst_vector<glm::vec3>& positions,
        knst_vector<glm::vec2>& texCoords,
        knst_vector<glm::vec3>& normals,
        MeshData& outData,
        knst_byte_string& mtlFile,
        knst_byte_string& currentMaterial,
        knst_byte_string& objName,
        uint32_t lineNum,
        uint32_t& faceCount,
        VertexDedupMap& dedup,
        const KnstObjLoadOptions& options
    ) {
        (void)lineNum;
        size_t len = (size_t)(lineEnd - lineStart);
        if (len == 0 || lineStart[0] == '#') return;
        if (len < 2) return;

        const char* data = lineStart;


        if (data[0] == 'v' && data[1] == ' ') {
            const char* p = data + 2;
            glm::vec3 pos;
            pos.x = ParseFloatFast(p, lineEnd);
            pos.y = ParseFloatFast(p, lineEnd);
            pos.z = ParseFloatFast(p, lineEnd);
            positions.push_back(pos);
            return;
        }


        if (data[0] == 'v' && data[1] == 't') {
            const char* p = data + 2;
            glm::vec2 uv;
            uv.x = ParseFloatFast(p, lineEnd);
            uv.y = ParseFloatFast(p, lineEnd);
            texCoords.push_back(uv);
            return;
        }


        if (data[0] == 'v' && data[1] == 'n') {
            const char* p = data + 2;
            glm::vec3 n;
            n.x = ParseFloatFast(p, lineEnd);
            n.y = ParseFloatFast(p, lineEnd);
            n.z = ParseFloatFast(p, lineEnd);
            normals.push_back(n);
            return;
        }


        if (data[0] == 'f' && data[1] == ' ') {
            ParseFaceFast(data + 2, lineEnd, positions, texCoords, normals, outData, dedup, options);
            faceCount++;
            return;
        }


        if (len >= 7 && data[0]=='m' && data[1]=='t' && data[2]=='l' && data[3]=='l' && data[4]=='i' && data[5]=='b' &&
            (data[6]==' ' || data[6]=='\t')) {
            const char* start = data + 6;
            while (start < lineEnd && (*start==' '||*start=='\t')) start++;
            mtlFile.clear();
            mtlFile.append((const unsigned char*)start, (size_t)(lineEnd - start));
            return;
        }


        if (len >= 7 && data[0]=='u' && data[1]=='s' && data[2]=='e' && data[3]=='m' && data[4]=='t' && data[5]=='l' &&
            (data[6]==' ' || data[6]=='\t')) {
            const char* start = data + 6;
            while (start < lineEnd && (*start==' '||*start=='\t')) start++;
            currentMaterial.clear();
            currentMaterial.append((const unsigned char*)start, (size_t)(lineEnd - start));
            return;
        }


        if (data[0] == 'o' && data[1] == ' ') {
            const char* start = data + 1;
            while (start < lineEnd && (*start==' '||*start=='\t')) start++;
            objName.clear();
            objName.append((const unsigned char*)start, (size_t)(lineEnd - start));
            return;
        }
    }


   
    static void ParseFaceFast( // It parses the OBJ 'f' line, triangulates the polygon, corrects the winding if necessary, adds vertices while deduplicating them, and writes the indices
        const char* p, const char* lineEnd,
        const knst_vector<glm::vec3>& positions,
        const knst_vector<glm::vec2>& texCoords,
        const knst_vector<glm::vec3>& normals,
        MeshData& outData,
        VertexDedupMap& dedup,
        const KnstObjLoadOptions& options
    ) {

        const int MAX_FACE_VERTS = 64;
        int32_t vIdx[MAX_FACE_VERTS];
        int32_t vtIdx[MAX_FACE_VERTS];
        int32_t vnIdx[MAX_FACE_VERTS];
        int count = 0;

        const char* ptr = p;
    while (ptr < lineEnd && count < MAX_FACE_VERTS) {
        while (ptr < lineEnd && (*ptr == ' ' || *ptr == '\t')) ptr++;
        if (ptr >= lineEnd) break;
        if (*ptr != '-' && !(*ptr >= '0' && *ptr <= '9')) break;

        bool neg = false;
        if (*ptr == '-') { neg = true; ptr++; }

        int32_t vi = 0;
        bool anyDigit = false;
        while (ptr < lineEnd && *ptr >= '0' && *ptr <= '9') { vi = vi * 10 + (*ptr - '0'); ptr++; anyDigit = true; }
        if (!anyDigit) break;

        int32_t vt = -1, vn = -1;
        if (ptr < lineEnd && *ptr == '/') {
            ptr++;
            bool vtNeg = false;
            if (ptr < lineEnd && *ptr == '-') { vtNeg = true; ptr++; }
            if (ptr < lineEnd && *ptr >= '0' && *ptr <= '9') {
                int32_t v2 = 0;
                while (ptr < lineEnd && *ptr >= '0' && *ptr <= '9') { v2 = v2 * 10 + (*ptr - '0'); ptr++; }
                vt = vtNeg ? (int32_t)texCoords.size() - v2 : v2 - 1;
            }
            if (ptr < lineEnd && *ptr == '/') {
                ptr++;
                bool vnNeg = false;
                if (ptr < lineEnd && *ptr == '-') { vnNeg = true; ptr++; }
                if (ptr < lineEnd && *ptr >= '0' && *ptr <= '9') {
                    int32_t v3 = 0;
                    while (ptr < lineEnd && *ptr >= '0' && *ptr <= '9') { v3 = v3 * 10 + (*ptr - '0'); ptr++; }
                    vn = vnNeg ? (int32_t)normals.size() - v3 : v3 - 1;
                }
            }
        }

        vIdx[count] = neg ? (int32_t)positions.size() - vi : vi - 1;
        vtIdx[count] = vt;
        vnIdx[count] = vn;
        count++;
    }

        if (count < 3) return;

        for (int i = 1; i + 1 < count; i++) {
            int tri[3] = { 0, i, i + 1 };

          
            if (options.autoFixWinding) {
                int32_t va = vIdx[tri[0]], vb = vIdx[tri[1]], vc = vIdx[tri[2]];
                int32_t vna = vnIdx[tri[0]], vnb = vnIdx[tri[1]], vnc = vnIdx[tri[2]];

                bool posOk = va >= 0 && va < (int32_t)positions.size() &&
                             vb >= 0 && vb < (int32_t)positions.size() &&
                             vc >= 0 && vc < (int32_t)positions.size();

                bool normOk = vna >= 0 && vna < (int32_t)normals.size() &&
                              vnb >= 0 && vnb < (int32_t)normals.size() &&
                              vnc >= 0 && vnc < (int32_t)normals.size();

                if (posOk && normOk) {
                    glm::vec3 p0 = positions[va];
                    glm::vec3 p1 = positions[vb];
                    glm::vec3 p2 = positions[vc];
                    glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);

                    glm::vec3 avgNormal = normals[vna] + normals[vnb] + normals[vnc];

                    if (glm::dot(faceNormal, avgNormal) < 0.0f) {
                        int tmp = tri[1];
                        tri[1] = tri[2];
                        tri[2] = tmp;
                    }
                }
            }

            for (int k = 0; k < 3; k++) {
                int j = tri[k];
                int32_t vi = vIdx[j], vti = vtIdx[j], vni = vnIdx[j];

                bool wasNew = false;
                uint32_t candidateIndex = (uint32_t)outData.vertices.size();
                uint32_t finalIndex = dedup.FindOrInsert(vi, vti, vni, candidateIndex, wasNew);

                if (wasNew) {
                    KnstVertex3D vertex{};

                    if (vi >= 0 && vi < (int32_t)positions.size()) {
                        vertex.x = positions[vi].x;
                        vertex.y = positions[vi].y;
                        vertex.z = positions[vi].z;
                    }
                    if (vti >= 0 && vti < (int32_t)texCoords.size()) {
                        vertex.u = texCoords[vti].x;
                        vertex.v = 1.0f - texCoords[vti].y;
                    }
                    if (vni >= 0 && vni < (int32_t)normals.size()) {
                        vertex.nx = normals[vni].x;
                        vertex.ny = normals[vni].y;
                        vertex.nz = normals[vni].z;
                    }

                    vertex.r = 1.0f; vertex.g = 1.0f; vertex.b = 1.0f; vertex.a = 1.0f;

                    outData.vertices.push_back(vertex);
                }

                outData.indices.push_back(finalIndex);
            }
        }
    }



    static void GenerateSmoothNormals(MeshData& data) { // It generates smooth normals. It calculates the face normal for each triangle and accumulates it into that triangle's vertices. Then, it normalizes the sum at each vertex—effectively averaging the normals of the adjacent faces. This results in smooth shading instead of sharp edges
        if (data.vertices.empty() || data.indices.size() < 3) return;

        knst_vector<glm::vec3> accum;
        accum.resize(data.vertices.size(), glm::vec3(0.0f, 0.0f, 0.0f));

        for (size_t i = 0; i + 2 < data.indices.size(); i += 3) {
            uint32_t i0 = data.indices[i];
            uint32_t i1 = data.indices[i + 1];
            uint32_t i2 = data.indices[i + 2];

            if (i0 >= data.vertices.size() || i1 >= data.vertices.size() || i2 >= data.vertices.size()) {
                continue;
            }

            glm::vec3 p0(data.vertices[i0].x, data.vertices[i0].y, data.vertices[i0].z);
            glm::vec3 p1(data.vertices[i1].x, data.vertices[i1].y, data.vertices[i1].z);
            glm::vec3 p2(data.vertices[i2].x, data.vertices[i2].y, data.vertices[i2].z);

            glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);

            accum[i0] += faceNormal;
            accum[i1] += faceNormal;
            accum[i2] += faceNormal;
        }

        for (size_t i = 0; i < data.vertices.size(); i++) {
            glm::vec3 n = accum[i];
            float lenSq = n.x * n.x + n.y * n.y + n.z * n.z;

            if (lenSq > 0.0000001f) {
                float invLen = 1.0f / std::sqrt(lenSq);
                n.x *= invLen;
                n.y *= invLen;
                n.z *= invLen;
            } else {
                n = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            data.vertices[i].nx = n.x;
            data.vertices[i].ny = n.y;
            data.vertices[i].nz = n.z;
        }
    }


    static knst_byte_string FindTextureInMTL(const knst_byte_string& mtlPath,const knst_byte_string& matName) { // It locates the material specified in the MTL file and extracts the `map_Kd` (diffuse texture) line. It tracks the start of the material using `newmtl` and captures the `map_Kd` value once the correct material is reached. It then returns the file path.

        uint8_t* file_data = nullptr;
        size_t file_size = 0;

        if (!read_file(mtlPath, &file_data, &file_size)) {
            return knst_byte_string();
        }

        const char* ptr = (const char*)file_data;
        const char* end = (const char*)file_data + file_size;
        bool inMaterial = false;
        knst_byte_string result;

        while (ptr < end) {
            const char* lineStart = ptr;
            while (ptr < end && *ptr != '\n' && *ptr != '\r') ptr++;
            const char* lineEnd = ptr;
            size_t lineLen = (size_t)(lineEnd - lineStart);

            if (lineLen > 0) {
                const char* data = lineStart;


                if (lineLen >= 7 && data[0]=='n' && data[1]=='e' && data[2]=='w' &&
                    data[3]=='m' && data[4]=='t' && data[5]=='l' && (data[6]==' '||data[6]=='\t')) {
                    const char* str = data + 6;
                    while (str < lineEnd && *str == ' ') str++;

                    size_t remainLen = (size_t)(lineEnd - str);
                    size_t matLen = matName.length();
                    inMaterial = (remainLen == matLen) && (matLen == 0 || memcmp(str, matName.data(), matLen) == 0);

                }


                if (inMaterial && lineLen >= 7 && data[0]=='m' && data[1]=='a' && data[2]=='p' &&
                    data[3]=='_' && data[4]=='K' && data[5]=='d' && (data[6]==' '||data[6]=='\t')) {
                    const char* str = data + 6;
                    while (str < lineEnd && *str == ' ') str++;
                    if (str < lineEnd) {
                        result.append((const unsigned char*)str, (size_t)(lineEnd - str));
                        break;
                    }
                }
            }

            while (ptr < end && (*ptr == '\n' || *ptr == '\r')) ptr++;
        }

        free_file_data(file_data, file_size);
        return result;
    }
};

