/*
 * Information tool.
 * Print information about the input pdf.
 */

#include "pdftool.h"

enum
{
	DIMENSIONS = 0x01,
	FONTS = 0x02,
	IMAGES = 0x04,
	SHADINGS = 0x08,
	PATTERNS = 0x10,
	XOBJS = 0x20,
	ALL = DIMENSIONS | FONTS | IMAGES | SHADINGS | PATTERNS | XOBJS
};

struct info
{
	int page;
	fz_obj *pageref;
	fz_obj *pageobj;
	union {
		struct {
			fz_obj *obj;
		} info;
		struct {
			fz_obj *obj;
		} crypt;
		struct {
			fz_obj *obj;
			fz_rect *bbox;
		} dim;
		struct {
			fz_obj *obj;
			fz_obj *subtype;
			fz_obj *name;
		} font;
		struct {
			fz_obj *obj;
			fz_obj *width;
			fz_obj *height;
			fz_obj *bpc;
			fz_obj *filter;
			fz_obj *cs;
			fz_obj *altcs;
		} image;
		struct {
			fz_obj *obj;
			fz_obj *type;
		} shading;
		struct {
			fz_obj *obj;
			fz_obj *type;
			fz_obj *paint;
			fz_obj *tiling;
			fz_obj *shading;
		} pattern;
		struct {
			fz_obj *obj;
			fz_obj *groupsubtype;
			fz_obj *reference;
		} form;
	} u;
};

static struct info *dim = nil;
static int dims = 0;
static struct info *font = nil;
static int fonts = 0;
static struct info *image = nil;
static int images = 0;
static struct info *shading = nil;
static int shadings = 0;
static struct info *pattern = nil;
static int patterns = 0;
static struct info *form = nil;
static int forms = 0;
static struct info *psobj = nil;
static int psobjs = 0;

static void local_cleanup(void)
{
	int i;

	if (dim)
	{
		for (i = 0; i < dims; i++)
			fz_free(dim[i].u.dim.bbox);
		fz_free(dim);
		dim = nil;
		dims = 0;
	}

	if (font)
	{
		fz_free(font);
		font = nil;
		fonts = 0;
	}

	if (image)
	{
		fz_free(image);
		image = nil;
		images = 0;
	}

	if (shading)
	{
		fz_free(shading);
		shading = nil;
		shadings = 0;
	}

	if (pattern)
	{
		fz_free(pattern);
		pattern = nil;
		patterns = 0;
	}

	if (form)
	{
		fz_free(form);
		form = nil;
		forms = 0;
	}

	if (psobj)
	{
		fz_free(psobj);
		psobj = nil;
		psobjs = 0;
	}

	if (xref && xref->store)
	{
		pdf_freestore(xref->store);
		xref->store = nil;
	}
}

static void
infousage(void)
{
	fprintf(stderr,
		"usage: pdfinfo [options] [file.pdf ... ]\n"
		"\t-d -\tpassword for decryption\n"
		"\t-f\tlist fonts\n"
		"\t-i\tlist images\n"
		"\t-m\tlist dimensions\n"
		"\t-p\tlist patterns\n"
		"\t-s\tlist shadings\n"
		"\t-x\tlist form and postscript xobjects\n");
	exit(1);
}

static void
showglobalinfo(void)
{
	fz_obj *obj;

	printf("\nPDF-%d.%d\n", xref->version / 10, xref->version % 10);

	obj = fz_dictgets(xref->trailer, "Info");
	if (obj)
	{
		printf("Info object (%d %d R):\n", fz_tonum(obj), fz_togen(obj));
		fz_debugobj(fz_resolveindirect(obj));
	}

	obj = fz_dictgets(xref->trailer, "Encrypt");
	if (obj)
	{
		printf("\nEncryption object (%d %d R):\n", fz_tonum(obj), fz_togen(obj));
		fz_debugobj(fz_resolveindirect(obj));
	}

	printf("\nPages: %d\n\n", pagecount);
}

