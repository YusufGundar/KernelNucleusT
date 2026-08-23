#version 450

// dostum buraya birçok özellik ekledim geliştirilebilirlik açısından esneklik açısından ihtiyacınız olan kısımları kullanabilirsiniz kod içinden tabi şimdilik kütüphanemiz destek vermiyor ancak ilerde tam teşekkürlü bir sistem sunucum bu hali için şimdiden kusura bakmayın çok dağınık çalıştım
layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;
layout(location = 4) in float fragIs2D;  // 1.0 = 2d, 0.0 = 3d


layout(location = 0) out vec4 outColor;


layout(set = 0, binding = 0) uniform sampler2D texSampler;

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



vec3 CalculateDiffuse(vec3 normal, vec3 lightDir, vec3 lightColor) {
    float diff = max(dot(normal, lightDir), 0.0);
    return diff * lightColor;
}

vec3 CalculateSpecular(vec3 normal, vec3 lightDir, vec3 viewDir, vec3 lightColor, float shininess) {
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), shininess);
    return spec * lightColor;
}


void main() {
    vec4 baseColor = fragColor;
    
   
    if (fragIs2D > 0.5) {
        if (push.useUV > 0.5) {
            vec4 texColor = texture(texSampler, fragUV);
            outColor = baseColor * texColor;
        } else {
            outColor = baseColor;
        }
        return;
    }
    

    vec3 normal = normalize(fragNormal);
    
  
    vec3 viewPos = vec3(push.camX, push.camY, push.camZ);
    

    vec3 lightPos = vec3(push.lightX, push.lightY, push.lightZ);
    vec3 lightColor = vec3(push.lightColorR, push.lightColorG, push.lightColorB) * push.lightIntensity;
    

    vec3 viewDir = normalize(viewPos - fragWorldPos);
    vec3 lightDir = normalize(lightPos - fragWorldPos);
    

    vec3 ambient = vec3(0.15) * lightColor;
    
   
    vec3 diffuse = CalculateDiffuse(normal, lightDir, lightColor);
    
   
    vec3 specular = CalculateSpecular(normal, lightDir, viewDir, lightColor, 32.0);
    
   
    vec4 texColor;
    if (push.useUV > 0.5) {
        texColor = texture(texSampler, fragUV);
    } else {
        texColor = vec4(1.0, 1.0, 1.0, 1.0);
    }
    
  
    vec3 finalColor = texColor.rgb * (ambient + diffuse) + specular * 0.3;
    
   
    float alpha = texColor.a * baseColor.a;
    
    outColor = vec4(finalColor, alpha);
}