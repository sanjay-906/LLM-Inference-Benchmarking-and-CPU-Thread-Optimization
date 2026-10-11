#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#include <cstdint>
#include <chrono>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

const std::wstring BENCH_EXE = LR"(D:\Projects\llamacpp\llama.cpp\build\bin\Release\llama-bench.exe)";
const std::wstring MODEL_PATH = LR"(D:\Projects\llamacpp\models\qwen2.5-1.5b-instruct-q4_k_m.gguf)";
const std::wstring RESULTS_DIR = LR"(D:\Projects\llamacpp\cpu-thread-benchmark\results)";

const int PROMPT_TOKENS = 512;
const int GENERATION_TOKENS = 128;
const int REPETITIONS = 5;

struct Metric {
    double mean = 0.0;
    double deviation = 0.0;
    bool found = false;
};

struct BenchmarkResult {
    int threads = 0;
    Metric prompt;
    Metric generation;

    double average_cpu_percent = 0.0;
    double peak_ram_mib = 0.0;
    double peak_commit_mib = 0.0;
};

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::vector<std::string> split_columns(const std::string& line) {
    std::vector<std::string> columns;
    std::stringstream stream(line);
    std::string column;

    while (std::getline(stream, column, '|')) {
        columns.push_back(trim(column));
    }

    return columns;
}

Metric parse_metric(const std::string& value) {
    Metric metric;

    try {
        std::size_t consumed = 0;
        metric.mean = std::stod(value, &consumed);

        // "±"
        const std::string remainder = value.substr(consumed);
        const auto marker = remainder.find("\xC2\xB1");

        if (marker != std::string::npos) {
            const std::string deviation_text = trim(remainder.substr(marker + 2));

            metric.deviation = std::stod(deviation_text);
        }

        metric.found = std::isfinite(metric.mean) && std::isfinite(metric.deviation) && metric.mean > 0.0 && metric.deviation >= 0.0;

    } catch (...) {
        metric.found = false;
    }

    return metric;
}

static uint64_t filetime_to_uint64(const FILETIME& ft) {
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

bool run_benchmark(int threads, std::string& output, double& cpu_percent, double& peak_ram_mib, double& peak_commit_mib) {
    std::wstring command = L"\"" + BENCH_EXE + L"\"" + L" -m \"" + MODEL_PATH + L"\"" + L" -t " + std::to_wstring(threads) + L" -p " + std::to_wstring(PROMPT_TOKENS) + L" -n " + std::to_wstring(GENERATION_TOKENS) + L" -r " + std::to_wstring(REPETITIONS);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE read_pipe = nullptr;
    HANDLE write_pipe = nullptr;

    if (!CreatePipe(&read_pipe, &write_pipe, &sa, 0)) {
        std::cerr << "CreatePipe failed: " << GetLastError() << '\n';
        return false;
    }

    if (!SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0)) {
        std::cerr << "SetHandleInformation failed: " << GetLastError() << '\n';
        CloseHandle(read_pipe);
        CloseHandle(write_pipe);
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write_pipe;
    si.hStdError = write_pipe;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    const BOOL created = CreateProcessW(
        BENCH_EXE.c_str(),
        mutable_command.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi
    );

    CloseHandle(write_pipe);

    if (!created) {
        std::cerr << "CreateProcessW failed: " << GetLastError() << '\n';
        CloseHandle(read_pipe);
        return false;
    }

    const auto wall_start = std::chrono::steady_clock::now();

    char buffer[4096];
    DWORD bytes_read = 0;

    while (ReadFile(read_pipe, buffer, sizeof(buffer), &bytes_read, nullptr) && bytes_read > 0) {
        output.append(buffer, bytes_read);
    }

    CloseHandle(read_pipe);

    WaitForSingleObject(pi.hProcess, INFINITE);

    const auto wall_end = std::chrono::steady_clock::now();

    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    FILETIME creation_time{}, exit_time{};
    FILETIME kernel_time{}, user_time{};

    if (GetProcessTimes(pi.hProcess, &creation_time, &exit_time, &kernel_time, &user_time)) {

        const uint64_t cpu_ticks = filetime_to_uint64(kernel_time) + filetime_to_uint64(user_time);
        const double cpu_seconds = static_cast<double>(cpu_ticks) / 10'000'000.0;
        const double wall_seconds = std::chrono::duration<double>(wall_end - wall_start).count();

        SYSTEM_INFO system_info{};
        GetSystemInfo(&system_info);

        const DWORD logical_processors = system_info.dwNumberOfProcessors;

        if (wall_seconds > 0.0 && logical_processors > 0) {
            cpu_percent = 100.0 * cpu_seconds / (wall_seconds * logical_processors);
        }
    }

    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);

    if (GetProcessMemoryInfo(pi.hProcess, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory))) {

        constexpr double BYTES_PER_MIB = 1024.0 * 1024.0;
        peak_ram_mib = static_cast<double>(memory.PeakWorkingSetSize) / BYTES_PER_MIB;
        peak_commit_mib = static_cast<double>(memory.PeakPagefileUsage) / BYTES_PER_MIB;

    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (exit_code != 0) {
        std::cerr << "llama-bench failed. Exit code: " << exit_code << '\n' << output << '\n';
        return false;
    }

    return true;
}

