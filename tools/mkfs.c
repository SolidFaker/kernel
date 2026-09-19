#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs/myfs.h"
#include "common.h"

#define SECTOR_SIZE 512
#define ENTRY_NUM   64

struct myfs_struct header;
struct myfs_entry entries[ENTRY_NUM];

int main(int argc, char **argv)
{
    int entry_num = (argc-1)/2;

    if (entry_num > ENTRY_NUM) {
        printf("Error: too many entries (max %d)\n", ENTRY_NUM);
        return 1;
    }

    // header
    header.magic = MYFS_MAGIC;
    header.entry_num = entry_num;

    printf("%d", entry_num);
    int sector_count = 0;
    printf("size of entry %d\n", (int)sizeof(struct myfs_entry));
    // put entries at tenth sector
    sector_count += 10;
    int entry_table_length = (sizeof(struct myfs_entry) * ENTRY_NUM) / SECTOR_SIZE + 1;
    sector_count += entry_table_length;

    // root directory
    entries[0].entry_type = MYFS_DIR;
    strcpy(entries[0].entry_name, "root");

    int i;
    for(i = 1; i < entry_num; i++)
    {
        strcpy(entries[i].entry_name, argv[i*2+2]);
        entries[i].data_sector = sector_count;
        FILE *stream = fopen(argv[i*2+1], "rb");
        if(stream == 0)
        {
            printf("Error: file not found: %s\n", argv[i*2+1]);
            return 1;
        }
        fseek(stream, 0, SEEK_END);
        entries[i].data_size = ftell(stream);
        entries[i].data_count = ftell(stream) / SECTOR_SIZE + 1;
        entries[i].entry_type = MYFS_FILE;
        sector_count += entries[i].data_count;
        printf("writing file %s->%s at sector: %ld count: %ld\n",
                argv[i*2+1], argv[i*2+2], entries[i].data_sector, entries[i].data_count);
        fclose(stream);
    }

    FILE *wstream = fopen("./kernel.img", "wb");

    // write header
    fwrite(&header, sizeof(struct myfs_struct), 1, wstream);
    // write entries
    fwrite(entries, sizeof(struct myfs_entry), ENTRY_NUM, wstream);

    // entries[0] is the root directory and holds no file data
    for(i = 1; i < entry_num; i++)
    {
        FILE *stream = fopen(argv[i*2+1], "rb");
        unsigned char *buf = (unsigned char *)malloc(entries[i].data_count * SECTOR_SIZE);
        fread(buf, 1, entries[i].data_count * SECTOR_SIZE, stream);
        // the image starts 10 sectors before the disk layout
        fseek(wstream, entries[i].data_sector * SECTOR_SIZE - 10 * SECTOR_SIZE, SEEK_SET);
        fwrite(buf, 1, entries[i].data_count * SECTOR_SIZE, wstream);
        fclose(stream);
        free(buf);
    }

    fclose(wstream);

    printf("writing finished\n");
    return 0;
}
