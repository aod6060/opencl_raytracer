#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include <SDL.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp>

#define CL_USE_DEPRECATED_OPENCL_2_0_APIS
#include <CL/cl.h>

int screen_width = 1280;
int screen_height = 720;
bool running = true;

SDL_Window* window = nullptr;

void app_init();
void app_update(float delta);
void app_render();
void app_release();

glm::vec3 toVec(cl_float3& v) {
	return glm::vec3(v.x, v.y, v.z);
}

void setFloat3(cl_float3& out, glm::vec3 v) {
	out.x = v.x;
	out.y = v.y;
	out.z = v.z;
}

int main(int argc, char** argv) {

	SDL_Init(SDL_INIT_EVERYTHING);

	window = SDL_CreateWindow(
		"OpenCL Raytracer", 
		SDL_WINDOWPOS_UNDEFINED, 
		SDL_WINDOWPOS_UNDEFINED, 
		screen_width, 
		screen_height, 
		SDL_WINDOW_SHOWN);

	SDL_Event e;

	
	app_init();

	uint32_t pre = SDL_GetTicks();
	uint32_t curr = 0;
	float delta = 0.0f;

	// create images
	while (running) {
		curr = SDL_GetTicks();
		delta = (curr - pre) / 1000.0f;
		pre = curr;

		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) {
				running = false;
			}
		}

		app_update(delta);
		app_render();

		SDL_UpdateWindowSurface(window);
	}


	app_release();

	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}

struct Color {
	float r;
	float g;
	float b;
};

struct Sphere {
	cl_float3 position;
	cl_float radius;
	cl_float3 color;

	Sphere(glm::vec3 position, float radius, glm::vec3 color) {
		this->position.x = position.x;
		this->position.y = position.y;
		this->position.z = position.z;

		this->radius = radius;

		this->color.x = color.r;
		this->color.y = color.g;
		this->color.z = color.b;
	}
};

std::vector<Sphere> spheres = {
	Sphere(glm::vec3(-8, 0, 0), 1, glm::vec3(0.5f, 0.5f, 0.5f)),
	Sphere(glm::vec3(0, 0, -8), 1, glm::vec3(0.0f, 0.5f, 0.0f)),
	Sphere(glm::vec3(8, 0, 0), 1, glm::vec3(0.0f, 0.0f, 0.5f)),
	Sphere(glm::vec3(0, 0, 8), 1, glm::vec3(0.5f, 0.0f, 0.0f)),
	Sphere(glm::vec3(0, -5001, 0), 5000, glm::vec3(1.0f, 1.0f, 0.0f))
};

struct Camera {
	cl_float3 pos;
	cl_float3 forward;
	cl_float3 right;
	cl_float3 up;
	cl_float width;
	cl_float height;

} camera;

struct Light {
	cl_float3 position;
	float intencity;
	cl_float3 color;

	Light() {}
	Light(glm::vec3 position, float intencity, glm::vec3 color) {
		setFloat3(this->position, position);
		this->intencity = intencity;
		setFloat3(this->color, color);
	}
};

std::vector<Light> lights = {
	Light(glm::vec3(0.0f, 1.0f, 0.0f), 0.6f, glm::vec3(1.0f, 1.0f, 1.0f))
};

void camera_setup();
void camera_update(float delta);

/*
std::vector<Light> lights = {
	Light(0.6, glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0, 1, 0))
};
*/

cl_platform_id platform;
cl_device_id device;
cl_context context;
cl_command_queue commands;

cl_program program;
// Kernels
cl_kernel rendererKernel;
cl_kernel presentKernel;
// Buffers
cl_mem framebuffer;
cl_mem spheresMem;
cl_mem lightsMem;
cl_mem screen;

