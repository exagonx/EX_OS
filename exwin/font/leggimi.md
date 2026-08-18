# I font di EX-OS

I dodici file `.ttf` di questa directory sono **Liberation 2.1.5**, presi
verbatim da chi li pubblica. Finiscono sull'ISO sotto `/exwin/font/` insieme al
`LICENSE`, e da li' li legge il toolkit.

## Perche' i .ttf COSTRUITI e non i sorgenti

I sorgenti dei font sono file FontForge (`.sfd`): ventisei megabyte che per
diventare `.ttf` vogliono **FontForge piu' Python**.

! **IN GIT SI ENTRA, NON SI ESCE.** Questo repository ha gia' pagato quel
prezzo una volta: `dist/exos-tools.iso` sta dentro otto commit e toglierlo
vorrebbe dire riscrivere ogni hash. La regola scritta nel `.gitignore` — GCC e
la userland di terzi restano fuori dalla cronologia — nasce da li'.

! **E NESSUNO LI RICOSTRUIREBBE MAI.** Aggiungere FontForge ai prerequisiti di
*chiunque* costruisca EX-OS, per rigenerare byte che a monte sono gia'
pubblicati e provati, e' un costo pagato da tutti per un beneficio di nessuno.
I `.ttf` costano un quinto, non aggiungono nessuna dipendenza, e sono l'unico
artefatto che viene davvero usato.

! **E LA OFL FA DELLA COPIA CHE DERIVA UN RISCHIO DI LICENZA**, non solo di
manutenzione. La clausola sul Reserved Font Name dice che una versione
MODIFICATA non si puo' piu' chiamare «Liberation»: una copia dei sorgenti
dentro questo albero invita esattamente a quello — basta una correzione a un
glifo. I file di upstream presi verbatim stanno chiaramente dentro la licenza.

I **sorgenti** dei font andranno sull'ISO DEGLI STRUMENTI, accanto a quelli di
GCC, FreeBASIC e SSH: quella e' l'immagine il cui mestiere e' dare tutto il
necessario per ricostruire EX-OS da dentro EX-OS. Non e' la cronologia di git.

## Perche' dodici facce e non sedici

Tre famiglie per quattro stili: `Regular`, `Bold`, `Italic`, `BoldItalic`.

! **E' ESATTAMENTE CIO' CHE L'HTML CHIEDE**, e non un numero scelto a occhio:
`font-family` generica (`sans-serif`, `serif`, `monospace`) per `font-weight` e
`font-style`. Le quattro facce «Narrow» non corrispondono a niente che una
pagina possa chiedere, e sarebbero mezzo megabyte per sempre.

    Sans    <->  sans-serif   (metrica di Arial / Helvetica)
    Serif   <->  serif        (metrica di Times New Roman)
    Mono    <->  monospace    (metrica di Courier New)

## La versione e' FISSATA, e si controlla

`IMPRONTE.txt` porta la SHA-256 di ognuno dei dodici. Non e' burocrazia: senza,
due macchine che costruiscono l'ISO con due versioni diverse dei font
produrrebbero due immagini diverse **senza dirlo**, e un difetto
d'impaginazione che compare su una sola delle due non avrebbe nessuna
spiegazione visibile.

    cd exwin/font && sha256sum -c IMPRONTE.txt

## Licenza

SIL Open Font License 1.1 — il testo sta in `LICENSE`, gli autori in `AUTHORS`.

! **IL `LICENSE` VA SPEDITO INSIEME AI FONT, ed e' un obbligo della OFL**, non
una cortesia: distribuire i file senza il testo della licenza la viola. Per
questo sta nella stessa directory e finisce sull'ISO con loro, invece che in un
elenco di licenze da qualche altra parte che qualcuno prima o poi dimentica di
copiare.
