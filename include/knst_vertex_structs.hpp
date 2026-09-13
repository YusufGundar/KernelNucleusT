#pragma once



struct KnstVertex3D { // 3D VERTEX STRUCTS
    float x, y, z;
    float r, g, b, a;
    float u, v;
    float nx, ny, nz;
    float tx, ty, tz;
    uint32_t boneIndices[4];
    float boneWeights[4];
    float customData[4];

    static KnstVertex3D Make(float x, float y, float z,float r=1, float g=1, float b=1, float a=1,float u=0, float v=0, float nx=0, float ny=0, float nz=1) {
    KnstVertex3D vertex;
    vertex.x = x;
    vertex.y = y;
    vertex.z = z;
    vertex.r = r;
    vertex.g = g;
    vertex.b = b;
    vertex.a = a;
    vertex.u = u;
    vertex.v = v;
    vertex.nx = nx;
    vertex.ny = ny;
    vertex.nz = nz;
    vertex.tx = 0;
    vertex.ty = 0;
    vertex.tz = 0;
    vertex.boneIndices[0] = 0;
    vertex.boneIndices[1] = 0;
    vertex.boneIndices[2] = 0;
    vertex.boneIndices[3] = 0;
    vertex.boneWeights[0] = 1.0f;
    vertex.boneWeights[1] = 0.0f;
    vertex.boneWeights[2] = 0.0f;
    vertex.boneWeights[3] = 0.0f;
    vertex.customData[0] = 0.0f;
    vertex.customData[1] = 0.0f;
    vertex.customData[2] = 0.0f;
    vertex.customData[3] = 0.0f;
    return vertex;
}
};



struct KnstVertex2D {  // 2D VERTEX STRUCTS
    float x, y, z;
    float r, g, b, a;
    float u, v;
    
    static KnstVertex2D Make(float x, float y, float z, float r=1, float g=1, float b=1, float a=1,float u=0, float v=0) {
                         
                         
    KnstVertex2D vertex;
    vertex.x = x; 
    vertex.y = y; 
    vertex.z = z;
    vertex.r = r; 
    vertex.g = g; 
    vertex.b = b; 
    vertex.a = a;
    vertex.u = u; 
    vertex.v = v;
    return vertex;
}
};