// 
void app_init() {
	//fb_init();
	camera_setup();

	cl_uint length;
	cl_int err;

	std::vector<cl_platform_id> platforms;

	clGetPlatformIDs(0, 0, &length);
	platforms.resize(length);
	clGetPlatformIDs(platforms.size(), platforms.data(), 0);

	for (int i = 0; i < platforms.size(); i++) {
		std::vector<cl_device_id> devices;

		clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, 0, 0, &length);
		devices.resize(length);
		clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_GPU, devices.size(), devices.data(), 0);

		if (devices.size() > 0) {
			device = devices[0];
			platform = platforms[i];
			break;
		}
	}

	context = clCreateContext(0, 1, &device, nullptr, nullptr, &err);

	if (!context) {
		std::cout << "Context wasn't create" << std::endl;
		running = false;
		exit(1);
	}

	commands = clCreateCommandQueue(context, device, 0, &err);

	if (!commands) {
		std::cout << "Commands wasn't create" << std::endl;
		running = false;
		exit(1);
	}

	std::ifstream in("data/kernel/raytracer.cl");
	std::stringstream ss;
	std::string temp;

	while (std::getline(in, temp)) {
		ss << temp << std::endl;
	}

	in.close();

	std::string src = ss.str();
	const char* c_src = src.c_str();

	std::cout << c_src << std::endl;

	program = clCreateProgramWithSource(context, 1, &c_src, nullptr, &err);

	if (!program) {
		std::cout << "Program wasn't create" << std::endl;
		running = false;
		exit(1);
	}

	err = clBuildProgram(program, 0, nullptr, nullptr, nullptr, nullptr);

	if (err != CL_SUCCESS) {
		char buf[2048];
		std::cout << "Error: Failed to build program" << std::endl;
		clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(buf), buf, &length);
		std::cout << buf << std::endl;
		std::getchar();
		running = false;
		exit(1);
	}

	// Kernels
	rendererKernel = clCreateKernel(program, "renderer", &err);

	if (!rendererKernel) {
		std::cout << "rendererKernel wasn't create" << std::endl;
		running = false;
		std::getchar();
		exit(1);
	}

	presentKernel = clCreateKernel(program, "present", &err);

	if (!presentKernel) {
		std::cout << "presentKernel wasn't create" << std::endl;
		running = false;
		std::getchar();
		exit(1);
	}

	// Create Buffers
	cl_uint size = screen_width * screen_height;

	// framebuffer
	framebuffer = clCreateBuffer(context, CL_MEM_READ_WRITE, size * sizeof(Color), nullptr, &err);

	if (!framebuffer) {
		std::cout << "framebuffer buffer wasn't create" << std::endl;
		running = false;
		std::getchar();
		exit(1);
	}

	// Spheres
	spheresMem = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, spheres.size() * sizeof(Sphere), spheres.data(), &err);
	if (!spheresMem) {
		std::cout << "spheresMem buffer wasn't create" << std::endl;
		running = false;
		std::getchar();
		exit(1);
	}

	// Lights
	lightsMem = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, lights.size() * sizeof(Light), lights.data(), &err);
	if (!lightsMem) {
		std::cout << "lightsMem buffer wasn't create" << std::endl;
		running = false;
		std::getchar();
		exit(1);
	}

	// screen
	screen = clCreateBuffer(context, CL_MEM_READ_WRITE, size * sizeof(SDL_Color), nullptr, &err);

	if (!screen) {
		std::cout << "screen buffer wasn't create" << std::endl;
		running = false;
		std::getchar();
		exit(1);
	}
}
void app_update(float delta) {
	camera_update(delta);
}

void app_render() {
	cl_int err;
	// Render
	Color clearColor;
	clearColor.r = 0.0f;
	clearColor.g = 0.0f;
	clearColor.b = 0.0f;

	size_t globalWorkSize[2] = {
		screen_width,
		screen_height
	};

	size_t localWorkSize[2] = {
		16, 16
	};

	size_t len = spheres.size();
	size_t len2 = lights.size();

	err = clSetKernelArg(rendererKernel, 0, sizeof(cl_mem), (void*)&framebuffer);
	err |= clSetKernelArg(rendererKernel, 1, sizeof(cl_mem), (void*)&spheresMem);
	err |= clSetKernelArg(rendererKernel, 2, sizeof(size_t), (void*)&len);
	err |= clSetKernelArg(rendererKernel, 3, sizeof(cl_mem), (void*)&lightsMem);
	err |= clSetKernelArg(rendererKernel, 4, sizeof(size_t), (void*)&len2);
	err |= clSetKernelArg(rendererKernel, 5, sizeof(Camera), (void*)&camera);
	err |= clSetKernelArg(rendererKernel, 6, sizeof(Color), (void*)&clearColor);

	if(err != CL_SUCCESS) {
		std::cout << "Failed to set rendererKernel Arguments" << std::endl;
		return;
	}

	err = clEnqueueNDRangeKernel(commands, rendererKernel, 2, nullptr, globalWorkSize, localWorkSize, 0, nullptr, nullptr);

	if (err != CL_SUCCESS) {
		std::cout << "Failed to submit range kernel for rendererKernel" << std::endl;
		return;
	}

	err = clFinish(commands);

	if (err != CL_SUCCESS) {
		std::cout << "Failed to synchronize using clFinish function" << std::endl;
		return;
	}

	// Present

	err = clSetKernelArg(presentKernel, 0, sizeof(cl_mem), (void*)&screen);
	err |= clSetKernelArg(presentKernel, 1, sizeof(cl_mem), (void*)&framebuffer);

	if (err != CL_SUCCESS) {
		std::cout << "Failed to set presentKernal Arguments" << std::endl;
		return;
	}
	
	err = clEnqueueNDRangeKernel(commands, presentKernel, 2, nullptr, globalWorkSize, localWorkSize, 0, nullptr, nullptr);

	if (err != CL_SUCCESS) {
		std::cout << "Failed to call presentKernel" << std::endl;
		return;
	}

	SDL_Surface* winScreen = SDL_GetWindowSurface(window);
	SDL_LockSurface(winScreen);
	SDL_Color* screenColors = (SDL_Color*)winScreen->pixels;
	cl_uint size = screen_width * screen_height;
	err = clEnqueueReadBuffer(commands, screen, CL_TRUE, 0, size * sizeof(SDL_Color), screenColors, 0, nullptr, nullptr);
	if (err != CL_SUCCESS) {
		std::cout << "Failed to read screen buffer" << std::endl;
	}
	// Read screen buffer
	SDL_UnlockSurface(winScreen);
}

