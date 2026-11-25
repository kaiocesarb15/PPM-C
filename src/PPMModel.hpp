// =============================================================================
// PPMModel.hpp — Modelo PPM-C (Prediction by Partial Matching, método C)
//                com mecanismo de EXCLUSÃO
// =============================================================================
//
// 1) O QUE É UM CONTEXTO?
// -----------------------
// O contexto de ordem k é a sequência dos k símbolos que vieram imediatamente
// antes do símbolo atual. Ex. (alfabeto A8, texto "abracadabra"), ao codificar
// o último 'a':
//       ordem 0 : ""        (nenhum símbolo — estatística geral)
//       ordem 1 : "r"
//       ordem 2 : "br"
//       ordem 3 : "abr"
// Para cada contexto que já apareceu, guardamos QUAIS símbolos vieram depois
// dele e QUANTAS VEZES (as "contagens"). Isso é a "tabela de probabilidades".
//
// 2) COMO O PPM CODIFICA UM SÍMBOLO
// ---------------------------------
// Tentamos primeiro o contexto MAIS LONGO (ordem Kmax). Se o símbolo já foi
// visto nesse contexto, codificamos ele ali (normalmente com probabilidade
// alta = poucos bits). Se NÃO foi visto, codificamos um símbolo especial
// ESCAPE ("não está aqui, desça uma ordem") e tentamos o contexto de ordem
// menor, e assim por diante até a ordem 0. Se nem na ordem 0 o símbolo
// apareceu, vamos para a ordem -1, onde todos os símbolos são equiprováveis.
//
// 3) O MÉTODO C (como estimar a probabilidade do escape)
// ------------------------------------------------------
// Num contexto onde já vimos  n  ocorrências no total, de  d  símbolos
// DISTINTOS:
//       P(símbolo s) = contagem(s) / (n + d)
//       P(escape)    =     d       / (n + d)
// Intuição: cada vez que um símbolo NOVO apareceu nesse contexto, foi
// "uma novidade"; então a chance de vir outra novidade é proporcional a d.
//
// 4) EXCLUSÃO
// -----------
// Se escapamos do contexto "abr", sabemos que o símbolo NÃO é nenhum dos que
// aparecem em "abr". Então, nas ordens menores, esses símbolos podem ser
// retirados da conta (excluídos) — a probabilidade que eles ocupariam é
// redistribuída para os outros. Isso economiza bits e é o "mecanismo de
// exclusão" pedido no trabalho. Na ordem -1 também só consideramos os
// símbolos que ainda não foram excluídos.
//
// Detalhe adicional: se, num contexto, TODOS os símbolos que ainda são
// possíveis (não excluídos) já foram vistos, o escape é impossível — não
// faz sentido reservar probabilidade para ele. Nesse caso usamos
// P(s) = contagem(s) / n. Isso é importante principalmente no alfabeto A1
// (só 2 símbolos), onde os contextos ficam "completos" muito rápido.
//
// 5) ATUALIZAÇÃO (depois de codificar o símbolo)
// ----------------------------------------------
// Incrementamos a contagem do símbolo no contexto onde ele foi encontrado e em
// todos os contextos de ordem MAIOR que foram visitados (onde houve escape).
// As ordens menores não são atualizadas ("exclusão de atualização", como no
// PPMC original de Moffat). Se o símbolo era novo num contexto, ele é
// adicionado com contagem 1.
//
// 6) ONDE GUARDAMOS AS TABELAS
// ----------------------------
// O contexto de ordem k são k símbolos de b bits => k*b bits. Nos casos do
// trabalho (A1 até k=32, A4 até k=8, A8 até k=8) isso sempre cabe em 64 bits.
// Então representamos cada contexto por um inteiro de 64 bits (a "chave") e
// usamos uma tabela hash (std::unordered_map) para cada ordem:
//       tables_[k][chave] -> Context (lista de símbolos + contagens)
//
// 7) ENCODER x DECODER
// --------------------
// O decodificador precisa tomar EXATAMENTE as mesmas decisões que o
// codificador. Por isso as duas funções (encodeSymbol e decodeSymbol) têm a
// mesma estrutura, linha por linha, e chamam as mesmas rotinas de atualização.
// =============================================================================
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include "ArithmeticCoder.hpp"

