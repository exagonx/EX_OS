#!/usr/bin/env python3
# =============================================================================
# tools/doc-installa-usb.py — la guida «installare senza lettore CD», in
# italiano e in inglese, HTML e testo, da UNA sorgente (7 ottobre 2026)
#
#     python3 tools/doc-installa-usb.py
#
# Scrive:
#     exwin/doc/installa-usb.html      exwin/doc/installa-usb.en.html
#     INSTALLA-USB.txt                 INSTALLA-USB.en.txt
#
# ! QUATTRO FILE SCRITTI A MANO DIVERGONO ALLA PRIMA CORREZIONE: chi cambia un
# comando lo cambia in uno e se ne dimentica tre. Qui il testo sta una volta
# per lingua, e le due forme (pagina e testo) escono dallo stesso elenco.
#
# ! SOLO ASCII, come il resto della documentazione di EX-OS: apostrofi al posto
# degli accenti.
#
# Il contenuto e' un elenco di blocchi:
#     ("h", titolo)            un capitolo
#     ("p", testo)             un paragrafo
#     ("c", [righe])           comandi da battere, cosi' come sono
#     ("l", [voci])            un elenco puntato
#     ("n", testo)             una nota: cosa sapere, cosa puo' andare storto
# =============================================================================
import html
import os
import textwrap

RADICE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

