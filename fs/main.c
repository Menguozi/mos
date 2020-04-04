/*************************************************************************//**
 *****************************************************************************
 * @file   main.c
 * @brief  
 * @author Forrest Y. Yu
 * @date   2007
 *****************************************************************************
 *****************************************************************************/

#include "type.h"
#include "config.h"
#include "const.h"
#include "protect.h"
#include "string.h"
#include "fs.h"
#include "proc.h"
#include "tty.h"
#include "console.h"
#include "global.h"
#include "proto.h"

#include "hd.h"

/* 将dst_起始的size个字节置为value */
static void fs_memset(void* dst_, uint8_t value, uint32_t size) {
   assert(dst_ != NULL);
   uint8_t* dst = (uint8_t*)dst_;
   while (size-- > 0)
      *dst++ = value;
}

/* 将src_起始的size个字节复制到dst_ */
static void fs_memcpy(void* dst_, const void* src_, uint32_t size) {
   assert(dst_ != NULL && src_ != NULL);
   uint8_t* dst = dst_;
   const uint8_t* src = src_;
   while (size-- > 0)
      *dst++ = *src++;
}

/* 连续比较以地址a_和地址b_开头的size个字节,若相等则返回0,若a_大于b_返回+1,否则返回-1 */
static int fs_memcmp(const void* a_, const void* b_, uint32_t size) {
   const char* a = a_;
   const char* b = b_;
   assert(a != NULL || b != NULL);
   while (size-- > 0) {
      if(*a != *b) {
	 return *a > *b ? 1 : -1; 
      }
      a++;
      b++;
   }
   return 0;
}

/* 将字符串从src_复制到dst_ */
static char* fs_strcpy(char* dst_, const char* src_) {
   assert(dst_ != NULL && src_ != NULL);
   char* r = dst_;		       // 用来返回目的字符串起始地址
   while((*dst_++ = *src_++));
   return r;
}

/* 比较两个字符串,若a_中的字符大于b_中的字符返回1,相等时返回0,否则返回-1. */
static int8_t fs_strcmp (const char* a, const char* b) {
   assert(a != NULL && b != NULL);
   while (*a != 0 && *a == *b) {
      a++;
      b++;
   }
/* 如果*a小于*b就返回-1,否则就属于*a大于等于*b的情况。在后面的布尔表达式"*a > *b"中,
 * 若*a大于*b,表达式就等于1,否则就表达式不成立,也就是布尔值为0,恰恰表示*a等于*b */
   return *a < *b ? -1 : *a > *b;
}

/** Chunk size in number of bytes. */
#define BLOCK_SIZE 512

/* define fields in flags_ require sync directory entry. */
#define F_OFLAG (O_RDWR | O_APPEND | O_SYNC)
#define F_FILE_DIR_DIRTY 0x80

/** Value for byte 510 and 511 of boot block or MBR. */
#define BOOTSIG 0x55aa

#define CACHE_FOR_READ  0    /* cache a block for read. */
#define CACHE_FOR_WRITE 1    /* cache a block and set dirty. */

/* FAT16 end of chain value used by Microsoft. */
#define EOC16 0xffff

/* Minimum value for FAT16 EOC.  Use to test for EOC. */
#define EOC16_MIN 0xfff8

/* FAT32 end of chain value used by Microsoft. */
#define EOC32 0x0fffffff

/* Minimum value for FAT32 EOC.  Use to test for EOC. */
#define EOC32_MIN 0x0ffffff8

/* Mask a for FAT32 entry. Entries are 28 bits. */
#define ENTRY32_MASK 0x0fffffff

/* Type name for directoryEntry. */

/* escape for name[0] = 0xe5. */
#define DIR_NAME_0XE5 0x05

/* name[0] value for entry that is free after being "deleted". */
#define DIR_NAME_DELETED 0xe5

/* name[0] value for entry that is free and no allocated entries follow. */
#define DIR_NAME_FREE 0x00

/* Test value for long name entry. */
#define DIR_ATTR_LONG_NAME 0x0f

/* Test mask for long name entry. */
#define DIR_ATTR_LONG_NAME_MASK 0x3f

/* defined attribute bits. */
#define DIR_ATTR_DEFINED_BITS 0x3f

/* Mask for file/subdirectory tests. */
#define DIR_ATTR_FILE_TYPE_MASK (DIR_ATTR_VOLUME_ID | DIR_ATTR_DIRECTORY)
#define DIR_ATTR_SKIP           (DIR_ATTR_VOLUME_ID | DIR_ATTR_DIRECTORY)

/** Default date for file timestamps is 1 Jan 2000. */
#define DEFAULT_DATE (((2000 - 1980) << 9) | (1 << 5) | 1)

/** Default time for file timestamp is 1 am. */
#define DEFAULT_TIME (1 << 11)



/**
 * Block read function callback.
 */
ssize_t read(void *dst_p,
               uint32_t src_block)
{
	RD_SECT(ROOT_DEV, src_block);
	fs_memcpy(dst_p, fsbuf, BLOCK_SIZE);
	return BLOCK_SIZE;
}

/**
 * Block write function callback.
 */
ssize_t write(uint32_t dst_block,
                         const void *src_p)
{
	fs_memcpy(fsbuf, src_p, BLOCK_SIZE);
	WR_SECT(ROOT_DEV, dst_block);
	return BLOCK_SIZE;
}

static int is_end_of_cluster(fat_t cluster)
{
    return (cluster >= 0xfff8);
}

/**
 * Directory entry is part of a long name.
 */
static inline int dir_is_long_name(const struct dir_t* dir_p)
{
    return ((dir_p->attributes & DIR_ATTR_LONG_NAME_MASK)
            == DIR_ATTR_LONG_NAME);
}

/**
 * Directory entry is for a file.
 */
static inline int dir_is_file(const struct dir_t* dir_p)
{
    return ((dir_p->attributes & DIR_ATTR_FILE_TYPE_MASK) == 0);
}

/**
 * Directory entry is for a subdirectory.
 */
static inline int dir_is_subdir(const struct dir_t* dir_p)
{
    return ((dir_p->attributes & DIR_ATTR_FILE_TYPE_MASK)
            == DIR_ATTR_DIRECTORY);
}

/**
 * Directory entry is for a file or subdirectory
 */
static inline int dir_is_file_or_subdir(const struct dir_t* dir_p)
{
    return ((dir_p->attributes & DIR_ATTR_VOLUME_ID) == 0);
}

/**
 * Create a 8.3 file name from given name.
 */
