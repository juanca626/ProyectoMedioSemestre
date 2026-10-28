#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <cmath>
#ifdef USE_IMGUI
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#endif

// estructura de datos del modelo
enum RenderMode{
    MODE_POINTS = 0,
    MODE_LINES = 1,
    MODE_SURFACE = 2
};

struct Model{
    std::string name;
    std::vector<glm::vec3> vertices;
    std::vector<unsigned int> indices;
    glm::vec3 minBounds;
    glm::vec3 maxBounds;
    glm::vec3 center;
    float normalizeScale;

    GLuint VAO = 0, VBO = 0, EBO = 0;
    bool isMathPlane = false;
};

// estado y camara
std::vector<Model> gModels;
int gCurrentModelIdx = 0;
int gRenderMode = MODE_SURFACE;

// trasnformaciones del modelo
glm::vec3 gRotation(0.0f, 0.0f, 0.0f);
bool gAutoRotateX = false;
bool gAutoRotateY = false;
bool gAutoRotateZ = false;
float gRotationSpeed = 30.0f; // grados por segundo, cambiarlo a 60 tmb sirve
float gScale = 1.0f;
bool gAutoNormalize = true;   // Se limita la camara

// camara orbital
float gCamYaw = 45.0f;
float gCamPitch = 25.0f;
float gCamRadius = 4.5f;
bool gMouseLeftDown = false;
double gLastMouseX = 0.0, gLastMouseY = 0.0;

// funciones para carga y descarga de shaders
std::string readFile(const std::string& path){
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[error] no se pudo abrir el archivo: " << path << std::endl;
        return "";
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

GLuint compileShader(GLenum type, const char* source){
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success){
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cerr << "[error] fallo en compilar el shader: \n" << infoLog << std::endl;
    }
    return shader;
}

GLuint createShaderProgram(const std::string& vertPath, const std::string& fragPath){
    std::string vertCode = readFile(vertPath);
    std::string fragCode = readFile(fragPath);

    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertCode.c_str());
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragCode.c_str());

    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);

    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success){
        char infoLog[512];
        glGetProgramInfoLog(program, 512, NULL, infoLog);
        std::cerr << "[ERROR] Fallo al enlazar el programa de shaders:\n" << infoLog << std::endl;
    }

    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    return program;
}

