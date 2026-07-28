// gui_control_panel — маленькая панель на Dear ImGui для управления нашими
// потоками (scripts/stream_ctl.sh: источник ezcap/encoder/webcam ->
// приёмник 188/212) без необходимости держать открытым терминал. Всегда
// поверх остальных окон
// (SDL_WINDOW_ALWAYS_ON_TOP), сворачивается стандартной кнопкой в
// заголовке окна (обычное поведение SDL/оконного менеджера, ничего
// самодельного не требуется).
//
// Команды выполняются в фоновом потоке через popen() — GUI не подвисает,
// пока скрипт SSH-ится на удалённые машины (это может занимать секунды).
// Вывод команды построчно копится в лог и рисуется в прокручиваемой
// области снизу.

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL.h>
#include <SDL_opengl.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

namespace {

// Запоминаем, где пользователь оставил окно, чтобы при следующем запуске
// разворачивать его там же, а не по центру экрана.
std::string statePath() {
    const char* home = getenv("HOME");
    return std::string(home ? home : "") + "/.config/gui_control_panel_state.ini";
}

bool loadWindowPos(int& x, int& y) {
    std::ifstream f(statePath());
    if (!f) return false;
    std::string key;
    bool gotX = false, gotY = false;
    while (f >> key) {
        size_t eq = key.find('=');
        if (eq == std::string::npos) continue;
        std::string name = key.substr(0, eq);
        int value = atoi(key.c_str() + eq + 1);
        if (name == "x") { x = value; gotX = true; }
        else if (name == "y") { y = value; gotY = true; }
    }
    return gotX && gotY;
}

void saveWindowPos(int x, int y) {
    std::ofstream f(statePath());
    if (!f) return;
    f << "x=" << x << "\ny=" << y << "\n";
}

// Путь к репозиторию — приложение личное, для этой конкретной машины,
// поэтому абсолютный путь проще и надёжнее, чем угадывать относительно
// текущей директории запуска.
constexpr const char* kScriptsDir = "/home/gera/fantom-desktop/scripts";

// Значения — аргументы для scripts/stream_ctl.sh <source> <target> <action>,
// подписи — то, что видит пользователь в выпадающих списках. Не все
// комбинации источник×приёмник реально размечены в stream_ctl.sh (например
// encoder+188 никогда не тестировался) — в этом случае скрипт сам вернёт
// понятную ошибку "неизвестный профиль" в лог, отдельно валидировать это
// в GUI незачем.
constexpr const char* kSourceValues[] = {"ezcap", "encoder", "webcam"};
constexpr const char* kSourceLabels[] = {"Ezcap (USB-капчер)", "Энкодер (RTSP, HW HEVC)", "Webcam"};
constexpr const char* kTargetValues[] = {"188", "212"};
constexpr const char* kTargetLabels[] = {".188 (vrx188)", "nsu212"};

std::mutex g_logMutex;
std::deque<std::string> g_logLines;
std::atomic<bool> g_running{false};
constexpr size_t kMaxLogLines = 2000;

void appendLog(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_logLines.push_back(line);
    if (g_logLines.size() > kMaxLogLines) {
        g_logLines.pop_front();
    }
}

// Выполняет команду в фоновом потоке, построчно льёт stdout+stderr в лог.
// running уже выставлен true вызывающей стороной ДО старта потока — так
// кнопки блокируются мгновенно, а не только после первого appendLog().
void runCommandAsync(const std::string& label, const std::string& command) {
    std::thread([label, command]() {
        appendLog("$ " + label);
        FILE* pipe = popen(command.c_str(), "r");
        if (!pipe) {
            appendLog("ошибка: не удалось запустить команду");
            g_running = false;
            return;
        }
        std::array<char, 512> buf{};
        std::string lineAcc;
        while (fgets(buf.data(), (int)buf.size(), pipe)) {
            lineAcc += buf.data();
            size_t pos;
            while ((pos = lineAcc.find('\n')) != std::string::npos) {
                appendLog(lineAcc.substr(0, pos));
                lineAcc.erase(0, pos + 1);
            }
        }
        if (!lineAcc.empty()) appendLog(lineAcc);
        int rc = pclose(pipe);
        appendLog("--- завершено (код " + std::to_string(rc) + ") ---");
        g_running = false;
    }).detach();
}

} // namespace

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const char* glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // ALWAYS_ON_TOP — держит окно поверх остальных стандартным средством
    // оконного менеджера. RESIZABLE + обычная рамка — значит стандартная
    // кнопка сворачивания в заголовке работает "из коробки", без нашего
    // кода.
    int startX = SDL_WINDOWPOS_CENTERED, startY = SDL_WINDOWPOS_CENTERED;
    loadWindowPos(startX, startY);

    SDL_Window* window = SDL_CreateWindow(
        "fantom-desktop control panel",
        startX, startY,
        720, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALWAYS_ON_TOP);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    ImGui::StyleColorsDark();

    // Дефолтный встроенный шрифт ImGui содержит только латиницу — русский
    // текст рисуется как "?????". Грузим DejaVu Sans с диапазоном
    // кириллических глифов; если файла нет (другой дистрибутив/окружение),
    // остаёмся на дефолтном шрифте, чтобы приложение не падало.
    static const ImWchar cyrillicRanges[] = {
        0x0020, 0x00FF, // базовая латиница + latin-1
        0x0400, 0x052F, // кириллица + доп. кириллица
        0x2010, 0x2027, // тире/кавычки-ёлочки (—, «», …) — используются в UI-тексте
        0,
    };
    const char* fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    if (FILE* f = fopen(fontPath, "rb")) {
        fclose(f);
        io.Fonts->AddFontFromFileTTF(fontPath, 18.0f, nullptr, cyrillicRanges);
    }

    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // "Свернуть" здесь — не системное сворачивание в трей/панель задач
    // (для этого хватает штатной кнопки на рамке окна), а сжатие рабочей
    // области до полоски с названием, при этом окно остаётся видимым и
    // поверх всех остальных (ALWAYS_ON_TOP никуда не девается).
    bool collapsed = false;
    int expandedW = 720, expandedH = 480;
    constexpr int kCollapsedH = 56;

    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) done = true;
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window)) {
                done = true;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("panel", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

        if (collapsed) {
            ImGui::Text("fantom-desktop");
            if (g_running.load()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "работает...");
            }
            if (ImGui::Button("Развернуть", ImVec2(-1, 0))) {
                collapsed = false;
                SDL_SetWindowResizable(window, SDL_TRUE);
                SDL_SetWindowSize(window, expandedW, expandedH);
            }
        } else {
            ImGui::Text("fantom-desktop — управление потоками");
            if (ImGui::Button("Свернуть", ImVec2(-1, 0))) {
                SDL_GetWindowSize(window, &expandedW, &expandedH);
                collapsed = true;
                SDL_SetWindowResizable(window, SDL_FALSE);
                SDL_SetWindowSize(window, expandedW, kCollapsedH);
            }
            ImGui::Separator();

            static int srcIdx = 0;
            static int tgtIdx = 0;
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##source", &srcIdx, kSourceLabels, IM_ARRAYSIZE(kSourceLabels));
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##target", &tgtIdx, kTargetLabels, IM_ARRAYSIZE(kTargetLabels));

            bool running = g_running.load();
            if (running) ImGui::BeginDisabled();
            const char* src = kSourceValues[srcIdx];
            const char* tgt = kTargetValues[tgtIdx];
            std::string base = std::string("bash ") + kScriptsDir + "/stream_ctl.sh " + src + " " + tgt + " ";
            std::string profileLabel = std::string(kSourceLabels[srcIdx]) + " -> " + kTargetLabels[tgtIdx];
            if (ImGui::Button("Старт", ImVec2(-1, 0))) {
                g_running = true;
                runCommandAsync(profileLabel + ": старт", base + "start 2>&1");
            }
            if (ImGui::Button("Стоп", ImVec2(-1, 0))) {
                g_running = true;
                runCommandAsync(profileLabel + ": стоп", base + "stop 2>&1");
            }
            if (ImGui::Button("Статус", ImVec2(-1, 0))) {
                g_running = true;
                runCommandAsync(profileLabel + ": статус", base + "status 2>&1");
            }
            if (running) {
                ImGui::EndDisabled();
                ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "выполняется...");
            }

            ImGui::Separator();
            ImGui::Text("Лог:");
            ImGui::BeginChild("log", ImVec2(0, 0), true,
                               ImGuiWindowFlags_HorizontalScrollbar);
            {
                std::lock_guard<std::mutex> lock(g_logMutex);
                for (const auto& line : g_logLines) {
                    ImGui::TextUnformatted(line.c_str());
                }
            }
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
                ImGui::SetScrollHereY(1.0f);
            }
            ImGui::EndChild();
        }

        ImGui::End();

        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    {
        int x = 0, y = 0;
        SDL_GetWindowPosition(window, &x, &y);
        saveWindowPos(x, y);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