static int make_83_name(const char* str_p,
                        ssize_t size,
                        uint8_t* name_p)
{
    uint8_t c;
    uint8_t n = 7;
    uint8_t i;
    uint8_t b;
    int pos;
    const char *invalid_p;

    fs_memset(name_p, ' ', 11);

    i = 0;

    for (pos = 0; pos < size; pos++) {
        c = str_p[pos];

        if (c == '.') {
            if (n == 10) {
                return (0);
            }

            n = 10;
            i = 8;
        } else {
            /* Invalid characters in file name. */
            invalid_p = "|<>^+=?/[];,*\"\\";

            while ((b = *invalid_p++) != '\0') {
                if (b == c) {
                    return (-1);
                }
            }

            /* Invalid characters in file name. */
            if (i > n || c < 0x21 || c > 0x7e) {
                return (-1);
            }

            /* Add valid character to name. */
            name_p[i++] = ((c < 'a' || c > 'z')
                           ?  c
                           : c + ('A' - 'a'));
        }
    }

    return (name_p[0] == ' ');
}

/**
 * Create file name from given 8.3 file name.
 */
static int make_name_from_83(char *dst_p, const char *dname_p)
{
    int i, pos;

    pos = 0;

    /* Set name. */
    for (i = 0; i < 11; i++) {
        if (dname_p[i] == ' ') {
            continue;
        }

        if (i == 8) {
            dst_p[pos] = '.';
            pos++;
        }

        dst_p[pos] = dname_p[i];
        pos++;
    }

    dst_p[pos] = '\0';

    return (0);
}

static int cache_flush(struct fat16_t *self_p)
{
    struct fat16_cache_t *cache_p = &self_p->cache;

    if (cache_p->dirty) {
        if (self_p->write(cache_p->block_number,
                          cache_p->buffer.data) != BLOCK_SIZE) {
            return (-1);
        }

        if (cache_p->mirror_block) {
            if (self_p->write(cache_p->mirror_block,
                              cache_p->buffer.data) != BLOCK_SIZE) {
                return (-1);
            }

            cache_p->mirror_block = 0;
        }

        cache_p->dirty = 0;
    }

    return (0);
}

static inline uint8_t block_of_cluster(uint8_t blocks_per_cluster,
                                       uint32_t position)
{
    return ((position >> 9) & (blocks_per_cluster - 1));
}

static inline uint16_t cache_data_offset(uint32_t position)
{
    return (position & 0x1ff);
}

static inline void cache_set_dirty(struct fat16_cache_t *cache_p)
{
    cache_p->dirty |= CACHE_FOR_WRITE;
}

static inline uint32_t data_block_lba(struct fat16_file_t *file_p,
                                      uint8_t block_of_cluster)
{
    return (file_p->fat16_p->data_start_block +
            (uint32_t)((file_p->cur_cluster - 2)
                       * file_p->fat16_p->blocks_per_cluster) +
            block_of_cluster);
}

static int cache_raw_block(struct fat16_t *self_p,
                           uint32_t block_number,
                           uint8_t action)
{
    struct fat16_cache_t *cache_p = &self_p->cache;

    if (cache_p->block_number != block_number) {
        if (cache_flush(self_p) != 0) {
            return (-1);
        }

        if (self_p->read(cache_p->buffer.data,
                         block_number) != BLOCK_SIZE) {
            return (-1);
        }

        cache_p->block_number = block_number;
    }

    cache_p->dirty |= action;

    return (0);
}

static int fat_get(struct fat16_t *self_p,
                   fat_t cluster,
                   fat_t* value)
{
    uint32_t lba;

    if (cluster > (self_p->cluster_count + 1)) {
        return (-1);
    }

    lba = self_p->fat_start_block + (cluster >> 8);

    if (lba != self_p->cache.block_number) {
        if (cache_raw_block(self_p, lba, CACHE_FOR_READ)) {
            return (-1);
        }
    }

    *value = self_p->cache.buffer.fat[cluster & 0xff];

    return (0);
}

static int fat_put(struct fat16_t *self_p, fat_t cluster, fat_t value)
{
    uint32_t lba;

    if (cluster < 2) {
        return (-1);
    }

    if (cluster > (self_p->cluster_count + 1)) {
        return (-1);
    }

    lba = self_p->fat_start_block + (cluster >> 8);

    if (lba != self_p->cache.block_number) {
        if (cache_raw_block(self_p, lba, CACHE_FOR_READ) != 0) {
            return (-1);
        }
    }

    self_p->cache.buffer.fat[cluster & 0xff] = value;
    cache_set_dirty(&self_p->cache);

    if (self_p->fat_count > 1) {
        self_p->cache.mirror_block = (lba + self_p->blocks_per_fat);
    }

    return (0);
}

static struct dir_t* cache_dir_entry(struct fat16_t *self_p,
                                     uint16_t block,
                                     uint16_t index,
                                     uint8_t action)
{
    if (cache_raw_block(self_p, block + (index >> 4), action) != 0) {
        return (NULL);
    }

    return (&self_p->cache.buffer.dir[index & 0xf]);
}

static int free_chain(struct fat16_t *self_p, fat_t cluster)
{
    fat_t next;

    while (1) {
        if (fat_get(self_p, cluster, &next) != 0) {
            return (-1);
        }

        if (fat_put(self_p, cluster, 0) != 0) {
            return (-1);
        }

        if (is_end_of_cluster(next)) {
            return (0);
        }

        cluster = next;
    }
}

/**
 * Write the volume start block to disk.
 */
static int write_volume_block(struct fat16_t *self_p,
                              uint32_t volume_start_block,
                              struct fbs_t *fbs_p)
{
    /* Cache volume start block. */
    if (cache_raw_block(self_p, volume_start_block, CACHE_FOR_WRITE) != 0) {
        return (-1);
    }

    /* Write the boot sector to the start block. */
    self_p->cache.buffer.fbs = *fbs_p;

    return (cache_flush(self_p));
}

static int format_fat_blocks(struct fat16_t *self_p,
                             uint32_t fat_start_block,
                             uint32_t fat_end_block)
{
    uint32_t block;

    for (block = fat_start_block; block < fat_end_block; block++) {
        /* Cache the next block within the fat. */
        if (cache_raw_block(self_p, block, CACHE_FOR_WRITE) != 0) {
            return (-1);

        }
        /* Format the block. */
        fs_memset(&self_p->cache.buffer, 0, sizeof(self_p->cache.buffer));

        if (block == fat_start_block) {
            self_p->cache.buffer.fat[0] = 0xfff8;
            self_p->cache.buffer.fat[1] = 0xffff;
        }

        if (cache_flush(self_p) != 0) {
            return (-1);
        }
    }

    return (0);
}

