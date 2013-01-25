/*
 * test-ioctl-tree
 *
 * Copyright (C) 2013 Canonical Ltd.
 * Author: Martin Pitt <martin.pitt@ubuntu.com>
 *
 * umockdev is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * umockdev is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

#include "ioctl_tree.h"

/* test ioctl data */
const struct usbdevfs_connectinfo ci = { 11, 0 };
const struct usbdevfs_connectinfo ci2 = { 12, 0 };
const struct usbdevfs_urb s_out1 = { 1, 2, 0, 0, "what", 4, 4 };
const struct usbdevfs_urb s_in1a = { 1, 129, 0, 0, "this\0\0\0\0\0\0", 10, 4 };
const struct usbdevfs_urb s_in1b = { 1, 129, 0, 0, "andthat\0\0\0", 10, 7 };
const struct usbdevfs_urb s_out2 = { 1, 2, 0, 0, "readfile", 8, 8 };
const struct usbdevfs_urb s_in2a = { 1, 129, 0, 0, "file1a\0\0\0\0\0\0\0\0\0", 15, 6 };
const struct usbdevfs_urb s_in2b = { 1, 129, 0, 0, "file1bb\0\0\0\0\0\0\0\0", 15, 7 };
const struct usbdevfs_urb s_in2c = { 1, 129, 0, 0, "file1ccc\0\0\0\0\0\0\0", 15, 8 };
const struct usbdevfs_urb s_in3  = { 1, 129, 0, 0, "file2\0\0\0\0\0\0\0\0\0\0", 15, 5 };

/* REAPURB expects a pointer to a urb struct pointer */
const struct usbdevfs_urb *out1 = &s_out1, *in1a = &s_in1a, *in1b = &s_in1b,
                          *out2 = &s_out2, *in2a = &s_in2a, *in2b = &s_in2b,
                          *in2c = &s_in2c, *in3 = &s_in3;

const gchar test_tree_str[] = "USBDEVFS_CONNECTINFO 11 0\n"
        "USBDEVFS_REAPURB 1 2 0 0 4 4 0 77686174\n"
        " USBDEVFS_REAPURB 1 129 0 0 10 4 0 74686973\n"
        "  USBDEVFS_REAPURB 1 129 0 0 10 7 0 616E6474686174\n"
        "USBDEVFS_REAPURB 1 2 0 0 8 8 0 7265616466696C65\n"
        " USBDEVFS_REAPURB 1 129 0 0 15 6 0 66696C653161\n"
        "  USBDEVFS_REAPURB 1 129 0 0 15 7 0 66696C65316262\n"
        "   USBDEVFS_REAPURB 1 129 0 0 15 8 0 66696C6531636363\n"
        " USBDEVFS_REAPURB 1 129 0 0 15 5 0 66696C6532\n"
        "USBDEVFS_CONNECTINFO 12 0\n";


static void
t_type_get_by (void)
{
    g_assert (ioctl_type_get_by_id (-1) == NULL);
    g_assert_cmpstr (ioctl_type_get_by_id (USBDEVFS_CONNECTINFO)->name, ==, "USBDEVFS_CONNECTINFO");
    g_assert_cmpstr (ioctl_type_get_by_id (USBDEVFS_REAPURBNDELAY)->name, ==, "USBDEVFS_REAPURBNDELAY");

    g_assert (ioctl_type_get_by_name ("no_such_ioctl") == NULL);
    g_assert_cmpint (ioctl_type_get_by_name ("USBDEVFS_CONNECTINFO")->id, ==, USBDEVFS_CONNECTINFO);
    g_assert_cmpint (ioctl_type_get_by_name ("USBDEVFS_REAPURBNDELAY")->id, ==, USBDEVFS_REAPURBNDELAY);
}

#if 0
static void print_tree (const char* description, const ioctl_tree *tree)
{
    size_t i;
    ioctl_tree *t;

    printf("------ tree: %s --------\n", description);
    ioctl_tree_write (stdout, tree);
    puts("\n#### add stack:");
    for (i = tree->last_added->n; i > 0; --i) {
        t = ioctl_node_list_get (tree->last_added, i-1);
        printf ("%s[", t->type->name);
        t->type->write (t, stdout);
        puts ("]");
    }
    puts("----------------------------");
}
#endif

#define assert_node(n,p,c,nx) \
    g_assert (n->parent == p);      \
    g_assert (n->child == c);        \
    g_assert (n->next == nx); 

