#!/usr/bin/env python3
# =============================================================================
# experimentos.py — Automatiza os experimentos do trabalho (seção 3)
# =============================================================================
#
# Subcomandos:
#
#   preparar <pasta_silesia>
#       Concatena os 12 arquivos do Silesia (em ordem fixa) em
#       dados/silesia_concat.bin e grava dados/fronteiras.csv com a posição
#       (em bytes) onde cada arquivo começa. Também cria uma AMOSTRA menor
#       (os primeiros N MB de cada arquivo) para testes rápidos.
#
#   ordem   <arquivo> [--alfabetos 1 4 8] [--jobs N]
#       Seção 3.1: varre Kmax (A1: 0..32, A4/A8: 0..8) SEM reset.
#       Para cada execução: comprime, descomprime, confere com o original
#       e anota bits/símbolo, bits/byte e os tempos.
#       -> resultados/ordem_<arquivo>.csv
#
#   reset   <arquivo> [--j ...] [--p ...] [--jobs N]
#       Seção 3.1 (estratégia de reset): para cada alfabeto, usa o melhor
#       Kmax encontrado no subcomando "ordem" e varre a janela j e o
#       percentual p.
#       -> resultados/reset_<arquivo>.csv
#
#   curvas  <arquivo> [--reset j p]
#       Seções 3.2 e 3.3: gera os CSVs da curva de comprimento médio
#       progressivo para os três alfabetos (com o melhor Kmax).
#       -> resultados/curva_<arquivo>_a<bits>[_reset].csv
#
#   resumo  <arquivo>
#       Monta a tabela final: melhor resultado SEM reset x COM reset por
#       alfabeto, junto com os compressores comerciais.
#       -> resultados/resumo_<arquivo>.csv  (e imprime na tela)
#
# Exemplo de uso completo (no computador potente):
#   python3 scripts/experimentos.py preparar ~/silesia
#   python3 scripts/experimentos.py ordem  dados/silesia_concat.bin --jobs 8
#   python3 scripts/experimentos.py reset  dados/silesia_concat.bin --jobs 8
#   python3 scripts/experimentos.py curvas dados/silesia_concat.bin --reset auto
#   bash    scripts/comerciais.sh dados/silesia_concat.bin
#   python3 scripts/experimentos.py resumo dados/silesia_concat.bin
#   python3 scripts/graficos.py
# =============================================================================
import argparse
import csv
import filecmp
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor, as_completed

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PPMC = os.path.join(RAIZ, "ppmc")
RESULTADOS = os.path.join(RAIZ, "resultados")
DADOS = os.path.join(RAIZ, "dados")

# Ordem fixa dos arquivos do Silesia na concatenação (ordem alfabética).
SILESIA = ["dickens", "mozilla", "mr", "nci", "ooffice", "osdb",
           "reymont", "samba", "sao", "webster", "x-ray", "xml"]

# Faixas de Kmax pedidas no enunciado.
KMAX_FAIXA = {1: range(0, 33), 4: range(0, 9), 8: range(0, 9)}

# Valores padrão da varredura de reset (j entre 1.000 e 100.000; p entre 5 e 50%).
J_PADRAO = [1000, 2000, 5000, 10000, 20000, 50000, 100000]
P_PADRAO = [5, 10, 20, 30, 40, 50]


# -----------------------------------------------------------------------------
# Funções auxiliares
# -----------------------------------------------------------------------------
def compilar():
    """Garante que o executável ppmc está compilado."""
    subprocess.run(["make", "-s", "-C", RAIZ, "ppmc"], check=True)


def ler_saida(texto):
    """Converte a saída 'chave: valor' do ppmc em dicionário."""
    d = {}
    for linha in texto.strip().splitlines():
        if ": " in linha:
            k, v = linha.split(": ", 1)
            d[k] = v
    return d


