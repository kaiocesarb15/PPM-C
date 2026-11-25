// =============================================================================
// ArithmeticCoder.hpp — Codificador e decodificador aritmético (inteiro, 32 bits)
// =============================================================================
//
// IDEIA GERAL
// -----------
// A codificação aritmética representa a mensagem inteira como UM número dentro
// do intervalo [0, 1). Começamos com o intervalo [0, 1) e, para cada símbolo,
// "encolhemos" o intervalo para a fatia que corresponde à probabilidade daquele
// símbolo. Símbolos prováveis encolhem pouco o intervalo (custam poucos bits);
// símbolos improváveis encolhem muito (custam muitos bits). No final, basta
// escrever bits suficientes para identificar um número dentro do intervalo.
// O custo total fica muito próximo de  Σ -log2(p(símbolo)),  a informação
// própria da mensagem — exatamente o que a Teoria da Informação diz ser o ótimo.
//
// COMO O MODELO "CONVERSA" COM O CODIFICADOR
// ------------------------------------------
// O modelo (PPM) não passa probabilidades em ponto flutuante. Ele passa três
// inteiros que descrevem uma FATIA de uma "régua" de tamanho 'total':
//
//     |-- símbolo A --|-- símbolo B --|---- escape ----|
//     0               3               5                7  (= total)
//
//  Para codificar B passamos (cumLow=3, cumHigh=5, total=7) => p(B) = 2/7.
//
// IMPLEMENTAÇÃO COM INTEIROS (Witten, Neal & Cleary, 1987)
// --------------------------------------------------------
// Em vez de [0,1) usamos inteiros de 32 bits: low = 0x00000000, high = 0xFFFFFFFF.
// Sempre que o intervalo fica "decidido" num bit, emitimos esse bit e
// dobramos o intervalo (renormalização). Três casos (vistos em aula):
//
//   E1: intervalo todo na metade de BAIXO  (high < 1/2)  -> emite 0
//   E2: intervalo todo na metade de CIMA   (low >= 1/2)  -> emite 1
//   E3: intervalo "em cima do meio" (1/4 <= low e high < 3/4): ainda não
//       sabemos se o bit será 0 ou 1. Guardamos um "bit pendente" e expandimos
//       em torno do meio. Quando o próximo E1/E2 acontecer, emitimos também os
//       pendentes com o valor OPOSTO.
//
// CONTAGEM DE BITS (importante para o reset!)
// -------------------------------------------
// Cada renormalização corresponde a exatamente 1 bit de saída (emitido na hora
// ou pendente). O DECODIFICADOR executa exatamente as mesmas renormalizações
// nos mesmos momentos. Logo, contando as renormalizações, compressor e
// descompressor sabem EXATAMENTE quantos bits cada símbolo custou — com números
// inteiros, sem nenhuma diferença entre os dois lados. É isso que usamos para
// medir a taxa local e decidir os resets de forma sincronizada.
// =============================================================================
#pragma once

#include <cstdint>
#include "BitIO.hpp"

namespace arith {
// Constantes do intervalo de 32 bits.
constexpr uint64_t TOP_VALUE     = 0xFFFFFFFFULL;  // maior valor possível
constexpr uint64_t HALF          = 0x80000000ULL;  // 1/2
constexpr uint64_t FIRST_QUARTER = 0x40000000ULL;  // 1/4
constexpr uint64_t THIRD_QUARTER = 0xC0000000ULL;  // 3/4
// O 'total' passado pelo modelo precisa ser bem menor que o tamanho do
// intervalo (que nunca fica menor que 1/4 de 2^32 = 2^30). Nosso modelo usa
// totais de no máximo ~65.800, muito abaixo deste limite.
constexpr uint64_t MAX_TOTAL     = 1ULL << 24;
}  // namespace arith

// -----------------------------------------------------------------------------
// CODIFICADOR
// -----------------------------------------------------------------------------
class ArithmeticEncoder {
public:
    explicit ArithmeticEncoder(BitWriter& out) : out_(out) {}

