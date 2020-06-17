/*
    raytracer.cl
*/

struct SDL_Color {
    uchar r;
    uchar g;
    uchar b;
    uchar a;
};

struct Color {
    float r;
    float g;
    float b;
};

struct Ray {
    float3 position;
    float3 direction;
};

struct Camera {
    float3 position;
    float3 foward;
    float3 right;
    float3 up;
    float width;
    float height;
};

struct Sphere {
    float3 position;
    float radius;
    float3 color;
};

struct Light {
    float3 position;
    float intencity;
    float3 color;
};

struct Hit {
    bool isHit;
    struct Sphere sphere;
    float t;
};

struct ReflectHit {
    struct Ray nextRay;
    float3 color;
};

struct Ray camera_makeRay(float2 point, struct Camera camera) {
    float3 d = camera.foward + point.x * camera.width * camera.right + point.y * camera.height * camera.up;
    struct Ray ray;
    ray.position = camera.position;
    ray.direction = normalize(d);
    return ray;
}

float2 intersection(struct Ray ray, struct Sphere sphere) {
    float3 v = ray.position - sphere.position;

    float k1 = dot(ray.direction, ray.direction);
    float k2 = 2 * dot(v, ray.direction);
    float k3 = dot(v, v) - sphere.radius * sphere.radius;

    float d = k2 * k2 - 4 * k1 * k3;

    float2 temp;

    temp.x = (-k2 + sqrt(d)) / (2 * k1);
    temp.y = (-k2 - sqrt(d)) / (2 * k1);

    return temp;
}

struct Hit closestIntersection(struct Ray ray, float zmin, float zmax, __global struct Sphere* spheres, uint sphereLength, float t) {
    //bool b = false;
    struct Hit hit;
    hit.isHit = false;
    hit.t = t;

    for(uint i = 0; i < sphereLength; i++) {
        float2 tv = intersection(ray, spheres[i]);

        if((tv.x >= zmin && tv.x <= zmax) && tv.x < hit.t) {
            hit.t = tv.x;
            hit.sphere = spheres[i];
            hit.isHit = true;
        }

        if((tv.y >= zmin && tv.y <= zmax) && tv.y < hit.t) {
            hit.t = tv.y;
            hit.sphere = spheres[i];
            hit.isHit = true;
        }
    }

    return hit;
}

float3 reflect(float3 R, float3 N) {
    return 2.0f * N * dot(N, R) - R;
}

struct ReflectHit computeAmbient(
    struct Ray ray,
    float zmin,
    float zmax,
    __global struct Sphere* spheres,
    uint sphereLength,
    __global struct Light* lights, 
    uint lightLength,
    float specularFactor,
    float3 clearColor
) {
    struct Hit hit = closestIntersection(
        ray, 
        zmin, 
        zmax, 
        spheres, 
        sphereLength, 
        zmax);

    if(!hit.isHit) {
        // Reflect Ray
        struct ReflectHit reflectHit;
        reflectHit.nextRay = ray;
        reflectHit.color = clearColor;
        return reflectHit;
    }

    float3 P = ray.position + ray.direction * hit.t;
    float3 N = P - hit.sphere.position;
    N = normalize(N);
    float3 V = -ray.direction;

    float3 light = (float3)(0.0, 0.0, 0.0);

    for(int i = 0; i < lightLength; i++) {
        struct Ray shadowRay;
        shadowRay.position = P;
        shadowRay.direction = lights[i].position - P;

        struct Hit shadowHit = closestIntersection(
            shadowRay,
            0.001f,
            1024.0f,
            spheres,
            sphereLength,
            1024.0f
        );

        if(shadowHit.isHit) {
            continue;
        }

        float3 L = normalize(lights[i].position - P);
        float3 H = normalize(L + V);


        float ndotl = dot(N, L);
        float ndoth = dot(N, H);

        float3 diffuse = hit.sphere.color * lights[i].color * ndotl;
        float3 specular = lights[i].color * pow(ndoth, specularFactor * 256.0f);

        light += (diffuse + specular) * lights[i].intencity;
    }

    light += (float3)(0.1f, 0.1f, 0.1f) * hit.sphere.color;

    // Reflect Ray
    float3 R = reflect(-ray.direction, N);

    struct Ray reflectRay;
    reflectRay.position = P;
    reflectRay.direction = R;

    struct ReflectHit reflectHit;
    reflectHit.nextRay = reflectRay;
    reflectHit.color = light;

    return reflectHit;
}