def executar(arquivo, bits, kmax, reset=None, memoria=None, descomprimir=True, log=None):
    """
    Comprime (e opcionalmente descomprime e verifica) um arquivo.
    reset = None ou (j, p).
    Devolve um dicionário com os resultados.
    """
    with tempfile.TemporaryDirectory() as tmp:
        comprimido = os.path.join(tmp, "c.ppmc")
        restaurado = os.path.join(tmp, "d.out")

        cmd = [PPMC, "c", "-a", str(bits), "-k", str(kmax)]
        if reset:
            cmd += ["-r", "-j", str(reset[0]), "-p", str(reset[1])]
        if memoria:
            cmd += ["-m", str(memoria)]
        if log:
            cmd += ["--log", log]
        cmd += [arquivo, comprimido]
        c = ler_saida(subprocess.run(cmd, capture_output=True, text=True, check=True).stdout)

        r = {
            "arquivo": os.path.basename(arquivo),
            "alfabeto_bits": bits,
            "kmax": kmax,
            "reset": 1 if reset else 0,
            "j": reset[0] if reset else "",
            "p": reset[1] if reset else "",
            "bytes_entrada": int(c["bytes_entrada"]),
            "bytes_saida": int(c["bytes_saida"]),
            "bits_por_simbolo": float(c["bits_por_simbolo"]),
            "bits_por_byte": float(c["bits_por_byte"]),
            "resets": int(c["resets"]),
            "resets_memoria": int(c["resets_memoria"]),
            "tempo_compressao_s": float(c["tempo_s"]),
            "tempo_descompressao_s": "",
            "identico": "",
        }
        if descomprimir:
            d = ler_saida(subprocess.run([PPMC, "d", comprimido, restaurado],
                                         capture_output=True, text=True, check=True).stdout)
            r["tempo_descompressao_s"] = float(d["tempo_s"])
            # Equivalente ao "cmp": compara byte a byte.
            r["identico"] = "sim" if filecmp.cmp(arquivo, restaurado, shallow=False) else "NAO"
        return r


def rodar_em_paralelo(tarefas, jobs, saida_csv):
    """Executa uma lista de tarefas (dicionários de argumentos) e grava um CSV."""
    resultados = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futuros = {pool.submit(executar, **t): t for t in tarefas}
        for i, f in enumerate(as_completed(futuros), 1):
            r = f.result()
            resultados.append(r)
            print(f"[{i}/{len(tarefas)}] A{r['alfabeto_bits']} K={r['kmax']:>2} "
                  f"j={r['j']!s:>6} p={r['p']!s:>3}  "
                  f"{r['bits_por_simbolo']:.4f} bits/simb  {r['bits_por_byte']:.4f} bits/byte  "
                  f"tc={r['tempo_compressao_s']}s td={r['tempo_descompressao_s']}s "
                  f"resets={r['resets']} identico={r['identico']}", flush=True)
    # Ordena para o CSV ficar legível.
    resultados.sort(key=lambda r: (r["alfabeto_bits"], r["kmax"], str(r["j"]), str(r["p"])))
    os.makedirs(RESULTADOS, exist_ok=True)
    with open(saida_csv, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(resultados[0].keys()))
        w.writeheader()
        w.writerows(resultados)
    print(f"\nResultados gravados em {saida_csv}")
    return resultados


def nome_base(arquivo):
    return os.path.splitext(os.path.basename(arquivo))[0]


def melhor_kmax(arquivo):
    """Lê resultados/ordem_<arquivo>.csv e devolve {bits: melhor Kmax}."""
    caminho = os.path.join(RESULTADOS, f"ordem_{nome_base(arquivo)}.csv")
    if not os.path.exists(caminho):
        sys.exit(f"Rode primeiro o subcomando 'ordem' ({caminho} nao existe).")
    melhor = {}
    with open(caminho) as f:
        for r in csv.DictReader(f):
            b, bps = int(r["alfabeto_bits"]), float(r["bits_por_simbolo"])
            if b not in melhor or bps < melhor[b][1]:
                melhor[b] = (int(r["kmax"]), bps)
    return {b: k for b, (k, _) in melhor.items()}


def melhor_reset(arquivo):
    """Lê resultados/reset_<arquivo>.csv e devolve {bits: (kmax, j, p)}."""
    caminho = os.path.join(RESULTADOS, f"reset_{nome_base(arquivo)}.csv")
    if not os.path.exists(caminho):
        sys.exit(f"Rode primeiro o subcomando 'reset' ({caminho} nao existe).")
    melhor = {}
    with open(caminho) as f:
        for r in csv.DictReader(f):
            b, bps = int(r["alfabeto_bits"]), float(r["bits_por_simbolo"])
            if b not in melhor or bps < melhor[b][1]:
                melhor[b] = ((int(r["kmax"]), int(r["j"]), float(r["p"])), bps)
    return {b: v for b, (v, _) in melhor.items()}


