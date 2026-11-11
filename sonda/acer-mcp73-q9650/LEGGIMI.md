# Il PC Acer con chipset NVIDIA MCP73 e Core 2 Quad Q9650 (192.168.0.247)

Analisi del 9 novembre 2026, fatta da telnet sulla macchina accesa con
EX-OS 0.318 a 64 bit installato sul disco. Tutto in sola lettura: hwinfo,
disk, mount, ps, dmesg, ipcfg, usb, `hdaudio.drv -i`, `ahci -d`,
`svga.drv -lista`, `nv50.drv -stato`, `sonda.drv -auto` (senza -ponte,
-mmio, -motore). Sulla macchina restano `/hwinfo.txt` e `/SONDA1.TXT`.

E' la stessa scheda madre di `sonda/mb_oem_000` (referti del 31/12/2025,
kernel 0.238 a 32 bit): stesso sottosistema Acer 1025:0137, stesse 27
funzioni PCI. In piu' oggi: una scheda di rete Realtek RTL8169 su PCI
(01:06.0), il disco Hitachi HTS543225L9A300, e la RAM vista per intero.

## I file

| file | cosa c'e' |
|---|---|
| `hwinfo.txt` | processore, processori, memoria, i 28 dispositivi PCI coi BAR e gli IRQ, i dispositivi senza driver |
| `SONDA1.TXT` | i 256 byte di configurazione di ogni funzione PCI, i registri VGA come li ha lasciati il BIOS (800x600x32 VBE) |
| `sistema.txt` | ps, disk (dischi e partizioni), mount, /boot/avvio.sh, /boot/kernel.cfg |
| `dmesg-e-altro.txt` | il registro del kernel dall'avvio, ipcfg, usb, audio, l'aiuto di sonda.drv, mem |
| `sonde-sola-lettura.txt` | hdaudio.drv -i, ahci -d, svga.drv -lista, l'aiuto di hdaudio/ahci/nv50/svga, nv50.drv -stato |

## La macchina

- **Processore**: Intel Core 2 Quad Q9650, 3,0 GHz, famiglia 6 modello 23
  (Penryn/Yorkfield), SSE fino a SSE4.1, 4 processori nella MADT, un I/O
  APIC a 0xfec00000. Col kernel 0.318 i 3 processori in piu' si accendono ma
  non lavorano (dalla 0.319 lavorano per difetto: basta aggiornarla).
- **Memoria**: 3839 MB secondo hwinfo e mem; il registro del kernel dice
  "PMM: RAM massima rilevata 0xbffa0000 (3071 MB)". Da verificare se i
  768 MB sopra i 4 GB (rimappati dal chipset) sono davvero usati.
- **Chipset**: NVIDIA MCP73 (nForce 630i), ponte 10de:07c5.
- **ACPI**: FADT a 0xbffa0200, revisione 1, PM1a_CNT 0x4004, S5 con
  SLP_TYP 7, nessun registro di reset.
- **Disco**: Hitachi HTS543225L9A300 (250 GB) come hd2, sul SATA MCP73 in
  modo RAID/IDE nativo, ext4 + swap 4 GB. Lettore DVD ATAPI DH16A6S come cd0.
- **Rete**: Realtek RTL8169 (01:06.0) in uso con rtl8169.drv,
  192.168.0.247/16. C'e' anche la rete integrata nForce 10de:07dc
  (nforce.drv, nata proprio su questa scheda), non usata.
- **USB**: OHCI 10de:07fe e EHCI 10de:056a, 8 porte. Sull'OHCI c'e' un
  lettore di schede Alcor 058f:9360 senza scheda dentro.
- **Audio**: HD Audio 10de:07fc con codec Realtek ALC888, IRQ 7:
  `hdaudio.drv -i` lo trova (6 uscite, 5 convertitori, 3 ingressi). Non e'
  acceso: `audio -i` lo installa.
- **Video**: NVIDIA 10de:07e5, PMC_BOOT_0 = 0x063000a2, chip 0x63 = C73
  (GeForce 7100 / nForce 630i), famiglia NV4x (Curie). BAR0 0xfd000000
  (registri), BAR1 0xd0000000 (memoria video, il framebuffer VBE),
  BAR3 0xfc000000, IRQ 10. Oggi gira col framebuffer VBE a 800x600x32.

## Cosa manca, in ordine di utilita'

1. **Il video NVIDIA NV4x (C73, 10de:07e5)**: un driver vero, che dia tutta
   la scheda come sis.drv: i modi nativi del monitor e l'accelerazione 2D
   per wserver. ! nv50.drv non serve qui: e' per il display G80 (EVO, a
   0x610000), e su questa scheda quei registri sono tutti zero
   (`nv50.drv -stato`). Il display NV4x e' quello a CRTC (PCRTC 0x600000,
   PRAMDAC 0x680000, i registri VGA estesi CR 19-9F), il 2D quello dei
   vecchi oggetti NV04 (PGRAPH, FIFO). Riferimento con licenza MIT:
   xf86-video-nv (nv_hw.c per i modi NV4x, nv_xaa.c per il 2D).
