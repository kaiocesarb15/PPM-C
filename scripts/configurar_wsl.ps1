# =============================================================================
# configurar_wsl.ps1 — Prepara o Windows 11 para rodar os experimentos
# -----------------------------------------------------------------------------
# Rode no PowerShell COMO ADMINISTRADOR:
#     powershell -ExecutionPolicy Bypass -File scripts\configurar_wsl.ps1
#
# O que ele faz:
#   1) Cria o arquivo %USERPROFILE%\.wslconfig liberando ~90% da RAM e todos
#      os núcleos para o WSL (por padrão o WSL só usa metade da RAM).
#   2) Instala o WSL2 com Ubuntu, se ainda não estiver instalado.
#      -> Nesse caso, REINICIE o computador quando ele terminar.
# =============================================================================

$ErrorActionPreference = "Stop"

# --- 1) Quanto de RAM e quantos núcleos a máquina tem ---
$ramGB  = [math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
$nucleos = (Get-CimInstance Win32_Processor | Measure-Object -Property NumberOfLogicalProcessors -Sum).Sum
$memWSL = [math]::Max(4, [math]::Floor($ramGB * 0.9))

$config = "[wsl2]`r`nmemory=${memWSL}GB`r`nprocessors=$nucleos`r`n"
$arquivo = Join-Path $env:USERPROFILE ".wslconfig"
Set-Content -Path $arquivo -Value $config -Encoding ascii
Write-Host "RAM total: $ramGB GB | nucleos: $nucleos"
Write-Host "Criado $arquivo com memory=${memWSL}GB e processors=$nucleos"

# --- 2) WSL + Ubuntu ---
$temUbuntu = $false
try {
    $distros = (wsl.exe -l -q 2>$null) -replace "`0", ""
    if ($distros -match "Ubuntu") { $temUbuntu = $true }
} catch { }

if ($temUbuntu) {
    wsl.exe --shutdown   # aplica o novo .wslconfig
    Write-Host ""
    Write-Host "Ubuntu (WSL) ja instalado. Configuracao aplicada."
    Write-Host "Abra o 'Ubuntu' no menu Iniciar e siga o README (secao Windows)."
} else {
    Write-Host ""
    Write-Host "Instalando WSL2 + Ubuntu (pode demorar alguns minutos)..."
    wsl.exe --install -d Ubuntu
    Write-Host ""
    Write-Host "PRONTO. Reinicie o computador."
    Write-Host "Depois abra o 'Ubuntu' no menu Iniciar, crie usuario e senha,"
    Write-Host "e siga o README (secao Windows)."
}