void app_release() {
	clReleaseMemObject(lightsMem);
	clReleaseMemObject(spheresMem);
	clReleaseMemObject(screen);
	clReleaseMemObject(framebuffer);
	clReleaseKernel(presentKernel);
	clReleaseKernel(rendererKernel);
	clReleaseProgram(program);
	clReleaseCommandQueue(commands);
	clReleaseContext(context);
}



float yaw = 0.0f;
float pitch = 0.0f;

void camera_setup() {
	float fov = 60.0f;
	float aspect = (float)screen_width / (float)screen_height;

	setFloat3(camera.pos, glm::vec3(0.0f));
	
	setFloat3(camera.forward, glm::normalize(glm::vec3(0.0f, 0.0f, -1.0f)));
	setFloat3(camera.right, glm::normalize(glm::cross(toVec(camera.forward), glm::vec3(0.0f, 1.0f, 0.0f))));
	setFloat3(camera.up, glm::cross(toVec(camera.forward), toVec(camera.right)));

	camera.height = tan(fov);
	camera.width = camera.height * aspect;
}

void camera_update(float delta) {
	const uint8_t* keys = SDL_GetKeyboardState(nullptr);

	float rotSpeed = 64.0f;

	if (keys[SDL_SCANCODE_LEFT]) {
		yaw -= rotSpeed * delta;
	}

	if (keys[SDL_SCANCODE_RIGHT]) {
		yaw += rotSpeed * delta;
	}

	if (yaw < -360.0f) {
		yaw += 360.0f;
	}

	if (yaw > 360.0f) {
		yaw -= 360.0f;
	}

	if (keys[SDL_SCANCODE_UP]) {
		pitch += rotSpeed * delta;
	}

	if (keys[SDL_SCANCODE_DOWN]) {
		pitch -= rotSpeed * delta;
	}


	if (pitch < -90.0f) {
		pitch = -90.0f;
	}

	if (pitch > 90.0f) {
		pitch = 90.0f;
	}

	glm::vec3 direction = glm::vec3(
		glm::cos(glm::radians(yaw)) * glm::cos(glm::radians(pitch)),
		glm::sin(glm::radians(pitch)),
		glm::sin(glm::radians(yaw)) * glm::cos(glm::radians(pitch))
	);

	setFloat3(camera.forward, glm::normalize(direction));
	setFloat3(camera.right, glm::normalize(glm::cross(toVec(camera.forward), glm::vec3(0.0f, 1.0f, 0.0f))));
	setFloat3(camera.up, glm::cross(toVec(camera.forward), toVec(camera.right)));

	glm::vec3 forward = glm::vec3(
		camera.forward.x,
		0.0f,
		camera.forward.z);

	float speed = 4.0f;

	if (keys[SDL_SCANCODE_W]) {
		camera.pos.x += speed * delta * forward.x;
		camera.pos.z += speed * delta * forward.z;
	}

	if (keys[SDL_SCANCODE_S]) {
		camera.pos.x -= speed * delta * forward.x;
		camera.pos.z -= speed * delta * forward.z;
	}

	if (keys[SDL_SCANCODE_A]) {
		camera.pos.x -= speed * delta * camera.right.x;
		camera.pos.z -= speed * delta * camera.right.z;
	}

	if (keys[SDL_SCANCODE_D]) {
		camera.pos.x += speed * delta * camera.right.x;
		camera.pos.z += speed * delta * camera.right.z;
	}

	if (keys[SDL_SCANCODE_SPACE]) {
		camera.pos.y += speed * delta;
	}

	if (keys[SDL_SCANCODE_LSHIFT]) {
		camera.pos.y -= speed * delta;
	}
}