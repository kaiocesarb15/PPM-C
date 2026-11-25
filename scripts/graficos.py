#!/usr/bin/env python3
# =============================================================================
# graficos.py — Gera os gráficos do trabalho a partir dos CSVs em resultados/
# =============================================================================
#
#  1) Comprimento médio progressivo (seções 3.2 e 3.3):
#       eixo X: posição n no arquivo (em BYTES, para comparar alfabetos)
#       eixo Y: (total de bits atribuídos até a posição n) / n
#     Um gráfico por alfabeto (bits/símbolo) e um combinado (bits/byte).
#     Se existir a versão "_reset", ela é desenhada junto, com os resets
#     marcados. Se existir dados/fronteiras*.csv, as fronteiras entre os
#     arquivos do Silesia são desenhadas como linhas verticais.
#     O ponto de ESTABILIZAÇÃO é estimado e marcado (ver estabilizacao()).
#
#  2) Bits/símbolo em função de Kmax (a partir de ordem_*.csv).
#
#  3) Mapa de calor de bits/símbolo em função de j e p (reset_*.csv).
#
# Uso: python3 scripts/graficos.py      (gera tudo o que encontrar)
# =============================================================================
import csv
import glob
import os
import re

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(RAIZ, "resultados")
DADOS = os.path.join(RAIZ, "dados")
CORES = {1: "#2a6fdb", 4: "#e07b00", 8: "#1b9e5a"}


def ler_curva(caminho):
    """Lê um CSV de curva. Devolve (posições, médias, resets)."""
    pos, media, resets = [], [], []
    with open(caminho) as f:
        for r in csv.DictReader(f):
            if not r.get("bits_totais"):  # linha incompleta (arquivo ainda sendo escrito)
                continue
            n, bits = int(r["posicao"]), int(r["bits_totais"])
            if r["tipo"] == "P":
                pos.append(n)
                media.append(bits / n)
            else:  # R (reset por taxa) ou M (reset por memória)
                resets.append((n, r["tipo"]))
    return pos, media, resets


def estabilizacao(pos, media, tolerancia=0.01):
    """
    Estima onde a curva "estabiliza": o primeiro ponto a partir do qual a
    média progressiva nunca mais se afasta mais de 'tolerancia' (1%) do
    valor final. Devolve o índice do ponto (ou None).
    """
    if not media:
        return None
    final = media[-1]
    idx = len(media) - 1
    for i in range(len(media) - 1, -1, -1):
        if abs(media[i] - final) > tolerancia * final:
            break
        idx = i
    return idx


def fronteiras_para(arquivo_base):
    """Fronteiras entre arquivos do Silesia, se for o arquivo concatenado."""
    nome = "fronteiras_amostra.csv" if "amostra" in arquivo_base else "fronteiras.csv"
    caminho = os.path.join(DADOS, nome)
    if "silesia" not in arquivo_base or not os.path.exists(caminho):
        return []
    return [(r["arquivo"], int(r["inicio_byte"])) for r in csv.DictReader(open(caminho))]


def desenhar_fronteiras(ax, fronteiras, escala):
    for nome, inicio in fronteiras:
        x = inicio * escala
        ax.axvline(x, color="0.75", lw=0.8, ls="--", zorder=0)
        ax.text(x, 1.0, " " + nome, rotation=90, va="top", ha="left", fontsize=7,
                color="0.4", transform=ax.get_xaxis_transform())