static void
gatherdimensions(int page, fz_obj *pageref, fz_obj *pageobj)
{
	fz_rect bbox;
	fz_obj *obj;
	int j;

	obj = fz_dictgets(pageobj, "MediaBox");
	if (!fz_isarray(obj))
		return;

	bbox = pdf_torect(obj);

	for (j = 0; j < dims; j++)
		if (!memcmp(dim[j].u.dim.bbox, &bbox, sizeof (fz_rect)))
			break;

	if (j < dims)
		return;

	dims++;

	dim = fz_realloc(dim, dims * sizeof (struct info));
	dim[dims - 1].page = page;
	dim[dims - 1].pageref = pageref;
	dim[dims - 1].pageobj = pageobj;
	dim[dims - 1].u.dim.bbox = fz_malloc(sizeof (fz_rect));
	memcpy(dim[dims - 1].u.dim.bbox, &bbox, sizeof (fz_rect));

	return;
}

static fz_error
gatherfonts(int page, fz_obj *pageref, fz_obj *pageobj, fz_obj *dict)
{
	int i;

	for (i = 0; i < fz_dictlen(dict); i++)
	{
		fz_obj *fontdict;
		fz_obj *subtype;
		fz_obj *basefont;
		fz_obj *name;
		int k;

		fontdict = fz_dictgetval(dict, i);
		if (!fz_isdict(fontdict))
		{
			fz_warn("not a font dict (%d %d R)", fz_tonum(fontdict), fz_togen(fontdict));
			continue;
		}

		subtype = fz_dictgets(fontdict, "Subtype");
		if (!fz_isname(subtype))
			fz_warn("not a font dict subtype (%d %d R)", fz_tonum(fontdict), fz_togen(fontdict));

		basefont = fz_dictgets(fontdict, "BaseFont");
		if (basefont && !fz_isnull(basefont))
		{
			if (!fz_isname(basefont))
				return fz_throw("not a font dict basefont (%d %d R)", fz_tonum(fontdict), fz_togen(fontdict));
		}
		else
		{
			name = fz_dictgets(fontdict, "Name");
			if (name && !fz_isname(name))
				return fz_throw("not a font dict name (%d %d R)", fz_tonum(fontdict), fz_togen(fontdict));
		}

		for (k = 0; k < fonts; k++)
			if (fz_tonum(font[k].u.font.obj) == fz_tonum(fontdict) &&
				fz_togen(font[k].u.font.obj) == fz_togen(fontdict))
				break;

		if (k < fonts)
			continue;

		fonts++;

		font = fz_realloc(font, fonts * sizeof (struct info));
		font[fonts - 1].page = page;
		font[fonts - 1].pageref = pageref;
		font[fonts - 1].pageobj = pageobj;
		font[fonts - 1].u.font.obj = fontdict;
		font[fonts - 1].u.font.subtype = subtype;
		font[fonts - 1].u.font.name = basefont ? basefont : name;
	}

	return fz_okay;
}

