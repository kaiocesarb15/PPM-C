#!/usr/bin/env bash
# =============================================================================
# verificar.sh — Teste de INTEGRIDADE
# -----------------------------------------------------------------------------
# Comprime e descomprime vários arquivos, com vários alfabetos, ordens e com
# o reset ligado/desligado, e usa o "cmp" para provar que o arquivo
# descomprimido é IDÊNTICO ao original (byte a byte).
#
# Uso:
#   bash scripts/verificar.sh                 # usa arquivos de teste gerados
#   bash scripts/verificar.sh arq1 arq2 ...   # testa também os seus arquivos
# =============================================================================
set -u
cd "$(dirname "$0")/.."
make -s ppmc || exit 1

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# ---- Arquivos de teste (casos-limite + dados reais) ----
: > "$TMP/vazio"                                         # 0 bytes
printf 'A' > "$TMP/um_byte"                              # 1 byte
head -c 50000 /dev/zero > "$TMP/zeros"                   # só zeros
head -c 50000 /dev/urandom > "$TMP/aleatorio"            # incompressível
yes "abracadabra " | head -c 60000 > "$TMP/repetitivo"   # muito repetitivo
# texto + binário + texto: mudança de fonte (para exercitar o reset)
{ head -c 30000 "$TMP/repetitivo"; head -c 30000 /dev/urandom; cat src/*.hpp; } > "$TMP/misto"
cp src/PPMModel.hpp "$TMP/codigo_fonte"

ARQUIVOS=("$TMP"/vazio "$TMP"/um_byte "$TMP"/zeros "$TMP"/aleatorio \
          "$TMP"/repetitivo "$TMP"/misto "$TMP"/codigo_fonte "$@")

falhas=0
total=0
# Cada configuração: "alfabeto kmax opções-extras"
CONFIGS=(
  "1 0 " "1 8 " "1 16 " "1 32 " "1 12 -r -j 1000 -p 5"
  "4 0 " "4 2 " "4 8 "  "4 4 -r -j 1000 -p 10"
  "8 0 " "8 1 " "8 4 "  "8 8 " "8 3 -r -j 1000 -p 5" "8 4 -r -j 2000 -p 20"
  "8 5 -m 1"   # limite de memória minúsculo: força resets por memória
)

for arq in "${ARQUIVOS[@]}"; do
  for cfg in "${CONFIGS[@]}"; do
    read -r a k extra <<< "$cfg"
    total=$((total + 1))
    # shellcheck disable=SC2086
    ./ppmc c -a "$a" -k "$k" $extra "$arq" "$TMP/c.ppmc" > /dev/null &&
    ./ppmc d "$TMP/c.ppmc" "$TMP/d.out" > /dev/null &&
    cmp -s "$arq" "$TMP/d.out"
    if [ $? -eq 0 ]; then
      printf "  OK    %-16s -a %s -k %-2s %s\n" "$(basename "$arq")" "$a" "$k" "$extra"
    else
      printf "  FALHA %-16s -a %s -k %-2s %s\n" "$(basename "$arq")" "$a" "$k" "$extra"
      falhas=$((falhas + 1))
    fi
  done
done

echo
echo "Testes: $total   Falhas: $falhas"
[ "$falhas" -eq 0 ] && echo "Todos os arquivos descomprimidos sao IDENTICOS aos originais (cmp)."
exit "$falhas"