IT = {
    "titolo": "Installare EX-OS senza lettore CD",
    "sotto": "Da una chiavetta USB, con il resto del sistema dalla rete",
    "aggiornato": "Kernel 0.245, 9 ottobre 2026",
    "altra": ("installa-usb.en.html", "English"),
    "blocchi": [
        ("h", "A cosa serve"),
        ("p", "Questa guida porta EX-OS sul disco rigido di un PC che non ha ne' "
              "lettore di floppy ne' lettore CD. Si avvia il PC da una chiavetta "
              "USB che contiene un sistema piccolissimo (1,44 MB, caricato tutto "
              "in memoria), con quello si prepara il disco e ci si installa un "
              "sistema minimo, e da li' il resto arriva dalla rete."),
        ("h", "Che cosa serve"),
        ("l", ["un PC con Linux e i sorgenti di EX-OS, per costruire l'immagine;",
               "una chiavetta USB qualunque: VIENE CANCELLATA;",
               "il PC di destinazione, capace di avviarsi da USB;",
               "un cavo di rete verso un router che da' gli indirizzi (DHCP);",
               "un server con il sistema pubblicato (quello di "
               "./exagonx/repo-update.sh), per il passo 6."]),
        ("n", "Su una chiavetta l'immagine occupa 1,44 MB: il resto dello spazio "
              "non si usa. Tenete un'altra chiavetta, formattata FAT32, se volete "
              "portarvi via dei file dal PC."),

        ("h", "1. Costruire l'immagine"),
        ("p", "Sul PC con Linux, nella directory dei sorgenti:"),
        ("c", ["make installa"]),
        ("p", "Esce dist/installa.img. Dentro ci sono il kernel, la shell, gli "
              "attrezzi del disco (disk, fdisk, mkfs, install), la rete "
              "(netdetect, lo stack IP, dhcp e i driver nforce, e1000, ne2k, "
              "pcnet) e netupdate."),

        ("h", "2. Scriverla sulla chiavetta"),
        ("p", "Infilate la chiavetta e guardate come si chiama:"),
        ("c", ["lsblk -d -o NAME,SIZE,RM,MODEL"]),
        ("p", "La chiavetta e' la riga con RM = 1 e la misura giusta, per esempio "
              "sdc. Poi, mettendo il nome vero al posto di sdX:"),
        ("c", ["sudo dd if=dist/installa.img of=/dev/sdX bs=512 conv=fsync",
               "sync"]),
        ("n", "ATTENZIONE: dd scrive dove gli si dice, senza chiedere. Un nome "
              "sbagliato e' il disco di qualcun altro cancellato. Controllate "
              "due volte."),

        ("h", "3. Avviare il PC dalla chiavetta"),
        ("p", "Infilate la chiavetta nel PC di destinazione, accendetelo e "
              "scegliete l'avvio da USB (di solito F12, F8 o Esc all'accensione, "
              "oppure dall'ordine di avvio nel BIOS)."),
        ("p", "Scorrono le righe dell'avvio, poi il sistema accende da solo "
              "l'USB e la rete e stampa le istruzioni. In mezzo deve comparire:"),
        ("c", ["VFS: root '/' sul volume in RAM (FAT12)"]),
        ("n", "Da questo momento la chiavetta non serve piu' al sistema: tutto "
              "sta in memoria. Quel che scrivete in / sparisce allo spegnimento."),

        ("h", "4. Preparare il disco"),
        ("p", "Guardate quali dischi vede il kernel:"),
        ("c", ["disk"]),
        ("p", "Il disco rigido e' hd0, hd1, hd2 o hd3. Negli esempi e' hd0: usate "
              "il nome che vedete voi. Poi create la partizione:"),
        ("c", ["fdisk hd0"]),
        ("p", "Dentro fdisk i comandi sono lettere. Su un disco vuoto, in ordine: "
              "n crea una partizione, t ne imposta il tipo (83), a la rende "
              "avviabile, p mostra il risultato, w scrive sul disco (chiede "
              "conferma), q esce. Niente tocca il disco prima di w."),
        ("n", "ATTENZIONE: w cancella la tabella delle partizioni che c'era. Se "
              "sul disco c'e' qualcosa che vi serve, fermatevi qui."),
        ("p", "Formattate la partizione e montatela:"),
        ("c", ["mkfs -t ext2 -L exos hd0p1",
               "mount hd0p1 /disk"]),
        ("p", "mkfs chiede conferma: si risponde si."),

        ("h", "5. Installare"),
        ("c", ["install -t /disk"]),
        ("p", "L'installatore chiede la lingua, la password di root, un nome "
              "utente con la sua password, e qualche conferma. Copia sul disco "
              "il sistema che sta girando e lo rende avviabile. Alla fine dice:"),
        ("p", "Se il controller del disco sa parlare AHCI ma il BIOS lo "
              "presenta come IDE, l'installatore lo dice in una sezione "
              "Disco e passa all'AHCI prima di copiare: la copia va molto "
              "piu' svelta, e nel kernel.cfg installato scrive ahci = 1 "
              "perche' succeda a ogni avvio. Se il passaggio non riesce il "
              "disco resta com'era."),
        ("c", ["Installazione completata. Togli il floppy e riavvia."]),
        ("p", "Spegnete, togliete la chiavetta e riaccendete: il PC parte dal "
              "disco."),
        ("c", ["shutdown"]),

        ("h", "6. Dal disco: la rete e il resto del sistema"),
        ("p", "Il sistema appena installato e' piccolo: ha la rete e netupdate, "
              "non la grafica. La prima volta la rete si accende a mano, in "
              "quest'ordine:"),
        ("c", ["/dev/pci.drv &",
               "netdetect -c",
               "/dev/ip.drv &",
               "dhcp",
               "ipcfg"]),
        ("p", "ipcfg mostra l'indirizzo preso. Se un anello manca, ipcfg dice "
              "quale e che cosa battere. Poi:"),
        ("c", ["netupdate"]),
        ("p", "La prima volta netupdate chiede come collegarsi (http e' la "
              "scelta semplice) e l'indirizzo del server senza http://, per "
              "esempio esempio.org/exos/netinst. Poi scarica e installa il resto "
              "del sistema."),

        ("h", "Se qualcosa non va"),
        ("l", ["Il disco non compare in disk: servono le righe dell'avvio che "
               "cominciano con ATA:. Un controller SATA in modo AHCI non e' "
               "ancora gestito: nel BIOS, se c'e' la scelta, mettete IDE o "
               "Compatibile.",
               "Errori «ATA: DMA» o «timeout BSY»: li dava il kernel 0.239 sui "
               "controller SATA in modo nativo. Dalla 0.240 quei controller "
               "lavorano in PIO, piu' lento ma senza quegli errori: rifate "
               "l'immagine con i sorgenti aggiornati.",
               "Installato, ma riavviando dal disco il sistema non trova il "
               "disco: succedeva fino al kernel 0.240, che cercava la radice "
               "solo su hd0. Rifate la chiavetta con i sorgenti aggiornati, "
               "avviate da li', poi mount hd0p1 /disk (col nome che vedete in "
               "disk) e install -a /disk, che aggiorna il kernel sul disco.",
               "La rete non prende l'indirizzo: netdetect dice che scheda vede "
               "e con quale driver. Per una NVIDIA nForce, nforce.drv -d stampa "
               "i registri, lo stato del PHY (link SU o GIU') e i conteggi dei "
               "frame inviati e ricevuti: sono le righe da mandare.",
               "Per portarsi via un file: infilate una chiavetta FAT32, "
               "ls /USB dice come si chiama (di solito HDD0p1), poi "
               "cp /file /USB/HDD0p1/ e shutdown.",
               "La tastiera scrive segni sbagliati: il dischetto nasce con la "
               "tastiera italiana. keymap us la cambia."]),

        ("h", "Che cosa e' provato e che cosa no"),
        ("l", ["In QEMU: tutta la procedura, dall'avvio in memoria al riavvio "
               "dal disco, con una scheda e1000 e un disco IDE.",
               "Su un PC vero (NVIDIA MCP73, Core 2 Quad): l'avvio dalla "
               "chiavetta, il disco SATA, l'installazione e l'avvio dal disco; "
               "la rete con una scheda RTL8169SC aggiunta dal supporto. La "
               "rete integrata (nforce 0.008) prende l'indirizzo all'avvio."]),
        ("h", "I driver che mancano: il supporto"),
        ("p", "Il dischetto non ha posto per tutti i driver, e il sistema che "
              "installa ne ha pochi. Gli altri si aggiungono dopo, dal "
              "supporto: sul PC di sviluppo make support riempie "
              "dist/support e ne fa anche dist/support.iso."),
        ("l", ["Copiate il CONTENUTO di dist/support (aggiungi, dev, bin) su "
               "una chiavetta FAT32, oppure masterizzate dist/support.iso.",
               "Sul PC, avviato dal disco. Se la chiavetta non compare da "
               "sola in ls /USB, tre comandi la accendono: /dev/pci.drv & "
               "poi /dev/ehci.drv -avvio & poi automount &",
               "Lanciate /USB/HDD0p1/aggiungi (o /cdrom/aggiungi dal CD). "
               "Copia i driver in /dev e i programmi in /bin senza chiedere "
               "niente, e aggiunge a /boot/autoexec.sh le righe che accendono "
               "chiavette e rete, se mancano.",
               "shutdown, togliete il supporto, riaccendete: netdetect -c "
               "trova la scheda di rete e dhcp prende l'indirizzo.",
               "Con due schede di rete vince quella aggiunta, non quella "
               "integrata. netdetect -c rtl8169 sceglie a mano."]),
    ],
}

