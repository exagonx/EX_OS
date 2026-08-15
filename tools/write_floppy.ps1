# =============================================================================
# tools/write_floppy.ps1
# EX-OS - Extensible Operating System
#
# Copyright (C) 2025 Graziano Falcone <exagonx@hotmail.com>
# SPDX-License-Identifier: GPL-2.0-or-later
# =============================================================================
#
# Scrive l'immagine floppy di EX-OS su un drive fisico.
#
# USO - da PowerShell:
#     .\tools\write_floppy.ps1
#     .\tools\write_floppy.ps1 -Drive B: -Yes
#     .\tools\write_floppy.ps1 -Image D:\altra.img
#
# USO - da cmd.exe o doppio clic:
#     tools\write-floppy.cmd
#
# USO - da WSL:
#     ./tools/write_floppy.sh
#
# Non serve aprire una console come Amministratore: lo script si RILANCIA da
# solo con l'elevazione (comparira' la richiesta UAC) in una nuova finestra
# che resta aperta per far leggere l'esito.
#
# Senza argomenti scrive ..\dist\floppy.img (rispetto alla posizione di questo
# script) sull'unita' A:.
#
# ---------------------------------------------------------------------------
# QUESTO FILE DEVE RESTARE IN PURO ASCII: niente accenti, niente trattini
# lunghi, niente virgolette tipografiche.
#
# PowerShell 5.1 legge uno script privo di BOM come Windows-1252, non come
# UTF-8. Un trattino lungo (U+2014, byte E2 80 94) diventa quindi una sequenza
# di TRE caratteri, e l'ultimo di questi e' U+201D: la virgoletta doppia
# tipografica di chiusura, che il parser di PowerShell accetta come
# delimitatore di stringa a tutti gli effetti.
#
# Il risultato e' che le virgolette del resto del file non tornano piu'. In
# questo script bastavano sei trattini lunghi nei commenti perche' il blocco
# Add-Type qui sotto smettesse di essere riconosciuto come here-string, con
# l'errore "Un'istruzione 'using' deve precedere tutte le altre istruzioni"
# segnalato su una riga che non c'entrava nulla.
# ---------------------------------------------------------------------------
#
# COSA VUOL DIRE SCRIVERE SU UN VOLUME GREZZO
#
# Windows tiene il volume montato e ne mette in cache i settori. Scriverci
# sopra senza precauzioni significa che il filesystem montato puo' riscrivere
# i propri metadati sopra i nostri, oppure che la cache restituisce dati
# vecchi alla verifica. Per questo il volume viene prima BLOCCATO
# (FSCTL_LOCK_VOLUME) e poi SMONTATO (FSCTL_DISMOUNT_VOLUME): da quel momento
# e' nostro fino alla chiusura dell'handle.
# =============================================================================

[CmdletBinding()]
param(
    [string] $Image,                    # default: ..\dist\floppy.img
    [string] $Drive = "A:",
    [switch] $Yes,                      # non chiedere conferma
    [switch] $Force,                    # accetta unita' non rimovibili (PERICOLOSO)
    [switch] $NoVerify,
    [switch] $Elevated                  # uso interno: gia' rilanciato come admin
)

$ErrorActionPreference = "Stop"

function Info([string] $m) { Write-Host "[..] $m"     -ForegroundColor Cyan }
function Ok  ([string] $m) { Write-Host "[OK] $m"     -ForegroundColor Green }
function Warn([string] $m) { Write-Host "[AVVISO] $m" -ForegroundColor Yellow }

function Fail([string] $m) {
    Write-Host "[ERRORE] $m" -ForegroundColor Red
    if ($Elevated) {
        Write-Host ""
        Read-Host "Premi INVIO per chiudere"
    }
    exit 1
}

# --- Percorso dell'immagine ---------------------------------------------------
# Risolto PRIMA dell'elevazione: il processo elevato parte da una directory di
# lavoro diversa (di solito system32), quindi un percorso relativo li' dentro
# non troverebbe piu' nulla.

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

if (-not $Image -or $Image -eq "") {
    $Image = Join-Path (Split-Path -Parent $ScriptDir) "dist\floppy.img"
}
if (-not [System.IO.Path]::IsPathRooted($Image)) {
    $Image = Join-Path (Get-Location).Path $Image
}

# --- Controlli che NON richiedono privilegi -----------------------------------
# Fatti PRIMA di elevare: un nome di file sbagliato o una lettera di unita'
# assurda non devono costare all'utente una richiesta UAC per poi scoprire che
# c'era solo un refuso.

