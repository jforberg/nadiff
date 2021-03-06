#include "parse.h"

#include <unistd.h> // isatty()
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "alloc.h"
#include "io.h"
#include "error.h"

static int
get_char(struct line * l, unsigned idx)
{
    if (idx >= l->len)
        return -1;

    return l->data[idx];
}

static bool
is_char_at_idx(struct line * l, unsigned idx, char expected)
{
    if (idx >= l->len)
        return false;

    return l->data[idx] == expected;
}

static bool
get_number(struct line * l, unsigned * cur_pos, unsigned * out_num)
{
    unsigned num = 0;

    while (true) {
        int digit = get_char(l, *cur_pos);
        if (digit < 0) {
            na_printf("Failed to parse number at row %u\n", l->row);
            return false;
        }

        /* if not a digit */
        if (digit < '0' || digit > '9')
            break;

        unsigned n = digit - '0';
        num = num * 10 + n;
        (*cur_pos)++;
    }

    *out_num = num;

    return true;
}

static bool
is_hunk_header(struct line * l)
{
    /* Format is @@ -<NUM>,<NUM> +<NUM>,<NUM> @@ */
    return is_char_at_idx(l, 0, '@') && is_char_at_idx(l, 1, '@');
}

static bool
is_diff_header(struct line *l)
{
    unsigned i = 0;
    /* Format is diff --git <filename> <filename> */
    return is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'f')
        && is_char_at_idx(l, i++, 'f')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, '-')
        && is_char_at_idx(l, i++, '-')
        && is_char_at_idx(l, i++, 'g')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 't')
        && is_char_at_idx(l, i++, ' ');
}

static bool
read_old_mode(struct line * l)
{
    unsigned i = 0;
    /* TODO also look at the mode */
    return is_char_at_idx(l, i++, 'o')
        && is_char_at_idx(l, i++, 'l')
        && is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 'm')
        && is_char_at_idx(l, i++, 'o')
        && is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, 'e');
}

static bool
read_new_mode(struct line * l)
{
    unsigned i = 0;
    /* TODO also look at the mode */
    return is_char_at_idx(l, i++, 'n')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 'w')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 'm')
        && is_char_at_idx(l, i++, 'o')
        && is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, 'e');
}

static bool
read_copy_from(struct line * l)
{
    unsigned i = 0;
    return is_char_at_idx(l, i++, 'c')
        && is_char_at_idx(l, i++, 'o')
        && is_char_at_idx(l, i++, 'p')
        && is_char_at_idx(l, i++, 'y')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 'f')
        && is_char_at_idx(l, i++, 'r')
        && is_char_at_idx(l, i++, 'o')
        && is_char_at_idx(l, i++, 'm');
}

static bool
read_copy_to(struct line * l)
{
    unsigned i = 0;
    return is_char_at_idx(l, i++, 'c')
        && is_char_at_idx(l, i++, 'o')
        && is_char_at_idx(l, i++, 'p')
        && is_char_at_idx(l, i++, 'y')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 't')
        && is_char_at_idx(l, i++, 'o');
}

static bool
read_index_line(struct line * l) {
    unsigned i = 0;

    return is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'n')
        && is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 'x');
}

static bool
is_post_image_add(struct line * l)
{
    return is_char_at_idx(l, 0, '+');
}

static int
is_pre_image_add(struct line * l)
{
    return is_char_at_idx(l, 0, '-');
}

static void
validate_diff_line(struct line * l)
{}

static bool
is_extended_header_new_line(struct line * l)
{
    if (l->data == NULL)
        return false;

    unsigned i = 0;
    return is_char_at_idx(l, i++, 'n')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 'w')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 'f')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'l')
        && is_char_at_idx(l, i++, 'e');
}

static bool
read_delete_line(struct line * l)
{
    if (l->data == NULL)
        return false;

    unsigned i = 0;
    return is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 'l')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 't')
        && is_char_at_idx(l, i++, 'e');
}

static bool
is_similarity_index_line(struct line * l)
{
    if (l->data == NULL)
        return false;

    unsigned i = 0;
    return is_char_at_idx(l, i++, 's')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'm')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'l')
        && is_char_at_idx(l, i++, 'a')
        && is_char_at_idx(l, i++, 'r')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 't')
        && is_char_at_idx(l, i++, 'y')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'n')
        && is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 'x');
}

