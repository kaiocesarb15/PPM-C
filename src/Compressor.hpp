// =============================================================================
// Compressor.hpp — Junta tudo: lê símbolos, usa o modelo PPM-C, o codificador
//                  aritmético e o monitor de reset.
// =============================================================================
//
// FORMATO DO ARQUIVO COMPRIMIDO (.ppmc)
// -------------------------------------
//   Cabeçalho (32 bytes):
//     4 bytes  "PPMC"              identificador
//     1 byte   versão do formato   (1)
//     1 byte   bits por símbolo    (1, 4 ou 8)
//     1 byte   Kmax
//     1 byte   reset ligado?       (0 ou 1)
//     4 bytes  j (janela)
//     8 bytes  p (percentual, double)
//     4 bytes  limite de memória (MB)
//     8 bytes  tamanho original (bytes)
//   Depois: o fluxo de bits produzido pelo codificador aritmético.
//
// Assim o descompressor descobre sozinho todos os parâmetros usados.
//
// LAÇO PRINCIPAL (igual nos dois sentidos)
// ----------------------------------------
//   para cada símbolo i:
//       codifica (ou decodifica) o símbolo com o modelo PPM-C
//       custo = bits gastos neste símbolo
//       se o monitor de taxa mandar resetar      -> limpa o modelo
//       senão, se a memória estourou o limite     -> limpa o modelo
// =============================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "ArithmeticCoder.hpp"
#include "BitIO.hpp"
#include "PPMModel.hpp"
#include "RateMonitor.hpp"
#include "SymbolStream.hpp"

// Parâmetros escolhidos pelo usuário.
struct Params {
    int bits = 8;            // 1, 4 ou 8
    int kmax = 2;            // ordem máxima
    bool resetEnabled = false;
    uint32_t window = 10000; // j
    double percent = 10.0;   // p (%)
    uint32_t memLimitMB = 2048;
};

// Estatísticas devolvidas depois de comprimir/descomprimir.
struct Stats {
    uint64_t numSymbols = 0;
    uint64_t resets = 0;        // resets pela regra da taxa (j, p)
    uint64_t memoryResets = 0;  // resets por limite de memória
};

// Registro do "comprimento médio progressivo" (para os gráficos).
// Gera um CSV com linhas:  tipo,posicao,bits_totais
//   tipo P = ponto da curva, R = reset por taxa, M = reset por memória
struct ProgressLog {
    FILE* file = nullptr;
    uint64_t step = 1;  // grava um ponto a cada 'step' símbolos
};

static constexpr size_t HEADER_SIZE = 32;

// ---------------------------------------------------------------------------
// Funções auxiliares para escrever/ler inteiros no cabeçalho (little-endian).
// ---------------------------------------------------------------------------
inline void putU64(std::vector<uint8_t>& v, size_t pos, uint64_t x, int nbytes) {
    for (int i = 0; i < nbytes; i++) v[pos + i] = static_cast<uint8_t>(x >> (8 * i));
}
inline uint64_t getU64(const std::vector<uint8_t>& v, size_t pos, int nbytes) {
    uint64_t x = 0;
    for (int i = 0; i < nbytes; i++) x |= static_cast<uint64_t>(v[pos + i]) << (8 * i);
    return x;
}

