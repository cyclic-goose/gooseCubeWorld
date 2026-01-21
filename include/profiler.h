#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <chrono>
#include <array>
#include <glad/glad.h>
#include <imgui.h>

// Configuration
#define PROFILER_HISTORY_SIZE 120
#define PROFILER_GPU_QUERY_BUFFERS 3

namespace Engine {

struct TimerData {
    std::string name;
    float history[PROFILER_HISTORY_SIZE] = { 0.0f };
    int historyOffset = 0;
    float min = 0.0f;
    float max = 0.0f;
    float avg = 0.0f;
    float current = 0.0f;

    void Update(float timeMs) {
        current = timeMs;
        history[historyOffset] = timeMs;
        historyOffset = (historyOffset + 1) % PROFILER_HISTORY_SIZE;

        // Exponential Moving Average (Alpha = 0.1) for smoothing UI
        avg = (timeMs * 0.1f) + (avg * 0.9f);

        // Simple Min/Max tracking (scanning history for correct window)
        min = history[0];
        max = history[0];
        for (int i = 1; i < PROFILER_HISTORY_SIZE; i++) {
            if (history[i] < min) min = history[i];
            if (history[i] > max) max = history[i];
        }
    }
};

struct GPUTimer {
    TimerData data;
    GLuint queries[PROFILER_GPU_QUERY_BUFFERS] = { 0 };
    bool queryPending[PROFILER_GPU_QUERY_BUFFERS] = { false };
};

class Profiler {
public:
    static Profiler& Get() {
        static Profiler instance;
        return instance;
    }

    // Master Toggle: If false, timers return immediately for zero overhead
    bool m_Enabled = false; 

    // Helper to flip state from Input (e.g., 'P' key)
    void Toggle() {
        m_Enabled = !m_Enabled;
    }

    // --- CPU Profiling (RAII) ---
    struct ScopedTimer {
        const char* name;
        std::chrono::high_resolution_clock::time_point start;
        bool active;

        ScopedTimer(const char* name) : name(name) {
            active = Profiler::Get().m_Enabled;
            if (active) start = std::chrono::high_resolution_clock::now();
        }

        ~ScopedTimer() {
            if (active) {
                auto end = std::chrono::high_resolution_clock::now();
                float duration = std::chrono::duration<float, std::milli>(end - start).count();
                Profiler::Get().StoreCPU(name, duration);
            }
        }
    };

    void StoreCPU(const char* name, float durationMs) {
        // Thread-safe for background mesh tasks
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_CpuTimers[name].Update(durationMs);
    }

    // --- GPU Profiling (OpenGL Queries) ---
    // NOTE: GL_TIME_ELAPSED queries cannot be nested!
    void BeginGPU(const std::string& name) {
        if (!m_Enabled) return;

        // Note: Map access/insertion here assumes BeginGPU is always called from 
        // the Main Render Thread (Context constraint).
        GPUTimer& timer = m_GpuTimers[name]; 
        timer.data.name = name;

        // Initialize queries lazily
        if (timer.queries[0] == 0) {
            glGenQueries(PROFILER_GPU_QUERY_BUFFERS, timer.queries);
        }

        // Use the query slot for the current frame index
        GLuint query = timer.queries[m_FrameIndex % PROFILER_GPU_QUERY_BUFFERS];
        glBeginQuery(GL_TIME_ELAPSED, query);
        timer.queryPending[m_FrameIndex % PROFILER_GPU_QUERY_BUFFERS] = true;
    }

    void EndGPU() {
        if (!m_Enabled) return;
        glEndQuery(GL_TIME_ELAPSED);
    }

    // --- System Loop ---
    void Update() {
        if (!m_Enabled) return;

        m_FrameIndex++;
        int readIndex = m_FrameIndex % PROFILER_GPU_QUERY_BUFFERS;

        // Check results for the frame slot we are about to overwrite/reuse
        // This effectively gives us data from 3 frames ago (latency), preventing CPU stalls
        for (auto& [name, timer] : m_GpuTimers) {
            if (timer.queryPending[readIndex]) {
                GLuint64 elapsed = 0;
                GLint available = 0;
                GLuint query = timer.queries[readIndex];

                // Non-blocking check
                glGetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
                
                if (available) {
                    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed);
                    // Convert nanoseconds to milliseconds
                    timer.data.Update(elapsed / 1000000.0f);
                    timer.queryPending[readIndex] = false;
                }
            }
        }
    }