static int format_root_dir_blocks(struct fat16_t *self_p,
                                  uint32_t root_dir_start_block,
                                  uint32_t root_dir_end_block)
{
    uint32_t block;

    for (block = root_dir_start_block; block < root_dir_end_block; block++) {
        /* Cache the next block within the root directory. */
        if (cache_raw_block(self_p, block, CACHE_FOR_WRITE) != 0) {
            return (-1);
        }

        /* Clear the block. */
        fs_memset(&self_p->cache.buffer, 0, sizeof(self_p->cache.buffer));

        /* The flush function writes to the mirrored fat block as well. */
        if (cache_flush(self_p) != 0) {
            return (-1);
        }
    }

    return (0);
}

int fat16_mount(struct fat16_t *self_p)
{
    assert(self_p != NULL);

    uint32_t total_blocks;
    struct bpb_t* bpb_p;

    /* Initialize the cache. */
    self_p->cache.block_number = 0xffffffff;
    self_p->cache.dirty = 0;
    self_p->cache.mirror_block = 0;
    self_p->volume_start_block = 0;

    if (cache_raw_block(self_p,
                            self_p->volume_start_block,
                            CACHE_FOR_READ) != 0) {
            return (-1);
        }

    self_p->volume_start_block = 0;

    if (cache_raw_block(self_p, self_p->volume_start_block, CACHE_FOR_READ) != 0) {
        return (-1);
    }

    /* Check boot block signature. */
    if (self_p->cache.buffer.fbs.boot_sector_sig != BOOTSIG) {
        return (-1);
    }

    bpb_p = &self_p->cache.buffer.fbs.bpb;
    self_p->fat_count = bpb_p->fat_count;
    self_p->blocks_per_cluster = bpb_p->sectors_per_cluster;
    self_p->blocks_per_fat = bpb_p->sectors_per_fat;
    self_p->root_dir_entry_count = bpb_p->root_dir_entry_count;
    self_p->fat_start_block = (self_p->volume_start_block
                               + bpb_p->reserved_sector_count);
    self_p->root_dir_start_block =
        (self_p->fat_start_block
         + bpb_p->fat_count * bpb_p->sectors_per_fat);
    self_p->data_start_block =
        (self_p->root_dir_start_block
         + ((32 * bpb_p->root_dir_entry_count + 511) / 512));
    total_blocks = (bpb_p->total_sectors_small
                    ? bpb_p->total_sectors_small
                    : bpb_p->total_sectors_large);
    self_p->cluster_count =
        ((total_blocks - (self_p->data_start_block - self_p->volume_start_block))
         / bpb_p->sectors_per_cluster);

    return (0);
}

int fat16_unmount(struct fat16_t *self_p)
{
    assert(self_p != NULL);

    return (cache_flush(self_p));
}

int fat16_init(struct fat16_t *self_p,
               fat16_read_t read,
               fat16_write_t write)
{
    assert(self_p != NULL);
    assert(read != NULL);
    assert(write != NULL);

    /* Initialize datastructure.*/
    self_p->read = read;
    self_p->write = write;

    return (0);
}

int fat16_format(struct fat16_t *self_p)
{
    assert(self_p != NULL);

    struct fbs_t fbs;
    uint32_t volume_start_block;
    uint32_t fat_start_block;
    uint32_t blocks_per_fat;
    uint32_t root_dir_start_block;
    uint32_t root_dir_block_count;

    /* Initialize the cache. */
    self_p->cache.block_number = 0xffffffff;
    self_p->cache.dirty = 0;
    self_p->cache.mirror_block = 0;

    volume_start_block = 0;

    /* Initiate the boot sector. */
    fbs.jmp_to_boot_code[0] = 0xeb;
    fbs.jmp_to_boot_code[1] = 0x3c;
    fbs.jmp_to_boot_code[2] = 0x90;
    fs_strcpy(fbs.oem_name, "menguozi");

    fbs.bpb.bytes_per_sector = 512;
    fbs.bpb.sectors_per_cluster = 1;
    fbs.bpb.reserved_sector_count = 1;
    fbs.bpb.fat_count = 2;
    fbs.bpb.root_dir_entry_count = 512;
    fbs.bpb.total_sectors_small = 40257;
    fbs.bpb.media_type = 248;
    fbs.bpb.sectors_per_fat = 32;
    fbs.bpb.sectors_per_track = 63;
    fbs.bpb.head_count = 16;
    fbs.bpb.hiddden_sectors = 0;
    fbs.bpb.total_sectors_large = 0;

    fbs.drive_number = 128;
    fbs.reserved1 = 0;
    fbs.boot_signature = 41;
    fbs.volume_serial_number = 0xd7f3d6ef;
    fs_memcpy(fbs.volume_label, "MOSFAT16   ", sizeof(fbs.volume_label));
    fs_memcpy(fbs.file_system_type, "FAT16   ", sizeof(fbs.file_system_type));
    fs_memset(fbs.boot_code, 0, sizeof(fbs.boot_code));
    fbs.boot_sector_sig = BOOTSIG;

    fat_start_block = (volume_start_block + fbs.bpb.reserved_sector_count);
    blocks_per_fat = fbs.bpb.sectors_per_fat;
    root_dir_start_block = (fat_start_block
                            + fbs.bpb.fat_count * fbs.bpb.sectors_per_fat);
    root_dir_block_count = (fbs.bpb.root_dir_entry_count
                            / (512 / sizeof(struct dir_t)));

    if (write_volume_block(self_p,
                           volume_start_block,
                           &fbs) != 0) {
        return (-1);
    }

    if (format_fat_blocks(self_p,
                          fat_start_block,
                          fat_start_block + blocks_per_fat) != 0) {
        return (-1);
    }

    if (fbs.bpb.fat_count > 1) {
        if (format_fat_blocks(self_p,
                              fat_start_block + blocks_per_fat,
                              fat_start_block + 2 * blocks_per_fat) != 0) {
            return (-1);
        }
    }

    if (format_root_dir_blocks(self_p,
                               root_dir_start_block,
                               root_dir_start_block + root_dir_block_count) != 0) {
        return (-1);
    }

    return (0);
}