static bool
is_rename_from_line(struct line * l)
{
    if (l->data == NULL)
        return false;

    unsigned i = 0;
    return is_char_at_idx(l, i++, 'r')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 'n')
        && is_char_at_idx(l, i++, 'a')
        && is_char_at_idx(l, i++, 'm')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 'f')
        && is_char_at_idx(l, i++, 'r')
        && is_char_at_idx(l, i++, 'o')
        && is_char_at_idx(l, i++, 'm');
}

static bool
is_rename_to_line(struct line * l)
{
    if (l->data == NULL)
        return false;

    unsigned i = 0;
    return is_char_at_idx(l, i++, 'r')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 'n')
        && is_char_at_idx(l, i++, 'a')
        && is_char_at_idx(l, i++, 'm')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 't')
        && is_char_at_idx(l, i++, 'o');
}

static bool
is_dissimiliarity_index_line(struct line * l)
{
    if (l->data == NULL)
        return false;

    unsigned i = 0;
    return is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 's')
        && is_char_at_idx(l, i++, 's')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'm')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'l')
        && is_char_at_idx(l, i++, 'a')
        && is_char_at_idx(l, i++, 'r')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 't')
        && is_char_at_idx(l, i++, 'y')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'n')
        && is_char_at_idx(l, i++, 'd')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 'x');
}

static bool
is_binary_file(struct line * l)
{
    if (l->data == NULL)
        return false;

    /* TODO Verify that this is the only format of Binary file output. Also add filename checks */
    unsigned i = 0;
    return is_char_at_idx(l, i++, 'B')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'n')
        && is_char_at_idx(l, i++, 'a')
        && is_char_at_idx(l, i++, 'r')
        && is_char_at_idx(l, i++, 'y')
        && is_char_at_idx(l, i++, ' ')
        && is_char_at_idx(l, i++, 'f')
        && is_char_at_idx(l, i++, 'i')
        && is_char_at_idx(l, i++, 'l')
        && is_char_at_idx(l, i++, 'e')
        && is_char_at_idx(l, i++, 's')
        && is_char_at_idx(l, i++, ' ');
}

/*
 * One more more extended header lines:
 * old mode <mode>
 * new mode <mode>
 * deleted file mode <mode>
 * new file mode <mode>
 * copy from <path>
 * copy to <path>
 * rename from <path>
 * rename to <path>
 * similarity index <number>
 * dissimilarity index <number>
 * index <hash>..<hash> <mode>
 */
static bool
read_extended_header_lines(struct diff * d)
{
    d->status = DIFF_STATUS_CHANGED;
    for (;;) {
        struct line * l = stdin_read_line();
        if (read_old_mode(l)) {
            /* if old mode, we also expect new mode */
            l = stdin_read_line();
            if (!read_new_mode(l)) {
                na_printf("Expected new mode header line at row %u\n", l->row);
                return false;
            }
        } else if (read_copy_from(l)) {
            /* if copy from then we expect copy to */
            l = stdin_read_line();
            if (!read_copy_to(l)) {
                na_printf("Expected copy to header line at row %u\n", l->row);
                return false;
            }
        } else if (is_extended_header_new_line(l)) {
            d->status = DIFF_STATUS_NEW;
        } else if (read_delete_line(l)) {
            d->status = DIFF_STATUS_DELETED;
        } else if (is_similarity_index_line(l) || is_dissimiliarity_index_line(l)) {
            /* expect rename from, and rename to */
            l = stdin_read_line();
            if (!is_rename_from_line(l)) {
                na_printf("Expected rename from line at row %u\n", l->row);
                return false;
            }
            l = stdin_read_line();
            if (!is_rename_to_line(l)) {
                na_printf("Expected rename to line at row %u\n", l->row);
                return false;
            }
        } else if (read_index_line(l)) {
            /* expect this extended header to be last */
            l = stdin_read_line();

            /* if binary file then we don't to anything else */
            if (!is_binary_file(l)) {
                d->expect_line_changes = true;
                stdin_reset_cur_line();
            }
            return true;
        } else {
            /* found a line which is not an extended header line */
            stdin_reset_cur_line();
            return true;
        }
    }
}

