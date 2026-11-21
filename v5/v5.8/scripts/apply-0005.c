/* SPDX-License-Identifier: MIT
 * Insert v5.8 /proc/handshake_holds and /proc/handshake_spinsites into
 * an already-patched (0001-0004) kernel/locking/hs_lockstat.c.
 * Official 0001-0004 patch files are not modified. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_all(const char *path, size_t *len)
{
    FILE *fp = fopen(path, "rb");
    char *buf;
    long n;
    if (!fp)
        return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    n = ftell(fp);
    if (n < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    buf[n] = '\0';
    *len = (size_t)n;
    return buf;
}

static const char funcs[] =
    "\n"
    "static int hs_holds_show(struct seq_file *m, void *v)\n"
    "{\n"
    "	int c, cpu, i;\n"
    "\n"
    "	seq_puts(m, \"class n avg_ns max_ns hist_lt1us hist_lt5us hist_lt20us hist_lt100us hist_ge100us\\n\");\n"
    "	for (c = 0; c < HS_HOLD_N; c++) {\n"
    "		struct hs_hold s = { };\n"
    "\n"
    "		for_each_possible_cpu(cpu) {\n"
    "			struct hs_hold *h = &per_cpu_ptr(hs_holds, cpu)[c];\n"
    "\n"
    "			s.n += READ_ONCE(h->n);\n"
    "			s.sum += READ_ONCE(h->sum);\n"
    "			s.max = max(s.max, READ_ONCE(h->max));\n"
    "			for (i = 0; i < 5; i++)\n"
    "				s.hist[i] += READ_ONCE(h->hist[i]);\n"
    "		}\n"
    "		seq_printf(m, \"%s %lu %llu %llu %lu %lu %lu %lu %lu\\n\",\n"
    "			   hs_hold_name[c], s.n, s.n ? s.sum / s.n : 0, s.max,\n"
    "			   s.hist[0], s.hist[1], s.hist[2], s.hist[3], s.hist[4]);\n"
    "	}\n"
    "	return 0;\n"
    "}\n"
    "\n"
    "static int hs_sites_show(struct seq_file *m, void *v)\n"
    "{\n"
    "	struct hs_site *all;\n"
    "	unsigned long overflow = 0;\n"
    "	int cpu, i, j, n = 0;\n"
    "\n"
    "	all = kcalloc(HS_SITES * 4, sizeof(*all), GFP_KERNEL);\n"
    "	if (!all)\n"
    "		return -ENOMEM;\n"
    "\n"
    "	for_each_possible_cpu(cpu) {\n"
    "		struct hs_sites *t = per_cpu_ptr(&hs_sites, cpu);\n"
    "\n"
    "		overflow += READ_ONCE(t->overflow);\n"
    "		for (i = 0; i < HS_SITES; i++) {\n"
    "			unsigned long ip = READ_ONCE(t->s[i].ip), c = READ_ONCE(t->s[i].n);\n"
    "\n"
    "			if (!ip)\n"
    "				continue;\n"
    "			for (j = 0; j < n; j++)\n"
    "				if (all[j].ip == ip)\n"
    "					break;\n"
    "			if (j == n) {\n"
    "				if (n == (int)(HS_SITES * 4)) {\n"
    "					overflow += c;\n"
    "					continue;\n"
    "				}\n"
    "				all[n].ip = ip;\n"
    "				all[n].wsum = all[n].wmax = 0;\n"
    "				all[n++].n = 0;\n"
    "			}\n"
    "			all[j].n += c;\n"
    "			all[j].wsum += READ_ONCE(t->s[i].wsum);\n"
    "			all[j].wmax = max(all[j].wmax, READ_ONCE(t->s[i].wmax));\n"
    "		}\n"
    "	}\n"
    "	for (i = 0; i < n; i++) {\n"
    "		int best = i;\n"
    "		struct hs_site tmp;\n"
    "\n"
    "		for (j = i + 1; j < n; j++)\n"
    "			if (all[j].n > all[best].n)\n"
    "				best = j;\n"
    "		tmp = all[i];\n"
    "		all[i] = all[best];\n"
    "		all[best] = tmp;\n"
    "	}\n"
    "	seq_puts(m, \"n wait_avg_ns wait_max_ns site\\n\");\n"
    "	for (i = 0; i < n; i++)\n"
    "		seq_printf(m, \"%lu %llu %llu %pS\\n\", all[i].n,\n"
    "			   all[i].n ? all[i].wsum / all[i].n : 0, all[i].wmax,\n"
    "			   (void *)all[i].ip);\n"
    "	if (overflow)\n"
    "		seq_printf(m, \"overflow %lu\\n\", overflow);\n"
    "	kfree(all);\n"
    "	return 0;\n"
    "}\n"
    "\n";

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : NULL;
    const char *inc = "#include <linux/ftrace.h>\n";
    const char *slab = "#include <linux/slab.h>\n";
    const char *lu = "static int hs_lu_show(struct seq_file *m, void *v)\n";
    const char *oldinit =
        "	proc_create_single(\"handshake_locks\", 0444, NULL, hs_ls_show);\n"
        "	proc_create_single(\"handshake_lockuse\", 0444, NULL, hs_lu_show);\n"
        "	register_reboot_notifier(&hs_ls_nb);\n";
    const char *newinit =
        "	proc_create_single(\"handshake_locks\", 0444, NULL, hs_ls_show);\n"
        "	proc_create_single(\"handshake_lockuse\", 0444, NULL, hs_lu_show);\n"
        "	proc_create_single(\"handshake_holds\", 0444, NULL, hs_holds_show);\n"
        "	proc_create_single(\"handshake_spinsites\", 0444, NULL, hs_sites_show);\n"
        "	register_reboot_notifier(&hs_ls_nb);\n";
    char *buf, *out, *p, *q;
    size_t len, cap, n;
    FILE *fp;

    if (!path) {
        fprintf(stderr, "usage: apply-0005 <hs_lockstat.c>\n");
        return 2;
    }
    buf = read_all(path, &len);
    if (!buf) {
        fprintf(stderr, "apply-0005: cannot read %s\n", path);
        return 1;
    }
    if (strstr(buf, "handshake_holds") && strstr(buf, "hs_holds_show") &&
        strstr(buf, "hs_sites_show")) {
        fprintf(stderr, "0005 already applied\n");
        free(buf);
        return 0;
    }
    cap = len + strlen(funcs) + strlen(slab) + strlen(newinit) + 64;
    out = (char *)malloc(cap);
    if (!out) {
        free(buf);
        return 1;
    }
    n = 0;
    p = buf;
    if (!strstr(buf, slab)) {
        q = strstr(p, inc);
        if (!q) {
            fprintf(stderr, "apply-0005: no ftrace.h include\n");
            free(buf);
            free(out);
            return 1;
        }
        q += strlen(inc);
        memcpy(out + n, p, (size_t)(q - p));
        n += (size_t)(q - p);
        memcpy(out + n, slab, strlen(slab));
        n += strlen(slab);
        p = q;
    }
    q = strstr(p, lu);
    if (!q) {
        fprintf(stderr, "apply-0005: no hs_lu_show\n");
        free(buf);
        free(out);
        return 1;
    }
    memcpy(out + n, p, (size_t)(q - p));
    n += (size_t)(q - p);
    memcpy(out + n, funcs, strlen(funcs));
    n += strlen(funcs);
    p = q;
    q = strstr(p, oldinit);
    if (!q) {
        fprintf(stderr, "apply-0005: no hs_ls_init proc_create block\n");
        free(buf);
        free(out);
        return 1;
    }
    memcpy(out + n, p, (size_t)(q - p));
    n += (size_t)(q - p);
    memcpy(out + n, newinit, strlen(newinit));
    n += strlen(newinit);
    p = q + strlen(oldinit);
    memcpy(out + n, p, strlen(p));
    n += strlen(p);
    out[n] = '\0';
    fp = fopen(path, "wb");
    if (!fp || fwrite(out, 1, n, fp) != n) {
        fprintf(stderr, "apply-0005: cannot write %s\n", path);
        if (fp)
            fclose(fp);
        free(buf);
        free(out);
        return 1;
    }
    fclose(fp);
    free(buf);
    free(out);
    fprintf(stderr, "0005 applied to %s\n", path);
    return 0;
}
