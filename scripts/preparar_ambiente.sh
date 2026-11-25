#!/usr/bin/env bash
# =============================================================================
# preparar_ambiente.sh — Instala o necessário no Ubuntu/WSL e prepara os dados
# -----------------------------------------------------------------------------
# Uso (dentro do Ubuntu, na pasta do repositório):
#     bash scripts/preparar_ambiente.sh [caminho/do/silesia.zip]
#
# Se o caminho do zip não for informado, ele procura um "silesia*.zip" nas
# pastas Downloads do Windows (/mnt/c/Users/*/Downloads).
#
# O que ele faz:
#   1) instala compilador, Python/matplotlib e os compressores comerciais;
#   2) compila o ppmc e roda o teste de integridade;
#   3) descompacta o Silesia em ~/silesia;
#   4) cria dados/silesia_concat.bin, a amostra e o link dados/dickens.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."
RAIZ=$(pwd)

# Aviso de desempenho: pastas do Windows (/mnt/c) são lentas dentro do WSL.
case "$RAIZ" in
  /mnt/*) echo "AVISO: o repositório está em $RAIZ (disco do Windows)."
          echo "       Fica bem mais rápido clonar dentro do Linux, ex.: cd ~ && git clone ..."
          echo ;;
esac

echo "== 1) Instalando dependências (vai pedir a senha do Ubuntu) =="
sudo apt-get update
sudo apt-get install -y build-essential python3 python3-matplotlib git unzip bc \
                        p7zip-full zstd xz-utils bzip2 gzip tmux

echo
echo "== 2) Compilando e testando a integridade =="
make
bash scripts/verificar.sh | tail -3

echo
echo "== 3) Silesia =="
ZIP="${1:-}"
if [ -z "$ZIP" ]; then
  ZIP=$(ls /mnt/c/Users/*/Downloads/silesia*.zip 2>/dev/null | head -1 || true)
fi
if [ -z "$ZIP" ] || [ ! -f "$ZIP" ]; then
  echo "ERRO: não achei o silesia.zip. Rode de novo informando o caminho, ex.:"
  echo "  bash scripts/preparar_ambiente.sh /mnt/c/Users/SEU_USUARIO/Downloads/silesia.zip"
  exit 1
fi
echo "Usando $ZIP"
mkdir -p ~/silesia
unzip -o -q "$ZIP" -d ~/silesia
# O zip pode ter os arquivos na raiz ou dentro de uma pasta: acha onde está o "dickens".
DICKENS=$(find ~/silesia -type f -name dickens | head -1)
if [ -z "$DICKENS" ]; then echo "ERRO: 'dickens' não encontrado dentro do zip."; exit 1; fi
PASTA=$(dirname "$DICKENS")

echo
echo "== 4) Preparando dados/ =="
python3 scripts/experimentos.py preparar "$PASTA"
ln -sf "$DICKENS" dados/dickens

echo
echo "Ambiente pronto. Máquina: $(nproc) núcleos, $(free -g | awk '/Mem:/{print $2}') GB de RAM visíveis no WSL."
echo "Próximo passo:  tmux new -s ppmc   e depois   bash scripts/rodar_tudo.sh"