# -----------------------------------------------------------------------------
# Subcomandos
# -----------------------------------------------------------------------------
def cmd_preparar(a):
    os.makedirs(DADOS, exist_ok=True)
    concat = os.path.join(DADOS, "silesia_concat.bin")
    amostra = os.path.join(DADOS, "silesia_amostra.bin")
    limite = a.amostra_mb * 1_000_000
    pos = 0
    with open(concat, "wb") as fc, open(amostra, "wb") as fa, \
         open(os.path.join(DADOS, "fronteiras.csv"), "w") as ff, \
         open(os.path.join(DADOS, "fronteiras_amostra.csv"), "w") as ffa:
        ff.write("arquivo,inicio_byte\n")
        ffa.write("arquivo,inicio_byte\n")
        pos_a = 0
        for nome in SILESIA:
            dados = open(os.path.join(a.pasta, nome), "rb").read()
            ff.write(f"{nome},{pos}\n")
            ffa.write(f"{nome},{pos_a}\n")
            fc.write(dados)
            fa.write(dados[:limite])
            pos += len(dados)
            pos_a += min(len(dados), limite)
            print(f"  {nome:10s} {len(dados):>12,d} bytes")
    print(f"Concatenado: {concat} ({pos:,d} bytes)")
    print(f"Amostra    : {amostra} ({pos_a:,d} bytes, {a.amostra_mb} MB de cada arquivo)")


def cmd_ordem(a):
    compilar()
    tarefas = []
    for bits in a.alfabetos:
        faixa = KMAX_FAIXA[bits]
        if a.kmax_a1 is not None and bits == 1:
            faixa = a.kmax_a1  # lista reduzida de Kmax para A1 (o alfabeto mais lento)
        if a.kmax_max is not None:
            faixa = [k for k in faixa if k <= a.kmax_max]
        for k in faixa:
            tarefas.append(dict(arquivo=a.arquivo, bits=bits, kmax=k,
                                memoria=a.memoria, descomprimir=not a.so_compressao))
    # As execuções mais lentas (Kmax alto) primeiro: aproveita melhor os núcleos.
    tarefas.sort(key=lambda t: -t["kmax"] * (9 - t["bits"]))
    rodar_em_paralelo(tarefas, a.jobs,
                      os.path.join(RESULTADOS, f"ordem_{nome_base(a.arquivo)}.csv"))


def cmd_reset(a):
    compilar()
    kmax = melhor_kmax(a.arquivo)
    print("Melhor Kmax (sem reset):", {f"A{b}": k for b, k in kmax.items()})
    tarefas = []
    for bits in a.alfabetos:
        if bits not in kmax:
            continue
        js = a.j_a1 if (bits == 1 and a.j_a1) else a.j
        ps = a.p_a1 if (bits == 1 and a.p_a1) else a.p
        for j in js:
            for p in ps:
                tarefas.append(dict(arquivo=a.arquivo, bits=bits, kmax=kmax[bits],
                                    reset=(j, p), memoria=a.memoria,
                                    descomprimir=not a.so_compressao))
    rodar_em_paralelo(tarefas, a.jobs,
                      os.path.join(RESULTADOS, f"reset_{nome_base(a.arquivo)}.csv"))


def cmd_curvas(a):
    compilar()
    os.makedirs(RESULTADOS, exist_ok=True)
    base = nome_base(a.arquivo)
    kmax = {b: k for b, k in zip(a.alfabetos, a.kmax)} if a.kmax else melhor_kmax(a.arquivo)
    tarefas = []
    for bits in a.alfabetos:
        # Curva sem reset
        log = os.path.join(RESULTADOS, f"curva_{base}_a{bits}.csv")
        tarefas.append(dict(arquivo=a.arquivo, bits=bits, kmax=kmax[bits],
                            memoria=a.memoria, descomprimir=False, log=log))
        # Curva com reset (opcional)
        if a.reset:
            if a.reset == ["auto"]:
                k, j, p = melhor_reset(a.arquivo)[bits]
            else:
                k, j, p = kmax[bits], int(a.reset[0]), float(a.reset[1])
            log = os.path.join(RESULTADOS, f"curva_{base}_a{bits}_reset.csv")
            tarefas.append(dict(arquivo=a.arquivo, bits=bits, kmax=k, reset=(j, p),
                                memoria=a.memoria, descomprimir=False, log=log))
    with ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for r in pool.map(lambda t: executar(**t), tarefas):
            print(f"A{r['alfabeto_bits']} K={r['kmax']} reset={r['reset']} j={r['j']} p={r['p']}: "
                  f"{r['bits_por_simbolo']:.4f} bits/simb, resets={r['resets']}")
    print(f"Curvas gravadas em {RESULTADOS}/curva_{base}_*.csv")