// ===========================================================================
// COMPRESSÃO
// ===========================================================================
inline std::vector<uint8_t> compress(const std::vector<uint8_t>& input,
                                     const Params& p, Stats& stats,
                                     ProgressLog* log = nullptr,
                                     bool verbose = false) {
    // --- Cabeçalho ---
    BitWriter writer;
    std::vector<uint8_t>& out = writer.bytes();
    out.assign(HEADER_SIZE, 0);
    std::memcpy(out.data(), "PPMC", 4);
    out[4] = 1;
    out[5] = static_cast<uint8_t>(p.bits);
    out[6] = static_cast<uint8_t>(p.kmax);
    out[7] = p.resetEnabled ? 1 : 0;
    putU64(out, 8, p.window, 4);
    uint64_t percentBits;
    std::memcpy(&percentBits, &p.percent, 8);
    putU64(out, 12, percentBits, 8);
    putU64(out, 20, p.memLimitMB, 4);
    putU64(out, 24, input.size(), 8);

    // --- Objetos principais ---
    ArithmeticEncoder encoder(writer);
    PPMModel model(p.bits, p.kmax, p.memLimitMB);
    RateMonitor monitor(p.resetEnabled, p.window, p.percent);

    uint64_t n = numSymbols(input.size(), p.bits);
    stats = Stats();
    stats.numSymbols = n;
    uint64_t nextPercent = 0;

    for (uint64_t i = 0; i < n; i++) {
        int sym = getSymbol(input, i, p.bits);

        uint64_t before = encoder.bitCount();
        model.encodeSymbol(sym, encoder);
        uint64_t cost = encoder.bitCount() - before;  // bits deste símbolo

        // Decisão de reset (o descompressor fará exatamente o mesmo).
        if (monitor.addSymbol(cost)) {
            model.reset();
            stats.resets++;
            if (log) std::fprintf(log->file, "R,%llu,%llu\n",
                                  (unsigned long long)(i + 1),
                                  (unsigned long long)encoder.bitCount());
        } else if (model.memoryExceeded()) {
            model.reset();
            monitor.notifyExternalReset();
            stats.memoryResets++;
            if (log) std::fprintf(log->file, "M,%llu,%llu\n",
                                  (unsigned long long)(i + 1),
                                  (unsigned long long)encoder.bitCount());
        }

        // Ponto da curva de comprimento médio progressivo.
        if (log && ((i + 1) % log->step == 0 || i + 1 == n)) {
            std::fprintf(log->file, "P,%llu,%llu\n",
                         (unsigned long long)(i + 1),
                         (unsigned long long)encoder.bitCount());
        }

        // Barra de progresso simples (opção -v).
        if (verbose && i * 100 >= nextPercent * n) {
            std::fprintf(stderr, "\r  progresso: %3llu%%", (unsigned long long)nextPercent);
            nextPercent++;
        }
    }
    if (verbose) std::fprintf(stderr, "\r  progresso: 100%%\n");

    encoder.finish();
    return out;
}

// ===========================================================================
// DESCOMPRESSÃO
// ===========================================================================
inline std::vector<uint8_t> decompress(const std::vector<uint8_t>& input,
                                       Params& p, Stats& stats,
                                       bool verbose = false) {
    // --- Lê e valida o cabeçalho ---
    if (input.size() < HEADER_SIZE || std::memcmp(input.data(), "PPMC", 4) != 0)
        throw std::runtime_error("arquivo nao esta no formato PPMC");
    if (input[4] != 1) throw std::runtime_error("versao de formato desconhecida");

    p.bits = input[5];
    p.kmax = input[6];
    p.resetEnabled = input[7] != 0;
    p.window = static_cast<uint32_t>(getU64(input, 8, 4));
    uint64_t percentBits = getU64(input, 12, 8);
    std::memcpy(&p.percent, &percentBits, 8);
    p.memLimitMB = static_cast<uint32_t>(getU64(input, 20, 4));
    uint64_t originalSize = getU64(input, 24, 8);

    // --- Mesmos objetos do compressor, com os mesmos parâmetros ---
    BitReader reader(input, HEADER_SIZE);
    ArithmeticDecoder decoder(reader);
    PPMModel model(p.bits, p.kmax, p.memLimitMB);
    RateMonitor monitor(p.resetEnabled, p.window, p.percent);

    std::vector<uint8_t> out(originalSize, 0);
    uint64_t n = numSymbols(originalSize, p.bits);
    stats = Stats();
    stats.numSymbols = n;
    uint64_t nextPercent = 0;

    for (uint64_t i = 0; i < n; i++) {
        uint64_t before = decoder.bitCount();
        int sym = model.decodeSymbol(decoder);
        uint64_t cost = decoder.bitCount() - before;
        putSymbol(out, i, sym, p.bits);

        // EXATAMENTE a mesma lógica de reset do compressor.
        if (monitor.addSymbol(cost)) {
            model.reset();
            stats.resets++;
        } else if (model.memoryExceeded()) {
            model.reset();
            monitor.notifyExternalReset();
            stats.memoryResets++;
        }

        if (verbose && i * 100 >= nextPercent * n) {
            std::fprintf(stderr, "\r  progresso: %3llu%%", (unsigned long long)nextPercent);
            nextPercent++;
        }
    }
    if (verbose) std::fprintf(stderr, "\r  progresso: 100%%\n");
    return out;
}