static void
t_create_from_bin (void)
{
    ioctl_tree *tree = NULL;
    ioctl_tree *n_ci, *n_ci2, *n_out1, *n_in1a, *n_in1b, *n_out2, *n_in2a,
               *n_in2b, *n_in2c, *n_out2_2, *n_in3;

    /* inputs should always follow (child) their outputs; outputs should be
     * top level nodes, but they should stay in order of addition; different
     * possibile inputs for an output chould be represented as alternative
     * children:
     *
     * (R) ---------------------
     *  | \      \              \
     *  v  v      v              v
     * CI OUT1   OUT2 -----     CI2
     *     |      |        \
     *     v      v         v
     *    IN1a   IN2a      IN3
     *     |      |
     *     v      v
     *    IN1b   IN2b
     *            |
     *            v
     *           IN2c
     */

    /* add ci */
    tree = n_ci = ioctl_tree_new_from_bin (USBDEVFS_CONNECTINFO, &ci);
    g_assert (ioctl_tree_insert (NULL, n_ci) == NULL);
    g_assert (n_ci->data != &ci); /* data should get copied */
    assert_node (n_ci, NULL, NULL, NULL);

    /* add out1, in1a, in1b */
    n_out1 = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &out1);
    g_assert (ioctl_tree_insert (tree, n_out1) == NULL);
    g_assert (n_out1->data != &out1); /* data should get copied */
    assert_node (n_out1, tree, NULL, NULL);
    g_assert (n_ci->next == n_out1);

    n_in1a = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in1a);
    g_assert (ioctl_tree_insert (tree, n_in1a) == NULL);
    assert_node (n_in1a, n_out1, NULL, NULL);
    assert_node (n_out1, tree, n_in1a, NULL);

    n_in1b = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in1b);
    g_assert (ioctl_tree_insert (tree, n_in1b) == NULL);

    /* add CI again, should not change anything */
    n_ci2 = ioctl_tree_new_from_bin (USBDEVFS_CONNECTINFO, &ci);
    g_assert (ioctl_tree_insert (tree, n_ci2) == n_ci);
    g_assert (n_ci2->parent == NULL);
    g_assert (tree == n_ci);

    /* add out2, should become a new top-level node */
    n_out2 = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &out2);
    g_assert (ioctl_tree_insert (tree, n_out2) == NULL);
    assert_node (n_out2, tree, NULL, NULL);

    /* add in2a and in2b */
    n_in2a = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in2a);
    g_assert (ioctl_tree_insert (tree, n_in2a) == NULL);
    n_in2b = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in2b);
    g_assert (ioctl_tree_insert (tree, n_in2b) == NULL);

    /* and append in2c, with an interjected ci */
    n_ci2 = ioctl_tree_new_from_bin (USBDEVFS_CONNECTINFO, &ci2);
    g_assert (ioctl_tree_insert (tree, n_ci2) == NULL);
    n_in2c = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in2c);
    g_assert (ioctl_tree_insert (tree, n_in2c) == NULL);

    /* add out2 again, should not get added but should become "last added" */
    n_out2_2 = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &out2);
    g_assert (ioctl_tree_insert (tree, n_out2_2) == n_out2);

    /* add in3, should become alternative of out2 */
    n_in3 = ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in3);
    g_assert (ioctl_tree_insert (tree, n_in3) == NULL);

    /* check tree structure */
    g_assert (tree == n_ci);
    assert_node (n_ci, NULL, NULL, n_out1);
    assert_node (n_out1, tree, n_in1a, n_out2);
    assert_node (n_in1a, n_out1, n_in1b, NULL);
    assert_node (n_in1b, n_in1a, NULL, NULL);
    assert_node (n_out2, tree, n_in2a, n_ci2);
    assert_node (n_in2a, n_out2, n_in2b, n_in3);
    assert_node (n_in2b, n_in2a, n_in2c, NULL);
    assert_node (n_in2c, n_in2b, NULL, NULL);
    assert_node (n_in3, n_out2, NULL, NULL);
    assert_node (n_ci2, tree, NULL, NULL);
}