def cmd_resumo(a):
    base = nome_base(a.arquivo)
    linhas = []
    for nome_csv, rotulo in ((f"ordem_{base}.csv", "sem reset"), (f"reset_{base}.csv", "com reset")):
        caminho = os.path.join(RESULTADOS, nome_csv)
        if not os.path.exists(caminho):
            continue
        melhor = {}
        for r in csv.DictReader(open(caminho)):
            b = int(r["alfabeto_bits"])
            if b not in melhor or float(r["bits_por_simbolo"]) < float(melhor[b]["bits_por_simbolo"]):
                melhor[b] = r
        for b in sorted(melhor):
            r = melhor[b]
            linhas.append({
                "metodo": f"PPM-C A{b} ({rotulo})",
                "parametros": f"Kmax={r['kmax']}" + (f", j={r['j']}, p={r['p']}%" if r["reset"] == "1" else ""),
                "bits_por_simbolo": r["bits_por_simbolo"],
                "bits_por_byte": r["bits_por_byte"],
                "bytes_saida": r["bytes_saida"],
                "tempo_compressao_s": r["tempo_compressao_s"],
                "tempo_descompressao_s": r["tempo_descompressao_s"],
            })
    caminho = os.path.join(RESULTADOS, f"comerciais_{base}.csv")
    if os.path.exists(caminho):
        for r in csv.DictReader(open(caminho)):
            linhas.append({
                "metodo": r["compressor"], "parametros": r["opcoes"],
                "bits_por_simbolo": "", "bits_por_byte": r["bits_por_byte"],
                "bytes_saida": r["bytes_saida"],
                "tempo_compressao_s": r["tempo_compressao_s"],
                "tempo_descompressao_s": r["tempo_descompressao_s"],
            })
    if not linhas:
        sys.exit("Nenhum resultado encontrado.")
    saida = os.path.join(RESULTADOS, f"resumo_{base}.csv")
    with open(saida, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(linhas[0].keys()))
        w.writeheader()
        w.writerows(linhas)
    print(f"{'metodo':32s} {'parametros':28s} {'bits/simb':>9s} {'bits/byte':>9s} {'t_comp':>8s} {'t_desc':>8s}")
    for l in linhas:
        print(f"{l['metodo']:32s} {l['parametros']:28s} {l['bits_por_simbolo'][:9]:>9s} "
              f"{l['bits_por_byte'][:9]:>9s} {l['tempo_compressao_s']:>8s} {l['tempo_descompressao_s']:>8s}")
    print(f"\nResumo gravado em {saida}")


# -----------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="Experimentos do trabalho PPM-C")
    sub = ap.add_subparsers(dest="cmd", required=True)

    def comuns(p):
        p.add_argument("arquivo")
        p.add_argument("--alfabetos", type=int, nargs="+", default=[1, 4, 8])
        p.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                       help="execucoes em paralelo (cuidado com a memoria!)")
        p.add_argument("--memoria", type=int, default=None,
                       help="limite de memoria do modelo em MB (opcao -m do ppmc)")
        p.add_argument("--so-compressao", action="store_true",
                       help="nao descomprime (mais rapido, mas sem tempo de descompressao/verificacao)")

    p = sub.add_parser("preparar"); p.add_argument("pasta")
    p.add_argument("--amostra-mb", type=int, default=2)
    p.set_defaults(f=cmd_preparar)

    p = sub.add_parser("ordem"); comuns(p)
    p.add_argument("--kmax-max", type=int, default=None, help="limita o maior Kmax testado")
    p.add_argument("--kmax-a1", type=int, nargs="+", default=None,
                   help="lista de Kmax para A1 (ex.: 0 4 8 12 16 20 24 28 32), para economizar tempo")
    p.set_defaults(f=cmd_ordem)

    p = sub.add_parser("reset"); comuns(p)
    p.add_argument("--j", type=int, nargs="+", default=J_PADRAO)
    p.add_argument("--p", type=float, nargs="+", default=P_PADRAO)
    p.add_argument("--j-a1", type=int, nargs="+", default=None,
                   help="valores de j só para A1 (grade reduzida para o alfabeto mais lento)")
    p.add_argument("--p-a1", type=float, nargs="+", default=None,
                   help="valores de p só para A1")
    p.set_defaults(f=cmd_reset)

    p = sub.add_parser("curvas"); comuns(p)
    p.add_argument("--kmax", type=int, nargs="+", default=None,
                   help="Kmax para cada alfabeto (na mesma ordem de --alfabetos); padrao: o melhor de 'ordem'")
    p.add_argument("--reset", nargs="+", default=None,
                   help="'auto' (melhor j,p de 'reset') ou dois valores: j p")
    p.set_defaults(f=cmd_curvas)

    p = sub.add_parser("resumo"); p.add_argument("arquivo")
    p.set_defaults(f=cmd_resumo)

    a = ap.parse_args()
    a.f(a)


if __name__ == "__main__":
    main()