struct Color computeLighting(
    struct Ray ray,
    float3 P, 
    float3 N, 
    float3 V, 
    struct Hit hit, 
    __global struct Light* lights, 
    uint lightLength,
    __global struct Sphere* spheres,
    uint sphereLength,
    float specularFactor,
    float3 clearColor) {

    float3 light = (float3)(0.0f, 0.0f, 0.0f);

    for(uint i = 0; i < lightLength; i++) {

        struct Ray shadowRay;
        shadowRay.position = P;
        shadowRay.direction = lights[i].position - P;

        struct Hit shadowHit = closestIntersection(
            shadowRay,
            0.001f,
            1024.0f,
            spheres,
            sphereLength,
            1024.0f
        );

        if(shadowHit.isHit) {
            continue;
        }

        float3 L = normalize(lights[i].position - P);
        float3 H = normalize(L + V);


        float ndotl = dot(N, L);
        float ndoth = dot(N, H);

        float3 diffuse = hit.sphere.color * lights[i].color * ndotl;
        float3 specular = lights[i].color * pow(ndoth, specularFactor * 256.0f);

        light += (diffuse + specular) * lights[i].intencity;
    }

    light += (float3)(0.1, 0.1, 0.1) * hit.sphere.color;

    // Iteration1
    if(specularFactor <= 0) {
        struct Color temp;
        temp.r = light.x;
        temp.g = light.y;
        temp.b = light.z;
        return temp;
    }

    // Ambient
    float3 R = reflect(-ray.direction, N);

    struct Ray reflectRay;
    reflectRay.position = P;
    reflectRay.direction = R;

    // Redo the scene...
    struct ReflectHit iteration1 = computeAmbient(
        reflectRay,
        0.1f,
        1024.0f,
        spheres,
        sphereLength,
        lights,
        lightLength,
        specularFactor,
        clearColor
    );

    struct ReflectHit finalIteration = computeAmbient(
        iteration1.nextRay,
        0.1f,
        1024.0f,
        spheres,
        sphereLength,
        lights,
        lightLength,
        specularFactor,
        clearColor
    );

    float3 ambient = (iteration1.color + finalIteration.color) * 0.5f;

    struct Color temp;
    temp.r = light.x * (specularFactor) + ambient.x * (1.0 - specularFactor);
    temp.g = light.y * (specularFactor) + ambient.y * (1.0 - specularFactor);
    temp.b = light.z * (specularFactor) + ambient.z * (1.0 - specularFactor);
    return temp;
}

struct Color raytracer(
    struct Ray ray, 
    float zmin, 
    float zmax, 
    struct Color clearColor, 
    __global struct Sphere* s, 
    uint sphereLength,
    __global struct Light* l,
    uint lightLength) 
{
    struct Hit hit = closestIntersection(
        ray, 
        zmin, 
        zmax, 
        s, 
        sphereLength, 
        zmax);

    if(!hit.isHit) {
        return clearColor;
    }

    // Lighting
    float3 P = ray.position + ray.direction * hit.t;
    float3 N = P - hit.sphere.position;
    N = normalize(N);

    struct Color temp = computeLighting(
        ray,
        P,
        N,
        -ray.direction,
        hit,
        l,
        lightLength,
        s,
        sphereLength,
        0.5,
        (float3)(clearColor.r, clearColor.g, clearColor.b)
    );

    return temp;
}

__kernel void renderer(
    __global struct Color* framebuffer,
    __global struct Sphere* spheres,
    uint sphereLength,
    __global struct Light* lights,
    uint lightLength,
    struct Camera camera,
    struct Color clearColor
) {
    uint x = get_global_id(0);
    uint y = get_global_id(1);

    uint width = get_global_size(0);
    uint height = get_global_size(1);

    float2 sc;
    sc.x = convert_float(x * 2) / width - 1.0;
    sc.y = convert_float(y * 2) / height - 1.0;

    struct Ray ray = camera_makeRay(sc, camera);

    struct Color color = raytracer(
        ray, 
        0.1f, 
        1024.0f, 
        clearColor, 
        spheres, 
        sphereLength,
        lights,
        lightLength);

    
    framebuffer[y * width + x].r = clamp(color.r, 0.0f, 1.0f);
    framebuffer[y * width + x].g = clamp(color.g, 0.0f, 1.0f);
    framebuffer[y * width + x].b = clamp(color.b, 0.0f, 1.0f);
}

__kernel void present(
    __global struct SDL_Color* screen,
    __global struct Color* framebuffer
) {
    uint x = get_global_id(0);
    uint y = get_global_id(1);

    uint width = get_global_size(0);

    screen[y * width + x].r = convert_uchar(framebuffer[y * width + x].b * 255);
    screen[y * width + x].g = convert_uchar(framebuffer[y * width + x].g * 255);
    screen[y * width + x].b = convert_uchar(framebuffer[y * width + x].r* 255);
    screen[y * width + x].a = 255;
}