// Um símbolo visto num contexto, com sua contagem.
struct Entry {
    uint8_t symbol;   // 0..255 (cabe em 1 byte para os 3 alfabetos)
    uint16_t count;   // quantas vezes apareceu neste contexto
};

// Um contexto: a lista de símbolos que já apareceram depois dele.
struct Context {
    std::vector<Entry> entries;  // só os símbolos vistos (lista "esparsa")
    uint32_t total = 0;          // soma das contagens (n, sem exclusões)
};

// Função de espalhamento para as chaves de 64 bits.
// (A função padrão do C++ para inteiros é a identidade; para chaves como
//  os bits de A1 isso pode concentrar valores nos mesmos baldes da tabela.
//  Esta "mistura" — o finalizador do SplitMix64 — embaralha bem os bits.)
struct MixHash {
    size_t operator()(uint64_t x) const {
        x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27; x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return static_cast<size_t>(x);
    }
};

class PPMModel {
public:
    // Quando a soma das contagens de um contexto passa deste valor, todas as
    // contagens são divididas por 2 ("rescale"). Isso evita estouro dos
    // contadores de 16 bits e, de quebra, dá mais peso ao passado recente.
    static constexpr uint32_t MAX_CONTEXT_TOTAL = 65000;

    // Estimativa de memória gasta por contexto (nó da tabela hash + vetor).
    // Usada só para o limite de memória; não precisa ser exata, mas precisa
    // ser calculada igual no compressor e no descompressor (e é).
    static constexpr uint64_t BYTES_PER_CONTEXT = 112;

    // bits        : bits por símbolo (1, 4 ou 8)
    // kmax        : ordem máxima do modelo
    // memLimitMB  : limite aproximado de memória para as tabelas
    PPMModel(int bits, int kmax, uint64_t memLimitMB)
        : bits_(bits),
          alphabetSize_(1 << bits),
          kmax_(kmax),
          memLimitBytes_(memLimitMB * 1024ULL * 1024ULL),
          tables_(kmax + 1),
          visited_(kmax + 1, nullptr) {
        excluded_.fill(false);
    }

    // -------------------------------------------------------------------------
    // CODIFICAÇÃO de um símbolo
    // -------------------------------------------------------------------------
    void encodeSymbol(int sym, ArithmeticEncoder& enc) {
        int maxOrder = currentMaxOrder();
        numVisited_ = 0;
        bool found = false;

        // Desce da maior ordem disponível até a ordem 0.
        for (int k = maxOrder; k >= 0; k--) {
            Context& ctx = getContext(k);
            visited_[numVisited_++] = &ctx;  // lembra para atualizar depois
            if (encodeInContext(ctx, sym, enc)) {
                found = true;
                break;  // achou: para de descer
            }
            // não achou: um escape foi codificado (ou o contexto estava vazio)
        }
        // Escapou de todas as ordens: usa a ordem -1 (distribuição uniforme).
        if (!found) encodeOrderMinus1(sym, enc);

        update(sym);
    }

    // -------------------------------------------------------------------------
    // DECODIFICAÇÃO de um símbolo (espelho exato de encodeSymbol)
    // -------------------------------------------------------------------------
    int decodeSymbol(ArithmeticDecoder& dec) {
        int maxOrder = currentMaxOrder();
        numVisited_ = 0;
        int sym = -1;

        for (int k = maxOrder; k >= 0; k--) {
            Context& ctx = getContext(k);
            visited_[numVisited_++] = &ctx;
            sym = decodeInContext(ctx, dec);
            if (sym >= 0) break;  // achou o símbolo nesta ordem
        }
        if (sym < 0) sym = decodeOrderMinus1(dec);

        update(sym);
        return sym;
    }

    // Apaga todas as tabelas de probabilidade (usado pelo reset).
    // O histórico dos últimos símbolos é mantido: ele não é uma "probabilidade",
    // apenas diz qual é o contexto atual.
    void reset() {
        for (auto& table : tables_) {
            // Trocar por uma tabela vazia libera de fato a memória.
            std::unordered_map<uint64_t, Context, MixHash>().swap(table);
        }
        numContexts_ = 0;
        numEntries_ = 0;
    }