def graficos_curvas():
    # Agrupa os arquivos curva_<base>_a<bits>[_reset].csv por <base>.
    grupos = {}
    for c in glob.glob(os.path.join(RES, "curva_*_a*.csv")):
        m = re.match(r"curva_(.+)_a(\d+)(_reset)?\.csv", os.path.basename(c))
        if m:
            grupos.setdefault(m.group(1), {}).setdefault(int(m.group(2)), {})[bool(m.group(3))] = c

    for base, por_alfabeto in grupos.items():
        fronteiras = fronteiras_para(base)

        # ---- (a) um gráfico por alfabeto, em bits/símbolo ----
        for bits, versoes in sorted(por_alfabeto.items()):
            # Para o Silesia (com fronteiras) desenhamos 2 painéis:
            #   em cima: o comprimento médio progressivo (o que o enunciado pede);
            #   embaixo: a taxa LOCAL (bits entre dois pontos consecutivos da
            #   curva / símbolos entre eles), que mostra melhor o efeito de
            #   cada mudança de arquivo e de cada reset.
            if fronteiras:
                fig, (ax, ax2) = plt.subplots(2, 1, figsize=(10, 7), sharex=True,
                                              gridspec_kw={"height_ratios": [3, 2]})
            else:
                fig, ax = plt.subplots(figsize=(10, 4.5))
                ax2 = None
            simb_por_byte = 8 // bits
            for com_reset in (False, True):
                if com_reset not in versoes:
                    continue
                pos, media, resets = ler_curva(versoes[com_reset])
                if not media:  # arquivo vazio (execução ainda em andamento)
                    continue
                x = [p / simb_por_byte / 1e6 for p in pos]  # posição em MB
                rot = "com reset" if com_reset else "sem reset"
                cor = CORES[bits] if not com_reset else "#222222"  # com reset: preto
                ax.plot(x, media, lw=1.3, label=f"A{bits} {rot} (final {media[-1]:.4f})", color=cor)
                if ax2 is not None:
                    # taxa local entre pontos consecutivos
                    bits_acum = [m * p for m, p in zip(media, pos)]
                    local = [(bits_acum[i] - bits_acum[i - 1]) / (pos[i] - pos[i - 1])
                             for i in range(1, len(pos))]
                    ax2.plot(x[1:], local, lw=0.7, color=cor, alpha=0.85, label=rot)
                if com_reset and resets:
                    rx = [n / simb_por_byte / 1e6 for n, t in resets if t == "R"]
                    alvo = ax2 if ax2 is not None else ax
                    for xr in rx:
                        alvo.axvline(xr, color="#c0392b", lw=0.6, alpha=0.5)
                    alvo.plot([], [], color="#c0392b", lw=0.6, label=f"resets ({len(rx)})")
                if not com_reset and not fronteiras:
                    # Ponto de estabilização (só faz sentido numa fonte "estacionária")
                    # Duas tolerâncias: 5% (fim da fase de aprendizado rápido)
                    # e 1% (curva praticamente plana).
                    for tol, dy in ((0.05, 45), (0.01, 25)):
                        i = estabilizacao(pos, media, tol)
                        if i is not None:
                            ax.axvline(x[i], color=CORES[bits], ls=":", lw=1)
                            ax.annotate(f"±{tol:.0%} do valor final\na partir de ≈ {x[i]:.2f} MB",
                                        (x[i], media[i]), xytext=(10, dy), textcoords="offset points",
                                        fontsize=8, arrowprops=dict(arrowstyle="->", lw=0.8))
            desenhar_fronteiras(ax, [(n, i / 1e6) for n, i in fronteiras], 1)
            ax.set_ylabel("bits atribuídos até n / n\n(bits/símbolo)")
            ax.set_title(f"Comprimento médio progressivo — {base}, alfabeto A{bits}")
            ax.grid(alpha=0.3)
            ax.legend(fontsize=8, loc="upper right")
            if ax2 is not None:
                for _, ini in fronteiras:
                    ax2.axvline(ini / 1e6, color="0.75", lw=0.8, ls="--", zorder=0)
                ax2.set_ylabel("taxa local\n(bits/símbolo)")
                ax2.grid(alpha=0.3)
                ax2.legend(fontsize=8, loc="upper right")
                ax2.set_xlabel("posição n no arquivo (MB)")
            else:
                ax.set_xlabel("posição n no arquivo (MB)")
            fig.tight_layout()
            saida = os.path.join(RES, f"grafico_curva_{base}_a{bits}.png")
            fig.savefig(saida, dpi=150)
            plt.close(fig)
            print("gerado:", saida)

        # ---- (b) os três alfabetos juntos, em bits/byte ----
        fig, ax = plt.subplots(figsize=(10, 4.5))
        for bits, versoes in sorted(por_alfabeto.items()):
            for com_reset in (False, True):
                if com_reset not in versoes:
                    continue
                pos, media, _ = ler_curva(versoes[com_reset])
                if not media:
                    continue
                spb = 8 // bits
                ax.plot([p / spb / 1e6 for p in pos], [m * spb for m in media], lw=1.2,
                        ls="--" if com_reset else "-", color=CORES[bits],
                        label=f"A{bits} {'com' if com_reset else 'sem'} reset")
        desenhar_fronteiras(ax, [(n, i / 1e6) for n, i in fronteiras], 1)
        ax.set_xlabel("posição no arquivo (MB)")
        ax.set_ylabel("comprimento médio progressivo (bits/byte)")
        ax.set_title(f"Comparação entre alfabetos — {base}")
        ax.grid(alpha=0.3)
        ax.legend(fontsize=8, loc="upper right")
        fig.tight_layout()
        saida = os.path.join(RES, f"grafico_curva_{base}_comparacao.png")
        fig.savefig(saida, dpi=150)
        plt.close(fig)
        print("gerado:", saida)