bool parse_results(const std::string& output, BenchmarkResult& result) {
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        auto columns = split_columns(line);

        if (!columns.empty() && columns.front().empty()) {
            columns.erase(columns.begin());
        }

        if (!columns.empty() && columns.back().empty()) {
            columns.pop_back();
        }

        if (columns.size() != 7) {
            continue;
        }

        try {
            const int reported_threads = std::stoi(columns[4]);

            if (reported_threads != result.threads) {
                continue;
            }

            if (columns[5] == "pp512") {
                result.prompt = parse_metric(columns[6]);
            } 
            else if (columns[5] == "tg128") {
                result.generation = parse_metric(columns[6]);
            }

        } catch (...) {
            continue;
        }
    }

    return result.prompt.found && result.generation.found;
}

int main() {
    if (!fs::exists(BENCH_EXE) || !fs::exists(MODEL_PATH)) {
        std::cerr << "Benchmark executable or model file not found.\n";
        std::cerr << "Check BENCH_EXE and MODEL_PATH in main.cpp.\n";
        return 1;
    }

    std::error_code ec;
    fs::create_directories(RESULTS_DIR, ec);

    if (ec) {
        std::cerr << "Cannot create results directory: " << ec.message() << '\n';
        return 1;
    }

    const fs::path csv_path = fs::path(RESULTS_DIR) / L"thread_results.csv";

    std::ofstream csv(csv_path, std::ios::out | std::ios::trunc);

    if (!csv) {
        std::cerr << "Cannot open CSV file for writing.\n";
        return 1;
    }

    csv << "threads,prompt_tps,prompt_deviation,generation_tps,generation_deviation,prompt_speedup,generation_speedup,average_cpu_percent,peak_ram_mib,peak_commit_mib\n";
    std::vector<BenchmarkResult> results;
    std::cout << "CPU Thread Scaling Benchmark\n" << "Prompt tokens: " << PROMPT_TOKENS << '\n' << "Generation tokens: " << GENERATION_TOKENS << '\n' << "Repetitions: " << REPETITIONS << "\n\n";

    for (int threads = 1; threads <= 8; ++threads) {
        std::cout << "[RUN] Threads: " << threads << " ...\n";

        std::string output;

        double cpu_percent = 0.0;
        double peak_ram_mib = 0.0;
        double peak_commit_mib = 0.0;

        if (!run_benchmark(threads, output, cpu_percent, peak_ram_mib, peak_commit_mib)) {
            std::cerr << "Run failed for " << threads << " threads. Stopping.\n";
            return 1;
        }

        BenchmarkResult result;
        result.threads = threads;

        if (!parse_results(output, result)) {
            std::cerr << "Could not parse benchmark results for " << threads << " threads.\n";
            std::cerr << "Captured output:\n" << output << '\n';
            return 1;
        }
        result.average_cpu_percent = cpu_percent;
        result.peak_ram_mib = peak_ram_mib;
        result.peak_commit_mib = peak_commit_mib;

        results.push_back(result);
        std::cout << std::fixed << std::setprecision(2) << "  Average CPU: " << result.average_cpu_percent << "%\n" << "  Peak RAM: " << result.peak_ram_mib << " MiB\n" << "  Peak committed memory: " << result.peak_commit_mib << " MiB\n";
        std::cout << std::fixed << std::setprecision(2) << "  Prompt: " << result.prompt.mean << " tok/s" << " (reported ± " << result.prompt.deviation << ")\n" << "  Generation: " << result.generation.mean << " tok/s" << " (reported ± " << result.generation.deviation << ")\n\n";
    }

    const double baseline_prompt = results.front().prompt.mean;
    const double baseline_generation = results.front().generation.mean;

    for (const auto& result : results) {
        csv << result.threads << ',' << result.prompt.mean << ',' << result.prompt.deviation << ',' << result.generation.mean << ',' << result.generation.deviation << ',' << result.prompt.mean / baseline_prompt << ',' << result.generation.mean / baseline_generation << ',' << result.average_cpu_percent << ',' << result.peak_ram_mib << ',' << result.peak_commit_mib << '\n';
    }

    csv.close();

    const auto best_generation = std::max_element(
        results.begin(), results.end(),
        [](const BenchmarkResult& a, const BenchmarkResult& b) {
            return a.generation.mean < b.generation.mean;
        });

    const auto best_prompt = std::max_element(
        results.begin(), results.end(),
        [](const BenchmarkResult& a, const BenchmarkResult& b) {
            return a.prompt.mean < b.prompt.mean;
        });

    std::cout << "Benchmark complete!\n" << "Best generation: " << best_generation->threads << " threads at " << best_generation->generation.mean << " tok/s\n" << "Best prompt processing: " << best_prompt->threads << " threads at " << best_prompt->prompt.mean << " tok/s\n" << "CSV saved to: " << csv_path.string() << '\n';

    return 0;
}