static void
t_write (void)
{
    ioctl_tree *tree = NULL;
    FILE* f;
    char contents[1000];

    /* same tree as in t_create_from_bin() */
    tree = ioctl_tree_new_from_bin (USBDEVFS_CONNECTINFO, &ci);
    ioctl_tree_insert (NULL, tree);
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &out1));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in1a));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in1b));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &out2));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in2a));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in2b));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_CONNECTINFO, &ci2));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in2c));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &out2));
    ioctl_tree_insert (tree, ioctl_tree_new_from_bin (USBDEVFS_REAPURB, &in3));

    f = tmpfile();
    g_assert (f != NULL);
    ioctl_tree_write (f, tree);
    rewind (f);
    memset (contents, 0, sizeof (contents));
    g_assert_cmpint (fread (contents, 1, sizeof (contents), f), >, 10);
    g_assert_cmpstr (contents, ==, test_tree_str);

    fclose (f);
}

static void
t_read (void)
{
    ioctl_tree *tree;
    FILE* f;
    char contents[1000];

    f = tmpfile();
    g_assert (f != NULL);
    g_assert_cmpint (fwrite (test_tree_str, strlen (test_tree_str), 1, f), ==, 1);
    rewind (f);
    tree = ioctl_tree_read (f);
    g_assert (tree != NULL);

    /* write it into the tempfile and read it back to compare with original
     * (easier than comparing nodes) */
    rewind (f);
    ftruncate (fileno (f), 0);
    ioctl_tree_write (f, tree);
    rewind (f);
    memset (contents, 0, sizeof (contents));
    g_assert_cmpint (fread (contents, 1, sizeof (contents), f), >, 10);
    g_assert_cmpstr (contents, ==, test_tree_str);

    fclose (f);
}

#define assert_ci(n,d) { \
    struct usbdevfs_connectinfo *nd = n->data;      \
    g_assert (n->type->id == USBDEVFS_CONNECTINFO); \
    g_assert_cmpint (nd->devnum, ==, (d)->devnum);    \
    g_assert_cmpint (nd->slow, ==, (d)->slow);        \
}

#define assert_urb(n,urb) { \
    g_assert (n != NULL);                       \
    struct usbdevfs_urb *nd = n->data;          \
    g_assert (n->type->id == USBDEVFS_REAPURB); \
    g_assert (nd->endpoint == (urb)->endpoint);   \
    g_assert (nd->status == (urb)->status);       \
    g_assert (nd->flags == (urb)->flags);         \
    g_assert (nd->buffer_length == (urb)->buffer_length);   \
    g_assert (nd->actual_length == (urb)->actual_length);   \
    g_assert (memcmp (nd->buffer, (urb)->buffer, nd->buffer_length) == 0);   \
}

static void
t_iteration (void)
{
    FILE* f;
    ioctl_tree *tree, *i;

    f = tmpfile();
    g_assert (f != NULL);
    g_assert_cmpint (fwrite (test_tree_str, strlen (test_tree_str), 1, f), ==, 1);
    rewind (f);
    tree = ioctl_tree_read (f);
    fclose (f);
    g_assert (tree != NULL);

    i = tree;
    assert_ci (i, &ci);
    i = ioctl_tree_next (i);
    assert_urb (i, &s_out1);
    i = ioctl_tree_next (i);
    assert_urb (i, &s_in1a);
    i = ioctl_tree_next (i);
    assert_urb (i, &s_in1b);
    i = ioctl_tree_next (i);
    assert_urb (i, &s_out2);
    i = ioctl_tree_next (i);
    assert_urb (i, &s_in2a);
    i = ioctl_tree_next (i);
    assert_urb (i, &s_in2b);
    i = ioctl_tree_next (i);
    assert_urb (i, &s_in2c);
    i = ioctl_tree_next (i);
    assert_urb (i, &s_in3);
    i = ioctl_tree_next (i);
    assert_ci (i, &ci2);

    g_assert (ioctl_tree_next (i) == NULL);
    g_assert (ioctl_tree_next_wrap (tree, i) == tree);
}

int
main (int argc, char **argv)
{
#if !defined(GLIB_VERSION_2_36)
  g_type_init ();
#endif
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/umockdev-ioctl-tree/type_get_by", t_type_get_by);
  g_test_add_func ("/umockdev-ioctl-tree/create_from_bin", t_create_from_bin);
  g_test_add_func ("/umockdev-ioctl-tree/write", t_write);
  g_test_add_func ("/umockdev-ioctl-tree/read", t_read);
  g_test_add_func ("/umockdev-ioctl-tree/iteration", t_iteration);

  return g_test_run ();
}