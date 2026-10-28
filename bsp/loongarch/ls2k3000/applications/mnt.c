/*
 * Copyright (C) 2020-2026 Loongson Technology Corporation Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author                    Notes
 * 2026-10-28     LoongsonLab               align file header with LS2K0300
 */

/*
 * Description:
 *   Expose the embedded UI through ROMFS and keep preferences in temporary storage.
 */

#include <rtthread.h>

#ifdef BSP_USING_NOVAUI
#include <dfs_fs.h>
#include <dfs_romfs.h>
#include <dfs_pcache.h>
#include <errno.h>

extern const unsigned char novaui_image[];
extern const unsigned long novaui_image_size;

static struct romfs_dirent applications[] = {
    {ROMFS_DIRENT_FILE, "novaui.elf", novaui_image, 0},
};

static const struct romfs_dirent directories[] = {
    {ROMFS_DIRENT_DIR, "dev", RT_NULL, 0},
    {ROMFS_DIRENT_DIR, "mnt", RT_NULL, 0},
    {ROMFS_DIRENT_DIR, "proc", RT_NULL, 0},
    {ROMFS_DIRENT_DIR, "etc", RT_NULL, 0},
    {ROMFS_DIRENT_DIR, "bin", (const rt_uint8_t *)applications, 1},
};

static const struct romfs_dirent ui_root = {
    ROMFS_DIRENT_DIR, "/", (const rt_uint8_t *)directories,
    sizeof(directories) / sizeof(directories[0]),
};

static const struct dfs_filesystem_ops *rom_ops;
static struct dfs_filesystem_ops ui_ops;
static struct dfs_filesystem_type ui_filesystem;

static ssize_t image_page_read(struct dfs_file *file, struct dfs_page *page)
{
    const struct romfs_dirent *entry = file->vnode->data;
    rt_size_t bytes;

    if (!page->page || page->fpos < 0 || entry->type != ROMFS_DIRENT_FILE)
    {
        return -EINVAL;
    }

    /* The shared page cache does not clear newly allocated pages. */
    rt_memset(page->page, 0, page->size);

    if ((rt_size_t)page->fpos >= entry->size)
    {
        return 0;
    }

    bytes = entry->size - (rt_size_t)page->fpos;

    if (bytes > page->size)
    {
        bytes = page->size;
    }

    rt_memcpy(page->page, entry->data + page->fpos, bytes);
    return bytes;
}

static ssize_t image_page_write(struct dfs_page *page)
{
    (void)page;
    return -EROFS;
}

static const struct dfs_aspace_ops image_page_ops = {
    .read = image_page_read,
    .write = image_page_write,
};

static struct dfs_vnode *image_lookup(struct dfs_dentry *dentry)
{
    struct dfs_vnode *vnode = rom_ops->lookup(dentry);

    if (vnode && vnode->type == FT_REGULAR)
    {
        vnode->aspace = dfs_aspace_create(dentry, vnode, &image_page_ops);

        if (!vnode->aspace)
        {
            dfs_vnode_destroy(vnode);
            return RT_NULL;
        }
    }

    return vnode;
}

static int filesystem_init(void)
{
    applications[0].size = novaui_image_size;

    /* DFS ROMFS supplies directory operations; this board adds the read-only
     * page source required by the Smart ELF loader, leaving shared DFS intact.
     */
    for (struct dfs_filesystem_type *fs = dfs_filesystems(); fs; fs = fs->next)
    {
        if (!rt_strcmp(fs->fs_ops->name, "rom"))
        {
            rom_ops = fs->fs_ops;
            break;
        }
    }

    if (!rom_ops)
    {
        return -RT_ENOSYS;
    }

    ui_ops = *rom_ops;
    ui_ops.name = "uirom";
    ui_ops.lookup = image_lookup;
    ui_filesystem.fs_ops = &ui_ops;

    if (dfs_register(&ui_filesystem) != 0 ||
        dfs_mount(RT_NULL, "/", "uirom", MS_RDONLY, &ui_root) != 0 ||
        dfs_mount(RT_NULL, "/mnt", "tmp", 0, RT_NULL) != 0)
    {
        rt_kprintf("NOVAUI: cannot mount temporary filesystem\n");
        return -RT_ERROR;
    }

    return RT_EOK;
}

INIT_ENV_EXPORT(filesystem_init);
#endif