2. **Il disco col DMA**: oggi il SATA MCP73 va in PIO (kernel/block/ata.c,
   7 ottobre 2026: col bus master del BAR4 il disco restava occupato).
   Il controller ha l'AHCI: `ahci -d` dice CAP 0xa3229f03 (4 porte, 64 bit,
   NCQ), GHC 0 (AHCI spento, il BIOS lo lascia in modo IDE), porta 0 con il
   disco (SSTS 0x123, SATA II), porta 1 col DVD (SSTS 0x113, SATA I).
   La strada c'e' gia' (`ahci`, SYS_AHCI_PASSA): va provata QUI, con
   `ahci -t` e poi `ahci -m FILE` per la velocita', ma tocca il disco di
   sistema: solo con l'utente presente e i dati al sicuro.
3. **FireWire VIA VT6306 (1106:3044, OHCI 1394)**: nessun driver.
   Serve solo se ci sono apparecchi FireWire da collegare.
4. **SMBus MCP73 (10de:07d8)**: nessun driver. Servirebbe per leggere le
   SPD della RAM e i sensori di temperatura e ventole della scheda.
5. **Il "processore" 10de:07da (classe 0b.40)**: e' una funzione
   dell'MCP73 che nemmeno Linux pilota. Non serve un driver: va segnato in
   hwinfo come "(non serve)".

## Difetti di EX-OS visti qui

- `svga.drv -lista` elenca un modo solo, "0x0 0 bit": la lista dei modi VBE
  non arriva (64 bit, kernel 0.318). Da capire se manca in Stage 2 (la
  tabella dei modi a 0xD400) o nella lettura a 64 bit.
- `sonda.drv -rom` fallisce: `mmio_map(0xC0000)` viene rifiutata a 64 bit.
  La ROM video servirebbe per le tabelle dei modi della GeForce 7100.
- La parte VGA di `sonda.drv` legge i registri estesi come se fossero SiS
  (SR05, CR 19 in su): per NVIDIA servono gli indici dei CR estesi NV
  (sblocco con CR1F = 0x57, come fa xf86-video-nv) e i registri MMIO della scheda.
- `mount` mostra /proc come "FAT4294967295" (voce 93, gia' in coda).
- `nv50.drv -avvio` parte a ogni avvio ed esce con codice 1: giusto, ma
  `nv50.drv -remove` toglie la riga inutile.
- La memoria: hwinfo 3839 MB, il registro del kernel 3071 (vedi sopra).
  ! La RAM e' condivisa con la GPU (l'utente): la memoria video integrata
  ne prende una parte, e questo spiega probabilmente la differenza. Da
  rivedere dopo l'aggiornamento del sistema; l'utente forse aggiunge un
  modulo di RAM.

## Aggiornamento dell'11 novembre 2026 (EX-OS 0.326 a 64 bit)

Referti nuovi in `0326/` (stessi comandi, sola lettura): `sistema.txt`
(ver, hwinfo, mem, cpu, disk, mount, ps, ipcfg, usb), `dmesg.txt`,
`sonde-sola-lettura.txt` (hdaudio.drv -i, ahci -d, svga.drv -lista,
nv50.drv -stato).

- **Processori**: 4 al lavoro (`cpu`: APIC 0-3; il registro dice "3 in piu'
  accesi, 3 dei quali lavorano").
- **Memoria**: la domanda del 9/11 e' chiusa. Il PMM conta 3071 MB sotto i
  4 GB (fino a 0xbffa0000) e in piu' "RAM oltre i 4 GB: 768 MB": insieme
  fanno i 3839 MB di hwinfo e mem, e si usano tutti. Ai 4 GB mancano circa
  257 MB: e' quasi certamente la memoria presa dalla GPU (UMA) e dal BIOS,
  che il chipset toglie dalla mappa E820. Non misurato registro per registro.
- **Porta parallela**: nessuna. `lpt.drv -avvia` (anche con -porta:0x378,
  0x278, 0x3BC) non trova un registro dati che rilegga 0xAA e 0x55, e fra i
  dispositivi PCI non c'e' una scheda parallela. Da guardare nel BIOS, o una
  scheda PCI/PCIe in uno slot.
- **telnetd** riparte da solo dopo il riavvio (telnetd 0.013 e
  `/boot/telnetd.cfg` con `avvio = root`, `da = 192.168.0.0/24`).
- Senza driver, come il 9/11: video NVIDIA C73 10de:07e5, FireWire VIA
  1106:3044, SMBus 10de:07d8, e 10de:07da (classe 0b.40, non serve).
  `svga.drv -lista` da' ancora "0x0 0 bit", nv50.drv ancora tutti zero
  (non e' una G80). AHCI come il 9/11: GHC 0, disco su porta 0, DVD su 1.
