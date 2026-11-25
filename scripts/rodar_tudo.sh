#!/usr/bin/env bash
# =============================================================================
# rodar_tudo.sh — Executa TODOS os experimentos da seção 3, na ordem certa
# -----------------------------------------------------------------------------
# Uso:   bash scripts/rodar_tudo.sh
#
# Pode demorar muitas horas (o A1 com Kmax alto é o mais lento). Rode dentro do
# tmux para poder fechar o terminal:   tmux new -s ppmc
#   - sair sem parar:  Ctrl+B e depois D
#   - voltar:          tmux attach -t ppmc
#
# Se for interrompido, basta rodar de novo: as etapas já concluídas (cujo CSV
# já existe) são puladas.
#
# Variáveis opcionais (ex.: JOBS=6 MEM_MB=4096 bash scripts/rodar_tudo.sh):
#   JOBS     execuções em paralelo        (padrão: automático, pela RAM e núcleos)
#   MEM_MB   limite de memória do modelo  (padrão: 4096 se RAM >= 48 GB, senão 2048)
#   KMAX_A1  lista de Kmax testados no A1 (padrão: "0 4 8 12 16 20 24 28 32")
# =============================================================================
set -uo pipefail
cd "$(dirname "$0")/.."
mkdir -p resultados
LOG=resultados/log_rodar_tudo.txt
exec > >(tee -a "$LOG") 2>&1

# ---------------------------------------------------------------------------
# Resultados preliminares (da VM de 2 núcleos) vão para resultados/preliminar_vm,
# para não serem confundidos nem fazerem as etapas serem puladas.
# ---------------------------------------------------------------------------
if [ ! -f resultados/.rodar_tudo_iniciado ]; then
  mkdir -p resultados/preliminar_vm
  find resultados -maxdepth 1 -type f \( -name '*.csv' -o -name '*.png' -o -name '*.txt' \) \
       ! -name 'log_rodar_tudo.txt' -exec mv {} resultados/preliminar_vm/ \;
  date > resultados/.rodar_tudo_iniciado
fi

# ---------------------------------------------------------------------------
# Paralelismo e memória
# ---------------------------------------------------------------------------
NUCLEOS=$(nproc)
RAM_GB=$(free -g | awk '/Mem:/{print $2}')
if [ -z "${MEM_MB:-}" ]; then
  if [ "$RAM_GB" -ge 48 ]; then MEM_MB=4096; else MEM_MB=2048; fi
fi
if [ -z "${JOBS:-}" ]; then
  # cada processo: limite do modelo + ~0,7 GB (arquivo de 212 MB, saída, folga)
  POR_JOB=$((MEM_MB + 700))
  JOBS=$(( RAM_GB * 1024 * 9 / 10 / POR_JOB ))
  [ "$JOBS" -gt "$NUCLEOS" ] && JOBS=$NUCLEOS
  [ "$JOBS" -lt 1 ] && JOBS=1
fi
KMAX_A1=${KMAX_A1:-"0 4 8 12 16 20 24 28 32"}

echo "=================================================================="
echo "Início: $(date)"
echo "Máquina: $NUCLEOS núcleos, $RAM_GB GB RAM | JOBS=$JOBS | MEM_MB=$MEM_MB"
echo "Kmax do A1: $KMAX_A1"
echo "=================================================================="

make -s ppmc || exit 1
EXP="python3 scripts/experimentos.py"

etapa() {  # etapa "descrição" arquivo_de_saida comando...
  local desc="$1" saida="$2"; shift 2
  if [ -n "$saida" ] && [ -f "$saida" ]; then
    echo; echo ">> [pulada, já existe $saida] $desc"; return
  fi
  echo; echo ">> $(date +%H:%M:%S) $desc"
  local t0=$SECONDS
  "$@" || { echo "!! FALHOU: $desc"; exit 1; }
  echo ">> concluída em $(( (SECONDS - t0) / 60 )) min"
}

# --- 3.1 Ordem (com compressão, descompressão e verificação) ---
etapa "Kmax no dickens" resultados/ordem_dickens.csv \
      $EXP ordem dados/dickens --jobs "$JOBS" --memoria "$MEM_MB"
# shellcheck disable=SC2086
etapa "Kmax no Silesia completo" resultados/ordem_silesia_concat.csv \
      $EXP ordem dados/silesia_concat.bin --jobs "$JOBS" --memoria "$MEM_MB" --kmax-a1 $KMAX_A1

# --- 3.1 Reset (grade j x p com o melhor Kmax de cada alfabeto) ---
etapa "Grade de reset no Silesia completo" resultados/reset_silesia_concat.csv \
      $EXP reset dados/silesia_concat.bin --jobs "$JOBS" --memoria "$MEM_MB" --so-compressao \
           --j 1000 2000 5000 10000 20000 50000 100000 --p 5 10 20 30 40 50 \
           --j-a1 10000 50000 100000 --p-a1 10 30 50

# --- 3.1 Compressores comerciais ---
etapa "Comerciais no dickens" resultados/comerciais_dickens.csv \
      bash scripts/comerciais.sh dados/dickens
etapa "Comerciais no Silesia completo" resultados/comerciais_silesia_concat.csv \
      bash scripts/comerciais.sh dados/silesia_concat.bin

# --- 3.2 e 3.3 Curvas de comprimento médio progressivo ---
etapa "Curvas no dickens" resultados/curva_dickens_a8.csv \
      $EXP curvas dados/dickens --jobs "$JOBS" --memoria "$MEM_MB"
etapa "Curvas no Silesia completo (com e sem reset)" resultados/curva_silesia_concat_a1_reset.csv \
      $EXP curvas dados/silesia_concat.bin --jobs "$JOBS" --memoria "$MEM_MB" --reset auto

# --- Tabelas finais e gráficos (sempre refeitos) ---
etapa "Resumo do dickens" "" $EXP resumo dados/dickens
etapa "Resumo do Silesia" "" $EXP resumo dados/silesia_concat.bin
etapa "Gráficos" "" python3 scripts/graficos.py

echo
echo "=================================================================="
echo "FIM: $(date)"
echo "Resultados em resultados/. Para enviar ao GitHub:"
echo "  git add resultados && git commit -m 'Resultados finais' && git push"
echo "=================================================================="
