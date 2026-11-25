// =============================================================================
// SymbolStream.hpp — Converte bytes em símbolos de 1, 4 ou 8 bits (e vice-versa)
// =============================================================================
//
// O trabalho pede que a MESMA fonte (o arquivo) seja vista com três
// "resoluções" de alfabeto diferentes:
//
//   A1 = {0, 1}         -> cada byte vira 8 símbolos de 1 bit
//   A4 = {0, ..., 15}   -> cada byte vira 2 símbolos de 4 bits (nibbles)
//   A8 = {0, ..., 255}  -> cada byte é 1 símbolo
//
// Exemplo com o byte 0xB1 = 1011 0001:
//   A8: [177]
//   A4: [11, 1]                (nibble alto primeiro, depois o baixo)
//   A1: [1,0,1,1,0,0,0,1]      (bit mais significativo primeiro)
//
// Como 8 é múltiplo de 1, 4 e 8, todo arquivo tem um número INTEIRO de
// símbolos em qualquer alfabeto:  numSímbolos = numBytes * 8 / bitsPorSímbolo
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

// Quantos símbolos o arquivo tem quando lido com 'bits' bits por símbolo.
inline uint64_t numSymbols(uint64_t numBytes, int bits) {
    return numBytes * 8 / bits;
}

// Devolve o i-ésimo símbolo do arquivo 'data' (alfabeto de 'bits' bits).
inline int getSymbol(const std::vector<uint8_t>& data, uint64_t i, int bits) {
    if (bits == 8) {
        return data[i];
    }
    if (bits == 4) {
        uint8_t byte = data[i / 2];
        // i par -> nibble alto (4 bits da esquerda); i ímpar -> nibble baixo
        return (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);
    }
    // bits == 1
    uint8_t byte = data[i / 8];
    int pos = 7 - static_cast<int>(i % 8);  // bit 7 primeiro, bit 0 por último
    return (byte >> pos) & 1;
}

// Grava o símbolo 'sym' na posição i do vetor de saída 'out'.
// O vetor 'out' já deve ter o tamanho final e estar zerado.
inline void putSymbol(std::vector<uint8_t>& out, uint64_t i, int sym, int bits) {
    if (bits == 8) {
        out[i] = static_cast<uint8_t>(sym);
    } else if (bits == 4) {
        if (i % 2 == 0) out[i / 2] |= static_cast<uint8_t>(sym << 4);  // nibble alto
        else            out[i / 2] |= static_cast<uint8_t>(sym);       // nibble baixo
    } else {  // bits == 1
        int pos = 7 - static_cast<int>(i % 8);
        out[i / 8] |= static_cast<uint8_t>(sym << pos);
    }
}
