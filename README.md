# PPM-C — Compressor PPM-C com exclusão e reset por taxa local

Trabalho prático da disciplina **Introdução à Teoria da Informação (2026.2)**.

O programa implementa um compressor/descompressor baseado no **PPM-C com mecanismo de exclusão**, com:

- alfabetos de **1 bit** (A1 = {0,1}), **4 bits** (A4 = {0..15}) e **8 bits** (A8 = {0..255});
- ordem máxima **Kmax** configurável (A1: 0–32; A4 e A8: 0–8);
- **reset por taxa local**: compara o comprimento médio de janelas adjacentes de **j** símbolos e limpa o modelo quando a piora passa de **p%**. O descompressor detecta o reset sozinho, sem nenhum bit extra no arquivo;
- registro da curva de **comprimento médio progressivo** para os gráficos.

Tudo em C++17, sem bibliotecas externas. Os scripts de experimentos e gráficos são em Python (matplotlib).

---

## 1. Compilar

```bash
make            # gera o executável ./ppmc
make test       # teste de integridade: comprime, descomprime e compara com cmp
```

Requisitos: `g++` com suporte a C++17 (Linux, macOS ou WSL no Windows).

## 2. Usar

```bash
# Comprimir (alfabeto A8, Kmax = 4, sem reset)
./ppmc c -a 8 -k 4 entrada.txt saida.ppmc

# Comprimir com reset (janela j = 10000 símbolos, p = 20%)
./ppmc c -a 4 -k 8 -r -j 10000 -p 20 entrada.txt saida.ppmc

# Descomprimir (os parâmetros são lidos do cabeçalho do arquivo)
./ppmc d saida.ppmc restaurado.txt

# Conferir a integridade
cmp entrada.txt restaurado.txt && echo "IDENTICOS"
```

| Opção | Significado | Padrão |
|---|---|---|
| `-a 1\|4\|8` | bits por símbolo (alfabeto) | 8 |
| `-k K` | ordem máxima Kmax | 2 |
| `-r` | liga o reset por taxa local | desligado |
| `-j J` | tamanho da janela (em símbolos) | 10000 |
| `-p P` | percentual de piora que dispara o reset | 10 |
| `-m MB` | limite aproximado de memória do modelo (ao atingir, o modelo é reiniciado) | 2048 |
| `--log arq.csv` | grava a curva de comprimento médio progressivo | — |
| `--pontos N` | número de pontos da curva | 2000 |
| `-v` | mostra o progresso | — |

Ao final, o programa imprime `bits_por_simbolo`, `bits_por_byte`, número de resets e tempo.

## 3. Organização do código

Os arquivos devem ser lidos nesta ordem. Cada um começa com um bloco que explica a teoria por trás dele.

| Arquivo | O que faz |
|---|---|
| `src/BitIO.hpp` | escreve e lê **bits** individuais (o codificador aritmético trabalha bit a bit) |
| `src/SymbolStream.hpp` | converte bytes em símbolos de 1, 4 ou 8 bits e vice-versa |
| `src/ArithmeticCoder.hpp` | **codificador aritmético** inteiro de 32 bits (casos E1, E2 e E3) |
| `src/PPMModel.hpp` | **modelo PPM-C**: contextos, escape (método C), **exclusão**, atualização |
| `src/RateMonitor.hpp` | **monitor de taxa local**: janelas j, regra de p%, decisão de reset |
| `src/Compressor.hpp` | junta tudo: cabeçalho do arquivo e laço principal de compressão e descompressão |
| `src/main.cpp` | linha de comando e medição de tempo |

### Decisões de projeto (resumo)

1. **Probabilidades do PPM-C.** Num contexto com `n` ocorrências de `d` símbolos distintos (já descontados os excluídos): `P(s) = c(s)/(n+d)` e `P(escape) = d/(n+d)`.
2. **Exclusão.** Ao escapar de um contexto, os símbolos dele são retirados das ordens menores e da ordem −1.
3. **Escape impossível.** Se um contexto já contém todos os símbolos ainda possíveis, não se reserva probabilidade para o escape. Isso é importante no A1.
4. **Atualização.** O símbolo é contado no contexto onde foi encontrado e nos de ordem maior (exclusão de atualização, como no PPMC original).
5. **Rescale.** Quando a soma das contagens de um contexto passa de 65.000, todas são divididas por 2.
6. **Tabelas.** Usamos uma `unordered_map` por ordem. A chave é o contexto empacotado em 64 bits (A1 com K=32 usa 32 bits; A8 com K=8 usa 64 bits).
7. **Sincronia do reset.** O "custo" de cada símbolo é o número de bits que o codificador aritmético emitiu, contado pelas renormalizações. O decodificador faz exatamente as mesmas renormalizações, então os dois lados medem as mesmas taxas e tomam a mesma decisão de reset, usando só números inteiros.
8. **Após um reset**, a janela seguinte não é comparada: ela vira a nova referência. Isso evita resets em cascata enquanto o modelo reaprende.
9. **Limite de memória** (`-m`). Quando a estimativa de memória das tabelas passa do limite, o modelo é reiniciado. É determinístico, logo também sincronizado. Sem isso, A8 com K=8 no Silesia inteiro precisaria de dezenas de GB.

## 4. Reproduzir os experimentos

```bash
# 0) Baixe e descompacte o Silesia numa pasta (ex.: ~/silesia, com os 12 arquivos)
#    https://sun.aei.polsl.pl/~sdeor/index.php?page=silesia
python3 scripts/experimentos.py preparar ~/silesia      # cria dados/silesia_concat.bin
ln -s ~/silesia/dickens dados/dickens

# 3.1 — Varredura de Kmax (sem reset), com compressão, descompressão e verificação
python3 scripts/experimentos.py ordem dados/dickens            --jobs 8
python3 scripts/experimentos.py ordem dados/silesia_concat.bin --jobs 8

# 3.1 — Varredura de j e p (reset), usando o melhor Kmax de cada alfabeto
python3 scripts/experimentos.py reset dados/silesia_concat.bin --jobs 8

# 3.1 — Compressores comerciais (gzip, bzip2, xz, zstd, 7z LZMA2 e 7z PPMd)
bash scripts/comerciais.sh dados/dickens
bash scripts/comerciais.sh dados/silesia_concat.bin

# 3.2 e 3.3 — Curvas de comprimento médio progressivo
python3 scripts/experimentos.py curvas dados/dickens
python3 scripts/experimentos.py curvas dados/silesia_concat.bin --reset auto

# Tabelas finais e gráficos
python3 scripts/experimentos.py resumo dados/dickens
python3 scripts/experimentos.py resumo dados/silesia_concat.bin
python3 scripts/graficos.py
```

**Cuidados com tempo e memória**

- Cada execução usa um núcleo. Use `--jobs` igual ao número de núcleos, mas confira a RAM. A8 com Kmax alto pode usar o limite inteiro de `-m` (2 GB por padrão) em cada processo.
- Na varredura de reset, `--so-compressao` pula a descompressão e corta o tempo pela metade.
- `--alfabetos 8` e `--kmax-max 24` restringem a varredura.
- Para testar rápido, use `dados/silesia_amostra.bin` (os primeiros 2 MB de cada arquivo).

Os CSVs e PNGs são gravados em `resultados/`.