EN = {
    "titolo": "Installing EX-OS without a CD drive",
    "sotto": "From a USB stick, with the rest of the system from the network",
    "aggiornato": "Kernel 0.245, 9 October 2026",
    "altra": ("installa-usb.html", "Italiano"),
    "blocchi": [
        ("h", "What it is for"),
        ("p", "This guide puts EX-OS on the hard disk of a PC that has neither a "
              "floppy drive nor a CD drive. The PC is booted from a USB stick "
              "holding a very small system (1.44 MB, loaded entirely into "
              "memory); with it the disk is prepared and a minimal system is "
              "installed, and from there the rest comes from the network."),
        ("h", "What you need"),
        ("l", ["a PC with Linux and the EX-OS sources, to build the image;",
               "any USB stick: IT IS ERASED;",
               "the target PC, able to boot from USB;",
               "a network cable to a router that hands out addresses (DHCP);",
               "a server with the published system (the one made by "
               "./exagonx/repo-update.sh), for step 6."]),
        ("n", "The image takes 1.44 MB of the stick: the rest of its space is "
              "not used. Keep another stick, formatted FAT32, if you want to "
              "take files away from the PC."),

        ("h", "1. Build the image"),
        ("p", "On the Linux PC, in the source directory:"),
        ("c", ["make installa"]),
        ("p", "This produces dist/installa.img. It holds the kernel, the shell, "
              "the disk tools (disk, fdisk, mkfs, install), the network "
              "(netdetect, the IP stack, dhcp and the nforce, e1000, ne2k and "
              "pcnet drivers) and netupdate."),

        ("h", "2. Write it to the stick"),
        ("p", "Plug the stick in and see what it is called:"),
        ("c", ["lsblk -d -o NAME,SIZE,RM,MODEL"]),
        ("p", "The stick is the line with RM = 1 and the right size, for example "
              "sdc. Then, with the real name in place of sdX:"),
        ("c", ["sudo dd if=dist/installa.img of=/dev/sdX bs=512 conv=fsync",
               "sync"]),
        ("n", "WARNING: dd writes where it is told, without asking. A wrong "
              "name is somebody else's disk erased. Check twice."),

        ("h", "3. Boot the PC from the stick"),
        ("p", "Plug the stick into the target PC, switch it on and choose to "
              "boot from USB (usually F12, F8 or Esc at power-on, or the boot "
              "order in the BIOS)."),
        ("p", "The boot lines scroll by, then the system starts USB and the "
              "network by itself and prints the instructions. Among the lines "
              "there must be:"),
        ("c", ["VFS: root '/' sul volume in RAM (FAT12)"]),
        ("n", "From this moment the system no longer needs the stick: "
              "everything is in memory. What you write in / is gone at "
              "power-off."),

        ("h", "4. Prepare the disk"),
        ("p", "See which disks the kernel sees:"),
        ("c", ["disk"]),
        ("p", "The hard disk is hd0, hd1, hd2 or hd3. The examples use hd0: use "
              "the name you see. Then create the partition:"),
        ("c", ["fdisk hd0"]),
        ("p", "Inside fdisk the commands are letters. On an empty disk, in "
              "order: n creates a partition, t sets its type (83), a makes it "
              "bootable, p shows the result, w writes to the disk (it asks for "
              "confirmation), q quits. Nothing touches the disk before w."),
        ("n", "WARNING: w erases the partition table that was there. If the "
              "disk holds something you need, stop here."),
        ("p", "Format the partition and mount it:"),
        ("c", ["mkfs -t ext2 -L exos hd0p1",
               "mount hd0p1 /disk"]),
        ("p", "mkfs asks for confirmation: the answer is si (Italian for yes)."),

        ("h", "5. Install"),
        ("c", ["install -t /disk"]),
        ("p", "The installer asks for the language, the root password, a user "
              "name with its password, and a few confirmations. It copies the "
              "running system to the disk and makes it bootable. At the end it "
              "says:"),
        ("p", "If the disk controller can speak AHCI but the BIOS presents it "
              "as IDE, the installer says so in a section called Disco and "
              "moves to AHCI before copying: the copy is much faster, and it "
              "writes ahci = 1 in the installed kernel.cfg so that it happens "
              "at every boot. If the switch fails the disk stays as it was."),
        ("c", ["Installazione completata. Togli il floppy e riavvia."]),
        ("p", "Shut down, remove the stick and switch on again: the PC boots "
              "from the disk."),
        ("c", ["shutdown"]),

        ("h", "6. From the disk: the network and the rest of the system"),
        ("p", "The freshly installed system is small: it has the network and "
              "netupdate, not the graphics. The first time the network is "
              "started by hand, in this order:"),
        ("c", ["/dev/pci.drv &",
               "netdetect -c",
               "/dev/ip.drv &",
               "dhcp",
               "ipcfg"]),
        ("p", "ipcfg shows the address obtained. If a link of the chain is "
              "missing, ipcfg says which one and what to type. Then:"),
        ("c", ["netupdate"]),
        ("p", "The first time netupdate asks how to connect (http is the simple "
              "choice) and the server address without http://, for example "
              "example.org/exos/netinst. Then it downloads and installs the "
              "rest of the system."),

        ("h", "If something goes wrong"),
        ("l", ["The disk does not show in disk: the boot lines starting with "
               "ATA: are needed. A SATA controller in AHCI mode is not handled "
               "yet: in the BIOS, if there is a choice, pick IDE or Compatible.",
               "«ATA: DMA» or «timeout BSY» errors: kernel 0.239 gave them on "
               "SATA controllers in native mode. From 0.240 those controllers "
               "work in PIO, slower but without those errors: rebuild the "
               "image from updated sources.",
               "Installed, but on rebooting from the disk the system cannot "
               "find the disk: this happened up to kernel 0.240, which looked "
               "for its root on hd0 only. Rewrite the stick from updated "
               "sources, boot from it, then mount hd0p1 /disk (with the name "
               "you see in disk) and install -a /disk, which updates the "
               "kernel on the disk.",
               "The network gets no address: netdetect says which card it sees "
               "and with which driver. For an NVIDIA nForce, nforce.drv -d "
               "prints the registers, the state of the PHY (link SU = up, "
               "GIU' = down) and the counts of frames sent and received: "
               "those are the lines to send.",
               "To take a file away: plug in a FAT32 stick, ls /USB says what "
               "it is called (usually HDD0p1), then cp /file /USB/HDD0p1/ and "
               "shutdown.",
               "The keyboard types wrong symbols: the image starts with the "
               "Italian keyboard. keymap us changes it."]),

        ("h", "What is tested and what is not"),
        ("l", ["In QEMU: the whole procedure, from booting in memory to "
               "rebooting from the disk, with an e1000 card and an IDE disk.",
               "On a real PC (NVIDIA MCP73, Core 2 Quad): booting from the "
               "stick, the SATA disk, installing and starting from the disk; "
               "the network with an RTL8169SC card added from the support "
               "medium. The built-in network (nforce 0.008) gets its address at boot."]),
        ("h", "The missing drivers: the support medium"),
        ("p", "The floppy has no room for every driver, and the system it "
              "installs has few. The others are added afterwards from the "
              "support medium: on the development PC make support fills "
              "dist/support and also builds dist/support.iso."),
        ("l", ["Copy the CONTENT of dist/support (aggiungi, dev, bin) to a "
               "FAT32 stick, or burn dist/support.iso.",
               "On the PC, started from the disk. If the stick does not show "
               "up by itself in ls /USB, three commands start it: "
               "/dev/pci.drv & then /dev/ehci.drv -avvio & then automount &",
               "Run /USB/HDD0p1/aggiungi (or /cdrom/aggiungi from the CD). It "
               "copies the drivers to /dev and the programs to /bin without "
               "asking, and adds to /boot/autoexec.sh the lines that start "
               "sticks and network, if missing.",
               "shutdown, remove the medium, power on: netdetect -c finds "
               "the network card and dhcp gets the address.",
               "With two network cards the added one wins, not the built-in "
               "one. netdetect -c rtl8169 chooses by hand."]),
    ],
}