    // O modelo passou do limite de memória estimado?
    bool memoryExceeded() const {
        uint64_t estimate = numContexts_ * BYTES_PER_CONTEXT + numEntries_ * sizeof(Entry);
        return estimate > memLimitBytes_;
    }

    uint64_t numContexts() const { return numContexts_; }

private:
    // ---------------------------------------------------------------------
    // Funções auxiliares sobre o histórico / contextos
    // ---------------------------------------------------------------------

    // Maior ordem utilizável agora. No início do arquivo ainda não temos
    // Kmax símbolos de histórico, então só podemos usar ordens menores.
    int currentMaxOrder() const {
        return static_cast<int>(std::min<uint64_t>(kmax_, seen_));
    }

    // Chave do contexto de ordem k = os últimos k símbolos (k*b bits do histórico).
    uint64_t contextKey(int k) const {
        if (k == 0) return 0;
        int nbits = k * bits_;
        uint64_t mask = (nbits >= 64) ? ~0ULL : ((1ULL << nbits) - 1);
        return history_ & mask;
    }

    // Busca (ou cria vazio) o contexto de ordem k.
    Context& getContext(int k) {
        auto [it, inserted] = tables_[k].try_emplace(contextKey(k));
        if (inserted) numContexts_++;
        return it->second;
    }

    // Quantos símbolos do alfabeto ainda NÃO foram excluídos.
    int remainingSymbols() const {
        return alphabetSize_ - static_cast<int>(excludedList_.size());
    }

    // Marca como excluídos todos os símbolos (ainda não excluídos) do contexto.
    void excludeAll(const Context& ctx) {
        for (const Entry& e : ctx.entries) {
            if (!excluded_[e.symbol]) {
                excluded_[e.symbol] = true;
                excludedList_.push_back(e.symbol);
            }
        }
    }

    // ---------------------------------------------------------------------
    // Codificação dentro de UM contexto. Devolve true se o símbolo foi
    // codificado aqui; false se escapou (ou se o contexto não tinha nada).
    // ---------------------------------------------------------------------
    bool encodeInContext(const Context& ctx, int sym, ArithmeticEncoder& enc) {
        uint32_t n = 0;     // soma das contagens dos símbolos não excluídos
        uint32_t d = 0;     // número de símbolos distintos não excluídos
        uint32_t cum = 0;   // início da fatia do símbolo na "régua"
        uint32_t freq = 0;  // contagem do símbolo (0 = não está aqui)

        for (const Entry& e : ctx.entries) {
            if (excluded_[e.symbol]) continue;  // EXCLUSÃO: ignora
            if (e.symbol == sym) { cum = n; freq = e.count; }
            n += e.count;
            d++;
        }

        // Contexto sem nenhum símbolo utilizável: não há o que codificar,
        // passamos direto para a ordem de baixo (sem gastar bits).
        if (d == 0) return false;

        // O escape só é possível se ainda existem símbolos não vistos aqui.
        bool canEscape = static_cast<int>(d) < remainingSymbols();
        uint32_t total = n + (canEscape ? d : 0);   // régua: [símbolos | escape]

        if (freq > 0) {
            enc.encode(cum, cum + freq, total);   // P = freq / (n + d)
            return true;
        }
        enc.encode(n, n + d, total);             // ESCAPE: P = d / (n + d)
        excludeAll(ctx);                         // símbolos daqui ficam excluídos
        return false;
    }

    // Espelho de encodeInContext. Devolve o símbolo ou -1 se houve escape.
    int decodeInContext(const Context& ctx, ArithmeticDecoder& dec) {
        uint32_t n = 0, d = 0;
        for (const Entry& e : ctx.entries) {
            if (excluded_[e.symbol]) continue;
            n += e.count;
            d++;
        }
        if (d == 0) return -1;

        bool canEscape = static_cast<int>(d) < remainingSymbols();
        uint32_t total = n + (canEscape ? d : 0);

        uint32_t target = dec.getTarget(total);  // ponto da régua
        if (target < n) {
            // O ponto caiu na parte dos símbolos: procura qual fatia o contém.
            uint32_t cum = 0;
            for (const Entry& e : ctx.entries) {
                if (excluded_[e.symbol]) continue;
                if (target < cum + e.count) {
                    dec.decode(cum, cum + e.count, total);
                    return e.symbol;
                }
                cum += e.count;
            }
        }
        // O ponto caiu na parte do escape.
        dec.decode(n, n + d, total);
        excludeAll(ctx);
        return -1;
    }