static bool
read_pre_img_line(struct line *l)
{
    if (l->data == NULL)
        return false;

    /* TODO assert that if diff is new, then --- /dev/null as pre file */
    return true;
}

static bool
read_post_img_line(struct line *l)
{
    if (l->data == NULL)
        return false;

    /* TODO assert that if diff is deleted, then +++ /dev/null as post file */
    return true;
}

/*
 * It is in the format @@ from-file-range to-file-range @@ [header].
 * The from-file-range is in the form -<start line>,<number of lines>, and to-file-range is
 * +<start line>,<number of lines>.
 * Both start-line and number-of-lines refer to position and length of hunk in preimage
 * and postimage, respectively.
 * If number-of-lines not shown it means that it is 0.
 */
static bool
set_hunk_header(struct hunk * c, struct line * l)
{
    /* @@ -1,8 +1 @@ */
    unsigned i = 0;
    try_ret(is_char_at_idx(l, i++, '@'));
    try_ret(is_char_at_idx(l, i++, '@'));
    try_ret(is_char_at_idx(l, i++, ' '));

    try_ret(is_char_at_idx(l, i++, '-'));
    unsigned a_line_nr;
    try_ret(get_number(l, &i, &a_line_nr));
    /* this part is optional */
    unsigned a_num_lines = 0;
    if (get_char(l, i) == ',') {
        try_ret(is_char_at_idx(l, i++, ','));
        try_ret(get_number(l, &i, &a_num_lines));
    }
    try_ret(is_char_at_idx(l, i++, ' '));

    try_ret(is_char_at_idx(l, i++, '+'));
    unsigned b_line_nr;
    try_ret(get_number(l, &i, &b_line_nr));
    /* this part is optional */
    unsigned b_num_lines = 0;
    if (get_char(l, i) == ',') {
        try_ret(is_char_at_idx(l, i++, ','));
        try_ret(get_number(l, &i, &b_num_lines));
    }
    /* end optional part */

    try_ret(is_char_at_idx(l, i++, ' '));
    try_ret(is_char_at_idx(l, i++, '@'));
    try_ret(is_char_at_idx(l, i++, '@'));

    *c = (struct hunk) {
        .pre_line_nr = a_line_nr,
        .pre_num_lines = a_num_lines,
        .post_line_nr = b_line_nr,
        .post_num_lines = b_num_lines,
        .cla = {0},
        .section_name = NULL,
    };

    /* get optional section name */
    if (i + 1 < l->len) {
        try_ret(l->data[i++] == ' ');
        unsigned size = l->len - i;
        char * section = malloc(sizeof(char) * size);
        memcpy(section, l->data + i, size - 1);
        section[size - 1] = '\0';
        c->section_name = section;
    }

    return true;
}

