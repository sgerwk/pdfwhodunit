/*
 * pdfwhodunit.c
 *
 * trace a PDF file
 *
 * see pdfwhodunit.1
 * also comments at the top of pdftrace.c
 */

#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

#include <poppler.h>
#include <cairo.h>
#include <cairo-pdf.h>

#define HOMELIB "bin"

/*
 * current and previous images
 */
cairo_surface_t *surface;
int size;
unsigned char *image;
unsigned char *previous;

/*
 * tracer
 */
int tracer() {
	cairo_surface_flush(surface);
	if (! memcmp(previous, image, size))
		return 0;
	memcpy(previous, image, size);
	return 1;
}

/*
 * main
 */
int main(int argc, char *argv[]) {
	char *preload, *home, *var;
	int opt;
	gboolean usage = FALSE;
	char *infile;
	gdouble rwidth, rheight;
	char c, d;

	void (*pdftrace)();
	void (*usenoterminal)();
	void (*register_tracer)(int (*t)());
	void (*traced_final)();

	GFile *gin;
	GError *err = NULL;
	int format = CAIRO_FORMAT_ARGB32;
	PopplerDocument *doc;
	PopplerPage *page;
	PopplerRectangle clip = {0.0, 0.0, 0.0, 0.0};
	int npages, n;
	gdouble width = 200, height = 200, stride;
	gdouble pwidth, pheight;
	cairo_t *cr;

				/* probe pdftrace.so */

	pdftrace = dlsym(RTLD_DEFAULT, "pdftrace");
	if (pdftrace == NULL) {
		printf("%s\n", dlerror());
		printf("not called with pdftrace.so preloaded\n");

		preload = getenv("LD_PRELOAD");
		if (preload == NULL) {
			printf("trying LD_PRELOAD=./pdftrace.so\n");
			putenv("LD_PRELOAD=./pdftrace.so");
			execvp(argv[0], argv);
			perror(argv[0]);
			exit(EXIT_FAILURE);
		}
		else if (! strcmp(preload, "./pdftrace.so")) {
			printf("trying LD_PRELOAD=pdftrace.so\n");
			putenv("LD_PRELOAD=pdftrace.so");
			execvp(argv[0], argv);
			perror(argv[0]);
			exit(EXIT_FAILURE);
		}
		else if (! strcmp(preload, "pdftrace.so")) {
			home = getenv("HOME");
			if (home != NULL) {
				var = malloc(strlen(home) + 100);
				sprintf(var, "LD_PRELOAD=%s/%s",
					home, "/" HOMELIB "/pdftrace.so");
				putenv(var);
				execvp(argv[0], argv);
				perror(argv[0]);
				exit(EXIT_FAILURE);
			}
		}

		printf("cannot find pdftrace.so\n");
		printf("call as:\n");
		printf("LD_PRELOAD=pdftrace.so pdfwhodunit ...\n");
		printf("or\n");
		printf("LD_PRELOAD=./pdftrace.so pdfwhodunit ...\n");
		exit(EXIT_FAILURE);
	}

				/* arguments */

	while ((opt = getopt(argc, argv, "h")) != -1)
		switch(opt) {
		case 'h':
			usage = TRUE;
			break;
		}

	if (! usage && argc - 1 < optind) {
		printf("input file name missing\n");
		usage = TRUE;
	}
	if (usage) {
		printf("usage:\n");
		printf("\tLD_PRELOAD=./pdftrace.so [GRANULARITY=n] \\\n");
		printf("\tpdfwhodunit ");
		printf("[-h] file.pdf [x1,y1,x2,y2]\n");
		printf("\t\t-h\t\tthis help\n");
		exit(EXIT_FAILURE);
	}
	infile = argv[optind];
	if (argc - 1 > optind) {
		if (5 == sscanf(argv[optind + 1], "[%lg,%lg+%lg,%lg%c%c",
				&clip.x1, &clip.y1, &rwidth, &rheight,
				&c, &d) && c == ']') {
			clip.x2 = clip.x1 + rwidth;
			clip.y2 = clip.y1 + rheight;
		}
		else if (5 == sscanf(argv[optind + 1], "[%lg,%lg-%lg,%lg%c%c",
				&clip.x1, &clip.y1, &clip.x2, &clip.y2,
				&c, &d) && c == ']') {
		}
		else {
			printf("error parsing rectangle: ");
			printf("%s\n", argv[optind + 1]);
			exit(EXIT_FAILURE);
		}
		printf("clipping rectangle: ");
		printf("[%g,%g,%g,%g]\n", clip.x1, clip.y1, clip.x2, clip.y2);
	}

				/* do not use terminal codes */

	usenoterminal = dlsym(RTLD_DEFAULT, "usenoterminal");
	usenoterminal();

				/* create images */

	stride = cairo_format_stride_for_width(format, width);
	size = stride * height;
	image = malloc(size);
	memset(image, 0, size);
	previous = malloc(size);
	memcpy(previous, image, size);
	surface = cairo_image_surface_create_for_data(image,
		format, width, height, stride);

				/* register tracer */

	register_tracer = dlsym(RTLD_DEFAULT, "register_tracer");
	register_tracer(tracer);

				/* open file */

	gin = g_file_new_for_path(infile);
	doc = poppler_document_new_from_gfile(gin, NULL, NULL, &err);
	if (doc == NULL) {
		g_printerr("error opening pdf file: %s\n", err->message);
		g_error_free(err);
		exit(EXIT_FAILURE);
	}

				/* pages */

	npages = poppler_document_get_n_pages(doc);
	if (npages < 1) {
		printf("no page in document\n");
		exit(EXIT_FAILURE);
	}

				/* copy to destination */

	printf("pages: \n");

	for (n = 0; n < npages; n++) {
		printf("  - page: %d\n", n);
		page = poppler_document_get_page(doc, n);
		poppler_page_get_size(page, &pwidth, &pheight);

		cr = cairo_create(surface);
		cairo_scale(cr, width / pwidth, height / pheight);
		if (clip.x2 != 0 || clip.y2 != 0) {
			cairo_rectangle(cr, clip.x1, clip.y1,
				clip.x2 - clip.x1, clip.y2 - clip.y1);
			cairo_clip(cr);
		}
		poppler_page_render_for_printing(page, cr);
		cairo_destroy(cr);
		cairo_surface_show_page(surface);

		g_object_unref(page);
	}

	traced_final = dlsym(RTLD_DEFAULT, "traced_final");
	traced_final();

	cairo_surface_destroy(surface);
	g_object_unref(gin);

	return EXIT_SUCCESS;
}