if (-not (Test-Path -LiteralPath $Image)) {
    Write-Host "[ERRORE] immagine non trovata: $Image" -ForegroundColor Red
    Write-Host "         Compilala prima con 'make all' (da WSL)." -ForegroundColor Red
    exit 1
}

$Drive = $Drive.TrimEnd('\')
if ($Drive -notmatch '^[A-Za-z]:$') {
    Write-Host "[ERRORE] lettera di unita' non valida: $Drive" -ForegroundColor Red
    exit 1
}

# --- Auto-elevazione ----------------------------------------------------------
# Aprire \\.\A: in scrittura richiede i privilegi di Amministratore. Invece di
# fermarsi con un errore, lo script si rilancia da solo.

$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin   = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    if ($Elevated) {
        Fail "elevazione non riuscita. Apri PowerShell come Amministratore e riprova."
    }

    Write-Host "Richiesta elevazione (UAC): scrivere sul volume grezzo richiede i privilegi di Amministratore." -ForegroundColor Yellow

    $argList = @(
        "-NoExit",                       # la finestra resta aperta per leggere l'esito
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", ('"' + $MyInvocation.MyCommand.Path + '"'),
        "-Image", ('"' + $Image + '"'),
        "-Drive", $Drive,
        "-Elevated"
    )
    if ($Yes)      { $argList += "-Yes" }
    if ($Force)    { $argList += "-Force" }
    if ($NoVerify) { $argList += "-NoVerify" }

    try {
        $p = Start-Process -FilePath "powershell.exe" -ArgumentList $argList -Verb RunAs -PassThru -Wait
        exit $p.ExitCode
    } catch {
        Write-Host "[ERRORE] elevazione rifiutata o non riuscita." -ForegroundColor Red
        exit 1
    }
}

# =============================================================================
# Da qui in poi si gira come Amministratore
# =============================================================================

Write-Host ""
Write-Host "=== EX-OS - scrittura floppy ===" -ForegroundColor Cyan
Write-Host ""

