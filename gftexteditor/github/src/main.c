/* main.c - Esempio d'uso dell'oggetto GF_TEXTEDITOR
 *
 * Uso: ./gf_edit [file1.c file2.bas ...]
 * (i file passati su riga di comando vengono aperti automaticamente,
 *  uno per ogni area/tab libera, fino a GF_MAX_TABS)
 */
#include "gf_texteditor.h"

int main(int argc, char *argv[])
{
    GF_TEXTEDITOR *MyTextEditor = gf_texteditor_constructor();
    if (!MyTextEditor) return 1;

    int i;
    for (i = 1; i < argc; i++)
        gf_texteditor_open_file(MyTextEditor, argv[i]);

    MyTextEditor->run(MyTextEditor);

    gf_texteditor_destroy(MyTextEditor);
    return 0;
}