static bool
set_diff_header(struct diff * d, struct line * hdr)
{
    /* we only accept git diff -p, where -p is default */
    unsigned i = 0;
    try_ret(is_char_at_idx(hdr, i++, 'd'));
    try_ret(is_char_at_idx(hdr, i++, 'i'));
    try_ret(is_char_at_idx(hdr, i++, 'f'));
    try_ret(is_char_at_idx(hdr, i++, 'f'));
    try_ret(is_char_at_idx(hdr, i++, ' '));
    try_ret(is_char_at_idx(hdr, i++, '-'));
    try_ret(is_char_at_idx(hdr, i++, '-'));
    try_ret(is_char_at_idx(hdr, i++, 'g'));
    try_ret(is_char_at_idx(hdr, i++, 'i'));
    try_ret(is_char_at_idx(hdr, i++, 't'));
    try_ret(is_char_at_idx(hdr, i++, ' '));

    /* pre image name */
    unsigned start_pos = i;
    unsigned cur_pos = start_pos;

    while (true) {
        int ch = get_char(hdr, cur_pos);
        try_ret_int(ch);

        if (ch == ' ')
            break;

        cur_pos++;
    }

    /* I think this is a bit ugly, we should +1 on pre_image_size instead to make up for the \0 */
    cur_pos++;

    unsigned pre_image_size = cur_pos - start_pos;
    char * pre_img_name = malloc(sizeof(char) * pre_image_size);
    memcpy(pre_img_name, hdr->data + start_pos, pre_image_size - 1);
    pre_img_name[pre_image_size - 1] = '\0';

    /* go backwards through pre_img_name to (potentially) find last '/' */
    char * short_pre_img_name = pre_img_name;
    for (int b = pre_image_size - 1; b > 0; b--) {
        if (pre_img_name[b] == '/') {
            short_pre_img_name = &pre_img_name[b+1];
            break;
        }
    }

    /* post image name */
    start_pos = cur_pos;
    cur_pos = hdr->len;

    unsigned post_image_size = cur_pos - start_pos;
    char * post_img_name = malloc(sizeof(char) * post_image_size);
    memcpy(post_img_name, hdr->data + start_pos, post_image_size - 1);
    post_img_name[post_image_size - 1] = '\0';

    /* go backwards through post_img_name to (potentially) find last '/' */
    char * short_post_img_name = post_img_name;
    for (int b = post_image_size - 1; b > 0; b--) {
        if (post_img_name[b] == '/') {
            short_post_img_name = &post_img_name[b+1];
            break;
        }
    }

    *d = (struct diff) {
        .ca = {0},
        .pre_img_name = pre_img_name,
        .short_pre_img_name = short_pre_img_name,
        .short_post_img_name = short_post_img_name,
        .post_img_name = post_img_name,
    };

    return true;
}

static char *
extract_and_allocate_code_line(struct line * l)
{
    /* + 1 for '\0' but -1 for '\n' */
    unsigned size = l->len + 1 - 1 - 1; /* smaller since no '+', '-' or ' ' */
    char * start = l->data + 1; /* start copying from pos 1 */
    char * line = malloc(sizeof(char) * size);
    // TODO think of sizeof(char) here as well
    memcpy(line, start, size - 1);
    line[size - 1] = '\0';
    return line;
}

static enum hunk_line_type get_hunk_line_type(struct line * l)
{
    if (is_pre_image_add(l))
        return PRE_LINE;
    else if (is_post_image_add(l))
        return POST_LINE;
    else
        return NEUTRAL_LINE;
}

static bool
read_hunk_line(struct line * l, struct hunk * c)
{
    enum hunk_line_type lt = get_hunk_line_type(l);

    char * code = extract_and_allocate_code_line(l);

    struct hunk_line * cl = allocate_hunk_line(&c->cla);
    *cl = (struct hunk_line) {
        .line = code,
        .len = strlen(code),
        .type = lt,
    };

    return true;
}


enum parse_state {
    PARSE_STATE_EXPECT_DIFF,
    PARSE_STATE_EXPECT_HUNK,
    PARSE_STATE_ACCEPT_ALL
};

static bool
parse_start(struct diff_array * da)
{
    struct line * l = stdin_read_line();

    if (l->data == NULL) {
        na_printf("No data to parse\n");
        return false;
    }

    stdin_reset_cur_line();

    enum parse_state state = PARSE_STATE_EXPECT_DIFF;

    struct diff * d = NULL;
    struct hunk * c = NULL;
    for (;;) {
        struct line * l = stdin_read_line();

        switch (state) {
            case PARSE_STATE_EXPECT_DIFF: {
                if (!is_diff_header(l)) {
                    na_printf("Expected diff header at row %u\n", l->row);
                    return false;
                }

                d = allocate_diff(da);
                if (!set_diff_header(d, l)) {
                    na_printf("Could not set diff header at row %u\n", l->row);
                    return false;
                }

                try_ret(read_extended_header_lines(d));

                if (!d->expect_line_changes) {
                    l = stdin_read_line();

                    if (l->data == NULL)
                        return true;

                    stdin_reset_cur_line();

                    /* Goto next diff */
                    continue;
                }

                l = stdin_read_line();
                if (!read_pre_img_line(l)) {
                   na_printf("Could not parse pre image line at row %u \n", l->row);
                   return false;
                }

                l = stdin_read_line();
                if (!read_post_img_line(l)) {
                   na_printf("Could not parse post image line at row %u \n", l->row);
                   return false;
                }

                state = PARSE_STATE_EXPECT_HUNK;
                break;
            }
            case PARSE_STATE_EXPECT_HUNK: {
                if (!is_hunk_header(l)) {
                    na_printf("Expected hunk header at row %u\n", l->row);
                    return false;
                }

                c = allocate_hunk(&d->ca);

                if (!set_hunk_header(c, l)) {
                    na_printf("Failed to set hunk header at row %u\n", l->row);
                    return false;
                }

                l = stdin_read_line();
                try_ret(read_hunk_line(l, c));

                state = PARSE_STATE_ACCEPT_ALL;

                break;
            }
            case PARSE_STATE_ACCEPT_ALL: {
                if (l->data == NULL)
                    return true;

                if (is_diff_header(l)) {
                    state = PARSE_STATE_EXPECT_DIFF;
                    stdin_reset_cur_line();
                }
                else if (is_hunk_header(l)) {
                    state = PARSE_STATE_EXPECT_HUNK;
                    stdin_reset_cur_line();
                }
                else
                    try_ret(read_hunk_line(l, c));

                break;
            }
        }
    }

    return true;
}