# --- Definizioni Win32 --------------------------------------------------------
if (-not ("ExOsRaw" -as [type])) {
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

public static class ExOsRaw
{
    public const uint GENERIC_READ    = 0x80000000;
    public const uint GENERIC_WRITE   = 0x40000000;
    public const uint FILE_SHARE_READ  = 0x00000001;
    public const uint FILE_SHARE_WRITE = 0x00000002;
    public const uint OPEN_EXISTING   = 3;
    public const uint FILE_FLAG_NO_BUFFERING   = 0x20000000;
    public const uint FILE_FLAG_WRITE_THROUGH  = 0x80000000;

    public const uint FSCTL_LOCK_VOLUME     = 0x00090018;
    public const uint FSCTL_DISMOUNT_VOLUME = 0x00090020;

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern SafeFileHandle CreateFileW(
        string lpFileName, uint dwDesiredAccess, uint dwShareMode,
        IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool DeviceIoControl(
        SafeFileHandle hDevice, uint dwIoControlCode,
        IntPtr lpInBuffer, uint nInBufferSize,
        IntPtr lpOutBuffer, uint nOutBufferSize,
        out uint lpBytesReturned, IntPtr lpOverlapped);
}
"@
}

# --- Controlli sull'immagine --------------------------------------------------

$img = Get-Item -LiteralPath $Image
Info "Immagine : $($img.FullName)"
Info "           $($img.Length) byte"

if ($img.Length -eq 0)       { Fail "l'immagine e' vuota" }
if ($img.Length % 512 -ne 0) { Fail "dimensione ($($img.Length)) non multipla di 512" }
if ($img.Length -ne 1474560) { Warn "non e' la dimensione di un floppy 1.44MB (1474560 byte)" }

# --- Controlli sull'unita' ----------------------------------------------------
# Questa e' la parte che evita di distruggere il disco sbagliato.

$disk = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$Drive'" -ErrorAction SilentlyContinue
if (-not $disk) { Fail "unita' $Drive non trovata. Il floppy e' collegato?" }

Info "Unita'   : $Drive  (DriveType=$($disk.DriveType), Size=$($disk.Size))"

# DriveType 2 = rimovibile. Qualunque altra cosa richiede -Force, e non e' una
# formalita': lo stesso codice puntato su C: lo renderebbe non avviabile.
if ($disk.DriveType -ne 2 -and -not $Force) {
    Fail "$Drive non e' un'unita' rimovibile (DriveType=$($disk.DriveType)). Se sei davvero sicuro, ripeti con -Force."
}

if ($disk.Size -and $disk.Size -gt 0 -and $img.Length -gt $disk.Size) {
    Fail "l'immagine ($($img.Length) byte) non entra nell'unita' ($($disk.Size) byte)"
}

if (-not $Yes) {
    Write-Host ""
    Write-Host "  Tutto il contenuto di $Drive sara' SOVRASCRITTO." -ForegroundColor Yellow
    Write-Host ""
    $risposta = Read-Host "  Procedere? (scrivi SI in maiuscolo)"
    if ($risposta -cne "SI") {
        Write-Host "Annullato." -ForegroundColor Yellow
        if ($Elevated) { Read-Host "Premi INVIO per chiudere" }
        exit 2
    }
}

# --- Scrittura ----------------------------------------------------------------

$path = "\\.\$Drive"
Info "Apertura volume grezzo $path"

# NO_BUFFERING + WRITE_THROUGH: si va sul supporto, senza la cache di Windows
# in mezzo. E' anche il motivo per cui i trasferimenti devono essere multipli
# della dimensione di settore.
$handle = [ExOsRaw]::CreateFileW(
    $path,
    ([ExOsRaw]::GENERIC_READ -bor [ExOsRaw]::GENERIC_WRITE),
    ([ExOsRaw]::FILE_SHARE_READ -bor [ExOsRaw]::FILE_SHARE_WRITE),
    [IntPtr]::Zero,
    [ExOsRaw]::OPEN_EXISTING,
    ([ExOsRaw]::FILE_FLAG_NO_BUFFERING -bor [ExOsRaw]::FILE_FLAG_WRITE_THROUGH),
    [IntPtr]::Zero)

if ($handle.IsInvalid) {
    $err = [Runtime.InteropServices.Marshal]::GetLastWin32Error()
    $spiega = switch ($err) {
        2       { "il dispositivo non esiste" }
        5       { "accesso negato" }
        19      { "supporto protetto da scrittura (linguetta aperta?)" }
        21      { "dispositivo non pronto: disco inserito?" }
        default { "errore Win32 $err" }
    }
    Fail "impossibile aprire $path - $spiega"
}

try {
    $ret = 0

    # Blocca il volume: da qui nessun altro puo' scriverci.
    if (-not [ExOsRaw]::DeviceIoControl($handle, [ExOsRaw]::FSCTL_LOCK_VOLUME,
            [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref] $ret, [IntPtr]::Zero)) {
        Warn "blocco del volume non riuscito: chiudi Esplora risorse e riprova se la verifica fallisce"
    }

    # Smonta: il filesystem montato non deve riscrivere i propri metadati sopra
    # l'immagine appena scritta.
    [void][ExOsRaw]::DeviceIoControl($handle, [ExOsRaw]::FSCTL_DISMOUNT_VOLUME,
            [IntPtr]::Zero, 0, [IntPtr]::Zero, 0, [ref] $ret, [IntPtr]::Zero)

    $stream = New-Object System.IO.FileStream($handle, [System.IO.FileAccess]::ReadWrite)
    $dati   = [System.IO.File]::ReadAllBytes($img.FullName)

    Info "Scrittura di $($dati.Length) byte su $Drive ..."
    $stream.Position = 0
    $stream.Write($dati, 0, $dati.Length)
    $stream.Flush()
    Ok "scrittura completata"

    if (-not $NoVerify) {
        Info "Verifica: rilettura e confronto byte per byte ..."
        $stream.Position = 0
        $letti = New-Object byte[] $dati.Length
        $tot = 0
        while ($tot -lt $dati.Length) {
            $n = $stream.Read($letti, $tot, $dati.Length - $tot)
            if ($n -le 0) { break }
            $tot += $n
        }
        if ($tot -ne $dati.Length) { Fail "riletti solo $tot byte su $($dati.Length)" }

        $diverso = -1
        for ($i = 0; $i -lt $dati.Length; $i++) {
            if ($letti[$i] -ne $dati[$i]) { $diverso = $i; break }
        }
        if ($diverso -ge 0) {
            $sett = [int]($diverso / 512)
            Fail "verifica FALLITA: primo byte diverso all'offset $diverso (settore $sett). Supporto difettoso: prova un altro dischetto."
        }
        Ok "verifica superata: il floppy coincide con l'immagine"
    }

    $stream.Close()
}
catch {
    Fail "errore durante la scrittura: $($_.Exception.Message)"
}
finally {
    if (-not $handle.IsClosed) { $handle.Close() }
}

Write-Host ""
Ok "Floppy $Drive pronto."
Write-Host "     Estrai e reinserisci il disco prima di leggerlo da Windows." -ForegroundColor Gray

if ($Elevated) {
    Write-Host ""
    Read-Host "Premi INVIO per chiudere"
}
exit 0