    // ---------------------------------------------------------------------
    // Ordem -1: todos os símbolos NÃO excluídos são equiprováveis.
    // A "posição" do símbolo na régua é quantos símbolos não excluídos
    // existem antes dele.
    // ---------------------------------------------------------------------
    void encodeOrderMinus1(int sym, ArithmeticEncoder& enc) {
        uint32_t rank = 0;
        for (int s = 0; s < sym; s++)
            if (!excluded_[s]) rank++;
        enc.encode(rank, rank + 1, remainingSymbols());  // P = 1 / restantes
    }

    int decodeOrderMinus1(ArithmeticDecoder& dec) {
        uint32_t remaining = remainingSymbols();
        uint32_t target = dec.getTarget(remaining);
        uint32_t rank = 0;
        for (int s = 0; s < alphabetSize_; s++) {
            if (excluded_[s]) continue;
            if (rank == target) {
                dec.decode(rank, rank + 1, remaining);
                return s;
            }
            rank++;
        }
        return -1;  // nunca deveria acontecer
    }

    // ---------------------------------------------------------------------
    // Atualização das tabelas depois de codificar/decodificar 'sym'.
    // ---------------------------------------------------------------------
    void update(int sym) {
        // Incrementa nos contextos visitados (ordem encontrada e as maiores).
        for (int i = 0; i < numVisited_; i++) increment(*visited_[i], sym);

        // Limpa as exclusões para o próximo símbolo.
        for (int s : excludedList_) excluded_[s] = false;
        excludedList_.clear();

        // Acrescenta o símbolo ao histórico (desloca b bits e insere à direita).
        history_ = (history_ << bits_) | static_cast<uint64_t>(sym);
        seen_++;
    }

    // Soma 1 na contagem de 'sym' no contexto (ou insere com contagem 1).
    void increment(Context& ctx, int sym) {
        bool found = false;
        for (Entry& e : ctx.entries) {
            if (e.symbol == sym) { e.count++; found = true; break; }
        }
        if (!found) {
            ctx.entries.push_back({static_cast<uint8_t>(sym), 1});
            numEntries_++;
        }
        ctx.total++;

        // Rescale: divide as contagens por 2 (arredondando para cima,
        // para nenhum símbolo ficar com contagem 0).
        if (ctx.total > MAX_CONTEXT_TOTAL) {
            ctx.total = 0;
            for (Entry& e : ctx.entries) {
                e.count = static_cast<uint16_t>((e.count + 1) / 2);
                ctx.total += e.count;
            }
        }
    }

    // ---------------------------------------------------------------------
    // Dados do modelo
    // ---------------------------------------------------------------------
    int bits_;              // bits por símbolo (1, 4 ou 8)
    int alphabetSize_;      // 2, 16 ou 256
    int kmax_;              // ordem máxima
    uint64_t memLimitBytes_;

    // Uma tabela hash por ordem: tables_[k][chave do contexto] -> Context
    std::vector<std::unordered_map<uint64_t, Context, MixHash>> tables_;

    uint64_t history_ = 0;  // últimos símbolos, empacotados (o mais recente à direita)
    uint64_t seen_ = 0;     // quantos símbolos já passaram (para saber a ordem máxima)

    // Exclusão: excluded_[s] == true se 's' já foi descartado neste símbolo.
    std::array<bool, 256> excluded_;
    std::vector<int> excludedList_;  // lista dos excluídos (para limpar rápido)

    // Contextos visitados ao codificar o símbolo atual (para a atualização).
    std::vector<Context*> visited_;
    int numVisited_ = 0;

    // Contadores para a estimativa de memória.
    uint64_t numContexts_ = 0;
    uint64_t numEntries_ = 0;
};