def graficos_ordem():
    for c in glob.glob(os.path.join(RES, "ordem_*.csv")):
        base = os.path.basename(c)[6:-4]
        dados = {}
        for r in csv.DictReader(open(c)):
            dados.setdefault(int(r["alfabeto_bits"]), []).append(
                (int(r["kmax"]), float(r["bits_por_simbolo"]), float(r["bits_por_byte"]),
                 float(r["tempo_compressao_s"])))
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(12, 4.5))
        for bits, linhas in sorted(dados.items()):
            linhas.sort()
            # Kmax em "bytes de contexto" para comparar alfabetos no mesmo eixo
            ks = [k * bits / 8 for k, *_ in linhas]
            ax1.plot(ks, [l[2] for l in linhas], "o-", ms=3, color=CORES[bits], label=f"A{bits}")
            ax2.plot(ks, [l[3] for l in linhas], "o-", ms=3, color=CORES[bits], label=f"A{bits}")
            kb, bps, bpb, _ = min(linhas, key=lambda l: l[1])
            ax1.annotate(f"K={kb}", (kb * bits / 8, bpb), xytext=(0, -14),
                         textcoords="offset points", ha="center", fontsize=8, color=CORES[bits])
        ax1.set_xlabel("tamanho do contexto máximo (Kmax × bits / 8, em bytes)")
        ax1.set_ylabel("bits/byte")
        ax1.set_title(f"Taxa × ordem — {base}")
        ax2.set_xlabel("tamanho do contexto máximo (bytes)")
        ax2.set_ylabel("tempo de compressão (s)")
        ax2.set_title("Tempo × ordem")
        for ax in (ax1, ax2):
            ax.grid(alpha=0.3)
            ax.legend()
        fig.tight_layout()
        saida = os.path.join(RES, f"grafico_ordem_{base}.png")
        fig.savefig(saida, dpi=150)
        plt.close(fig)
        print("gerado:", saida)


def graficos_reset():
    for c in glob.glob(os.path.join(RES, "reset_*.csv")):
        base = os.path.basename(c)[6:-4]
        dados = {}
        for r in csv.DictReader(open(c)):
            dados.setdefault(int(r["alfabeto_bits"]), {})[(int(r["j"]), float(r["p"]))] = \
                (float(r["bits_por_simbolo"]), int(r["kmax"]))
        # Referência sem reset (para mostrar o ganho)
        ref = {}
        cord = os.path.join(RES, f"ordem_{base}.csv")
        if os.path.exists(cord):
            for r in csv.DictReader(open(cord)):
                ref[(int(r["alfabeto_bits"]), int(r["kmax"]))] = float(r["bits_por_simbolo"])

        fig, axes = plt.subplots(1, len(dados), figsize=(5.2 * len(dados), 4.3), squeeze=False)
        for ax, (bits, grade) in zip(axes[0], sorted(dados.items())):
            js = sorted({j for j, _ in grade})
            ps = sorted({p for _, p in grade})
            m = [[grade.get((j, p), (float("nan"), 0))[0] for j in js] for p in ps]
            im = ax.imshow(m, cmap="viridis_r", aspect="auto", origin="lower")
            ax.set_xticks(range(len(js)), [f"{j//1000}k" for j in js])
            ax.set_yticks(range(len(ps)), [f"{p:g}%" for p in ps])
            for yi, linha in enumerate(m):
                for xi, v in enumerate(linha):
                    ax.text(xi, yi, f"{v:.3f}", ha="center", va="center", fontsize=6.5, color="w")
            kmax = next(iter(grade.values()))[1]
            titulo = f"A{bits} (Kmax={kmax})"
            if (bits, kmax) in ref:
                titulo += f" — sem reset: {ref[(bits, kmax)]:.3f}"
            ax.set_title(titulo, fontsize=9)
            ax.set_xlabel("janela j (símbolos)")
            ax.set_ylabel("percentual p")
            fig.colorbar(im, ax=ax, label="bits/símbolo")
        fig.suptitle(f"Estratégia de reset — {base}")
        fig.tight_layout()
        saida = os.path.join(RES, f"grafico_reset_{base}.png")
        fig.savefig(saida, dpi=150)
        plt.close(fig)
        print("gerado:", saida)


if __name__ == "__main__":
    graficos_curvas()
    graficos_ordem()
    graficos_reset()