def ascii_solo(s):
    return (s.replace("«", '"').replace("»", '"'))


def in_html(d, lingua):
    e = lambda s: html.escape(ascii_solo(s), quote=False)
    nav = ('<p class="navi">\n<a href="index.html">Documentazione</a> |\n'
           '<a href="installa.html">Installazione</a> |\n'
           '<a href="%s">%s</a>\n</p>\n' % d["altra"])
    out = ['<!DOCTYPE html>', '<html lang="%s">' % lingua, '<head>',
           '<meta charset="utf-8">',
           '<title>%s &mdash; EX-OS</title>' % e(d["titolo"]),
           '<link rel="stylesheet" href="stile.css">',
           '<!-- Copyright (C) 2026 Graziano Falcone <exagonx@hotmail.com>  GPL-2.0-or-later',
           '     ! NON SI SCRIVE A MANO: lo genera tools/doc-installa-usb.py, insieme',
           '     alla versione nell\'altra lingua e alle due in testo. SOLO ASCII. -->',
           '</head>', '<body>', '', nav,
           '<h1>%s</h1>' % e(d["titolo"]),
           '<p><i>%s. %s.</i></p>' % (e(d["sotto"]), e(d["aggiornato"])), '']
    for tipo, c in d["blocchi"]:
        if tipo == "h":
            out += ['<h2>%s</h2>' % e(c), '']
        elif tipo == "p":
            out += ['<p>', textwrap.fill(e(c), 78), '</p>', '']
        elif tipo == "c":
            out += ['<pre>'] + ['    ' + e(r) for r in c] + ['</pre>', '']
        elif tipo == "l":
            out += ['<ul>'] + ['<li>%s</li>' % textwrap.fill(e(v), 76) for v in c] + ['</ul>', '']
        elif tipo == "n":
            out += ['<p class="nota">', textwrap.fill(e(c), 78), '</p>', '']
    out += ['</body>', '</html>', '']
    return "\n".join(out)