static int add_cluster(struct fat16_file_t *file_p)
{
    /* Start search after last cluster of file or at cluster two in FAT. */
    fat_t free_cluster = file_p->cur_cluster ? file_p->cur_cluster : 1;
    fat_t value;
    fat_t i;
    fat_t cluster_count = file_p->fat16_p->cluster_count;

    for (i = 0; ; i++) {
        /* Return no free clusters. */
        if (i >= cluster_count) {
            return (-1);
        }

        /* Fat has cluster_count + 2 entries. */
        if (free_cluster > cluster_count) {
            free_cluster = 1;
        }

        free_cluster++;

        if (fat_get(file_p->fat16_p, free_cluster, &value) != 0) {
            return (-1);
        }

        if (value == 0) {
            break;
        }
    }

    /* Mark cluster allocated. */
    if (fat_put(file_p->fat16_p, free_cluster, EOC16) != 0) {
        return (-1);
    }

    if (file_p->cur_cluster != 0) {
        /* Link cluster to chain. */
        if (fat_put(file_p->fat16_p, file_p->cur_cluster, free_cluster) != 0) {
            return (-1);
        }
    } else {
        /* first cluster of file so update directory entry. */
        file_p->flags |= F_FILE_DIR_DIRTY;
        file_p->first_cluster = free_cluster;
    }

    file_p->cur_cluster = free_cluster;

    return (0);
}

static int dir_init(struct dir_t *dir_p,
                    uint8_t *name_p,
                    uint8_t attributes)
{
    /* Initialize as empty file. */
    fs_memset(dir_p, 0, sizeof(*dir_p));
    fs_memcpy(dir_p->name, name_p, 11);

    dir_p->attributes = attributes;

    /* Set timestamps with user function or use default date/time. */
    /* if (dateTime) { */
    /*     dateTime(&dir_p->creationDate, &dir_p->creationTime); */
    /* } else { */
    dir_p->creation_date = DEFAULT_DATE;
    dir_p->creation_time = DEFAULT_TIME;
    /* } */

    dir_p->last_access_date = dir_p->creation_date;
    dir_p->last_write_date = dir_p->creation_date;
    dir_p->last_write_time = dir_p->creation_time;

    return (0);
}

static int get_dir_at_index_in_root(struct fat16_t *self_p,
                                    int16_t index,
                                    struct dir_t *dir_p)
{
    int dir_entries_per_block;
    int block;
    struct dir_t *d_p;

    dir_entries_per_block = (512 / sizeof(struct dir_t));
    block = (self_p->root_dir_start_block + (index / dir_entries_per_block));
    index = (index % dir_entries_per_block);

    if (!(d_p = cache_dir_entry(self_p,
                                block,
                                index,
                                CACHE_FOR_READ))) {
        return (-1);
    }

    /* Copy the directory entry to output variable. */
    *dir_p = *d_p;

    return (0);
}

/**
 * Try to open directory entry of given name within range of blocks.
 *
 * @param[in] name_p Name of the file or directory to look for.
 * @param[out] block_p Block index of the block containing name_p, or
 *                     the first free block if the name was not found.
 * @param[out] index_p Index within the block where the name matched,
 *                     or the first free index in the block.
 * @param[in] start_block Block to search from.
 * @param[in] end_block Block to search to.
 *
 * @return true(1) if a directory entry for given name was found,
 *         false(0) if the name was not found and the block_p and
 *         index_p holds the address of an empty slot, otherwise
 *         negative error code.
 */
static int dir_open_in_blocks(struct fat16_t *self_p,
                              const uint8_t *name_p,
                              int16_t *block_p,
                              int16_t *index_p,
                              int start_block,
                              int end_block)
{
    int16_t block;
    int16_t index;
    int dir_entries_per_block;
    struct dir_t *dir_p;

    dir_entries_per_block = (512 / sizeof(*dir_p));

    for (block = start_block; block < end_block; block++) {
        for (index = 0; index < dir_entries_per_block; index++) {
            if (!(dir_p = cache_dir_entry(self_p,
                                          block,
                                          index,
                                          CACHE_FOR_READ))) {
                return (-1);
            }

            if ((dir_p->name[0] == DIR_NAME_FREE)
                || (dir_p->name[0] == DIR_NAME_DELETED)) {
                /* Remember first empty slot */
                if (*index_p < 0) {
                    *block_p = block;
                    *index_p = index;
                }

                /* Done if no entries follow. */
                if (dir_p->name[0] == DIR_NAME_FREE) {
                    return (0);
                }
            } else if (fs_memcmp(name_p, dir_p->name, 11) == 0) {
                /* Existing file. */
                *block_p = block;
                *index_p = index;
                return (1);
            }
        }
    }

    return (0);
}

/**
 * Open directory with given name in the root folder.
 *
 * @param[out] index_p If true(1) is returned this is the index of an
 *                     existing file. If false(0) is returned this is
 *                     the index of the first empty directory
 *                     entry. Otherwise ignore this value.
 *
 * @return zero(0) or negative error code.
 */
static int dir_open_in_root(struct fat16_t *self_p,
                            const uint8_t *name_p,
                            int16_t *block_p,
                            int16_t *index_p)
{
    int root_dir_block_count;
    int dir_entries_per_block;

    *index_p = -1;         /* index of empty slot. */
    dir_entries_per_block = (512 / sizeof(struct dir_t));
    root_dir_block_count = (self_p->root_dir_entry_count / dir_entries_per_block);

    /* Search for the file in the root directory. */
    return (dir_open_in_blocks(self_p,
                               name_p,
                               block_p,
                               index_p,
                               self_p->root_dir_start_block,
                               self_p->root_dir_start_block + root_dir_block_count));
}

/**
 * Open directory with given name in given sub folder.
 *
 * @param[out] index_p If true(1) is returned this is the index of an
 *                     existing file. If false(0) is returned this is
 *                     the index of the first empty directory
 *                     entry. Otherwise ignore this value.
 *
 * @return zero(0) or negative error code.
 */
static int dir_open_in_subdir(struct fat16_t *self_p,
                              const uint8_t *name_p,
                              int16_t *block_p,
                              int16_t *index_p)
{
    fat_t cluster;
    int start_block;
    struct dir_t *dir_p;
    int res = -1;

    /* Cache the parent directory. */
    if (!(dir_p = cache_dir_entry(self_p,
                                  *block_p,
                                  *index_p,
                                  CACHE_FOR_WRITE))) {
        return (-1);
    }

    *index_p = -1;         /* index of empty slot. */

    /* Search for the file in the subdirectory. Iterate over clusters
       allocated by this folder. */
    cluster = dir_p->first_cluster_low;

    do {
        start_block = (self_p->data_start_block
                       + ((cluster - 2) * self_p->blocks_per_cluster));

        if ((res = dir_open_in_blocks(self_p,
                                      name_p,
                                      block_p,
                                      index_p,
                                      start_block,
                                      start_block + self_p->blocks_per_cluster)) < 0) {
            return (-1);
        }

        if (fat_get(self_p, cluster, &cluster) != 0) {
            return (-1);
        }
    } while (!is_end_of_cluster(cluster));

    return (res);
}

