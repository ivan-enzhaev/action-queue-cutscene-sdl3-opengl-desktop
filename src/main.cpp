#define SDL_MAIN_USE_CALLBACKS 1
#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <cglm/cglm.h>

#include <vector>
#include <deque>
#include <memory>
#include <cmath>
#include <algorithm>

// ============================================================================
// Action / Command Queue System
// ============================================================================

enum class ActionStatus {
    Running,
    Finished
};

class Action {
public:
    virtual ~Action() = default;
    virtual void on_start() {}
    virtual ActionStatus update(float dt) = 0;
};

class ActionQueue {
public:
    void push(std::unique_ptr<Action> action) {
        queue.push_back(std::move(action));
    }

    void update(float dt) {
        if (queue.empty()) return;

        if (!started) {
            queue.front()->on_start();
            started = true;
        }

        if (queue.front()->update(dt) == ActionStatus::Finished) {
            queue.pop_front();
            started = false; // Next action will call on_start() on next frame
        }
    }

    bool is_empty() const { return queue.empty(); }
    void clear() { queue.clear(); started = false; }

private:
    std::deque<std::unique_ptr<Action>> queue;
    bool started = false;
};

// ============================================================================
// Application & Render State
// ============================================================================

struct AppState {
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;

    GLuint shaderProgram = 0;
    GLuint VAO = 0;
    GLuint VBO = 0;
    GLint uMVPLoc = -1;

    // Transformation variables modified by our Action Queue
    float rotationAngle = 0.0f; // in radians
    float scale = 1.0f;
    float clearColor[3] = { 0.12f, 0.12f, 0.15f };

    uint64_t lastTicks = 0;
    ActionQueue actionQueue;
};

// ============================================================================
// Concrete Cutscene Actions
// ============================================================================

class RotateAction : public Action {
public:
    RotateAction(AppState* state, float targetAngleRad, float duration)
        : state(state), target(targetAngleRad), duration(duration) {}

    void on_start() override {
        startAngle = state->rotationAngle;
        elapsed = 0.0f;
    }

    ActionStatus update(float dt) override {
        elapsed += dt;
        float t = (duration > 0.0f) ? std::min(1.0f, elapsed / duration) : 1.0f;
        state->rotationAngle = startAngle + (target - startAngle) * t;
        return (t >= 1.0f) ? ActionStatus::Finished : ActionStatus::Running;
    }

private:
    AppState* state;
    float startAngle = 0.0f;
    float target;
    float duration;
    float elapsed = 0.0f;
};

class PulseAction : public Action {
public:
    PulseAction(AppState* state, float maxScale, float duration)
        : state(state), maxScale(maxScale), duration(duration) {}

    ActionStatus update(float dt) override {
        elapsed += dt;
        float t = std::min(1.0f, elapsed / duration);
        state->scale = 1.0f + (maxScale - 1.0f) * std::sin(t * 3.14159265f);
        return (t >= 1.0f) ? ActionStatus::Finished : ActionStatus::Running;
    }

private:
    AppState* state;
    float maxScale;
    float duration;
    float elapsed = 0.0f;
};

class FadeColorAction : public Action {
public:
    FadeColorAction(AppState* state, float r, float g, float b, float duration)
        : state(state), targetR(r), targetG(g), targetB(b), duration(duration) {}

    void on_start() override {
        startR = state->clearColor[0];
        startG = state->clearColor[1];
        startB = state->clearColor[2];
        elapsed = 0.0f;
    }

    ActionStatus update(float dt) override {
        elapsed += dt;
        float t = (duration > 0.0f) ? std::min(1.0f, elapsed / duration) : 1.0f;
        state->clearColor[0] = startR + (targetR - startR) * t;
        state->clearColor[1] = startG + (targetG - startG) * t;
        state->clearColor[2] = startB + (targetB - startB) * t;
        return (t >= 1.0f) ? ActionStatus::Finished : ActionStatus::Running;
    }

private:
    AppState* state;
    float startR = 0, startG = 0, startB = 0;
    float targetR, targetG, targetB;
    float duration;
    float elapsed = 0.0f;
};

static void setupCutscene(AppState* state) {
    // Step 1: Rotate 360 degrees (2*PI) in 1.5 seconds
    state->actionQueue.push(std::make_unique<RotateAction>(state, 6.2831853f, 1.5f));

    // Step 2: Pulse scale up to 1.8x and back in 1.0 second
    state->actionQueue.push(std::make_unique<PulseAction>(state, 1.8f, 1.0f));

    // Step 3: Shift background to vibrant purple over 1.0 second
    state->actionQueue.push(std::make_unique<FadeColorAction>(state, 0.35f, 0.10f, 0.45f, 1.0f));

    // Step 4: Rotate back to 0 in 1.2 seconds
    state->actionQueue.push(std::make_unique<RotateAction>(state, 0.0f, 1.2f));

    // Step 5: Shift background back to dark slate in 1.0 second
    state->actionQueue.push(std::make_unique<FadeColorAction>(state, 0.12f, 0.12f, 0.15f, 1.0f));
}

