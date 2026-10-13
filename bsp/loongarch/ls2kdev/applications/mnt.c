#include <rthw.h>
#include <rtthread.h>
#include <klibc/kerrno.h>

#ifdef PKG_USING_LWEXT4

#include <dfs.h>
#include <dfs_fs.h>
#include <dfs_file.h>
#include <dfs_mnt.h>
#include <ext4.h>

int mount_ssd(void)
{
    // 获取块设备
    struct blk_device *blkdev = rt_device_find("ls_ahci_blk");

    if (blkdev == RT_NULL)
    {
        rt_kprintf("mount device not found\n");
        return -RT_ERROR;
    }

    // 挂载块设备
    dfs_mount("ls_ahci_blk","/","ext",0,(void *)1);

    return 0;
}
INIT_ENV_EXPORT(mount_ssd);

#endif