    // Codifica um evento cuja fatia na régua é [cumLow, cumHigh) de 'total'.
    void encode(uint32_t cumLow, uint32_t cumHigh, uint32_t total) {
        // Tamanho atual do intervalo.
        uint64_t range = high_ - low_ + 1;
        // Encolhe o intervalo para a fatia do símbolo (regra de três com inteiros).
        high_ = low_ + range * cumHigh / total - 1;
        low_  = low_ + range * cumLow  / total;

        // Renormaliza: emite os bits que já estão decididos.
        for (;;) {
            if (high_ < arith::HALF) {
                // E1: tudo na metade de baixo -> bit 0
                emitBitPlusPending(0);
            } else if (low_ >= arith::HALF) {
                // E2: tudo na metade de cima -> bit 1
                emitBitPlusPending(1);
                low_  -= arith::HALF;
                high_ -= arith::HALF;
            } else if (low_ >= arith::FIRST_QUARTER && high_ < arith::THIRD_QUARTER) {
                // E3: intervalo em volta do meio -> bit pendente
                pendingBits_++;
                low_  -= arith::FIRST_QUARTER;
                high_ -= arith::FIRST_QUARTER;
            } else {
                break;  // nada decidido ainda
            }
            // Dobra o intervalo (desloca 1 bit para a esquerda).
            low_  = 2 * low_;
            high_ = 2 * high_ + 1;
            bitCount_++;  // cada renormalização = 1 bit de saída
        }
    }

    // Finaliza a codificação: escreve bits suficientes para que o
    // decodificador identifique um número dentro do intervalo final.
    void finish() {
        pendingBits_++;
        if (low_ < arith::FIRST_QUARTER) emitBitPlusPending(0);
        else                             emitBitPlusPending(1);
        bitCount_ += 2;
        out_.flush();
    }

    // Quantos bits foram produzidos até agora (ver explicação no topo).
    uint64_t bitCount() const { return bitCount_; }

private:
    // Emite 'bit' e, em seguida, todos os bits pendentes com o valor oposto.
    void emitBitPlusPending(int bit) {
        out_.writeBit(bit);
        while (pendingBits_ > 0) {
            out_.writeBit(!bit);
            pendingBits_--;
        }
    }

    BitWriter& out_;
    uint64_t low_ = 0;
    uint64_t high_ = arith::TOP_VALUE;
    uint64_t pendingBits_ = 0;
    uint64_t bitCount_ = 0;
};

// -----------------------------------------------------------------------------
// DECODIFICADOR
// -----------------------------------------------------------------------------
// Ele mantém os mesmos low/high do codificador e, além disso, 'value': os 32
// bits do arquivo comprimido que estão "na janela" agora. Para decodificar:
//   1) getTarget(total): descobre em que ponto da régua 'value' está;
//   2) o modelo descobre qual símbolo tem a fatia que contém esse ponto;
//   3) decode(cumLow, cumHigh, total): atualiza o intervalo igual ao codificador.
// -----------------------------------------------------------------------------
class ArithmeticDecoder {
public:
    explicit ArithmeticDecoder(BitReader& in) : in_(in) {
        // Carrega os primeiros 32 bits.
        for (int i = 0; i < 32; i++) value_ = (value_ << 1) | in_.readBit();
    }

    // Passo 1: em que posição da régua [0, total) o valor atual cai?
    uint32_t getTarget(uint32_t total) const {
        uint64_t range = high_ - low_ + 1;
        return static_cast<uint32_t>(((value_ - low_ + 1) * total - 1) / range);
    }

    // Passo 3: mesma atualização do codificador + leitura de novos bits.
    void decode(uint32_t cumLow, uint32_t cumHigh, uint32_t total) {
        uint64_t range = high_ - low_ + 1;
        high_ = low_ + range * cumHigh / total - 1;
        low_  = low_ + range * cumLow  / total;

        for (;;) {
            if (high_ < arith::HALF) {
                // E1: nada a subtrair
            } else if (low_ >= arith::HALF) {
                // E2
                low_   -= arith::HALF;
                high_  -= arith::HALF;
                value_ -= arith::HALF;
            } else if (low_ >= arith::FIRST_QUARTER && high_ < arith::THIRD_QUARTER) {
                // E3
                low_   -= arith::FIRST_QUARTER;
                high_  -= arith::FIRST_QUARTER;
                value_ -= arith::FIRST_QUARTER;
            } else {
                break;
            }
            low_   = 2 * low_;
            high_  = 2 * high_ + 1;
            value_ = 2 * value_ + in_.readBit();  // entra um bit novo do arquivo
            bitCount_++;  // mesma contagem que o codificador faz
        }
    }

    uint64_t bitCount() const { return bitCount_; }

private:
    BitReader& in_;
    uint64_t low_ = 0;
    uint64_t high_ = arith::TOP_VALUE;
    uint64_t value_ = 0;
    uint64_t bitCount_ = 0;
};
