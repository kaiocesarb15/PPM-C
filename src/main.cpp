// =============================================================================
// main.cpp — Programa de linha de comando do compressor PPM-C
// =============================================================================
//
// USO
//   Comprimir:    ppmc c [opções] <entrada> <saida.ppmc>
//   Descomprimir: ppmc d [-v] <entrada.ppmc> <saida>
//
// OPÇÕES (compressão)
//   -a <1|4|8>     bits por símbolo (alfabeto A1, A4 ou A8)       [padrão 8]
//   -k <Kmax>      ordem máxima do contexto                         [padrão 2]
//   -r             liga o reset por taxa local
//   -j <j>         tamanho da janela em símbolos                    [padrão 10000]
//   -p <p>         percentual de piora que dispara o reset (%)     [padrão 10]
//   -m <MB>        limite aproximado de memória do modelo (MB)     [padrão 2048]
//   --log <arq>    grava a curva de comprimento médio progressivo (CSV)
//   --pontos <N>   número aproximado de pontos da curva            [padrão 2000]
//   -v             mostra o progresso
//
// SAÍDA
//   Ao final, imprime estatísticas no formato "chave: valor" (uma por linha),
//   fáceis de ler por um humano e por um script.
// =============================================================================
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include "Compressor.hpp"

// Lê um arquivo inteiro para a memória.
static std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("nao foi possivel abrir " + path);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}

// Grava um vetor de bytes num arquivo.
static void writeFile(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("nao foi possivel criar " + path);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

static void usage() {
    std::fprintf(stderr,
        "Uso:\n"
        "  ppmc c [opcoes] <entrada> <saida.ppmc>   (comprimir)\n"
        "  ppmc d [-v] <entrada.ppmc> <saida>       (descomprimir)\n"
        "Opcoes:\n"
        "  -a <1|4|8>    bits por simbolo           (padrao 8)\n"
        "  -k <Kmax>     ordem maxima               (padrao 2)\n"
        "  -r            liga o reset por taxa local\n"
        "  -j <j>        janela em simbolos         (padrao 10000)\n"
        "  -p <p>        percentual de piora (%%)    (padrao 10)\n"
        "  -m <MB>       limite de memoria (MB)     (padrao 2048)\n"
        "  --log <arq>   CSV com a curva de comprimento medio progressivo\n"
        "  --pontos <N>  pontos na curva            (padrao 2000)\n"
        "  -v            mostra o progresso\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    std::string mode = argv[1];

    Params params;
    std::string logPath;
    uint64_t logPoints = 2000;
    bool verbose = false;
    std::vector<std::string> files;

    // --- Leitura dos argumentos ---
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { usage(); std::exit(1); }
            return argv[++i];
        };
        if      (a == "-a") params.bits = std::atoi(next().c_str());
        else if (a == "-k") params.kmax = std::atoi(next().c_str());
        else if (a == "-r") params.resetEnabled = true;
        else if (a == "-j") params.window = static_cast<uint32_t>(std::atol(next().c_str()));
        else if (a == "-p") params.percent = std::atof(next().c_str());
        else if (a == "-m") params.memLimitMB = static_cast<uint32_t>(std::atol(next().c_str()));
        else if (a == "--log") logPath = next();
        else if (a == "--pontos") logPoints = std::strtoull(next().c_str(), nullptr, 10);
        else if (a == "-v") verbose = true;
        else files.push_back(a);
    }
    if (files.size() != 2 || (mode != "c" && mode != "d")) { usage(); return 1; }

    // --- Validação dos parâmetros ---
    if (mode == "c") {
        if (params.bits != 1 && params.bits != 4 && params.bits != 8) {
            std::fprintf(stderr, "Erro: -a deve ser 1, 4 ou 8\n");
            return 1;
        }
        // O contexto (Kmax * bits) precisa caber em 64 bits.
        int maxK = 64 / params.bits;
        if (params.kmax < 0 || params.kmax > maxK) {
            std::fprintf(stderr, "Erro: para -a %d, Kmax deve estar entre 0 e %d\n",
                         params.bits, maxK);
            return 1;
        }
        if (params.window == 0) {
            std::fprintf(stderr, "Erro: -j deve ser maior que zero\n");
            return 1;
        }
    }

    try {
        std::vector<uint8_t> input = readFile(files[0]);
        Stats stats;
        using Clock = std::chrono::steady_clock;

        if (mode == "c") {
            // ---------------- COMPRESSÃO ----------------
            ProgressLog log;
            ProgressLog* logPtr = nullptr;
            if (!logPath.empty()) {
                log.file = std::fopen(logPath.c_str(), "w");
                if (!log.file) throw std::runtime_error("nao foi possivel criar " + logPath);
                uint64_t n = numSymbols(input.size(), params.bits);
                log.step = (logPoints > 0 && n > logPoints) ? n / logPoints : 1;
                std::fprintf(log.file, "tipo,posicao,bits_totais\n");
                logPtr = &log;
            }

            auto t0 = Clock::now();
            std::vector<uint8_t> output = compress(input, params, stats, logPtr, verbose);
            auto t1 = Clock::now();
            double seconds = std::chrono::duration<double>(t1 - t0).count();

            writeFile(files[1], output);
            if (log.file) std::fclose(log.file);

            double bitsOut = 8.0 * static_cast<double>(output.size());
            std::printf("modo: compressao\n");
            std::printf("alfabeto_bits: %d\n", params.bits);
            std::printf("kmax: %d\n", params.kmax);
            std::printf("reset: %d\n", params.resetEnabled ? 1 : 0);
            std::printf("janela_j: %u\n", params.window);
            std::printf("percentual_p: %g\n", params.percent);
            std::printf("bytes_entrada: %zu\n", input.size());
            std::printf("bytes_saida: %zu\n", output.size());
            std::printf("simbolos: %llu\n", (unsigned long long)stats.numSymbols);
            std::printf("bits_por_simbolo: %.6f\n",
                        stats.numSymbols ? bitsOut / stats.numSymbols : 0.0);
            std::printf("bits_por_byte: %.6f\n",
                        input.size() ? bitsOut / input.size() : 0.0);
            std::printf("resets: %llu\n", (unsigned long long)stats.resets);
            std::printf("resets_memoria: %llu\n", (unsigned long long)stats.memoryResets);
            std::printf("tempo_s: %.3f\n", seconds);
        } else {
            // ---------------- DESCOMPRESSÃO ----------------
            auto t0 = Clock::now();
            std::vector<uint8_t> output = decompress(input, params, stats, verbose);
            auto t1 = Clock::now();
            double seconds = std::chrono::duration<double>(t1 - t0).count();

            writeFile(files[1], output);

            std::printf("modo: descompressao\n");
            std::printf("alfabeto_bits: %d\n", params.bits);
            std::printf("kmax: %d\n", params.kmax);
            std::printf("bytes_saida: %zu\n", output.size());
            std::printf("simbolos: %llu\n", (unsigned long long)stats.numSymbols);
            std::printf("resets: %llu\n", (unsigned long long)stats.resets);
            std::printf("resets_memoria: %llu\n", (unsigned long long)stats.memoryResets);
            std::printf("tempo_s: %.3f\n", seconds);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Erro: %s\n", e.what());
        return 1;
    }
    return 0;
}
