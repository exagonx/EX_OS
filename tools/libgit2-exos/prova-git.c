/* =============================================================================
 * tools/libgit2-exos/prova-git.c — libgit2 inside EX-OS, step by step
 *
 *     prova-git DIR          works in DIR/repo and DIR/clone (DIR writable)
 *
 * Each step prints "ok N ..." or "NO N ...: <libgit2's error>"; the last line
 * says how many passed. Built by tools/libgit2-exos/prova.sh with
 * x86_64-exos-gcc -static and libgit2.a, libssl.a, libcrypto.a.
 * ============================================================================= */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <git2.h>

static int g_ok, g_n;

static void passo(int rc, const char *cosa)
{
    g_n++;
    if (rc >= 0) {
        g_ok++;
        printf("ok %d %s\n", g_n, cosa);
    } else {
        const git_error *e = git_error_last();

        printf("NO %d %s: %d %s\n", g_n, cosa, rc, e && e->message ? e->message : "?");
    }
    fflush(stdout);
}

static int scrivi(const char *p, const char *testo)
{
    FILE *f = fopen(p, "w");

    if (!f) return -1;
    fputs(testo, f);
    fclose(f);
    return 0;
}

/* One commit of everything in the work tree, on HEAD. */
static int committa(git_repository *r, const char *msg, git_oid *out)
{
    git_index     *ix = 0;
    git_oid        albero_id;
    git_tree      *albero = 0;
    git_signature *io = 0;
    git_commit    *padre = 0;
    git_oid        padre_id;
    int            rc;

    rc = git_repository_index(&ix, r);
    if (rc == 0) rc = git_index_add_all(ix, 0, 0, 0, 0);
    if (rc == 0) rc = git_index_write(ix);
    if (rc == 0) rc = git_index_write_tree(&albero_id, ix);
    if (rc == 0) rc = git_tree_lookup(&albero, r, &albero_id);
    if (rc == 0) rc = git_signature_new(&io, "EX-OS", "prova@ex-os", 1792137600, 120);
    if (rc == 0) {
        int ha_padre = git_reference_name_to_id(&padre_id, r, "HEAD") == 0;
        if (ha_padre) rc = git_commit_lookup(&padre, r, &padre_id);
        if (rc == 0)
            rc = git_commit_create_v(out, r, "HEAD", io, io, 0, msg, albero,
                                     padre ? 1 : 0, padre);
    }
    git_commit_free(padre);
    git_signature_free(io);
    git_tree_free(albero);
    git_index_free(ix);
    return rc;
}

static int conta_stati(git_repository *r, unsigned int *quanti)
{
    git_status_list *l = 0;
    int rc = git_status_list_new(&l, r, 0);

    if (rc == 0) *quanti = (unsigned int)git_status_list_entrycount(l);
    git_status_list_free(l);
    return rc;
}