    // Must be called BEFORE glfwTerminate/OpenGL context destruction
    void Shutdown() {
        m_Enabled = false; // Prevent new timers from registering
        std::lock_guard<std::mutex> lock(m_Mutex);

        for (auto& [name, timer] : m_GpuTimers) {
            if (timer.queries[0] != 0) {
                glDeleteQueries(PROFILER_GPU_QUERY_BUFFERS, timer.queries);
                // Zero out to prevent double deletion in destructor
                for (int i = 0; i < PROFILER_GPU_QUERY_BUFFERS; i++) timer.queries[i] = 0;
            }
        }
        m_GpuTimers.clear();
        m_CpuTimers.clear();
    }

    void DrawUI(bool isMouseLocked = false) {
        // If disabled, do not draw anything (Zero overhead + No UI)
        if (!m_Enabled) return;

        ImGuiWindowFlags flags = 0;
        if (isMouseLocked) {
            flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoNav;
            ImGui::SetNextWindowBgAlpha(0.75f); // Transparent when locked
        }

        if (ImGui::Begin("Profiler Stats", nullptr, flags)) {
            // FPS & Frame Time Display
            float fps = ImGui::GetIO().Framerate;
            float frameMs = 1000.0f / (fps > 0.0f ? fps : 60.0f);
            
            // Color Coding: Red < 30, Yellow < 55, Green > 55
            ImVec4 fpsColor = (fps < 30.0f) ? ImVec4(1, 0, 0, 1) : ((fps < 55.0f) ? ImVec4(1, 1, 0, 1) : ImVec4(0, 1, 0, 1));

            ImGui::Text("Performance:"); ImGui::SameLine();
            ImGui::TextColored(fpsColor, "%.1f FPS", fps); ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(%.2f ms)", frameMs);
            ImGui::Separator();

            if (ImGui::Checkbox("Enable Profiler (P to Toggle)", &m_Enabled)) {
                 // If user unchecks this, the window will close immediately on next frame
            }
            
            ImGui::Separator();
            
            // Helper to draw list
            auto RenderTimerList = [](auto& timers, const char* label, ImVec4 color) {
                if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                    for (auto& [name, entry] : timers) {
                        // entry is either TimerData or GPUTimer
                        const TimerData* d = nullptr;
                        // SFINAE or simple check not needed if we know types, 
                        // but here we mix types in the lambda. 
                        // Hacky cast approach for clean code:
                        if constexpr (std::is_same_v<std::decay_t<decltype(entry)>, TimerData>) {
                            d = &entry;
                        } else {
                            d = &entry.data;
                        }

                        ImGui::PushID(name.c_str());
                        ImGui::Columns(2);
                        ImGui::Text("%s", name.c_str());
                        ImGui::NextColumn();
                        ImGui::TextColored(color, "%.3f ms", d->avg);
                        ImGui::NextColumn();
                        ImGui::Columns(1);
                        
                        ImGui::PlotLines("", d->history, PROFILER_HISTORY_SIZE, d->historyOffset, 
                                            nullptr, 0.0f, d->max * 1.5f, ImVec2(ImGui::GetContentRegionAvail().x, 40));
                        ImGui::PopID();
                        ImGui::Spacing();
                    }
                }
            };

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                RenderTimerList(m_CpuTimers, "CPU Tasks", ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            }
            
            RenderTimerList(m_GpuTimers, "GPU Passes (Latency: 3 Frames)", ImVec4(1.0f, 0.6f, 0.6f, 1.0f));
        }
        ImGui::End();
    }

private:
    Profiler() = default;
    
    ~Profiler() {
        // Safe fallback in case Shutdown() wasn't called manually.
        // Note: If context is already gone, glDeleteQueries inside Shutdown might still be unsafe,
        // but checking queries[0] != 0 helps.
        Shutdown();
    }

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    std::mutex m_Mutex;
    std::unordered_map<std::string, TimerData> m_CpuTimers;
    std::unordered_map<std::string, GPUTimer> m_GpuTimers;
    
    uint64_t m_FrameIndex = 0;
};

} // namespace Engine