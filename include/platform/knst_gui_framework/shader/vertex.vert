#version 450


layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inNormal;


layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outUV;
layout(location = 2) out vec3 outNormal;
layout(location = 3) out vec3 outWorldPos;
layout(location = 4) out float outIs2D;


layout(push_constant) uniform PushConstants {
    float width;
    float height;
    float time;
    float useUV;
    float useNormal;
    float useTangent;
    float useBones;
    float useCustom;
    float is2D;
    float rotX;
    float rotY;
    float rotZ;
    float translateX;
    float translateY;
    float translateZ;
    float scaleX;
    float scaleY;
    float scaleZ;
    float camX;
    float camY;
    float camZ;
    float camTargetX;
    float camTargetY;
    float camTargetZ;
    float camUpX;
    float camUpY;
    float camUpZ;
    float lightX;
    float lightY;
    float lightZ;
    float lightIntensity;
    float lightColorR;
    float lightColorG;
    float lightColorB;
    float colorR;
    float colorG;
    float colorB;
    float colorA;
    float projectionType;
    float fov;
    float nearPlane;
    float farPlane;
    float orthoSize;
    float coordSystem;
} push;


mat4 translateMatrix(float tx, float ty, float tz) {
    return mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        tx,  ty,  tz,  1.0
    );
}

mat4 rotationX(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat4(
        1.0, 0.0, 0.0, 0.0,
        0.0, c,   s,   0.0,
        0.0, -s,  c,   0.0,
        0.0, 0.0, 0.0, 1.0
    );
}

mat4 rotationY(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat4(
        c,   0.0, -s,  0.0,
        0.0, 1.0, 0.0, 0.0,
        s,   0.0, c,   0.0,
        0.0, 0.0, 0.0, 1.0
    );
}

mat4 rotationZ(float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return mat4(
        c,   s,   0.0, 0.0,
        -s,  c,   0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0
    );
}

mat4 scaleMatrix(float sx, float sy, float sz) {
    return mat4(
        sx,  0.0, 0.0, 0.0,
        0.0, sy,  0.0, 0.0,
        0.0, 0.0, sz,  0.0,
        0.0, 0.0, 0.0, 1.0
    );
}

mat4 lookAtMatrix(vec3 eye, vec3 center, vec3 up) {
    vec3 f = normalize(center - eye);
    vec3 s = normalize(cross(f, up));
    vec3 u = cross(s, f);
    return mat4(
        s.x, u.x, -f.x, 0.0,
        s.y, u.y, -f.y, 0.0,
        s.z, u.z, -f.z, 0.0,
        -dot(s, eye), -dot(u, eye), dot(f, eye), 1.0
    );
}

mat4 perspectiveMatrix(float fov, float aspect, float near, float far) {
    float tanHalfFov = tan(fov * 0.5);
    float f = 1.0 / tanHalfFov;
    return mat4(
        f / aspect, 0.0, 0.0, 0.0,
        0.0, f, 0.0, 0.0,
        0.0, 0.0, (far + near) / (near - far), -1.0,
        0.0, 0.0, (2.0 * far * near) / (near - far), 0.0
    );
}

mat4 orthographicMatrix(float left, float right, float bottom, float top, float near, float far) {
    return mat4(
        2.0 / (right - left), 0.0, 0.0, 0.0,
        0.0, 2.0 / (top - bottom), 0.0, 0.0,
        0.0, 0.0, -2.0 / (far - near), 0.0,
        -(right + left) / (right - left), -(top + bottom) / (top - bottom), -(far + near) / (far - near), 1.0
    );
}


void main() {
    vec3 position = inPosition;
    vec3 normal = inNormal;

   
    if (push.is2D > 0.5) {
        vec3 finalPos = position;

       
        if (push.coordSystem < 0.5) {
            finalPos.x = (finalPos.x / push.width) * 2.0 - 1.0;
            finalPos.y = 1.0 - (finalPos.y / push.height) * 2.0;
          
        }

        gl_Position = vec4(finalPos, 1.0);
        outColor = inColor;
        outUV = inUV;
        outNormal = vec3(0, 0, 1);
        outWorldPos = vec3(0, 0, 0);
        outIs2D = 1.0;
        return;
    }


   
    mat4 model = scaleMatrix(push.scaleX, push.scaleY, push.scaleZ);
    model = rotationX(push.rotX) * model;
    model = rotationY(push.rotY) * model;
    model = rotationZ(push.rotZ) * model;
    model = translateMatrix(push.translateX, push.translateY, push.translateZ) * model;

    vec4 worldPos = model * vec4(position, 1.0);
    vec3 worldNormal = normalize((model * vec4(normal, 0.0)).xyz);

   
    vec3 eye = vec3(push.camX, push.camY, push.camZ);
    vec3 center = vec3(push.camTargetX, push.camTargetY, push.camTargetZ);
    vec3 up = vec3(push.camUpX, push.camUpY, push.camUpZ);
    mat4 view = lookAtMatrix(eye, center, up);


    mat4 proj;
    float aspect = push.width / push.height;

    if (push.projectionType < 0.5) {
        proj = mat4(1.0);
    } else if (push.projectionType < 1.5) {
        proj = perspectiveMatrix(push.fov, aspect, push.nearPlane, push.farPlane);
    } else if (push.projectionType < 2.5) {
        float size = push.orthoSize;
        float left = -size * aspect;
        float right = size * aspect;
        float bottom = -size;
        float top = size;
        proj = orthographicMatrix(left, right, bottom, top, push.nearPlane, push.farPlane);
    } else {
        proj = mat4(1.0);
    }

    gl_Position = proj * view * worldPos;

    outColor = inColor * vec4(push.colorR, push.colorG, push.colorB, push.colorA);
    outUV = inUV;
    outNormal = worldNormal;
    outWorldPos = worldPos.xyz;
    outIs2D = 0.0;
}