static fz_error
gatherimages(int page, fz_obj *pageref, fz_obj *pageobj, fz_obj *dict)
{
	int i;

	for (i = 0; i < fz_dictlen(dict); i++)
	{
		fz_obj *imagedict;
		fz_obj *type;
		fz_obj *width;
		fz_obj *height;
		fz_obj *bpc = nil;
		fz_obj *filter = nil;
		fz_obj *mask;
		fz_obj *cs = nil;
		fz_obj *altcs;
		int k;

		imagedict = fz_dictgetval(dict, i);
		if (!fz_isdict(imagedict))
			return fz_throw("not an image dict (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));

		type = fz_dictgets(imagedict, "Subtype");
		if (!fz_isname(type))
			return fz_throw("not an image subtype (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));
		if (strcmp(fz_toname(type), "Image"))
			continue;

		filter = fz_dictgets(imagedict, "Filter");
		if (filter && !fz_isname(filter) && !fz_isarray(filter))
			return fz_throw("not an image filter (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));

		mask = fz_dictgets(imagedict, "ImageMask");

		altcs = nil;
		cs = fz_dictgets(imagedict, "ColorSpace");
		if (fz_isarray(cs))
		{
			fz_obj *cses = cs;

			cs = fz_arrayget(cses, 0);
			if (fz_isname(cs) && (!strcmp(fz_toname(cs), "DeviceN") || !strcmp(fz_toname(cs), "Separation")))
			{
				altcs = fz_arrayget(cses, 2);
				if (fz_isarray(altcs))
					altcs = fz_arrayget(altcs, 0);
			}
		}

		if (fz_isbool(mask) && fz_tobool(mask))
		{
			if (cs)
				fz_warn("image mask (%d %d R) may not have colorspace", fz_tonum(imagedict), fz_togen(imagedict));
		}
		if (cs && !fz_isname(cs))
			return fz_throw("not an image colorspace (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));
		if (altcs && !fz_isname(altcs))
			return fz_throw("not an image alternate colorspace (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));

		width = fz_dictgets(imagedict, "Width");
		if (!fz_isint(width))
			return fz_throw("not an image width (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));

		height = fz_dictgets(imagedict, "Height");
		if (!fz_isint(height))
			return fz_throw("not an image height (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));

		bpc = fz_dictgets(imagedict, "BitsPerComponent");
		if (!fz_tobool(mask) && !fz_isint(bpc))
			return fz_throw("not an image bits per component (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));
		if (fz_tobool(mask) && fz_isint(bpc) && fz_toint(bpc) != 1)
			return fz_throw("not an image mask bits per component (%d %d R)", fz_tonum(imagedict), fz_togen(imagedict));

		for (k = 0; k < images; k++)
			if (fz_tonum(image[k].u.image.obj) == fz_tonum(imagedict) &&
				fz_togen(image[k].u.image.obj) == fz_togen(imagedict))
				break;

		if (k < images)
			continue;

		images++;

		image = fz_realloc(image, images * sizeof (struct info));
		image[images - 1].page = page;
		image[images - 1].pageref = pageref;
		image[images - 1].pageobj = pageobj;
		image[images - 1].u.image.obj = imagedict;
		image[images - 1].u.image.width = width;
		image[images - 1].u.image.height = height;
		image[images - 1].u.image.bpc = bpc;
		image[images - 1].u.image.filter = filter;
		image[images - 1].u.image.cs = cs;
		image[images - 1].u.image.altcs = altcs;
	}

	return fz_okay;
}

static fz_error
gatherforms(int page, fz_obj *pageref, fz_obj *pageobj, fz_obj *dict)
{
	int i;

	for (i = 0; i < fz_dictlen(dict); i++)
	{
		fz_obj *xobjdict;
		fz_obj *type;
		fz_obj *subtype;
		fz_obj *group;
		fz_obj *groupsubtype;
		fz_obj *reference;
		int k;

		xobjdict = fz_dictgetval(dict, i);
		if (!fz_isdict(xobjdict))
			return fz_throw("not a xobject dict (%d %d R)", fz_tonum(xobjdict), fz_togen(xobjdict));

		type = fz_dictgets(xobjdict, "Subtype");
		if (!fz_isname(type))
			return fz_throw("not a xobject type (%d %d R)", fz_tonum(xobjdict), fz_togen(xobjdict));
		if (strcmp(fz_toname(type), "Form"))
			continue;

		subtype = fz_dictgets(xobjdict, "Subtype2");
		if (subtype && !fz_isname(subtype))
			return fz_throw("not a xobject subtype (%d %d R)", fz_tonum(xobjdict), fz_togen(xobjdict));

		if (!strcmp(fz_toname(subtype), "PS"))
			continue;

		group = fz_dictgets(xobjdict, "Group");
		if (group && !fz_isdict(group))
			return fz_throw("not a form xobject group dict (%d %d R)", fz_tonum(xobjdict), fz_togen(xobjdict));
		groupsubtype = fz_dictgets(group, "S");

		reference = fz_dictgets(xobjdict, "Ref");
		if (reference && !fz_isdict(reference))
			return fz_throw("not a form xobject reference dict (%d %d R)", fz_tonum(xobjdict), fz_togen(xobjdict));

		for (k = 0; k < forms; k++)
			if (fz_tonum(form[k].u.form.obj) == fz_tonum(xobjdict) &&
				fz_togen(form[k].u.form.obj) == fz_togen(xobjdict))
				break;

		if (k < forms)
			continue;

		forms++;

		form = fz_realloc(form, forms * sizeof (struct info));
		form[forms - 1].page = page;
		form[forms - 1].pageref = pageref;
		form[forms - 1].pageobj = pageobj;
		form[forms - 1].u.form.obj = xobjdict;
		form[forms - 1].u.form.groupsubtype = groupsubtype;
		form[forms - 1].u.form.reference = reference;
	}

	return fz_okay;
}

static fz_error
gatherpsobjs(int page, fz_obj *pageref, fz_obj *pageobj, fz_obj *dict)
{
	int i;

	for (i = 0; i < fz_dictlen(dict); i++)
	{
		fz_obj *xobjdict;
		fz_obj *type;
		fz_obj *subtype;
		int k;

		xobjdict = fz_dictgetval(dict, i);
		if (!fz_isdict(xobjdict))
			return fz_throw("not a xobject dict (%d %d R)", fz_tonum(xobjdict), fz_togen(xobjdict));

		type = fz_dictgets(xobjdict, "Subtype");
		if (!fz_isname(type))
			return fz_throw("not a xobject type (%d %d R)", fz_tonum(xobjdict), fz_togen(xobjdict));

		subtype = fz_dictgets(xobjdict, "Subtype2");
		if (subtype && !fz_isname(subtype))
			return fz_throw("not a xobject subtype (%d %d R)", fz_tonum(xobjdict), fz_togen(xobjdict));

		if (strcmp(fz_toname(type), "PS") &&
			(strcmp(fz_toname(type), "Form") || strcmp(fz_toname(subtype), "PS")))
			continue;

		for (k = 0; k < psobjs; k++)
			if (fz_tonum(psobj[k].u.form.obj) == fz_tonum(xobjdict) &&
				fz_togen(psobj[k].u.form.obj) == fz_togen(xobjdict))
				break;

		if (k < psobjs)
			continue;

		psobjs++;

		psobj = fz_realloc(psobj, psobjs * sizeof (struct info));
		psobj[psobjs - 1].page = page;
		psobj[psobjs - 1].pageref = pageref;
		psobj[psobjs - 1].pageobj = pageobj;
		psobj[psobjs - 1].u.form.obj = xobjdict;
	}

	return fz_okay;
}

static fz_error
gathershadings(int page, fz_obj *pageref, fz_obj *pageobj, fz_obj *dict)
{
	int i;

	for (i = 0; i < fz_dictlen(dict); i++)
	{
		fz_obj *shade;
		fz_obj *type;
		int k;

		shade = fz_dictgetval(dict, i);
		if (!fz_isdict(shade))
			return fz_throw("not a shading dict (%d %d R)", fz_tonum(shade), fz_togen(shade));

		type = fz_dictgets(shade, "ShadingType");
		if (!fz_isint(type) || fz_toint(type) < 1 || fz_toint(type) > 7)
		{
			fz_warn("not a shading type (%d %d R)", fz_tonum(shade), fz_togen(shade));
			type = nil;
		}

		for (k = 0; k < shadings; k++)
			if (fz_tonum(shading[k].u.shading.obj) == fz_tonum(shade) &&
				fz_togen(shading[k].u.shading.obj) == fz_togen(shade))
				break;

		if (k < shadings)
			continue;

		shadings++;

		shading = fz_realloc(shading, shadings * sizeof (struct info));
		shading[shadings - 1].page = page;
		shading[shadings - 1].pageref = pageref;
		shading[shadings - 1].pageobj = pageobj;
		shading[shadings - 1].u.shading.obj = shade;
		shading[shadings - 1].u.shading.type = type;
	}

	return fz_okay;
}

static fz_error
gatherpatterns(int page, fz_obj *pageref, fz_obj *pageobj, fz_obj *dict)
{
	int i;

	for (i = 0; i < fz_dictlen(dict); i++)
	{
		fz_obj *patterndict;
		fz_obj *type;
		fz_obj *paint = nil;
		fz_obj *tiling = nil;
		fz_obj *shading = nil;
		int k;

		patterndict = fz_dictgetval(dict, i);
		if (!fz_isdict(patterndict))
			return fz_throw("not a pattern dict (%d %d R)", fz_tonum(patterndict), fz_togen(patterndict));

		type = fz_dictgets(patterndict, "PatternType");
		if (!fz_isint(type) || fz_toint(type) < 1 || fz_toint(type) > 2)
		{
			fz_warn("not a pattern type (%d %d R)", fz_tonum(patterndict), fz_togen(patterndict));
			type = nil;
		}

		if (fz_toint(type) == 1)
		{
			paint = fz_dictgets(patterndict, "PaintType");
			if (!fz_isint(paint) || fz_toint(paint) < 1 || fz_toint(paint) > 2)
			{
				fz_warn("not a pattern paint type (%d %d R)", fz_tonum(patterndict), fz_togen(patterndict));
				paint = nil;
			}

			tiling = fz_dictgets(patterndict, "TilingType");
			if (!fz_isint(tiling) || fz_toint(tiling) < 1 || fz_toint(tiling) > 3)
			{
				fz_warn("not a pattern tiling type (%d %d R)", fz_tonum(patterndict), fz_togen(patterndict));
				tiling = nil;
			}
		}
		else
		{
			shading = fz_dictgets(patterndict, "Shading");
		}

		for (k = 0; k < patterns; k++)
			if (fz_tonum(pattern[k].u.pattern.obj) == fz_tonum(patterndict) &&
				fz_togen(pattern[k].u.pattern.obj) == fz_togen(patterndict))
				break;

		if (k < patterns)
			continue;

		patterns++;

		pattern = fz_realloc(pattern, patterns * sizeof (struct info));
		pattern[patterns - 1].page = page;
		pattern[patterns - 1].pageref = pageref;
		pattern[patterns - 1].pageobj = pageobj;
		pattern[patterns - 1].u.pattern.obj = patterndict;
		pattern[patterns - 1].u.pattern.type = type;
		pattern[patterns - 1].u.pattern.paint = paint;
		pattern[patterns - 1].u.pattern.tiling = tiling;
		pattern[patterns - 1].u.pattern.shading = shading;
	}

	return fz_okay;
}

static void
gatherresourceinfo(int page, fz_obj *rsrc)
{
	fz_error error;
	fz_obj *pageobj;
	fz_obj *pageref;
	fz_obj *font;
	fz_obj *xobj;
	fz_obj *shade;
	fz_obj *pattern;
	fz_obj *subrsrc;
	int i;

	pageobj = pdf_getpageobject(xref, page);
	pageref = pdf_getpageref(xref, page);

	if (!pageobj)
		die(fz_throw("cannot retrieve info from page %d", page));

	font = fz_dictgets(rsrc, "Font");
	if (font)
	{
		error = gatherfonts(page, pageref, pageobj, font);
		if (error)
			die(fz_rethrow(error, "gathering fonts at page %d (%d %d R)", page, fz_tonum(pageref), fz_togen(pageref)));

		for (i = 0; i < fz_dictlen(font); i++)
		{
			fz_obj *obj = fz_dictgetval(font, i);

			subrsrc = fz_dictgets(obj, "Resources");
			if (subrsrc && fz_objcmp(rsrc, subrsrc))
				gatherresourceinfo(page, subrsrc);
		}
	}

	xobj = fz_dictgets(rsrc, "XObject");
	if (xobj)
	{
		error = gatherimages(page, pageref, pageobj, xobj);
		if (error)
			die(fz_rethrow(error, "gathering images at page %d (%d %d R)", page, fz_tonum(pageref), fz_togen(pageref)));
		error = gatherforms(page, pageref, pageobj, xobj);
		if (error)
			die(fz_rethrow(error, "gathering forms at page %d (%d %d R)", page, fz_tonum(pageref), fz_togen(pageref)));
		error = gatherpsobjs(page, pageref, pageobj, xobj);
		if (error)
			die(fz_rethrow(error, "gathering postscript objects at page %d (%d %d R)", page, fz_tonum(pageref), fz_togen(pageref)));

		for (i = 0; i < fz_dictlen(xobj); i++)
		{
			fz_obj *obj = fz_dictgetval(xobj, i);
			subrsrc = fz_dictgets(obj, "Resources");
			if (subrsrc && fz_objcmp(rsrc, subrsrc))
				gatherresourceinfo(page, subrsrc);
		}
	}

	shade = fz_dictgets(rsrc, "Shading");
	if (shade)
	{
		error = gathershadings(page, pageref, pageobj, shade);
		if (error)
			die(fz_rethrow(error, "gathering shadings at page %d (%d %d R)", page, fz_tonum(pageref), fz_togen(pageref)));
	}

	pattern = fz_dictgets(rsrc, "Pattern");
	if (pattern)
	{
		error = gatherpatterns(page, pageref, pageobj, pattern);
		if (error)
			die(fz_rethrow(error, "gathering shadings at page %d (%d %d R)", page, fz_tonum(pageref), fz_togen(pageref)));

		for (i = 0; i < fz_dictlen(pattern); i++)
		{
			fz_obj *obj = fz_dictgetval(pattern, i);
			subrsrc = fz_dictgets(obj, "Resources");
			if (subrsrc && fz_objcmp(rsrc, subrsrc))
				gatherresourceinfo(page, subrsrc);
		}
	}
}

static void
gatherpageinfo(int page)
{
	fz_obj *pageobj;
	fz_obj *pageref;
	fz_obj *rsrc;

	pageobj = pdf_getpageobject(xref, page);
	pageref = pdf_getpageref(xref, page);

	if (!pageobj)
		die(fz_throw("cannot retrieve info from page %d", page));

	gatherdimensions(page, pageref, pageobj);

	rsrc = fz_dictgets(pageobj, "Resources");
	gatherresourceinfo(page, rsrc);
}

static void
printinfo(char *filename, int show, int page)
{
	int i;
	int j;

#define PAGE_FMT "\t% 5d (% 7d %1d R): "

	if (show & DIMENSIONS && dims > 0)
	{
		printf("Mediaboxes (%d):\n", dims);
		for (i = 0; i < dims; i++)
		{
			printf(PAGE_FMT "[ %g %g %g %g ]\n",
				dim[i].page,
				fz_tonum(dim[i].pageref), fz_togen(dim[i].pageref),
				dim[i].u.dim.bbox->x0,
				dim[i].u.dim.bbox->y0,
				dim[i].u.dim.bbox->x1,
				dim[i].u.dim.bbox->y1);
		}
		printf("\n");
	}

	if (show & FONTS && fonts > 0)
	{
		printf("Fonts (%d):\n", fonts);
		for (i = 0; i < fonts; i++)
		{
			printf(PAGE_FMT "%s '%s' (%d %d R)\n",
				font[i].page,
				fz_tonum(font[i].pageref), fz_togen(font[i].pageref),
				fz_toname(font[i].u.font.subtype),
				fz_toname(font[i].u.font.name),
				fz_tonum(font[i].u.font.obj), fz_togen(font[i].u.font.obj));
		}
		printf("\n");
	}

	if (show & IMAGES && images > 0)
	{
		printf("Images (%d):\n", images);
		for (i = 0; i < images; i++)
		{
			char *cs = nil;
			char *altcs = nil;

			printf(PAGE_FMT "[ ",
				image[i].page,
				fz_tonum(image[i].pageref), fz_togen(image[i].pageref));

			if (fz_isarray(image[i].u.image.filter))
				for (j = 0; j < fz_arraylen(image[i].u.image.filter); j++)
				{
					fz_obj *obj = fz_arrayget(image[i].u.image.filter, j);
					char *filter = fz_strdup(fz_toname(obj));

					if (strstr(filter, "Decode"))
						*(strstr(filter, "Decode")) = '\0';

					printf("%s%s",
							filter,
							j == fz_arraylen(image[i].u.image.filter) - 1 ? "" : " ");
					fz_free(filter);
				}
			else if (image[i].u.image.filter)
			{
				fz_obj *obj = image[i].u.image.filter;
				char *filter = fz_strdup(fz_toname(obj));

				if (strstr(filter, "Decode"))
					*(strstr(filter, "Decode")) = '\0';

				printf("%s", filter);
				fz_free(filter);
			}
			else
				printf("Raw");

			if (image[i].u.image.cs)
			{
				cs = fz_strdup(fz_toname(image[i].u.image.cs));

				if (!strncmp(cs, "Device", 6))
				{
					for (j = 0; cs[j + 6] != '\0'; j++)
						cs[3 + j] = cs[j + 6];
					cs[j + 6] = '\0';
				}
				if (strstr(cs, "ICC"))
					fz_strlcpy(cs, "ICC", 3);
				if (strstr(cs, "Indexed"))
					fz_strlcpy(cs, "Idx", 3);
				if (strstr(cs, "Pattern"))
					fz_strlcpy(cs, "Pat", 3);
				if (strstr(cs, "Separation"))
					fz_strlcpy(cs, "Sep", 3);
			}
			if (image[i].u.image.altcs)
			{
				altcs = fz_strdup(fz_toname(image[i].u.image.altcs));

				if (!strncmp(altcs, "Device", 6))
				{
					for (j = 0; altcs[j + 6] != '\0'; j++)
						altcs[3 + j] = altcs[j + 6];
					altcs[j + 6] = '\0';
				}
				if (strstr(altcs, "ICC"))
					fz_strlcpy(altcs, "ICC", 3);
				if (strstr(altcs, "Indexed"))
					fz_strlcpy(altcs, "Idx", 3);
				if (strstr(altcs, "Pattern"))
					fz_strlcpy(altcs, "Pat", 3);
				if (strstr(altcs, "Separation"))
					fz_strlcpy(altcs, "Sep", 3);
			}

			printf(" ] %dx%d %dbpc %s%s%s (%d %d R)\n",
				fz_toint(image[i].u.image.width),
				fz_toint(image[i].u.image.height),
				image[i].u.image.bpc ? fz_toint(image[i].u.image.bpc) : 1,
				image[i].u.image.cs ? cs : "ImageMask",
				image[i].u.image.altcs ? " " : "",
				image[i].u.image.altcs ? altcs : "",
				fz_tonum(image[i].u.image.obj), fz_togen(image[i].u.image.obj));

			fz_free(cs);
			fz_free(altcs);
		}
		printf("\n");
	}

	if (show & SHADINGS && shadings > 0)
	{
		printf("Shading patterns (%d):\n", shadings);
		for (i = 0; i < shadings; i++)
		{
			char *shadingtype[] =
			{
				"",
				"Function",
				"Axial",
				"Radial",
				"Triangle mesh",
				"Lattice",
				"Coons patch",
				"Tensor patch",
			};

			printf(PAGE_FMT "%s (%d %d R)\n",
				shading[i].page,
				fz_tonum(shading[i].pageref), fz_togen(shading[i].pageref),
				shadingtype[fz_toint(shading[i].u.shading.type)],
				fz_tonum(shading[i].u.shading.obj), fz_togen(shading[i].u.shading.obj));
		}
		printf("\n");
	}

	if (show & PATTERNS && patterns > 0)
	{
		printf("Patterns (%d):\n", patterns);
		for (i = 0; i < patterns; i++)
		{
			if (fz_toint(pattern[i].u.pattern.type) == 1)
			{
				char *painttype[] =
				{
					"",
					"Colored",
					"Uncolored",
				};
				char *tilingtype[] =
				{
					"",
					"Constant",
					"No distortion",
					"Constant/fast tiling",
				};

				printf(PAGE_FMT "Tiling %s %s (%d %d R)\n",
						pattern[i].page,
						fz_tonum(pattern[i].pageref), fz_togen(pattern[i].pageref),
						painttype[fz_toint(pattern[i].u.pattern.paint)],
						tilingtype[fz_toint(pattern[i].u.pattern.tiling)],
						fz_tonum(pattern[i].u.pattern.obj), fz_togen(pattern[i].u.pattern.obj));
			}
			else
			{
				printf(PAGE_FMT "Shading %d %d R (%d %d R)\n",
						pattern[i].page,
						fz_tonum(pattern[i].pageref), fz_togen(pattern[i].pageref),
						fz_tonum(pattern[i].u.pattern.shading), fz_togen(pattern[i].u.pattern.shading),
						fz_tonum(pattern[i].u.pattern.obj), fz_togen(pattern[i].u.pattern.obj));
			}
		}
		printf("\n");
	}

	if (show & XOBJS && forms > 0)
	{
		printf("Form xobjects (%d):\n", forms);
		for (i = 0; i < forms; i++)
		{
			printf(PAGE_FMT "Form%s%s%s%s (%d %d R)\n",
				form[i].page,
				fz_tonum(form[i].pageref), fz_togen(form[i].pageref),
				form[i].u.form.groupsubtype ? " " : "",
				form[i].u.form.groupsubtype ? fz_toname(form[i].u.form.groupsubtype) : "",
				form[i].u.form.groupsubtype ? " Group" : "",
				form[i].u.form.reference ? " Reference" : "",
				fz_tonum(form[i].u.form.obj), fz_togen(form[i].u.form.obj));
		}
		printf("\n");
	}

	if (show & XOBJS && psobjs > 0)
	{
		printf("Postscript xobjects (%d):\n", psobjs);
		for (i = 0; i < psobjs; i++)
		{
			printf(PAGE_FMT "(%d %d R)\n",
				psobj[i].page,
				fz_tonum(psobj[i].pageref), fz_togen(psobj[i].pageref),
				fz_tonum(psobj[i].u.form.obj), fz_togen(psobj[i].u.form.obj));
		}
		printf("\n");
	}
}

static void
showinfo(char *filename, int show, char *pagelist)
{
	int page, spage, epage;
	char *spec, *dash;
	int allpages;

	if (!xref)
		infousage();

	allpages = !strcmp(pagelist, "1-");

	spec = fz_strsep(&pagelist, ",");
	while (spec)
	{
		dash = strchr(spec, '-');

		if (dash == spec)
			spage = epage = 1;
		else
			spage = epage = atoi(spec);

		if (dash)
		{
			if (strlen(dash) > 1)
				epage = atoi(dash + 1);
			else
				epage = pagecount;
		}

		if (spage > epage)
			page = spage, spage = epage, epage = page;

		if (spage < 1)
			spage = 1;
		if (epage > pagecount)
			epage = pagecount;
		if (spage > pagecount)
			spage = pagecount;

		if (allpages)
			printf("Retrieving info from pages %d-%d...\n", spage, epage);
		if (spage >= 1)
		{
			for (page = spage; page <= epage; page++)
			{
				gatherpageinfo(page);
				if (!allpages)
				{
					printf("Page %d:\n", page);
					printinfo(filename, show, page);
					printf("\n");
				}
			}
		}

		spec = fz_strsep(&pagelist, ",");
	}

	if (allpages)
		printinfo(filename, show, -1);
}

int main(int argc, char **argv)
{
	enum { NO_FILE_OPENED, NO_INFO_GATHERED, INFO_SHOWN } state;
	char *filename = "";
	char *password = "";
	int show = ALL;
	int c;

	while ((c = fz_getopt(argc, argv, "mfispxd:")) != -1)
	{
		switch (c)
		{
		case 'm': if (show == ALL) show = DIMENSIONS; else show |= DIMENSIONS; break;
		case 'f': if (show == ALL) show = FONTS; else show |= FONTS; break;
		case 'i': if (show == ALL) show = IMAGES; else show |= IMAGES; break;
		case 's': if (show == ALL) show = SHADINGS; else show |= SHADINGS; break;
		case 'p': if (show == ALL) show = PATTERNS; else show |= PATTERNS; break;
		case 'x': if (show == ALL) show = XOBJS; else show |= XOBJS; break;
		case 'd': password = fz_optarg; break;
		default:
			infousage();
			break;
		}
	}

	if (fz_optind == argc)
		infousage();

	setcleanup(local_cleanup);

	state = NO_FILE_OPENED;
	while (fz_optind < argc)
	{
		if (strstr(argv[fz_optind], ".pdf") || strstr(argv[fz_optind], ".PDF"))
		{
			if (state == NO_INFO_GATHERED)
			{
				showinfo(filename, show, "1-");
				closexref();
			}

			closexref();
			filename = argv[fz_optind];
			printf("%s:\n", filename);
			openxref(filename, password, 0, 1);
			showglobalinfo();
			state = NO_INFO_GATHERED;
		}
		else
		{
			showinfo(filename, show, argv[fz_optind]);
			state = INFO_SHOWN;
		}

		fz_optind++;
	}

	if (state == NO_INFO_GATHERED)
		showinfo(filename, show, "1-");

	closexref();

	return 0;
}