/**
 * Get name before first '/' or end of path.
 *
 * @param[in,out] path_p Path to parse. Is set to NULL when the
 *                       function returns if the name is the last part
 *                       of the path.
 *
 * @return zero(0) or negative error code.
 */
static int get_next_name(const char **path_p, uint8_t *name_p)
{
    const char *begin_p = *path_p;
    size_t size;

    /* Forward to end of name. */
    while ((**path_p != '/') && (**path_p != '\0')) {
        (*path_p)++;
    }

    size = (*path_p - begin_p);

    /* Check valid 8.3 file name. */
    if (make_83_name(begin_p, size, name_p) != 0) {
        return (-1);
    }

    if (**path_p == '/') {
        (*path_p)++;
    }

    if (**path_p == '\0') {
        *path_p = NULL;
    }

    return (0);
}

static int dir_open(struct fat16_t *self_p,
                    const char *path_p,
                    int oflag,
                    uint8_t attributes,
                    int16_t *block_p,
                    int16_t *index_p)
{
    uint8_t dname[11];              /* name formated for dir entry. */
    struct dir_t *dir_p = NULL;     /* pointer to cached dir entry. */
    int res;
    int depth = 0;
    int16_t parent_block = -1;
    int16_t parent_index = -1;

    do {
        if (get_next_name(&path_p, dname) != 0) {
            return (-1);
        }

        if (depth == 0) {
            /* Search for the name in the root folder. */
            res = dir_open_in_root(self_p, dname, block_p, index_p);
        } else {
            /* Search for the name in a subfolder. */
            res = dir_open_in_subdir(self_p, dname, block_p, index_p);
        }

        if (res < 0) {
            return (res);
        } else if (path_p != NULL) {
            if (res == 0) {
                /* Containing directory of next part of path was not found. */
                return (-1);
            } else {
                /* Potentially parent directory. */
                parent_block = *block_p;
                parent_index = *index_p;
            }
        }

        depth++;
    } while (path_p != NULL);

    /* The file or directory already exists. */
    if (res == 1) {
        /* Don't open existing file if O_CREAT and O_EXCL. */
        if ((oflag & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL)) {
            return (-1);
        }

        return (0);
    }

    /* Error if directory is full. */
    if (*index_p < 0) {
        return (-1);
    }

    /* Only create file if O_CREAT and O_WRITE. */
    if ((oflag & (O_CREAT | O_WRITE)) != (O_CREAT | O_WRITE)) {
        return (-1);
    }

    /* Add the newly created file or directory to the parent
       folder. */
    if (parent_index >= 0) {
        if (!(dir_p = cache_dir_entry(self_p,
                                      parent_block,
                                      parent_index,
                                      CACHE_FOR_WRITE))) {
            return (-1);
        }

        dir_p->file_size += sizeof(*dir_p);
    }

    if (!(dir_p = cache_dir_entry(self_p,
                                  *block_p,
                                  *index_p,
                                  CACHE_FOR_WRITE))) {
        return (-1);
    }

    /* Initialize as empty file. */
    dir_init(dir_p, dname, attributes);

    /* Force created directory entry to be written to storage
       device. */
    if (cache_flush(self_p) != 0) {
        return (-1);
    }

    return (0);
}

static int get_block(struct fat16_file_t *file_p,
                     uint16_t *block_offset_p)
{
    uint8_t blk_of_cluster;
    fat_t next;
    uint32_t lba;

    blk_of_cluster = block_of_cluster(file_p->fat16_p->blocks_per_cluster,
                                      file_p->cur_position);

    *block_offset_p = cache_data_offset(file_p->cur_position);

    if ((blk_of_cluster == 0) && (*block_offset_p == 0)) {
        /* Start of new cluster. */
        if (file_p->cur_cluster == 0) {
            if (file_p->first_cluster == 0) {
                /* Allocate first cluster of file. */
                if (add_cluster(file_p) != 0) {
                    return (FAT16_EOF);
                }
            } else {
                file_p->cur_cluster = file_p->first_cluster;
            }
        } else {
            if (fat_get(file_p->fat16_p, file_p->cur_cluster, &next) != 0) {
                return (FAT16_EOF);
            }

            if (is_end_of_cluster(next)) {
                /* Add cluster if at end of chain. */
                if (add_cluster(file_p) != 0) {
                    return (FAT16_EOF);
                }
            } else {
                file_p->cur_cluster = next;
            }
        }
    }

    lba = data_block_lba(file_p, blk_of_cluster);

    if ((*block_offset_p == 0) && (file_p->cur_position >= file_p->file_size)) {
        /* Start of new block don't need to read into cache. */
        if (cache_flush(file_p->fat16_p) != 0) {
            return (FAT16_EOF);
        }

        file_p->fat16_p->cache.block_number = lba;
        fs_memset(&file_p->fat16_p->cache.buffer,
               0,
               sizeof(file_p->fat16_p->cache.buffer));
        cache_set_dirty(&file_p->fat16_p->cache);
    } else {
        /* Rewrite part of block. */
        if (cache_raw_block(file_p->fat16_p, lba, CACHE_FOR_WRITE) != 0) {
            return (FAT16_EOF);
        }
    }

    return (0);
}

static int file_open(struct fat16_t *self_p,
                     struct fat16_file_t *file_p,
                     const char* path_p,
                     int oflag,
                     uint8_t attributes)
{
    int16_t block;
    int16_t index;
    struct dir_t* dir_p;

    /* Check for valid attributes. */
    if ((oflag & O_TRUNC) && !(oflag & O_WRITE)) {
        return (-1);
    }

    /* Find directory index of existing file or create a new one. */
    if (dir_open(self_p, path_p, oflag, attributes, &block, &index) != 0) {
        return (-1);
    }

    dir_p = cache_dir_entry(self_p, block, index, CACHE_FOR_READ);

    /* If bad file index or I/O error. */
    if (dir_p == NULL) {
        return (-1);
    }

    /* Error if volume label or subdirectory. */
    if ((dir_p->attributes & ~DIR_ATTR_ARCHIVE) != attributes) {
        return (-1);
    }

    /* Don't allow write or truncate if read-only. */
    if ((dir_p->attributes & DIR_ATTR_READ_ONLY)
        && (oflag & (O_WRITE | O_TRUNC))) {
        return (-1);
    }

    /* Initiate the file datastructure. */
    file_p->fat16_p = self_p;
    file_p->cur_cluster = 0;
    file_p->cur_position = 0;
    file_p->dir_entry_block = block;
    file_p->dir_entry_index = index;
    file_p->file_size = dir_p->file_size;
    file_p->first_cluster = dir_p->first_cluster_low;
    file_p->flags = oflag & (O_RDWR | O_SYNC | O_APPEND);

    if (oflag & O_TRUNC) {
        return (fat16_file_truncate(file_p, 0));
    }

    return (0);
}

