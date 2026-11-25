// =============================================================================
// RateMonitor.hpp — Monitoramento da taxa local e decisão de RESET
// =============================================================================
//
// MOTIVAÇÃO
// ---------
// O PPM aprende as estatísticas da fonte aos poucos. Se a fonte MUDA (ex.: no
// Corpus Silesia, passamos de um texto em inglês para um executável), as
// tabelas aprendidas viram "lixo": as previsões ficam ruins e o modelo demora
// para desaprender. Às vezes é melhor jogar tudo fora e recomeçar do zero.
//
// REGRA (enunciado, seção 2)
// --------------------------
// Dividimos a sequência de símbolos em janelas adjacentes de j símbolos.
// Ao fim de cada janela calculamos o comprimento médio (bits/símbolo):
//
//       L_atual    = bits gastos na janela atual    / j
//       L_anterior = bits gastos na janela anterior / j
//
// Se  L_atual > L_anterior * (1 + p/100)  => RESET do modelo.
//
// Como as duas janelas têm o mesmo tamanho j, basta comparar os bits:
//       bits_atual * 100 > bits_anterior * (100 + p)
//
// SINCRONIA COM O DESCOMPRESSOR
// -----------------------------
// Os "bits gastos" vêm da contagem de renormalizações do codificador
// aritmético (ver ArithmeticCoder.hpp), que é IDÊNTICA no descompressor.
// Então o descompressor, rodando este mesmo monitor, chega à mesma decisão
// no mesmo símbolo — sem precisarmos gravar nenhuma marca no arquivo.
//
// DETALHE: DEPOIS DE UM RESET
// ---------------------------
// Logo após o reset, o modelo está vazio e a próxima janela será RUIM (ele
// está reaprendendo). Se a comparássemos com a janela anterior, poderíamos
// disparar resets em cascata. Por isso, a primeira janela depois de um reset
// não é comparada: ela apenas vira a nova referência. (O mesmo vale para a
// primeira janela do arquivo.)
// =============================================================================
#pragma once

#include <cstdint>

class RateMonitor {
public:
    // enabled : liga/desliga o mecanismo
    // window  : tamanho j da janela (em símbolos)
    // percent : p (em %), ex.: 10.0 significa 10%
    RateMonitor(bool enabled, uint64_t window, double percent)
        : enabled_(enabled), window_(window), percent_(percent) {}

    // Chamado depois de CADA símbolo, com o custo (em bits) daquele símbolo.
    // Devolve true se o modelo deve ser reiniciado agora.
    bool addSymbol(uint64_t bits) {
        if (!enabled_) return false;

        currentBits_ += bits;
        currentCount_++;
        if (currentCount_ < window_) return false;  // janela ainda não fechou

        // ---- A janela atual acabou de fechar ----
        bool doReset = false;
        if (hasPrevious_) {
            // L_atual > L_anterior * (1 + p/100) ?
            double lhs = static_cast<double>(currentBits_) * 100.0;
            double rhs = static_cast<double>(previousBits_) * (100.0 + percent_);
            doReset = lhs > rhs;
        }

        if (doReset) {
            hasPrevious_ = false;          // próxima janela vira só referência
        } else {
            hasPrevious_ = true;
            previousBits_ = currentBits_;  // a atual vira a "anterior"
        }
        currentBits_ = 0;
        currentCount_ = 0;
        return doReset;
    }

    // Chamado quando o modelo é reiniciado por OUTRO motivo (limite de
    // memória): também não faz sentido comparar a próxima janela.
    void notifyExternalReset() {
        hasPrevious_ = false;
        currentBits_ = 0;
        currentCount_ = 0;
    }

private:
    bool enabled_;
    uint64_t window_;
    double percent_;

    bool hasPrevious_ = false;   // já existe uma janela de referência?
    uint64_t previousBits_ = 0;  // bits gastos na janela anterior
    uint64_t currentBits_ = 0;   // bits gastos na janela atual (até agora)
    uint64_t currentCount_ = 0;  // símbolos na janela atual (até agora)
};
