/*
 * pdfshow -- the ultimate pdf debugging tool
 */

#include "fitz.h"
#include "mupdf.h"

static pdf_xref *xref = NULL;
static fz_context *ctx = NULL;
static int showbinary = 0;
static int showdecode = 1;
static int showcolumn;

void die(fz_error error)
{
	fz_error_handle(ctx, error, "aborting");
	if (xref)
		pdf_free_xref(xref);
	exit(1);
}

static void usage(void)
{
	fprintf(stderr, "usage: pdfshow [options] file.pdf [grepable] [xref] [trailer] [pagetree] [object numbers]\n");
	fprintf(stderr, "\t-b\tprint streams as binary data\n");
	fprintf(stderr, "\t-e\tprint encoded streams (don't decode)\n");
	fprintf(stderr, "\t-p\tpassword\n");
	exit(1);
}

static void showtrailer(void)
{
	if (!xref)
		die(fz_error_make(ctx, "no file specified"));
	printf("trailer\n");
	fz_debug_obj(ctx, xref->trailer);
	printf("\n");
}

static void showxref(void)
{
	if (!xref)
		die(fz_error_make(ctx, "no file specified"));
	pdf_debug_xref(xref);
	printf("\n");
}

static void showpagetree(void)
{
	fz_error error;
	fz_obj *ref;
	int count;
	int i;

	if (!xref)
		die(fz_error_make(ctx, "no file specified"));

	if (!xref->page_len)
	{
		error = pdf_load_page_tree(xref);
		if (error)
			die(fz_error_note(ctx, error, "cannot load page tree"));
	}

	count = pdf_count_pages(xref);
	for (i = 0; i < count; i++)
	{
		ref = xref->page_refs[i];
		printf("page %d = %d %d R\n", i + 1, fz_to_num(ref), fz_to_gen(ref));
	}
	printf("\n");
}

static void showsafe(unsigned char *buf, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if (buf[i] == '\r' || buf[i] == '\n') {
			putchar('\n');
			showcolumn = 0;
		}
		else if (buf[i] < 32 || buf[i] > 126) {
			putchar('.');
			showcolumn ++;
		}
		else {
			putchar(buf[i]);
			showcolumn ++;
		}
		if (showcolumn == 79) {
			putchar('\n');
			showcolumn = 0;
		}
	}
}

static void showstream(int num, int gen)
{
	fz_error error;
	fz_stream *stm;
	unsigned char buf[2048];
	int n;

	showcolumn = 0;

	if (showdecode)
		error = pdf_open_stream(&stm, xref, num, gen);
	else
		error = pdf_open_raw_stream(&stm, xref, num, gen);
	if (error)
		die(error);

	while (1)
	{
		n = fz_read(stm, buf, sizeof buf);
		if (n < 0)
			die(n);
		if (n == 0)
			break;
		if (showbinary)
			fwrite(buf, 1, n, stdout);
		else
			showsafe(buf, n);
	}

	fz_close(stm);
}

static void showobject(int num, int gen)
{
	fz_error error;
	fz_obj *obj;

	if (!xref)
		die(fz_error_make(ctx, "no file specified"));

	error = pdf_load_object(&obj, xref, num, gen);
	if (error)
		die(error);

	if (pdf_is_stream(xref, num, gen))
	{
		if (showbinary)
		{
			showstream(num, gen);
		}
		else
		{
			printf("%d %d obj\n", num, gen);
			fz_debug_obj(ctx, obj);
			printf("stream\n");
			showstream(num, gen);
			printf("endstream\n");
			printf("endobj\n\n");
		}
	}
	else
	{
		printf("%d %d obj\n", num, gen);
		fz_debug_obj(ctx, obj);
		printf("endobj\n\n");
	}

	fz_drop_obj(ctx, obj);
}

static void showgrep(char *filename)
{
	fz_error error;
	fz_obj *obj;
	int i;

	for (i = 0; i < xref->len; i++)
	{
		if (xref->table[i].type == 'n' || xref->table[i].type == 'o')
		{
			error = pdf_load_object(&obj, xref, i, 0);
			/* SumatraPDF: skip invalid objects instead of aborting */
			if (error)
			{
				fz_error_handle(ctx, error, "skipping object (%d 0 R)", i);
				continue;
			}

			fz_sort_dict(ctx, obj);

			printf("%s:%d: ", filename, i);
			fz_fprint_obj(ctx, stdout, obj, 1);

			fz_drop_obj(ctx, obj);
		}
	}

	printf("%s:trailer: ", filename);
	fz_fprint_obj(ctx, stdout, xref->trailer, 1);
}

int main(int argc, char **argv)
{
	char *password = NULL; /* don't throw errors if encrypted */
	char *filename;
	fz_error error;
	int c;

	while ((c = fz_getopt(argc, argv, "p:be")) != -1)
	{
		switch (c)
		{
		case 'p': password = fz_optarg; break;
		case 'b': showbinary = 1; break;
		case 'e': showdecode = 0; break;
		default: usage(); break;
		}
	}

	if (fz_optind == argc)
		usage();

	filename = argv[fz_optind++];

	ctx = fz_context_init(&fz_alloc_default);
	if (ctx == NULL)
		die(fz_error_note(ctx, 1, "failed to initialise context"));

	error = pdf_open_xref(ctx, &xref, filename, password);
	if (error)
		die(fz_error_note(ctx, error, "cannot open document: %s", filename));

	if (fz_optind == argc)
		showtrailer();

	while (fz_optind < argc)
	{
		switch (argv[fz_optind][0])
		{
		case 't': showtrailer(); break;
		case 'x': showxref(); break;
		case 'p': showpagetree(); break;
		case 'g': showgrep(filename); break;
		default: showobject(atoi(argv[fz_optind]), 0); break;
		}
		fz_optind++;
	}

	pdf_free_xref(xref);

	fz_flush_warnings(ctx);
	fz_context_fin(ctx);

	return 0;
}