int fat16_file_open(struct fat16_t *self_p,
                    struct fat16_file_t *file_p,
                    const char* path_p,
                    int oflag)
{
    assert(self_p != NULL);
    assert(file_p != NULL);
    assert(path_p != NULL);

    return (file_open(self_p, file_p, path_p, oflag, 0));
}

int fat16_file_close(struct fat16_file_t *file_p)
{
    assert(file_p != NULL);

    if (fat16_file_sync(file_p) != 0) {
        return (FAT16_EOF);
    }

    return (0);
}

ssize_t fat16_file_read(struct fat16_file_t *file_p,
                        void* buf_p,
                        size_t size)
{
    assert(file_p != NULL);
    assert((buf_p != NULL) || (size == 0));

    size_t left;
    uint8_t blk_of_cluster;
    uint16_t block_offset;
    uint8_t *src_p, *dst_p;
    size_t n;

    /* Error if not open for read. */
    if (!(file_p->flags & O_READ)) {
        return (FAT16_EOF);
    }

    /* Don't read beyond end of file. */
    if ((file_p->cur_position + size) > file_p->file_size) {
        size = file_p->file_size - file_p->cur_position;
    }

    /* Bytes left to read in loop. */
    left = size;
    dst_p = buf_p;

    while (left > 0) {
        blk_of_cluster = block_of_cluster(file_p->fat16_p->blocks_per_cluster,
                                          file_p->cur_position);
        block_offset = cache_data_offset(file_p->cur_position);

        if (blk_of_cluster == 0 && block_offset == 0) {
            /* Start next cluster. */
            if (file_p->cur_cluster == 0) {
                file_p->cur_cluster = file_p->first_cluster;
            } else {
                if (fat_get(file_p->fat16_p, file_p->cur_cluster, &file_p->cur_cluster) != 0) {
                    return (FAT16_EOF);
                }
            }

            /* Return error if bad cluster chain. */
            if (file_p->cur_cluster < 2 || is_end_of_cluster(file_p->cur_cluster)) {
                return (FAT16_EOF);
            }
        }

        /* Cache data block. */
        if (cache_raw_block(file_p->fat16_p,
                            data_block_lba(file_p, blk_of_cluster),
                            CACHE_FOR_READ) != 0) {
            return (FAT16_EOF);
        }

        /* Location of data in cache. */
        src_p = file_p->fat16_p->cache.buffer.data + block_offset;

        /* Max number of byte available in block. */
        n = 512 - block_offset;

        /* Lesser of available and amount to read. */
        if (n > left) {
            n = left;
        }

        /* Copy data to caller. */
        fs_memcpy(dst_p, src_p, n);

        file_p->cur_position += n;
        dst_p += n;
        left -= n;
    }

    return (size);
}

ssize_t fat16_file_write(struct fat16_file_t *file_p,
                         const void *src_p,
                         size_t size)
{
    assert(file_p != NULL);
    assert((src_p != NULL) || (size == 0));

    size_t left = size;
    uint16_t block_offset;
    uint8_t* dst_p;
    size_t n;
    const char *csrc_p;

    csrc_p = src_p;

    /* Error if file is not open for write. */
    if (!(file_p->flags & O_WRITE)) {
        return (FAT16_EOF);
    }

    /* Go to end of file if O_APPEND. */
    if ((file_p->flags & O_APPEND)
        && (file_p->cur_position != file_p->file_size)) {
        if (fat16_file_seek(file_p, 0, FAT16_SEEK_END) != 0) {
            return (FAT16_EOF);
        }
    }

    while (left > 0) {
        if (get_block(file_p, &block_offset) != 0) {
            return (FAT16_EOF);
        }

        dst_p = file_p->fat16_p->cache.buffer.data + block_offset;

        /* Max space in block. */
        n = 512 - block_offset;

        /* Lesser free space in current block than amount to write. */
        if (n > left) {
            n = left;
        }

        /* Copy data to cache. */
        fs_memcpy(dst_p, csrc_p, n);

        file_p->cur_position += n;
        left -= n;
        csrc_p += n;
    }

    if (file_p->cur_position > file_p->file_size) {
        /* Update file_size and insure sync will update dir entry. */
        file_p->file_size = file_p->cur_position;
        file_p->flags |= F_FILE_DIR_DIRTY;
    }

    if (file_p->flags & O_SYNC) {
        if (fat16_file_sync(file_p) != 0) {
            return (FAT16_EOF);
        }
    }

    return (size);
}

int fat16_file_seek(struct fat16_file_t *file_p,
                    int pos,
                    int whence)
{
    assert(file_p != NULL);

    fat_t n;
    uint8_t blocks_per_cluster = file_p->fat16_p->blocks_per_cluster;

    if (whence == FAT16_SEEK_CUR) {
        pos += file_p->cur_position;
    } else if (whence == FAT16_SEEK_END) {
        pos += file_p->file_size;
    } else if (whence != FAT16_SEEK_SET) {
        return (-1);
    }

    /* Error if file not open or seek past end of file. */
    if (pos > file_p->file_size) {
        return (-1);
    }

    /* Error if seek before beginning of file. */
    if (pos < 0) {
        return (-1);
    }

    if (pos == 0) {
        /* Set position to start of file. */
        file_p->cur_cluster = 0;
        file_p->cur_position = 0;

        return (0);
    }

    n = ((pos - 1) >> 9) / blocks_per_cluster;

    if (pos < file_p->cur_position || file_p->cur_position == 0) {
        /* Must follow chain from first cluster. */
        file_p->cur_cluster = file_p->first_cluster;
    } else {
        /* Advance from cur_position. */
        n -= ((file_p->cur_position - 1) >> 9) / blocks_per_cluster;
    }

    while (n--) {
        if (fat_get(file_p->fat16_p,
                    file_p->cur_cluster,
                    &file_p->cur_cluster) != 0) {
            return (-1);
        }
    }

    file_p->cur_position = pos;

    return (0);
}