def in_testo(d):
    riga = "=" * 78
    out = [riga, " EX-OS - " + ascii_solo(d["titolo"]).upper(), riga, "",
           textwrap.fill(ascii_solo(d["sotto"]) + ". " + d["aggiornato"] + ".", 78), ""]
    for tipo, c in d["blocchi"]:
        if tipo == "h":
            out += ["", "-" * 78, ascii_solo(c).upper(), "-" * 78, ""]
        elif tipo == "p":
            out += [textwrap.fill(ascii_solo(c), 78), ""]
        elif tipo == "c":
            out += ["    " + ascii_solo(r) for r in c] + [""]
        elif tipo == "l":
            for v in c:
                out += [textwrap.fill(ascii_solo(v), 78, initial_indent="  - ",
                                      subsequent_indent="    ")]
            out += [""]
        elif tipo == "n":
            out += [textwrap.fill(ascii_solo(c), 78, initial_indent="  ! ",
                                  subsequent_indent="    "), ""]
    return "\n".join(out) + "\n"


def scrivi(percorso, testo):
    testo.encode("ascii")           # se non e' ASCII, meglio saperlo qui
    with open(os.path.join(RADICE, percorso), "w") as f:
        f.write(testo)
    print("scritto", percorso)


if __name__ == "__main__":
    scrivi("exwin/doc/installa-usb.html", in_html(IT, "it"))
    scrivi("exwin/doc/installa-usb.en.html", in_html(EN, "en"))
    scrivi("INSTALLA-USB.txt", in_testo(IT))
    scrivi("INSTALLA-USB.en.txt", in_testo(EN))