static bool
change_pre_post_lines(struct hunk_line_array * cla,
        unsigned pre_start_idx,
        unsigned post_start_idx,
        unsigned size)
{

    for (unsigned i = pre_start_idx; i < pre_start_idx + size; ++i) {
        struct hunk_line * cl = &cla->data[i];
        if (cl->type != PRE_LINE) {
            na_printf("Expected PRE_LINE\n");
            return false;
        }
        cl->type = PRE_CHANGED_LINE;
    }

    for (unsigned i = post_start_idx; i < post_start_idx + size; ++i) {
        struct hunk_line * cl = &cla->data[i];
        if (cl->type != POST_LINE) {
            na_printf("Expected POST_LINE\n");
            return false;
        }
        cl->type = POST_CHANGED_LINE;
    }

    return true;
}

/* Look for PRE_LINES directly followed by matching number of POST_LINES, change all of them to
 * PRE_/POST_CHANGED_LINE instead.
 */
static bool
find_changes_in_hunk(struct hunk_line_array * cla)
{
    enum ongoing_state {
        STATE_ONGOING_START,
        STATE_ONGOING_PRE,
        STATE_ONGOING_POST
    };

    enum ongoing_state state = STATE_ONGOING_START;

    unsigned start_pre_idx = 0;
    unsigned start_post_idx = 0;

    unsigned num_pres = 0;
    unsigned num_posts = 0;

    for (unsigned i = 0; i < cla->size; ++i) {
        struct hunk_line * cl = &cla->data[i];

        if (cl->type == PRE_LINE) {
            if (state == STATE_ONGOING_POST) {
                na_printf("Error when parsing changes. This should not happen\n");
                return false;
            }

            if (state == STATE_ONGOING_START) {
                start_pre_idx = i;
                state = STATE_ONGOING_PRE;
            }
        } else if (cl->type == POST_LINE) {
            if (state == STATE_ONGOING_PRE) {
                num_pres = i - start_pre_idx;
                start_post_idx = i;
                state = STATE_ONGOING_POST;
            }
        } else {
            if (state == STATE_ONGOING_POST) {
                num_posts = i - start_post_idx;
                if (num_pres == num_posts)
                    try_ret(change_pre_post_lines(cla, start_pre_idx, start_post_idx, num_pres));
            }

            if (state != STATE_ONGOING_START)
                state = STATE_ONGOING_START;
        }
    }

    return true;
}

static bool
find_changes(struct diff_array * da)
{
    for (unsigned i = 0; i < da->size; ++i) {
        struct diff * d = &da->data[i];

        for (unsigned j = 0; j < d->ca.size; ++j) {
            struct hunk * c = &d->ca.data[j];

            try_ret(find_changes_in_hunk(&c->cla));
        }
    }

    return true;
}


bool
parse_stdin(struct diff_array * da)
{
    if (isatty(fileno(stdin))) {
        na_printf("stdin should be a pipe! E.g. do 'git diff | nadiff'\n");
        return false;
    }

    try_ret(parse_start(da));

    try_ret(find_changes(da));

    return true;
}