ssize_t fat16_file_tell(struct fat16_file_t *file_p)
{
    assert(file_p != NULL);

    return (file_p->cur_position);
}

int fat16_file_truncate(struct fat16_file_t *file_p,
                        size_t size)
{
    assert(file_p != NULL);

    uint32_t new_pos;
    fat_t to_free;
    size_t n;
    char zero;

    /* Error if file is not open for write. */
    if (!(file_p->flags & O_WRITE)) {
        return (-1);
    }

    if (size > file_p->file_size) {
        if (fat16_file_seek(file_p, 0, FAT16_SEEK_END) != 0) {
            return (-1);
        }

        n = (size - file_p->file_size);
        zero = '\0';

        /* Write one '\0' byte at a time for simplicity. */
        while (n--) {
            if (fat16_file_write(file_p, &zero, 1) != 1) {
                return (-1);
            }
        }

        return (0);
    }

    /* Filesize and size are zero - nothing to do. */
    if (file_p->file_size == 0) {
        return (0);
    }

    new_pos = (file_p->cur_position > size
               ? size
               : file_p->cur_position);

    if (size == 0) {
        /* Free all clusters. */
        if (free_chain(file_p->fat16_p, file_p->first_cluster) != 0) {
            return (-1);
        }

        file_p->cur_cluster = file_p->first_cluster = 0;
    } else {
        if (fat16_file_seek(file_p, size, FAT16_SEEK_SET) != 0) {
            return (-1);
        }

        if (fat_get(file_p->fat16_p, file_p->cur_cluster, &to_free) != 0) {
            return (-1);
        }

        if (!is_end_of_cluster(to_free)) {
            /* Free extra clusters. */
            if (!fat_put(file_p->fat16_p, file_p->cur_cluster, EOC16)) {
                return (-1);
            }

            if (!free_chain(file_p->fat16_p, to_free)) {
                return (-1);
            }
        }
    }

    file_p->file_size = size;
    file_p->flags |= F_FILE_DIR_DIRTY;

    if (fat16_file_sync(file_p) != 0) {
        return (-1);
    }

    return (fat16_file_seek(file_p, new_pos, FAT16_SEEK_SET));
}

ssize_t fat16_file_size(struct fat16_file_t *file_p)
{
    assert(file_p != NULL);

    return (file_p->file_size);
}

int fat16_file_sync(struct fat16_file_t *file_p)
{
    assert(file_p != NULL);

    struct dir_t* dir_p;

    if (file_p->flags & F_FILE_DIR_DIRTY) {
        /* Cache directory entry. */
        dir_p = cache_dir_entry(file_p->fat16_p,
                                file_p->dir_entry_block,
                                file_p->dir_entry_index,
                                CACHE_FOR_WRITE);

        if (!dir_p) {
            return (-1);
        }

        /* Update file size and first cluster. */
        dir_p->file_size = file_p->file_size;
        dir_p->first_cluster_low = file_p->first_cluster;

        /* Set modify time if user supplied a callback date/time function. */
        /* if (dateTime) { */
        /*     dateTime(&dir_p->last_write_date, &dir_p->last_write_time); */
        /*     dir_p->last_access_date = dir_p->last_write_date; */
        /* } */

        file_p->flags &= ~F_FILE_DIR_DIRTY;
    }

    return (cache_flush(file_p->fat16_p));
}

int fat16_dir_open(struct fat16_t *self_p,
                   struct fat16_dir_t *dir_p,
                   const char *path_p,
                   int oflag)
{
    assert(dir_p != NULL);
    assert(path_p != NULL);

    struct fat16_file_t *file_p = &dir_p->file;
    uint8_t dname[11];
    struct dir_t dir;

    /* Root directory is special. */
    if ((fs_strcmp(path_p, ".") == 0) && (oflag & O_READ)) {
        /* First index in root folder. */
        dir_p->root_index = 0;
        file_p->fat16_p = self_p;

        return (0);
    } else if (file_open(self_p, file_p, path_p, oflag, DIR_ATTR_DIRECTORY) != 0) {
        return (-1);
    }

    dir_p->root_index = -1;

    /* Add '.' and '..' when a new directory is created. */
    if ((oflag & (O_CREAT | O_WRITE)) == (O_CREAT | O_WRITE)) {
        /* Add '.'. */
        fs_memset(dname, ' ', sizeof(dname));
        dname[0] = '.';
        dir_init(&dir, dname, DIR_ATTR_DIRECTORY);

        if (fat16_file_write(file_p, &dir, sizeof(dir)) != sizeof(dir)) {
            return (-1);
        }

        /* Add '..'. */
        dname[1] = '.';
        dir_init(&dir, dname, DIR_ATTR_DIRECTORY);

        if (fat16_file_write(file_p, &dir, sizeof(dir)) != sizeof(dir)) {
            return (-1);
        }
    }

    return (0);
}

int fat16_dir_close(struct fat16_dir_t *dir_p)
{
    assert(dir_p != NULL);

    if (dir_p->root_index == -1) {
        if (fat16_file_sync(&dir_p->file) != 0) {
            return (FAT16_EOF);
        }
    }

    return (0);
}

int fat16_dir_read(struct fat16_dir_t *dir_p,
                   struct fat16_dir_entry_t *entry_p)
{
    assert(dir_p != NULL);
    assert(entry_p != NULL);

    struct fat16_file_t *file_p = &dir_p->file;
    struct dir_t dir;
    union fat16_date_t date;
    union fat16_time_t time;

    /* Search for the next entry. */
    while (1) {
        /* Break if there are no more entries. */
        if (dir_p->root_index != -1) {
            if (get_dir_at_index_in_root(dir_p->file.fat16_p,
                                         dir_p->root_index,
                                         &dir) != 0) {
                return (-1);
            }

            dir_p->root_index++;
        } else {
            if (fat16_file_read(file_p, &dir, sizeof(dir)) != sizeof(dir)) {
                return (0);
            }
        }

        /* No more entries in the list. */
        if (dir.name[0] == DIR_NAME_FREE) {
            return (0);
        }

        /* Skip volume id. */
        if (dir.attributes & DIR_ATTR_VOLUME_ID) {
            continue;
        }

        if (dir.name[0] != DIR_NAME_DELETED) {
            break;
        }
    }

    /* Copy information from directory entry to user supplied
       structure. */
    make_name_from_83(entry_p->name, (char *)dir.name);

    /* Set type and size. */
    entry_p->is_dir = dir_is_subdir(&dir);
    entry_p->size = dir.file_size;

