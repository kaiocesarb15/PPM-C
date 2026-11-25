// =============================================================================
// BitIO.hpp — Escrita e leitura de bits individuais
// =============================================================================
//
// POR QUE PRECISAMOS DISSO?
// -------------------------
// O computador só sabe gravar BYTES (grupos de 8 bits) em arquivos. Mas o
// codificador aritmético produz a saída BIT A BIT: a cada passo ele decide
// emitir um "0" ou um "1". Então precisamos de uma pequena "ponte":
//
//   - BitWriter: junta os bits que chegam, um por um, e a cada 8 bits completos
//     forma um byte e guarda num vetor.
//   - BitReader: faz o caminho inverso — pega os bytes do arquivo e entrega
//     um bit de cada vez.
//
// Convenção usada: o PRIMEIRO bit escrito vai para a posição MAIS significativa
// do byte (bit 7). Ex.: escrever 1,0,1,1,0,0,0,1 gera o byte 10110001 = 0xB1.
// =============================================================================
#pragma once

#include <cstdint>
#include <vector>

// -----------------------------------------------------------------------------
// BitWriter: acumula bits e produz um vetor de bytes.
// -----------------------------------------------------------------------------
class BitWriter {
public:
    // Escreve UM bit (0 ou 1).
    void writeBit(int bit) {
        // Desloca o acumulador para a esquerda e coloca o novo bit na direita.
        current_ = static_cast<uint8_t>((current_ << 1) | (bit & 1));
        numBits_++;
        // Quando completamos 8 bits, o byte está pronto.
        if (numBits_ == 8) {
            bytes_.push_back(current_);
            current_ = 0;
            numBits_ = 0;
        }
    }

    // Completa o último byte com zeros (se estiver pela metade).
    // Deve ser chamado uma única vez, ao final da codificação.
    void flush() {
        while (numBits_ != 0) writeBit(0);
    }

    // Acesso ao resultado (os bytes produzidos até agora).
    std::vector<uint8_t>& bytes() { return bytes_; }

private:
    std::vector<uint8_t> bytes_;  // bytes já completos
    uint8_t current_ = 0;         // byte sendo montado
    int numBits_ = 0;             // quantos bits já estão em current_
};

// -----------------------------------------------------------------------------
// BitReader: lê bits, um de cada vez, de um vetor de bytes.
// -----------------------------------------------------------------------------
class BitReader {
public:
    // data  : vetor com os bytes do arquivo
    // start : posição (em bytes) onde começam os dados (pulamos o cabeçalho)
    BitReader(const std::vector<uint8_t>& data, size_t start)
        : data_(data), bytePos_(start) {}

    // Lê UM bit. Se os dados acabarem, devolve 0.
    // (O decodificador aritmético às vezes "espia" alguns bits além do fim;
    //  devolver 0 nesses casos é a convenção padrão e é seguro, pois o
    //  codificador terminou a mensagem de forma a não depender desses bits.)
    int readBit() {
        if (bytePos_ >= data_.size()) return 0;
        int bit = (data_[bytePos_] >> (7 - bitPos_)) & 1;  // do bit 7 até o bit 0
        bitPos_++;
        if (bitPos_ == 8) {  // terminou o byte: passa para o próximo
            bitPos_ = 0;
            bytePos_++;
        }
        return bit;
    }

private:
    const std::vector<uint8_t>& data_;
    size_t bytePos_;   // índice do byte atual
    int bitPos_ = 0;   // qual bit (0..7) do byte atual será lido a seguir
};
