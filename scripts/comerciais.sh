#!/usr/bin/env bash
# =============================================================================
# comerciais.sh — Compara com compressores comerciais/populares
# -----------------------------------------------------------------------------
# Roda, no mesmo arquivo, os compressores disponíveis na máquina
# (7-Zip, xz, bzip2, gzip, zstd), mede tamanho e tempo, confere a
# descompressão com cmp e grava resultados/comerciais_<arquivo>.csv
#
# Uso: bash scripts/comerciais.sh <arquivo>
# (No Windows com WinRAR/7-Zip gráfico, anote os tamanhos manualmente.)
# =============================================================================
set -eu
cd "$(dirname "$0")/.."
ARQ=$(readlink -f "$1")   # caminho real (resolve links simbólicos)
BASE=$(basename "${1%.*}")
OUT="resultados/comerciais_${BASE}.csv"
mkdir -p resultados
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
ORIG=$(stat -c %s "$ARQ")

echo "compressor,opcoes,bytes_entrada,bytes_saida,bits_por_byte,tempo_compressao_s,tempo_descompressao_s,identico" > "$OUT"

# mede o tempo (em segundos) de um comando
cronometro() { local t0 t1; t0=$(date +%s.%N); "$@"; t1=$(date +%s.%N); echo "$t1 - $t0" | bc; }

testar() {  # nome opcoes cmd_comprimir cmd_descomprimir arquivo_comprimido
  local nome="$1" opc="$2" comp="$3" desc="$4" saida="$5"
  command -v "${comp%% *}" > /dev/null || { echo "  ($nome nao instalado, pulando)"; return; }
  local tc td tam ident
  tc=$(cronometro bash -c "$comp")
  tam=$(stat -c %s "$saida")
  td=$(cronometro bash -c "$desc")
  if cmp -s "$ARQ" "$TMP/restaurado"; then ident=sim; else ident=NAO; fi
  local bpb; bpb=$(echo "scale=6; $tam * 8 / $ORIG" | bc)
  echo "$nome,$opc,$ORIG,$tam,$bpb,$tc,$td,$ident" >> "$OUT"
  printf "  %-8s %-14s %12d bytes  %s bits/byte  comp=%ss desc=%ss  identico=%s\n" \
         "$nome" "$opc" "$tam" "$bpb" "$tc" "$td" "$ident"
  rm -f "$saida" "$TMP/restaurado"
}

echo "Arquivo: $ARQ ($ORIG bytes)"
testar gzip  "-9"           "gzip -9 -c '$ARQ' > '$TMP/a.gz'"         "gzip -d -c '$TMP/a.gz' > '$TMP/restaurado'"         "$TMP/a.gz"
testar bzip2 "-9"           "bzip2 -9 -c '$ARQ' > '$TMP/a.bz2'"       "bzip2 -d -c '$TMP/a.bz2' > '$TMP/restaurado'"       "$TMP/a.bz2"
testar xz    "-9 (LZMA2)"   "xz -9 -T1 -c '$ARQ' > '$TMP/a.xz'"       "xz -d -c '$TMP/a.xz' > '$TMP/restaurado'"           "$TMP/a.xz"
testar zstd  "-19"          "zstd -q -19 -c '$ARQ' > '$TMP/a.zst'"    "zstd -q -d -c '$TMP/a.zst' > '$TMP/restaurado'"     "$TMP/a.zst"
testar 7z    "-mx9 (LZMA2)" "7z a -mx9 -bd -bso0 '$TMP/a.7z' '$ARQ'"  "7z e -so '$TMP/a.7z' > '$TMP/restaurado'"            "$TMP/a.7z"
testar 7z    "-m0=PPMd -mx9" "7z a -m0=PPMd -mx9 -bd -bso0 '$TMP/b.7z' '$ARQ'" "7z e -so '$TMP/b.7z' > '$TMP/restaurado'"  "$TMP/b.7z"
echo "Resultados em $OUT"