int main(int argc, char **argv)
{
    char            p_repo[256], p_clone[256], p[300], buf[64];
    git_repository *r = 0, *c = 0;
    git_oid         primo, secondo;
    git_revwalk    *w = 0;
    git_reference  *ramo = 0;
    git_odb        *odb = 0;
    git_oid         blob_id;
    int             maj, min, rev, rc, n;
    unsigned int    stati = 99;

    if (argc < 2) { printf("uso: prova-git DIR\n"); return 2; }
    snprintf(p_repo, sizeof(p_repo), "%s/repo", argv[1]);
    snprintf(p_clone, sizeof(p_clone), "%s/clone", argv[1]);

    passo(git_libgit2_init(), "git_libgit2_init");
    git_libgit2_version(&maj, &min, &rev);
    n = git_libgit2_features();
    printf("   libgit2 %d.%d.%d, threads %s, https %s, ssh %s\n", maj, min, rev,
           n & GIT_FEATURE_THREADS ? "si" : "no", n & GIT_FEATURE_HTTPS ? "si" : "no",
           n & GIT_FEATURE_SSH ? "si" : "no");

    passo(git_repository_init(&r, p_repo, 0), "git init");
    snprintf(p, sizeof(p), "%s/leggimi.txt", p_repo);
    passo(scrivi(p, "EX-OS e libgit2\nprima riga\n"), "scrive leggimi.txt");
    snprintf(p, sizeof(p), "%s/sotto", p_repo);
    passo(mkdir(p, 0755), "mkdir sotto");
    snprintf(p, sizeof(p), "%s/sotto/dati.txt", p_repo);
    passo(scrivi(p, "1 2 3\n"), "scrive sotto/dati.txt");
    passo(committa(r, "primo commit\n", &primo), "primo commit (index, tree, commit)");
    git_oid_tostr(buf, sizeof(buf), &primo);
    printf("   commit %s\n", buf);

    passo(conta_stati(r, &stati) == 0 && stati == 0 ? 0 : -1, "status pulito dopo il commit");

    snprintf(p, sizeof(p), "%s/leggimi.txt", p_repo);
    scrivi(p, "EX-OS e libgit2\nprima riga\nseconda riga\n");
    passo(conta_stati(r, &stati) == 0 && stati == 1 ? 0 : -1, "status: un file cambiato");
    passo(committa(r, "secondo commit\n", &secondo), "secondo commit");

    rc = git_revwalk_new(&w, r);
    if (rc == 0) rc = git_revwalk_push_head(w);
    n = 0;
    if (rc == 0) {
        git_oid o;
        while (git_revwalk_next(&o, w) == 0) n++;
    }
    passo(rc == 0 && n == 2 ? 0 : -1, "log: due commit dalla HEAD");
    git_revwalk_free(w);

    {
        git_commit *cm = 0;
        rc = git_commit_lookup(&cm, r, &primo);
        if (rc == 0) rc = git_branch_create(&ramo, r, "prova", cm, 0);
        git_commit_free(cm);
        passo(rc, "branch prova sul primo commit");
        git_reference_free(ramo);
    }

    rc = git_repository_odb(&odb, r);
    if (rc == 0) rc = git_odb_write(&blob_id, odb, "ciao\n", 5, GIT_OBJECT_BLOB);
    git_oid_tostr(buf, sizeof(buf), &blob_id);
    passo(rc == 0 && strcmp(buf, "887ae9333d92a1d72400c210546e28baa1050e44") == 0 ? 0 : -1,
          "hash-object \"ciao\\n\" = 887ae93 (come git)");
    git_odb_free(odb);

    {
        git_clone_options co = GIT_CLONE_OPTIONS_INIT;
        rc = git_clone(&c, p_repo, p_clone, &co);
        passo(rc, "clone locale");
    }
    if (c) {
        FILE *f;
        char  riga[128] = "";

        snprintf(p, sizeof(p), "%s/leggimi.txt", p_clone);
        f = fopen(p, "r");
        if (f) {
            fgets(riga, sizeof(riga), f);
            fgets(riga, sizeof(riga), f);
            fgets(riga, sizeof(riga), f);
            fclose(f);
        }
        passo(strcmp(riga, "seconda riga\n") == 0 ? 0 : -1, "il clone ha il file del secondo commit");
        {
            git_oid h;
            rc = git_reference_name_to_id(&h, c, "HEAD");
            passo(rc == 0 && git_oid_equal(&h, &secondo) ? 0 : -1, "HEAD del clone = secondo commit");
        }
        git_repository_free(c);
    }

    {
        git_diff     *d = 0;
        git_commit   *a = 0, *b = 0;
        git_tree     *ta = 0, *tb = 0;
        git_diff_stats *st = 0;

        rc = git_commit_lookup(&a, r, &primo);
        if (rc == 0) rc = git_commit_lookup(&b, r, &secondo);
        if (rc == 0) rc = git_commit_tree(&ta, a);
        if (rc == 0) rc = git_commit_tree(&tb, b);
        if (rc == 0) rc = git_diff_tree_to_tree(&d, r, ta, tb, 0);
        if (rc == 0) rc = git_diff_get_stats(&st, d);
        passo(rc == 0 && git_diff_stats_files_changed(st) == 1 &&
              git_diff_stats_insertions(st) == 1 ? 0 : -1, "diff: 1 file, 1 riga aggiunta");
        git_diff_stats_free(st);
        git_diff_free(d);
        git_tree_free(ta); git_tree_free(tb);
        git_commit_free(a); git_commit_free(b);
    }

    git_repository_free(r);
    git_libgit2_shutdown();
    printf("PROVA-GIT %d/%d\n", g_ok, g_n);
    return g_ok == g_n ? 0 : 1;
}