// caja dedimitadora y shaders
void setupModelBuffersAndBounds(Model& model){
    if (model.vertices.empty()) return;

    model.minBounds = model.vertices[0];
    model.maxBounds = model.vertices[0];

    for (int i = 0; i < (int)model.vertices.size(); i++){
        glm::vec3 v = model.vertices[i];
        if (v.x < model.minBounds.x) model.minBounds.x = v.x;
        if (v.y < model.minBounds.y) model.minBounds.y = v.y;
        if (v.z < model.minBounds.z) model.minBounds.z = v.z;

        if (v.x > model.maxBounds.x) model.maxBounds.x = v.x;
        if (v.y > model.maxBounds.y) model.maxBounds.y = v.y;
        if (v.z > model.maxBounds.z) model.maxBounds.z = v.z;
    }

    model.center = (model.minBounds + model.maxBounds) * 0.5f;
    glm::vec3 size = model.maxBounds - model.minBounds;
    float maxDim = size.x;
    if (size.y > maxDim) maxDim = size.y;
    if (size.z > maxDim) maxDim = size.z;

    if (maxDim > 0.0001f){
        model.normalizeScale = 2.5f / maxDim;
    } else{
        model.normalizeScale = 1.0f;
    }

    if (model.indices.empty()){
        for (unsigned int i = 0; i < model.vertices.size(); i++){
            model.indices.push_back(i);
        }
    }

    glGenVertexArrays(1, &model.VAO);
    glGenBuffers(1, &model.VBO);
    glGenBuffers(1, &model.EBO);

    glBindVertexArray(model.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, model.VBO);
    glBufferData(GL_ARRAY_BUFFER, model.vertices.size() * sizeof(glm::vec3), &model.vertices[0], GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, model.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(unsigned int), &model.indices[0], GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

// lector de archivos
bool loadOBJ(const std::string& path, const std::string& displayName, Model& outModel) {
    std::ifstream file(path);
    if (!file.is_open()){
        std::cerr << "[ADVERTENCIA] No se pudo abrir el archivo OBJ: " << path << std::endl;
        return false;
    }

    outModel.name = displayName;
    outModel.vertices.clear();
    outModel.indices.clear();
    outModel.isMathPlane = false;

    std::string line;
    while (std::getline(file, line)){
        if (line.empty() || line[0] == '#') continue;

        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix;

        if (prefix == "v"){
            glm::vec3 v;
            ss >> v.x >> v.y >> v.z;
            outModel.vertices.push_back(v);
        } else if (prefix == "f"){
            std::vector<unsigned int> faceIndices;
            std::string vertexToken;
            while (ss >> vertexToken){
                size_t slashPos = vertexToken.find('/');
                std::string vIdxStr = vertexToken;
                if (slashPos != std::string::npos){
                    vIdxStr = vertexToken.substr(0, slashPos);
                }
                int idx = std::stoi(vIdxStr);
                unsigned int zeroBasedIdx = idx - 1;
                faceIndices.push_back(zeroBasedIdx);
            }
            for (int i = 1; i + 1 < (int)faceIndices.size(); i++){
                outModel.indices.push_back(faceIndices[0]);
                outModel.indices.push_back(faceIndices[i]);
                outModel.indices.push_back(faceIndices[i + 1]);
            }
        }
    }

    file.close();
    if (outModel.vertices.empty()) return false;

    setupModelBuffersAndBounds(outModel);
    return true;
}

// plano matematicos
Model createMathPlaneModel(){
    Model plane;
    plane.name = "plano 100x100: f(x,z) = cos(x) + sin(z)";
    plane.isMathPlane = true;

    int N = 100;
    float minDom = -2.0f * 3.14159265f;
    float maxDom =  2.0f * 3.14159265f;
    float step = (maxDom - minDom) / (float)(N - 1);

    for (int i = 0; i < N; i++){
        float z = minDom + i * step;
        for (int j = 0; j < N; j++){
            float x = minDom + j * step;
            float y = cos(x) + sin(z);
            plane.vertices.push_back(glm::vec3(x, y, z));
        }
    }

    for (int i = 0; i < N - 1; i++) {
        for (int j = 0; j < N - 1; j++) {
            unsigned int topLeft     = i * N + j;
            unsigned int topRight    = topLeft + 1;
            unsigned int bottomLeft  = (i + 1) * N + j;
            unsigned int bottomRight = bottomLeft + 1;

            plane.indices.push_back(topLeft);
            plane.indices.push_back(bottomLeft);
            plane.indices.push_back(topRight);

            plane.indices.push_back(topRight);
            plane.indices.push_back(bottomLeft);
            plane.indices.push_back(bottomRight);
        }
    }

    setupModelBuffersAndBounds(plane);
    return plane;
}

// imprimr enterminal
void printCurrentModelInfo(){
    if (gModels.empty()) return;
    Model m = gModels[gCurrentModelIdx];

    std::cout << " Modelo: [" << (gCurrentModelIdx + 1) << "/" << gModels.size() << "]: " << m.name << "\n";
    std::cout << " Vertices: " << m.vertices.size() << "\n";
    std::cout << " Minimo: (" << m.minBounds.x << ", " << m.minBounds.y << ", " << m.minBounds.z << ")\n";
    std::cout << " Maximo: (" << m.maxBounds.x << ", " << m.maxBounds.y << ", " << m.maxBounds.z << ")\n";
    if (m.isMathPlane){
        std::cout << " -> Minimo de f(x,z): " << m.minBounds.y << "\n";
        std::cout << " -> Maximo de f(x,z): " << m.maxBounds.y << "\n";
    }
}

void selectModel(int index){
    if (gModels.empty()) return;
    if (index < 0) index = (int)gModels.size() - 1;
    if (index >= (int)gModels.size()) index = 0;
    gCurrentModelIdx = index;
    printCurrentModelInfo();
}

// ventana teclaod y raton
void framebuffer_size_callback(GLFWwindow* window, int width, int height){
    glViewport(0, 0, width, height);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods){
#ifdef USE_IMGUI
    if (ImGui::GetIO().WantCaptureKeyboard) return;
#endif
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
        switch (key){
            case GLFW_KEY_ESCAPE:
                glfwSetWindowShouldClose(window, true);
                break;
            case GLFW_KEY_RIGHT:
            case GLFW_KEY_N:
                if (action == GLFW_PRESS) selectModel(gCurrentModelIdx + 1);
                break;
            case GLFW_KEY_LEFT:
            case GLFW_KEY_P:
                if (action == GLFW_PRESS) selectModel(gCurrentModelIdx - 1);
                break;
            case GLFW_KEY_1:
                gRenderMode = MODE_POINTS;
                break;
            case GLFW_KEY_2:
                gRenderMode = MODE_LINES;
                break;
            case GLFW_KEY_3:
                gRenderMode = MODE_SURFACE;
                break;
            case GLFW_KEY_X:
                if (action == GLFW_PRESS) gAutoRotateX = !gAutoRotateX;
                break;
            case GLFW_KEY_Y:
                if (action == GLFW_PRESS) gAutoRotateY = !gAutoRotateY;
                break;
            case GLFW_KEY_Z:
                if (action == GLFW_PRESS) gAutoRotateZ = !gAutoRotateZ;
                break;
            case GLFW_KEY_W: gRotation.x += 5.0f; break;
            case GLFW_KEY_S: gRotation.x -= 5.0f; break;
            case GLFW_KEY_D: gRotation.y += 5.0f; break;
            case GLFW_KEY_A: gRotation.y -= 5.0f; break;
            case GLFW_KEY_E: gRotation.z += 5.0f; break;
            case GLFW_KEY_Q: gRotation.z -= 5.0f; break;
            case GLFW_KEY_R:
                gRotation = glm::vec3(0.0f);
                gScale = 1.0f;
                gAutoRotateX = gAutoRotateY = gAutoRotateZ = false;
                break;
        }
    }
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods){
#ifdef USE_IMGUI
    if (ImGui::GetIO().WantCaptureMouse) return;
#endif
    if (button == GLFW_MOUSE_BUTTON_LEFT){
        if (action == GLFW_PRESS) {
            gMouseLeftDown = true;
            glfwGetCursorPos(window, &gLastMouseX, &gLastMouseY);
        } else if (action == GLFW_RELEASE){
            gMouseLeftDown = false;
        }
    }
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos){
#ifdef USE_IMGUI
    if (ImGui::GetIO().WantCaptureMouse) return;
#endif
    if (gMouseLeftDown){
        float dx = (float)(xpos - gLastMouseX);
        float dy = (float)(ypos - gLastMouseY);
        gLastMouseX = xpos;
        gLastMouseY = ypos;

        gCamYaw   += dx * 0.4f;
        gCamPitch += dy * 0.4f;
        if (gCamPitch >  89.0f) gCamPitch =  89.0f;
        if (gCamPitch < -89.0f) gCamPitch = -89.0f;
    }
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset){
#ifdef USE_IMGUI
    if (ImGui::GetIO().WantCaptureMouse) return;
#endif
    gCamRadius -= (float)yoffset * 0.35f;
    if (gCamRadius < 0.5f)  gCamRadius = 0.5f;
    if (gCamRadius > 30.0f) gCamRadius = 30.0f;
}

// ejes cordenados xyz
void createAxesVAO(GLuint& axisVAO, GLuint& axisVBO){
    float axisLength = 3.0f;
    float axesVertices[] = {
        -axisLength, 0.0f, 0.0f,   0.4f, 0.1f, 0.1f,
         axisLength, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,
         0.0f, -axisLength, 0.0f,  0.1f, 0.4f, 0.1f,
         0.0f,  axisLength, 0.0f,  0.0f, 1.0f, 0.0f,
         0.0f, 0.0f, -axisLength,  0.1f, 0.1f, 0.4f,
         0.0f, 0.0f,  axisLength,  0.2f, 0.5f, 1.0f
    };

    glGenVertexArrays(1, &axisVAO);
    glGenBuffers(1, &axisVBO);

    glBindVertexArray(axisVAO);
    glBindBuffer(GL_ARRAY_BUFFER, axisVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(axesVertices), axesVertices, GL_STATIC_DRAW);

    // 0 posicion
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // 1 eje
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
}

// funcion Main
int main(int argc, char** argv) {
    if (!glfwInit()) {
        std::cerr << "[error] Fallo con GLFW" << std::endl;
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 800, "Visualizador", NULL, NULL);
    if (!window){
        std::cerr << "[error] No se abrio la ventana GLFW" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetCursorPosCallback(window, cursor_position_callback);
    glfwSetScrollCallback(window, scroll_callback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "[ERROR] Fallo al inicializar GLEW." << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);
    glPointSize(4.0f);

#ifdef USE_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");
#endif

    GLuint shaderProgram = createShaderProgram("shaders/vertex.glsl", "shaders/fragment.glsl");

    GLuint axisVAO, axisVBO;
    createAxesVAO(axisVAO, axisVBO);

    if (argc > 1){
        for (int i = 1; i < argc; i++){
            Model m;
            if (loadOBJ(argv[i], argv[i], m)){
                gModels.push_back(m);
            }
        }
    } else{
            Model m1, m2, m3, m4, m5;
            if (loadOBJ("modelos/bunny.obj", "bunny.obj", m1)) gModels.push_back(m1);
            if (loadOBJ("modelos/car.obj", "car.obj", m2)) gModels.push_back(m2);
            if (loadOBJ("modelos/casa.obj", "casa.obj", m3)) gModels.push_back(m3);
            if (loadOBJ("modelos/cat.obj", "cat.obj", m4)) gModels.push_back(m4);
            if (loadOBJ("modelos/Teapot.obj", "Teapot.obj", m5)) gModels.push_back(m5);
        }

    gModels.push_back(createMathPlaneModel());

    std::cout << "======================================================\n";
    std::cout << " COntroles:\n";
    std::cout << "  [Flechas lados] : cambiar de modelo\n";
    std::cout << "  [1 - 3]             : 1=puntos, 2=lineas, 3=superficie\n";
    std::cout << "  [X  Y  Z]             : alternar rotacion en eje X, Y o Z\n";
    std::cout << "  [W/S A/D Q/E]         : rotar manualmente en X, Y, Z\n";
    std::cout << "  [raton + click Izq+Mover] : orbitar)\n";
    std::cout << "  [rueda raton]       : Zoom\n";
    std::cout << "  [r]                     : reiniciar\n";
    std::cout << "  [esc]                   : salir\n";
    std::cout << "======================================================" << std::endl;

    selectModel(0);

    float lastTime = (float)glfwGetTime();

    while (!glfwWindowShouldClose(window)){
        float currentTime = (float)glfwGetTime();
        float deltaTime = currentTime - lastTime;
        lastTime = currentTime;

        glfwPollEvents();

        if (gAutoRotateX) gRotation.x += gRotationSpeed * deltaTime;
        if (gAutoRotateY) gRotation.y += gRotationSpeed * deltaTime;
        if (gAutoRotateZ) gRotation.z += gRotationSpeed * deltaTime;

        glClearColor(0.12f, 0.13f, 0.16f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        float aspect = 1.0f;
        if (height > 0) aspect = (float)width / (float)height;

        // posicion orbital
        float radYaw   = glm::radians(gCamYaw);
        float radPitch = glm::radians(gCamPitch);
        glm::vec3 camPos;
        camPos.x = gCamRadius * cos(radPitch) * sin(radYaw);
        camPos.y = gCamRadius * sin(radPitch);
        camPos.z = gCamRadius * cos(radPitch) * cos(radYaw);

        glm::mat4 view = glm::lookAt(camPos, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);

        glUseProgram(shaderProgram);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uView"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));

        // ejes en el origen
        glm::mat4 identityModel = glm::mat4(1.0f);
        glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(identityModel));
        glUniform1i(glGetUniformLocation(shaderProgram, "uIsAxis"), 1);

        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(2.5f);
        glBindVertexArray(axisVAO);
        glDrawArrays(GL_LINES, 0, 6);
        glLineWidth(1.0f);

        if (!gModels.empty()){
            Model& model = gModels[gCurrentModelIdx];

            glUniform1i(glGetUniformLocation(shaderProgram, "uIsAxis"), 0);
            glUniform1f(glGetUniformLocation(shaderProgram, "uMinY"), model.minBounds.y);
            glUniform1f(glGetUniformLocation(shaderProgram, "uMaxY"), model.maxBounds.y);

            glm::mat4 modelMat = glm::mat4(1.0f);
            modelMat = glm::rotate(modelMat, glm::radians(gRotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            modelMat = glm::rotate(modelMat, glm::radians(gRotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            modelMat = glm::rotate(modelMat, glm::radians(gRotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

            float finalScale = gScale;
            if (gAutoNormalize){
                finalScale = gScale * model.normalizeScale;
            }
            modelMat = glm::scale(modelMat, glm::vec3(finalScale));

            // centrar
            if (gAutoNormalize) {
                modelMat = glm::translate(modelMat, -model.center);
            }

            glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "uModel"), 1, GL_FALSE, glm::value_ptr(modelMat));

            glBindVertexArray(model.VAO);
            if (gRenderMode == MODE_POINTS) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
                glDrawArrays(GL_POINTS, 0, (int)model.vertices.size());
            } else if (gRenderMode == MODE_LINES) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glDrawElements(GL_TRIANGLES, (int)model.indices.size(), GL_UNSIGNED_INT, 0);
            } else {
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glDrawElements(GL_TRIANGLES, (int)model.indices.size(), GL_UNSIGNED_INT, 0);
            }
            glBindVertexArray(0);
        }

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

#ifdef USE_IMGUI
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Controles");
        ImGui::Text("Modelo actual: %s", gModels[gCurrentModelIdx].name.c_str());
        if (ImGui::Button("Anterior")) selectModel(gCurrentModelIdx - 1);
        ImGui::SameLine();
        if (ImGui::Button("Siguiente")) selectModel(gCurrentModelIdx + 1);

        ImGui::Separator();
        ImGui::Text("Vertices: %d", (int)gModels[gCurrentModelIdx].vertices.size());

        ImGui::Separator();
        ImGui::RadioButton("Puntos", &gRenderMode, MODE_POINTS);
        ImGui::RadioButton("Lineas", &gRenderMode, MODE_LINES);
        ImGui::RadioButton("Superficie", &gRenderMode, MODE_SURFACE);

        ImGui::Separator();
        ImGui::SliderFloat("Rot X", &gRotation.x, -180.0f, 180.0f);
        ImGui::SliderFloat("Rot Y", &gRotation.y, -180.0f, 180.0f);
        ImGui::SliderFloat("Rot Z", &gRotation.z, -180.0f, 180.0f);
        ImGui::Checkbox("Girar X", &gAutoRotateX);
        ImGui::Checkbox("Girar Y", &gAutoRotateY);
        ImGui::Checkbox("Girar Z", &gAutoRotateZ);

        ImGui::SliderFloat("Escala", &gScale, 0.1f, 5.0f);
        ImGui::Checkbox("Ajustar caja", &gAutoNormalize);

        if (ImGui::Button("Reiniciar")) {
            gRotation = glm::vec3(0.0f);
            gScale = 1.0f;
            gAutoRotateX = gAutoRotateY = gAutoRotateZ = false;
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif

        glfwSwapBuffers(window);
    }

#ifdef USE_IMGUI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}