    /* Set the modification date. */
    date = (union fat16_date_t)dir.last_write_date;
    time = (union fat16_time_t)dir.last_write_time;
    entry_p->latest_mod_date.year = 1980 + date.bits.year;
    entry_p->latest_mod_date.month = date.bits.month;
    entry_p->latest_mod_date.day = date.bits.day;
    entry_p->latest_mod_date.hour = time.bits.hours;
    entry_p->latest_mod_date.minute = time.bits.minutes;
    entry_p->latest_mod_date.second = (2 * time.bits.seconds);

    return (1);
}

int fat16_stat(struct fat16_t *self_p,
               const char *path_p,
               struct fat16_stat_t *stat_p)
{
    struct fat16_file_t file;
    struct fat16_dir_t dir;

    /* Try to open given path as a file. */
    if (fat16_file_open(self_p, &file, path_p, O_READ) == 0) {
        stat_p->size = file.file_size;
        stat_p->is_dir = 0;

        return (fat16_file_close(&file));
    }

    /* Try to open given path as a directory. */
    if (fat16_dir_open(self_p, &dir, path_p, O_READ) == 0) {
        stat_p->size = dir.file.file_size;
        stat_p->is_dir = 1;

        return (fat16_dir_close(&dir));
    }

    return (-1);
}

PRIVATE void init_fs();
PRIVATE void mkfs();

/*****************************************************************************
 *                                task_fs
 *****************************************************************************/
/**
 * <Ring 1> The main loop of TASK FS.
 * 
 *****************************************************************************/
PUBLIC void task_fs()
{
	printl("Task FS begins.\n");

	/*while (1) {
		send_recv(RECEIVE, ANY, &fs_msg);

		int src = fs_msg.source;
		pcaller = &proc_table[src];

		switch (fs_msg.type) {
		case OPEN:
			fs_msg.FD = do_open();
			break;*/
		/*case CLOSE:
			fs_msg.RETVAL = do_close();
			break;*/
		/* case READ: */
		/* case WRITE: */
		/* 	fs_msg.CNT = do_rdwt(); */
		/* 	break; */
		/* case LSEEK: */
		/* 	fs_msg.OFFSET = do_lseek(); */
		/* 	break; */
		/* case UNLINK: */
		/* 	fs_msg.RETVAL = do_unlink(); */
		/* 	break; */
		/* case RESUME_PROC: */
		/* 	src = fs_msg.PROC_NR; */
		/* 	break; */
		/* case FORK: */
		/* 	fs_msg.RETVAL = fs_fork(); */
		/* 	break; */
		/* case EXIT: */
		/* 	fs_msg.RETVAL = fs_exit(); */
		/* 	break; */
		/* case STAT: */
		/* 	fs_msg.RETVAL = do_stat(); */
		/* 	break; */
		/*default:
			dump_msg("FS::unknown message:", &fs_msg);
			assert(0);
			break;
		}*/

		/* reply */
		/*fs_msg.type = SYSCALL_RET;
		send_recv(SEND, src, &fs_msg);
	}*/

	init_fs();
}

/*****************************************************************************
 *                                init_fs
 *****************************************************************************/
/**
 * <Ring 1> Do some preparation.
 * 
 *****************************************************************************/
PRIVATE void init_fs()
{
	/* open the device: hard disk */
	MESSAGE driver_msg;
	driver_msg.type = DEV_OPEN;
	driver_msg.DEVICE = MINOR(ROOT_DEV);
	assert(dd_map[MAJOR(ROOT_DEV)].driver_nr != INVALID_DRIVER);
	send_recv(BOTH, dd_map[MAJOR(ROOT_DEV)].driver_nr, &driver_msg);

	mkfs();
}

/*****************************************************************************
 *                                mkfs
 *****************************************************************************/
/**
 * <Ring 1> Make a available Orange'S FS in the disk. It will
 *          - Write a super block to sector 1.
 *          - Create three special files: dev_tty0, dev_tty1, dev_tty2
 *          - Create the inode map
 *          - Create the sector map
 *          - Create the inodes of the files
 *          - Create `/', the root directory
 *****************************************************************************/
PRIVATE void mkfs()
{
	MESSAGE driver_msg;

	/* get the geometry of ROOTDEV */
	struct part_info geo;
	driver_msg.type		= DEV_IOCTL;
	driver_msg.DEVICE	= MINOR(ROOT_DEV);
	driver_msg.REQUEST	= DIOCTL_GET_GEO;
	driver_msg.BUF		= &geo;
	driver_msg.PROC_NR	= TASK_FS;
	assert(dd_map[MAJOR(ROOT_DEV)].driver_nr != INVALID_DRIVER);
	send_recv(BOTH, dd_map[MAJOR(ROOT_DEV)].driver_nr, &driver_msg);

	printl("\n\n");
	printl("< Fat16_FS > dev size: 0x%x sectors\n", geo.size);

	fat16_init(&fat16,
               read,
               write);

	fat16_format(&fat16);

	fat16_mount(&fat16);

	printl("< Fat16_FS > volume_start_block: #%d sector\n", fat16.volume_start_block);
	printl("< Fat16_FS > fat_start_block: #%d sector\n", fat16.fat_start_block);
	printl("< Fat16_FS > root_dir_start_block: #%d sector\n", fat16.root_dir_start_block);
	printl("< Fat16_FS > data_start_block: #%d sector\n", fat16.data_start_block);
}

/*****************************************************************************
 *                                rw_sector
 *****************************************************************************/
/**
 * <Ring 1> R/W a sector via messaging with the corresponding driver.
 * 
 * @param io_type  DEV_READ or DEV_WRITE
 * @param dev      device nr
 * @param pos      Byte offset from/to where to r/w.
 * @param bytes    r/w count in bytes.
 * @param proc_nr  To whom the buffer belongs.
 * @param buf      r/w buffer.
 * 
 * @return Zero if success.
 *****************************************************************************/
PUBLIC int rw_sector(int io_type, int dev, u64 pos, int bytes, int proc_nr,
		     void* buf)
{
	MESSAGE driver_msg;

	driver_msg.type		= io_type;
	driver_msg.DEVICE	= MINOR(dev);
	driver_msg.POSITION	= pos;
	driver_msg.BUF		= buf;
	driver_msg.CNT		= bytes;
	driver_msg.PROC_NR	= proc_nr;
	assert(dd_map[MAJOR(dev)].driver_nr != INVALID_DRIVER);
	send_recv(BOTH, dd_map[MAJOR(dev)].driver_nr, &driver_msg);

	return 0;
}