// ============================================================================
// OpenGL Shaders
// ============================================================================

const char* vertexShaderSource = 
    "#version 330 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec3 aColor;\n"
    "uniform mat4 uMVP;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 0.0, 1.0);\n"
    "    vColor = aColor;\n"
    "}\0";

const char* fragmentShaderSource = 
    "#version 330 core\n"
    "in vec3 vColor;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    FragColor = vec4(vColor, 1.0);\n"
    "}\0";

static GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
}

// ============================================================================
// SDL3 Callbacks
// ============================================================================

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* state = new AppState();
    *appstate = state;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    state->window = SDL_CreateWindow(
        "Action Queue Cutscene Demo (SDL3 Callbacks + cglm)",
        800, 600,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    if (!state->window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    state->glContext = SDL_GL_CreateContext(state->window);
    if (!state->glContext) {
        SDL_Log("Failed to create OpenGL context: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_GL_MakeCurrent(state->window, state->glContext);
    SDL_GL_SetSwapInterval(1);

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        SDL_Log("Failed to initialize GLAD");
        return SDL_APP_FAILURE;
    }

    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    state->shaderProgram = glCreateProgram();
    glAttachShader(state->shaderProgram, vs);
    glAttachShader(state->shaderProgram, fs);
    glLinkProgram(state->shaderProgram);

    glDeleteShader(vs);
    glDeleteShader(fs);

    state->uMVPLoc = glGetUniformLocation(state->shaderProgram, "uMVP");

    float vertices[] = {
         0.0f,  0.5f,   1.0f, 0.2f, 0.2f, // top (red)
        -0.5f, -0.5f,   0.2f, 1.0f, 0.2f, // bottom-left (green)
         0.5f, -0.5f,   0.2f, 0.4f, 1.0f  // bottom-right (blue)
    };

    glGenVertexArrays(1, &state->VAO);
    glGenBuffers(1, &state->VBO);

    glBindVertexArray(state->VAO);
    glBindBuffer(GL_ARRAY_BUFFER, state->VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    state->lastTicks = SDL_GetTicksNS();
    setupCutscene(state);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* state = static_cast<AppState*>(appstate);

    uint64_t currentTicks = SDL_GetTicksNS();
    float dt = static_cast<float>(currentTicks - state->lastTicks) / 1e9f;
    state->lastTicks = currentTicks;

    // 1. Advance the Action Queue
    state->actionQueue.update(dt);

    // If the sequence finished, loop it again
    if (state->actionQueue.is_empty()) {
        setupCutscene(state);
    }

    // 2. Render Frame
    int w, h;
    SDL_GetWindowSizeInPixels(state->window, &w, &h);
    glViewport(0, 0, w, h);

    glClearColor(state->clearColor[0], state->clearColor[1], state->clearColor[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(state->shaderProgram);

    // 3. Matrix Math using cglm
    float aspect = (h > 0) ? static_cast<float>(w) / static_cast<float>(h) : 1.0f;

    mat4 proj, view, model, vp, mvp;

    // Orthographic projection preserving aspect ratio
    glm_ortho(-aspect, aspect, -1.0f, 1.0f, -1.0f, 1.0f, proj);

    // Standard view matrix (camera looking down -Z axis)
    vec3 eye = {0.0f, 0.0f, 1.0f};
    vec3 center = {0.0f, 0.0f, 0.0f};
    vec3 up = {0.0f, 1.0f, 0.0f};
    glm_lookat(eye, center, up, view);

    // Model transformation: rotation around Z axis + uniform scale
    glm_mat4_identity(model);
    vec3 axisZ = {0.0f, 0.0f, 1.0f};
    glm_rotate(model, state->rotationAngle, axisZ);
    glm_scale_uni(model, state->scale);

    // Multiply: MVP = Projection * View * Model
    glm_mat4_mul(proj, view, vp);
    glm_mat4_mul(vp, model, mvp);

    // Send the combined MVP matrix to the shader uniform
    glUniformMatrix4fv(state->uMVPLoc, 1, GL_FALSE, (const GLfloat*)mvp);

    glBindVertexArray(state->VAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    SDL_GL_SwapWindow(state->window);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    if (appstate) {
        auto* state = static_cast<AppState*>(appstate);
        if (state->VAO) glDeleteVertexArrays(1, &state->VAO);
        if (state->VBO) glDeleteBuffers(1, &state->VBO);
        if (state->shaderProgram) glDeleteProgram(state->shaderProgram);
        if (state->glContext) SDL_GL_DestroyContext(state->glContext);
        if (state->window) SDL_DestroyWindow(state->window);
        delete state;
    }
    SDL_Quit();
}
